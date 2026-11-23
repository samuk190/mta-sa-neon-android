// Managed fires. Desktop CLuaFireDefs.cpp 175-541 and CClientFireManager.h damage bits.
// The numeric createFire form still returns a boolean. The options table creates a
// local "fire" element. CFire and FxSystem are not started, and DoPulse does not run,
// so remaining time does not tick down.

#include "LuaFireApi.h"

#include "LuaApiExtra.h"
#include "LuaVMStub.h"

#include "lua.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>

namespace WorldRPG::Script {
namespace {

constexpr unsigned char kPlayers = 1 << 0;
constexpr unsigned char kPeds = 1 << 1;
constexpr unsigned char kVehicles = 1 << 2;
constexpr unsigned char kObjects = 1 << 3;
constexpr unsigned char kAll = kPlayers | kPeds | kVehicles | kObjects;

struct Fire {
    uint32_t id = 0;
    float x = 0.f;
    float y = 0.f;
    float z = 0.f;
    double duration = 5000;
    double remaining = 5000;
    double strength = 1;
    bool damage = true;
    unsigned char mask = kAll;
    bool spread = false;
    double maxGenerations = 0;
    double generation = 0;
    uint32_t source = 0;
    uint32_t target = 0;
};

std::unordered_map<uint32_t, Fire> g_fires;
bool g_diagNumeric = false;
bool g_diagManaged = false;
bool g_diagExtinguish = false;

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

bool IsElementArg(lua_State* L, int index) {
    if (!lua_istable(L, index)) return false;
    lua_getfield(L, index, "element");
    const bool element = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    return element;
}

Fire* Find(uint32_t id) {
    auto found = g_fires.find(id);
    if (found == g_fires.end()) return nullptr;
    if (!KnownClientElement(id)) {
        g_fires.erase(found);
        return nullptr;
    }
    return &found->second;
}

bool TableBool(lua_State* L, int table, const char* key, bool fallback) {
    lua_getfield(L, table, key);
    const bool value = lua_isboolean(L, -1) ? lua_toboolean(L, -1) != 0 : fallback;
    lua_pop(L, 1);
    return value;
}

double TableNumber(lua_State* L, int table, const char* key, double fallback) {
    lua_getfield(L, table, key);
    const double value = lua_isnumber(L, -1) ? lua_tonumber(L, -1) : fallback;
    lua_pop(L, 1);
    return value;
}

uint32_t TableElement(lua_State* L, int table, const char* key) {
    lua_getfield(L, table, key);
    uint32_t id = 0;
    if (IsElementArg(L, -1) && KnownClientElement(ElementIdOf(L, -1))) id = ElementIdOf(L, -1);
    lua_pop(L, 1);
    return id;
}

unsigned char DamageMask(lua_State* L, int options) {
    lua_getfield(L, options, "damageTargets");
    unsigned char mask = kAll;
    if (lua_istable(L, -1)) {
        const int table = lua_gettop(L);
        mask = 0;
        if (TableBool(L, table, "players", true)) mask = static_cast<unsigned char>(mask | kPlayers);
        if (TableBool(L, table, "peds", true)) mask = static_cast<unsigned char>(mask | kPeds);
        if (TableBool(L, table, "vehicles", true)) mask = static_cast<unsigned char>(mask | kVehicles);
        if (TableBool(L, table, "objects", true)) mask = static_cast<unsigned char>(mask | kObjects);
    }
    lua_pop(L, 1);
    return mask;
}

void PushMask(lua_State* L, unsigned char mask) {
    lua_createtable(L, 0, 4);
    lua_pushboolean(L, (mask & kPlayers) != 0);
    lua_setfield(L, -2, "players");
    lua_pushboolean(L, (mask & kPeds) != 0);
    lua_setfield(L, -2, "peds");
    lua_pushboolean(L, (mask & kVehicles) != 0);
    lua_setfield(L, -2, "vehicles");
    lua_pushboolean(L, (mask & kObjects) != 0);
    lua_setfield(L, -2, "objects");
}

bool ReadNumber(lua_State* L, int index, double& out) {
    if (!lua_isnumber(L, index)) return false;
    const lua_Number number = lua_tonumber(L, index);
    if (!std::isfinite(number)) return false;
    out = number;
    return true;
}

void PushLinked(lua_State* L, uint32_t id) {
    if (id != 0 && KnownClientElement(id)) PushElement(L, id);
    else lua_pushboolean(L, 0);
}

int CreateFire(lua_State* L) {
    // CLuaFireDefs::CreateFire (175-240).
    float x = 0.f, y = 0.f, z = 0.f;
    if (!Finite(L, 1, x) || !Finite(L, 2, y) || !Finite(L, 3, z)) return Fail(L);
    if (!lua_istable(L, 4)) {
        if (!lua_isnoneornil(L, 4) && !lua_isnumber(L, 4)) return Fail(L);
        if (!g_diagNumeric) {
            g_diagNumeric = true;
            LUA_LOGE("DIAG createFire: CFireManager::StartFire is not hooked");
        }
        lua_pushboolean(L, 1);
        return 1;
    }
    const uint32_t id = CreateResourceElement("fire");
    if (!id) return Fail(L);
    Fire created;
    created.id = id;
    created.x = x;
    created.y = y;
    created.z = z;
    created.duration = std::max(0.0, TableNumber(L, 4, "duration", 5000.0));
    created.remaining = created.duration;
    created.strength = std::max(0.1, TableNumber(L, 4, "strength", 1.0));
    created.damage = TableBool(L, 4, "damage", true);
    created.spread = TableBool(L, 4, "spread", false);
    created.maxGenerations = std::max(0.0, TableNumber(L, 4, "maxGenerations", 0.0));
    created.mask = DamageMask(L, 4);
    created.source = TableElement(L, 4, "source");
    created.target = TableElement(L, 4, "target");
    g_fires[id] = created;
    if (!g_diagManaged) {
        g_diagManaged = true;
        LUA_LOGE("DIAG createFire: CClientFireManager does not pulse; FxSystem is not created and remaining time stays stored");
    }
    PushElement(L, id);
    return 1;
}

int ExtinguishFire(lua_State* L) {
    if (IsElementArg(L, 1)) {
        Fire* fire = Find(ElementIdOf(L, 1));
        if (!fire) return Fail(L);
        const uint32_t id = fire->id;
        if (!DestroyClientElement(id)) return Fail(L);
        lua_pushboolean(L, 1);
        return 1;
    }
    if (lua_gettop(L) == 0) {
        if (!g_diagExtinguish) {
            g_diagExtinguish = true;
            LUA_LOGE("DIAG extinguishFire: CFireManager::ExtinguishAllFires is not hooked");
        }
        lua_pushboolean(L, 1);
        return 1;
    }
    float x = 0.f, y = 0.f, z = 0.f;
    if (!Finite(L, 1, x) || !Finite(L, 2, y) || !Finite(L, 3, z)) return Fail(L);
    if (!lua_isnoneornil(L, 4) && !Finite(L, 4, x)) return Fail(L);
    (void)y;
    (void)z;
    if (!g_diagExtinguish) {
        g_diagExtinguish = true;
        LUA_LOGE("DIAG extinguishFire: CFireManager::ExtinguishPoint is not hooked");
    }
    lua_pushboolean(L, 1);
    return 1;
}

int GetFireDuration(lua_State* L) {
    Fire* fire = IsElementArg(L, 1) ? Find(ElementIdOf(L, 1)) : nullptr;
    if (!fire) return Fail(L);
    lua_pushnumber(L, fire->duration);
    return 1;
}

int SetFireDuration(lua_State* L) {
    Fire* fire = IsElementArg(L, 1) ? Find(ElementIdOf(L, 1)) : nullptr;
    double duration = 0;
    if (!fire || !ReadNumber(L, 2, duration) || duration < 0.0) return Fail(L);
    fire->duration = duration;
    fire->remaining = duration;
    lua_pushboolean(L, 1);
    return 1;
}

int GetFireRemainingTime(lua_State* L) {
    Fire* fire = IsElementArg(L, 1) ? Find(ElementIdOf(L, 1)) : nullptr;
    if (!fire) return Fail(L);
    lua_pushnumber(L, std::max(0.0, fire->remaining));
    return 1;
}

int SetFireRemainingTime(lua_State* L) {
    Fire* fire = IsElementArg(L, 1) ? Find(ElementIdOf(L, 1)) : nullptr;
    double remaining = 0;
    if (!fire || !ReadNumber(L, 2, remaining) || remaining < 0.0) return Fail(L);
    if (fire->duration <= 0.0 && remaining > 0.0) fire->duration = remaining;
    fire->remaining = remaining;
    lua_pushboolean(L, 1);
    return 1;
}

int GetFireStrength(lua_State* L) {
    Fire* fire = IsElementArg(L, 1) ? Find(ElementIdOf(L, 1)) : nullptr;
    if (!fire) return Fail(L);
    lua_pushnumber(L, fire->strength);
    return 1;
}

int SetFireStrength(lua_State* L) {
    Fire* fire = IsElementArg(L, 1) ? Find(ElementIdOf(L, 1)) : nullptr;
    double strength = 0;
    if (!fire || !ReadNumber(L, 2, strength) || !(strength > 0.0)) return Fail(L);
    fire->strength = strength;
    lua_pushboolean(L, 1);
    return 1;
}

int IsFireDamageEnabled(lua_State* L) {
    Fire* fire = IsElementArg(L, 1) ? Find(ElementIdOf(L, 1)) : nullptr;
    if (!fire) return Fail(L);
    lua_pushboolean(L, fire->damage ? 1 : 0);
    return 1;
}

int SetFireDamageEnabled(lua_State* L) {
    Fire* fire = IsElementArg(L, 1) ? Find(ElementIdOf(L, 1)) : nullptr;
    if (!fire || lua_type(L, 2) != LUA_TBOOLEAN) return Fail(L);
    fire->damage = lua_toboolean(L, 2) != 0;
    lua_pushboolean(L, 1);
    return 1;
}

int GetFireDamageTargets(lua_State* L) {
    Fire* fire = IsElementArg(L, 1) ? Find(ElementIdOf(L, 1)) : nullptr;
    if (!fire) return Fail(L);
    PushMask(L, fire->mask);
    return 1;
}

int SetFireDamageTargets(lua_State* L) {
    Fire* fire = IsElementArg(L, 1) ? Find(ElementIdOf(L, 1)) : nullptr;
    if (!fire || !lua_istable(L, 2)) return Fail(L);
    unsigned char mask = 0;
    if (TableBool(L, 2, "players", true)) mask = static_cast<unsigned char>(mask | kPlayers);
    if (TableBool(L, 2, "peds", true)) mask = static_cast<unsigned char>(mask | kPeds);
    if (TableBool(L, 2, "vehicles", true)) mask = static_cast<unsigned char>(mask | kVehicles);
    if (TableBool(L, 2, "objects", true)) mask = static_cast<unsigned char>(mask | kObjects);
    fire->mask = mask;
    lua_pushboolean(L, 1);
    return 1;
}

int IsFireSpreadEnabled(lua_State* L) {
    Fire* fire = IsElementArg(L, 1) ? Find(ElementIdOf(L, 1)) : nullptr;
    if (!fire) return Fail(L);
    lua_pushboolean(L, fire->spread ? 1 : 0);
    return 1;
}

int SetFireSpreadEnabled(lua_State* L) {
    Fire* fire = IsElementArg(L, 1) ? Find(ElementIdOf(L, 1)) : nullptr;
    if (!fire || lua_type(L, 2) != LUA_TBOOLEAN) return Fail(L);
    fire->spread = lua_toboolean(L, 2) != 0;
    lua_pushboolean(L, 1);
    return 1;
}

int GetFireMaxGenerations(lua_State* L) {
    Fire* fire = IsElementArg(L, 1) ? Find(ElementIdOf(L, 1)) : nullptr;
    if (!fire) return Fail(L);
    lua_pushnumber(L, fire->maxGenerations);
    return 1;
}

int SetFireMaxGenerations(lua_State* L) {
    Fire* fire = IsElementArg(L, 1) ? Find(ElementIdOf(L, 1)) : nullptr;
    double generations = 0;
    if (!fire || !ReadNumber(L, 2, generations) || generations < 0.0 || generations > 255.0) return Fail(L);
    fire->maxGenerations = generations;
    lua_pushboolean(L, 1);
    return 1;
}

int GetFireSource(lua_State* L) {
    Fire* fire = IsElementArg(L, 1) ? Find(ElementIdOf(L, 1)) : nullptr;
    if (!fire) return Fail(L);
    PushLinked(L, fire->source);
    return 1;
}

int SetFireSource(lua_State* L) {
    Fire* fire = IsElementArg(L, 1) ? Find(ElementIdOf(L, 1)) : nullptr;
    if (!fire) return Fail(L);
    if (lua_isnoneornil(L, 2)) {
        fire->source = 0;
        lua_pushboolean(L, 1);
        return 1;
    }
    if (!IsElementArg(L, 2) || !KnownClientElement(ElementIdOf(L, 2))) return Fail(L);
    fire->source = ElementIdOf(L, 2);
    lua_pushboolean(L, 1);
    return 1;
}

int GetFireTarget(lua_State* L) {
    Fire* fire = IsElementArg(L, 1) ? Find(ElementIdOf(L, 1)) : nullptr;
    if (!fire) return Fail(L);
    PushLinked(L, fire->target);
    return 1;
}

int SetFireTarget(lua_State* L) {
    Fire* fire = IsElementArg(L, 1) ? Find(ElementIdOf(L, 1)) : nullptr;
    if (!fire) return Fail(L);
    if (lua_isnoneornil(L, 2)) {
        fire->target = 0;
        lua_pushboolean(L, 1);
        return 1;
    }
    if (!IsElementArg(L, 2) || !KnownClientElement(ElementIdOf(L, 2))) return Fail(L);
    fire->target = ElementIdOf(L, 2);
    lua_pushboolean(L, 1);
    return 1;
}

void Register(lua_State* L) {
    g_fires.clear();
    const struct {
        const char* name;
        lua_CFunction fn;
    } binds[] = {
        {"createFire", CreateFire},
        {"extinguishFire", ExtinguishFire},
        {"getFireDuration", GetFireDuration},
        {"setFireDuration", SetFireDuration},
        {"getFireRemainingTime", GetFireRemainingTime},
        {"setFireRemainingTime", SetFireRemainingTime},
        {"getFireStrength", GetFireStrength},
        {"setFireStrength", SetFireStrength},
        {"isFireDamageEnabled", IsFireDamageEnabled},
        {"setFireDamageEnabled", SetFireDamageEnabled},
        {"getFireDamageTargets", GetFireDamageTargets},
        {"setFireDamageTargets", SetFireDamageTargets},
        {"isFireSpreadEnabled", IsFireSpreadEnabled},
        {"setFireSpreadEnabled", SetFireSpreadEnabled},
        {"getFireMaxGenerations", GetFireMaxGenerations},
        {"setFireMaxGenerations", SetFireMaxGenerations},
        {"getFireSource", GetFireSource},
        {"setFireSource", SetFireSource},
        {"getFireTarget", GetFireTarget},
        {"setFireTarget", SetFireTarget},
    };
    for (const auto& bind : binds) {
        lua_pushcfunction(L, bind.fn);
        lua_setglobal(L, bind.name);
    }
}

}  // namespace

void FireRelease(uint32_t id) { g_fires.erase(id); }

void InstallFireLua(lua_State* L) { Register(L); }

}  // namespace WorldRPG::Script
