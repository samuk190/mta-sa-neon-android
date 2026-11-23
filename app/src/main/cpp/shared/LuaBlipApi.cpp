// Radar blips. Desktop CLuaBlipDefs.cpp and CStaticFunctionDefinitions.cpp 4826-4910.
// The radar sprite is not drawn.

#include "LuaBlipApi.h"

#include "LuaApiExtra.h"
#include "LuaVMStub.h"

#include "lua.hpp"

#include <cmath>
#include <cstdio>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace WorldRPG::Script {
namespace {

struct Blip {
    uint32_t id = 0;
    uint32_t attached = 0;
    float x = 0, y = 0, z = 0;
    int icon = 0;
    int size = 2;
    int r = 255, g = 0, b = 0, a = 255;
    int ordering = 0;
    int distance = 16383;
};

std::unordered_map<uint32_t, Blip> g_blips;
bool g_diag = false;

int Fail(lua_State* L) {
    lua_pushboolean(L, 0);
    return 1;
}

void Warn(const std::string& message) { LUA_LOGI("WARNING: %s", message.c_str()); }

std::string Got(lua_State* L, int index) {
    const int type = lua_type(L, index);
    if (type == LUA_TNUMBER) {
        const char* text = lua_tostring(L, index);
        return std::string("number (") + (text ? text : "0") + ")";
    }
    if (type == LUA_TBOOLEAN) return lua_toboolean(L, index) ? "boolean (true)" : "boolean (false)";
    if (type == LUA_TSTRING) {
        const char* text = lua_tostring(L, index);
        return std::string("string (\"") + (text ? text : "") + "\")";
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

// Optional integer in [min, max]. Missing stays at the current value, which the caller set to the default.
bool OptionalInt(lua_State* L, const char* function, int index, int min, int max, int& out) {
    if (lua_isnoneornil(L, index)) return true;
    if (!lua_isnumber(L, index)) return Bad(L, function, "Expected number at argument " + std::to_string(index) + ", got " + Got(L, index)), false;
    const lua_Number number = lua_tonumber(L, index);
    if (std::isnan(number)) return Bad(L, function, "Expected number at argument " + std::to_string(index) + ", got NaN"), false;
    if (!std::isfinite(number) || number < min || number > max)
        return Bad(L, function,
                   "Expected number within the destination type range at argument " + std::to_string(index) + ", got out-of-range number"),
               false;
    out = static_cast<int>(number);
    return true;
}

Blip* Find(uint32_t id) {
    auto found = g_blips.find(id);
    if (found == g_blips.end()) return nullptr;
    if (!KnownClientElement(id)) {
        g_blips.erase(found);
        return nullptr;
    }
    return &g_blips.find(id)->second;
}

bool ValidIcon(int icon) { return icon >= 0 && icon <= 63; }

int ClampSize(lua_State* L, int size) {
    if (size <= 25) return size;
    char message[160];
    std::snprintf(message, sizeof(message), "Blip size beyond 25 is no longer supported (got %d). It will be clamped between 0 and 25.", size);
    Warn(message);
    (void)L;
    return 25;
}

int CreateBlip(lua_State* L) {
    // CLuaBlipDefs::CreateBlip. Defaults: icon 0, size 2, color 255,0,0,255, ordering 0, distance 16383.
    float x = 0, y = 0, z = 0;
    if (!Finite(L, 1, x) || !Finite(L, 2, y) || !Finite(L, 3, z))
        return Bad(L, "createBlip", "Expected vector3 at argument 1, got " + Got(L, 1));
    int icon = 0, size = 2, r = 255, g = 0, b = 0, a = 255, ordering = 0, distance = 16383;
    if (!OptionalInt(L, "createBlip", 4, 0, 255, icon)) return 1;
    if (!OptionalInt(L, "createBlip", 5, 0, 255, size)) return 1;
    if (!OptionalInt(L, "createBlip", 6, 0, 255, r)) return 1;
    if (!OptionalInt(L, "createBlip", 7, 0, 255, g)) return 1;
    if (!OptionalInt(L, "createBlip", 8, 0, 255, b)) return 1;
    if (!OptionalInt(L, "createBlip", 9, 0, 255, a)) return 1;
    if (!OptionalInt(L, "createBlip", 10, -32768, 32767, ordering)) return 1;
    if (!OptionalInt(L, "createBlip", 11, 0, 65535, distance)) return 1;
    if (!ValidIcon(icon)) {
        Warn("Invalid icon");
        return Fail(L);
    }
    size = ClampSize(L, size);
    const uint32_t id = CreateResourceElement("blip");
    if (!id) return Fail(L);
    Blip blip;
    blip.id = id;
    blip.x = x;
    blip.y = y;
    blip.z = z;
    blip.icon = icon;
    blip.size = size;
    blip.r = r;
    blip.g = g;
    blip.b = b;
    blip.a = a;
    blip.ordering = ordering;
    blip.distance = distance;
    g_blips[id] = blip;
    if (!g_diag) {
        g_diag = true;
        LUA_LOGE("DIAG createBlip: CClientRadarMarker is not drawn");
    }
    PushElement(L, id);
    return 1;
}

int CreateBlipAttachedTo(lua_State* L) {
    const uint32_t target = ElementIdOf(L, 1);
    if (!lua_istable(L, 1) || !KnownClientElement(target))
        return Bad(L, "createBlipAttachedTo", "Expected element at argument 1, got " + Got(L, 1));
    int icon = 0, size = 2, r = 255, g = 0, b = 0, a = 255, ordering = 0, distance = 16383;
    if (!OptionalInt(L, "createBlipAttachedTo", 2, 0, 255, icon)) return 1;
    if (!OptionalInt(L, "createBlipAttachedTo", 3, 0, 255, size)) return 1;
    if (!OptionalInt(L, "createBlipAttachedTo", 4, 0, 255, r)) return 1;
    if (!OptionalInt(L, "createBlipAttachedTo", 5, 0, 255, g)) return 1;
    if (!OptionalInt(L, "createBlipAttachedTo", 6, 0, 255, b)) return 1;
    if (!OptionalInt(L, "createBlipAttachedTo", 7, 0, 255, a)) return 1;
    if (!OptionalInt(L, "createBlipAttachedTo", 8, -32768, 32767, ordering)) return 1;
    if (!OptionalInt(L, "createBlipAttachedTo", 9, 0, 65535, distance)) return 1;
    if (!ValidIcon(icon)) {
        Warn("Invalid icon");
        return Fail(L);
    }
    size = ClampSize(L, size);
    const uint32_t id = CreateResourceElement("blip");
    if (!id) return Fail(L);
    Blip blip;
    blip.id = id;
    blip.attached = target;
    blip.icon = icon;
    blip.size = size;
    blip.r = r;
    blip.g = g;
    blip.b = b;
    blip.a = a;
    blip.ordering = ordering;
    blip.distance = distance;
    g_blips[id] = blip;
    if (!g_diag) {
        g_diag = true;
        LUA_LOGE("DIAG createBlip: CClientRadarMarker is not drawn");
    }
    PushElement(L, id);
    return 1;
}

int Miss(lua_State* L, const char* function) {
    return Bad(L, function, "Expected blip at argument 1, got " + Got(L, 1));
}

int GetBlipIcon(lua_State* L) {
    Blip* blip = Find(ElementIdOf(L, 1));
    if (!blip) return Miss(L, "getBlipIcon");
    lua_pushnumber(L, blip->icon);
    return 1;
}

int GetBlipSize(lua_State* L) {
    Blip* blip = Find(ElementIdOf(L, 1));
    if (!blip) return Miss(L, "getBlipSize");
    lua_pushnumber(L, blip->size);
    return 1;
}

int GetBlipColor(lua_State* L) {
    Blip* blip = Find(ElementIdOf(L, 1));
    if (!blip) return Miss(L, "getBlipColor");
    lua_pushnumber(L, blip->r);
    lua_pushnumber(L, blip->g);
    lua_pushnumber(L, blip->b);
    lua_pushnumber(L, blip->a);
    return 4;
}

int GetBlipOrdering(lua_State* L) {
    Blip* blip = Find(ElementIdOf(L, 1));
    if (!blip) return Miss(L, "getBlipOrdering");
    lua_pushnumber(L, blip->ordering);
    return 1;
}

int GetBlipVisibleDistance(lua_State* L) {
    Blip* blip = Find(ElementIdOf(L, 1));
    if (!blip) return Miss(L, "getBlipVisibleDistance");
    lua_pushnumber(L, blip->distance);
    return 1;
}

int SetBlipIcon(lua_State* L) {
    Blip* blip = Find(ElementIdOf(L, 1));
    if (!blip) return Miss(L, "setBlipIcon");
    int icon = 0;
    if (!OptionalInt(L, "setBlipIcon", 2, 0, 255, icon) || lua_isnoneornil(L, 2)) {
        if (lua_isnoneornil(L, 2)) return Bad(L, "setBlipIcon", "Expected number at argument 2, got " + Got(L, 2));
        return 1;
    }
    if (!ValidIcon(icon)) {
        Warn("Invalid icon");
        return Fail(L);
    }
    blip->icon = icon;
    lua_pushboolean(L, 1);
    return 1;
}

int SetBlipSize(lua_State* L) {
    Blip* blip = Find(ElementIdOf(L, 1));
    if (!blip) return Miss(L, "setBlipSize");
    int size = 0;
    if (!OptionalInt(L, "setBlipSize", 2, 0, 255, size) || lua_isnoneornil(L, 2)) {
        if (lua_isnoneornil(L, 2)) return Bad(L, "setBlipSize", "Expected number at argument 2, got " + Got(L, 2));
        return 1;
    }
    blip->size = ClampSize(L, size);
    lua_pushboolean(L, 1);
    return 1;
}

int SetBlipColor(lua_State* L) {
    Blip* blip = Find(ElementIdOf(L, 1));
    if (!blip) return Miss(L, "setBlipColor");
    int r = 0, g = 0, b = 0, a = 0;
    for (int index = 2; index <= 5; ++index) {
        int* channel = index == 2 ? &r : index == 3 ? &g : index == 4 ? &b : &a;
        if (lua_isnoneornil(L, index))
            return Bad(L, "setBlipColor", "Expected number at argument " + std::to_string(index) + ", got " + Got(L, index));
        if (!OptionalInt(L, "setBlipColor", index, 0, 255, *channel)) return 1;
    }
    blip->r = r;
    blip->g = g;
    blip->b = b;
    blip->a = a;
    lua_pushboolean(L, 1);
    return 1;
}

int SetBlipOrdering(lua_State* L) {
    Blip* blip = Find(ElementIdOf(L, 1));
    if (!blip) return Miss(L, "setBlipOrdering");
    int ordering = 0;
    if (!OptionalInt(L, "setBlipOrdering", 2, -32768, 32767, ordering) || lua_isnoneornil(L, 2)) {
        if (lua_isnoneornil(L, 2)) return Bad(L, "setBlipOrdering", "Expected number at argument 2, got " + Got(L, 2));
        return 1;
    }
    blip->ordering = ordering;
    lua_pushboolean(L, 1);
    return 1;
}

int SetBlipVisibleDistance(lua_State* L) {
    Blip* blip = Find(ElementIdOf(L, 1));
    if (!blip) return Miss(L, "setBlipVisibleDistance");
    int distance = 0;
    if (!OptionalInt(L, "setBlipVisibleDistance", 2, 0, 65535, distance) || lua_isnoneornil(L, 2)) {
        if (lua_isnoneornil(L, 2)) return Bad(L, "setBlipVisibleDistance", "Expected number at argument 2, got " + Got(L, 2));
        return 1;
    }
    blip->distance = distance;
    lua_pushboolean(L, 1);
    return 1;
}

int GetElementPositionBlip(lua_State* L) {
    Blip* blip = Find(ElementIdOf(L, 1));
    if (!blip || !lua_isfunction(L, lua_upvalueindex(1))) {
        if (!blip) {
            if (!lua_isfunction(L, lua_upvalueindex(1))) return Fail(L);
            const int count = lua_gettop(L);
            lua_pushvalue(L, lua_upvalueindex(1));
            lua_insert(L, 1);
            lua_call(L, count, LUA_MULTRET);
            return lua_gettop(L);
        }
    }
    if (blip->attached && KnownClientElement(blip->attached)) {
        lua_pushvalue(L, lua_upvalueindex(1));
        PushElement(L, blip->attached);
        lua_call(L, 1, 3);
        return 3;
    }
    lua_pushnumber(L, blip->x);
    lua_pushnumber(L, blip->y);
    lua_pushnumber(L, blip->z);
    return 3;
}

int SetElementPositionBlip(lua_State* L) {
    Blip* blip = Find(ElementIdOf(L, 1));
    if (!blip) {
        if (!lua_isfunction(L, lua_upvalueindex(1))) return Fail(L);
        const int count = lua_gettop(L);
        lua_pushvalue(L, lua_upvalueindex(1));
        lua_insert(L, 1);
        lua_call(L, count, LUA_MULTRET);
        return lua_gettop(L);
    }
    float x = 0, y = 0, z = 0;
    if (!Finite(L, 2, x) || !Finite(L, 3, y) || !Finite(L, 4, z)) return Fail(L);
    blip->x = x;
    blip->y = y;
    blip->z = z;
    blip->attached = 0;
    lua_pushboolean(L, 1);
    return 1;
}

void Register(lua_State* L) {
    g_blips.clear();
    const struct { const char* name; lua_CFunction fn; } binds[] = {
        {"createBlip", CreateBlip},
        {"createBlipAttachedTo", CreateBlipAttachedTo},
        {"getBlipIcon", GetBlipIcon},
        {"getBlipSize", GetBlipSize},
        {"getBlipColor", GetBlipColor},
        {"getBlipOrdering", GetBlipOrdering},
        {"getBlipVisibleDistance", GetBlipVisibleDistance},
        {"setBlipIcon", SetBlipIcon},
        {"setBlipSize", SetBlipSize},
        {"setBlipColor", SetBlipColor},
        {"setBlipOrdering", SetBlipOrdering},
        {"setBlipVisibleDistance", SetBlipVisibleDistance},
    };
    for (const auto& bind : binds) {
        lua_pushcfunction(L, bind.fn);
        lua_setglobal(L, bind.name);
    }
    lua_getglobal(L, "getElementPosition");
    lua_pushcclosure(L, GetElementPositionBlip, 1);
    lua_setglobal(L, "getElementPosition");
    lua_getglobal(L, "setElementPosition");
    lua_pushcclosure(L, SetElementPositionBlip, 1);
    lua_setglobal(L, "setElementPosition");
}

}  // namespace

void BlipRelease(uint32_t id) { g_blips.erase(id); }
void InstallBlipLua(lua_State* L) { Register(L); }

}  // namespace WorldRPG::Script
