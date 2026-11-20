#include "BreakEffects.h"

#include "LuaVMStub.h"

#include <cmath>

extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

namespace WorldRPG::Script {
namespace {

int g_cacheEntries = 0;

void LogBad(const char* function) { LUA_LOGE("Bad argument @ '%s'", function); }

int CreateEffect(lua_State* L) {
    (void)L;
    // CLuaBreakEffectDefs.cpp:661-711. No object element can be read yet, so this is the
    // bad-argument path. A real object would then require an options table and a streamed mesh.
    LogBad("createObjectBreakEffect");
    lua_pushboolean(L, 0);
    return 1;
}

bool HasBreakEffect(lua_State* L) { return lua_type(L, 1) == LUA_TUSERDATA; }

int FragmentCount(lua_State* L) {
    lua_pushboolean(L, HasBreakEffect(L) ? 0 : 0);
    return 1;
}

int SourceTriangles(lua_State* L) {
    lua_pushboolean(L, HasBreakEffect(L) ? 0 : 0);
    return 1;
}

int SleepingFragments(lua_State* L) {
    lua_pushboolean(L, HasBreakEffect(L) ? 0 : 0);
    return 1;
}

int CacheHit(lua_State* L) {
    lua_pushboolean(L, HasBreakEffect(L) ? 0 : 0);
    return 1;
}

int IsPaused(lua_State* L) {
    lua_pushboolean(L, HasBreakEffect(L) ? 0 : 0);
    return 1;
}

int SetPaused(lua_State* L) {
    if (!HasBreakEffect(L) || lua_type(L, 2) != LUA_TBOOLEAN) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushboolean(L, 0);
    return 1;
}

int CacheSize(lua_State* L) {
    lua_pushnumber(L, g_cacheEntries);
    return 1;
}

int ClearCache(lua_State* L) {
    g_cacheEntries = 0;
    lua_pushboolean(L, 1);
    return 1;
}

int SetProfile(lua_State* L) {
    (void)L;
    LogBad("setObjectBreakProfile");
    lua_pushboolean(L, 0);
    return 1;
}

int GetProfile(lua_State* L) {
    lua_pushboolean(L, lua_type(L, 1) == LUA_TUSERDATA ? 0 : 0);
    return 1;
}

int GetHealth(lua_State* L) {
    lua_pushboolean(L, lua_type(L, 1) == LUA_TUSERDATA ? 0 : 0);
    return 1;
}

int SetHealth(lua_State* L) {
    if (lua_type(L, 1) != LUA_TUSERDATA || !lua_isnumber(L, 2) || !std::isfinite(lua_tonumber(L, 2))) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushboolean(L, 0);
    return 1;
}

int ResetHealth(lua_State* L) {
    lua_pushboolean(L, lua_type(L, 1) == LUA_TUSERDATA ? 0 : 0);
    return 1;
}

int ClearProfile(lua_State* L) {
    lua_pushboolean(L, lua_type(L, 1) == LUA_TUSERDATA ? 0 : 0);
    return 1;
}

}  // namespace

void BindBreakEffectLua(lua_State* L) {
    const struct { const char* name; lua_CFunction fn; } binds[] = {
        {"createObjectBreakEffect", CreateEffect},
        {"getBreakEffectFragmentCount", FragmentCount},
        {"getBreakEffectSourceTriangleCount", SourceTriangles},
        {"getBreakEffectSleepingFragmentCount", SleepingFragments},
        {"getBreakEffectCacheHit", CacheHit},
        {"isBreakEffectPaused", IsPaused},
        {"setBreakEffectPaused", SetPaused},
        {"getBreakEffectCacheSize", CacheSize},
        {"clearBreakEffectCache", ClearCache},
        {"setObjectBreakProfile", SetProfile},
        {"getObjectBreakProfile", GetProfile},
        {"getObjectBreakHealth", GetHealth},
        {"setObjectBreakHealth", SetHealth},
        {"resetObjectBreakHealth", ResetHealth},
        {"clearObjectBreakProfile", ClearProfile},
    };
    for (const auto& bind : binds) {
        lua_pushcfunction(L, bind.fn);
        lua_setglobal(L, bind.name);
    }
}

}  // namespace WorldRPG::Script
