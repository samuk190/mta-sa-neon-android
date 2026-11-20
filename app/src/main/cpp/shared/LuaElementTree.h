#pragma once

#include <cstdint>

struct lua_State;

namespace WorldRPG::Script {

void InstallElementTreeLua(lua_State* L);
void NoteClientElementDestroyed(uint32_t id);
void ResetElementTreeExtras();
void ReleaseElementStreamingLeases(uint32_t resourceRoot);

// True when setElementVelocity stored a move speed, or the vehicle is frozen
// (CClientVehicle::GetMoveSpeed returns zero while frozen).
bool CopyScriptMoveSpeed(uint32_t id, float& x, float& y, float& z);

// Right, front, up, position. False when setElementMatrix has not stored one.
bool CopyScriptMatrix(uint32_t id, float rows[4][3]);

}  // namespace WorldRPG::Script
