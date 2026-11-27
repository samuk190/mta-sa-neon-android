#pragma once

#include <cstdint>

struct lua_State;

namespace WorldRPG::Script {
void WaterRelease(uint32_t id);
void InstallWaterLua(lua_State* L);
void RpcSetWorldWaterLevel(float level, bool includeWorldNonSeaLevel, bool includeAllWaterElements,
                           bool includeWorldSeaLevel, bool includeOutsideWorldLevel);
void RpcResetWorldWaterLevel();
void RpcSetElementWaterLevel(uint32_t id, float level);
void RpcSetAllElementWaterLevel(float level);
void RpcSetWaterVertexPosition(uint32_t id, uint8_t vertexIndex, int16_t x, int16_t y, float z);
void RpcSetWorldSeabedOuterBoundary(float boundary);
}
