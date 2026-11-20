#include "LuaStoredElementApi.h"
#include "LuaDxApi.h"
#include "LuaWeaponApi.h"
#include "../gtasa/ProjectileEngine.h"

#include "LuaApiExtra.h"
#include "LuaVMStub.h"

#include "lua.hpp"

#include <cmath>
#include <string>
#include <vector>

namespace WorldRPG::Script {
namespace {

struct Foliage {
    uint32_t id = 0;
    bool alive = false;
    float v[9] = {};
    int surface = 0;
    float density = 1.f;
};

struct Effect {
    uint32_t id = 0;
    bool alive = false;
    std::string name;
    float x = 0, y = 0, z = 0;
    float rx = 0, ry = 0, rz = 0;
    float draw = 0;
    bool sound = false;
    float speed = 1.f;
    float density = 1.f;
};

Foliage g_foliage[32];
Effect g_effects[32];
uint32_t g_nextFoliage = 0x31000001u;
uint32_t g_nextEffect = 0x32000001u;
bool g_diagFoliage = false;
bool g_diagEffect = false;
bool g_diagFire = false;

bool Finite(lua_State* L, int index, float& out) {
    if (!lua_isnumber(L, index)) return false;
    out = static_cast<float>(lua_tonumber(L, index));
    return std::isfinite(out);
}

Foliage* FindFoliage(uint32_t id) {
    for (Foliage& item : g_foliage)
        if (item.alive && item.id == id) return &item;
    return nullptr;
}

Effect* FindEffect(uint32_t id) {
    for (Effect& item : g_effects)
        if (item.alive && item.id == id) return &item;
    return nullptr;
}

int l_createFoliage(lua_State* L) {
    // CLuaFoliageDefs::CreateFoliage (73-120). The triangle is stored. The mesh is not built.
    Foliage created;
    for (int i = 0; i < 9; ++i) {
        if (!Finite(L, i + 1, created.v[i])) {
            lua_pushboolean(L, 0);
            return 1;
        }
    }
    float surface = 0;
    if (!Finite(L, 10, surface) || surface < 0.f || surface > 255.f) {
        lua_pushboolean(L, 0);
        return 1;
    }
    created.surface = static_cast<int>(surface);
    if (!lua_isnoneornil(L, 11)) {
        if (!Finite(L, 11, created.density)) {
            lua_pushboolean(L, 0);
            return 1;
        }
    }
    for (Foliage& slot : g_foliage) {
        if (slot.alive) continue;
        created.id = g_nextFoliage++;
        created.alive = true;
        slot = created;
        if (!g_diagFoliage) {
            g_diagFoliage = true;
            LUA_LOGE("DIAG createFoliage: CClientFoliage::Initialize is not hooked; the triangle is stored");
        }
        PushElement(L, slot.id);
        return 1;
    }
    lua_pushboolean(L, 0);
    return 1;
}

int l_getFoliageSurface(lua_State* L) {
    Foliage* item = FindFoliage(ElementIdOf(L, 1));
    if (!item) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushinteger(L, item->surface);
    return 1;
}

int l_setFoliageSurface(lua_State* L) {
    Foliage* item = FindFoliage(ElementIdOf(L, 1));
    float surface = 0;
    if (!item || !Finite(L, 2, surface) || surface < 0.f || surface > 255.f) {
        lua_pushboolean(L, 0);
        return 1;
    }
    item->surface = static_cast<int>(surface);
    lua_pushboolean(L, 1);
    return 1;
}

void PushVertex(lua_State* L, float x, float y, float z) {
    lua_newtable(L);
    lua_pushnumber(L, x); lua_setfield(L, -2, "x");
    lua_pushnumber(L, y); lua_setfield(L, -2, "y");
    lua_pushnumber(L, z); lua_setfield(L, -2, "z");
}

int l_getFoliageVertices(lua_State* L) {
    Foliage* item = FindFoliage(ElementIdOf(L, 1));
    if (!item) {
        lua_pushboolean(L, 0);
        return 1;
    }
    for (int i = 0; i < 3; ++i) PushVertex(L, item->v[i * 3], item->v[i * 3 + 1], item->v[i * 3 + 2]);
    return 3;
}

int l_setFoliageVertices(lua_State* L) {
    Foliage* item = FindFoliage(ElementIdOf(L, 1));
    if (!item) {
        lua_pushboolean(L, 0);
        return 1;
    }
    float value[9];
    for (int i = 0; i < 9; ++i) {
        if (!Finite(L, i + 2, value[i])) {
            lua_pushboolean(L, 0);
            return 1;
        }
    }
    for (int i = 0; i < 9; ++i) item->v[i] = value[i];
    lua_pushboolean(L, 1);
    return 1;
}

int l_getFoliageDensity(lua_State* L) {
    Foliage* item = FindFoliage(ElementIdOf(L, 1));
    if (!item) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushnumber(L, item->density);
    return 1;
}

int l_setFoliageDensity(lua_State* L) {
    Foliage* item = FindFoliage(ElementIdOf(L, 1));
    float density = 0;
    if (!item || !Finite(L, 2, density)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    item->density = density;
    lua_pushboolean(L, 1);
    return 1;
}

int l_createEffect(lua_State* L) {
    // CLuaEffectDefs::CreateEffect (502-545). The record is stored. FxSystem is not created.
    const char* name = lua_tostring(L, 1);
    Effect created;
    if (!name || name[0] == '\0' || !Finite(L, 2, created.x) || !Finite(L, 3, created.y) || !Finite(L, 4, created.z)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    created.name = name;
    if (!lua_isnoneornil(L, 5) && (!Finite(L, 5, created.rx) || !Finite(L, 6, created.ry) || !Finite(L, 7, created.rz))) {
        lua_pushboolean(L, 0);
        return 1;
    }
    if (!lua_isnoneornil(L, 8) && !Finite(L, 8, created.draw)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    if (lua_type(L, 9) == LUA_TBOOLEAN) created.sound = lua_toboolean(L, 9) != 0;
    for (Effect& slot : g_effects) {
        if (slot.alive) continue;
        created.id = g_nextEffect++;
        created.alive = true;
        slot = std::move(created);
        if (!g_diagEffect) {
            g_diagEffect = true;
            LUA_LOGE("DIAG createEffect: CClientEffect FxSystem is not hooked; the effect is stored");
        }
        PushElement(L, slot.id);
        return 1;
    }
    lua_pushboolean(L, 0);
    return 1;
}

int l_getEffectSpeed(lua_State* L) {
    Effect* item = FindEffect(ElementIdOf(L, 1));
    if (!item) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushnumber(L, item->speed);
    return 1;
}

int l_setEffectSpeed(lua_State* L) {
    Effect* item = FindEffect(ElementIdOf(L, 1));
    float speed = 0;
    if (!item || !Finite(L, 2, speed)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    item->speed = speed;
    lua_pushboolean(L, 1);
    return 1;
}

int l_getEffectDensity(lua_State* L) {
    Effect* item = FindEffect(ElementIdOf(L, 1));
    if (!item) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushnumber(L, item->density);
    return 1;
}

int l_setEffectDensity(lua_State* L) {
    Effect* item = FindEffect(ElementIdOf(L, 1));
    float density = 0;
    if (!item || !Finite(L, 2, density)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    item->density = density;
    lua_pushboolean(L, 1);
    return 1;
}

int l_createFire(lua_State* L) {
    // CLuaFireDefs::CreateFire numeric form (175-203) returns a boolean. The GTA fire is not started.
    float x = 0, y = 0, z = 0, size = 1.8f;
    if (!Finite(L, 1, x) || !Finite(L, 2, y) || !Finite(L, 3, z)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    if (!lua_isnoneornil(L, 4)) {
        if (!Finite(L, 4, size)) {
            lua_pushboolean(L, 0);
            return 1;
        }
    }
    (void)size;
    if (!g_diagFire) {
        g_diagFire = true;
        LUA_LOGE("DIAG createFire: CStaticFunctionDefinitions::CreateFire is not hooked");
    }
    lua_pushboolean(L, 1);
    return 1;
}

}  // namespace

bool FoliageElementAlive(uint32_t id) { return FindFoliage(id) != nullptr; }
bool EffectElementAlive(uint32_t id) { return FindEffect(id) != nullptr; }

const char* StoredElementType(uint32_t id) {
    Projectiles::Snapshot projectile;
    if (Projectiles::Copy(id, projectile)) return "projectile";
    if (LocalWeaponAlive(id)) return "weapon";
    if (FindFoliage(id)) return "foliage";
    if (FindEffect(id)) return "effect";
    return DxElementType(id);
}

bool ReleaseStoredElement(uint32_t id) {
    if (Projectiles::Release(id)) return true;
    if (ReleaseLocalWeapon(id)) return true;
    if (Foliage* item = FindFoliage(id)) {
        item->alive = false;
        return true;
    }
    if (Effect* item = FindEffect(id)) {
        item->alive = false;
        return true;
    }
    return DxElementRelease(id);
}

void InstallStoredElementLua(lua_State* L) {
    const struct { const char* name; lua_CFunction fn; } binds[] = {
        {"createFoliage", l_createFoliage},
        {"getFoliageSurface", l_getFoliageSurface},
        {"setFoliageSurface", l_setFoliageSurface},
        {"getFoliageVertices", l_getFoliageVertices},
        {"setFoliageVertices", l_setFoliageVertices},
        {"getFoliageDensity", l_getFoliageDensity},
        {"setFoliageDensity", l_setFoliageDensity},
        {"createEffect", l_createEffect},
        {"getEffectSpeed", l_getEffectSpeed},
        {"setEffectSpeed", l_setEffectSpeed},
        {"getEffectDensity", l_getEffectDensity},
        {"setEffectDensity", l_setEffectDensity},
        {"createFire", l_createFire},
    };
    for (const auto& bind : binds) {
        lua_pushcfunction(L, bind.fn);
        lua_setglobal(L, bind.name);
    }
}

}  // namespace WorldRPG::Script
