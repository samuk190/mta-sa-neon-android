// Line of sight, screen projection, roof height, and ped-path patch queries.
// Desktop CLuaWorldDefs.cpp 895-1190, 3092-3115, and 3155-3209.
// CWorld collision and CGraphics projection are not hooked, so a valid query
// takes the desktop failure result. Ped-path state is empty: no patch is loaded,
// which is GetGraphRevision's "no-patches" result.

#include "LuaSightApi.h"

#include "LuaApiExtra.h"
#include "LuaVMStub.h"

#include "lua.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>

namespace WorldRPG::Script {
namespace {

bool g_diagRoof = false;
bool g_diagSight = false;
bool g_diagScreen = false;
bool g_diagSphere = false;
bool g_diagPath = false;
bool g_diagPathUnload = false;

const char* TypeName(int type) {
    switch (type) {
        case LUA_TNIL: return "nil";
        case LUA_TBOOLEAN: return "boolean";
        case LUA_TNUMBER: return "number";
        case LUA_TSTRING: return "string";
        case LUA_TTABLE: return "table";
        default: return type == LUA_TNONE ? "none" : "userdata";
    }
}

void LogFull(const char* function, const char* message) {
    char buf[512];
    std::snprintf(buf, sizeof(buf), "Bad argument @ '%s' [%s]", function, message);
    LUA_LOGI("WARNING: %s", buf);
}

int Fail(lua_State* L) {
    lua_pushboolean(L, 0);
    return 1;
}

bool Finite(lua_State* L, int index) {
    if (!lua_isnumber(L, index)) return false;
    return std::isfinite(lua_tonumber(L, index));
}

bool ReadVec(lua_State* L, const char* function, int index) {
    const int type = lua_type(L, index);
    if (type != LUA_TNUMBER && type != LUA_TSTRING) {
        char buf[160];
        std::snprintf(buf, sizeof(buf), "Expected vector3 at argument %d, got %s", index, TypeName(type));
        LogFull(function, buf);
        return false;
    }
    for (int i = 0; i < 3; ++i) {
        if (!Finite(L, index + i)) {
            LogFull(function, "Expected number, got NaN");
            return false;
        }
    }
    return true;
}

bool ReadBoolSlot(lua_State* L, const char* function, int index, bool optional) {
    const int type = lua_type(L, index);
    if (type == LUA_TBOOLEAN) return true;
    if (optional && (type == LUA_TNONE || type == LUA_TNIL)) return true;
    char buf[160];
    std::snprintf(buf, sizeof(buf), "Expected boolean at argument %d, got %s", index, TypeName(type));
    LogFull(function, buf);
    return false;
}

bool IsElementArg(lua_State* L, int index) {
    if (!lua_istable(L, index)) return false;
    lua_getfield(L, index, "element");
    const bool element = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    return element;
}

int GetRoofPosition(lua_State* L) {
    // A bad vector raises, unlike the other world queries (CLuaWorldDefs.cpp 903).
    if (!ReadVec(L, "getRoofPosition", 1))
        return luaL_error(L, "Bad argument @ 'getRoofPosition' [Expected vector3 at argument 1]");
    if (!g_diagRoof) {
        g_diagRoof = true;
        LUA_LOGE("DIAG getRoofPosition: CWorld::FindRoofZFor3DCoord is not hooked");
    }
    return Fail(L);
}

int ProcessLineOfSight(lua_State* L) {
    if (!ReadVec(L, "processLineOfSight", 1) || !ReadVec(L, "processLineOfSight", 4)) return Fail(L);
    for (int index = 7; index <= 14; ++index) {
        if (!ReadBoolSlot(L, "processLineOfSight", index, true)) return Fail(L);
    }
    const int ignored = lua_type(L, 15);
    if (ignored != LUA_TNONE && ignored != LUA_TNIL && !IsElementArg(L, 15)) {
        LogFull("processLineOfSight", "Expected element at argument 15");
        return Fail(L);
    }
    if (!ReadBoolSlot(L, "processLineOfSight", 16, true) || !ReadBoolSlot(L, "processLineOfSight", 17, true) ||
        !ReadBoolSlot(L, "processLineOfSight", 18, true))
        return Fail(L);
    if (!g_diagSight) {
        g_diagSight = true;
        LUA_LOGE("DIAG processLineOfSight: CWorld::ProcessLineOfSight is not hooked");
    }
    return Fail(L);
}

int IsLineOfSightClear(lua_State* L) {
    if (!ReadVec(L, "isLineOfSightClear", 1) || !ReadVec(L, "isLineOfSightClear", 4)) return Fail(L);
    for (int index = 7; index <= 13; ++index) {
        if (!ReadBoolSlot(L, "isLineOfSightClear", index, true)) return Fail(L);
    }
    const int ignored = lua_type(L, 14);
    if (ignored != LUA_TNONE && ignored != LUA_TNIL && !IsElementArg(L, 14)) {
        LogFull("isLineOfSightClear", "Expected element at argument 14");
        return Fail(L);
    }
    if (!g_diagSight) {
        g_diagSight = true;
        LUA_LOGE("DIAG isLineOfSightClear: CWorld::IsLineOfSightClear is not hooked");
    }
    // The static call did not run, so this is the failure false, not a clear line.
    return Fail(L);
}

int GetWorldFromScreenPosition(lua_State* L) {
    if (!ReadVec(L, "getWorldFromScreenPosition", 1)) return Fail(L);
    if (!g_diagScreen) {
        g_diagScreen = true;
        LUA_LOGE("DIAG getWorldFromScreenPosition: CGraphics::CalcWorldCoors is not hooked");
    }
    return Fail(L);
}

int GetScreenFromWorldPosition(lua_State* L) {
    if (!ReadVec(L, "getScreenFromWorldPosition", 1)) return Fail(L);
    float tolerance = 0.f;
    if (!lua_isnoneornil(L, 4) && !Finite(L, 4)) {
        LogFull("getScreenFromWorldPosition", "Expected number at argument 4");
        return Fail(L);
    }
    (void)tolerance;
    if (!ReadBoolSlot(L, "getScreenFromWorldPosition", 5, true)) return Fail(L);
    if (!g_diagScreen) {
        g_diagScreen = true;
        LUA_LOGE("DIAG getScreenFromWorldPosition: CGraphics::CalcScreenCoors is not hooked");
    }
    return Fail(L);
}

int ProcessLineAgainstMesh(lua_State* L) {
    if (!IsElementArg(L, 1) || !KnownClientElement(ElementIdOf(L, 1)))
        return luaL_error(L, "Bad argument @ 'processLineAgainstMesh' [Expected element at argument 1, got %s]", TypeName(lua_type(L, 1)));
    if (!ReadVec(L, "processLineAgainstMesh", 2) || !ReadVec(L, "processLineAgainstMesh", 5))
        return luaL_error(L, "Bad argument @ 'processLineAgainstMesh' [Expected vector3]");
    // No streamed game entity has a collision mesh, so this is the desktop no-hit result.
    return Fail(L);
}

int TestSphereAgainstWorld(lua_State* L) {
    if (!ReadVec(L, "testSphereAgainstWorld", 1) || !Finite(L, 4))
        return luaL_error(L, "Bad argument @ 'testSphereAgainstWorld' [Expected vector3 and radius]");
    if (!g_diagSphere) {
        g_diagSphere = true;
        LUA_LOGE("DIAG testSphereAgainstWorld: CWorld::TestSphereAgainstWorld is not hooked");
    }
    lua_pushboolean(L, 0);
    lua_pushnil(L);
    lua_pushinteger(L, 0);
    for (int i = 0; i < 6; ++i) lua_pushnumber(L, 0);
    lua_pushinteger(L, 0);
    lua_pushstring(L, "unknown");
    return 11;
}

void PathPair(lua_State* L, bool ok, const char* text) {
    if (ok) {
        lua_pushstring(L, text);
        return;
    }
    lua_pushboolean(L, 0);
    lua_pushstring(L, text);
}

int EngineLoadPedPathPatch(lua_State* L) {
    if (lua_type(L, 1) != LUA_TSTRING || ActiveVmOwner().empty()) {
        PathPair(L, false, "invalid-resource-path");
        return 2;
    }
    if (!g_diagPath) {
        g_diagPath = true;
        LUA_LOGE("DIAG engineLoadPedPathPatch: the ped path file is not loaded and the pathfinder hooks are not installed");
    }
    PathPair(L, false, "patch-file-unavailable-or-too-large");
    return 2;
}

int EngineUnloadPedPathPatch(lua_State* L) {
    (void)L;
    // CPedPathPatchesSA.cpp Service::Unload returns false when the owner has no lease.
    // engineLoadPedPathPatch never stores one.
    if (!g_diagPathUnload) {
        g_diagPathUnload = true;
        LUA_LOGE("DIAG engineUnloadPedPathPatch: no ped-path lease is stored");
    }
    lua_pushboolean(L, 0);
    return 1;
}

int EngineGetPedPathPatchRevision(lua_State* L) {
    (void)L;
    PathPair(L, false, "no-patch");
    return 2;
}

int EngineGetPedPathGraphRevision(lua_State* L) {
    (void)L;
    PathPair(L, false, "no-patches");
    return 2;
}

void Register(lua_State* L) {
    const struct {
        const char* name;
        lua_CFunction fn;
    } binds[] = {
        {"getRoofPosition", GetRoofPosition},
        {"processLineOfSight", ProcessLineOfSight},
        {"isLineOfSightClear", IsLineOfSightClear},
        {"getWorldFromScreenPosition", GetWorldFromScreenPosition},
        {"getScreenFromWorldPosition", GetScreenFromWorldPosition},
        {"processLineAgainstMesh", ProcessLineAgainstMesh},
        {"testSphereAgainstWorld", TestSphereAgainstWorld},
        {"engineLoadPedPathPatch", EngineLoadPedPathPatch},
        {"engineUnloadPedPathPatch", EngineUnloadPedPathPatch},
        {"engineGetPedPathPatchRevision", EngineGetPedPathPatchRevision},
        {"engineGetPedPathGraphRevision", EngineGetPedPathGraphRevision},
    };
    for (const auto& bind : binds) {
        lua_pushcfunction(L, bind.fn);
        lua_setglobal(L, bind.name);
    }
}

}  // namespace

void InstallSightLua(lua_State* L) { Register(L); }

}  // namespace WorldRPG::Script
