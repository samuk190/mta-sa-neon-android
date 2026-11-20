#pragma once

#include <cstdint>

struct lua_State;

namespace WorldRPG::Script {
void BindEngineAssetLua(lua_State* L);
void ReleaseDffElement(uint32_t id);
void DropResourceDffs(uint32_t generation);
void ResetDffElements();
}
