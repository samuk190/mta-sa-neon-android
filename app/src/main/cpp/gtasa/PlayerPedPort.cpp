#include "PlayerPedPort.h"
#include "GameEngine.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>

#ifdef __ANDROID__
#include <dlfcn.h>
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

// CControllerState: ButtonCircle is the 14th short (0x26), pressed = 255.
constexpr std::size_t kButtonCircleOffset = 0x26;

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
    const short circle = entry->fire ? 255 : 0;
    std::memcpy(pad + kButtonCircleOffset, &circle, sizeof circle);
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

}  // namespace WorldRPG::Game
