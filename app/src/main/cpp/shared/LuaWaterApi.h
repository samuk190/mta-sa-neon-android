#pragma once

#include <cstdint>

struct lua_State;

namespace WorldRPG::Script {
void WaterRelease(uint32_t id);
void InstallWaterLua(lua_State* L);
void RpcSetWorldWaterLevel(float level, bool includeWorldNonSeaLevel, bool includeAllWaterElements,
                           bool includeWorldSeaLevel, bool includeOutsideWorldLevel);
void RpcResetWorldWaterLevel();
}
