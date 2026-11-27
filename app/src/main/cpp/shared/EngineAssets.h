#pragma once

#include <cstdint>
#include <vector>

struct lua_State;

namespace WorldRPG::Script {
void BindEngineAssetLua(lua_State* L);
void ReleaseModelAsset(uint32_t id);
void DropResourceModelAssets(uint32_t generation);
// Copies the raw TXD bytes for a TXD element owned by expectedGeneration (CClientTXD::CopyDataForRadar).
bool CopyTxdDataForRadar(uint32_t id, uint32_t expectedGeneration, std::vector<uint8_t>& outBytes, bool& outFiltering);
// A model id engineRequestModel gave a running resource.
bool CustomModelAllocated(uint32_t model);
void ResetModelAssets();
}
