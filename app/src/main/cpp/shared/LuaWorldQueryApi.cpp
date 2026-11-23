// World queries that do not need a live GTA pool for their argument rules.
// Zones: CZoneNames.cpp. Buildings: CLuaBuildingDefs.cpp 36-79 and WorldLimits.h.
// Explosions and ropes: CLuaWorldDefs.cpp 215-244 and 2649-2670. They return true
// without spawning. World-model removal returns a count of 0 because the building
// removal list is not loaded. Garage geometry reads CGarage, which is not hooked.

#include "LuaWorldQueryApi.h"

#include "LuaApiExtra.h"
#include "LuaVMStub.h"

#include "lua.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace WorldRPG::Script {
namespace {

#include "ZoneTable.inc"

const char* kCities[] = {"Tierra Robada", "Bone County", "Las Venturas", "San Fierro", "Red County", "Whetstone", "Flint County", "Los Santos"};

bool g_diagGarage = false;
bool g_diagExplosion = false;
bool g_diagRope = false;
bool g_diagRemoval = false;
bool g_diagWorld = false;
bool g_gameWorldRemoved = false;

const char* TypeName(int type) {
    switch (type) {
        case LUA_TNIL: return "nil";
        case LUA_TBOOLEAN: return "boolean";
        case LUA_TNUMBER: return "number";
        case LUA_TSTRING: return "string";
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
    char buf[256];
    if (value && value[0] != '\0')
        std::snprintf(buf, sizeof(buf), "Expected %s at argument %d, got %s '%.64s'", expected, index, TypeName(type), value);
    else
        std::snprintf(buf, sizeof(buf), "Expected %s at argument %d, got %s", expected, index, TypeName(type));
    LogFull(function, "Bad argument", buf);
}

int Fail(lua_State* L) {
    lua_pushboolean(L, 0);
    return 1;
}

bool Finite(lua_State* L, int index, float& out) {
    if (!lua_isnumber(L, index)) return false;
    const lua_Number number = lua_tonumber(L, index);
    if (!std::isfinite(number)) return false;
    out = static_cast<float>(number);
    return true;
}

bool ReadNumber(lua_State* L, const char* function, int index, float& out) {
    const int type = lua_type(L, index);
    if (type != LUA_TNUMBER && type != LUA_TSTRING) {
        LogType(L, function, "number", index);
        return false;
    }
    if (!Finite(L, index, out)) {
        LogFull(function, "Bad argument", "Expected number, got NaN");
        return false;
    }
    return true;
}

bool ReadVec(lua_State* L, const char* function, int index, float& x, float& y, float& z) {
    const int type = lua_type(L, index);
    if (type != LUA_TNUMBER && type != LUA_TSTRING) {
        LogType(L, function, "vector3", index);
        return false;
    }
    return ReadNumber(L, function, index, x) && ReadNumber(L, function, index + 1, y) && ReadNumber(L, function, index + 2, z);
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

bool CityName(const char* name) {
    for (const char* city : kCities)
        if (std::strcmp(name, city) == 0) return true;
    return false;
}

bool Overlaps(const Zone& zone, float x, float y, float z, float radius) {
    return zone.x1 <= x + radius && zone.x2 >= x - radius && zone.y1 <= y + radius && zone.y2 >= y - radius && zone.z1 <= z &&
           zone.z2 >= z;
}

const char* ZoneAt(float x, float y, float z, bool cities) {
    // CZoneNames::GetZoneName / GetCityName. The search box is expanded in X and Y
    // only; Z stays on the query plane (CZoneNames.cpp 426-521).
    if (x < -3000.f || y < -3000.f || z < -3000.f || x > 3000.f || y > 3000.f || z > 3000.f) return "Unknown";
    float radius = 0.f;
    for (int step = 0; step < 10; ++step) {
        const Zone* best = nullptr;
        float bestSize = 0.f;
        for (const Zone& zone : kZones) {
            if (!Overlaps(zone, x, y, z, radius)) continue;
            if (cities) {
                if (CityName(zone.name)) return zone.name;
                continue;
            }
            const float size = static_cast<float>(zone.x2 - zone.x1) * static_cast<float>(zone.y2 - zone.y1) *
                               static_cast<float>(zone.z2 - zone.z1);
            if (!best || size < bestSize) {
                best = &zone;
                bestSize = size;
            }
        }
        if (!cities && best) return best->name;
        radius = std::pow(2.f, static_cast<float>(step));
    }
    return "Unknown";
}

bool BuildingModel(uint32_t model) {
    if (model >= 20000 || model <= 312) return false;
    if (model >= 313 && model <= 320) return false;
    if (model >= 384 && model <= 397) return false;
    if (model >= 400 && model <= 611) return false;
    return true;
}

int GetZoneName(lua_State* L) {
    float x = 0.f, y = 0.f, z = 0.f;
    if (!ReadVec(L, "getZoneName", 1, x, y, z)) return Fail(L);
    bool cities = false;
    if (!ReadBool(L, "getZoneName", 4, cities, true, false)) return Fail(L);
    lua_pushstring(L, ZoneAt(x, y, z, cities));
    return 1;
}

int CreateExplosion(lua_State* L) {
    float x = 0.f, y = 0.f, z = 0.f, type = 0.f, shake = -1.f;
    bool sound = true, damaging = true;
    if (!ReadVec(L, "createExplosion", 1, x, y, z) || !ReadNumber(L, "createExplosion", 4, type) ||
        !ReadBool(L, "createExplosion", 5, sound, true, true))
        return Fail(L);
    if (!lua_isnoneornil(L, 6) && !ReadNumber(L, "createExplosion", 6, shake)) return Fail(L);
    if (!ReadBool(L, "createExplosion", 7, damaging, true, true)) return Fail(L);
    (void)x;
    (void)y;
    (void)z;
    (void)type;
    (void)sound;
    (void)shake;
    (void)damaging;
    if (!g_diagExplosion) {
        g_diagExplosion = true;
        LUA_LOGE("DIAG createExplosion: CExplosionManager::Create is not hooked");
    }
    lua_pushboolean(L, 1);
    return 1;
}

int CreateSWATRope(lua_State* L) {
    float x = 0.f, y = 0.f, z = 0.f, duration = 4000.f;
    if (!ReadVec(L, "createSWATRope", 1, x, y, z)) return Fail(L);
    if (!lua_isnoneornil(L, 4) && !ReadNumber(L, "createSWATRope", 4, duration)) return Fail(L);
    (void)x;
    (void)y;
    (void)z;
    (void)duration;
    if (!g_diagRope) {
        g_diagRope = true;
        LUA_LOGE("DIAG createSWATRope: CRopes::CreateRopeForSwatPed is not hooked");
    }
    lua_pushboolean(L, 1);
    return 1;
}

int CreateBuilding(lua_State* L) {
    float model = 0.f, x = 0.f, y = 0.f, z = 0.f;
    if (!ReadNumber(L, "createBuilding", 1, model) || !ReadVec(L, "createBuilding", 2, x, y, z)) return Fail(L);
    if (model < 0.f || model > 65534.f || !BuildingModel(static_cast<uint32_t>(model)))
        return luaL_error(L, "Bad argument @ 'createBuilding' [Invalid building model id]");
    if (x < -10000.f || y < -10000.f || x > 9999.f || y > 9999.f)
        return luaL_error(L, "Bad argument @ 'createBuilding' [Position is outside of game world]");
    const uint32_t id = CreateResourceElement("building");
    if (!id) return Fail(L);
    PushElement(L, id);
    return 1;
}

bool RemovalArgs(lua_State* L, const char* function, float& model, float& radius, float& x, float& y, float& z) {
    if (!ReadNumber(L, function, 1, model) || model < 0.f || model > 19999.f) {
        if (model < 0.f || model > 19999.f) LogFull(function, "Bad argument", "Expected number within the supported range");
        return false;
    }
    return ReadNumber(L, function, 2, radius) && ReadVec(L, function, 3, x, y, z);
}

int RemoveWorldModel(lua_State* L) {
    float model = 0.f, radius = 0.f, x = 0.f, y = 0.f, z = 0.f;
    if (!RemovalArgs(L, "removeWorldModel", model, radius, x, y, z)) return Fail(L);
    if (ActiveVmOwner().empty()) return Fail(L);
    (void)model;
    (void)radius;
    (void)x;
    (void)y;
    (void)z;
    if (!g_diagRemoval) {
        g_diagRemoval = true;
        LUA_LOGE("DIAG removeWorldModel: CBuildingRemoval is not loaded; the removed count is 0");
    }
    lua_pushboolean(L, 1);
    lua_pushnumber(L, 0);
    return 2;
}

int RestoreWorldModel(lua_State* L) {
    float model = 0.f, radius = 0.f, x = 0.f, y = 0.f, z = 0.f;
    if (!RemovalArgs(L, "restoreWorldModel", model, radius, x, y, z)) return Fail(L);
    if (ActiveVmOwner().empty()) return Fail(L);
    (void)model;
    (void)radius;
    (void)x;
    (void)y;
    (void)z;
    lua_pushboolean(L, 1);
    lua_pushnumber(L, 0);
    return 2;
}

int RestoreAllWorldModels(lua_State* L) {
    (void)L;
    if (ActiveVmOwner().empty()) return Fail(L);
    lua_pushboolean(L, 1);
    lua_pushnumber(L, 0);
    return 2;
}

int RemoveGameWorld(lua_State* L) {
    (void)L;
    g_gameWorldRemoved = true;
    if (!g_diagWorld) {
        g_diagWorld = true;
        LUA_LOGE("DIAG removeGameWorld: CGame::RemoveGameWorld is not hooked; scripted buildings stay");
    }
    lua_pushboolean(L, 1);
    return 1;
}

int RestoreGameWorld(lua_State* L) {
    (void)L;
    g_gameWorldRemoved = false;
    lua_pushboolean(L, 1);
    return 1;
}

int GarageQuery(lua_State* L, const char* function) {
    float id = 0.f;
    if (!ReadNumber(L, function, 1, id)) return Fail(L);
    (void)static_cast<unsigned char>(static_cast<int>(id));
    if (!g_diagGarage) {
        g_diagGarage = true;
        LUA_LOGE("DIAG %s: CGarages is not readable", function);
    }
    return Fail(L);
}

int GetGaragePosition(lua_State* L) { return GarageQuery(L, "getGaragePosition"); }
int GetGarageSize(lua_State* L) { return GarageQuery(L, "getGarageSize"); }
int GetGarageBoundingBox(lua_State* L) { return GarageQuery(L, "getGarageBoundingBox"); }

int GarageLease(lua_State* L, const char* function) {
    if (!lua_isnumber(L, 1)) return luaL_error(L, "Bad argument @ '%s' [Expected number at argument 1]", function);
    if (!g_diagGarage) {
        g_diagGarage = true;
        LUA_LOGE("DIAG %s: CGarages is not readable", function);
    }
    return Fail(L);
}

int AcquireGarageControl(lua_State* L) { return GarageLease(L, "acquireGarageControl"); }
int ReleaseGarageControl(lua_State* L) { return GarageLease(L, "releaseGarageControl"); }

void Register(lua_State* L) {
    g_gameWorldRemoved = false;
    const struct {
        const char* name;
        lua_CFunction fn;
    } binds[] = {
        {"getZoneName", GetZoneName},
        {"createExplosion", CreateExplosion},
        {"createSWATRope", CreateSWATRope},
        {"createBuilding", CreateBuilding},
        {"removeWorldModel", RemoveWorldModel},
        {"restoreWorldModel", RestoreWorldModel},
        {"restoreAllWorldModels", RestoreAllWorldModels},
        {"removeGameWorld", RemoveGameWorld},
        {"restoreGameWorld", RestoreGameWorld},
        {"removeAllGameBuildings", RemoveGameWorld},
        {"restoreAllGameBuildings", RestoreGameWorld},
        {"getGaragePosition", GetGaragePosition},
        {"getGarageSize", GetGarageSize},
        {"getGarageBoundingBox", GetGarageBoundingBox},
        {"acquireGarageControl", AcquireGarageControl},
        {"releaseGarageControl", ReleaseGarageControl},
    };
    for (const auto& bind : binds) {
        lua_pushcfunction(L, bind.fn);
        lua_setglobal(L, bind.name);
    }
}

}  // namespace

void InstallWorldQueryLua(lua_State* L) { Register(L); }

}  // namespace WorldRPG::Script
