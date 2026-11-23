#include "LuaProjectileApi.h"
#include "LuaApiExtra.h"
#include "LuaVMStub.h"
#include "ServerWorld.h"
#include "../gtasa/GameEngine.h"
#include "../gtasa/ProjectileEngine.h"
#include "lua.hpp"
#include <cmath>
#include <cstring>
namespace WorldRPG::Script {
namespace {
int Fail(lua_State* L) { lua_pushboolean(L, 0); return 1; }
bool Number(lua_State* L, int index, float& result, bool optional = false) {
    if (optional && lua_isnoneornil(L, index)) return true;
    if (!lua_isnumber(L, index)) return false;
    const double value = lua_tonumber(L, index);
    result = static_cast<float>(value);
    return std::isfinite(value) && std::isfinite(result);
}
bool Read(lua_State* L, Projectiles::Snapshot& out) {
    return lua_istable(L, 1) && Projectiles::Copy(ElementIdOf(L, 1), out);
}
// CLuaProjectileDefs.cpp:51-114 / CStaticFunctionDefinitions.cpp:7536-7592.
int CreateProjectile(lua_State* L) {
    Projectiles::Request r;
    if (!lua_istable(L, 1) || ActiveVmOwner().empty()) return Fail(L);
    r.creator = ElementIdOf(L, 1);
    const char* type = ServerWorld::ElementKindName(r.creator);
    if (std::strcmp(type, "player") && std::strcmp(type, "ped") && std::strcmp(type, "vehicle") &&
        !Game::ScriptPedAlive(r.creator)) return Fail(L);
    float weapon = 0;
    if (!Number(L, 2, weapon) || weapon < 0 || weapon > 255) return Fail(L);
    r.weapon = static_cast<int>(weapon);
    if (r.weapon != 16 && r.weapon != 17 && r.weapon != 18 && r.weapon != 19 && r.weapon != 20 &&
        r.weapon != 21 && r.weapon != 39 && r.weapon != 58) return Fail(L);
    r.originMask = 0;
    float* origin[] = {&r.origin.x, &r.origin.y, &r.origin.z};
    for (int i = 0; i < 3; ++i) {
        if (lua_isnoneornil(L, i + 3)) continue;
        if (!Number(L, i + 3, *origin[i])) return Fail(L);
        r.originMask |= 1u << i;
    }
    if (!Number(L, 6, r.force, true)) return Fail(L);
    if (!lua_isnoneornil(L, 7)) {
        if (!lua_istable(L, 7)) return Fail(L);
        r.target = ElementIdOf(L, 7);
        if (!r.target || r.target == 0xFFFFFFFFu) return Fail(L);
    }
    // The native force controls the initial trajectory. Rotation/model overrides
    // need additional mobile hooks: reject explicitly rather than silently drop.
    for (int i = 8; i <= 10; ++i) {
        float rotation = 0;
        if (!Number(L, i, rotation, true) || rotation != 0) {
            LUA_LOGE("DIAG createProjectile: initial rotation override is not ported");
            return Fail(L);
        }
    }
    if (!Number(L, 11, r.velocity.x, true) || !Number(L, 12, r.velocity.y, true) || !Number(L, 13, r.velocity.z, true))
        return Fail(L);
    float model = 0;
    if (!Number(L, 14, model, true) || model != 0) {
        LUA_LOGE("DIAG createProjectile: model override is not ported"); return Fail(L);
    }
    r.owner = ActiveVmOwner(); r.netId = ActiveVmOwnerNet(); r.generation = ActiveVmOwnerGeneration();
    const uint32_t id = Projectiles::Create(r);
    if (!id) return Fail(L);
    PushElement(L, id);
    return 1;
}
// CLuaProjectileDefs.cpp:116-265; counter is backed by the native timer, not a Lua countdown.
int GetProjectileType(lua_State* L) {
    Projectiles::Snapshot p; if (!Read(L, p)) return Fail(L);
    lua_pushnumber(L, p.weapon); return 1;
}
int GetProjectileCreator(lua_State* L) {
    Projectiles::Snapshot p; if (!Read(L, p)) return Fail(L);
    if (p.creator) PushElement(L, p.creator); else lua_pushnil(L); return 1;
}
int GetProjectileTarget(lua_State* L) {
    Projectiles::Snapshot p; if (!Read(L, p)) return Fail(L);
    if (p.weapon != 20) {
        if (p.weapon == 39) LUA_LOGE("DIAG getProjectileTarget: satchel attachment hook is not ported");
        return Fail(L);
    }
    if (p.target) PushElement(L, p.target); else lua_pushnil(L); return 1;
}
int GetProjectileForce(lua_State* L) {
    Projectiles::Snapshot p; if (!Read(L, p)) return Fail(L);
    lua_pushnumber(L, p.force); return 1;
}
int GetProjectileCounter(lua_State* L) {
    Projectiles::Snapshot p; if (!Read(L, p)) return Fail(L);
    lua_pushnumber(L, p.counter); return 1;
}
int SetProjectileCounter(lua_State* L) {
    Projectiles::Snapshot p;
    if (!Read(L, p) || !lua_isnumber(L, 2)) return Fail(L);
    const double value = lua_tonumber(L, 2);
    if (!std::isfinite(value) || value < 0 || value > 4294967295.0) return Fail(L);
    lua_pushboolean(L, Projectiles::SetCounter(p.id, static_cast<uint32_t>(value))); return 1;
}
}
void InstallProjectileLua(lua_State* L) {
    static const struct { const char* name; lua_CFunction fn; } binds[] = {
        {"createProjectile", CreateProjectile}, {"getProjectileType", GetProjectileType},
        {"getProjectileCreator", GetProjectileCreator}, {"getProjectileTarget", GetProjectileTarget},
        {"getProjectileForce", GetProjectileForce}, {"getProjectileCounter", GetProjectileCounter},
        {"setProjectileCounter", SetProjectileCounter},
    };
    for (const auto& b : binds) { lua_pushcfunction(L, b.fn); lua_setglobal(L, b.name); }
}
}
