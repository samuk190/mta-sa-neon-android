#pragma once
#include <cstdint>
struct lua_State;
namespace WorldRPG::Script {
void InstallPickupLua(lua_State* L);
void PickupRelease(uint32_t id);
uint32_t PickupColShape(uint32_t id);
}
