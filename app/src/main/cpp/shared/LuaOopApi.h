#pragma once

struct lua_State;

namespace WorldRPG::Script {

// Registers desktop MTA Lua OOP classes (Vector2, Vector3, Vector4, Matrix,
// Element hierarchy, GUI, DX, Engine, Camera, World, Resource, Timer, File, XML).
// Works in both Lua 5.1 and Lua 5.3 VMs.
void InstallOopLua(lua_State* L);

// Attaches the shared Element OOP metatable to the table on top of L.
void AttachElementOopMetatable(lua_State* L);

// Attaches the shared Resource OOP metatable to the table on top of L.
void AttachResourceOopMetatable(lua_State* L);

}  // namespace WorldRPG::Script
