#pragma once

// fromJSON / toJSON for the phone's Lua VM. Value mapping follows the desktop client:
// CLuaArguments::WriteToJSONString / ReadFromJSONString and CLuaArgument::WriteToJSONObject /
// ReadFromJSONObject (Client/mods/deathmatch/logic/lua/CLuaArguments.cpp 537-797 and
// CLuaArgument.cpp 832-1123). Output layout is json-c's JSON_C_TO_STRING_SPACED (the desktop
// default) and PLAIN for compact = true, from vendor/json-c/json_object.c.

struct lua_State;

namespace WorldRPG::Script {

void InstallJsonBindings(lua_State* L);

} // namespace WorldRPG::Script
