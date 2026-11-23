#pragma once
#include <cstdint>
struct lua_State;
namespace WorldRPG::Script {
void InstallBirdLua(lua_State* L);
void BirdRelease(uint32_t id);
}
