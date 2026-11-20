#pragma once
#include <cstdint>
struct lua_State;
namespace WorldRPG::Script {
void InstallRopeLua(lua_State* L);
void RopeRelease(uint32_t id);
}
