#include "PlayerPedPort.h"
#include "GameEngine.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

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

struct Matrix {
    float rightX, rightY, rightZ, rightPad;
    float upX, upY, upZ, upPad;
    float atX, atY, atZ, atPad;
    float posX, posY, posZ, posPad;
};

struct PortFns {
    bool ready = false;
    void* (*pedNew)(unsigned long) = nullptr;
    void (*playerCtor)(void*, int, bool) = nullptr;
    void (*setModel)(void*, unsigned) = nullptr;
    void (*setCreatedBy)(void*, unsigned char) = nullptr;
    void (*worldAdd)(void*) = nullptr;
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

void PlaceRemotePlayerPed(void* ped, float x, float y, float z, float heading) {
    if (!ped) return;
    auto& api = Fns();
    if (api.setMatrix) {
        // Same yaw the pose reader uses: heading = atan2(-at.x, at.y), up = +Z.
        Matrix matrix{};
        const float s = std::sin(heading);
        const float c = std::cos(heading);
        matrix.rightX = c;
        matrix.rightY = s;
        matrix.upZ = 1.f;
        matrix.atX = -s;
        matrix.atY = c;
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
    *reinterpret_cast<float*>(raw + 0x14) = heading;
    auto* speed = reinterpret_cast<float*>(raw + 0x68);
    speed[0] = speed[1] = speed[2] = 0.f;
}

void* CreateRemotePlayerPed(int model, float x, float y, float z, float heading) {
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
    const int skin = VisibleModel(model);
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
bool g_controlHook = false;

RemoteStick* FindStick(void* ped) {
    for (auto& entry : g_sticks) {
        if (entry.ped == ped) return &entry;
    }
    return nullptr;
}

// Replacement for CPlayerPed::ProcessControl. Only a remote ped with a stick set
// swaps the pad. Everything else calls the original untouched.
void RemotePlayerProcessControl(void* self) {
    if (!g_originalControl) return;
    RemoteStick* entry = FindStick(self);
    if (!entry || !g_getPad) {
        g_originalControl(self);
        return;
    }
    auto* pad = static_cast<unsigned char*>(g_getPad(0));
    if (!pad) {
        g_originalControl(self);
        return;
    }
    unsigned char saved[kPadBytes];
    std::memcpy(saved, pad, kPadBytes);
    std::memcpy(pad + 0, &entry->x, sizeof entry->x);
    std::memcpy(pad + 2, &entry->y, sizeof entry->y);
    g_originalControl(self);
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
    void* owner = nullptr;
    bool have = false;
    bool fullLine = false;  // captured from a call with incrementalHit == 0
    float start[3] = {0, 0, 0};
    float end[3] = {0, 0, 0};
};
FireCapture g_fireCapture;

void LocalBulletImpactHook(void* self, void* owner, void* victim, void* startPoint, void* endPoint, void* colPoint,
    int incrementalHit) {
    FireCapture& cap = g_fireCapture;
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
    const bool local = self && owner && g_findPlayerPed && owner == g_findPlayerPed(-1);
    const int weapon = local ? *static_cast<const int*>(self) : 0;
    const bool synced = local && weapon >= kFirstBulletSyncWeapon && weapon <= kLastBulletSyncWeapon;

    // Capture the vectors FireInstantHit computes (LocalBulletImpactHook). The previous state is restored so a
    // nested Fire cannot leave the capture armed.
    const FireCapture outer = g_fireCapture;
    if (synced) {
        g_fireCapture = FireCapture{};
        g_fireCapture.active = true;
        g_fireCapture.owner = owner;
    }
    // The original runs first. Its return value is passed through. The desktop does not gate on it either.
    const bool result = g_originalFire
        ? g_originalFire(self, owner, vecOrigin, vecEffectPos, targetEntity, vecTarget, vecAlt)
        : false;
    if (!synced) return result;
    const FireCapture shot = g_fireCapture;
    g_fireCapture = outer;

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
    // DIAG gap: the desktop also trims the end to the weapon range (1585-1593); the range is not read here.
    if (start && muzzle) {
        const float dx = end[0] - start[0];
        const float dy = end[1] - start[1];
        if (dx * dx + dy * dy < 0.0001f) start = muzzle;
    }
    NoteLocalShot(weapon, start, end);

    static int reported = 0;
    if (reported < 4) {
        ++reported;
        ENGINE_LOGI("Local bullet shot weapon %d end %.0f %.0f %.0f", weapon, end[0], end[1], end[2]);
    }
    return result;
}

}  // namespace

bool InstallLocalBulletFireHook() {
#ifdef __ANDROID__
    if (g_bulletFireHook) return true;
    void* handle = dlopen("libGTASA.so", RTLD_NOLOAD | RTLD_GLOBAL);
    if (!handle) return false;
    void* target = dlsym(handle, "_ZN7CWeapon4FireEP7CEntityP7CVectorS3_S1_S3_S3_");
    g_findPlayerPed = reinterpret_cast<FindPlayerPedFn>(dlsym(handle, "_Z13FindPlayerPedi"));
    if (!target || !g_findPlayerPed) {
        ENGINE_LOGE("Local bullet fire hook: missing CWeapon::Fire=%p FindPlayerPed=%p",
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

    // End point source for shots with a null vecTarget (see LocalBulletImpactHook). Without it those shots are not
    // synced, which is logged per shot; the Fire hook still works for shots that carry a target.
    void* impact = dlsym(handle, "_ZN7CWeapon14DoBulletImpactEP7CEntityS1_P7CVectorS3_P9CColPointi");
    if (!impact) {
        ENGINE_LOGE("DIAG local bullet end point: CWeapon::DoBulletImpact not found; free-aim shots are not synced");
        return true;
    }
    static Hook::MemoryPatcher impactPatcher;
    const auto impactHooked = impactPatcher.installHook(reinterpret_cast<uintptr_t>(impact),
        reinterpret_cast<uintptr_t>(&LocalBulletImpactHook));
    if (!impactHooked.success) {
        ENGINE_LOGE("DIAG local bullet end point: DoBulletImpact hook failed: %s; free-aim shots are not synced",
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

bool RemoteControlHookActive() {
    return g_controlHook;
}

void SetRemoteStick(void* ped, int stickX, int stickY, bool fire) {
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
    void (*driverCtor)(void*, void*, void*) = nullptr;
    void (*driverProcess)(void*, void*) = nullptr;
    void (*driverDtor)(void*) = nullptr;
    // CTaskSimpleCarSetPedOut(CVehicle*, int door, bool switchOffEngine).
    void (*outCtor)(void*, void*, int, bool) = nullptr;
    void (*outProcess)(void*, void*) = nullptr;
    void (*outDtor)(void*) = nullptr;
    // CTaskSimpleCarSetPedInAsPassenger(CVehicle*, int door, CTaskUtilityLineUpPedWithCar*) with a null utility, as CTasksSA passes.
    void (*passengerCtor)(void*, void*, int, void*) = nullptr;
    void (*passengerProcess)(void*, void*) = nullptr;
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
    api.driverProcess = reinterpret_cast<void (*)(void*, void*)>(
        dlsym(handle, "_ZN30CTaskSimpleCarSetPedInAsDriver10ProcessPedEP4CPed"));
    api.outCtor = reinterpret_cast<void (*)(void*, void*, int, bool)>(
        dlsym(handle, "_ZN23CTaskSimpleCarSetPedOutC1EP8CVehicleib"));
    api.outProcess = reinterpret_cast<void (*)(void*, void*)>(
        dlsym(handle, "_ZN23CTaskSimpleCarSetPedOut10ProcessPedEP4CPed"));
    api.outDtor = reinterpret_cast<void (*)(void*)>(dlsym(handle, "_ZN23CTaskSimpleCarSetPedOutD1Ev"));
    api.driverDtor = reinterpret_cast<void (*)(void*)>(dlsym(handle, "_ZN30CTaskSimpleCarSetPedInAsDriverD1Ev"));
    api.passengerCtor = reinterpret_cast<void (*)(void*, void*, int, void*)>(
        dlsym(handle, "_ZN33CTaskSimpleCarSetPedInAsPassengerC1EP8CVehicleiP28CTaskUtilityLineUpPedWithCar"));
    api.passengerProcess = reinterpret_cast<void (*)(void*, void*)>(
        dlsym(handle, "_ZN33CTaskSimpleCarSetPedInAsPassenger10ProcessPedEP4CPed"));
    api.passengerDtor = reinterpret_cast<void (*)(void*)>(dlsym(handle, "_ZN33CTaskSimpleCarSetPedInAsPassengerD1Ev"));
    api.complete = api.driverCtor && api.driverProcess && api.driverDtor && api.outCtor && api.outProcess && api.outDtor &&
                   api.passengerCtor && api.passengerProcess && api.passengerDtor;
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

bool SeatRemotePedInCar(void* ped, void* car) {
    if (!ped || !car) return false;
    SeatFns& api = SeatApi();
    if (!api.complete) return false;
    // Desktop InternalWarpIntoVehicle (CClientPed.cpp 7441): CreateTaskSimpleCarSetPedInAsDriver, SetIsWarpingPedIntoCar,
    // ProcessPed, Destroy. The destructor must run: the constructor registers a vehicle reference at task+0x20
    // (CEntity::RegisterReference) and D1 clears it. Skipping it leaves the car pointing into this stack frame.
    alignas(16) unsigned char task[kSeatTaskBytes] = {};
    api.driverCtor(task, car, nullptr);
    task[kDriverWarpingOffset] = 1;
    api.driverProcess(task, ped);
    api.driverDtor(task);
    return true;
}

bool UnseatRemotePedFromCar(void* ped, void* car) {
    if (!ped || !car) return false;
    SeatFns& api = SeatApi();
    if (!api.complete) return false;
    // Desktop InternalRemoveFromVehicle (CClientPed.cpp 7464): CreateTaskSimpleCarSetPedOut(vehicle, 1, false),
    // SetKnockedOffBike, ProcessPed, Destroy. SetIsWarpingPedOutOfCar is not set: no verified offset for that flag, so
    // the ped leaves with the knocked-off exit instead of an instant warp. See the report for the gap.
    alignas(16) unsigned char task[kSeatTaskBytes] = {};
    api.outCtor(task, car, kDesktopOutDoor, false);
    task[kOutSwitchEngineOffset] = 0;
    task[kOutKnockedOffOffset] = 1;
    api.outProcess(task, ped);
    api.outDtor(task);
    return true;
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
    alignas(16) unsigned char task[kSeatTaskBytes] = {};
    api.passengerCtor(task, car, gameDoor, nullptr);
    task[kPassengerWarpingOffset] = 1;
    api.passengerProcess(task, ped);
    api.passengerDtor(task);
    return true;
}

}  // namespace WorldRPG::Game
