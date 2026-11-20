#pragma once

struct lua_State;

namespace WorldRPG::Script {
void InstallAmbientLua(lua_State* L);
}
