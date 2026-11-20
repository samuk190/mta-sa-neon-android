#pragma once

#include <cstdint>

struct lua_State;

namespace WorldRPG::Script {
void FireRelease(uint32_t id);
void InstallFireLua(lua_State* L);
}
