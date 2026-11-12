#pragma once

#include <cstdint>

namespace WorldRPG::Game {

// One remote CPlayerPed from the ped pool. Null when the pool is full or
// CPlayerPed::CPlayerPed is not exported. Does not call SetupPlayerPed or
// CPed::Teleport.
void* CreateRemotePlayerPed(int model, float x, float y, float z, float heading);

// Copies position and yaw into the ped matrix the way CEntitySA::SetMatrix
// does. Falls back to the CPlaceable position fields when SetMatrix is missing.
void PlaceRemotePlayerPed(void* ped, float x, float y, float z, float heading);

// Plan step A (controller switch). Hooks CPlayerPed::ProcessControl once. For a
// remote ped with a stick set, CPad::GetPad(0) gets that stick for one call and
// the whole pad is restored afterwards. Returns false when the symbol, CPad::GetPad
// or the hook is missing; the caller then keeps the velocity fallback.
bool InstallRemoteControlHook();
bool RemoteControlHookActive();
// Stick in PC CControllerState units (-128..128). Negative Y is forward.
// fire sets ButtonCircle (CControllerState offset 0x26) to 255, the desktop pressed value.
void SetRemoteStick(void* ped, int stickX, int stickY, bool fire);
void ClearRemoteStick(void* ped);

// Hooks CWeapon::Fire once (desktop CWeaponSA::Fire / CMultiplayerSA shotsync). For the local ped with a
// bullet-weapon id (22..34) the shot is queued with NoteLocalShot after the original runs. Returns false when
// the symbol, FindPlayerPed or the hook is missing; no bullet sync is sent then.
bool InstallLocalBulletFireHook();

}
