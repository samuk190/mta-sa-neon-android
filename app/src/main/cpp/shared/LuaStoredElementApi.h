#pragma once
#include <cstdint>
struct lua_State;
namespace WorldRPG::Script {
void InstallStoredElementLua(lua_State* L);
bool FoliageElementAlive(uint32_t id);
bool EffectElementAlive(uint32_t id);
const char* StoredElementType(uint32_t id);
bool ReleaseStoredElement(uint32_t id);
}
