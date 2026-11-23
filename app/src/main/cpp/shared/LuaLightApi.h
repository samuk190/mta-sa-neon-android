#pragma once
#include <cstdint>
struct lua_State;
namespace WorldRPG::Script {
void InstallLightLua(lua_State* L);
void LightRelease(uint32_t id);
}
