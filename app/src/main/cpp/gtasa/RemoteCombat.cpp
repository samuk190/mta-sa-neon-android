#include "RemoteCombat.h"

#include "GameEngine.h"
#include "../shared/WorldNativeBridge.h"

#include <cmath>
#include <cstdio>

#ifdef __ANDROID__
#include <android/log.h>
#define COMBAT_LOG_TAG "WorldRPGNeon"
#define COMBAT_LOGI(...) __android_log_print(ANDROID_LOG_INFO, COMBAT_LOG_TAG, __VA_ARGS__)
#else
#define COMBAT_LOGI(...) do { std::printf(__VA_ARGS__); std::printf("\n"); } while (0)
#endif

namespace WorldRPG::Combat {

namespace {
constexpr int kMaxDiagLines = 24;
int g_tracerLines = 0;
int g_fireLines = 0;
int g_meleeLines = 0;
int g_healthLines = 0;

void EmitBulletImpactFx(const float start[3], const float end[3]) {
    float dx = start[0] - end[0];
    float dy = start[1] - end[1];
    float dz = start[2] - end[2];
    const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (len > 1e-4f) {
        dx /= len;
        dy /= len;
        dz /= len;
    } else {
        dx = 0.f;
        dy = 0.f;
        dz = 1.f;
    }
    // Emit GTA bullet impact + sparks at hit point (runs on game thread via WorldNativeHooks).
    WorldNative::FxAddBulletImpact(end[0], end[1], end[2], dx, dy, dz, 1, 4, 1.0f);
    WorldNative::FxAddSparks(end[0], end[1], end[2], dx, dy, dz, 1.5f, 3, 0.f, 0.f, 0.f, true, 0.5f, 0.4f);
}
} // namespace

void ShowRemoteTracer(uint32_t shooter, uint8_t weapon, const float start[3], const float end[3]) {
    EmitBulletImpactFx(start, end);
    if (g_tracerLines < kMaxDiagLines) {
        ++g_tracerLines;
        COMBAT_LOGI("BULLET_SYNC impact fx shooter=%u weapon=%u from %.1f %.1f %.1f to %.1f %.1f %.1f",
                    shooter, weapon, start[0], start[1], start[2], end[0], end[1], end[2]);
    }
}

void ShowRemoteWeaponFire(uint32_t weaponElement, const float start[3], const float end[3]) {
    EmitBulletImpactFx(start, end);
    if (g_fireLines < kMaxDiagLines) {
        ++g_fireLines;
        COMBAT_LOGI("WEAPON_BULLET_SYNC impact fx weapon_element=%u from %.1f %.1f %.1f to %.1f %.1f %.1f",
                    weaponElement, start[0], start[1], start[2], end[0], end[1], end[2]);
    }
}

void StartRemoteMelee(uint32_t id, float heading) {
    if (g_meleeLines >= kMaxDiagLines) return;
    ++g_meleeLines;
    COMBAT_LOGI("DIAG MELEE ped=%u heading=%.2f CTaskSimpleFight requested (engine hook not wired)", id, heading);
}

void SetRemoteHealth(uint32_t id, uint8_t health) {
    if (id != 0) {
        Game::RpcSetHealth(id, static_cast<float>(health));
    }
    if (g_healthLines < kMaxDiagLines) {
        ++g_healthLines;
        COMBAT_LOGI("VICTIM_HEALTH applied ped=%u health=%u", id, health);
    }
}

} // namespace WorldRPG::Combat
