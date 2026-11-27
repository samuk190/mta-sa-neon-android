#pragma once

#include <cstddef>
#include <cstdint>

struct lua_State;

namespace WorldRPG::Script {
void InstallWorldQueryLua(lua_State* L);
int RpcRemoveWorldModel(uint16_t model, float radius, float x, float y, float z, int8_t interior);
int RpcRestoreWorldModel(uint16_t model, float radius, float x, float y, float z, int8_t interior);
int RpcRestoreAllWorldModels();
size_t RemovedWorldModelCount();
void WorldQueryReset();
}
