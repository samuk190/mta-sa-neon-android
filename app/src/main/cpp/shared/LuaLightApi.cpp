// Point lights and searchlights.
// Desktop CLuaPointLightDefs.cpp, CLuaSearchLightDefs.cpp,
// CStaticFunctionDefinitions.cpp 10215-10317.
// CPointLights::RenderHeliLight and the point-light draw are not called.
// createLight reads createsShadow and does not pass it to CreateLight.

#include "LuaLightApi.h"

#include "LuaApiExtra.h"
#include "LuaVMStub.h"

#include "lua.hpp"

#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace WorldRPG::Script {
namespace {

struct Light {
    uint32_t id = 0;
    bool search = false;
    int mode = 0;
    float x = 0, y = 0, z = 0;
    float radius = 3;
    int r = 255, g = 0, b = 0, a = 0;
    float dx = 0, dy = 0, dz = 0;
    float ex = 0, ey = 0, ez = 0;
    float endRadius = 0;
    bool renderSpot = true;
};

std::unordered_map<uint32_t, Light> g_lights;
bool g_diagLight = false;
bool g_diagSearch = false;

int Fail(lua_State* L) {
    lua_pushboolean(L, 0);
    return 1;
}

void Warn(const std::string& message) { LUA_LOGI("WARNING: %s", message.c_str()); }

std::string Got(lua_State* L, int index) {
    const int type = lua_type(L, index);
    if (type == LUA_TNUMBER) {
        const char* text = lua_tostring(L, index);
        return std::string("number '") + (text ? text : "0") + "'";
    }
    if (type == LUA_TBOOLEAN) return "boolean";
    if (type == LUA_TSTRING) {
        const char* text = lua_tostring(L, index);
        return std::string("string '") + (text ? text : "") + "'";
    }
    if (type == LUA_TNIL) return "nil";
    if (type == LUA_TNONE) return "none";
    if (type == LUA_TTABLE) {
        lua_getfield(L, index, "element");
        const bool element = lua_toboolean(L, -1) != 0;
        lua_pop(L, 1);
        if (!element) return "table";
        const uint32_t id = ElementIdOf(L, index);
        if (!KnownClientElement(id)) return "destroyed element";
        auto found = g_lights.find(id);
        if (found != g_lights.end()) return found->second.search ? "searchlight" : "light";
        const int top = lua_gettop(L);
        lua_getglobal(L, "getElementType");
        lua_pushvalue(L, index);
        std::string name = "element";
        if (lua_isfunction(L, -2) && lua_pcall(L, 1, 1, 0) == 0) {
            if (const char* text = lua_tostring(L, -1)) name = text;
        }
        lua_settop(L, top);
        return name;
    }
    return "userdata";
}

int Bad(lua_State* L, const char* function, const std::string& detail) {
    Warn(std::string("Bad argument @ '") + function + "' [" + detail + "]");
    return Fail(L);
}

bool Finite(lua_State* L, int index, float& out) {
    if (!lua_isnumber(L, index)) return false;
    const lua_Number number = lua_tonumber(L, index);
    if (!std::isfinite(number)) return false;
    out = static_cast<float>(number);
    return true;
}

bool OptionalFloat(lua_State* L, const char* function, int index, float fallback, float& out) {
    if (lua_isnoneornil(L, index)) {
        out = fallback;
        return true;
    }
    if (!Finite(L, index, out)) return Bad(L, function, "Expected number at argument " + std::to_string(index) + ", got " + Got(L, index)), false;
    return true;
}

bool ByteChannel(lua_State* L, const char* function, int index, int fallback, int& out) {
    if (lua_isnoneornil(L, index)) {
        out = fallback;
        return true;
    }
    float value = 0;
    if (!Finite(L, index, value)) return Bad(L, function, "Expected number at argument " + std::to_string(index) + ", got " + Got(L, index)), false;
    if (value < 0) return Bad(L, function, "Expected positive value, got negative"), false;
    if (value > 255)
        return Bad(L, function, "Expected number within the destination type range at argument " + std::to_string(index) + ", got out-of-range number"), false;
    out = static_cast<int>(value);
    return true;
}

Light* Find(uint32_t id, bool search) {
    auto found = g_lights.find(id);
    if (found == g_lights.end() || found->second.search != search || !KnownClientElement(id)) {
        if (found != g_lights.end() && !KnownClientElement(id)) g_lights.erase(found);
        return nullptr;
    }
    return &found->second;
}

int Miss(lua_State* L, const char* function, const char* expected) {
    return Bad(L, function, std::string("Expected ") + expected + " at argument 1, got " + Got(L, 1));
}

int CreateLight(lua_State* L) {
    // CLuaPointLightDefs::CreateLight (56-102). Radius defaults to 3, color to red, direction to 0.
    float modeNumber = 0;
    if (!Finite(L, 1, modeNumber) || modeNumber < static_cast<float>(INT32_MIN) || modeNumber > static_cast<float>(INT32_MAX))
        return Bad(L, "createLight", "Expected number at argument 1, got " + Got(L, 1));
    float x = 0, y = 0, z = 0, radius = 3, dx = 0, dy = 0, dz = 0;
    if (!Finite(L, 2, x) || !Finite(L, 3, y) || !Finite(L, 4, z))
        return Bad(L, "createLight", "Expected vector3 at argument 2, got " + Got(L, 2));
    if (!OptionalFloat(L, "createLight", 5, 3.f, radius)) return 1;
    int r = 255, g = 0, b = 0;
    if (!ByteChannel(L, "createLight", 6, 255, r)) return 1;
    if (!ByteChannel(L, "createLight", 7, 0, g)) return 1;
    if (!ByteChannel(L, "createLight", 8, 0, b)) return 1;
    if (!lua_isnoneornil(L, 9)) {
        if (!Finite(L, 9, dx) || !Finite(L, 10, dy) || !Finite(L, 11, dz))
            return Bad(L, "createLight", "Expected vector3 at argument 9, got " + Got(L, 9));
    }
    if (!lua_isnoneornil(L, 12) && lua_type(L, 12) != LUA_TBOOLEAN)
        return Bad(L, "createLight", "Expected bool at argument 12, got " + Got(L, 12));
    const uint32_t id = CreateResourceElement("light");
    if (!id) return Fail(L);
    Light light;
    light.id = id;
    light.mode = static_cast<int>(modeNumber);
    light.x = x;
    light.y = y;
    light.z = z;
    light.radius = radius;
    light.r = r;
    light.g = g;
    light.b = b;
    light.dx = dx;
    light.dy = dy;
    light.dz = dz;
    g_lights[id] = light;
    if (!g_diagLight) {
        g_diagLight = true;
        LUA_LOGE("DIAG createLight: CPointLights is not drawn");
    }
    PushElement(L, id);
    return 1;
}

int GetLightType(lua_State* L) {
    Light* light = Find(ElementIdOf(L, 1), false);
    if (!light) return Miss(L, "getLightType", "light");
    lua_pushnumber(L, light->mode);
    return 1;
}

int GetLightRadius(lua_State* L) {
    Light* light = Find(ElementIdOf(L, 1), false);
    if (!light) return Miss(L, "getLightRadius", "light");
    lua_pushnumber(L, light->radius);
    return 1;
}

int GetLightColor(lua_State* L) {
    Light* light = Find(ElementIdOf(L, 1), false);
    if (!light) return Miss(L, "getLightColor", "light");
    lua_pushnumber(L, light->r);
    lua_pushnumber(L, light->g);
    lua_pushnumber(L, light->b);
    return 3;
}

int GetLightDirection(lua_State* L) {
    Light* light = Find(ElementIdOf(L, 1), false);
    if (!light) return Miss(L, "getLightDirection", "light");
    lua_pushnumber(L, light->dx);
    lua_pushnumber(L, light->dy);
    lua_pushnumber(L, light->dz);
    return 3;
}

int SetLightRadius(lua_State* L) {
    Light* light = Find(ElementIdOf(L, 1), false);
    if (!light) return Miss(L, "setLightRadius", "light");
    float radius = 0;
    if (!Finite(L, 2, radius)) return Bad(L, "setLightRadius", "Expected number at argument 2, got " + Got(L, 2));
    light->radius = radius;
    lua_pushboolean(L, 1);
    return 1;
}

int SetLightColor(lua_State* L) {
    Light* light = Find(ElementIdOf(L, 1), false);
    if (!light) return Miss(L, "setLightColor", "light");
    int r = 0, g = 0, b = 0;
    if (!ByteChannel(L, "setLightColor", 2, 0, r) || lua_isnoneornil(L, 2)) {
        if (lua_isnoneornil(L, 2)) return Bad(L, "setLightColor", "Expected number at argument 2, got " + Got(L, 2));
        return 1;
    }
    if (!ByteChannel(L, "setLightColor", 3, 0, g) || lua_isnoneornil(L, 3)) {
        if (lua_isnoneornil(L, 3)) return Bad(L, "setLightColor", "Expected number at argument 3, got " + Got(L, 3));
        return 1;
    }
    if (!ByteChannel(L, "setLightColor", 4, 0, b) || lua_isnoneornil(L, 4)) {
        if (lua_isnoneornil(L, 4)) return Bad(L, "setLightColor", "Expected number at argument 4, got " + Got(L, 4));
        return 1;
    }
    light->r = r;
    light->g = g;
    light->b = b;
    lua_pushboolean(L, 1);
    return 1;
}

int SetLightDirection(lua_State* L) {
    Light* light = Find(ElementIdOf(L, 1), false);
    if (!light) return Miss(L, "setLightDirection", "light");
    float x = 0, y = 0, z = 0;
    if (!Finite(L, 2, x) || !Finite(L, 3, y) || !Finite(L, 4, z))
        return Bad(L, "setLightDirection", "Expected vector3 at argument 2, got " + Got(L, 2));
    light->dx = x;
    light->dy = y;
    light->dz = z;
    lua_pushboolean(L, 1);
    return 1;
}

int CreateSearchLight(lua_State* L) {
    // CLuaSearchLightDefs::CreateSearchLight (59-98). renderSpot defaults to true.
    float sx = 0, sy = 0, sz = 0, ex = 0, ey = 0, ez = 0, startRadius = 0, endRadius = 0;
    if (!Finite(L, 1, sx) || !Finite(L, 2, sy) || !Finite(L, 3, sz))
        return Bad(L, "createSearchLight", "Expected vector3 at argument 1, got " + Got(L, 1));
    if (!Finite(L, 4, ex) || !Finite(L, 5, ey) || !Finite(L, 6, ez))
        return Bad(L, "createSearchLight", "Expected vector3 at argument 4, got " + Got(L, 4));
    if (!Finite(L, 7, startRadius)) return Bad(L, "createSearchLight", "Expected number at argument 7, got " + Got(L, 7));
    if (!Finite(L, 8, endRadius)) return Bad(L, "createSearchLight", "Expected number at argument 8, got " + Got(L, 8));
    bool renderSpot = true;
    if (!lua_isnoneornil(L, 9)) {
        if (lua_type(L, 9) != LUA_TBOOLEAN) return Bad(L, "createSearchLight", "Expected bool at argument 9, got " + Got(L, 9));
        renderSpot = lua_toboolean(L, 9) != 0;
    }
    const uint32_t id = CreateResourceElement("searchlight");
    if (!id) return Fail(L);
    Light light;
    light.id = id;
    light.search = true;
    light.x = sx;
    light.y = sy;
    light.z = sz;
    light.ex = ex;
    light.ey = ey;
    light.ez = ez;
    light.radius = startRadius;
    light.endRadius = endRadius;
    light.renderSpot = renderSpot;
    light.r = 200;
    light.g = 200;
    light.b = 255;
    light.a = 0;
    g_lights[id] = light;
    if (!g_diagSearch) {
        g_diagSearch = true;
        LUA_LOGE("DIAG createSearchLight: CPointLights::RenderHeliLight is not hooked");
    }
    PushElement(L, id);
    return 1;
}

int GetSearchPos(lua_State* L, const char* function, bool end) {
    Light* light = Find(ElementIdOf(L, 1), true);
    if (!light) return Miss(L, function, "searchlight");
    if (end) {
        lua_pushnumber(L, light->ex);
        lua_pushnumber(L, light->ey);
        lua_pushnumber(L, light->ez);
    } else {
        lua_pushnumber(L, light->x);
        lua_pushnumber(L, light->y);
        lua_pushnumber(L, light->z);
    }
    return 3;
}

int GetSearchLightStartPosition(lua_State* L) { return GetSearchPos(L, "getSearchLightStartPosition", false); }
int GetSearchLightEndPosition(lua_State* L) { return GetSearchPos(L, "getSearchLightEndPosition", true); }

int GetSearchLightStartRadius(lua_State* L) {
    Light* light = Find(ElementIdOf(L, 1), true);
    if (!light) return Miss(L, "getSearchLightStartRadius", "searchlight");
    lua_pushnumber(L, light->radius);
    return 1;
}

int GetSearchLightEndRadius(lua_State* L) {
    Light* light = Find(ElementIdOf(L, 1), true);
    if (!light) return Miss(L, "getSearchLightEndRadius", "searchlight");
    lua_pushnumber(L, light->endRadius);
    return 1;
}

int Hard(lua_State* L, const char* function, const std::string& detail) {
    return luaL_error(L, "Bad argument @ '%s' [%s]", function, detail.c_str());
}

int GetSearchLightColor(lua_State* L) {
    // ArgumentParser. A non-searchlight raises.
    Light* light = Find(ElementIdOf(L, 1), true);
    if (!light) return Hard(L, "getSearchLightColor", "Expected searchlight at argument 1, got " + Got(L, 1));
    lua_pushnumber(L, light->r);
    lua_pushnumber(L, light->g);
    lua_pushnumber(L, light->b);
    lua_pushnumber(L, light->a);
    return 4;
}

int SetSearchLightColor(lua_State* L) {
    // SColor is one tocolor number, 0xAARRGGBB.
    Light* light = Find(ElementIdOf(L, 1), true);
    if (!light) return Hard(L, "setSearchLightColor", "Expected searchlight at argument 1, got " + Got(L, 1));
    if (!lua_isnumber(L, 2)) return Hard(L, "setSearchLightColor", "Expected colour at argument 2, got " + Got(L, 2));
    const lua_Number number = lua_tonumber(L, 2);
    if (std::isnan(number)) return Hard(L, "setSearchLightColor", "Expected number at argument 2, got NaN");
    if (!std::isfinite(number)) return Hard(L, "setSearchLightColor", "Expected number at argument 2, got inf");
    const uint32_t packed = static_cast<uint32_t>(static_cast<int64_t>(number));
    light->b = static_cast<int>(packed & 255u);
    light->g = static_cast<int>((packed >> 8) & 255u);
    light->r = static_cast<int>((packed >> 16) & 255u);
    light->a = static_cast<int>((packed >> 24) & 255u);
    return 0;
}

int SetSearchPos(lua_State* L, const char* function, bool end) {
    Light* light = Find(ElementIdOf(L, 1), true);
    if (!light) return Miss(L, function, "searchlight");
    float x = 0, y = 0, z = 0;
    if (!Finite(L, 2, x) || !Finite(L, 3, y) || !Finite(L, 4, z))
        return Bad(L, function, "Expected vector3 at argument 2, got " + Got(L, 2));
    if (end) {
        light->ex = x;
        light->ey = y;
        light->ez = z;
    } else {
        light->x = x;
        light->y = y;
        light->z = z;
    }
    lua_pushboolean(L, 1);
    return 1;
}

int SetSearchLightStartPosition(lua_State* L) { return SetSearchPos(L, "setSearchLightStartPosition", false); }
int SetSearchLightEndPosition(lua_State* L) { return SetSearchPos(L, "setSearchLightEndPosition", true); }

int SetSearchRadius(lua_State* L, const char* function, bool end) {
    Light* light = Find(ElementIdOf(L, 1), true);
    if (!light) return Miss(L, function, "searchlight");
    float radius = 0;
    if (!Finite(L, 2, radius)) return Bad(L, function, "Expected number at argument 2, got " + Got(L, 2));
    if (end) light->endRadius = radius;
    else light->radius = radius;
    lua_pushboolean(L, 1);
    return 1;
}

int SetSearchLightStartRadius(lua_State* L) { return SetSearchRadius(L, "setSearchLightStartRadius", false); }
int SetSearchLightEndRadius(lua_State* L) { return SetSearchRadius(L, "setSearchLightEndRadius", true); }

int GetElementPositionLight(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    auto found = g_lights.find(id);
    if (found == g_lights.end() || !KnownClientElement(id)) {
        if (!lua_isfunction(L, lua_upvalueindex(1))) return Fail(L);
        const int count = lua_gettop(L);
        lua_pushvalue(L, lua_upvalueindex(1));
        lua_insert(L, 1);
        lua_call(L, count, LUA_MULTRET);
        return lua_gettop(L);
    }
    lua_pushnumber(L, found->second.x);
    lua_pushnumber(L, found->second.y);
    lua_pushnumber(L, found->second.z);
    return 3;
}

int SetElementPositionLight(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    auto found = g_lights.find(id);
    if (found == g_lights.end() || !KnownClientElement(id)) {
        if (!lua_isfunction(L, lua_upvalueindex(1))) return Fail(L);
        const int count = lua_gettop(L);
        lua_pushvalue(L, lua_upvalueindex(1));
        lua_insert(L, 1);
        lua_call(L, count, LUA_MULTRET);
        return lua_gettop(L);
    }
    float x = 0, y = 0, z = 0;
    if (!Finite(L, 2, x) || !Finite(L, 3, y) || !Finite(L, 4, z)) return Fail(L);
    // CClientSearchLight::SetPosition is empty. A point light stores the vector.
    if (!found->second.search) {
        found->second.x = x;
        found->second.y = y;
        found->second.z = z;
    }
    lua_pushboolean(L, 1);
    return 1;
}

void Register(lua_State* L) {
    g_lights.clear();
    const struct { const char* name; lua_CFunction fn; } binds[] = {
        {"createLight", CreateLight},
        {"getLightType", GetLightType},
        {"getLightRadius", GetLightRadius},
        {"setLightRadius", SetLightRadius},
        {"getLightColor", GetLightColor},
        {"setLightColor", SetLightColor},
        {"getLightDirection", GetLightDirection},
        {"setLightDirection", SetLightDirection},
        {"createSearchLight", CreateSearchLight},
        {"getSearchLightStartPosition", GetSearchLightStartPosition},
        {"getSearchLightEndPosition", GetSearchLightEndPosition},
        {"getSearchLightStartRadius", GetSearchLightStartRadius},
        {"getSearchLightEndRadius", GetSearchLightEndRadius},
        {"getSearchLightColor", GetSearchLightColor},
        {"setSearchLightStartPosition", SetSearchLightStartPosition},
        {"setSearchLightEndPosition", SetSearchLightEndPosition},
        {"setSearchLightStartRadius", SetSearchLightStartRadius},
        {"setSearchLightEndRadius", SetSearchLightEndRadius},
        {"setSearchLightColor", SetSearchLightColor},
    };
    for (const auto& bind : binds) {
        lua_pushcfunction(L, bind.fn);
        lua_setglobal(L, bind.name);
    }
    lua_getglobal(L, "getElementPosition");
    lua_pushcclosure(L, GetElementPositionLight, 1);
    lua_setglobal(L, "getElementPosition");
    lua_getglobal(L, "setElementPosition");
    lua_pushcclosure(L, SetElementPositionLight, 1);
    lua_setglobal(L, "setElementPosition");
}

}  // namespace

void LightRelease(uint32_t id) { g_lights.erase(id); }
void InstallLightLua(lua_State* L) { Register(L); }

}  // namespace WorldRPG::Script
