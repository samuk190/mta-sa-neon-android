#pragma once
#include <cstdint>
struct lua_State;
namespace WorldRPG::Script {
void InstallResourceLua(lua_State* L);
void KeepResourceEnvironment(lua_State* L, uint32_t generation, int index);
void DropResourceEnvironment(lua_State* L, uint32_t generation);
// sourceResource / sourceResourceRoot for the resource that is triggering an event.
// netId 0 clears both. CMapEventManager.cpp 227-244.
void ApplyEventSource(lua_State* L, uint16_t netId, uint32_t generation);
// Pushes the resource table for that start. False when it is gone or failed.
bool PushResourceInstance(lua_State* L, uint16_t netId, uint32_t generation);
}
