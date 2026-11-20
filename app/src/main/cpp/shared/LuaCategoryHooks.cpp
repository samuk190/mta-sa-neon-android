// One install point for the client Lua category ports. InstallElementLua is
// defined in this build. The ped, vehicle and GUI ports are optional: a missing
// sibling is a weak symbol and does not fail the link.

struct lua_State;

#include "LuaEngineApi.h"

extern "C" void InstallElementLua(lua_State* L);
extern "C" void InstallPedLua(lua_State* L) __attribute__((weak));
extern "C" void InstallVehicleLua(lua_State* L) __attribute__((weak));
extern "C" void InstallGuiLua(lua_State* L) __attribute__((weak));

extern "C" void InstallCategoryLua(lua_State* L) {
    InstallElementLua(L);
    if (InstallPedLua) InstallPedLua(L);
    if (InstallVehicleLua) InstallVehicleLua(L);
    if (InstallGuiLua) InstallGuiLua(L);
    WorldRPG::Script::InstallEngineLua(L);
}
