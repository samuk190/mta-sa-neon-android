#pragma once

#include <cstdint>

struct lua_State;

namespace WorldRPG::Script {
void BindEngineAssetLua(lua_State* L);
void ReleaseModelAsset(uint32_t id);
void DropResourceModelAssets(uint32_t generation);
void ResetModelAssets();
}
