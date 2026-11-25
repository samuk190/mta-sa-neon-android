#pragma once

// CScriptArgReader::ReadString on desktop accepts a number and copies
// lua_tostring(index) with length lua_strlen(index) (CScriptArgReader.h:607-625).
// In MTA's Lua 5.1, lua_objlen converts a number to its string first
// (vendor/lua/src/lapi.c:401-406), so 1700 reads as "1700". The text is the
// Lua 5.1 LUA_NUMBER_FMT form ("%.14g"), the same for scripts on the 5.3 VM.

#include "lua.hpp"

#include <cstdio>
#include <string>

namespace WorldRPG::Script {

inline std::string DesktopNumberText(lua_State* L, int index) {
    char text[64];
    std::snprintf(text, sizeof(text), "%.14g", static_cast<double>(lua_tonumber(L, index)));
    return text;
}

}  // namespace WorldRPG::Script
