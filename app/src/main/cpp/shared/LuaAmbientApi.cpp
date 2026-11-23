// Ambient ped model lease.
// Desktop CLuaWorldDefs.cpp 742-755 and CLuaFunctionParser.h vector3 parsing.
// CGameSA.cpp 1550-1587 returns false when the navigation zone array is missing.
// CGameSA.cpp 1661-1667 returns after restoring zones when the lease is inactive.
// setAmbientPedPopulationZoneState follows CLuaWorldDefs.cpp 764-852 and
// CGameSA.cpp 1863-1892. FindZoneByLabel is not hooked, so a known label returns false.
// The other ambient queries return the desktop failure for the same missing hooks.
// isWorldRPGMobilePopulation stays true until those hooks exist. The traffic scripts
// keep the puppet and the polyfills while it does.

#include "LuaAmbientApi.h"

#include "LuaVMStub.h"

#include "lua.hpp"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace WorldRPG::Script {
namespace {

bool g_diagUpdate = false;
bool g_diagReset = false;
bool g_diagZone = false;
bool g_diagBootstrap = false;
bool g_diagProfile = false;
bool g_diagSpawn = false;
bool g_diagCouple = false;
bool g_diagVehicleModel = false;
bool g_diagOccupant = false;
bool g_diagVehicleSpawn = false;
bool g_diagVisible = false;

#include "AmbientZoneLabels.inc"

bool KnownZoneLabel(const char* label) {
    for (const char* known : kAmbientZoneLabels) {
        if (std::strcmp(known, label) == 0) return true;
    }
    return false;
}

void Warn(const char* message) { LUA_LOGI("WARNING: %s", message); }

bool ReadOptionalByte(lua_State* L, const char* field, unsigned maximum) {
    lua_getfield(L, 2, field);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        return true;
    }
    const lua_Number value = lua_tonumber(L, -1);
    const bool valid = lua_isnumber(L, -1) != 0 && std::isfinite(value) && value == std::floor(value) && value >= 0 &&
                       value <= static_cast<lua_Number>(maximum);
    lua_pop(L, 1);
    return valid;
}

int SetAmbientPedPopulationZoneState(lua_State* L) {
    // CLuaWorldDefs.cpp 766-770. A bad shape warns and returns false. It does not raise.
    if (lua_isstring(L, 1) == 0 || lua_istable(L, 2) == 0) {
        Warn("Expected zone label and population state table");
        lua_pushboolean(L, 0);
        return 1;
    }
    if (!ReadOptionalByte(L, "populationType", 19) || !ReadOptionalByte(L, "races", 15) ||
        !ReadOptionalByte(L, "dealerStrength", 255)) {
        Warn("Invalid numeric population zone field");
        lua_pushboolean(L, 0);
        return 1;
    }

    lua_getfield(L, 2, "noCops");
    if (!lua_isnil(L, -1) && lua_type(L, -1) != LUA_TBOOLEAN) {
        lua_pop(L, 1);
        Warn("Population zone noCops must be boolean");
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pop(L, 1);

    lua_getfield(L, 2, "gangStrengths");
    if (!lua_isnil(L, -1)) {
        if (!lua_istable(L, -1)) {
            lua_pop(L, 1);
            Warn("Population zone gangStrengths must be a table");
            lua_pushboolean(L, 0);
            return 1;
        }
        for (int gangId = 0; gangId < 10; ++gangId) {
            lua_rawgeti(L, -1, gangId + 1);
            if (!lua_isnil(L, -1)) {
                const lua_Number value = lua_tonumber(L, -1);
                if (lua_isnumber(L, -1) == 0 || !std::isfinite(value) || value != std::floor(value) || value < 0 || value > 255) {
                    lua_pop(L, 2);
                    Warn("Invalid gang strength in population zone state");
                    lua_pushboolean(L, 0);
                    return 1;
                }
            }
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);

    // CGameSA.cpp 1865-1880, then 1890: FindZoneByLabel is not hooked.
    const char* raw = lua_tostring(L, 1);
    const std::string label = raw != nullptr ? raw : "";
    if (label.empty() || label.size() > 7) {
        lua_pushboolean(L, 0);
        return 1;
    }
    for (unsigned char character : label) {
        if (!(character >= 'A' && character <= 'Z') && !(character >= '0' && character <= '9')) {
            lua_pushboolean(L, 0);
            return 1;
        }
    }
    if (!KnownZoneLabel(label.c_str())) {
        lua_pushboolean(L, 0);
        return 1;
    }
    if (!g_diagZone) {
        g_diagZone = true;
        LUA_LOGE("DIAG setAmbientPedPopulationZoneState: FindZoneByLabel and the zone info array are not hooked");
    }
    lua_pushboolean(L, 0);
    return 1;
}

std::string ReadParameter(lua_State* L, int index) {
    switch (lua_type(L, index)) {
        case LUA_TNUMBER: {
            const char* text = lua_tostring(L, index);
            return std::string("number (") + (text != nullptr ? text : "") + ")";
        }
        case LUA_TSTRING: {
            size_t length = 0;
            const char* value = lua_tolstring(L, index, &length);
            std::string text(value != nullptr ? value : "", length);
            if (text.size() > 10) {
                text.resize(10);
                text[7] = '.';
                text[8] = '.';
                text[9] = '.';
            }
            for (unsigned char ch : text) {
                if (std::isprint(ch) == 0) return "string";
            }
            return "string (\"" + text + "\")";
        }
        case LUA_TBOOLEAN:
            return lua_toboolean(L, index) != 0 ? "boolean (true)" : "boolean (false)";
        case LUA_TNIL:
            return "nil";
        case LUA_TNONE:
            return "none";
        case LUA_TTABLE:
            return "table";
        case LUA_TFUNCTION:
            return "function";
        case LUA_TTHREAD:
            return "coroutine";
        default:
            return "userdata";
    }
}

int UpdateAmbientPedPopulationModels(lua_State* L) {
    // CLuaFunctionParser TypeMatch<CVector>: three lua_isnumber slots, or vector userdata.
    // This client has no CLuaVector userdata, so a userdata argument is a type error.
    const bool threeNumbers = lua_isnumber(L, 1) != 0 && lua_isnumber(L, 2) != 0 && lua_isnumber(L, 3) != 0;
    if (!threeNumbers) {
        const std::string got = ReadParameter(L, 1);
        return luaL_error(L, "Bad argument @ 'updateAmbientPedPopulationModels' [Expected vector3 at argument 1, got %s]",
                          got.c_str());
    }
    for (int i = 0; i < 3; ++i) {
        const double value = lua_tonumber(L, 1 + i);
        const char* got = nullptr;
        if (std::isnan(value)) got = "NaN";
        else if (std::isinf(value)) got = "inf";
        if (got != nullptr) {
            return luaL_error(L, "Bad argument @ 'updateAmbientPedPopulationModels' [Expected number at argument %d, got %s]",
                              1 + i, got);
        }
    }
    // CGameSA.cpp 1574-1587: no navigation zone at this origin.
    if (!g_diagUpdate) {
        g_diagUpdate = true;
        LUA_LOGE("DIAG updateAmbientPedPopulationModels: navigation zone array and StreamZoneModels are not hooked");
    }
    lua_pushboolean(L, 0);
    return 1;
}

int ResetAmbientPedPopulationModels(lua_State* L) {
    (void)L;
    // Inactive lease: RestoreAmbientPedPopulationZones and return. Lua still returns true.
    if (!g_diagReset) {
        g_diagReset = true;
        LUA_LOGE("DIAG resetAmbientPedPopulationModels: streaming lease and zone restore are not hooked");
    }
    lua_pushboolean(L, 1);
    return 1;
}

int FailBool(lua_State* L) {
    lua_pushboolean(L, 0);
    return 1;
}

int FailReason(lua_State* L, const char* reason) {
    lua_pushboolean(L, 0);
    lua_pushstring(L, reason);
    return 2;
}

void WarnArg(const char* function, const char* message) {
    char buf[320];
    std::snprintf(buf, sizeof(buf), "Bad argument @ '%s' [%s]", function, message);
    Warn(buf);
}

bool RequireOrigin(lua_State* L, const char* function) {
    if (lua_isnumber(L, 1) == 0 || lua_isnumber(L, 2) == 0 || lua_isnumber(L, 3) == 0) {
        const std::string got = ReadParameter(L, 1);
        char buf[200];
        std::snprintf(buf, sizeof(buf), "Expected vector3 at argument 1, got %s", got.c_str());
        WarnArg(function, buf);
        return false;
    }
    for (int i = 0; i < 3; ++i) {
        const double value = lua_tonumber(L, 1 + i);
        if (std::isnan(value) || std::isinf(value)) {
            char buf[80];
            std::snprintf(buf, sizeof(buf), "Expected number at argument %d, got %s", 1 + i, std::isnan(value) ? "NaN" : "inf");
            WarnArg(function, buf);
            return false;
        }
    }
    return true;
}

bool ReadOptionalNumber(lua_State* L, const char* function, int index, double fallback, double& out) {
    if (lua_isnoneornil(L, index)) {
        out = fallback;
        return true;
    }
    if (lua_isnumber(L, index) == 0) {
        const std::string got = ReadParameter(L, index);
        char buf[180];
        std::snprintf(buf, sizeof(buf), "Expected number at argument %d, got %s", index, got.c_str());
        WarnArg(function, buf);
        return false;
    }
    const double value = lua_tonumber(L, index);
    if (std::isnan(value) || std::isinf(value)) {
        char buf[80];
        std::snprintf(buf, sizeof(buf), "Expected number at argument %d, got %s", index, std::isnan(value) ? "NaN" : "inf");
        WarnArg(function, buf);
        return false;
    }
    out = value;
    return true;
}

bool RequireNumber(lua_State* L, const char* function, int index, double& out) {
    if (lua_isnoneornil(L, index)) {
        const std::string got = ReadParameter(L, index);
        char buf[180];
        std::snprintf(buf, sizeof(buf), "Expected number at argument %d, got %s", index, got.c_str());
        WarnArg(function, buf);
        return false;
    }
    return ReadOptionalNumber(L, function, index, 0, out);
}

bool ReadOptionalString(lua_State* L, const char* function, int index, const char* fallback, std::string& out) {
    if (lua_isnoneornil(L, index)) {
        out = fallback;
        return true;
    }
    if (lua_type(L, index) != LUA_TSTRING) {
        const std::string got = ReadParameter(L, index);
        char buf[180];
        std::snprintf(buf, sizeof(buf), "Expected string at argument %d, got %s", index, got.c_str());
        WarnArg(function, buf);
        return false;
    }
    const char* text = lua_tostring(L, index);
    out = text != nullptr ? text : "";
    return true;
}

bool AmbientEngineHooksReady() { return false; }

int IsWorldRPGMobilePopulation(lua_State* L) {
    // Phone signal for the traffic scripts. Desktop clients do not have this name.
    // It stays true until FindZoneByLabel, popcycle streaming, and CCamera::IsSphereVisible exist.
    lua_pushboolean(L, AmbientEngineHooksReady() ? 0 : 1);
    return 1;
}

int ResetAmbientPedPopulationZonesToBootstrap(lua_State* L) {
    (void)L;
    // CGameSA.cpp 1831-1841. FindZoneByLabel is not hooked, so the first label fails.
    if (!g_diagBootstrap) {
        g_diagBootstrap = true;
        LUA_LOGE("DIAG resetAmbientPedPopulationZonesToBootstrap: FindZoneByLabel and the zone info array are not hooked");
    }
    return FailBool(L);
}

int GetAmbientPedPopulationProfile(lua_State* L) {
    (void)L;
    // CGameSA.cpp 2179-2180. UpdateAmbientPedPopulationModels never marks the lease active.
    if (!g_diagProfile) {
        g_diagProfile = true;
        LUA_LOGE("DIAG getAmbientPedPopulationProfile: ambient models are inactive");
    }
    return FailReason(L, "inactive");
}

int GetAmbientPedSpawnCandidate(lua_State* L) {
    if (!RequireOrigin(L, "getAmbientPedSpawnCandidate")) return FailBool(L);
    std::string selection;
    double gang = -1;
    if (!ReadOptionalString(L, "getAmbientPedSpawnCandidate", 4, "auto", selection) ||
        !ReadOptionalNumber(L, "getAmbientPedSpawnCandidate", 5, -1, gang))
        return FailBool(L);
    if (selection != "auto" && selection != "civilian" && selection != "gang" && selection != "dealer" && selection != "cop") {
        Warn("Population selection must be auto, civilian, gang, dealer or cop");
        return FailBool(L);
    }
    const bool gangSelection = selection == "gang";
    if ((gangSelection && (gang < 0 || gang > 7 || gang != std::floor(gang))) || (!gangSelection && gang != -1)) {
        Warn("Gang selection requires a gang ID from 0 to 7; other selections require -1");
        return FailBool(L);
    }
    // CGameSA.cpp 2628-2631. The creation-distance multipliers are not hooked.
    if (!g_diagSpawn) {
        g_diagSpawn = true;
        LUA_LOGE("DIAG getAmbientPedSpawnCandidate: ped creation distance multipliers are not hooked");
    }
    return FailReason(L, "invalid-origin");
}

int GetAmbientPedGangGroupCandidate(lua_State* L) {
    if (!RequireOrigin(L, "getAmbientPedGangGroupCandidate")) return FailBool(L);
    double gang = 0;
    double maxMembers = 4;
    if (!RequireNumber(L, "getAmbientPedGangGroupCandidate", 4, gang) ||
        !ReadOptionalNumber(L, "getAmbientPedGangGroupCandidate", 5, 4, maxMembers))
        return FailBool(L);
    if (gang < 0 || gang > 7 || gang != std::floor(gang) || maxMembers < 2 || maxMembers > 4 || maxMembers != std::floor(maxMembers)) {
        Warn("Gang groups require gang 0..7 and maxMembers 2..4");
        return FailBool(L);
    }
    // The anchor query is GetAmbientPedSpawnCandidateForPopulation, which stops at the same multiplier check.
    return FailReason(L, "invalid-origin");
}

int GetAmbientPedCivilianCoupleCandidate(lua_State* L) {
    if (!RequireOrigin(L, "getAmbientPedCivilianCoupleCandidate")) return FailBool(L);
    // CGameSA.cpp 2936. This port is not the US 1.0 PC game version.
    if (!g_diagCouple) {
        g_diagCouple = true;
        LUA_LOGE("DIAG getAmbientPedCivilianCoupleCandidate: the US 1.0 couple selector is not hooked");
    }
    return FailReason(L, "invalid-origin");
}

int GetAmbientVehicleModelCandidate(lua_State* L) {
    (void)L;
    // CGameSA.cpp 2285-2286. The current popcycle zone info pointer is not hooked.
    if (!g_diagVehicleModel) {
        g_diagVehicleModel = true;
        LUA_LOGE("DIAG getAmbientVehicleModelCandidate: popcycle zone info is not hooked");
    }
    return FailReason(L, "population-unavailable");
}

int GetAmbientVehicleOccupantModelCandidate(lua_State* L) {
    double model = 0;
    double maximum = 4;
    if (!RequireNumber(L, "getAmbientVehicleOccupantModelCandidate", 1, model)) return FailBool(L);
    if (!ReadOptionalNumber(L, "getAmbientVehicleOccupantModelCandidate", 2, 4, maximum)) return FailBool(L);
    if (maximum == 0 || maximum > 4 || maximum != std::floor(maximum)) return FailBool(L);
    if (model < 400 || model > 611 || model != std::floor(model)) return FailBool(L);
    // CGameSA.cpp 2332-2334. CModelInfo is not hooked.
    if (!g_diagOccupant) {
        g_diagOccupant = true;
        LUA_LOGE("DIAG getAmbientVehicleOccupantModelCandidate: vehicle model info is not hooked");
    }
    return FailBool(L);
}

int GetAmbientVehicleSpawnCandidate(lua_State* L) {
    if (!RequireOrigin(L, "getAmbientVehicleSpawnCandidate")) return FailBool(L);
    double model = 0;
    if (!RequireNumber(L, "getAmbientVehicleSpawnCandidate", 4, model)) return FailBool(L);
    // CGameSA.cpp 2412-2414. GetModelInfo is not hooked, so every model is unsupported.
    if (!g_diagVehicleSpawn) {
        g_diagVehicleSpawn = true;
        LUA_LOGE("DIAG getAmbientVehicleSpawnCandidate: vehicle model info and the road path are not hooked");
    }
    return FailReason(L, "unsupported-model");
}

int IsAmbientPedSphereVisible(lua_State* L) {
    if (lua_isnumber(L, 1) == 0 || lua_isnumber(L, 2) == 0 || lua_isnumber(L, 3) == 0) {
        const std::string got = ReadParameter(L, 1);
        return luaL_error(L, "Bad argument @ 'isAmbientPedSphereVisible' [Expected vector3 at argument 1, got %s]", got.c_str());
    }
    for (int i = 0; i < 3; ++i) {
        const double value = lua_tonumber(L, 1 + i);
        if (std::isnan(value) || std::isinf(value)) {
            return luaL_error(L, "Bad argument @ 'isAmbientPedSphereVisible' [Expected number at argument %d, got %s]", 1 + i,
                              std::isnan(value) ? "NaN" : "inf");
        }
    }
    if (lua_isnumber(L, 4) == 0) {
        const std::string got = ReadParameter(L, 4);
        return luaL_error(L, "Bad argument @ 'isAmbientPedSphereVisible' [Expected number at argument 4, got %s]", got.c_str());
    }
    const double radius = lua_tonumber(L, 4);
    if (std::isnan(radius) || std::isinf(radius)) {
        return luaL_error(L, "Bad argument @ 'isAmbientPedSphereVisible' [Expected number at argument 4, got %s]",
                          std::isnan(radius) ? "NaN" : "inf");
    }
    // CGameSA.cpp 2274-2277. An out-of-range radius is not visible. A valid query needs the camera.
    if (radius < 0.0 || radius > 100.0) return FailBool(L);
    if (!g_diagVisible) {
        g_diagVisible = true;
        LUA_LOGE("DIAG isAmbientPedSphereVisible: CCamera::IsSphereVisible is not hooked");
    }
    return FailBool(L);
}

void Register(lua_State* L) {
    lua_pushcfunction(L, UpdateAmbientPedPopulationModels);
    lua_setglobal(L, "updateAmbientPedPopulationModels");
    lua_pushcfunction(L, ResetAmbientPedPopulationModels);
    lua_setglobal(L, "resetAmbientPedPopulationModels");
    lua_pushcfunction(L, SetAmbientPedPopulationZoneState);
    lua_setglobal(L, "setAmbientPedPopulationZoneState");
    lua_pushcfunction(L, ResetAmbientPedPopulationZonesToBootstrap);
    lua_setglobal(L, "resetAmbientPedPopulationZonesToBootstrap");
    lua_pushcfunction(L, GetAmbientPedPopulationProfile);
    lua_setglobal(L, "getAmbientPedPopulationProfile");
    lua_pushcfunction(L, GetAmbientPedSpawnCandidate);
    lua_setglobal(L, "getAmbientPedSpawnCandidate");
    lua_pushcfunction(L, GetAmbientPedGangGroupCandidate);
    lua_setglobal(L, "getAmbientPedGangGroupCandidate");
    lua_pushcfunction(L, GetAmbientPedCivilianCoupleCandidate);
    lua_setglobal(L, "getAmbientPedCivilianCoupleCandidate");
    lua_pushcfunction(L, GetAmbientVehicleModelCandidate);
    lua_setglobal(L, "getAmbientVehicleModelCandidate");
    lua_pushcfunction(L, GetAmbientVehicleOccupantModelCandidate);
    lua_setglobal(L, "getAmbientVehicleOccupantModelCandidate");
    lua_pushcfunction(L, GetAmbientVehicleSpawnCandidate);
    lua_setglobal(L, "getAmbientVehicleSpawnCandidate");
    lua_pushcfunction(L, IsAmbientPedSphereVisible);
    lua_setglobal(L, "isAmbientPedSphereVisible");
    lua_pushcfunction(L, IsWorldRPGMobilePopulation);
    lua_setglobal(L, "isWorldRPGMobilePopulation");
}

}  // namespace

void InstallAmbientLua(lua_State* L) { Register(L); }

}  // namespace WorldRPG::Script
