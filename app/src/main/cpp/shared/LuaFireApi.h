#pragma once

#include <cstdint>

struct lua_State;

namespace WorldRPG::Script {
void FireRelease(uint32_t id);
bool FireNoteDamage(uint32_t fireId, uint32_t targetId, float damage);
void InstallFireLua(lua_State* L);
}
