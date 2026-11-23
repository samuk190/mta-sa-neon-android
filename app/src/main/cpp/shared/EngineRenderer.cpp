#include "EngineRenderer.h"

#include "LuaVMStub.h"

extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

#include <cmath>
#include <cstdint>
#include <string>

namespace WorldRPG::Models {
namespace {

DistantLights g_lights;
RendererStats g_stats;

}  // namespace

const DistantLights& GetDistantLights() { return g_lights; }

bool SetDistantLightDistance(float distance) {
    if (!std::isfinite(distance) || distance < kDistantLightMin || distance > kDistantLightMax) return false;
    g_lights.distance = distance;
    g_lights.automatic = false;
    return true;
}

void SetDistantLightsEnabled(bool enabled) { g_lights.enabled = enabled; }

void SetDistantLightsAutomatic(bool enabled) { g_lights.automatic = enabled; }

RendererStats GetRendererStats() { return g_stats; }

void ResetRendererStats() {
    g_stats.visibleEntityHighWater = g_stats.visibleEntities;
    g_stats.visibleLodHighWater = g_stats.visibleLods;
    g_stats.streamingRwObjectHighWater = g_stats.streamingRwObjects;
}

}  // namespace WorldRPG::Models

namespace WorldRPG::Script {
namespace {

std::string Bad(const char* function, const char* expected, int index, const std::string& got) {
    return std::string("Bad argument @ '") + function + "' [Expected " + expected + " at argument " +
           std::to_string(index) + ", got " + got + "]";
}

std::string Argument(const char* function, const char* reason) {
    return std::string("Bad argument @ '") + function + "' [" + reason + "]";
}

std::string Got(lua_State* L, int index) {
    switch (lua_type(L, index)) {
        case LUA_TNONE: return "none";
        case LUA_TNIL: return "nil";
        case LUA_TBOOLEAN: return lua_toboolean(L, index) ? "boolean (true)" : "boolean (false)";
        case LUA_TNUMBER: {
            const char* text = lua_tostring(L, index);
            return std::string("number (") + (text ? text : "") + ")";
        }
        case LUA_TSTRING: return std::string("string (\"") + (lua_tostring(L, index) ? lua_tostring(L, index) : "") + "\")";
        case LUA_TTABLE: return "table";
        default: return lua_typename(L, lua_type(L, index));
    }
}

bool ReadNumber(lua_State* L, int index, const char* function, lua_Number& out, std::string& error) {
    const int type = lua_type(L, index);
    if (type != LUA_TNUMBER && type != LUA_TSTRING) {
        error = Bad(function, "number", index, Got(L, index));
        return false;
    }
    if (!lua_isnumber(L, index)) {
        error = Argument(function, "Expected number, got non-convertible string");
        return false;
    }
    out = lua_tonumber(L, index);
    if (std::isnan(out)) {
        error = Argument(function, "Expected number, got NaN");
        return false;
    }
    return true;
}

bool ReadBool(lua_State* L, int index, const char* function, bool& out, std::string& error) {
    if (lua_type(L, index) != LUA_TBOOLEAN) {
        error = Bad(function, "bool", index, Got(L, index));
        return false;
    }
    out = lua_toboolean(L, index) != 0;
    return true;
}

int SetLightsEnabled(lua_State* L) {
    bool enabled = false;
    std::string error;
    if (!ReadBool(L, 1, "engineSetDistantLightsEnabled", enabled, error)) return luaL_error(L, "%s", error.c_str());
    Models::SetDistantLightsEnabled(enabled);
    lua_pushboolean(L, 1);
    return 1;
}

int SetLightDistance(lua_State* L) {
    lua_Number distance = 0;
    std::string error;
    if (!ReadNumber(L, 1, "engineSetDistantLightsDrawDistance", distance, error)) return luaL_error(L, "%s", error.c_str());
    lua_pushboolean(L, Models::SetDistantLightDistance(static_cast<float>(distance)) ? 1 : 0);
    return 1;
}

int SetLightAutomatic(lua_State* L) {
    bool enabled = false;
    std::string error;
    if (!ReadBool(L, 1, "engineSetDistantLightsAutomaticDrawDistance", enabled, error)) return luaL_error(L, "%s", error.c_str());
    Models::SetDistantLightsAutomatic(enabled);
    lua_pushboolean(L, 1);
    return 1;
}

int RebuildLights(lua_State* L) {
    (void)L;
    if (Models::GetDistantLights().enabled)
        LUA_LOGI("DIAG engineRebuildDistantLights: the distant-light list is not rebuilt");
    lua_pushboolean(L, 1);
    return 1;
}

int GetLightStats(lua_State* L) {
    const Models::DistantLights lights = Models::GetDistantLights();
    lua_newtable(L);
    lua_pushboolean(L, lights.enabled ? 1 : 0);
    lua_setfield(L, -2, "enabled");
    lua_pushnumber(L, 0);
    lua_setfield(L, -2, "definitions");
    lua_pushnumber(L, 0);
    lua_setfield(L, -2, "activeCoronas");
    lua_pushnumber(L, Models::kDistantLightCapacity);
    lua_setfield(L, -2, "coronaCapacity");
    lua_pushnumber(L, lights.distance);
    lua_setfield(L, -2, "drawDistance");
    lua_pushboolean(L, lights.automatic ? 1 : 0);
    lua_setfield(L, -2, "automaticDrawDistance");
    return 1;
}

void PushStat(lua_State* L, const char* name, std::uint32_t value) {
    lua_pushnumber(L, value);
    lua_setfield(L, -2, name);
}

int GetRendererStats(lua_State* L) {
    const Models::RendererStats stats = Models::GetRendererStats();
    lua_newtable(L);
    PushStat(L, "visibleEntities", stats.visibleEntities);
    PushStat(L, "visibleEntityHighWater", stats.visibleEntityHighWater);
    PushStat(L, "visibleEntityCapacity", Models::kVisibleEntityCapacity);
    PushStat(L, "visibleLods", stats.visibleLods);
    PushStat(L, "visibleLodHighWater", stats.visibleLodHighWater);
    PushStat(L, "visibleLodCapacity", Models::kVisibleLodCapacity);
    PushStat(L, "streamingRwObjects", stats.streamingRwObjects);
    PushStat(L, "streamingRwObjectHighWater", stats.streamingRwObjectHighWater);
    PushStat(L, "streamingRwObjectCapacity", Models::kStreamingObjectCapacity);
    return 1;
}

int ResetRendererStats(lua_State* L) {
    Models::ResetRendererStats();
    lua_pushboolean(L, 1);
    return 1;
}

int SetRadarTile(lua_State* L) {
    lua_Number column = 0;
    lua_Number row = 0;
    std::string error;
    if (!ReadNumber(L, 1, "engineSetRadarMapTile", column, error)) return luaL_error(L, "%s", error.c_str());
    if (!ReadNumber(L, 2, "engineSetRadarMapTile", row, error)) return luaL_error(L, "%s", error.c_str());
    if (lua_type(L, 3) != LUA_TUSERDATA) return luaL_error(L, "%s", Bad("engineSetRadarMapTile", "txd", 3, Got(L, 3)).c_str());
    (void)column;
    (void)row;
    lua_pushboolean(L, 0);
    return 1;
}

int ResetRadarTile(lua_State* L) {
    lua_Number column = 0;
    lua_Number row = 0;
    std::string error;
    if (!ReadNumber(L, 1, "engineResetRadarMapTile", column, error)) return luaL_error(L, "%s", error.c_str());
    if (!ReadNumber(L, 2, "engineResetRadarMapTile", row, error)) return luaL_error(L, "%s", error.c_str());
    const int columnInt = static_cast<int>(column);
    const int rowInt = static_cast<int>(row);
    if (columnInt < 0 || columnInt >= 40 || rowInt < 0 || rowInt >= 40) {
        lua_pushboolean(L, 0);
        return 1;
    }
    LUA_LOGI("DIAG engineResetRadarMapTile: the radar tile registry is not hooked");
    lua_pushboolean(L, 0);
    return 1;
}

int GetRadarStats(lua_State* L) {
    lua_newtable(L);
    lua_pushboolean(L, 0);
    lua_setfield(L, -2, "hooksInstalled");
    lua_pushnumber(L, 0);
    lua_setfield(L, -2, "registeredTiles");
    lua_pushnumber(L, 0);
    lua_setfield(L, -2, "loadedTiles");
    lua_pushnumber(L, 0);
    lua_setfield(L, -2, "failedTiles");
    lua_pushnumber(L, 0);
    lua_setfield(L, -2, "sourceBytes");
    return 1;
}

}  // namespace

void BindEngineRendererLua(lua_State* L) {
    const struct { const char* name; lua_CFunction fn; } binds[] = {
        {"engineGetRendererStats", GetRendererStats},
        {"engineResetRendererStats", ResetRendererStats},
        {"engineSetDistantLightsEnabled", SetLightsEnabled},
        {"engineSetDistantLightsDrawDistance", SetLightDistance},
        {"engineSetDistantLightsAutomaticDrawDistance", SetLightAutomatic},
        {"engineRebuildDistantLights", RebuildLights},
        {"engineGetDistantLightStats", GetLightStats},
        {"engineSetRadarMapTile", SetRadarTile},
        {"engineResetRadarMapTile", ResetRadarTile},
        {"engineGetRadarMapStats", GetRadarStats},
    };
    for (const auto& bind : binds) {
        lua_pushcfunction(L, bind.fn);
        lua_setglobal(L, bind.name);
    }
}

}  // namespace WorldRPG::Script
