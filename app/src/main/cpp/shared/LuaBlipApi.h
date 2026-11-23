#pragma once
#include <cstdint>
struct lua_State;
namespace WorldRPG::Script {
void InstallBlipLua(lua_State* L);
void BlipRelease(uint32_t id);
}
