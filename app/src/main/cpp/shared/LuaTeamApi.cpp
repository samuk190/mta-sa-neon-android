// Teams. Desktop CLuaTeamDefs.cpp and CClientTeamManager::GetTeam.
// Teams arrive in ENTITY_ADD. There is no client createTeam in this category.

#include "LuaTeamApi.h"

#include "LuaApiExtra.h"
#include "LuaVMStub.h"
#include "ServerWorld.h"

#include "lua.hpp"

#include <cstring>
#include <string>
#include <vector>

namespace WorldRPG::Script {
namespace {

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
        const uint32_t id = ElementIdOf(L, index);
        ServerWorld::TeamFields team;
        if (id && ServerWorld::CopyTeam(id, team)) return "team";
        if (!element && !id) return "table";
        if (id == ServerWorld::LocalPlayer()) return "player";
        if (id) {
            const char* kind = ServerWorld::ElementKindName(id);
            if (kind && std::strcmp(kind, "element") != 0) return kind;
        }
        if (!element) return "table";
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
    if (type == LUA_TBOOLEAN) return lua_toboolean(L, index) ? "boolean (true)" : "boolean (false)";
    return "userdata";
}

int Bad(lua_State* L, const char* function, const std::string& detail) {
    Warn(std::string("Bad argument @ '") + function + "' [" + detail + "]");
    return Fail(L);
}

bool NameArg(lua_State* L, int index, std::string& out) {
    if (lua_type(L, index) != LUA_TSTRING && lua_type(L, index) != LUA_TNUMBER) return false;
    const char* text = lua_tostring(L, index);
    if (!text) return false;
    out = text;
    return true;
}

int GetTeamFromName(lua_State* L) {
    // CClientTeamManager::GetTeam compares with strcmp, so the match is case sensitive.
    std::string name;
    if (!NameArg(L, 1, name)) return Bad(L, "getTeamFromName", "Expected string at argument 1, got " + Got(L, 1));
    std::vector<ServerWorld::TeamFields> teams;
    ServerWorld::CopyTeams(teams);
    for (const ServerWorld::TeamFields& team : teams) {
        if (team.name == name) {
            PushElement(L, team.id);
            return 1;
        }
    }
    return Fail(L);
}

int RequireTeam(lua_State* L, const char* function, ServerWorld::TeamFields& out) {
    if (!lua_istable(L, 1) || !ServerWorld::CopyTeam(ElementIdOf(L, 1), out))
        return Bad(L, function, "Expected team at argument 1, got " + Got(L, 1));
    return 0;
}

int GetTeamName(lua_State* L) {
    ServerWorld::TeamFields team;
    if (RequireTeam(L, "getTeamName", team)) return 1;
    lua_pushlstring(L, team.name.data(), team.name.size());
    return 1;
}

int GetTeamColor(lua_State* L) {
    ServerWorld::TeamFields team;
    if (RequireTeam(L, "getTeamColor", team)) return 1;
    lua_pushnumber(L, team.r);
    lua_pushnumber(L, team.g);
    lua_pushnumber(L, team.b);
    return 3;
}

int GetTeamFriendlyFire(lua_State* L) {
    ServerWorld::TeamFields team;
    if (RequireTeam(L, "getTeamFriendlyFire", team)) return 1;
    lua_pushboolean(L, team.friendlyFire ? 1 : 0);
    return 1;
}

int GetPlayersInTeam(lua_State* L) {
    ServerWorld::TeamFields team;
    if (RequireTeam(L, "getPlayersInTeam", team)) return 1;
    lua_newtable(L);
    int index = 0;
    for (uint32_t id : team.players) {
        PushElement(L, id);
        lua_rawseti(L, -2, ++index);
    }
    return 1;
}

int CountPlayersInTeam(lua_State* L) {
    ServerWorld::TeamFields team;
    if (RequireTeam(L, "countPlayersInTeam", team)) return 1;
    lua_pushnumber(L, static_cast<lua_Number>(team.players.size()));
    return 1;
}

void Register(lua_State* L) {
    const struct { const char* name; lua_CFunction fn; } binds[] = {
        {"getTeamFromName", GetTeamFromName},
        {"getTeamName", GetTeamName},
        {"getTeamColor", GetTeamColor},
        {"getTeamFriendlyFire", GetTeamFriendlyFire},
        {"getPlayersInTeam", GetPlayersInTeam},
        {"countPlayersInTeam", CountPlayersInTeam},
    };
    for (const auto& bind : binds) {
        lua_pushcfunction(L, bind.fn);
        lua_setglobal(L, bind.name);
    }
}

}  // namespace

void InstallTeamLua(lua_State* L) { Register(L); }

}  // namespace WorldRPG::Script
