// Client markers. Desktop CLuaMarkerDefs.cpp, CStaticFunctionDefinitions.cpp 4948-5122,
// CClientMarker.cpp and CClientCheckpoint.cpp. The checkpoint, 3D marker and corona
// drawables are not created. Collision shapes owned by the marker are not created.

#include "LuaMarkerApi.h"

#include "LuaApiExtra.h"
#include "LuaSpatialApi.h"
#include "LuaVMStub.h"
#include "ServerWorld.h"

#include "lua.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace WorldRPG::Script {
namespace {

enum class Kind : int { Checkpoint = 0, Ring = 1, Cylinder = 2, Arrow = 3, Corona = 4 };

struct Marker {
    uint32_t id = 0;
    Kind kind = Kind::Checkpoint;
    float x = 0, y = 0, z = 0, size = 4;
    int r = 0, g = 0, b = 255, a = 255;
    bool ignoreAlpha = false;
    bool server = false;
    int icon = 0;
    bool hasTarget = false;
    float targetX = 0, targetY = 0, targetZ = 0;
    int arrowR = 255, arrowG = 64, arrowB = 64, arrowA = 255;
    float arrowSize = 2.5f;
    bool reflection = false;
};

std::unordered_map<uint32_t, Marker> g_markers;
bool g_diagDraw = false;
bool g_diagLimits = false;
bool g_diagArea = false;

const char* TypeName(Kind kind) {
    switch (kind) {
        case Kind::Checkpoint: return "checkpoint";
        case Kind::Ring: return "ring";
        case Kind::Cylinder: return "cylinder";
        case Kind::Arrow: return "arrow";
        case Kind::Corona: return "corona";
    }
    return "invalid";
}

bool CheckpointFamily(Kind kind) { return kind == Kind::Checkpoint || kind == Kind::Ring; }
bool SolidFamily(Kind kind) { return kind == Kind::Cylinder || kind == Kind::Arrow; }

int KindFromName(const std::string& text) {
    std::string lower = text;
    for (char& ch : lower) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    if (lower == "default" || lower == "checkpoint") return 0;
    if (lower == "ring") return 1;
    if (lower == "cylinder") return 2;
    if (lower == "arrow") return 3;
    if (lower == "corona") return 4;
    return 0xFF;
}

const char* IconName(int icon) {
    if (icon == 0) return "none";
    if (icon == 1) return "arrow";
    if (icon == 2) return "finish";
    return nullptr;
}

int IconFromName(const std::string& text) {
    std::string lower = text;
    for (char& ch : lower) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    if (lower == "none") return 0;
    if (lower == "arrow") return 1;
    if (lower == "finish") return 2;
    return 3;
}

int Fail(lua_State* L) {
    lua_pushboolean(L, 0);
    return 1;
}

void Warn(const std::string& message) { LUA_LOGI("WARNING: %s", message.c_str()); }

std::string ScriptGot(lua_State* L, int index) {
    const int type = lua_type(L, index);
    if (type == LUA_TTABLE) {
        lua_getfield(L, index, "element");
        const bool element = lua_toboolean(L, -1) != 0;
        lua_pop(L, 1);
        if (element) {
            const uint32_t id = ElementIdOf(L, index);
            ServerWorld::MarkerFields fields;
            if (!KnownClientElement(id) && !ServerWorld::CopyMarkerFields(id, fields)) return "destroyed element";
            if (!KnownClientElement(id)) return "marker";
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
    }
    switch (type) {
        case LUA_TNIL: return "nil";
        case LUA_TBOOLEAN: return "boolean";
        case LUA_TNUMBER: {
            const char* text = lua_tostring(L, index);
            return std::string("number '") + (text ? text : "0") + "'";
        }
        case LUA_TSTRING: {
            const char* text = lua_tostring(L, index);
            return std::string("string '") + (text ? text : "") + "'";
        }
        case LUA_TTABLE: return "table";
        case LUA_TFUNCTION: return "function";
        case LUA_TNONE: return "none";
        default: return "userdata";
    }
}

std::string ParserGot(lua_State* L, int index) {
    const int type = lua_type(L, index);
    if (type == LUA_TBOOLEAN) return lua_toboolean(L, index) ? "boolean (true)" : "boolean (false)";
    if (type == LUA_TNUMBER) {
        const char* text = lua_tostring(L, index);
        return std::string("number (") + (text ? text : "0") + ")";
    }
    if (type == LUA_TSTRING) {
        const char* text = lua_tostring(L, index);
        std::string value = text ? text : "";
        if (value.size() > 10) {
            value.resize(10);
            value[7] = value[8] = value[9] = '.';
        }
        return "string (\"" + value + "\")";
    }
    if (type == LUA_TNIL) return "nil";
    if (type == LUA_TNONE) return "none";
    if (type == LUA_TTABLE) return ScriptGot(L, index) == "table" ? "table" : ScriptGot(L, index);
    if (type == LUA_TFUNCTION) return "function";
    return "userdata";
}

bool FiniteAt(lua_State* L, int index, float& out) {
    if (!lua_isnumber(L, index)) return false;
    const lua_Number number = lua_tonumber(L, index);
    if (!std::isfinite(number)) return false;
    out = static_cast<float>(number);
    return true;
}

Marker FromFields(uint32_t id, const ServerWorld::MarkerFields& fields) {
    Marker marker;
    marker.id = id;
    marker.server = true;
    marker.kind = static_cast<Kind>(std::clamp(fields.type, 0, 4));
    marker.x = fields.x;
    marker.y = fields.y;
    marker.z = fields.z;
    marker.size = fields.size;
    marker.r = fields.r;
    marker.g = fields.g;
    marker.b = fields.b;
    marker.a = fields.a;
    marker.ignoreAlpha = fields.ignoreAlpha;
    marker.hasTarget = fields.hasTarget;
    marker.targetX = fields.targetX;
    marker.targetY = fields.targetY;
    marker.targetZ = fields.targetZ;
    marker.arrowR = fields.arrowR;
    marker.arrowG = fields.arrowG;
    marker.arrowB = fields.arrowB;
    marker.arrowA = fields.arrowA;
    marker.arrowSize = fields.arrowSize;
    if (marker.hasTarget && CheckpointFamily(marker.kind)) marker.icon = 1;
    return marker;
}

Marker* Find(uint32_t id) {
    auto found = g_markers.find(id);
    if (found == g_markers.end()) return nullptr;
    if (found->second.server) {
        ServerWorld::MarkerFields fields;
        if (!ServerWorld::CopyMarkerFields(id, fields)) {
            g_markers.erase(found);
            return nullptr;
        }
    } else if (!KnownClientElement(id)) {
        g_markers.erase(found);
        return nullptr;
    }
    return &g_markers.find(id)->second;
}

Marker* Edit(uint32_t id) {
    if (Marker* marker = Find(id)) return marker;
    ServerWorld::MarkerFields fields;
    if (!ServerWorld::CopyMarkerFields(id, fields)) return nullptr;
    g_markers[id] = FromFields(id, fields);
    return &g_markers[id];
}

void NoteDraw() {
    if (g_diagDraw) return;
    g_diagDraw = true;
    LUA_LOGE("DIAG createMarker: CClientCheckpoint, CClient3DMarker and CClientCorona are not created");
}

void ApplyKind(Marker& marker, Kind kind) {
    if (marker.kind == kind) return;
    const bool keepCheckpoint = CheckpointFamily(marker.kind) && CheckpointFamily(kind);
    const bool keepSolid = SolidFamily(marker.kind) && SolidFamily(kind);
    marker.kind = kind;
    if (!(keepCheckpoint || keepSolid)) {
        marker.ignoreAlpha = false;
        marker.icon = 0;
        marker.hasTarget = false;
        marker.reflection = false;
        marker.arrowR = 255;
        marker.arrowG = 64;
        marker.arrowB = 64;
        marker.arrowA = 255;
        marker.arrowSize = marker.size * 0.625f;
    }
    // CreateOfType replaces the collision when the marker type changes, including checkpoint/ring.
    RetainMarkerColShape(marker.id, static_cast<int>(marker.kind), marker.x, marker.y, marker.z, marker.size);
}

void Each(uint32_t id, const std::function<void(uint32_t)>& visit) {
    if (ClientCallPropagationEnabled(id)) {
        std::vector<uint32_t> children;
        CopyClientChildren(id, children);
        for (uint32_t child : children) Each(child, visit);
    }
    visit(id);
}

int CreateMarker(lua_State* L) {
    // CLuaMarkerDefs::CreateMarker (107-152) and CStaticFunctionDefinitions::CreateMarker (4948-4976).
    float x = 0, y = 0, z = 0;
    if (!FiniteAt(L, 1, x) || !FiniteAt(L, 2, y) || !FiniteAt(L, 3, z)) {
        Warn(std::string("Bad argument @ 'createMarker' [Expected vector3 at argument 1, got ") + ScriptGot(L, 1) + "]");
        return Fail(L);
    }
    std::string type = "default";
    if (!lua_isnoneornil(L, 4)) {
        if (!lua_isstring(L, 4)) {
            Warn(std::string("Bad argument @ 'createMarker' [Expected string at argument 4, got ") + ScriptGot(L, 4) + "]");
            return Fail(L);
        }
        type = lua_tostring(L, 4);
    }
    float size = 4.f;
    if (!lua_isnoneornil(L, 5)) {
        if (!FiniteAt(L, 5, size)) {
            Warn(std::string("Bad argument @ 'createMarker' [Expected number at argument 5, got ") + ScriptGot(L, 5) + "]");
            return Fail(L);
        }
    }
    int color[4] = {0, 0, 255, 255};
    for (int i = 0; i < 4; ++i) {
        if (lua_isnoneornil(L, 6 + i)) continue;
        float channel = 0;
        if (!FiniteAt(L, 6 + i, channel)) {
            Warn(std::string("Bad argument @ 'createMarker' [Expected number at argument ") + std::to_string(6 + i) + ", got " +
                 ScriptGot(L, 6 + i) + "]");
            return Fail(L);
        }
        if (channel < 0.f) {
            Warn("Bad argument @ 'createMarker' [Expected positive value, got negative]");
            return Fail(L);
        }
        color[i] = static_cast<int>(static_cast<unsigned char>(channel));
    }
    bool ignore = false;
    if (!lua_isnoneornil(L, 10)) {
        if (lua_type(L, 10) != LUA_TBOOLEAN) {
            Warn(std::string("Bad argument @ 'createMarker' [Expected bool at argument 10, got ") + ScriptGot(L, 10) + "]");
            return Fail(L);
        }
        ignore = lua_toboolean(L, 10) != 0;
    }
    const int kind = KindFromName(type);
    if (kind == 0xFF) return Fail(L);
    const uint32_t id = CreateResourceElement("marker");
    if (!id) return Fail(L);
    Marker marker;
    marker.id = id;
    marker.kind = static_cast<Kind>(kind);
    marker.x = x;
    marker.y = y;
    marker.z = z;
    marker.size = size;
    marker.r = color[0];
    marker.g = color[1];
    marker.b = color[2];
    marker.a = color[3];
    marker.arrowSize = size * 0.625f;
    if (marker.kind == Kind::Checkpoint || marker.kind == Kind::Arrow) marker.ignoreAlpha = ignore;
    g_markers[id] = marker;
    NoteDraw();
    RetainMarkerColShape(id, kind, x, y, z, size);
    PushElement(L, id);
    return 1;
}

int GetMarkerCount(lua_State* L) {
    // CLuaMarkerDefs::GetMarkerCount (154-159). Streamed server markers stay in CClientMarkerManager.
    (void)L;
    int count = ServerWorld::CountKind(4);
    for (const auto& [id, marker] : g_markers) {
        if (!marker.server && KnownClientElement(id)) ++count;
    }
    lua_pushnumber(L, count);
    return 1;
}

int GetMarkerLimitStats(lua_State* L) {
    // CLuaMarkerDefs::GetMarkerLimitStats (161-183). Capacities are the desktop constants.
    // Process limits stay 0 because C3DMarkersSA never raises them, and nothing is streamed.
    if (!g_diagLimits) {
        g_diagLimits = true;
        LUA_LOGE("DIAG getMarkerLimitStats: C3DMarkers and CCheckpoints are not read");
    }
    lua_createtable(L, 0, 10);
    const struct { const char* name; int value; } fields[] = {
        {"streamedMarkers", 0},
        {"streamedMarkerLimit", 4096},
        {"allocated3DMarkers", 0},
        {"marker3DLimit", 4096},
        {"marker3DProcessLimit", 0},
        {"activeCheckpoints", 0},
        {"checkpointLimit", 4096},
        {"checkpointProcessLimit", 0},
        {"directionArrowLimit", 4096},
        {"directionArrowProcessLimit", 0},
    };
    for (const auto& field : fields) {
        lua_pushnumber(L, field.value);
        lua_setfield(L, -2, field.name);
    }
    return 1;
}

bool Resolve(uint32_t id, Marker& out) {
    if (Marker* marker = Find(id)) {
        out = *marker;
        return true;
    }
    ServerWorld::MarkerFields fields;
    if (!ServerWorld::CopyMarkerFields(id, fields)) return false;
    out = FromFields(id, fields);
    return true;
}

int Miss(lua_State* L, const char* function) {
    Warn(std::string("Bad argument @ '") + function + "' [Expected marker at argument 1, got " + ScriptGot(L, 1) + "]");
    return Fail(L);
}

int GetMarkerType(lua_State* L) {
    Marker marker;
    if (!Resolve(ElementIdOf(L, 1), marker)) return Miss(L, "getMarkerType");
    lua_pushstring(L, TypeName(marker.kind));
    return 1;
}

int GetMarkerSize(lua_State* L) {
    Marker marker;
    if (!Resolve(ElementIdOf(L, 1), marker)) return Miss(L, "getMarkerSize");
    lua_pushnumber(L, marker.size);
    return 1;
}

int GetMarkerColor(lua_State* L) {
    // Alpha is forced to 255 for checkpoint and arrow unless the limit is ignored.
    Marker marker;
    if (!Resolve(ElementIdOf(L, 1), marker)) return Miss(L, "getMarkerColor");
    int alpha = marker.a;
    if (!marker.ignoreAlpha && (marker.kind == Kind::Checkpoint || marker.kind == Kind::Arrow)) alpha = 255;
    lua_pushnumber(L, marker.r);
    lua_pushnumber(L, marker.g);
    lua_pushnumber(L, marker.b);
    lua_pushnumber(L, alpha);
    return 4;
}

int GetMarkerTarget(lua_State* L) {
    Marker marker;
    if (!Resolve(ElementIdOf(L, 1), marker)) return Miss(L, "getMarkerTarget");
    if (!CheckpointFamily(marker.kind) || !marker.hasTarget) return Fail(L);
    lua_pushnumber(L, marker.targetX);
    lua_pushnumber(L, marker.targetY);
    lua_pushnumber(L, marker.targetZ);
    return 3;
}

int GetMarkerIcon(lua_State* L) {
    Marker marker;
    if (!Resolve(ElementIdOf(L, 1), marker)) return Miss(L, "getMarkerIcon");
    const char* icon = CheckpointFamily(marker.kind) ? IconName(marker.icon) : nullptr;
    if (!icon) return Fail(L);
    lua_pushstring(L, icon);
    return 1;
}

int SetMarkerType(lua_State* L) {
    // A valid type name returns true for any element. CStaticFunctionDefinitions::SetMarkerType (4993-5013).
    const uint32_t id = ElementIdOf(L, 1);
    ServerWorld::MarkerFields probe;
    if (!lua_istable(L, 1) || (!KnownClientElement(id) && !ServerWorld::CopyMarkerFields(id, probe) && !Find(id)))
        return Miss(L, "setMarkerType");
    if (lua_type(L, 2) != LUA_TSTRING && lua_type(L, 2) != LUA_TNUMBER) {
        Warn(std::string("Bad argument @ 'setMarkerType' [Expected string at argument 2, got ") + ScriptGot(L, 2) + "]");
        return Fail(L);
    }
    const int kind = KindFromName(lua_tostring(L, 2) ? lua_tostring(L, 2) : "");
    Each(id, [&](uint32_t child) {
        if (kind == 0xFF) return;
        if (Marker* marker = Edit(child)) ApplyKind(*marker, static_cast<Kind>(kind));
    });
    lua_pushboolean(L, kind != 0xFF ? 1 : 0);
    return 1;
}

int SetMarkerSize(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    ServerWorld::MarkerFields probe;
    if (!lua_istable(L, 1) || (!KnownClientElement(id) && !ServerWorld::CopyMarkerFields(id, probe) && !Find(id)))
        return Miss(L, "setMarkerSize");
    float size = 0;
    if (!FiniteAt(L, 2, size)) {
        Warn(std::string("Bad argument @ 'setMarkerSize' [Expected number at argument 2, got ") + ScriptGot(L, 2) + "]");
        return Fail(L);
    }
    bool changed = false;
    Each(id, [&](uint32_t child) {
        Marker* marker = Edit(child);
        if (!marker) return;
        if (marker->size != size && CheckpointFamily(marker->kind)) marker->arrowSize = size * 0.625f;
        marker->size = size;
        RetainMarkerColShape(marker->id, static_cast<int>(marker->kind), marker->x, marker->y, marker->z, marker->size);
        changed = true;
    });
    lua_pushboolean(L, changed ? 1 : 0);
    return 1;
}

int SetMarkerColor(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    ServerWorld::MarkerFields probe;
    if (!lua_istable(L, 1) || (!KnownClientElement(id) && !ServerWorld::CopyMarkerFields(id, probe) && !Find(id)))
        return Miss(L, "setMarkerColor");
    lua_Number channels[4] = {};
    for (int i = 0; i < 3; ++i) {
        if (!lua_isnumber(L, 2 + i) || !std::isfinite(lua_tonumber(L, 2 + i))) {
            Warn(std::string("Bad argument @ 'setMarkerColor' [Expected number at argument ") + std::to_string(2 + i) + ", got " +
                 ScriptGot(L, 2 + i) + "]");
            return Fail(L);
        }
        channels[i] = lua_tonumber(L, 2 + i);
    }
    channels[3] = 255;
    if (!lua_isnoneornil(L, 5)) {
        if (!lua_isnumber(L, 5) || !std::isfinite(lua_tonumber(L, 5))) {
            Warn(std::string("Bad argument @ 'setMarkerColor' [Expected number at argument 5, got ") + ScriptGot(L, 5) + "]");
            return Fail(L);
        }
        channels[3] = lua_tonumber(L, 5);
    }
    for (lua_Number channel : channels) {
        if (channel < 0 || channel > 255) return Fail(L);
    }
    bool changed = false;
    Each(id, [&](uint32_t child) {
        Marker* marker = Edit(child);
        if (!marker) return;
        marker->r = static_cast<int>(channels[0]);
        marker->g = static_cast<int>(channels[1]);
        marker->b = static_cast<int>(channels[2]);
        marker->a = static_cast<int>(channels[3]);
        changed = true;
    });
    lua_pushboolean(L, changed ? 1 : 0);
    return 1;
}

int SetMarkerTarget(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    ServerWorld::MarkerFields probe;
    if (!lua_istable(L, 1) || (!KnownClientElement(id) && !ServerWorld::CopyMarkerFields(id, probe) && !Find(id)))
        return Miss(L, "setMarkerTarget");
    float x = 0, y = 0, z = 0;
    if (!FiniteAt(L, 2, x) || !FiniteAt(L, 3, y) || !FiniteAt(L, 4, z)) {
        Warn(std::string("Bad argument @ 'setMarkerTarget' [Expected vector3 at argument 2, got ") + ScriptGot(L, 2) + "]");
        return Fail(L);
    }
    bool changed = false;
    Each(id, [&](uint32_t child) {
        Marker* marker = Edit(child);
        if (!marker || !CheckpointFamily(marker->kind)) return;
        marker->hasTarget = true;
        marker->icon = 1;
        marker->targetX = x;
        marker->targetY = y;
        marker->targetZ = z;
        changed = true;
    });
    lua_pushboolean(L, changed ? 1 : 0);
    return 1;
}

int SetMarkerIcon(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    ServerWorld::MarkerFields probe;
    if (!lua_istable(L, 1) || (!KnownClientElement(id) && !ServerWorld::CopyMarkerFields(id, probe) && !Find(id)))
        return Miss(L, "setMarkerIcon");
    if (lua_type(L, 2) != LUA_TSTRING && lua_type(L, 2) != LUA_TNUMBER) {
        Warn(std::string("Bad argument @ 'setMarkerIcon' [Expected string at argument 2, got ") + ScriptGot(L, 2) + "]");
        return Fail(L);
    }
    const int icon = IconFromName(lua_tostring(L, 2) ? lua_tostring(L, 2) : "");
    if (icon == 3) return Fail(L);
    bool changed = false;
    Each(id, [&](uint32_t child) {
        Marker* marker = Edit(child);
        if (!marker || !CheckpointFamily(marker->kind)) return;
        marker->icon = icon;
        changed = true;
    });
    lua_pushboolean(L, changed ? 1 : 0);
    return 1;
}

int Hard(lua_State* L, const char* function, const std::string& detail) {
    return luaL_error(L, "Bad argument @ '%s' [%s]", function, detail.c_str());
}

bool RequireMarker(lua_State* L, const char* function, Marker& out) {
    const uint32_t id = ElementIdOf(L, 1);
    if (Resolve(id, out)) return true;
    Hard(L, function, "Expected marker at argument 1, got " + ParserGot(L, 1));
    return false;
}

int GetMarkerTargetArrowProperties(lua_State* L) {
    Marker marker;
    if (!RequireMarker(L, "getMarkerTargetArrowProperties", marker)) return 0;
    if (marker.kind != Kind::Checkpoint || !marker.hasTarget) return Fail(L);
    lua_pushnumber(L, marker.arrowR);
    lua_pushnumber(L, marker.arrowG);
    lua_pushnumber(L, marker.arrowB);
    lua_pushnumber(L, marker.arrowA);
    lua_pushnumber(L, marker.arrowSize);
    return 5;
}

bool ReadOptionalByte(lua_State* L, const char* function, int index, int fallback, int& out) {
    if (lua_isnoneornil(L, index)) {
        out = fallback;
        return true;
    }
    if (!lua_isnumber(L, index)) {
        Hard(L, function, "Expected number at argument " + std::to_string(index) + ", got " + ParserGot(L, index));
        return false;
    }
    const lua_Number number = lua_tonumber(L, index);
    if (std::isnan(number)) {
        Hard(L, function, "Expected number at argument " + std::to_string(index) + ", got NaN");
        return false;
    }
    if (!std::isfinite(number) || number < 0 || number > 255) {
        Hard(L, function, "Expected number within the destination type range at argument " + std::to_string(index) + ", got out-of-range number");
        return false;
    }
    out = static_cast<int>(number);
    return true;
}

int SetMarkerTargetArrowProperties(lua_State* L) {
    Marker marker;
    if (!RequireMarker(L, "setMarkerTargetArrowProperties", marker)) return 0;
    int r = 0, g = 0, b = 0, a = 0;
    if (!ReadOptionalByte(L, "setMarkerTargetArrowProperties", 2, 255, r)) return 0;
    if (!ReadOptionalByte(L, "setMarkerTargetArrowProperties", 3, 64, g)) return 0;
    if (!ReadOptionalByte(L, "setMarkerTargetArrowProperties", 4, 64, b)) return 0;
    if (!ReadOptionalByte(L, "setMarkerTargetArrowProperties", 5, 255, a)) return 0;
    float size = marker.size * 0.625f;
    if (!lua_isnoneornil(L, 6)) {
        if (!lua_isnumber(L, 6)) return Hard(L, "setMarkerTargetArrowProperties", "Expected number at argument 6, got " + ParserGot(L, 6));
        const lua_Number number = lua_tonumber(L, 6);
        if (std::isnan(number)) return Hard(L, "setMarkerTargetArrowProperties", "Expected number at argument 6, got NaN");
        if (!std::isfinite(number)) return Hard(L, "setMarkerTargetArrowProperties", "Expected number at argument 6, got inf");
        size = static_cast<float>(number);
    }
    if (!CheckpointFamily(marker.kind) || marker.icon != 1) return Fail(L);
    const uint32_t id = marker.id;
    bool changed = false;
    Each(id, [&](uint32_t child) {
        Marker* item = Edit(child);
        if (!item || !CheckpointFamily(item->kind) || item->icon != 1) return;
        item->arrowR = r;
        item->arrowG = g;
        item->arrowB = b;
        item->arrowA = a;
        item->arrowSize = size;
        changed = true;
    });
    lua_pushboolean(L, changed ? 1 : 0);
    return 1;
}

int SetCoronaReflectionEnabled(lua_State* L) {
    Marker marker;
    if (!RequireMarker(L, "setCoronaReflectionEnabled", marker)) return 0;
    if (lua_type(L, 2) != LUA_TBOOLEAN)
        return Hard(L, "setCoronaReflectionEnabled", "Expected boolean at argument 2, got " + ParserGot(L, 2));
    if (marker.kind != Kind::Corona) return Fail(L);
    if (Marker* item = Edit(marker.id)) item->reflection = lua_toboolean(L, 2) != 0;
    lua_pushboolean(L, 1);
    return 1;
}

int IsCoronaReflectionEnabled(lua_State* L) {
    Marker marker;
    if (!RequireMarker(L, "isCoronaReflectionEnabled", marker)) return 0;
    lua_pushboolean(L, marker.kind == Kind::Corona && marker.reflection ? 1 : 0);
    return 1;
}

int RenderScriptImportantArea(lua_State* L) {
    // CLuaMarkerDefs::RenderScriptImportantArea (48-75). No C3DMarkers, so a parsed call returns false.
    float x = 0, y = 0, z = 0, rx = 0, ry = 0;
    if (!FiniteAt(L, 1, x) || !FiniteAt(L, 2, y) || !FiniteAt(L, 3, z))
        return Hard(L, "renderScriptImportantArea", "Expected vector3 at argument 1, got " + ParserGot(L, 1));
    if (!lua_isnumber(L, 4)) return Hard(L, "renderScriptImportantArea", "Expected number at argument 4, got " + ParserGot(L, 4));
    if (!lua_isnumber(L, 5)) return Hard(L, "renderScriptImportantArea", "Expected number at argument 5, got " + ParserGot(L, 5));
    const lua_Number radiusX = lua_tonumber(L, 4);
    const lua_Number radiusY = lua_tonumber(L, 5);
    if (std::isnan(radiusX)) return Hard(L, "renderScriptImportantArea", "Expected number at argument 4, got NaN");
    if (std::isnan(radiusY)) return Hard(L, "renderScriptImportantArea", "Expected number at argument 5, got NaN");
    if (!std::isfinite(radiusX)) return Hard(L, "renderScriptImportantArea", "Expected number at argument 4, got inf");
    if (!std::isfinite(radiusY)) return Hard(L, "renderScriptImportantArea", "Expected number at argument 5, got inf");
    rx = static_cast<float>(radiusX);
    ry = static_cast<float>(radiusY);
    if (!lua_isnoneornil(L, 6)) {
        if (!lua_isnumber(L, 6)) return Hard(L, "renderScriptImportantArea", "Expected number at argument 6, got " + ParserGot(L, 6));
        const lua_Number id = lua_tonumber(L, 6);
        if (std::isnan(id) || !std::isfinite(id) || id < 0 || id > 4294967295.0)
            return Hard(L, "renderScriptImportantArea", "Expected number within the destination type range at argument 6, got out-of-range number");
    }
    if (rx <= 0.f || ry <= 0.f || ActiveVmOwner().empty()) return Fail(L);
    if (!g_diagArea) {
        g_diagArea = true;
        LUA_LOGE("DIAG renderScriptImportantArea: C3DMarkers::RenderScriptImportantArea is not hooked");
    }
    return Fail(L);
}

int GetElementPositionMarker(lua_State* L) {
    Marker marker;
    if (!Resolve(ElementIdOf(L, 1), marker)) {
        if (!lua_isfunction(L, lua_upvalueindex(1))) return Fail(L);
        const int count = lua_gettop(L);
        lua_pushvalue(L, lua_upvalueindex(1));
        lua_insert(L, 1);
        lua_call(L, count, LUA_MULTRET);
        return lua_gettop(L);
    }
    lua_pushnumber(L, marker.x);
    lua_pushnumber(L, marker.y);
    lua_pushnumber(L, marker.z);
    return 3;
}

int SetElementPositionMarker(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    ServerWorld::MarkerFields fields;
    if (!Find(id) && !ServerWorld::CopyMarkerFields(id, fields)) {
        if (!lua_isfunction(L, lua_upvalueindex(1))) return Fail(L);
        const int count = lua_gettop(L);
        lua_pushvalue(L, lua_upvalueindex(1));
        lua_insert(L, 1);
        lua_call(L, count, LUA_MULTRET);
        return lua_gettop(L);
    }
    float x = 0, y = 0, z = 0;
    if (!FiniteAt(L, 2, x) || !FiniteAt(L, 3, y) || !FiniteAt(L, 4, z)) return Fail(L);
    Marker* marker = Edit(id);
    if (!marker) return Fail(L);
    marker->x = x;
    marker->y = y;
    marker->z = z;
    RetainMarkerColShape(marker->id, static_cast<int>(marker->kind), marker->x, marker->y, marker->z, marker->size);
    if (marker->server) ServerWorld::NoteElementPosition(id, x, y, z);
    lua_pushboolean(L, 1);
    return 1;
}

void Register(lua_State* L) {
    g_markers.clear();
    const struct { const char* name; lua_CFunction fn; } binds[] = {
        {"createMarker", CreateMarker},
        {"renderScriptImportantArea", RenderScriptImportantArea},
        {"getMarkerCount", GetMarkerCount},
        {"getMarkerLimitStats", GetMarkerLimitStats},
        {"getMarkerType", GetMarkerType},
        {"getMarkerSize", GetMarkerSize},
        {"getMarkerColor", GetMarkerColor},
        {"getMarkerTarget", GetMarkerTarget},
        {"getMarkerIcon", GetMarkerIcon},
        {"getMarkerTargetArrowProperties", GetMarkerTargetArrowProperties},
        {"setMarkerType", SetMarkerType},
        {"setMarkerSize", SetMarkerSize},
        {"setMarkerColor", SetMarkerColor},
        {"setMarkerTarget", SetMarkerTarget},
        {"setMarkerIcon", SetMarkerIcon},
        {"setMarkerTargetArrowProperties", SetMarkerTargetArrowProperties},
        {"setCoronaReflectionEnabled", SetCoronaReflectionEnabled},
        {"isCoronaReflectionEnabled", IsCoronaReflectionEnabled},
    };
    for (const auto& bind : binds) {
        lua_pushcfunction(L, bind.fn);
        lua_setglobal(L, bind.name);
    }
    lua_getglobal(L, "getElementPosition");
    lua_pushcclosure(L, GetElementPositionMarker, 1);
    lua_setglobal(L, "getElementPosition");
    lua_getglobal(L, "setElementPosition");
    lua_pushcclosure(L, SetElementPositionMarker, 1);
    lua_setglobal(L, "setElementPosition");
}

}  // namespace

void MarkerRelease(uint32_t id) {
    g_markers.erase(id);
    ReleaseMarkerColShape(id);
}

uint32_t MarkerColShape(uint32_t id) {
    Marker marker;
    if (!Resolve(id, marker)) return 0;
    return RetainMarkerColShape(marker.id, static_cast<int>(marker.kind), marker.x, marker.y, marker.z, marker.size);
}

void InstallMarkerLua(lua_State* L) { Register(L); }

}  // namespace WorldRPG::Script
