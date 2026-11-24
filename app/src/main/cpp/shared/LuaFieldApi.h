#pragma once
#include <cstdint>
struct lua_State;
namespace WorldRPG::Script {
void InstallFieldLua(lua_State* L);
void ApplySpatialArea(uint32_t id,uint64_t generation,uint8_t& interior,uint16_t& dimension);
// Interior, dimension, collisions, frozen and alpha state scripts set on a client element (defaults otherwise).
void ClientElementWorldState(uint32_t id, uint8_t& interior, uint16_t& dimension, bool& collisions, bool& frozen,
                             uint8_t& alpha);
}
