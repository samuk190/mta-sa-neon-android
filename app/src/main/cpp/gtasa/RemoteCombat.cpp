#include "RemoteCombat.h"

#include <cstdio>

#ifdef __ANDROID__
#include <android/log.h>
#define COMBAT_LOG_TAG "WorldRPGNeon"
#define COMBAT_LOGI(...) __android_log_print(ANDROID_LOG_INFO, COMBAT_LOG_TAG, __VA_ARGS__)
#else
#define COMBAT_LOGI(...) do { std::printf(__VA_ARGS__); std::printf("\n"); } while (0)
#endif

// Engine hook stubs. Each one logs a DIAG line (rate limited) and returns.
// No GTA native is called from here.
namespace WorldRPG::Combat {

namespace {
constexpr int kMaxDiagLines = 24;
int g_tracerLines = 0;
int g_fireLines = 0;
int g_meleeLines = 0;
int g_healthLines = 0;
} // namespace

void ShowRemoteTracer(uint32_t shooter, uint8_t weapon, const float start[3], const float end[3]) {
    if (g_tracerLines >= kMaxDiagLines) return;
    ++g_tracerLines;
    COMBAT_LOGI("DIAG BULLET_SYNC tracer shooter=%u weapon=%u from %.1f %.1f %.1f to %.1f %.1f %.1f (engine hook not wired)",
                shooter, weapon, start[0], start[1], start[2], end[0], end[1], end[2]);
}

void ShowRemoteWeaponFire(uint32_t weaponElement, const float start[3], const float end[3]) {
    if (g_fireLines >= kMaxDiagLines) return;
    ++g_fireLines;
    COMBAT_LOGI("DIAG WEAPON_BULLET_SYNC weapon_element=%u from %.1f %.1f %.1f to %.1f %.1f %.1f (engine hook not wired)",
                weaponElement, start[0], start[1], start[2], end[0], end[1], end[2]);
}

void StartRemoteMelee(uint32_t id, float heading) {
    if (g_meleeLines >= kMaxDiagLines) return;
    ++g_meleeLines;
    COMBAT_LOGI("DIAG MELEE ped=%u heading=%.2f CTaskSimpleFight requested (engine hook not wired)", id, heading);
}

void SetRemoteHealth(uint32_t id, uint8_t health) {
    if (g_healthLines >= kMaxDiagLines) return;
    ++g_healthLines;
    COMBAT_LOGI("DIAG VICTIM_HEALTH ped=%u health=%u (engine hook not wired)", id, health);
}

} // namespace WorldRPG::Combat
