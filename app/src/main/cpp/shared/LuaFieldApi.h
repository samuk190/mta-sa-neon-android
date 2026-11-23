#pragma once
#include <cstdint>
struct lua_State;
namespace WorldRPG::Script {
void InstallFieldLua(lua_State* L);
void ApplySpatialArea(uint32_t id,uint64_t generation,uint8_t& interior,uint16_t& dimension);
}
