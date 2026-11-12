#include "PlayerPedPort.h"
#include "GameEngine.h"

#include <cmath>
#include <cstring>

#ifdef __ANDROID__
#include <dlfcn.h>
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

}  // namespace WorldRPG::Game
