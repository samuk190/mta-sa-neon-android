#include "PlayerPedPort.h"
#include "GameEngine.h"
#include "../shared/DebugFlags.h"
#include "../shared/LuaVMStub.h"
#include "../shared/ServerWorld.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <chrono>
#include <cstring>
#include <iterator>
#include <unordered_map>

#ifdef __ANDROID__
#include <dlfcn.h>
#include <sys/mman.h>
#include <unistd.h>
#include "../hook/MemoryPatcher.h"
#endif

namespace WorldRPG::Game {

namespace {

// libGTASA 2.10 arm64, confirmed from the binary rather than a guessed vtable.
//
// CPed::operator new ignores its size and returns a pool slot of stride 0x9c8.
// SetupPlayerPed passes 0x998. The same call is used here.
//
// CPlayerPed::CPlayerPed(int, bool) stores pPlayerData at ped+0x540 as
// &CWorld::Players[index] + 8 and calls CPlayerPedData::AllocateData on that
// slot. It does not write Players[index].pPed. SetupPlayerPed is the function
// that does (`str` of the new ped at the slot base) and, only for index 1,
// writes ped+0x720. FindPlayerPed(0) reads Players[0] unless
// CHID::currentInstanceIndex is 1, in which case it reads Players[1].pPed,
// which this constructor leaves untouched.
//
// Index 1 therefore does not replace the local ped. A private CPlayerPedData
// is not allocated, and SetupPlayerPed is not called.
constexpr unsigned long kPlayerPedNewSize = 0x998;
constexpr int kRemotePlayerSlot = 1;
constexpr int kVisibleStandInModel = 7;
constexpr int kPedStateIdle = 1;
constexpr unsigned char kCreatedByScript = 2;

// libGTASA CMatrix / CMatrixSAInterface: right, forward, up, pos.
// Not an RwMatrix (that one stores up in the second row and at in the third).
struct Matrix {
    float rightX, rightY, rightZ, rightPad;
    float frontX, frontY, frontZ, frontPad;
    float upX, upY, upZ, upPad;
    float posX, posY, posZ, posPad;
};

struct PortFns {
    bool ready = false;
    void* (*pedNew)(unsigned long) = nullptr;
    void (*playerCtor)(void*, int, bool) = nullptr;
    void (*setModel)(void*, unsigned) = nullptr;
    void (*setCreatedBy)(void*, unsigned char) = nullptr;
    void (*worldAdd)(void*) = nullptr;
    void (*worldRemove)(void*) = nullptr;
    void (*setMatrix)(void*, const Matrix*) = nullptr;
    void (*setState)(void*, int) = nullptr;
};

PortFns& Fns() {
    static PortFns api;
    if (api.ready) return api;
    api.ready = true;
#ifdef __ANDROID__
    void* handle = dlopen("libGTASA.so", RTLD_NOLOAD | RTLD_GLOBAL);
    if (!handle) return api;
    api.pedNew = reinterpret_cast<void* (*)(unsigned long)>(dlsym(handle, "_ZN4CPednwEm"));
    api.playerCtor = reinterpret_cast<void (*)(void*, int, bool)>(
        dlsym(handle, "_ZN10CPlayerPedC1Eib"));
    if (!api.playerCtor) {
        api.playerCtor = reinterpret_cast<void (*)(void*, int, bool)>(
            dlsym(handle, "_ZN10CPlayerPedC2Eib"));
    }
    api.setModel = reinterpret_cast<void (*)(void*, unsigned)>(
        dlsym(handle, "_ZN4CPed13SetModelIndexEj"));
    api.setCreatedBy = reinterpret_cast<void (*)(void*, unsigned char)>(
        dlsym(handle, "_ZN4CPed16SetCharCreatedByEh"));
    api.worldAdd = reinterpret_cast<void (*)(void*)>(dlsym(handle, "_ZN6CWorld3AddEP7CEntity"));
    api.worldRemove = reinterpret_cast<void (*)(void*)>(dlsym(handle, "_ZN6CWorld6RemoveEP7CEntity"));
    api.setMatrix = reinterpret_cast<void (*)(void*, const Matrix*)>(
        dlsym(handle, "_ZN10CPlaceable9SetMatrixERK7CMatrix"));
    api.setState = reinterpret_cast<void (*)(void*, int)>(
        dlsym(handle, "_ZN4CPed11SetPedStateE9ePedState"));
    if (!api.pedNew || !api.playerCtor || !api.worldAdd) {
        ENGINE_LOGI("CPlayerPed port missing new=%p ctor=%p world=%p",
            reinterpret_cast<void*>(api.pedNew),
            reinterpret_cast<void*>(api.playerCtor),
            reinterpret_cast<void*>(api.worldAdd));
    }
#endif
    return api;
}

// Desktop CMultiplayerSA.cpp, "ALLOW more than 8 players" (0x60D64D): the
// group-creation block in CPlayerPed::CPlayerPed(int, bool) is skipped with
// NOP + JMP. On libGTASA 2.10 arm64 the same branch is at ctor + 0x9c and is
// `tbnz w20, #0` (bool true skips the block). Replacing it with an
// unconditional `b` to the same target gives the desktop behaviour.
// Applied lazily on the first remote ped, once per process.
bool EnsurePlayerPedGroupPatch() {
#ifdef __ANDROID__
    static bool done = false;
    static bool ok = false;
    if (done) return ok;
    done = true;

    constexpr std::uint32_t kOriginalWord = 0x370005d4u;  // tbnz w20, #0, #+0xb8
    constexpr std::uint32_t kPatchedWord = 0x1400002eu;   // b #+0xb8
    constexpr std::uintptr_t kSiteOffset = 0x9c;

    void* handle = dlopen("libGTASA.so", RTLD_NOLOAD | RTLD_GLOBAL);
    if (!handle) {
        ENGINE_LOGE("CPlayerPed group patch: libGTASA.so not loaded");
        return false;
    }
    void* ctor = dlsym(handle, "_ZN10CPlayerPedC2Eib");
    if (!ctor) ctor = dlsym(handle, "_ZN10CPlayerPedC1Eib");
    if (!ctor) {
        ENGINE_LOGE("CPlayerPed group patch: constructor symbol not found");
        return false;
    }

    auto* site = static_cast<unsigned char*>(ctor) + kSiteOffset;
    std::uint32_t word = 0;
    std::memcpy(&word, site, sizeof(word));
    if (word == kPatchedWord) {
        ok = true;
        return true;
    }
    if (word != kOriginalWord) {
        ENGINE_LOGE("CPlayerPed group patch: unexpected word 0x%08x at ctor+0x9c", word);
        return false;
    }

    // The 4-byte word is 4-byte aligned, so it never straddles a page.
    const long pageSizeLong = sysconf(_SC_PAGESIZE);
    if (pageSizeLong <= 0) {
        ENGINE_LOGE("CPlayerPed group patch: sysconf(_SC_PAGESIZE) failed");
        return false;
    }
    const auto pageSize = static_cast<std::uintptr_t>(pageSizeLong);
    const auto addr = reinterpret_cast<std::uintptr_t>(site);
    void* page = reinterpret_cast<void*>(addr & ~(pageSize - 1));

    if (mprotect(page, pageSize, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
        ENGINE_LOGE("CPlayerPed group patch: mprotect (RWX) failed");
        return false;
    }
    std::memcpy(site, &kPatchedWord, sizeof(kPatchedWord));
    if (mprotect(page, pageSize, PROT_READ | PROT_EXEC) != 0) {
        ENGINE_LOGE("CPlayerPed group patch: mprotect (RX restore) failed");
        return false;
    }
    __builtin___clear_cache(reinterpret_cast<char*>(site), reinterpret_cast<char*>(site) + 4);

    ENGINE_LOGI("CPlayerPed group patch applied (desktop 0x60D64D)");
    ok = true;
    return true;
#else
    return true;
#endif
}

int VisibleModel(int model) {
    // Model 0 is the shared CJ clump. Skin 7 is a complete body. RebuildPlayer
    // is never called, so the local player's clothes stay his.
    if (model <= 0 || model > 312) return kVisibleStandInModel;
    return model;
}

}  // namespace

void SetRemotePedHeading(void* ped, float heading) {
    if (!ped || !std::isfinite(heading)) return;
    auto* raw = static_cast<unsigned char*>(ped);
    auto* matrix = *reinterpret_cast<unsigned char**>(raw + 0x18);
    const auto matrixAddr = reinterpret_cast<uintptr_t>(matrix);
    if (matrix && (matrixAddr & 3u) == 0 && matrixAddr > 0x10000u) {
        // CMatrix::SetRotateZOnly (libGTASA 2.10, 0x537958) writes these rows and
        // does not touch position at +0x30. Desktop CMatrix::SetRotation with only
        // Z set (Shared/sdk/CMatrix.h 253-279) is the same orientation:
        // right=(cos, sin, 0), forward=(-sin, cos, 0), up=(0, 0, 1).
        const float s = std::sin(heading);
        const float c = std::cos(heading);
        auto* right = reinterpret_cast<float*>(matrix);
        auto* front = reinterpret_cast<float*>(matrix + 0x10);
        auto* up = reinterpret_cast<float*>(matrix + 0x20);
        right[0] = c;
        right[1] = s;
        right[2] = 0.f;
        front[0] = -s;
        front[1] = c;
        front[2] = 0.f;
        up[0] = 0.f;
        up[1] = 0.f;
        up[2] = 1.f;
    }
    *reinterpret_cast<float*>(raw + 0x14) = heading;
    // CPedSA::SetCurrentRotation / SetTargetRotation (CPedSA.h 435-436).
    // CPed::UpdatePosition (0x597b3c) passes +0x6C4 to SetRotateZOnly.
    // CPed::TurnBody (0x59dd58) stores the desired heading at +0x6C8 and steps +0x6C4 toward it.
    // CClientPed::SetCurrentRotation(rotation, true) writes both (CClientPed.cpp 5302-5307).
    *reinterpret_cast<float*>(raw + 0x6C4) = heading;
    *reinterpret_cast<float*>(raw + 0x6C8) = heading;
}

void PlaceRemotePlayerPed(void* ped, float x, float y, float z, float heading) {
    if (!ped) return;
    auto& api = Fns();
    if (api.setMatrix && std::isfinite(heading)) {
        // up.z at +0x28 is 1, so CPlaceable::SetMatrix (0x4ebf5c) keeps a simple
        // transform when the ped has no matrix yet: position, and heading
        // atan2(-forward.x, forward.y) from the second row. A pitched matrix
        // (forward along Z) fails that test and is stored whole.
        Matrix matrix{};
        const float s = std::sin(heading);
        const float c = std::cos(heading);
        matrix.rightX = c;
        matrix.rightY = s;
        matrix.frontX = -s;
        matrix.frontY = c;
        matrix.upZ = 1.f;
        matrix.posX = x;
        matrix.posY = y;
        matrix.posZ = z;
        api.setMatrix(ped, &matrix);
    }
    // CEntitySA::SetMatrix also copies the position into CPlaceable's
    // transform. CPlaceable::SetMatrix only does that when the ped has no
    // matrix yet, so write it here as well.
    auto* raw = static_cast<unsigned char*>(ped);
    *reinterpret_cast<float*>(raw + 0x08) = x;
    *reinterpret_cast<float*>(raw + 0x0C) = y;
    *reinterpret_cast<float*>(raw + 0x10) = z;
    SetRemotePedHeading(ped, heading);
    auto* speed = reinterpret_cast<float*>(raw + 0x68);
    speed[0] = speed[1] = speed[2] = 0.f;
}

// CPlayerPedSA::CPlayerPedSA for a remote ped (CPlayerPedSA.cpp 66-90): a private CPlayerPedDataSAInterface copied from
// the local player's, the local CWanted kept (m_Wanted = pLocalWanted), and a private CPedClothesDesc (operator new +
// CPedClothesDesc::Initialise). The phone used to leave every remote body on the shared &CWorld::Players[1]+8, so a
// clothes rebuild on one body changed the others; that is why model 0 was replaced by skin 7.
// libGTASA 2.10 arm64 layout: CPlayerPedData is embedded at CPlayerInfo+8 (CPlayerInfo ctor 0x4ed54c writes the same
// fields 8 higher than CPlayerPedData ctor 0x4f10cc) and ends at 0xd8 (the ctor's last store is
// stp xzr, xzr, [x19, #0xc8]). +0x00 is CWanted* and +0x08 CPedClothesDesc* (AllocateData 0x4f12a4: new 0x3a0 stored at
// [x19], new 0x78 + CPedClothesDesc C1 stored at [x19, #0x8], then Initialise). ped+0x540 is pPlayerData (the
// constructor note above). m_bRenderWeapon and m_fTimeCanRun keep the local player's values: their arm64 offsets are
// not mapped. Allocated and freed with the operator new / delete libGTASA links (dlsym through its handle); the game
// never frees this block, because CPlayerPed::~CPlayerPed is not run (DestroyRemotePlayerPed).
constexpr size_t kPlayerPedDataBytes = 0xd8;
constexpr size_t kPedClothesDescBytes = 0x78;
struct PrivateData {
    void* data = nullptr;
    void* clothes = nullptr;
};
std::unordered_map<void*, PrivateData> g_privateData;  // game thread only

struct DataFns {
    bool looked = false;
    void* (*opNew)(unsigned long) = nullptr;
    void (*opDelete)(void*) = nullptr;
    void (*clothesCtor)(void*) = nullptr;
    void (*clothesInit)(void*) = nullptr;
    void* (*findPlayerPed)(int) = nullptr;
};
DataFns& DataApi() {
    static DataFns api;
    if (api.looked) return api;
    api.looked = true;
    void* handle = dlopen("libGTASA.so", RTLD_NOLOAD | RTLD_GLOBAL);
    if (!handle) return api;
    api.opNew = reinterpret_cast<void* (*)(unsigned long)>(dlsym(handle, "_Znwm"));
    api.opDelete = reinterpret_cast<void (*)(void*)>(dlsym(handle, "_ZdlPv"));
    api.clothesCtor = reinterpret_cast<void (*)(void*)>(dlsym(handle, "_ZN15CPedClothesDescC1Ev"));
    api.clothesInit = reinterpret_cast<void (*)(void*)>(dlsym(handle, "_ZN15CPedClothesDesc10InitialiseEv"));
    api.findPlayerPed = reinterpret_cast<void* (*)(int)>(dlsym(handle, "_Z13FindPlayerPedi"));
    if (!api.opNew || !api.opDelete || !api.clothesCtor || !api.clothesInit || !api.findPlayerPed)
        ENGINE_LOGE("DIAG private player data: new=%p delete=%p CPedClothesDesc C1=%p Initialise=%p FindPlayerPed=%p",
            reinterpret_cast<void*>(api.opNew), reinterpret_cast<void*>(api.opDelete),
            reinterpret_cast<void*>(api.clothesCtor), reinterpret_cast<void*>(api.clothesInit),
            reinterpret_cast<void*>(api.findPlayerPed));
    return api;
}

bool AttachPrivatePlayerData(void* ped) {
    if (!ped) return false;
    auto& api = DataApi();
    if (!api.opNew || !api.opDelete || !api.clothesCtor || !api.clothesInit || !api.findPlayerPed) return false;
    void* local = api.findPlayerPed(-1);
    if (!local || local == ped) return false;
    void* localData = *reinterpret_cast<void**>(static_cast<unsigned char*>(local) + 0x540);
    if (!localData) return false;
    void* data = api.opNew(kPlayerPedDataBytes);
    void* clothes = api.opNew(kPedClothesDescBytes);
    if (!data || !clothes) {
        if (data) api.opDelete(data);
        if (clothes) api.opDelete(clothes);
        return false;
    }
    std::memcpy(data, localData, kPlayerPedDataBytes);
    api.clothesCtor(clothes);
    api.clothesInit(clothes);
    *reinterpret_cast<void**>(static_cast<unsigned char*>(data) + 0x8) = clothes;
    *reinterpret_cast<void**>(static_cast<unsigned char*>(ped) + 0x540) = data;
    g_privateData[ped] = PrivateData{data, clothes};
    return true;
}

bool HasPrivatePlayerData(void* ped) {
    return g_privateData.count(ped) != 0;
}

bool CanAttachPrivatePlayerData() {
    auto& api = DataApi();
    return api.opNew && api.opDelete && api.clothesCtor && api.clothesInit && api.findPlayerPed;
}

void* CreateRemotePlayerPed(int model, float x, float y, float z, float heading, bool keepCj) {
    auto& api = Fns();
    if (!api.pedNew || !api.playerCtor || !api.worldAdd) return nullptr;
    if (!EnsurePlayerPedGroupPatch()) {
        static bool reportedPatchFailure = false;
        if (!reportedPatchFailure) {
            reportedPatchFailure = true;
            ENGINE_LOGE("CPlayerPed not created: group patch unavailable");
        }
        return nullptr;
    }
    void* ped = api.pedNew(kPlayerPedNewSize);
    if (!ped) {
        static int reported = 0;
        if (reported < 4) {
            ++reported;
            ENGINE_LOGI("CPlayerPed pool returned null");
        }
        return nullptr;
    }
    // Slot 1, AI behaviour (the false). Slot 0 is the local player.
    api.playerCtor(ped, kRemotePlayerSlot, false);
    // Desktop replaces the constructor's player data with a private copy before anything else (CPlayerPedSA.cpp 69-77).
    const bool privateData = AttachPrivatePlayerData(ped);
    // Model 0 is CJ. Without private data it would rebuild the shared descriptor, so it stays skin 7 (VisibleModel).
    const int skin = ((keepCj || privateData) && model == 0) ? 0 : VisibleModel(model);
    if (api.setModel) api.setModel(ped, static_cast<unsigned>(skin));
    if (api.setCreatedBy) api.setCreatedBy(ped, kCreatedByScript);
    PlaceRemotePlayerPed(ped, x, y, z, heading);
    if (api.setState) api.setState(ped, kPedStateIdle);
    api.worldAdd(ped);
    static int reported = 0;
    if (reported < 4) {
        ++reported;
        ENGINE_LOGI("CPlayerPed(1, false) model %d at %.0f %.0f %.0f", skin, x, y, z);
    }
    return ped;
}

// Desktop CPlayerPedSA::~CPlayerPedSA (CPlayerPedSA.cpp 137-159): CWorld::Remove, then the game's deleting destructor
// (m_pInterface->Destructor(true)), which returns the ped, its CPedIntelligence and every task to their pools.
// The phone only called CWorld::Remove, so each dropped body stayed in CPools::ms_pPedPool with its tasks. Device
// 2026-11-14 02:27-02:29: peds 1 -> 124/140 while live remote bodies fell 41 -> 17, tasks 479/500, then
// CTask::operator new returned null (CTaskSimple ctor with this == 0).
//
// Deviation: CPlayerPed::~CPlayerPed (D2 0x5c0efc) is not run. It releases anim-block refs stored in pPlayerData
// (+0x5c / +0x60) and calls CPedGroups::RemoveGroup(pPlayerData+0x4c). Desktop gives every remote body its own
// CPlayerPedData (CPlayerPedSA.cpp 69-77); here every body shares &CWorld::Players[1]+8 (see the constructor note), so
// that cleanup would act on state other live bodies still use. The base CPed::~CPed (D2 0x59541c) does not read +0x540;
// it destroys the intelligence, weapons and audio, calls CWorld::Remove(this) again (0x595490, as desktop's sequence
// does), then CPhysical D2. CPed::operator delete (0x59571c) frees the pool slot, as CPlayerPed D0 (0x5c0fb0) does.
// Desktop's CProjectileInfo::RemoveEntityReferences has no export on libGTASA 2.10 and is not run.
bool DestroyRemotePlayerPed(void* ped) {
    if (!ped) return false;
    static void (*pedDtor)(void*) = nullptr;
    static void (*pedDelete)(void*) = nullptr;
    static bool looked = false;
    if (!looked) {
        looked = true;
        if (void* handle = dlopen("libGTASA.so", RTLD_NOLOAD | RTLD_GLOBAL)) {
            pedDtor = reinterpret_cast<void (*)(void*)>(dlsym(handle, "_ZN4CPedD2Ev"));
            pedDelete = reinterpret_cast<void (*)(void*)>(dlsym(handle, "_ZN4CPeddlEPv"));
        }
        if (!pedDtor || !pedDelete)
            ENGINE_LOGE("DIAG remote ped destroy: CPed::~CPed=%p operator delete=%p missing; bodies leak in the ped pool",
                reinterpret_cast<void*>(pedDtor), reinterpret_cast<void*>(pedDelete));
    }
    // The stick table is keyed by the body pointer; a reused pool slot must not inherit this entry.
    ClearRemoteStick(ped);
    // CPlayerPedSA::~CPlayerPedSA (CPlayerPedSA.cpp 141-148) skips CWorld::Remove and the destructor once the vtable
    // is CPlaceable: the game already destroyed the ped. CWorld::Remove's vtable+0x20 call then SIGBUSes at
    // MI_CRANE_HARNESS, and CPed D2 / delete would free the slot twice. ReleasePlayerPed (script peds) reaches here
    // without DropBody's check.
    {
        static const unsigned char* placeable = nullptr;
        static bool lookedVtable = false;
        if (!lookedVtable) {
            lookedVtable = true;
            if (void* handle = dlopen("libGTASA.so", RTLD_NOLOAD | RTLD_GLOBAL))
                if (auto* symbol = static_cast<unsigned char*>(dlsym(handle, "_ZTV10CPlaceable"))) placeable = symbol + 16;
        }
        if (placeable && *reinterpret_cast<const unsigned char* const*>(ped) == placeable) {
            ENGINE_LOGI("DIAG remote ped destroy skipped: body %p vtable is CPlaceable", ped);
            g_privateData.erase(ped);
            return false;
        }
    }
    auto& api = Fns();
    if (api.worldRemove) api.worldRemove(ped);
    if (!pedDtor || !pedDelete) return false;
    // CPhysical::~CPhysical returns the ped's real-time shadow: ldr x1, [x19, #0x190] (0x4e0530), then
    // CRealTimeShadowManager::ReturnRealTimeShadow loads shadow->owner (ldr x8, [x1], 0x6dcec0) and clears
    // owner+0x190 (0x6dcec4) without a null check. Device 2026-11-14 03:32:23: owner was null (x8 = 0, fault 0x190),
    // so this body still held a shadow the manager had already given back. The game's invariant is
    // shadow->owner == entity while the entity holds it; a pointer that breaks it is cleared here, with the same store
    // ReturnRealTimeShadow would have made.
    auto* raw = static_cast<unsigned char*>(ped);
    void* shadow = *reinterpret_cast<void**>(raw + 0x190);
    if (shadow && *reinterpret_cast<void**>(shadow) != ped) {
        static int stale = 0;
        if (stale < 4) {
            ++stale;
            ENGINE_LOGI("DIAG remote ped destroy: stale real-time shadow %p (owner %p) cleared on %p", shadow,
                *reinterpret_cast<void**>(shadow), ped);
        }
        *reinterpret_cast<void**>(raw + 0x190) = nullptr;
    }
    pedDtor(ped);
    pedDelete(ped);
    // CPlayerPedSA::~CPlayerPedSA deletes the private data after the game object (CPlayerPedSA.cpp 152-157).
    auto priv = g_privateData.find(ped);
    if (priv != g_privateData.end()) {
        auto& data = DataApi();
        if (data.opDelete) {
            data.opDelete(priv->second.clothes);
            data.opDelete(priv->second.data);
        }
        g_privateData.erase(priv);
    }
    return true;
}

void ReleasePlayerPed(void* ped) {
    if (!ped) return;
    DestroyRemotePlayerPed(ped);
}

namespace {

// CPad::GetPad(i) returns pads + i * 0x158 on libGTASA 2.10 (disassembled:
// smaddl with stride 0x158). CControllerState NewState starts at offset 0, so
// LeftStickX is the short at +0 and LeftStickY the short at +2 (CPad::GetPedWalkLeftRight
// loads [pad] and CPad::GetPedWalkUpDown loads [pad, #2]).
constexpr std::size_t kPadBytes = 0x158;
constexpr int kMaxRemoteSticks = 64;

struct RemoteStick {
    void* ped = nullptr;
    short x = 0;
    short y = 0;
    bool fire = false;
    // CRemoteDataStorageSA::m_fCameraRotation, written into the camera for the call (multiplayer_keysync.cpp 997-998).
    float camera = 0.f;
    bool hasCamera = false;
    // SFullKeysyncSync button flags, and the flags seen on the previous ProcessControl (for the press edges).
    uint8_t buttons = 0;
    uint8_t prevButtons = 0;
    PlayerWeaponSync::Aim aim;
    std::array<uint8_t, 12> weaponCamera{};
    // Time of the last ButtonCircle press, for the follow-up tap (IsDoubleTapped on the attack widget).
    int64_t lastCirclePressMs = 0;
};

// Fire is not written into the pad. libGTASA 2.10 arm64 does not read the fire button from CControllerState:
// CPad::GetMeleeAttack (0x4ddf50) loads the disable flag at [pad, #0x110] and then returns
// CTouchInterface::IsTouched(1, nullptr, 1), the touch widget. A short written at pad+0x26 (the value used here
// before, with a comment that named a different offset) therefore never started the fight task; it only overwrote
// an unverified field of NewState for the length of the call. The remote punch is shown by PresentOnFoot (GameEngine.cpp)
// until a touch-independent fire source is hooked.

RemoteStick g_sticks[kMaxRemoteSticks]{};
using ProcessControlFn = void (*)(void*);
using GetPadFn = void* (*)(int);
ProcessControlFn g_originalControl = nullptr;
GetPadFn g_getPad = nullptr;
using FindLocalPedFn = void* (*)(int);
FindLocalPedFn g_controlFindPlayerPed = nullptr;
bool g_controlHook = false;

// True while CPlayerPed::ProcessControl runs for a body that is not the local player. Game thread only.
bool g_remoteContext = false;
// CWorld::Players (nm 0xbdc738, 2 x CPlayerInfo of 0x1d8). FindPlayerPed (0x4efae0) returns [Players + idx*0x1d8 + 0]:
// smaddl x8, w9, w10(0x1d8), x8; ldr x0, [x8]. Desktop SwitchContext writes the remote ped into CWorld::Players[0].pPed
// (MemPutFast<DWORD>(0xB7CD98, ped), multiplayer_keysync.cpp 1102-1106) and ReturnContextToLocalPlayer writes the local
// ped back (907-911), so FindPlayerPed() is the ped being processed. libGTASA's melee input (CPad::MeleeAttackJustDown
// 0x4ddf84) checks FindPlayerPed(-1)'s tasks and weapon, so without the swap a remote player's combo and kick were judged
// against the local ped (device 2026-11-14: desktop punches replayed, kicks did not).
void** LocalPlayerSlot(void* local) {
    static unsigned char* players = nullptr;
    static bool looked = false;
    if (!looked) {
        looked = true;
        if (void* handle = dlopen("libGTASA.so", RTLD_NOLOAD | RTLD_GLOBAL))
            players = static_cast<unsigned char*>(dlsym(handle, "_ZN6CWorld7PlayersE"));
        if (!players) ENGINE_LOGE("DIAG player context switch: CWorld::Players missing");
    }
    if (!players || !local) return nullptr;
    for (int i = 0; i < 2; ++i) {
        auto** slot = reinterpret_cast<void**>(players + i * 0x1d8);
        if (*slot == local) return slot;
    }
    return nullptr;
}

// Buttons held and pressed this frame by the body being processed (0 for a server ped).
uint8_t g_remoteHeld = 0;
uint8_t g_remotePressed = 0;
// A ButtonCircle press that follows another within kCircleFollowUpMs.
bool g_remoteCircleFollowUp = false;
constexpr int64_t kCircleFollowUpMs = 600;

// Desktop SwitchContext hands GTA the remote player's whole CControllerState, so the game plays sprint, jump and attack
// combos from it. libGTASA 2.10 reads those buttons from touch widgets instead (ids from the CPad getters):
//   GetMeleeAttack (IsTouched 0x1), GetWeapon (IsTouched 0x1), GetSprint (IsTouched 0xa8 / 0x1f),
//   GetJump / JumpJustDown (IsDoubleTapped 0xa8 / 0x1f).
// On foot the PC pad maps ButtonCircle to attack, ButtonCross to sprint and ButtonSquare to jump. In remote context the
// hooks answer from the keysync flags. Not mapped yet: RightShoulder1 (targeting widgets 0x13 / 0x15 / 0x16),
// ButtonTriangle (vehicle entry goes through VEHICLE_INOUT), ShockButtonL (desktop ReadKeysync clears it on foot and
// calls Duck from the flag, which PresentOnFoot does).
constexpr uint8_t kButtonSquare = 0x04;
constexpr uint8_t kButtonCross = 0x08;
constexpr uint8_t kButtonCircle = 0x10;
constexpr int kWidgetAttack = 0x1;
constexpr int kWidgetSprintA = 0xa8;
constexpr int kWidgetSprintB = 0x1f;
constexpr uint8_t kButtonRightShoulder1 = 0x02;
constexpr uint8_t kButtonTriangle = 0x20;
// Block: CPad::GetBlock is IsTouched(0x13) and ProcessPlayerWeapon makes it fight command 2 (0x6538ac-0x6538bc); PC block
// is ButtonSquare while targeting (MeleeAttackJustDown -> 3 -> command 2).
constexpr int kWidgetBlock = 0x13;
bool RemoteWidgetHeld(int widget) {
    if (widget == kWidgetBlock) return (g_remoteHeld & kButtonRightShoulder1) && (g_remoteHeld & kButtonSquare);
    if (widget == kWidgetAttack) return (g_remoteHeld & kButtonCircle) != 0;
    if (widget == kWidgetSprintA || widget == kWidgetSprintB) return (g_remoteHeld & kButtonCross) != 0;
    return false;
}
// CTouchInterface queries the pad getters fall back to on a touch device. They read one global touch state, so a
// remote body would see the local finger. While g_remoteContext is set they report "not touched": the mobile form of
// desktop SwitchContext handing the ped its own (zeroed) pad. All are static members (widget id in w0).
using IsTouchedFn = bool (*)(int, void*, int);
using IsHeldDownFn = bool (*)(int, int);
using IsReleasedFn = bool (*)(int, void*, int);
using IsDoubleTappedFn = bool (*)(int, bool, int);
IsTouchedFn g_origIsTouched = nullptr;
IsHeldDownFn g_origIsHeldDown = nullptr;
IsReleasedFn g_origIsReleased = nullptr;
IsDoubleTappedFn g_origIsDoubleTapped = nullptr;
bool g_touchHooks = false;

#ifdef __ANDROID__
using AimVector = PlayerWeaponSync::Vector;
using CameraTargetFn = void (*)(void*, float, AimVector, AimVector&, AimVector&);
CameraTargetFn g_originalCameraTarget = nullptr;
const PlayerWeaponSync::Aim* g_remoteWeaponAim = nullptr;
uint8_t* weaponCamera = nullptr;
using CameraTargeting = bool (*)(void*);
CameraTargeting originalTargeting = nullptr;
std::chrono::steady_clock::time_point lastLocalTargetButton;
bool localTargetHeld = false;
uintptr_t drivebyButtonCaller = 0;
void* drivebyButtonPed = nullptr;
bool drivebyButtonHeld = false;
bool drivebyButtonPressed = false;
bool CameraIsTargeting(void* camera) {
    if (g_remoteContext) return (g_remoteHeld & PlayerWeaponSync::kAim) != 0;
    return originalTargeting(camera);
}

using WeaponHeldFn = int (*)(void*, void*, bool);
using WeaponPressFn = bool (*)(void*, void*, bool, bool);
WeaponHeldFn g_originalWeaponHeld = nullptr;
WeaponPressFn g_originalWeaponPress = nullptr;
using TargetButtonFn = bool (*)(void*, bool);
TargetButtonFn g_originalTargetButton = nullptr;

RemoteStick* FindStick(void* ped);
constexpr int drivebyAnimations[2][4] = {{233, 231, 234, 232}, {237, 236, 238, 235}};
using PlayerTarget = void (*)(void*, void*);
using GunDirection = bool (*)(void*, float, float, bool, float);
using UseGunProcess = bool (*)(void*, void*);
using PointArm = void (*)(void*, char*, int, void*, void*, int, AimVector*, float, int, float);
using Torso = void (*)(void*, const AimVector&);
PlayerTarget originalPlayerTarget = nullptr, nativeAimGun = nullptr, nativeAbortIK = nullptr;
GunDirection originalGunDirection = nullptr;
UseGunProcess originalUseGunProcess = nullptr;
PointArm originalPointArm = nullptr;
Torso originalTorso = nullptr;
void* lastLocalAimPed = nullptr;
float lastLocalAimYaw = 0.f;
std::chrono::steady_clock::time_point lastLocalDrivebyAim;
bool useGunLayout = false;
uintptr_t aimGunStart = 0;

void DrivebyPlayerTarget(void* task, void* ped) {
    auto* stick = FindStick(ped);
    if (!stick && !g_remoteContext && !IsRemoteNetworkPlayer(ped)) {
        originalPlayerTarget(task, ped);
        return;
    }
    // PlayerTarget selects the window animation and marks aim/fire ready.
    // GTA 2.10 stores the animation at 0x5e8c0c (ManageAnim loads it at
    // 0x5e80c4), and sets both flags at 0x5e8aac (loads 0x5e8944/0x5e8164).
    // Its remaining work switches the active player's global camera mode.
    auto* bytes = static_cast<uint8_t*>(task);
    const unsigned direction = stick && stick->aim.inVehicle ? stick->aim.drivebyDirection : 0;
    *reinterpret_cast<int*>(bytes + 0x2c) = drivebyAnimations[bytes[0x12] != 0][direction & 3];
    bytes[0x13] = bytes[0x14] = 1;
}

bool PointGun(void* ik, float yaw, float pitch, bool arm, float blend) {
    void* ped = *static_cast<void**>(ik);
    if (auto* stick = FindStick(ped); stick && stick->aim.full) {
        const auto& aim = stick->aim;
        yaw = aim.inVehicle ? aim.arm / PlayerWeaponSync::kArmUnits : aim.yaw;
        pitch = aim.inVehicle ? 0.f : aim.arm / PlayerWeaponSync::kArmUnits;
    } else if (!g_remoteContext && g_controlFindPlayerPed && ped == g_controlFindPlayerPed(-1) &&
               HasNativeDriveby(ped)) {
        lastLocalAimPed = ped;
        lastLocalAimYaw = yaw;
        lastLocalDrivebyAim = std::chrono::steady_clock::now();
    }
    return originalGunDirection(ik, yaw, pitch, arm, blend);
}

// Only redirect the arm and torso calls made by CTaskSimpleUseGun::AimGun.
const PlayerWeaponSync::Aim* WeaponPose(void* ped, uintptr_t caller) {
    if (!aimGunStart || caller <= aimGunStart || caller >= aimGunStart + 0x740) return nullptr;
    auto* stick = FindStick(ped);
    return stick && stick->aim.full && stick->aim.range > 0.f ? &stick->aim : nullptr;
}

AimVector AimTarget(const PlayerWeaponSync::Aim& aim) {
    return {aim.origin.x + aim.direction.x * aim.range, aim.origin.y + aim.direction.y * aim.range,
            aim.origin.z + aim.direction.z * aim.range};
}
void AimArm(void* chains, char* name, int arm, void* ped, void* entity, int bone, AimVector* offset,
            float speed, int duration, float priority) {
    AimVector target;
    const auto* aim = WeaponPose(ped, reinterpret_cast<uintptr_t>(__builtin_return_address(0)));
    if (aim && (offset || entity) && !(aim->akimboUp && entity == ped)) {
        // AimGun itself uses (nullptr, -1, worldPoint) at 0x5df178-0x5df198.
        entity = nullptr;
        bone = -1;
        target = AimTarget(*aim);
        offset = &target;
    }
    originalPointArm(chains, name, arm, ped, entity, bone, offset, speed, duration, priority);
}
void AimTorso(void* ik, const AimVector& position) {
    const auto* aim = WeaponPose(*static_cast<void**>(ik), reinterpret_cast<uintptr_t>(__builtin_return_address(0)));
    originalTorso(ik, aim ? AimTarget(*aim) : position);
}
bool ProcessWeaponTask(void* task, void* ped) {
    const bool finished = originalUseGunProcess(task, ped);
    auto* stick = FindStick(ped);
    if (finished || !stick || !stick->aim.full || !stick->aim.akimbo) return finished;
    auto* bytes = static_cast<uint8_t*>(task);
    if (bytes[0x18] >= 1 && bytes[0x18] <= 3 && (bytes[0x16] != 0) != stick->aim.akimboUp) {
        // ProcessPed stores skip-aim at 0x5dea9c and tests it at 0x5deab0.
        bytes[0x16] = stick->aim.akimboUp;
        (stick->aim.akimboUp ? nativeAbortIK : nativeAimGun)(task, ped);
    }
    return finished;
}

bool RemoteTargetButton(void* pad, bool checkCamera) {
    if (g_remoteContext) return (g_remoteHeld & PlayerWeaponSync::kAim) != 0;
    const bool held = g_originalTargetButton && g_originalTargetButton(pad, checkCamera);
    localTargetHeld = held;
    lastLocalTargetButton = std::chrono::steady_clock::now();
    return held;
}

int RemoteWeaponHeld(void* pad, void* ped, bool targeting) {
    if (g_remoteContext) return (g_remoteHeld & PlayerWeaponSync::kFire) ? 255 : 0;
    const int value = g_originalWeaponHeld ? g_originalWeaponHeld(pad, ped, targeting) : 0;
    if (reinterpret_cast<uintptr_t>(__builtin_return_address(0)) == drivebyButtonCaller &&
        ped && g_controlFindPlayerPed && ped == g_controlFindPlayerPed(-1)) {
        drivebyButtonPressed |= value != 0 && (drivebyButtonPed != ped || !drivebyButtonHeld);
        drivebyButtonHeld = value != 0;
        drivebyButtonPed = ped;
    }
    return value;
}

bool RemoteWeaponPress(void* pad, void* ped, bool targeting, bool checkHeld) {
    if (g_remoteContext) return (g_remotePressed & PlayerWeaponSync::kFire) != 0;
    return g_originalWeaponPress && g_originalWeaponPress(pad, ped, targeting, checkHeld);
}

void RemoteCameraTarget(void* camera, float range, AimVector muzzle, AimVector& origin, AimVector& target) {
    if (g_remoteContext && g_remoteWeaponAim && g_remoteWeaponAim->full) {
        const auto& aim = *g_remoteWeaponAim;
        origin = aim.origin;
        target = {origin.x + aim.direction.x * range,
                  origin.y + aim.direction.y * range,
                  origin.z + aim.direction.z * range};
        return;
    }
    if (g_originalCameraTarget) g_originalCameraTarget(camera, range, muzzle, origin, target);
}
#endif


bool HookIsTouched(int widget, void* out, int arg) {
    if (g_remoteContext) {
        if (out) std::memset(out, 0, 8);  // CVector2D
        return RemoteWidgetHeld(widget);
    }
    return g_origIsTouched ? g_origIsTouched(widget, out, arg) : false;
}
bool HookIsHeldDown(int widget, int arg) {
    if (g_remoteContext) return false;
    return g_origIsHeldDown ? g_origIsHeldDown(widget, arg) : false;
}
bool HookIsReleased(int widget, void* out, int arg) {
    if (g_remoteContext) {
        if (out) std::memset(out, 0, 8);
        return false;
    }
    return g_origIsReleased ? g_origIsReleased(widget, out, arg) : false;
}
bool HookIsDoubleTapped(int widget, bool flag, int arg) {
    if (g_remoteContext) {
        // CPad::MeleeAttackJustDown (0x4ddf84) on touch: IsTouched(0x1) -> 1, IsDoubleTapped(0x1) -> 2 (0x4de138).
        // CTaskSimplePlayerOnFoot::ProcessPlayerWeapon (0x653870) turns 1 into fight command 11 and 2 into 12 (11 for a
        // heavy weapon: tst [info+0x19], #2; cinc). On PC, 12 comes from ButtonTriangle while targeting
        // (MeleeAttackJustDown -> 4, gta-reversed Pad.cpp 930-948 and TaskSimplePlayerOnFoot.cpp 266-300). Desktop sends
        // exactly that for its kick (device 2026-11-14 04:30: R1 held, 0x02 -> 0x22 per press). Cross and Square were
        // mapped here before; on PC neither is the heavy attack (Square is block, Cross is no fight command).
        // A Circle press soon after the previous one still answers it (combo follow-up, confirmed on device).
        if (widget == kWidgetAttack)
            return g_remoteCircleFollowUp ||
                   ((g_remoteHeld & kButtonRightShoulder1) && (g_remotePressed & kButtonTriangle));
        return (widget == kWidgetSprintA || widget == kWidgetSprintB) && (g_remotePressed & kButtonSquare) != 0;
    }
    return g_origIsDoubleTapped ? g_origIsDoubleTapped(widget, flag, arg) : false;
}

// TheCamera (nm 0xBBA8D0) + 0x154 is the orientation PlayerControlZelda subtracts from the stick angle: loaded at
// 0x655a28, stored by CCamera::Process at 0x4bbcc0 (GameEngine.cpp SampleLocalPadAndCamera). Desktop VAR_CameraRotation.
float* CameraOrientation() {
    static float* ptr = nullptr;
    static bool looked = false;
    if (!looked) {
        looked = true;
        const uintptr_t base = Hook::MemoryPatcher::getLibraryBaseAddress("libGTASA.so");
        if (base) ptr = reinterpret_cast<float*>(base + 0xBBA8D0 + 0x154);
    }
    return ptr;
}

RemoteStick* FindStick(void* ped) {
    for (auto& entry : g_sticks) {
        if (entry.ped == ped) return &entry;
    }
    return nullptr;
}

// Replacement for CPlayerPed::ProcessControl.
// Desktop SwitchContext (multiplayer_keysync.cpp 942-995) gives every CPlayerPed that is not the local player its own
// pad: MemCpyFast(pLocalPadInterface, &data->m_pad, ...), where m_pad starts zeroed (CRemoteDataSA.h 28) and only
// SetControllerState fills it. A server ped therefore sees no input. The phone used to run the original untouched for
// any body without a keysync stick, so every server ped read the local pad and played the walk cycle whenever the
// local player moved (device 2026-11-14 02:00, after the moving-list fix let those peds be processed).
// Only the left stick (+0 / +2) is swapped; the rest of the mobile CPad is not mapped, and fire/jump on libGTASA 2.10
// come from CTouchInterface, not the pad (see above).
void RemotePlayerProcessControl(void* self) {
    if (!g_originalControl) return;
    const bool local = !g_controlFindPlayerPed || self == g_controlFindPlayerPed(-1);
    RemoteStick* entry = FindStick(self);
    if (local || !g_getPad) {
        g_originalControl(self);
        return;
    }
    auto* pad = static_cast<unsigned char*>(g_getPad(0));
    if (!pad) {
        g_originalControl(self);
        return;
    }
    const short stickX = entry ? entry->x : 0;
    const short stickY = entry ? entry->y : 0;
    unsigned char saved[kPadBytes];
    std::memcpy(saved, pad, kPadBytes);
    std::memcpy(pad + 0, &stickX, sizeof stickX);
    std::memcpy(pad + 2, &stickY, sizeof stickY);
    // Disable-controls fallback, only when the touch hooks are not installed. With the hooks the getters fall through
    // to the pad stick above (CPad::GetPedWalkLeftRight 0x4dcdb4-0x4dcdcc: not touched and CHID::GetInputType 0 ->
    // ldrh w0, [x19]), which is what lets a keysync stick drive a remote player.
    if (!entry && !g_touchHooks) {
        // The stick swap alone did not stop it (device 2026-11-14, server peds still walked in place with the local
        // player). On a touch device CPad::GetPedWalkLeftRight (0x4dcd40) and GetPedWalkUpDown (0x4dcdd4) never read
        // NewState: they return 0 when the uint16 at pad+0x110 is set (ldrh w8, [x19, #0x110]; cbnz), otherwise they
        // return the touch joystick (CTouchInterface::IsTouched widget 167), and read [pad] / [pad,#2] only when
        // CHID::GetInputType is non-zero. The touch widget is global, so it cannot be swapped per ped.
        // pad+0x110 is CPad::DisablePlayerControls (gta-reversed Pad.h 80: uint16 after Mode/ShakeDur, PC 0x10E).
        // Setting it for the call is the mobile stand-in for desktop's zeroed m_pad. CPlayerPed::ProcessControl runs
        // the task tree inside the call (CPed::ProcessControl -> CPedIntelligence::Process), and a remote body
        // reads GetPad(0): CPlayerPed::GetPadFromPlayer (0x5c1f9c) picks the pad from ped+0x720, which
        // CPlayerPed(1, false) leaves 0. The whole pad is restored below.
        // Gap: a remote player with a keysync stick (entry set) still sees the local touch joystick while it is held.
        const uint16_t disabled = 1;
        std::memcpy(pad + 0x110, &disabled, sizeof disabled);
    }
    float* camera = (entry && entry->hasCamera) ? CameraOrientation() : nullptr;
    const float savedCamera = camera ? *camera : 0.f;
    if (camera) *camera = entry->camera;
    const uint8_t held = entry ? entry->buttons : 0;
    g_remoteHeld = held;
    g_remotePressed = entry ? static_cast<uint8_t>(held & ~entry->prevButtons) : 0;
    // CPad::MeleeAttackJustDown (0x4ddf84) continues a combo on IsDoubleTapped(0x1), then ClearTapHistory(0x1): on
    // touch the follow-up hit and the kick come from a second tap. A remote press that follows the previous one within
    // kCircleFollowUpMs answers it. The window is an estimate, not a value read from libGTASA.
    g_remoteCircleFollowUp = false;
    if (entry && (g_remotePressed & kButtonCircle)) {
        const int64_t nowMs = static_cast<int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
        g_remoteCircleFollowUp = entry->lastCirclePressMs != 0 && nowMs - entry->lastCirclePressMs <= kCircleFollowUpMs;
        entry->lastCirclePressMs = nowMs;
    }
    // Player context: FindPlayerPed() is this body for the call (desktop SwitchContext 0xB7CD98). `nocontextswap` in
    // neon_debug_flags turns it off for bisection.
    void* localPed = g_controlFindPlayerPed ? g_controlFindPlayerPed(-1) : nullptr;
    void** playerSlot = WorldRPG::Debug::Flag("nocontextswap") ? nullptr : LocalPlayerSlot(localPed);
    if (playerSlot) *playerSlot = self;
    g_remoteContext = true;
#ifdef __ANDROID__
    g_remoteWeaponAim = entry ? &entry->aim : nullptr;
    // IsTargetingActive loads the weapon mode at 0x4b819c. SetNewPlayerWeaponMode
    // stores mode, duration and zoom limits at 0x4bf27c-0x4bf288. Keep these
    // camera fields per player during native weapon control, as with the pad.
    std::array<uint8_t, 12> savedWeaponCamera{}, emptyWeaponCamera{};
    if (weaponCamera) {
        std::memcpy(savedWeaponCamera.data(), weaponCamera, savedWeaponCamera.size());
        std::memcpy(weaponCamera, entry ? entry->weaponCamera.data() : emptyWeaponCamera.data(), savedWeaponCamera.size());
    }
#endif
    g_originalControl(self);
#ifdef __ANDROID__
    if (weaponCamera) {
        if (entry) std::memcpy(entry->weaponCamera.data(), weaponCamera, savedWeaponCamera.size());
        std::memcpy(weaponCamera, savedWeaponCamera.data(), savedWeaponCamera.size());
    }
    g_remoteWeaponAim = nullptr;
#endif
    g_remoteContext = false;
    if (playerSlot) *playerSlot = localPed;
    g_remoteHeld = 0;
    g_remotePressed = 0;
    g_remoteCircleFollowUp = false;
    if (entry) entry->prevButtons = held;
    if (camera) *camera = savedCamera;
    std::memcpy(pad, saved, kPadBytes);
}

}  // namespace

bool InstallRemoteControlHook() {
#ifdef __ANDROID__
    if (g_controlHook) return true;
    void* handle = dlopen("libGTASA.so", RTLD_NOLOAD | RTLD_GLOBAL);
    if (!handle) return false;
    void* target = dlsym(handle, "_ZN10CPlayerPed14ProcessControlEv");
    g_getPad = reinterpret_cast<GetPadFn>(dlsym(handle, "_ZN4CPad6GetPadEi"));
    g_controlFindPlayerPed = reinterpret_cast<FindLocalPedFn>(dlsym(handle, "_Z13FindPlayerPedi"));
    if (!g_controlFindPlayerPed)
        ENGINE_LOGE("DIAG remote control hook: FindPlayerPed missing; every CPlayerPed reads the local pad");
    if (!target || !g_getPad) {
        ENGINE_LOGE("Remote control hook: missing ProcessControl=%p GetPad=%p",
            target, reinterpret_cast<void*>(g_getPad));
        return false;
    }
    static Hook::MemoryPatcher patcher;
    const auto hooked = patcher.installHook(reinterpret_cast<uintptr_t>(target),
        reinterpret_cast<uintptr_t>(&RemotePlayerProcessControl));
    if (!hooked.success) {
        ENGINE_LOGE("Remote control hook failed: %s", hooked.errorMessage.c_str());
        return false;
    }
    g_originalControl = reinterpret_cast<ProcessControlFn>(hooked.trampolineAddress);
    g_controlHook = true;
    ENGINE_LOGI("Remote CPlayerPed controller hook installed");

    // Touch suppression for remote bodies (see HookIsTouched). All four must install, or none is used and the
    // disable-controls fallback stays on.
    struct TouchHook { const char* symbol; uintptr_t replacement; void** original; };
    static Hook::MemoryPatcher touchPatchers[4];
    const TouchHook touch[4] = {
        {"_ZN15CTouchInterface9IsTouchedENS_9WidgetIDsEP9CVector2Di", reinterpret_cast<uintptr_t>(&HookIsTouched),
         reinterpret_cast<void**>(&g_origIsTouched)},
        {"_ZN15CTouchInterface10IsHeldDownENS_9WidgetIDsEi", reinterpret_cast<uintptr_t>(&HookIsHeldDown),
         reinterpret_cast<void**>(&g_origIsHeldDown)},
        {"_ZN15CTouchInterface10IsReleasedENS_9WidgetIDsEP9CVector2Di", reinterpret_cast<uintptr_t>(&HookIsReleased),
         reinterpret_cast<void**>(&g_origIsReleased)},
        {"_ZN15CTouchInterface14IsDoubleTappedENS_9WidgetIDsEbi", reinterpret_cast<uintptr_t>(&HookIsDoubleTapped),
         reinterpret_cast<void**>(&g_origIsDoubleTapped)},
    };
    void* targets[4] = {};
    for (int i = 0; i < 4; ++i) {
        targets[i] = dlsym(handle, touch[i].symbol);
        if (!targets[i]) {
            ENGINE_LOGE("DIAG touch suppression: %s missing; remote bodies use the disable-controls fallback", touch[i].symbol);
            return true;
        }
    }
    int installed = 0;
    for (int i = 0; i < 4; ++i) {
        const auto result = touchPatchers[i].installHook(reinterpret_cast<uintptr_t>(targets[i]), touch[i].replacement);
        if (!result.success) {
            ENGINE_LOGE("DIAG touch suppression: hook %s failed: %s", touch[i].symbol, result.errorMessage.c_str());
            break;
        }
        *touch[i].original = reinterpret_cast<void*>(result.trampolineAddress);
        ++installed;
    }
    // A partial set still runs through the originals (the trampolines are valid), but the fallback is kept.
    g_touchHooks = installed == 4;
    ENGINE_LOGI("Touch suppression for remote CPlayerPed bodies: %s (%d/4)", g_touchHooks ? "on" : "off", installed);
    return true;
#else
    return false;
#endif
}

namespace {

// Local bullet sync source. Desktop CWeaponSA::Fire (CWeaponSA.cpp 77-85) feeds FireInstantHit, and
// CMultiplayerSA shotsync (multiplayer_shotsync.cpp 1571-1600) reports the start/end to CClientGame::BulletFire.
// CWeapon::Fire is a member function: x0 is this (the weapon). libGTASA 2.10 arm64 (CWeapon::Fire at 0x700c64):
// `mov x19, x0` then `ldr w0, [x19]` loads m_eWeaponType at offset 0 (same as CWeaponSAInterface, whose first
// member is m_eWeaponType). The arguments are x1 owner, x2 vecOrigin, x3 vecEffectPos, x4 target, x5 vecTarget,
// x6 vecAlt.
using WeaponFireFn = bool (*)(void*, void*, void*, void*, void*, void*, void*);
using FindPlayerPedFn = void* (*)(int);
WeaponFireFn g_originalFire = nullptr;
FindPlayerPedFn g_findPlayerPed = nullptr;
bool g_bulletFireHook = false;

// End point source. The desktop does not take the shot from CWeapon::Fire's arguments: its hook sits inside
// CWeapon::FireInstantHit (HOOK_CWeapon_FireInstantHit_Mid, multiplayer_shotsync.cpp 1571-1650), after the game has
// worked out the start and end. That matters because CTaskSimpleUseGun::FireGun (libGTASA 2.10 arm64, 0x5dfd50)
// passes a null vecTarget for a free-aim shot: `mov x5, x21; ldr s0, [x5, #0x30]!` keeps &m_vecTarget only when its x
// and y are non-zero, and the other branch does `mov x5, xzr` (0x5dff0c) before `bl CWeapon::Fire` (0x5dff14).
// The same computed vectors reach the exported CWeapon::DoBulletImpact (0x705c7c): FireInstantHit calls it with
// x3 = &start and x4 = &end (0x701f04-0x701f1c and 0x702124-0x702138; the function saves them with `mov x23, x3` and
// `mov x22, x4`). That call is hooked and its vectors are captured while a local CWeapon::Fire is running.
// Game thread only: both hooks run inside the game's own call.
using BulletImpactFn = void (*)(void*, void*, void*, void*, void*, void*, int);
BulletImpactFn g_originalBulletImpact = nullptr;
struct FireCapture {
    bool active = false;
    int weapon = 0;
    Combat::BulletHit hit;
    uint32_t collision = 0xFFFFFFFFu;
    float impact[3]{};
    void* owner = nullptr;
    bool have = false;
    bool fullLine = false;  // captured from a call with incrementalHit == 0
    float start[3] = {0, 0, 0};
    float end[3] = {0, 0, 0};
};
thread_local FireCapture g_fireCapture;
thread_local const Combat::SBulletSync* g_replayRay = nullptr;

#if defined(__ANDROID__) && defined(__aarch64__)
using ShotLineFn = void (*)(const void*, const void*);
ShotLineFn g_originalShotLine = nullptr;
uintptr_t g_instantHitAddress = 0;
struct ShotLineCall { uintptr_t offset; uint32_t instruction; };
constexpr ShotLineCall kShotLineCalls[] = {
    {0x3bc, 0x97ec7290}, {0x500, 0x97ec723f}, {0x6cc, 0x97ec71cc},
    {0x1064, 0x97ec6f66}
    // +0xcf4 is the continuation from an impact (0x702164-0x702188).
    // GTA advances its origin there; replaying the primary ray would rewind it.
};

// Two primary traces join the impact path without the windscreen check used
// by the camera-specific trace at 0x702650. These calls return the collision
// by reference, before GTA decides whether to damage a vehicle or its occupant.
using ShotSightFn = bool (*)(const AimVector&, const AimVector&, void*, void**, bool, bool, bool, bool, bool, bool, bool, bool);
using CheckOccupantFn = void (*)(void**, void*, int, const AimVector&, const AimVector&);
ShotSightFn originalShotSight = nullptr;
CheckOccupantFn checkVehicleOccupant = nullptr;
bool* includeTyres = nullptr;
bool ShotSight(const AimVector& start, const AimVector& end, void* point, void** entity,
               bool buildings, bool vehicles, bool peds, bool objects, bool dummies,
               bool seeThrough, bool cameraIgnore, bool shootThrough) {
    const auto caller = reinterpret_cast<uintptr_t>(__builtin_return_address(0)) - g_instantHitAddress;
    const bool remotePrimary = g_replayRay && (caller == 0x544 || caller == 0x714);
    if (!remotePrimary)
        return originalShotSight(start, end, point, entity, buildings, vehicles, peds, objects, dummies,
                                 seeThrough, cameraIgnore, shootThrough);
    const bool previous = *includeTyres;
    *includeTyres = true;
    const bool hit = originalShotSight(start, end, point, entity, buildings, vehicles, peds, objects, dummies,
                                       seeThrough, cameraIgnore, shootThrough);
    *includeTyres = previous;
    // The native helper first checks entity type (0x704b50-0x704b5c).
    if (hit && entity && *entity) checkVehicleOccupant(entity, point, g_replayRay->weapon, start, end);
    return hit;
}

// FireInstantHit passes two mutable stack CVectors to CBirds immediately before
// tracing them. The verified call sites below exclude calls from other GTA code.
// Desktop HandleRemoteInstantHit replaces precisely these ray endpoints.
void SyncShotLine(const void* start, const void* end) {
    const auto caller = reinterpret_cast<uintptr_t>(__builtin_return_address(0));
    const auto offset = caller - g_instantHitAddress;
    if (g_instantHitAddress && (g_replayRay || g_fireCapture.active) &&
        std::any_of(std::begin(kShotLineCalls), std::end(kShotLineCalls),
            [offset](const ShotLineCall& call) { return offset == call.offset + sizeof(uint32_t); })) {
        if (g_replayRay) {
            std::memcpy(const_cast<void*>(start), g_replayRay->start, sizeof g_replayRay->start);
            std::memcpy(const_cast<void*>(end), g_replayRay->end, sizeof g_replayRay->end);
        } else if (!g_fireCapture.have) {
            std::memcpy(g_fireCapture.start, start, sizeof g_fireCapture.start);
            std::memcpy(g_fireCapture.end, end, sizeof g_fireCapture.end);
            g_fireCapture.have = g_fireCapture.fullLine = true;
        }
    }
    g_originalShotLine(start, end);
}

using SniperBulletFn = void (*)(void*, int, AimVector, AimVector);
SniperBulletFn g_originalSniperBullet = nullptr;
void CaptureSniperBullet(void* owner, int weapon, AimVector start, AimVector velocity) {
    if (g_fireCapture.active && g_fireCapture.owner == owner && weapon == 34) {
        // FireSniper uses CBulletInfo::AddBullet (0x702c6c), outside the
        // instant-hit path. NoteLocalShot clips the line to the rifle's range.
        const float length = std::hypot(velocity.x, velocity.y, velocity.z);
        const float range = NativeWeaponRange(weapon);
        if (length > 0.f && std::isfinite(length) && range > 0.f && std::isfinite(range)) {
            const float scale = range / length;
            const AimVector end{start.x + velocity.x * scale, start.y + velocity.y * scale, start.z + velocity.z * scale};
            std::memcpy(g_fireCapture.start, &start, sizeof start);
            std::memcpy(g_fireCapture.end, &end, sizeof end);
            g_fireCapture.have = g_fireCapture.fullLine = true;
        }
    }
    g_originalSniperBullet(owner, weapon, start, velocity);
}
#endif

void LocalBulletImpactHook(void* self, void* owner, void* victim, void* startPoint, void* endPoint, void* colPoint,
    int incrementalHit) {
    // FireInstantHit calls DoBulletImpact before GenerateDamageEvent (0x7063e0).
    // The authority processes the true collision on the server. A replay on the
    // mobile owner must not simulate another impact on this particular body.
    if (g_replayRay && victim) {
        const uint32_t id = EntityElementId(victim);
        if (id != 0xFFFFFFFFu && ServerWorld::ActorHosted(id)) {
            double value = 0;
            std::string text;
            bool number = false, boolean = false;
            if (ServerWorld::GetData(id, "neon:serverBulletDamage", value, text, number, &boolean) && boolean && value != 0)
                return;
        }
    }
    FireCapture& cap = g_fireCapture;
    if (cap.active && cap.owner == owner && cap.collision == 0xFFFFFFFFu && victim && colPoint) {
        cap.collision = EntityElementId(victim);
        std::memcpy(cap.impact, colPoint, sizeof cap.impact); // CColPoint begins with the collision position.
    }
    // The first call of this shot is kept. A later call replaces it only when it is the full line (incrementalHit 0)
    // and the kept one is not: the pellet and pass-through calls (0x7020c0) pass a collision point as the end.
    if (cap.active && owner && owner == cap.owner && startPoint && endPoint &&
        (!cap.have || (incrementalHit == 0 && !cap.fullLine))) {
        std::memcpy(cap.start, startPoint, sizeof cap.start);
        std::memcpy(cap.end, endPoint, sizeof cap.end);
        cap.have = true;
        cap.fullLine = incrementalHit == 0;
    }
    if (g_originalBulletImpact) g_originalBulletImpact(self, owner, victim, startPoint, endPoint, colPoint, incrementalHit);
}

// Desktop CClientWeaponManager::HasWeaponBulletSync and the server's CWeaponStatManager::HasWeaponBulletSync: 22..34.
constexpr int kFirstBulletSyncWeapon = 22;
constexpr int kLastBulletSyncWeapon = 34;

bool LocalBulletFireHook(void* self, void* owner, void* vecOrigin, void* vecEffectPos, void* targetEntity,
    void* vecTarget, void* vecAlt) {
    // Read before the original runs: the weapon slot can change during the call.
    // In a remote body's ProcessControl FindPlayerPed() is that body (player context switch), so a shot fired there
    // must not be taken for the local player's.
    const bool local = !g_remoteContext && self && owner && g_findPlayerPed && owner == g_findPlayerPed(-1);
    const int weapon = self ? *static_cast<const int*>(self) : 0;
    const bool supportsBulletSync = weapon >= kFirstBulletSyncWeapon && weapon <= kLastBulletSyncWeapon;
    // The desktop discharges server-synced bullets separately from the pad.
    // Keep the animation running while preventing a second, local-only hit.
    if (!local && supportsBulletSync && Combat::WeaponUsesBulletSync(static_cast<uint8_t>(weapon)) &&
        IsRemoteNetworkPlayer(owner)) return true;
    const uint32_t pedId = supportsBulletSync ? EntityElementId(owner) : 0xFFFFFFFFu;
    const bool hosted = !local && pedId != 0xFFFFFFFFu && ServerWorld::ActorHosted(pedId);
    const bool synced = supportsBulletSync && (local || hosted);

    // Capture the vectors FireInstantHit computes (LocalBulletImpactHook). The previous state is restored so a
    // nested Fire cannot leave the capture armed.
    const FireCapture outer = g_fireCapture;
    g_fireCapture = {};
    if (synced) {
        g_fireCapture.active = true;
        g_fireCapture.owner = owner;
        g_fireCapture.weapon = weapon;
    }
    // The original runs first. Its return value is passed through. The desktop does not gate on it either.
    const bool result = g_originalFire
        ? g_originalFire(self, owner, vecOrigin, vecEffectPos, targetEntity, vecTarget, vecAlt)
        : false;
    const FireCapture shot = g_fireCapture;
    g_fireCapture = outer;
    if (!synced) return result;

    const auto* origin = static_cast<const float*>(vecOrigin);
    const auto* muzzle = static_cast<const float*>(vecEffectPos);
    // The computed vectors when the game produced them, as on the desktop. vecTarget is only the fallback for a shot
    // that never reached DoBulletImpact (or when that hook is not installed).
    const float* start = shot.have ? shot.start : origin;
    const float* end = shot.have ? shot.end : static_cast<const float*>(vecTarget);
    if (!end) {
        static int dropped = 0;
        if (dropped < 4) {
            ++dropped;
            ENGINE_LOGI("DIAG local bullet shot weapon %d has no end point (null vecTarget, no DoBulletImpact); not synced",
                weapon);
        }
        return result;
    }
    // Desktop shotsync (multiplayer_shotsync.cpp 1582-1583): the 2D length from the aimed start to the end
    // is checked against 0.0001. When it is degenerate, the non-aimed start (the muzzle position) is used.
    // NoteLocalShot trims the resulting ray to the weapon range.
    if (start && muzzle) {
        const float dx = end[0] - start[0];
        const float dy = end[1] - start[1];
        if (dx * dx + dy * dy < 0.0001f) start = muzzle;
    }
    if (result || shot.have) {
        if (pedId != 0xFFFFFFFFu && start && !g_replayRay) {
            const auto* native = static_cast<const int*>(self);
            const float* point = shot.collision != 0xFFFFFFFFu ? shot.impact : end;
            Script::QueueNativeWeaponFire({pedId, shot.collision, weapon, native[3], native[2],
                point[0], point[1], point[2], start[0], start[1], start[2], local});
        }
        if (local) NoteLocalShot(weapon, start, end, shot.hit);
    }
    if (hosted) return result;

    static int reported = 0;
    if (reported < 4) {
        ++reported;
        ENGINE_LOGI("Local bullet shot weapon %d end %.0f %.0f %.0f", weapon, end[0], end[1], end[2]);
    }
    return result;
}

}  // namespace

bool SetBulletReplayRay(const Combat::SBulletSync* shot) {
#if defined(__ANDROID__) && defined(__aarch64__)
    if (shot && !g_originalShotLine) return false;
    g_replayRay = shot;
    return true;
#else
    return false;
#endif
}

void CaptureLocalBulletHit(void* attacker, int weapon, const Combat::BulletHit& hit) {
    if (g_fireCapture.active && attacker == g_fireCapture.owner && weapon == g_fireCapture.weapon)
        g_fireCapture.hit = hit;
}

bool InstallLocalBulletFireHook() {
#ifdef __ANDROID__
    if (g_bulletFireHook) return true;
    void* handle = dlopen("libGTASA.so", RTLD_NOLOAD | RTLD_GLOBAL);
    if (!handle) return false;
    void* target = dlsym(handle, "_ZN7CWeapon4FireEP7CEntityP7CVectorS3_S1_S3_S3_");
    g_findPlayerPed = reinterpret_cast<FindPlayerPedFn>(dlsym(handle, "_Z13FindPlayerPedi"));
    constexpr uint32_t firePrologue[] = {0xd10243ff, 0x6d0223e9, 0xa9036ffc, 0xa90467fa};
    if (!target || !g_findPlayerPed || std::memcmp(target, firePrologue, sizeof firePrologue) != 0) {
        ENGINE_LOGE("DIAG local fire hook unavailable for CWeapon::Fire=%p FindPlayerPed=%p",
            target, reinterpret_cast<void*>(g_findPlayerPed));
        return false;
    }
    static Hook::MemoryPatcher patcher;
    const auto hooked = patcher.installHook(reinterpret_cast<uintptr_t>(target),
        reinterpret_cast<uintptr_t>(&LocalBulletFireHook));
    if (!hooked.success) {
        ENGINE_LOGE("Local bullet fire hook failed: %s", hooked.errorMessage.c_str());
        return false;
    }
    g_originalFire = reinterpret_cast<WeaponFireFn>(hooked.trampolineAddress);
    g_bulletFireHook = true;
    ENGINE_LOGI("Local CWeapon::Fire hook installed (PLAYER_BULLETSYNC source)");

    // Fallback endpoint when the shot reaches an impact without a captured full ray.
    void* impact = dlsym(handle, "_ZN7CWeapon14DoBulletImpactEP7CEntityS1_P7CVectorS3_P9CColPointi");
    constexpr uint32_t impactPrologue[] = {0xd10343ff, 0x6d0623e9, 0xa9076ffc, 0xa90867fa};
    if (!impact || std::memcmp(impact, impactPrologue, sizeof impactPrologue) != 0) {
        ENGINE_LOGE("DIAG local bullet impact fallback unavailable for this GTA binary");
        return true;
    }
    static Hook::MemoryPatcher impactPatcher;
    const auto impactHooked = impactPatcher.installHook(reinterpret_cast<uintptr_t>(impact),
        reinterpret_cast<uintptr_t>(&LocalBulletImpactHook));
    if (!impactHooked.success) {
        ENGINE_LOGE("DIAG local bullet end point: local bullet impact fallback hook failed: %s",
            impactHooked.errorMessage.c_str());
        return true;
    }
    g_originalBulletImpact = reinterpret_cast<BulletImpactFn>(impactHooked.trampolineAddress);
    ENGINE_LOGI("Local CWeapon::DoBulletImpact hook installed (bullet end point)");
    return true;
#else
    return false;
#endif
}

bool SampleDrivebyButton(void* ped, bool& pressed) {
    pressed = false;
#if defined(__ANDROID__) && defined(__aarch64__)
    if (ped && ped == drivebyButtonPed) {
        pressed = drivebyButtonPressed;
        drivebyButtonPressed = false;
        return drivebyButtonHeld;
    }
    drivebyButtonPed = nullptr;
    drivebyButtonHeld = drivebyButtonPressed = false;
#endif
    return false;
}

bool LocalTargetButtonHeld() {
#ifdef __ANDROID__
    return localTargetHeld && std::chrono::steady_clock::now() - lastLocalTargetButton < std::chrono::milliseconds(100);
#else
    return false;
#endif
}

bool DrivebyAimHookReady() {
#if defined(__ANDROID__) && defined(__aarch64__)
    return originalPlayerTarget && originalGunDirection;
#else
    return false;
#endif
}

void SampleNativeWeaponAim(void* ped, PlayerWeaponSync::LocalWeapon& weapon, bool firstPersonAim) {
#if defined(__ANDROID__) && defined(__aarch64__)
    static void* lib = dlopen("libGTASA.so", RTLD_NOLOAD | RTLD_GLOBAL);
    if (!lib || !ped) return;
    static void* camera = dlsym(lib, "TheCamera");
    static auto pitch = reinterpret_cast<float (*)(void*)>(dlsym(lib, "_ZN7CCamera26Find3rdPersonQuickAimPitchEv"));
    auto& aim = weapon.aim;
    aim.inVehicle = weapon.gangDriveby;
    float angle = aim.arm / PlayerWeaponSync::kArmUnits;
    if (aim.inVehicle) {
        const bool fresh = lastLocalAimPed == ped && std::chrono::steady_clock::now() - lastLocalDrivebyAim <
            std::chrono::milliseconds(150);
        angle = fresh ? lastLocalAimYaw : std::atan2(-aim.direction.x, aim.direction.y);
        if (auto* task = static_cast<const uint8_t*>(NativeDrivebyTask(ped)); task && originalPlayerTarget) {
            const auto& animations = drivebyAnimations[task[0x12] != 0];
            for (uint8_t i = 0; i < 4; ++i)
                if (*reinterpret_cast<const int*>(task + 0x2c) == animations[i]) aim.drivebyDirection = i;
        }
    } else if (camera && pitch && weapon.aiming && !firstPersonAim) {
        angle = pitch(camera);
    }
    if (std::isfinite(angle)) {
        angle = std::remainder(angle, 6.283185307f);
        aim.arm = static_cast<int16_t>(angle * PlayerWeaponSync::kArmUnits);
    }
    static auto gunTask = reinterpret_cast<void* (*)(void*)>(dlsym(lib, "_ZNK16CPedIntelligence13GetTaskUseGunEv"));
    if (useGunLayout && gunTask && PlayerWeaponSync::IsAkimbo(weapon.type)) {
        void* intel = *reinterpret_cast<void**>(static_cast<uint8_t*>(ped) + 0x538);
        if (auto* task = intel ? static_cast<const uint8_t*>(gunTask(intel)) : nullptr) aim.akimboUp = task[0x16] != 0;
    }
#endif
}

void InstallWeaponAimHooks() {
#if defined(__ANDROID__) && defined(__aarch64__)
    void* lib = dlopen("libGTASA.so", RTLD_NOLOAD | RTLD_GLOBAL);
    if (!lib) {
        ENGINE_LOGE("DIAG weapon aim: libGTASA is unavailable");
        return;
    }
    static Hook::MemoryPatcher patcher;
    // The native pad getters also check the local camera and touch widgets.
    // Give the remote control context its own buttons and shot line instead.
    auto install = [&](const char* symbol, const uint32_t (&prologue)[4], uintptr_t replacement) -> uintptr_t {
        const auto* entry = static_cast<const uint8_t*>(dlsym(lib, symbol));
        if (entry && std::memcmp(entry, prologue, sizeof prologue) == 0) {
            const auto hook = patcher.installHook(reinterpret_cast<uintptr_t>(entry), replacement);
            if (hook.success) return hook.trampolineAddress;
        }
        ENGINE_LOGE("DIAG could not install the remote weapon hook for %s", symbol);
        return 0;
    };
    const char* targetingName = "_ZN7CCamera17IsTargetingActiveEv";
    const auto* targetingCode = static_cast<const uint32_t*>(dlsym(lib, targetingName));
    const auto* setMode = static_cast<const uint32_t*>(dlsym(lib, "_ZN7CCamera22SetNewPlayerWeaponModeEsss"));
    auto* camera = static_cast<uint8_t*>(dlsym(lib, "TheCamera"));
    constexpr uint32_t modeStores[] = {0x79101801, 0x79102c03, 0x79102802, 0xb908101f};
    if (camera && setMode && targetingCode && targetingCode[0x48 / 4] == 0x79501a68 &&
        std::memcmp(setMode, modeStores, sizeof modeStores) == 0) weaponCamera = camera + 0x80c;
    constexpr uint32_t cameraHead[] = {0xf81e0ff3, 0xa9017bfd, 0x910043fd, 0xaa0003f3};
    originalTargeting = reinterpret_cast<CameraTargeting>(install(targetingName, cameraHead, reinterpret_cast<uintptr_t>(&CameraIsTargeting)));
    constexpr uint32_t directionHead[] = {0xd10183ff, 0xfd000bea, 0x6d01a3e9, 0xf90017f7};
    originalGunDirection = reinterpret_cast<GunDirection>(install("_ZN6CPedIK19PointGunInDirectionEffbf",
        directionHead, reinterpret_cast<uintptr_t>(&PointGun)));
    const char* targetName = "_ZN22CTaskSimpleGangDriveBy12PlayerTargetEP4CPed";
    const auto* targetCode = static_cast<const uint32_t*>(dlsym(lib, targetName));
    constexpr uint32_t drivebyHead[] = {0xd10103ff, 0xfd000be8, 0xa9024ff4, 0xa9037bfd};
    if (targetCode && targetCode[0x4c / 4] == 0x78013289 && targetCode[0x15c / 4] == 0x39404a88 &&
        targetCode[0x1ac / 4] == 0xb9002e88)
        originalPlayerTarget = reinterpret_cast<PlayerTarget>(install(targetName, drivebyHead, reinterpret_cast<uintptr_t>(&DrivebyPlayerTarget)));
    nativeAimGun = reinterpret_cast<PlayerTarget>(dlsym(lib, "_ZN17CTaskSimpleUseGun6AimGunEP4CPed"));
    nativeAbortIK = reinterpret_cast<PlayerTarget>(dlsym(lib, "_ZN17CTaskSimpleUseGun7AbortIKEP4CPed"));
    aimGunStart = reinterpret_cast<uintptr_t>(nativeAimGun);
    const char* processName = "_ZN17CTaskSimpleUseGun10ProcessPedEP4CPed";
    const auto* process = static_cast<const uint32_t*>(dlsym(lib, processName));
    useGunLayout = process && process[0xc74 / 4] == 0x39c06268 && process[0xc84 / 4] == 0x39405a68;
    constexpr uint32_t useGunHead[] = {0xd102c3ff, 0xfd002bea, 0x6d0623e9, 0xa9075ff8};
    if (useGunLayout && nativeAimGun && nativeAbortIK)
        originalUseGunProcess = reinterpret_cast<UseGunProcess>(install(processName, useGunHead, reinterpret_cast<uintptr_t>(&ProcessWeaponTask)));
    constexpr uint32_t armHead[] = {0x6db82beb, 0x6d0123e9, 0xf90013fb, 0xa90367fa};
    originalPointArm = reinterpret_cast<PointArm>(install("_ZN16IKChainManager_c8PointArmEPciP4CPedP7CEntityiP5RwV3dfif",
        armHead, reinterpret_cast<uintptr_t>(&AimArm)));
    constexpr uint32_t torsoHead[] = {0xfc1d0fe8, 0xa9014ff4, 0xa9027bfd, 0x910083fd};
    originalTorso = reinterpret_cast<Torso>(install("_ZN6CPedIK17RotateTorsoForArmERK7CVector",
        torsoHead, reinterpret_cast<uintptr_t>(&AimTorso)));
    constexpr uint32_t padPrologue[] = {0xa9be4ff4, 0xa9017bfd, 0x910043fd, 0x79422008};
    const auto* drivebyProcess = static_cast<const uint32_t*>(dlsym(lib, "_ZN22CTaskSimpleGangDriveBy10ProcessPedEP4CPed"));
    // ProcessPed passes the local ped to CPad::GetWeapon at 0x5e87e8. Observe that read once.
    if (drivebyProcess && drivebyProcess[0x578 / 4] == 0x97f0fd56)
        drivebyButtonCaller = reinterpret_cast<uintptr_t>(drivebyProcess) + 0x57c;
    else ENGINE_LOGI("DIAG drive-by fire input call site unavailable");
    g_originalWeaponHeld = reinterpret_cast<WeaponHeldFn>(install("_ZN4CPad9GetWeaponEP4CPedb",
        padPrologue, reinterpret_cast<uintptr_t>(&RemoteWeaponHeld)));
    g_originalWeaponPress = reinterpret_cast<WeaponPressFn>(install("_ZN4CPad14WeaponJustDownEP4CPedbb",
        padPrologue, reinterpret_cast<uintptr_t>(&RemoteWeaponPress)));
    constexpr uint32_t targetPrologue[] = {0xa9be4ff4, 0xa9017bfd, 0x910043fd, 0xaa0003f3};
    g_originalTargetButton = reinterpret_cast<TargetButtonFn>(install("_ZN4CPad9GetTargetEb",
        targetPrologue, reinterpret_cast<uintptr_t>(&RemoteTargetButton)));
    g_instantHitAddress = reinterpret_cast<uintptr_t>(dlsym(lib, "_ZN7CWeapon14FireInstantHitEP7CEntityP7CVectorS3_S1_S3_S3_bb"));
    constexpr uint32_t linePrologue[] = {0xd101c3ff, 0xf90023f5, 0xa9054ff4, 0xa9067bfd};
    if (g_instantHitAddress && std::all_of(std::begin(kShotLineCalls), std::end(kShotLineCalls),
        [](const ShotLineCall& call) {
            return *reinterpret_cast<const uint32_t*>(g_instantHitAddress + call.offset) == call.instruction;
        })) {
        g_originalShotLine = reinterpret_cast<ShotLineFn>(install("_ZN6CBirds13HandleGunShotEPK7CVectorS2_",
            linePrologue, reinterpret_cast<uintptr_t>(&SyncShotLine)));
    } else {
        ENGINE_LOGE("DIAG FireInstantHit shot-line call sites differ from GTA 2.10 ARM64");
    }
    const auto* instant = reinterpret_cast<const uint32_t*>(g_instantHitAddress);
    checkVehicleOccupant = reinterpret_cast<CheckOccupantFn>(dlsym(lib,
        "_ZN7CWeapon31CheckForShootingVehicleOccupantEPP7CEntityP9CColPoint11eWeaponTypeRK7CVectorS8_"));
    includeTyres = static_cast<bool*>(dlsym(lib, "_ZN6CWorld16bIncludeCarTyresE"));
    if (instant && instant[0x540 / 4] == 0x97ec8c5b && instant[0x710 / 4] == 0x97ec8be7 &&
        checkVehicleOccupant && includeTyres) {
        constexpr uint32_t sightHead[] = {0xd104c3ff, 0xfd005bea, 0x6d0c23e9, 0xa90d6ffc};
        originalShotSight = reinterpret_cast<ShotSightFn>(install(
            "_ZN6CWorld18ProcessLineOfSightERK7CVectorS2_R9CColPointRP7CEntitybbbbbbbb", sightHead,
            reinterpret_cast<uintptr_t>(&ShotSight)));
    } else ENGINE_LOGE("DIAG remote bullet vehicle occupant check unavailable");
    constexpr uint32_t sniperPrologue[] = {0x6dbb33ed, 0x6d012beb, 0x6d0223e9, 0xa9034ff4};
    g_originalSniperBullet = reinterpret_cast<SniperBulletFn>(install("_ZN11CBulletInfo9AddBulletEP7CEntity11eWeaponType7CVectorS3_",
        sniperPrologue, reinterpret_cast<uintptr_t>(&CaptureSniperBullet)));
    constexpr uint32_t rayPrologue[] = {0x6db933ed, 0x6d012beb, 0x6d0223e9, 0xa9035ff8};
    g_originalCameraTarget = reinterpret_cast<CameraTargetFn>(install(
        "_ZN7CCamera28Find3rdPersonCamTargetVectorEf7CVectorRS0_S1_",
        rayPrologue, reinterpret_cast<uintptr_t>(&RemoteCameraTarget)));
    dlclose(lib);
#endif
}

bool RemoteControlHookActive() {
    return g_controlHook;
}

bool RemoteTouchActive() {
    return g_touchHooks;
}

bool InRemoteControlContext() {
#ifdef __ANDROID__
    return g_remoteContext;
#else
    return false;
#endif
}

void SetRemoteStick(void* ped, int stickX, int stickY, bool fire, float camera, bool hasCamera,
                    uint8_t buttons, const PlayerWeaponSync::Aim& aim) {
    if (!ped) return;
    RemoteStick* entry = FindStick(ped);
    if (!entry) {
        for (auto& slot : g_sticks) {
            if (!slot.ped) {
                entry = &slot;
                break;
            }
        }
    }
    if (!entry) return;
    entry->ped = ped;
    entry->x = static_cast<short>(std::clamp(stickX, -128, 128));
    entry->y = static_cast<short>(std::clamp(stickY, -128, 128));
    entry->fire = fire;
    entry->camera = camera;
    entry->hasCamera = hasCamera && std::isfinite(camera);
    entry->buttons = buttons;
    entry->aim = aim;
}

void ClearRemoteStick(void* ped) {
    if (RemoteStick* entry = FindStick(ped)) *entry = RemoteStick{};
}

namespace {

// The task entry points the desktop reaches through CTasksSA (TaskCarAccessoriesSA.cpp 20-146). The task
// objects are built on the stack, as the game does in CCarEnterExit::SetPedInCarDirect (0x61b044) and
// CEventKnockOffBike::SetPedOutCar (0x448ae8). Only exported symbols are used.
struct SeatFns {
    bool ready = false;
    bool complete = false;
    // CTaskSimpleCarSetPedInAsDriver(CVehicle*, CTaskUtilityLineUpPedWithCar*) with a null utility, as CTasksSA passes.
    bool (*isDriver)(const void*, const void*) = nullptr;
    bool (*isPassenger)(const void*, const void*) = nullptr;
    void (*driverCtor)(void*, void*, void*) = nullptr;
    bool (*driverProcess)(void*, void*) = nullptr;
    void (*driverDtor)(void*) = nullptr;
    // CTaskSimpleCarSetPedOut(CVehicle*, int door, bool switchOffEngine).
    void (*outCtor)(void*, void*, int, bool) = nullptr;
    bool (*outProcess)(void*, void*) = nullptr;
    void (*outDtor)(void*) = nullptr;
    // CTaskSimpleCarSetPedInAsPassenger(CVehicle*, int door, CTaskUtilityLineUpPedWithCar*) with a null utility, as CTasksSA passes.
    void (*passengerCtor)(void*, void*, int, void*) = nullptr;
    bool (*passengerProcess)(void*, void*) = nullptr;
    void (*passengerDtor)(void*) = nullptr;
};

SeatFns& SeatApi() {
    static SeatFns api;
    if (api.ready) return api;
    api.ready = true;
#ifdef __ANDROID__
    void* handle = dlopen("libGTASA.so", RTLD_NOLOAD | RTLD_GLOBAL);
    if (!handle) return api;
    api.driverCtor = reinterpret_cast<void (*)(void*, void*, void*)>(
        dlsym(handle, "_ZN30CTaskSimpleCarSetPedInAsDriverC1EP8CVehicleP28CTaskUtilityLineUpPedWithCar"));
    api.driverProcess = reinterpret_cast<bool (*)(void*, void*)>(
        dlsym(handle, "_ZN30CTaskSimpleCarSetPedInAsDriver10ProcessPedEP4CPed"));
    api.outCtor = reinterpret_cast<void (*)(void*, void*, int, bool)>(
        dlsym(handle, "_ZN23CTaskSimpleCarSetPedOutC1EP8CVehicleib"));
    api.outProcess = reinterpret_cast<bool (*)(void*, void*)>(
        dlsym(handle, "_ZN23CTaskSimpleCarSetPedOut10ProcessPedEP4CPed"));
    api.outDtor = reinterpret_cast<void (*)(void*)>(dlsym(handle, "_ZN23CTaskSimpleCarSetPedOutD1Ev"));
    api.driverDtor = reinterpret_cast<void (*)(void*)>(dlsym(handle, "_ZN30CTaskSimpleCarSetPedInAsDriverD1Ev"));
    api.passengerCtor = reinterpret_cast<void (*)(void*, void*, int, void*)>(
        dlsym(handle, "_ZN33CTaskSimpleCarSetPedInAsPassengerC1EP8CVehicleiP28CTaskUtilityLineUpPedWithCar"));
    api.passengerProcess = reinterpret_cast<bool (*)(void*, void*)>(
        dlsym(handle, "_ZN33CTaskSimpleCarSetPedInAsPassenger10ProcessPedEP4CPed"));
    api.passengerDtor = reinterpret_cast<void (*)(void*)>(dlsym(handle, "_ZN33CTaskSimpleCarSetPedInAsPassengerD1Ev"));
    api.isDriver = reinterpret_cast<decltype(api.isDriver)>(dlsym(handle, "_ZNK8CVehicle8IsDriverEPK4CPed"));
    api.isPassenger = reinterpret_cast<decltype(api.isPassenger)>(dlsym(handle, "_ZNK8CVehicle11IsPassengerEPK4CPed"));
    api.complete = api.driverCtor && api.driverProcess && api.driverDtor && api.outCtor && api.outProcess && api.outDtor &&
                   api.passengerCtor && api.passengerProcess && api.passengerDtor && api.isDriver && api.isPassenger;
    if (!api.complete) {
        ENGINE_LOGI("Remote seat tasks missing: driver=%p/%p out=%p/%p/%p passenger=%p/%p/%p",
            reinterpret_cast<void*>(api.driverCtor), reinterpret_cast<void*>(api.driverProcess),
            reinterpret_cast<void*>(api.outCtor), reinterpret_cast<void*>(api.outProcess),
            reinterpret_cast<void*>(api.outDtor), reinterpret_cast<void*>(api.passengerCtor),
            reinterpret_cast<void*>(api.passengerProcess), reinterpret_cast<void*>(api.passengerDtor));
    }
#endif
    return api;
}

// Byte the game writes after the driver task constructor, before ProcessPed (SetPedInCarDirect 0x61b084:
// strb w22 = 1 at [sp, #0x30]). The desktop SetIsWarpingPedIntoCar (TaskCarAccessoriesSA.cpp) sets the same flag.
constexpr std::size_t kDriverWarpingOffset = 0x30;
// Bytes the game writes after the out-task constructor, before ProcessPed (SetPedOutCar 0x448b80 and 0x448b84:
// strb 1 at task+0x1F, strb 0 at task+0x1C). The desktop SetKnockedOffBike (TaskCarAccessoriesSA.cpp 142-146)
// writes m_bKnockedOffBike = true, then m_bSwitchOffEngine = false, which is the same pair.
constexpr std::size_t kOutKnockedOffOffset = 0x1F;
constexpr std::size_t kOutSwitchEngineOffset = 0x1C;
// Desktop CClientPed::InternalRemoveFromVehicle builds the out task with door 1 (CClientPed.cpp 7474).
constexpr int kDesktopOutDoor = 1;
// Stack storage for one task. SetPedInCarDirect (0x61b044) saves x23 at sp+0x40, so sp+0..0x3f is free for the task.
// The passenger constructor writes up to +0x3a (strb at +58), the base CTaskSimple constructor writes +0..+0xf.
constexpr std::size_t kSeatTaskBytes = 0x40;
// Byte the game writes after the passenger task constructor, before ProcessPed. SetPedInCarDirect passenger branch
// (0x61b0d4): C1 through 0x22fa10, then `orr w8, wzr, #1` and `strb w8, [sp, #0x38]` at 0x61b130-0x61b13c, then
// ProcessPed through 0x227860, then D1 through 0x219950 at 0x61b254. Desktop CTaskSimpleCarSetPedInAsPassengerSA::
// SetIsWarpingPedIntoCar (TaskCarAccessoriesSA.cpp 90) sets m_bWarpingInToCar, which is the same byte.
constexpr std::size_t kPassengerWarpingOffset = 0x38;
// Game door arguments (Client/sdk/game/TaskCar.h): DOOR_FRONT_LEFT 0, DOOR_FRONT_RIGHT 8, DOOR_REAR_RIGHT 9,
// DOOR_REAR_LEFT 11. SetPedInCarDirect's passenger branch takes doors 8..11 (0x61b0ec: sub w8, w2, #8; cmp w8, #4).
constexpr unsigned char kDoorFrontLeft = 0;
constexpr unsigned char kDoorFrontRight = 8;
constexpr unsigned char kDoorRearRight = 9;
constexpr unsigned char kDoorRearLeft = 11;
constexpr int kPassengerDoorMin = 8;
constexpr int kPassengerDoorMax = 11;
constexpr unsigned char kNoSeat = 0xFF;

// Desktop CClientVehicleManager.cpp 19-32 (g_ucMaxPassengers): passenger count per standard model 400..611.
// Index is model - 400. 0 and 255 mean no passenger seats.
constexpr unsigned char kMaxPassengers[212] = {3, 1,   1,   1,   3,   3, 0,   1,  1, 3, 1, 1,   1, 3, 1, 1,  // 400->415
                                                           3, 1,   3,   1,   3,   3, 1,   1,  1, 0, 3, 3,   3, 1, 0, 8,  // 416->431
                                                           0, 1,   1,   255, 1,   8, 3,   1,  3, 0, 1, 1,   1, 3, 0, 1,  // 432->447
                                                           0, 1,   255, 1,   0,   0, 0,   1,  1, 1, 3, 3,   1, 1, 1,     // 448->462
                                                           1, 1,   1,   3,   3,   1, 1,   3,  1, 0, 0, 1,   1, 0, 1, 1,  // 463->478
                                                           3, 1,   0,   3,   3,   0, 0,   0,  3, 1, 1, 3,   1, 3, 0, 1,  // 479->494
                                                           1, 1,   3,   3,   1,   1, 1,   1,  1, 1, 1, 1,   3, 1, 0, 0,  // 495->510
                                                           1, 0,   0,   1,   1,   3, 1,   1,  0, 0, 1, 1,   1, 1, 1, 1,  // 511->526
                                                           1, 1,   3,   0,   0,   0, 1,   1,  1, 1, 1, 1,   0, 3, 1,     // 527->541
                                                           1, 1,   1,   1,   3,   3, 1,   1,  3, 3, 1, 0,   1, 1, 1, 1,  // 542->557
                                                           1, 1,   3,   3,   1,   1, 0,   1,  3, 3, 0, 255, 1, 0, 0,     // 558->572
                                                           1, 0,   1,   1,   1,   1, 3,   3,  1, 3, 0, 255, 3, 1, 1, 1,  // 573->588
                                                           1, 255, 255, 1,   1,   1, 0,   3,  3, 3, 1, 1,   1, 1, 1,     // 589->604
                                                           3, 1,   255, 255, 255, 3, 255, 255};

// Desktop CModelInfoSA::IsBike and IsQuadBike (CModelInfoSA.h FUNC_IsBikeModel 0x4c5b60, FUNC_IsQuadBikeModel 0x4c5bf0)
// call the exported CModelInfo::IsBikeModel(int) and IsQuadBikeModel(int). Both index ms_aInfoForModel without a bounds
// check, so they are called only for standard models 400..611.
using ModelPredicateFn = bool (*)(int);
struct BikeFns {
    bool ready = false;
    ModelPredicateFn isBike = nullptr;
    ModelPredicateFn isQuadBike = nullptr;
};

BikeFns& BikeApi() {
    static BikeFns api;
    if (api.ready) return api;
    api.ready = true;
#ifdef __ANDROID__
    void* handle = dlopen("libGTASA.so", RTLD_NOLOAD | RTLD_GLOBAL);
    if (!handle) return api;
    api.isBike = reinterpret_cast<ModelPredicateFn>(dlsym(handle, "_ZN10CModelInfo11IsBikeModelEi"));
    api.isQuadBike = reinterpret_cast<ModelPredicateFn>(dlsym(handle, "_ZN10CModelInfo15IsQuadBikeModelEi"));
    if (!api.isBike || !api.isQuadBike) {
        ENGINE_LOGI("Remote seat bike check missing: bike=%p quad=%p", reinterpret_cast<void*>(api.isBike),
            reinterpret_cast<void*>(api.isQuadBike));
    }
#endif
    return api;
}

// Desktop GetVehicleType returns BIKE or QUADBIKE. The caller has checked the model range.
bool IsBikeOrQuadBike(int model) {
    BikeFns& api = BikeApi();
    if (api.isBike && api.isBike(model)) return true;
    return api.isQuadBike && api.isQuadBike(model);
}

}  // namespace

// On ARM64, CPlayerPed::GetPlayerInfoForThisPlayerPed (0x5c7a2c-0x5c7a54)
// searches the two CWorld::Players slots by ped pointer. The native seat
// tasks call CPed::RemoveWeaponWhenEnteringVehicle, which dereferences that
// result at 0x59bad4. Register the remote ped only during the synchronous
// native task on the game thread, and restore the slot as soon as it returns.
SeatPlayerContext::SeatPlayerContext(void* ped) {
#ifdef __ANDROID__
    static auto* players = []() -> unsigned char* {
        if (void* handle = dlopen("libGTASA.so", RTLD_NOLOAD | RTLD_GLOBAL))
            return static_cast<unsigned char*>(dlsym(handle, "_ZN6CWorld7PlayersE"));
        return nullptr;
    }();
    if (!players) {
        static bool reported = false;
        if (!reported) {
            reported = true;
            ENGINE_LOGE("DIAG vehicle seat: CWorld::Players is unavailable");
        }
        return;
    }
    auto** local = reinterpret_cast<void**>(players);
    auto** remote = reinterpret_cast<void**>(players + 0x1d8);
    if (*local != ped && *remote != ped) {
        slot = remote;
        previous = *slot;
        *slot = ped;
    }
    ready = true;
#else
    (void)ped;
#endif
}

bool SeatRemotePedInCar(void* ped, void* car) {
    if (!ped || !car) return false;
    SeatFns& api = SeatApi();
    if (!api.complete) return false;
    // Desktop InternalWarpIntoVehicle (CClientPed.cpp 7441): CreateTaskSimpleCarSetPedInAsDriver, SetIsWarpingPedIntoCar,
    // ProcessPed, Destroy. The destructor must run: the constructor registers a vehicle reference at task+0x20
    // (CEntity::RegisterReference) and D1 clears it. Skipping it leaves the car pointing into this stack frame.
    SeatPlayerContext context(ped);
    if (!context.ready) return false;
    alignas(16) unsigned char task[kSeatTaskBytes] = {};
    api.driverCtor(task, car, nullptr);
    task[kDriverWarpingOffset] = 1;
    api.driverProcess(task, ped);
    api.driverDtor(task);
    return api.isDriver(car, ped);
}

bool UnseatRemotePedFromCar(void* ped, void* car) {
    if (!ped || !car) return false;
    SeatFns& api = SeatApi();
    if (!api.complete) return false;
    if (!api.isDriver(car, ped) && !api.isPassenger(car, ped)) return true;
    // Desktop InternalRemoveFromVehicle (CClientPed.cpp 7464): CreateTaskSimpleCarSetPedOut(vehicle, 1, false),
    // SetKnockedOffBike, ProcessPed, Destroy. SetIsWarpingPedOutOfCar is not set: no verified offset for that flag, so
    // the ped leaves with the knocked-off exit instead of an instant warp. See the report for the gap.
    SeatPlayerContext context(ped);
    if (!context.ready) return false;
    alignas(16) unsigned char task[kSeatTaskBytes] = {};
    api.outCtor(task, car, kDesktopOutDoor, false);
    task[kOutSwitchEngineOffset] = 0;
    task[kOutKnockedOffOffset] = 1;
    api.outProcess(task, ped);
    api.outDtor(task);
    return !api.isDriver(car, ped) && !api.isPassenger(car, ped);
}

unsigned char PassengerGameDoor(int model, unsigned char seatIndex) {
    // Desktop CClientVehicleManager::ConvertIndexToGameSeat (CClientVehicleManager.cpp 580-645). A model outside 400..611
    // takes the 0xFF branch of GetMaxPassengerCount, which is the same as a model with no passenger seats.
    const unsigned char maxPassengers = (model >= 400 && model <= 611) ? kMaxPassengers[model - 400] : 255;
    switch (maxPassengers) {
        case 0:
        case 255:
            return seatIndex == 0 ? kDoorFrontLeft : kNoSeat;
        case 1: {
            // Desktop bIsBike: BIKE or QUADBIKE. The bike's passenger sits in a rear door.
            const bool bike = IsBikeOrQuadBike(model);
            if (seatIndex == 0) return kDoorFrontLeft;
            if (seatIndex == 1) return bike ? kDoorRearRight : kDoorFrontRight;
            if (bike) {
                if (seatIndex == 2) return kDoorRearLeft;
                if (seatIndex == 3) return kDoorRearRight;
            }
            return kNoSeat;
        }
        case 3:
            switch (seatIndex) {
                case 0:
                    return kDoorFrontLeft;
                case 1:
                    return kDoorFrontRight;
                case 2:
                    return kDoorRearLeft;
                case 3:
                    return kDoorRearRight;
                default:
                    return kNoSeat;
            }
        case 8:
            if (seatIndex == 0) return kDoorFrontLeft;
            if (seatIndex <= 8) return kDoorFrontRight;
            return kNoSeat;
        default:
            return kNoSeat;
    }
}

bool SeatRemotePedAsPassenger(void* ped, void* car, int gameDoor) {
    if (!ped || !car) return false;
    if (gameDoor < kPassengerDoorMin || gameDoor > kPassengerDoorMax) return false;
    SeatFns& api = SeatApi();
    if (!api.complete) return false;
    // Desktop InternalWarpIntoVehicle passenger branch (CClientPed.cpp 3441-3462): CreateTaskSimpleCarSetPedInAsPassenger
    // (vehicle, door), SetIsWarpingPedIntoCar, ProcessPed, Destroy. The destructor always runs, as in SeatRemotePedInCar.
    SeatPlayerContext context(ped);
    if (!context.ready) return false;
    alignas(16) unsigned char task[kSeatTaskBytes] = {};
    api.passengerCtor(task, car, gameDoor, nullptr);
    task[kPassengerWarpingOffset] = 1;
    api.passengerProcess(task, ped);
    api.passengerDtor(task);
    return api.isPassenger(car, ped);
}

}  // namespace WorldRPG::Game
