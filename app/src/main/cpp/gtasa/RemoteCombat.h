#pragma once

// Engine hooks for remote combat (S6). The S6 definitions in RemoteCombat.cpp
// only log through DIAG. They do not call GTA natives yet; wiring them to the
// ped/tracer natives is a follow-up.

#include <cstdint>

namespace WorldRPG::Combat {

// PLAYER_BULLETSYNC (98) accepted shot from a remote shooter.
void ShowRemoteTracer(uint32_t shooter, uint8_t weapon, const float start[3], const float end[3]);

// WEAPON_BULLETSYNC (100) instant-hit fire from a weapon element.
void ShowRemoteWeaponFire(uint32_t weaponElement, const float start[3], const float end[3]);

// Remote ped with ButtonCircle keysync and a melee slot: CTaskSimpleFight hook.
void StartRemoteMelee(uint32_t id, float heading);

// Victim health reported by PLAYER_PURESYNC (0..255). Never called for the local player.
void SetRemoteHealth(uint32_t id, uint8_t health);

} // namespace WorldRPG::Combat
