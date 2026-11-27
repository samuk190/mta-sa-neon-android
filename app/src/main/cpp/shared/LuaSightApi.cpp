// Line of sight, screen projection, roof height, and ped-path patch queries.
// Desktop CLuaWorldDefs.cpp 895-1190, 3092-3115, and 3155-3209.
// Roof height and line-of-sight run on the GTA game thread via GameEngine;
// screen/world projection uses the cached TheCamera.m_mViewMatrix snapshot
// (CGraphics.cpp 624-670, CStaticFunctionDefinitions.cpp 6769-6799).
// Ped-path state is empty: no patch is loaded, which is GetGraphRevision's "no-patches" result.

#include "LuaSightApi.h"

#include "LuaApiExtra.h"
#include "LuaVMStub.h"
#include "../gtasa/GameEngine.h"

#include "lua.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>

namespace WorldRPG::Script {
namespace {

bool g_diagWorldModel = false;
bool g_diagSphere = false;
bool g_diagPath = false;
bool g_diagPathUnload = false;

bool ReadVecValues(lua_State* L, const char* function, int index, float& x, float& y, float& z);

bool ReadBoolDefault(lua_State* L, int index, bool fallback) {
    const int type = lua_type(L, index);
    if (type == LUA_TNONE || type == LUA_TNIL) return fallback;
    return lua_toboolean(L, index) != 0;
}

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

bool ReadVecValues(lua_State* L, const char* function, int index, float& x, float& y, float& z) {
    if (!ReadVec(L, function, index)) return false;
    x = static_cast<float>(lua_tonumber(L, index));
    y = static_cast<float>(lua_tonumber(L, index + 1));
    z = static_cast<float>(lua_tonumber(L, index + 2));
    return true;
}

int GetRoofPosition(lua_State* L) {
    // A bad vector raises, unlike the other world queries (CLuaWorldDefs.cpp 895-918).
    float x = 0.f, y = 0.f, z = 0.f;
    if (!ReadVecValues(L, "getRoofPosition", 1, x, y, z))
        return luaL_error(L, "Bad argument @ 'getRoofPosition' [Expected vector3 at argument 1]");
    float roofZ = 0.f;
    if (Game::FindRoofZFor3DPosition(x, y, z, roofZ)) {
        lua_pushnumber(L, roofZ);
        return 1;
    }
    return Fail(L);
}

int ProcessLineOfSight(lua_State* L) {
    // CLuaWorldDefs::ProcessLineOfSight (CLuaWorldDefs.cpp 951-1060).
    float sx = 0.f, sy = 0.f, sz = 0.f, ex = 0.f, ey = 0.f, ez = 0.f;
    if (!ReadVecValues(L, "processLineOfSight", 1, sx, sy, sz) ||
        !ReadVecValues(L, "processLineOfSight", 4, ex, ey, ez))
        return Fail(L);
    for (int index = 7; index <= 14; ++index) {
        if (!ReadBoolSlot(L, "processLineOfSight", index, true)) return Fail(L);
    }
    const int ignored = lua_type(L, 15);
    uint32_t ignoredId = 0;
    if (ignored != LUA_TNONE && ignored != LUA_TNIL) {
        if (!IsElementArg(L, 15)) {
            LogFull("processLineOfSight", "Expected element at argument 15");
            return Fail(L);
        }
        ignoredId = ElementIdOf(L, 15);
    }
    if (!ReadBoolSlot(L, "processLineOfSight", 16, true) || !ReadBoolSlot(L, "processLineOfSight", 17, true) ||
        !ReadBoolSlot(L, "processLineOfSight", 18, true))
        return Fail(L);
    const bool wantWorldModel = ReadBoolDefault(L, 16, false);
    const bool wantCarTyres = ReadBoolDefault(L, 17, false);
    const bool wantBuildings = ReadBoolDefault(L, 18, false);
    if ((wantWorldModel || wantCarTyres || wantBuildings) && !g_diagWorldModel) {
        g_diagWorldModel = true;
        LUA_LOGI("DIAG processLineOfSight: includeWorldModelInformation/carTyres/buildingModels metadata is not populated");
    }
    Game::LineOfSightFlags flags;
    flags.buildings = ReadBoolDefault(L, 7, true);
    flags.vehicles = ReadBoolDefault(L, 8, true);
    flags.peds = ReadBoolDefault(L, 9, true);
    flags.objects = ReadBoolDefault(L, 10, true);
    flags.dummies = ReadBoolDefault(L, 11, true);
    flags.seeThrough = ReadBoolDefault(L, 12, false);
    flags.ignoreCameraObjects = ReadBoolDefault(L, 13, false);
    flags.shootThrough = ReadBoolDefault(L, 14, false);
    flags.ignoredElement = ignoredId;

    Game::LineOfSightResult hit;
    if (!Game::ProcessLineOfSight(sx, sy, sz, ex, ey, ez, flags, hit)) return Fail(L);
    lua_pushboolean(L, 1);
    lua_pushnumber(L, hit.x);
    lua_pushnumber(L, hit.y);
    lua_pushnumber(L, hit.z);
    if (hit.hitElement != 0xFFFFFFFFu && hit.hitElement != 0) PushElement(L, hit.hitElement);
    else lua_pushnil(L);
    lua_pushnumber(L, hit.nx);
    lua_pushnumber(L, hit.ny);
    lua_pushnumber(L, hit.nz);
    lua_pushinteger(L, hit.material);
    lua_pushinteger(L, hit.lighting);
    lua_pushinteger(L, hit.piece);
    return 11;
}

int IsLineOfSightClear(lua_State* L) {
    // CLuaWorldDefs::IsLineOfSightClear (CLuaWorldDefs.cpp 1087-1135).
    float sx = 0.f, sy = 0.f, sz = 0.f, ex = 0.f, ey = 0.f, ez = 0.f;
    if (!ReadVecValues(L, "isLineOfSightClear", 1, sx, sy, sz) ||
        !ReadVecValues(L, "isLineOfSightClear", 4, ex, ey, ez))
        return Fail(L);
    for (int index = 7; index <= 13; ++index) {
        if (!ReadBoolSlot(L, "isLineOfSightClear", index, true)) return Fail(L);
    }
    const int ignored = lua_type(L, 14);
    uint32_t ignoredId = 0;
    if (ignored != LUA_TNONE && ignored != LUA_TNIL) {
        if (!IsElementArg(L, 14)) {
            LogFull("isLineOfSightClear", "Expected element at argument 14");
            return Fail(L);
        }
        ignoredId = ElementIdOf(L, 14);
    }
    Game::LineOfSightFlags flags;
    flags.buildings = ReadBoolDefault(L, 7, true);
    flags.vehicles = ReadBoolDefault(L, 8, true);
    flags.peds = ReadBoolDefault(L, 9, true);
    flags.objects = ReadBoolDefault(L, 10, true);
    flags.dummies = ReadBoolDefault(L, 11, true);
    flags.seeThrough = ReadBoolDefault(L, 12, false);
    flags.ignoreCameraObjects = ReadBoolDefault(L, 13, false);
    flags.ignoredElement = ignoredId;

    bool clear = false;
    if (!Game::IsLineOfSightClear(sx, sy, sz, ex, ey, ez, flags, clear)) return Fail(L);
    lua_pushboolean(L, clear ? 1 : 0);
    return 1;
}

int GetWorldFromScreenPosition(lua_State* L) {
    // CLuaWorldDefs::GetWorldFromScreenPosition (1137-1161) + CGraphics::CalcWorldCoors (CGraphics.cpp 624-647).
    float sx = 0.f, sy = 0.f, depth = 0.f;
    if (!ReadVecValues(L, "getWorldFromScreenPosition", 1, sx, sy, depth)) return Fail(L);
    float m[16];
    if (!Game::CopyCameraViewMatrix(m)) return Fail(L);
    int screenW = 0, screenH = 0;
    ActiveVmScreenSize(screenW, screenH);
    if (screenW <= 0 || screenH <= 0 || std::fabs(depth) < 1e-6f) return Fail(L);

    // Invert upper-left 3x3 of row-major CMatrix m (m._11..m._33).
    const double a00 = m[0], a01 = m[1], a02 = m[2];
    const double a10 = m[4], a11 = m[5], a12 = m[6];
    const double a20 = m[8], a21 = m[9], a22 = m[10];
    const double c00 = a11 * a22 - a12 * a21;
    const double c01 = -(a10 * a22 - a12 * a20);
    const double c02 = a10 * a21 - a11 * a20;
    const double c10 = -(a01 * a22 - a02 * a21);
    const double c11 = a00 * a22 - a02 * a20;
    const double c12 = -(a00 * a21 - a01 * a20);
    const double c20 = a01 * a12 - a02 * a11;
    const double c21 = -(a00 * a12 - a02 * a10);
    const double c22 = a00 * a11 - a01 * a10;
    const double det = a00 * c00 + a01 * c01 + a02 * c02;
    if (!std::isfinite(det) || std::fabs(det) < 1e-12) return Fail(L);
    const double invDet = 1.0 / det;

    // Reverse screen coordinates: view = (sx * depth / screenW, sy * depth / screenH, depth)
    const double vx = (static_cast<double>(sx) * depth / static_cast<double>(screenW)) - m[12];
    const double vy = (static_cast<double>(sy) * depth / static_cast<double>(screenH)) - m[13];
    const double vz = static_cast<double>(depth) - m[14];

    // Row-vector multiplication by transpose(cofactor) / det = inverse(M3).
    const double wx = (vx * c00 + vy * c01 + vz * c02) * invDet;
    const double wy = (vx * c10 + vy * c11 + vz * c12) * invDet;
    const double wz = (vx * c20 + vy * c21 + vz * c22) * invDet;
    if (!std::isfinite(wx) || !std::isfinite(wy) || !std::isfinite(wz)) return Fail(L);

    lua_pushnumber(L, wx);
    lua_pushnumber(L, wy);
    lua_pushnumber(L, wz);
    return 3;
}

int GetScreenFromWorldPosition(lua_State* L) {
    // CLuaWorldDefs::GetScreenFromWorldPosition (1163-1190) + CStaticFunctionDefinitions (6776-6798) + CGraphics (649-670).
    float wx = 0.f, wy = 0.f, wz = 0.f;
    if (!ReadVecValues(L, "getScreenFromWorldPosition", 1, wx, wy, wz)) return Fail(L);
    float tolerance = 0.f;
    if (!lua_isnoneornil(L, 4)) {
        if (!Finite(L, 4)) {
            LogFull("getScreenFromWorldPosition", "Expected number at argument 4");
            return Fail(L);
        }
        tolerance = static_cast<float>(lua_tonumber(L, 4));
    }
    if (!ReadBoolSlot(L, "getScreenFromWorldPosition", 5, true)) return Fail(L);
    const bool relative = ReadBoolDefault(L, 5, true);

    float m[16];
    if (!Game::CopyCameraViewMatrix(m)) return Fail(L);
    int screenW = 0, screenH = 0;
    ActiveVmScreenSize(screenW, screenH);
    if (screenW <= 0 || screenH <= 0) return Fail(L);

    const float vx = wz * m[8] + wy * m[4] + wx * m[0] + m[12];
    const float vy = wz * m[9] + wy * m[5] + wx * m[1] + m[13];
    const float vz = wz * m[10] + wy * m[6] + wx * m[2] + m[14];
    if (!(vz > 0.1f)) return Fail(L);

    const float fResW = static_cast<float>(screenW);
    const float fResH = static_cast<float>(screenH);
    const float sx = (vx / vz) * fResW;
    const float sy = (vy / vz) * fResH;
    const float tolX = std::clamp(relative ? tolerance * fResW : tolerance, 0.f, fResW * 10.f);
    const float tolY = std::clamp(relative ? tolerance * fResH : tolerance, 0.f, fResH * 10.f);
    if (sx >= -tolX && sx <= fResW + tolX && sy >= -tolY && sy <= fResH + tolY) {
        lua_pushnumber(L, sx);
        lua_pushnumber(L, sy);
        lua_pushnumber(L, vz);
        return 3;
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
