#pragma once

namespace WorldRPG::Game {

// One remote CPlayerPed from the ped pool. Null when the pool is full or
// CPlayerPed::CPlayerPed is not exported. Does not call SetupPlayerPed or
// CPed::Teleport.
void* CreateRemotePlayerPed(int model, float x, float y, float z, float heading);

// Copies position and yaw into the ped matrix the way CEntitySA::SetMatrix
// does. Falls back to the CPlaceable position fields when SetMatrix is missing.
void PlaceRemotePlayerPed(void* ped, float x, float y, float z, float heading);

}
