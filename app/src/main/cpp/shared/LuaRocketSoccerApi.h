#pragma once
// rocketSim* (desktop CLuaRocketSoccerDefs.cpp) over RocketSoccerSimulation.

#include <cstdint>

struct lua_State;

namespace WorldRPG::Script {

void InstallRocketSoccerLua(lua_State* L);
// Frees the scene a resource start created (desktop: lua_close of that
// resource's VM).
void RocketSoccerDropResource(uint32_t generation);
// Frees every scene (new session or VM).
void RocketSoccerReset();

}  // namespace WorldRPG::Script
