// Radar areas. Desktop CLuaRadarAreaDefs.cpp and CStaticFunctionDefinitions.cpp 4471-4568.
// The radar rectangle is not drawn. Flashing is stored and does not pulse the alpha.

#include "LuaRadarApi.h"

#include "LuaApiExtra.h"
#include "LuaVMStub.h"
#include "ServerWorld.h"

#include "lua.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace WorldRPG::Script {
namespace {

struct Area {
    uint32_t id = 0;
    bool server = false;
    float x = 0, y = 0, w = 0, h = 0;
    int r = 255, g = 0, b = 0, a = 255;
    bool flashing = false;
};

std::unordered_map<uint32_t, Area> g_areas;
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
        ServerWorld::RadarFields fields;
        if (!KnownClientElement(id) && !ServerWorld::CopyRadarFields(id, fields)) return "destroyed element";
        if (g_areas.count(id) || ServerWorld::CopyRadarFields(id, fields)) return "radararea";
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

// SColorARGB stores static_cast<unsigned char>(value), then clamps that byte.
int ColorByte(float value) {
    const long truncated = static_cast<long>(value);
    return static_cast<unsigned char>(truncated);
}

bool ReadColor(lua_State* L, const char* function, int index, float fallback, int& out) {
    if (lua_isnoneornil(L, index)) {
        out = ColorByte(fallback);
        return true;
    }
    float value = 0;
    if (!Finite(L, index, value)) return Bad(L, function, "Expected number at argument " + std::to_string(index) + ", got " + Got(L, index)), false;
    out = ColorByte(value);
    return true;
}

Area* Find(uint32_t id) {
    auto found = g_areas.find(id);
    if (found == g_areas.end()) return nullptr;
    if (found->second.server) {
        ServerWorld::RadarFields fields;
        if (!ServerWorld::CopyRadarFields(id, fields)) {
            g_areas.erase(found);
            return nullptr;
        }
    } else if (!KnownClientElement(id)) {
        g_areas.erase(found);
        return nullptr;
    }
    return &g_areas.find(id)->second;
}

bool Resolve(uint32_t id, Area& out) {
    if (Area* area = Find(id)) {
        out = *area;
        return true;
    }
    ServerWorld::RadarFields fields;
    if (!ServerWorld::CopyRadarFields(id, fields)) return false;
    out = {};
    out.id = id;
    out.server = true;
    out.x = fields.x;
    out.y = fields.y;
    out.w = fields.w;
    out.h = fields.h;
    out.r = fields.r;
    out.g = fields.g;
    out.b = fields.b;
    out.a = fields.a;
    out.flashing = fields.flashing;
    return true;
}

Area* Edit(uint32_t id) {
    if (Area* area = Find(id)) return area;
    Area resolved;
    if (!Resolve(id, resolved)) return nullptr;
    g_areas[id] = resolved;
    return &g_areas[id];
}

int Miss(lua_State* L, const char* function) {
    return Bad(L, function, "Expected radararea at argument 1, got " + Got(L, 1));
}

int CreateRadarArea(lua_State* L) {
    // CLuaRadarAreaDefs::CreateRadarArea (49-95). Color defaults are 255, 0, 0, 255. Flashing starts off.
    float x = 0, y = 0, w = 0, h = 0;
    if (!Finite(L, 1, x) || !Finite(L, 2, y))
        return Bad(L, "createRadarArea", "Expected vector2 at argument 1, got " + Got(L, 1));
    if (!Finite(L, 3, w) || !Finite(L, 4, h))
        return Bad(L, "createRadarArea", "Expected vector2 at argument 3, got " + Got(L, 3));
    int r = 255, g = 0, b = 0, a = 255;
    if (!ReadColor(L, "createRadarArea", 5, 255, r)) return 1;
    if (!ReadColor(L, "createRadarArea", 6, 0, g)) return 1;
    if (!ReadColor(L, "createRadarArea", 7, 0, b)) return 1;
    if (!ReadColor(L, "createRadarArea", 8, 255, a)) return 1;
    const uint32_t id = CreateResourceElement("radararea");
    if (!id) return Fail(L);
    Area area;
    area.id = id;
    area.x = x;
    area.y = y;
    area.w = w;
    area.h = h;
    area.r = r;
    area.g = g;
    area.b = b;
    area.a = a;
    g_areas[id] = area;
    if (!g_diag) {
        g_diag = true;
        LUA_LOGE("DIAG createRadarArea: CClientRadarArea is not drawn");
    }
    PushElement(L, id);
    return 1;
}

int GetRadarAreaColor(lua_State* L) {
    Area area;
    if (!Resolve(ElementIdOf(L, 1), area)) return Miss(L, "getRadarAreaColor");
    lua_pushnumber(L, area.r);
    lua_pushnumber(L, area.g);
    lua_pushnumber(L, area.b);
    lua_pushnumber(L, area.a);
    return 4;
}

int GetRadarAreaSize(lua_State* L) {
    Area area;
    if (!Resolve(ElementIdOf(L, 1), area)) return Miss(L, "getRadarAreaSize");
    lua_pushnumber(L, area.w);
    lua_pushnumber(L, area.h);
    return 2;
}

int IsRadarAreaFlashing(lua_State* L) {
    Area area;
    if (!Resolve(ElementIdOf(L, 1), area)) return Miss(L, "isRadarAreaFlashing");
    lua_pushboolean(L, area.flashing ? 1 : 0);
    return 1;
}

int SetRadarAreaColor(lua_State* L) {
    Area* area = Edit(ElementIdOf(L, 1));
    if (!area) return Miss(L, "setRadarAreaColor");
    int r = 0, g = 0, b = 0, a = 255;
    if (!ReadColor(L, "setRadarAreaColor", 2, 0, r) || lua_isnoneornil(L, 2)) {
        if (lua_isnoneornil(L, 2)) return Bad(L, "setRadarAreaColor", "Expected number at argument 2, got " + Got(L, 2));
        return 1;
    }
    if (!ReadColor(L, "setRadarAreaColor", 3, 0, g) || lua_isnoneornil(L, 3)) {
        if (lua_isnoneornil(L, 3)) return Bad(L, "setRadarAreaColor", "Expected number at argument 3, got " + Got(L, 3));
        return 1;
    }
    if (!ReadColor(L, "setRadarAreaColor", 4, 0, b) || lua_isnoneornil(L, 4)) {
        if (lua_isnoneornil(L, 4)) return Bad(L, "setRadarAreaColor", "Expected number at argument 4, got " + Got(L, 4));
        return 1;
    }
    if (!ReadColor(L, "setRadarAreaColor", 5, 255, a)) return 1;
    area->r = r;
    area->g = g;
    area->b = b;
    area->a = a;
    lua_pushboolean(L, 1);
    return 1;
}

int SetRadarAreaSize(lua_State* L) {
    Area* area = Edit(ElementIdOf(L, 1));
    if (!area) return Miss(L, "setRadarAreaSize");
    float w = 0, h = 0;
    if (!Finite(L, 2, w) || !Finite(L, 3, h))
        return Bad(L, "setRadarAreaSize", "Expected vector2 at argument 2, got " + Got(L, 2));
    area->w = w;
    area->h = h;
    lua_pushboolean(L, 1);
    return 1;
}

int SetRadarAreaFlashing(lua_State* L) {
    Area* area = Edit(ElementIdOf(L, 1));
    if (!area) return Miss(L, "setRadarAreaFlashing");
    if (lua_type(L, 2) != LUA_TBOOLEAN) return Bad(L, "setRadarAreaFlashing", "Expected bool at argument 2, got " + Got(L, 2));
    area->flashing = lua_toboolean(L, 2) != 0;
    lua_pushboolean(L, 1);
    return 1;
}

bool Inside(const Area& area, float px, float py) {
    // CStaticFunctionDefinitions::IsInsideRadarArea sorts each axis so a negative size still contains its rectangle.
    const float x0 = std::min(area.x, area.x + area.w);
    const float x1 = std::max(area.x, area.x + area.w);
    const float y0 = std::min(area.y, area.y + area.h);
    const float y1 = std::max(area.y, area.y + area.h);
    return px >= x0 && px <= x1 && py >= y0 && py <= y1;
}

int IsInsideRadarArea(lua_State* L) {
    Area area;
    if (!Resolve(ElementIdOf(L, 1), area)) return Miss(L, "isInsideRadarArea");
    float x = 0, y = 0;
    if (!Finite(L, 2, x) || !Finite(L, 3, y))
        return Bad(L, "isInsideRadarArea", "Expected vector2 at argument 2, got " + Got(L, 2));
    lua_pushboolean(L, Inside(area, x, y) ? 1 : 0);
    return 1;
}

int GetElementPositionRadar(lua_State* L) {
    Area area;
    if (!Resolve(ElementIdOf(L, 1), area)) {
        if (!lua_isfunction(L, lua_upvalueindex(1))) return Fail(L);
        const int count = lua_gettop(L);
        lua_pushvalue(L, lua_upvalueindex(1));
        lua_insert(L, 1);
        lua_call(L, count, LUA_MULTRET);
        return lua_gettop(L);
    }
    lua_pushnumber(L, area.x);
    lua_pushnumber(L, area.y);
    lua_pushnumber(L, 0);
    return 3;
}

int SetElementPositionRadar(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    ServerWorld::RadarFields fields;
    if (!Find(id) && !ServerWorld::CopyRadarFields(id, fields)) {
        if (!lua_isfunction(L, lua_upvalueindex(1))) return Fail(L);
        const int count = lua_gettop(L);
        lua_pushvalue(L, lua_upvalueindex(1));
        lua_insert(L, 1);
        lua_call(L, count, LUA_MULTRET);
        return lua_gettop(L);
    }
    float x = 0, y = 0, z = 0;
    if (!Finite(L, 2, x) || !Finite(L, 3, y) || !Finite(L, 4, z)) return Fail(L);
    Area* area = Edit(id);
    if (!area) return Fail(L);
    area->x = x;
    area->y = y;
    (void)z;
    lua_pushboolean(L, 1);
    return 1;
}

void Register(lua_State* L) {
    g_areas.clear();
    const struct { const char* name; lua_CFunction fn; } binds[] = {
        {"createRadarArea", CreateRadarArea},
        {"getRadarAreaColor", GetRadarAreaColor},
        {"getRadarAreaSize", GetRadarAreaSize},
        {"isRadarAreaFlashing", IsRadarAreaFlashing},
        {"setRadarAreaColor", SetRadarAreaColor},
        {"setRadarAreaFlashing", SetRadarAreaFlashing},
        {"setRadarAreaSize", SetRadarAreaSize},
        {"isInsideRadarArea", IsInsideRadarArea},
    };
    for (const auto& bind : binds) {
        lua_pushcfunction(L, bind.fn);
        lua_setglobal(L, bind.name);
    }
    lua_getglobal(L, "getElementPosition");
    lua_pushcclosure(L, GetElementPositionRadar, 1);
    lua_setglobal(L, "getElementPosition");
    lua_getglobal(L, "setElementPosition");
    lua_pushcclosure(L, SetElementPositionRadar, 1);
    lua_setglobal(L, "setElementPosition");
}

}  // namespace

void RadarRelease(uint32_t id) { g_areas.erase(id); }
void InstallRadarLua(lua_State* L) { Register(L); }

}  // namespace WorldRPG::Script
