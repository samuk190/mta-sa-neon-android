#pragma once
#include <cstdint>
struct lua_State;
namespace WorldRPG::Script {
void InstallRadarLua(lua_State* L);
void RadarRelease(uint32_t id);
}
