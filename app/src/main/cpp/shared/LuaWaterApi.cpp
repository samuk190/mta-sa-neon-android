// Scripted water. Desktop:
//   CLuaWaterDefs.cpp 70-431
//   WaterFlags.h
//   CStaticFunctionDefinitions.cpp 6693-6755
//   CClientWater.cpp 15-180 (vertex order, 1-based Lua index, level is the average Z)
//   CWaterManagerSA.cpp 1254-1314 (segment vs polygon, then the outside-world ocean)
// The GTA sea mesh is not queried. CWaterPoly is not created.

#include "LuaWaterApi.h"

#include "LuaApiExtra.h"
#include "LuaVMStub.h"
#include "ResourceLoader.h"

#include "lua.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace WorldRPG::Script {
namespace {

struct Vec3 {
    float x = 0.f;
    float y = 0.f;
    float z = 0.f;
};

struct Water {
    uint32_t id = 0;
    std::vector<Vec3> verts;
    unsigned char flags = 1;
};

std::unordered_map<uint32_t, Water> g_water;
float g_worldLevel = 0.f;
float g_outsideLevel = 0.f;
bool g_diagSea = false;
bool g_diagPoly = false;
bool g_diagLevel = false;

const char* LuaTypeName(int type) {
    switch (type) {
        case LUA_TNIL: return "nil";
        case LUA_TBOOLEAN: return "boolean";
        case LUA_TNUMBER: return "number";
        case LUA_TSTRING: return "string";
        case LUA_TTABLE: return "table";
        default: return type == LUA_TNONE ? "none" : "userdata";
    }
}

void LogFull(const char* function, const char* category, const char* message) {
    char buf[512];
    std::snprintf(buf, sizeof(buf), "%s @ '%s' [%s]", category, function, message);
    LUA_LOGI("WARNING: %s", buf);
}

void LogType(lua_State* L, const char* function, const char* expected, int index) {
    const int type = lua_type(L, index);
    const char* value = (type == LUA_TNUMBER || type == LUA_TSTRING) ? lua_tostring(L, index) : nullptr;
    char buf[512];
    if (value && value[0] != '\0') {
        std::snprintf(buf, sizeof(buf), "Bad argument @ '%s' [Expected %s at argument %d, got %s '%.64s']", function, expected,
                      index, LuaTypeName(type), value);
    } else {
        std::snprintf(buf, sizeof(buf), "Bad argument @ '%s' [Expected %s at argument %d, got %s]", function, expected, index,
                      LuaTypeName(type));
    }
    LUA_LOGI("WARNING: %s", buf);
}

int Fail(lua_State* L) {
    lua_pushboolean(L, 0);
    return 1;
}

bool IsElementArg(lua_State* L, int index) {
    if (!lua_istable(L, index)) return false;
    lua_getfield(L, index, "element");
    const bool element = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    return element;
}

bool ReadFinite(lua_State* L, const char* function, int index, float& out) {
    const int type = lua_type(L, index);
    if (type != LUA_TNUMBER && type != LUA_TSTRING) {
        LogType(L, function, "number", index);
        return false;
    }
    if (!lua_isnumber(L, index)) {
        LogFull(function, "Bad argument", "Expected number, got non-convertible string");
        return false;
    }
    const lua_Number number = lua_tonumber(L, index);
    if (std::isnan(number)) {
        LogFull(function, "Bad argument", "Expected number, got NaN");
        return false;
    }
    if (!std::isfinite(number)) {
        LogFull(function, "Bad argument", "Expected number within the supported range");
        return false;
    }
    out = static_cast<float>(number);
    return true;
}

bool ReadVec(lua_State* L, const char* function, int index, Vec3& out) {
    const int type = lua_type(L, index);
    if (type != LUA_TNUMBER && type != LUA_TSTRING) {
        LogType(L, function, "vector3", index);
        return false;
    }
    return ReadFinite(L, function, index, out.x) && ReadFinite(L, function, index + 1, out.y) &&
           ReadFinite(L, function, index + 2, out.z);
}

bool ReadBool(lua_State* L, const char* function, int index, bool& out, bool optional, bool fallback) {
    const int type = lua_type(L, index);
    if (type == LUA_TBOOLEAN) {
        out = lua_toboolean(L, index) != 0;
        return true;
    }
    if (optional && (type == LUA_TNONE || type == LUA_TNIL)) {
        out = fallback;
        return true;
    }
    LogType(L, function, "boolean", index);
    return false;
}

bool CouldBeNumber(lua_State* L, int index) {
    const int type = lua_type(L, index);
    return (type == LUA_TNUMBER || type == LUA_TSTRING) && lua_isnumber(L, index);
}

Water* Find(uint32_t id) {
    auto found = g_water.find(id);
    if (found == g_water.end()) return nullptr;
    if (!KnownClientElement(id)) {
        g_water.erase(found);
        return nullptr;
    }
    return &found->second;
}

bool ReadWater(lua_State* L, const char* function, Water*& out) {
    if (!IsElementArg(L, 1) || !(out = Find(ElementIdOf(L, 1)))) {
        LogType(L, function, "water", 1);
        return false;
    }
    return true;
}

float Dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 Sub(const Vec3& a, const Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 Add(const Vec3& a, const Vec3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 Mul(const Vec3& a, float s) { return {a.x * s, a.y * s, a.z * s}; }
Vec3 Cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
float Length(const Vec3& a) { return std::sqrt(Dot(a, a)); }

// CVector::IntersectsSegmentTriangle (CVector.h 147-198).
bool SegmentTriangle(const Vec3& origin, const Vec3& dir, const Vec3& v1, const Vec3& v2, const Vec3& v3, Vec3& out) {
    constexpr float kEps = 1e-6f;
    const float span = Length(dir);
    if (span < kEps) return false;
    const Vec3 ray = Mul(dir, 1.f / span);
    const Vec3 edge1 = Sub(v2, v1);
    const Vec3 edge2 = Sub(v3, v1);
    const Vec3 h = Cross(ray, edge2);
    const float a = Dot(edge1, h);
    if (a > -kEps && a < kEps) return false;
    const float f = 1.f / a;
    const Vec3 s = Sub(origin, v1);
    const float u = f * Dot(s, h);
    if (u < 0.f || u > 1.f) return false;
    const Vec3 sCross = Cross(s, edge1);
    const float v = f * Dot(ray, sCross);
    if (v < 0.f || u + v > 1.f) return false;
    const float t = f * Dot(edge2, sCross);
    if (t > kEps && t <= span) {
        out = Add(origin, Mul(ray, t));
        return true;
    }
    return false;
}

bool SurfaceAt(const Vec3& a, const Vec3& b, const Vec3& c, float x, float y, float& z) {
    const float denom = (b.y - c.y) * (a.x - c.x) + (c.x - b.x) * (a.y - c.y);
    if (std::fabs(denom) < 1e-6f) return false;
    const float u = ((b.y - c.y) * (x - c.x) + (c.x - b.x) * (y - c.y)) / denom;
    const float v = ((c.y - a.y) * (x - c.x) + (a.x - c.x) * (y - c.y)) / denom;
    const float w = 1.f - u - v;
    if (u < -1e-4f || v < -1e-4f || w < -1e-4f) return false;
    z = u * a.z + v * b.z + w * c.z;
    return true;
}

bool PolyHeight(const Water& water, float x, float y, float& z) {
    if (water.verts.size() < 3) return false;
    if (SurfaceAt(water.verts[0], water.verts[1], water.verts[2], x, y, z)) return true;
    if (water.verts.size() < 4) return false;
    return SurfaceAt(water.verts[1], water.verts[2], water.verts[3], x, y, z);
}

Water* PolyAt(float x, float y) {
    for (auto& [id, water] : g_water) {
        (void)id;
        if (!KnownClientElement(water.id)) continue;
        float z = 0.f;
        if (PolyHeight(water, x, y, z)) return &water;
    }
    return nullptr;
}

void SetPolyLevel(Water& water, float level) {
    for (Vec3& vert : water.verts) vert.z = level;
}

float AverageZ(const Water& water) {
    float sum = 0.f;
    for (const Vec3& vert : water.verts) sum += vert.z;
    return water.verts.empty() ? 0.f : sum / static_cast<float>(water.verts.size());
}

void NoteSea() {
    if (g_diagSea) return;
    g_diagSea = true;
    LUA_LOGE("DIAG getWaterLevel: CWaterManager::GetWaterLevel is not hooked; only scripted water is tested and the distance threshold is not applied");
}

void NotePoly() {
    if (g_diagPoly) return;
    g_diagPoly = true;
    LUA_LOGE("DIAG createWater: CWaterPoly is not created; the polygon is stored");
}

int CreateWater(lua_State* L) {
    // CLuaWaterDefs::CreateWater (70-113). Three corners, an optional fourth, then flags.
    Vec3 v1{}, v2{}, v3{}, v4{};
    if (!ReadVec(L, "createWater", 1, v1) || !ReadVec(L, "createWater", 4, v2) || !ReadVec(L, "createWater", 7, v3)) return Fail(L);
    int next = 10;
    const bool quad = CouldBeNumber(L, 12);
    if (quad) {
        if (!ReadVec(L, "createWater", 10, v4)) return Fail(L);
        next = 13;
    }
    unsigned char flags = 1;
    if (CouldBeNumber(L, next)) {
        float number = 0.f;
        if (!ReadFinite(L, "createWater", next, number)) return Fail(L);
        if (number <= -1.f || number >= 4.f) {
            LogFull("createWater", "Bad argument", "Expected a water.dat flag from 0 to 3");
            return Fail(L);
        }
        flags = static_cast<unsigned char>(static_cast<int>(number));
    } else {
        bool shallow = false;
        if (!ReadBool(L, "createWater", next, shallow, true, false)) return Fail(L);
        flags = shallow ? 0 : 1;  // WaterFlags::FromLegacyShallow
    }
    const uint32_t id = CreateResourceElement("water");
    if (!id) return Fail(L);
    Water created;
    created.id = id;
    created.flags = flags;
    created.verts = {v1, v2, v3};
    if (quad) created.verts.push_back(v4);
    g_water[id] = created;
    NotePoly();
    PushElement(L, id);
    return 1;
}

int GetWaterLevel(lua_State* L) {
    if (IsElementArg(L, 1)) {
        Water* water = nullptr;
        if (!ReadWater(L, "getWaterLevel", water)) return Fail(L);
        lua_pushnumber(L, AverageZ(*water));
        return 1;
    }
    Vec3 pos{};
    if (!ReadVec(L, "getWaterLevel", 1, pos)) return Fail(L);
    bool ignore = false;
    if (!ReadBool(L, "getWaterLevel", 4, ignore, true, false)) return Fail(L);
    (void)ignore;
    NoteSea();
    if (Water* water = PolyAt(pos.x, pos.y)) {
        float z = 0.f;
        if (PolyHeight(*water, pos.x, pos.y, z)) {
            lua_pushnumber(L, z);
            return 1;
        }
    }
    return Fail(L);
}

int GetWaterVertexPosition(lua_State* L) {
    Water* water = nullptr;
    float index = 0.f;
    if (!ReadWater(L, "getWaterVertexPosition", water) || !ReadFinite(L, "getWaterVertexPosition", 2, index)) return Fail(L);
    const int slot = static_cast<int>(index) - 1;
    if (slot < 0 || slot >= static_cast<int>(water->verts.size())) return Fail(L);
    const Vec3& vert = water->verts[static_cast<size_t>(slot)];
    lua_pushnumber(L, vert.x);
    lua_pushnumber(L, vert.y);
    lua_pushnumber(L, vert.z);
    return 3;
}

int SetWaterVertexPosition(lua_State* L) {
    Water* water = nullptr;
    float index = 0.f;
    Vec3 pos{};
    if (!ReadWater(L, "setWaterVertexPosition", water) || !ReadFinite(L, "setWaterVertexPosition", 2, index) ||
        !ReadVec(L, "setWaterVertexPosition", 3, pos))
        return Fail(L);
    const int slot = static_cast<int>(index) - 1;
    if (slot < 0 || slot >= static_cast<int>(water->verts.size())) return Fail(L);
    water->verts[static_cast<size_t>(slot)] = pos;
    lua_pushboolean(L, 1);
    return 1;
}

int SetWaterLevel(lua_State* L) {
    // Numeric forms keep working with no resource: setWaterLevel(level) is already host-tested.
    // The element form follows CLuaWaterDefs::SetWaterLevel (204-221).
    if (IsElementArg(L, 1)) {
        Water* water = nullptr;
        float level = 0.f;
        if (!ReadWater(L, "setWaterLevel", water) || !ReadFinite(L, "setWaterLevel", 2, level)) return Fail(L);
        SetPolyLevel(*water, level);
        lua_pushboolean(L, 1);
        return 1;
    }
    if (CouldBeNumber(L, 2)) {
        Vec3 pos{};
        float level = 0.f;
        if (!ReadVec(L, "setWaterLevel", 1, pos) || !ReadFinite(L, "setWaterLevel", 4, level)) return Fail(L);
        Water* water = PolyAt(pos.x, pos.y);
        if (!water) return Fail(L);
        SetPolyLevel(*water, level);
        lua_pushboolean(L, 1);
        return 1;
    }
    float level = 0.f;
    if (!ReadFinite(L, "setWaterLevel", 1, level)) return Fail(L);
    bool nonSea = true, all = true, sea = true, outside = false;
    if (!ReadBool(L, "setWaterLevel", 2, nonSea, true, true) || !ReadBool(L, "setWaterLevel", 3, all, true, true) ||
        !ReadBool(L, "setWaterLevel", 4, sea, true, true) || !ReadBool(L, "setWaterLevel", 5, outside, true, false))
        return Fail(L);
    (void)nonSea;
    (void)sea;
    if (all) {
        for (auto& [id, water] : g_water) {
            (void)id;
            if (KnownClientElement(water.id)) SetPolyLevel(water, level);
        }
    }
    g_worldLevel = level;
    if (outside) g_outsideLevel = level;
    if (!g_diagLevel) {
        g_diagLevel = true;
        LUA_LOGE("DIAG setWaterLevel: CWaterManager world vertices are not written; the level is stored");
    }
    lua_pushboolean(L, 1);
    return 1;
}

int ResetWaterLevel(lua_State* L) {
    (void)L;
    g_worldLevel = 0.f;
    g_outsideLevel = 0.f;
    lua_pushboolean(L, 1);
    return 1;
}

bool HitPoly(const Water& water, const Vec3& start, const Vec3& dir, Vec3& hit) {
    if (water.verts.size() < 3) return false;
    if (SegmentTriangle(start, dir, water.verts[0], water.verts[1], water.verts[2], hit)) return true;
    if (water.verts.size() < 4) return false;
    return SegmentTriangle(start, dir, water.verts[1], water.verts[2], water.verts[3], hit);
}

int TestLineAgainstWater(lua_State* L) {
    Vec3 start{}, end{};
    if (!ReadVec(L, "testLineAgainstWater", 1, start) || !ReadVec(L, "testLineAgainstWater", 4, end)) return Fail(L);
    const Vec3 dir = Sub(end, start);
    Vec3 hit{};
    for (const auto& [id, water] : g_water) {
        (void)id;
        if (!KnownClientElement(water.id)) continue;
        if (HitPoly(water, start, dir, hit)) {
            lua_pushboolean(L, 1);
            lua_pushnumber(L, hit.x);
            lua_pushnumber(L, hit.y);
            lua_pushnumber(L, hit.z);
            return 4;
        }
    }
    // CWaterManagerSA::TestLineAgainstWater: the outside ocean is an infinite plane,
    // and only a hit outside the -3000..3000 game square counts.
    const float span = Length(dir);
    if (span > 1e-6f) {
        const Vec3 ray = Mul(dir, 1.f / span);
        const float denom = ray.z;
        if (std::fabs(denom) > 1e-4f) {
            const float dist = (g_outsideLevel - start.z) / denom;
            if (dist >= 0.f && dist <= span) {
                const Vec3 at = Add(start, Mul(ray, dist));
                if (at.x < -3000.f || at.x > 3000.f || at.y < -3000.f || at.y > 3000.f) {
                    lua_pushboolean(L, 1);
                    lua_pushnumber(L, at.x);
                    lua_pushnumber(L, at.y);
                    lua_pushnumber(L, at.z);
                    return 4;
                }
            }
        }
    }
    return Fail(L);
}

void Register(lua_State* L) {
    g_water.clear();
    g_worldLevel = 0.f;
    g_outsideLevel = 0.f;
    const struct {
        const char* name;
        lua_CFunction fn;
    } binds[] = {
        {"createWater", CreateWater},
        {"getWaterLevel", GetWaterLevel},
        {"getWaterVertexPosition", GetWaterVertexPosition},
        {"setWaterVertexPosition", SetWaterVertexPosition},
        {"setWaterLevel", SetWaterLevel},
        {"resetWaterLevel", ResetWaterLevel},
        {"testLineAgainstWater", TestLineAgainstWater},
    };
    for (const auto& bind : binds) {
        lua_pushcfunction(L, bind.fn);
        lua_setglobal(L, bind.name);
    }
}

}  // namespace

void WaterRelease(uint32_t id) { g_water.erase(id); }

void InstallWaterLua(lua_State* L) { Register(L); }

}  // namespace WorldRPG::Script
