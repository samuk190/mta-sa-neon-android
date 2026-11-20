#include "EngineWorld.h"

#include "LuaVMStub.h"

extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>

namespace WorldRPG::Models {
namespace {

constexpr int kModdedCapacity[kPoolCount] = {
    32000, 140, 1200, 2500, 110, 30000, 5000, 5000, 16, 140, 64, 72600, 64, 32, 64, 74800, 90000, 16000, 4000, 16000,
};

bool g_asyncEnabled = false;
bool g_asyncForced = false;
float g_gamma = 0.95f;
float g_brightness = 1.03f;
float g_contrast = 1.0f;
float g_saturation = 1.0f;
bool g_gammaEnabled = false;
bool g_brightnessEnabled = false;
bool g_contrastEnabled = false;
bool g_saturationEnabled = false;
bool g_applyFullscreen = false;
bool g_applyWindowed = false;
std::uint32_t g_maxObjects = kMaxStreamedObjects / 2;
std::uint32_t g_maxLowLod = kMaxStreamedObjects - kMaxStreamedObjects / 2;

}  // namespace

int PoolDefaultCapacity(int pool) {
    if (pool < 0 || pool >= kPoolCount) return 0;
    return kModdedCapacity[pool];
}

PoolSetDecision DecidePoolCapacity(int pool, std::uint32_t size, std::uint32_t used) {
    if (size < static_cast<std::uint32_t>(PoolDefaultCapacity(pool))) return PoolSetDecision::BelowDefault;
    if (size < used) return PoolSetDecision::BelowUsed;
    if (pool != kBuildingPool && pool != kPointerSingleLinkPool) return PoolSetDecision::NotChangeable;
    return PoolSetDecision::Resize;
}

void SetAsyncLoading(bool enabled, bool forced) {
    g_asyncEnabled = enabled;
    g_asyncForced = forced;
}

bool AsyncLoadingEnabled() { return g_asyncEnabled; }

bool AsyncLoadingForced() { return g_asyncForced; }

float PostFXValue(PostFX type) {
    switch (type) {
        case PostFX::Gamma: return g_gamma;
        case PostFX::Brightness: return g_brightness;
        case PostFX::Contrast: return g_contrast;
        case PostFX::Saturation: return g_saturation;
    }
    return 0.f;
}

bool PostFXEnabled(PostFX type) {
    switch (type) {
        case PostFX::Gamma: return g_gammaEnabled;
        case PostFX::Brightness: return g_brightnessEnabled;
        case PostFX::Contrast: return g_brightnessEnabled;
        case PostFX::Saturation: return g_saturationEnabled;
    }
    return false;
}

int PostFXMode() { return g_applyFullscreen ? 1 : g_applyWindowed ? 2 : 0; }

void SetPostFXValue(PostFX type, float value) {
    switch (type) {
        case PostFX::Gamma: g_gamma = value; break;
        case PostFX::Brightness: g_brightness = value; break;
        case PostFX::Contrast: g_contrast = value; break;
        case PostFX::Saturation: g_saturation = value; break;
    }
}

void SetPostFXEnabled(PostFX type, bool enabled) {
    switch (type) {
        case PostFX::Gamma: g_gammaEnabled = enabled; break;
        case PostFX::Brightness: g_brightnessEnabled = enabled; break;
        case PostFX::Contrast: g_contrastEnabled = enabled; break;
        case PostFX::Saturation: g_saturationEnabled = enabled; break;
    }
}

void SetPostFXMode(bool fullscreen, bool windowed) {
    g_applyFullscreen = fullscreen;
    g_applyWindowed = windowed;
}

bool SetObjectStreamingLimits(std::uint32_t objects, std::uint32_t lowLod) {
    if (objects > kMaxStreamedObjects || lowLod > kMaxStreamedObjects - objects) return false;
    g_maxObjects = objects;
    g_maxLowLod = lowLod;
    return true;
}

std::uint32_t MaxStreamedObjects() { return g_maxObjects; }

std::uint32_t MaxLowLodObjects() { return g_maxLowLod; }

}  // namespace WorldRPG::Models

namespace WorldRPG::Script {
namespace {

const char* kPools[] = {
    "building", "ped", "object", "dummy", "vehicle", "col-model", "task", "event", "task-allocator", "ped-intelligence",
    "ped-attractor", "entry-info-node", "node-route", "patrol-route", "point-route", "pointer-double-link-pool",
    "pointer-single-link-pool", "env-map-material", "env-map-atomic", "spec-map-material",
};

std::string Bad(const char* function, const char* expected, int index, const std::string& got) {
    return std::string("Bad argument @ '") + function + "' [Expected " + expected + " at argument " +
           std::to_string(index) + ", got " + got + "]";
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
        case LUA_TSTRING: {
            size_t length = 0;
            const char* text = lua_tolstring(L, index, &length);
            std::string value(text ? text : "", text ? length : 0);
            if (value.size() > 10) {
                value.resize(10);
                value[7] = '.';
                value[8] = '.';
                value[9] = '.';
            }
            return "string (\"" + value + "\")";
        }
        case LUA_TTABLE: return "table";
        default: return lua_typename(L, lua_type(L, index));
    }
}

std::string Lower(const char* text) {
    std::string out = text ? text : "";
    for (char& ch : out) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (c >= 'A' && c <= 'Z') ch = static_cast<char>(c + 32);
    }
    return out;
}

bool ReadUint32(lua_State* L, int index, const char* function, std::uint32_t& out, std::string& error) {
    if (!lua_isnumber(L, index)) {
        error = Bad(function, "number", index, Got(L, index));
        return false;
    }
    const lua_Number number = lua_tonumber(L, index);
    if (std::isnan(number)) {
        error = Bad(function, "number", index, "NaN");
        return false;
    }
    if (std::isinf(number)) {
        error = Bad(function, "number", index, "inf");
        return false;
    }
    const lua_Number upper = std::ldexp(1.0, std::numeric_limits<std::uint32_t>::digits);
    if (number < 0.0 || number >= upper) {
        error = Bad(function, "number within the destination type range", index, "out-of-range number");
        return false;
    }
    out = static_cast<std::uint32_t>(number);
    return true;
}

bool ReadFloat(lua_State* L, int index, const char* function, float& out, std::string& error) {
    if (!lua_isnumber(L, index)) {
        error = Bad(function, "number", index, Got(L, index));
        return false;
    }
    const lua_Number number = lua_tonumber(L, index);
    if (std::isnan(number)) {
        error = Bad(function, "number", index, "NaN");
        return false;
    }
    if (std::isinf(number)) {
        error = Bad(function, "number", index, "inf");
        return false;
    }
    out = static_cast<float>(number);
    return true;
}

bool ReadPool(lua_State* L, const char* function, int& pool, std::string& error) {
    if (lua_type(L, 1) != LUA_TSTRING) {
        error = Bad(function, "ePools", 1, Got(L, 1));
        return false;
    }
    const std::string key = Lower(lua_tostring(L, 1));
    for (int i = 0; i < Models::kPoolCount; ++i) {
        if (key == kPools[i]) {
            pool = i;
            return true;
        }
    }
    error = Bad(function, "ePools", 1, Got(L, 1));
    return false;
}

bool ResolveModel(std::uint32_t supplied, std::uint32_t& runtime) {
    if (supplied > 65534u || supplied >= 42341u) return false;
    runtime = supplied;
    return true;
}

int GetPoolDefault(lua_State* L) {
    int pool = 0;
    std::string error;
    if (!ReadPool(L, "engineGetPoolDefaultCapacity", pool, error)) return luaL_error(L, "%s", error.c_str());
    lua_pushnumber(L, Models::PoolDefaultCapacity(pool));
    return 1;
}

int GetPoolCapacity(lua_State* L) {
    int pool = 0;
    std::string error;
    if (!ReadPool(L, "engineGetPoolCapacity", pool, error)) return luaL_error(L, "%s", error.c_str());
    LUA_LOGI("DIAG engineGetPoolCapacity: the live pool size is not hooked");
    lua_pushboolean(L, 0);
    return 1;
}

int GetPoolUsed(lua_State* L) {
    int pool = 0;
    std::string error;
    if (!ReadPool(L, "engineGetPoolUsedCapacity", pool, error)) return luaL_error(L, "%s", error.c_str());
    LUA_LOGI("DIAG engineGetPoolUsedCapacity: the live pool count is not hooked");
    lua_pushboolean(L, 0);
    return 1;
}

int SetPoolCapacity(lua_State* L) {
    int pool = 0;
    std::uint32_t size = 0;
    std::string error;
    if (!ReadPool(L, "engineSetPoolCapacity", pool, error)) return luaL_error(L, "%s", error.c_str());
    if (!ReadUint32(L, 2, "engineSetPoolCapacity", size, error)) return luaL_error(L, "%s", error.c_str());
    if (size < static_cast<std::uint32_t>(Models::PoolDefaultCapacity(pool))) {
        LUA_LOGI("WARNING: Cannot set the pool capacity to less than the default capacity.");
        lua_pushboolean(L, 0);
        return 1;
    }
    LUA_LOGI("DIAG engineSetPoolCapacity: the live used count is not hooked");
    lua_pushboolean(L, 0);
    return 1;
}

int SetAsync(lua_State* L) {
    if (lua_type(L, 1) != LUA_TBOOLEAN) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const bool enabled = lua_toboolean(L, 1) != 0;
    bool forced = false;
    const int forcedType = lua_type(L, 2);
    if (forcedType == LUA_TNONE || forcedType == LUA_TNIL) forced = false;
    else if (forcedType == LUA_TBOOLEAN) forced = lua_toboolean(L, 2) != 0;
    else {
        lua_pushboolean(L, 0);
        return 1;
    }
    Models::SetAsyncLoading(enabled, forced);
    lua_pushboolean(L, 1);
    return 1;
}

int PreloadArea(lua_State* L) {
    if (!lua_isnumber(L, 1)) return luaL_error(L, "%s", Bad("enginePreloadWorldArea", "vector3", 1, Got(L, 1)).c_str());
    float x = 0, y = 0, z = 0;
    std::string error;
    if (!ReadFloat(L, 1, "enginePreloadWorldArea", x, error) || !ReadFloat(L, 2, "enginePreloadWorldArea", y, error) ||
        !ReadFloat(L, 3, "enginePreloadWorldArea", z, error))
        return luaL_error(L, "%s", error.c_str());
    const int optionType = lua_type(L, 4);
    if (optionType != LUA_TNONE && optionType != LUA_TNIL) {
        if (optionType != LUA_TSTRING) return luaL_error(L, "%s", Bad("enginePreloadWorldArea", "preload-area-option", 4, Got(L, 4)).c_str());
        const std::string key = Lower(lua_tostring(L, 4));
        if (key != "models" && key != "collisions" && key != "all")
            return luaL_error(L, "%s", Bad("enginePreloadWorldArea", "preload-area-option", 4, Got(L, 4)).c_str());
    }
    (void)x;
    (void)y;
    (void)z;
    LUA_LOGI("DIAG enginePreloadWorldArea: CStreaming::LoadScene is not hooked");
    return 0;
}

int PreloadDirection(lua_State* L) {
    if (!lua_isnumber(L, 1)) return luaL_error(L, "%s", Bad("enginePreloadWorldAreaInDirection", "vector3", 1, Got(L, 1)).c_str());
    float x = 0, y = 0, z = 0, heading = 0;
    std::string error;
    if (!ReadFloat(L, 1, "enginePreloadWorldAreaInDirection", x, error) || !ReadFloat(L, 2, "enginePreloadWorldAreaInDirection", y, error) ||
        !ReadFloat(L, 3, "enginePreloadWorldAreaInDirection", z, error) || !ReadFloat(L, 4, "enginePreloadWorldAreaInDirection", heading, error))
        return luaL_error(L, "%s", error.c_str());
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || !std::isfinite(heading)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    LUA_LOGI("DIAG enginePreloadWorldAreaInDirection: CStreaming::LoadSceneInDirection is not hooked");
    lua_pushboolean(L, 0);
    return 1;
}

int Restream(lua_State* L) {
    const int type = lua_type(L, 1);
    if (type != LUA_TNONE && type != LUA_TNIL) {
        if (type != LUA_TSTRING) return luaL_error(L, "%s", Bad("engineRestream", "restream-option", 1, Got(L, 1)).c_str());
        const std::string key = Lower(lua_tostring(L, 1));
        if (key != "world" && key != "vehicles" && key != "peds" && key != "objects")
            return luaL_error(L, "%s", Bad("engineRestream", "restream-option", 1, Got(L, 1)).c_str());
    }
    LUA_LOGI("DIAG engineRestream: CClientGame::Restream is not hooked");
    return 0;
}

int RestreamModel(lua_State* L) {
    std::uint32_t supplied = 0;
    std::string error;
    if (!ReadUint32(L, 1, "engineRestreamModel", supplied, error)) return luaL_error(L, "%s", error.c_str());
    std::uint32_t runtime = 0;
    if (!ResolveModel(supplied, runtime)) return luaL_error(L, "Expected a valid model ID at argument 1");
    LUA_LOGI("DIAG engineRestreamModel: model type and entity restream are not hooked");
    lua_pushboolean(L, 0);
    return 1;
}

bool PostFXFromName(const char* text, Models::PostFX& type) {
    const std::string key = Lower(text);
    if (key == "gamma") type = Models::PostFX::Gamma;
    else if (key == "brightness") type = Models::PostFX::Brightness;
    else if (key == "contrast") type = Models::PostFX::Contrast;
    else if (key == "saturation") type = Models::PostFX::Saturation;
    else return false;
    return true;
}

int GetPostFXValue(lua_State* L) {
    if (lua_type(L, 1) != LUA_TSTRING) return luaL_error(L, "%s", Bad("getPostFXValue", "postfx-type", 1, Got(L, 1)).c_str());
    Models::PostFX type;
    if (!PostFXFromName(lua_tostring(L, 1), type)) return luaL_error(L, "%s", Bad("getPostFXValue", "postfx-type", 1, Got(L, 1)).c_str());
    lua_pushnumber(L, Models::PostFXValue(type));
    return 1;
}

int GetPostFXMode(lua_State* L) {
    (void)L;
    lua_pushnumber(L, Models::PostFXMode());
    return 1;
}

int IsPostFXEnabled(lua_State* L) {
    if (lua_type(L, 1) != LUA_TSTRING) return luaL_error(L, "%s", Bad("isPostFXEnabled", "postfx-type", 1, Got(L, 1)).c_str());
    Models::PostFX type;
    if (!PostFXFromName(lua_tostring(L, 1), type)) return luaL_error(L, "%s", Bad("isPostFXEnabled", "postfx-type", 1, Got(L, 1)).c_str());
    lua_pushboolean(L, Models::PostFXEnabled(type) ? 1 : 0);
    return 1;
}

int GetObjectLimits(lua_State* L) {
    (void)L;
    lua_pushnumber(L, Models::MaxStreamedObjects());
    lua_pushnumber(L, Models::MaxLowLodObjects());
    lua_pushnumber(L, Models::kMaxStreamedObjects);
    return 3;
}

int SetObjectLimits(lua_State* L) {
    std::uint32_t objects = 0;
    std::uint32_t lowLod = 0;
    std::string error;
    if (!ReadUint32(L, 1, "engineSetObjectStreamingLimits", objects, error)) return luaL_error(L, "%s", error.c_str());
    if (!ReadUint32(L, 2, "engineSetObjectStreamingLimits", lowLod, error)) return luaL_error(L, "%s", error.c_str());
    if (!Models::SetObjectStreamingLimits(objects, lowLod))
        return luaL_error(L, "Object and low-LOD object streaming limits must have a combined value between 0 and %d", Models::kMaxStreamedObjects);
    lua_pushboolean(L, 1);
    return 1;
}

int RestreamWorld(lua_State* L) {
    (void)L;
    LUA_LOGI("DIAG engineRestreamWorld: CClientGame::RestreamWorld is not hooked");
    lua_pushboolean(L, 0);
    return 1;
}

}  // namespace

void BindEngineWorldLua(lua_State* L) {
    const struct { const char* name; lua_CFunction fn; } binds[] = {
        {"engineGetPoolCapacity", GetPoolCapacity},
        {"engineGetPoolDefaultCapacity", GetPoolDefault},
        {"engineGetPoolUsedCapacity", GetPoolUsed},
        {"engineSetPoolCapacity", SetPoolCapacity},
        {"engineSetAsynchronousLoading", SetAsync},
        {"enginePreloadWorldArea", PreloadArea},
        {"enginePreloadWorldAreaInDirection", PreloadDirection},
        {"engineRestream", Restream},
        {"engineRestreamModel", RestreamModel},
        {"engineRestreamWorld", RestreamWorld},
        {"getPostFXValue", GetPostFXValue},
        {"getPostFXMode", GetPostFXMode},
        {"isPostFXEnabled", IsPostFXEnabled},
        {"engineGetObjectStreamingLimits", GetObjectLimits},
        {"engineSetObjectStreamingLimits", SetObjectLimits},
    };
    for (const auto& bind : binds) {
        lua_pushcfunction(L, bind.fn);
        lua_setglobal(L, bind.name);
    }
}

}  // namespace WorldRPG::Script
