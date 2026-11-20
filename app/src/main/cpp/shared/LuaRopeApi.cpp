// Managed ropes. Desktop CLuaRopeDefs.cpp and CClientRopeManager.
// The native rope slot is not leased, so isRopeActive and getRopePositionAt stay false.
// Remaining time does not tick because DoPulse does not run.

#include "LuaRopeApi.h"

#include "LuaApiExtra.h"
#include "LuaVMStub.h"
#include "LuaWorldObjectApi.h"
#include "ServerWorld.h"

#include "lua.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>

namespace WorldRPG::Script {
namespace {

struct Vec {
    float x = 0, y = 0, z = 0;
};

struct Rope {
    uint32_t id = 0;
    float x = 0, y = 0, z = 0;
    int type = 8;
    double duration = 0;
    double remaining = 0;
    double winch = 0.5;
    double length = 0;
    double fixedNode = 0;
    bool ground = false;
    bool physics = true;
    Vec offset;
    Vec velocity;
    uint32_t holder = 0;
    uint32_t carried = 0;
};

std::unordered_map<uint32_t, Rope> g_ropes;
bool g_diag = false;
bool g_diagNative = false;

int Fail(lua_State* L) {
    lua_pushboolean(L, 0);
    return 1;
}

void NoteNative() {
    if (g_diagNative) return;
    g_diagNative = true;
    LUA_LOGE("DIAG rope: CRopes is not leased, so isRopeActive and getRopePositionAt stay false");
}

const char* TypeName(int type) {
    switch (type) {
        case 1: return "winchMagnet";
        case 2: return "harness";
        case 3: return "miniMagnet";
        case 4: return "dockCrane";
        case 5: return "wreckingBall";
        case 6: return "quarryCrane";
        case 7: return "vegasCrane";
        case 8: return "swat";
        default: return nullptr;
    }
}

int TypeFromName(const char* name) {
    if (!name) return 0;
    if (std::strcmp(name, "winchMagnet") == 0) return 1;
    if (std::strcmp(name, "harness") == 0) return 2;
    if (std::strcmp(name, "miniMagnet") == 0) return 3;
    if (std::strcmp(name, "dockCrane") == 0) return 4;
    if (std::strcmp(name, "wreckingBall") == 0) return 5;
    if (std::strcmp(name, "quarryCrane") == 0) return 6;
    if (std::strcmp(name, "vegasCrane") == 0) return 7;
    if (std::strcmp(name, "swat") == 0) return 8;
    return 0;
}

bool Finite(lua_State* L, int index, float& out) {
    if (!lua_isnumber(L, index)) return false;
    const lua_Number number = lua_tonumber(L, index);
    if (!std::isfinite(number)) return false;
    out = static_cast<float>(number);
    return true;
}

bool ReadVectorField(lua_State* L, int table, const char* field, Vec& out) {
    lua_getfield(L, table, field);
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        return false;
    }
    auto component = [&](const char* name, int index, float& value) {
        lua_getfield(L, -1, name);
        if (!lua_isnumber(L, -1)) {
            lua_pop(L, 1);
            lua_rawgeti(L, -1, index);
        }
        if (!lua_isnumber(L, -1) || !std::isfinite(lua_tonumber(L, -1))) {
            lua_pop(L, 1);
            return false;
        }
        value = static_cast<float>(lua_tonumber(L, -1));
        lua_pop(L, 1);
        return true;
    };
    const bool ok = component("x", 1, out.x) && component("y", 2, out.y) && component("z", 3, out.z);
    lua_pop(L, 1);
    return ok;
}

bool ReadNumberField(lua_State* L, int table, const char* field, double& out) {
    lua_getfield(L, table, field);
    const bool ok = lua_isnumber(L, -1) && std::isfinite(lua_tonumber(L, -1));
    if (ok) out = lua_tonumber(L, -1);
    lua_pop(L, 1);
    return ok;
}

bool ReadBoolField(lua_State* L, int table, const char* field, bool& out) {
    lua_getfield(L, table, field);
    const bool ok = lua_type(L, -1) == LUA_TBOOLEAN;
    if (ok) out = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    return ok;
}

bool Physical(uint32_t id) {
    if (!id || !KnownClientElement(id)) return false;
    float x = 0, y = 0, z = 0;
    if (CopyObjectPose(id, x, y, z)) return true;
    const char* kind = ServerWorld::ElementKindName(id);
    return kind && (std::strcmp(kind, "object") == 0 || std::strcmp(kind, "vehicle") == 0);
}

uint32_t ElementField(lua_State* L, int table, const char* field) {
    lua_getfield(L, table, field);
    uint32_t id = 0;
    if (lua_istable(L, -1)) id = ElementIdOf(L, -1);
    lua_pop(L, 1);
    return id;
}

Rope* Find(uint32_t id) {
    auto found = g_ropes.find(id);
    if (found == g_ropes.end()) return nullptr;
    if (!KnownClientElement(id)) {
        g_ropes.erase(found);
        return nullptr;
    }
    return &g_ropes.find(id)->second;
}

void PushLinked(lua_State* L, uint32_t id) {
    if (id && KnownClientElement(id)) PushElement(L, id);
    else lua_pushboolean(L, 0);
}

void PushVector(lua_State* L, const Vec& value) {
    lua_getglobal(L, "Vector3");
    lua_pushnumber(L, value.x);
    lua_pushnumber(L, value.y);
    lua_pushnumber(L, value.z);
    if (!lua_isfunction(L, -4) || lua_pcall(L, 3, 1, 0) != 0) {
        lua_pop(L, 1);
        lua_pushboolean(L, 0);
    }
}

int CreateRope(lua_State* L) {
    // CLuaRopeDefs::CreateRope (237-319). Default type is swat. A bad position or type returns false.
    float x = 0, y = 0, z = 0;
    if (!Finite(L, 1, x) || !Finite(L, 2, y) || !Finite(L, 3, z)) return Fail(L);
    int type = 8;
    double duration = 0, winch = 0.5, length = 0, fixedNode = 0;
    bool ground = false, physics = true;
    Vec offset, velocity;
    uint32_t holder = 0, carried = 0;
    if (lua_istable(L, 4)) {
        lua_getfield(L, 4, "type");
        if (lua_isstring(L, -1)) type = TypeFromName(lua_tostring(L, -1));
        lua_pop(L, 1);
        if (type < 1 || type > 8) return Fail(L);
        ReadNumberField(L, 4, "duration", duration);
        ReadNumberField(L, 4, "winchHeight", winch);
        ReadNumberField(L, 4, "length", length);
        ReadNumberField(L, 4, "fixedNode", fixedNode);
        ReadBoolField(L, 4, "sitOnGround", ground);
        ReadBoolField(L, 4, "physics", physics);
        ReadVectorField(L, 4, "holderOffset", offset);
        ReadVectorField(L, 4, "topVelocity", velocity);
        holder = ElementField(L, 4, "holder");
        carried = ElementField(L, 4, "carriedElement");
    }
    if (duration < 0 || winch < 0.01 || fixedNode < 0 || fixedNode > 30 || (holder && !Physical(holder)) || (carried && !Physical(carried)))
        return Fail(L);
    const uint32_t id = CreateResourceElement("rope");
    if (!id) return Fail(L);
    Rope rope;
    rope.id = id;
    rope.x = x;
    rope.y = y;
    rope.z = z;
    rope.type = type;
    rope.duration = duration;
    rope.remaining = duration;
    rope.winch = winch;
    rope.length = std::max(0.0, length);
    rope.fixedNode = fixedNode;
    rope.ground = ground;
    rope.physics = physics;
    rope.offset = offset;
    rope.velocity = velocity;
    rope.holder = holder;
    rope.carried = carried;
    g_ropes[id] = rope;
    if (!g_diag) {
        g_diag = true;
        LUA_LOGE("DIAG createRope: CRopes is not created and remaining time does not tick");
    }
    PushElement(L, id);
    return 1;
}

int GetRopeType(lua_State* L) {
    Rope* rope = Find(ElementIdOf(L, 1));
    const char* name = rope ? TypeName(rope->type) : nullptr;
    if (!name) return Fail(L);
    lua_pushstring(L, name);
    return 1;
}

int SetRopeType(lua_State* L) {
    Rope* rope = Find(ElementIdOf(L, 1));
    const int type = TypeFromName(lua_isstring(L, 2) ? lua_tostring(L, 2) : nullptr);
    if (!rope || type <= 0) return Fail(L);
    rope->type = type;
    lua_pushboolean(L, 1);
    return 1;
}

int GetNumberValue(lua_State* L, double Rope::*field) {
    Rope* rope = Find(ElementIdOf(L, 1));
    if (!rope) return Fail(L);
    lua_pushnumber(L, rope->*field);
    return 1;
}

int GetRopeDuration(lua_State* L) { return GetNumberValue(L, &Rope::duration); }
int GetRopeRemainingTime(lua_State* L) { return GetNumberValue(L, &Rope::remaining); }
int GetRopeWinchHeight(lua_State* L) { return GetNumberValue(L, &Rope::winch); }
int GetRopeLength(lua_State* L) { return GetNumberValue(L, &Rope::length); }
int GetRopeFixedNode(lua_State* L) { return GetNumberValue(L, &Rope::fixedNode); }

int SetRopeDuration(lua_State* L) {
    Rope* rope = Find(ElementIdOf(L, 1));
    const double value = luaL_optnumber(L, 2, -1);
    if (!rope || value < 0) return Fail(L);
    rope->duration = value;
    rope->remaining = value;
    lua_pushboolean(L, 1);
    return 1;
}

int SetRopeRemainingTime(lua_State* L) {
    Rope* rope = Find(ElementIdOf(L, 1));
    const double value = luaL_optnumber(L, 2, -1);
    if (!rope || value < 0) return Fail(L);
    rope->remaining = value;
    lua_pushboolean(L, 1);
    return 1;
}

int SetRopeWinchHeight(lua_State* L) {
    Rope* rope = Find(ElementIdOf(L, 1));
    const double value = luaL_optnumber(L, 2, 0);
    if (!rope || value < 0.01) return Fail(L);
    rope->winch = value;
    lua_pushboolean(L, 1);
    return 1;
}

int SetRopeLength(lua_State* L) {
    Rope* rope = Find(ElementIdOf(L, 1));
    const double value = luaL_optnumber(L, 2, 0);
    if (!rope || value < 0) return Fail(L);
    rope->length = value;
    lua_pushboolean(L, 1);
    return 1;
}

int SetRopeFixedNode(lua_State* L) {
    Rope* rope = Find(ElementIdOf(L, 1));
    const int value = static_cast<int>(luaL_optinteger(L, 2, -1));
    if (!rope || value < 0 || value > 30) return Fail(L);
    rope->fixedNode = value;
    lua_pushboolean(L, 1);
    return 1;
}

int GetRopeHolder(lua_State* L) {
    Rope* rope = Find(ElementIdOf(L, 1));
    if (!rope) return Fail(L);
    PushLinked(L, rope->holder);
    return 1;
}

int SetRopeHolder(lua_State* L) {
    Rope* rope = Find(ElementIdOf(L, 1));
    const bool clear = lua_isnoneornil(L, 2);
    const uint32_t holder = clear ? 0 : ElementIdOf(L, 2);
    if (!rope || (!clear && !Physical(holder))) return Fail(L);
    rope->holder = holder;
    lua_pushboolean(L, 1);
    return 1;
}

int GetRopeHolderOffset(lua_State* L) {
    Rope* rope = Find(ElementIdOf(L, 1));
    if (!rope) return Fail(L);
    PushVector(L, rope->offset);
    return 1;
}

int SetRopeHolderOffset(lua_State* L) {
    Rope* rope = Find(ElementIdOf(L, 1));
    float x = 0, y = 0, z = 0;
    if (!rope || !Finite(L, 2, x) || !Finite(L, 3, y) || !Finite(L, 4, z)) return Fail(L);
    rope->offset = {x, y, z};
    lua_pushboolean(L, 1);
    return 1;
}

int GetRopeTopVelocity(lua_State* L) {
    Rope* rope = Find(ElementIdOf(L, 1));
    if (!rope) return Fail(L);
    PushVector(L, rope->velocity);
    return 1;
}

int SetRopeTopVelocity(lua_State* L) {
    Rope* rope = Find(ElementIdOf(L, 1));
    float x = 0, y = 0, z = 0;
    if (!rope || !Finite(L, 2, x) || !Finite(L, 3, y) || !Finite(L, 4, z)) return Fail(L);
    rope->velocity = {x, y, z};
    lua_pushboolean(L, 1);
    return 1;
}

int IsRopeOnGround(lua_State* L) {
    Rope* rope = Find(ElementIdOf(L, 1));
    if (!rope) return Fail(L);
    lua_pushboolean(L, rope->ground ? 1 : 0);
    return 1;
}

int SetRopeOnGround(lua_State* L) {
    Rope* rope = Find(ElementIdOf(L, 1));
    if (!rope || lua_type(L, 2) != LUA_TBOOLEAN) return Fail(L);
    rope->ground = lua_toboolean(L, 2) != 0;
    lua_pushboolean(L, 1);
    return 1;
}

int IsRopePhysicsEnabled(lua_State* L) {
    Rope* rope = Find(ElementIdOf(L, 1));
    if (!rope) return Fail(L);
    lua_pushboolean(L, rope->physics ? 1 : 0);
    return 1;
}

int SetRopePhysicsEnabled(lua_State* L) {
    Rope* rope = Find(ElementIdOf(L, 1));
    if (!rope || lua_type(L, 2) != LUA_TBOOLEAN) return Fail(L);
    rope->physics = lua_toboolean(L, 2) != 0;
    lua_pushboolean(L, 1);
    return 1;
}

int GetRopeCarriedElement(lua_State* L) {
    Rope* rope = Find(ElementIdOf(L, 1));
    if (!rope) return Fail(L);
    PushLinked(L, rope->carried);
    return 1;
}

int AttachElementToRope(lua_State* L) {
    Rope* rope = Find(ElementIdOf(L, 1));
    const uint32_t carried = ElementIdOf(L, 2);
    if (!rope || !Physical(carried)) return Fail(L);
    rope->carried = carried;
    lua_pushboolean(L, 1);
    return 1;
}

int DetachElementFromRope(lua_State* L) {
    Rope* rope = Find(ElementIdOf(L, 1));
    if (!rope) return Fail(L);
    rope->carried = 0;
    lua_pushboolean(L, 1);
    return 1;
}

int GetRopePositionAt(lua_State* L) {
    Rope* rope = Find(ElementIdOf(L, 1));
    const float progress = static_cast<float>(luaL_optnumber(L, 2, -1));
    if (!rope || progress < 0.f || progress > 1.f) return Fail(L);
    NoteNative();
    return Fail(L);
}

int IsRopeActive(lua_State* L) {
    Rope* rope = Find(ElementIdOf(L, 1));
    if (rope) NoteNative();
    lua_pushboolean(L, 0);
    return 1;
}

int GetElementPositionRope(lua_State* L) {
    Rope* rope = Find(ElementIdOf(L, 1));
    if (!rope) {
        if (!lua_isfunction(L, lua_upvalueindex(1))) return Fail(L);
        const int count = lua_gettop(L);
        lua_pushvalue(L, lua_upvalueindex(1));
        lua_insert(L, 1);
        lua_call(L, count, LUA_MULTRET);
        return lua_gettop(L);
    }
    lua_pushnumber(L, rope->x);
    lua_pushnumber(L, rope->y);
    lua_pushnumber(L, rope->z);
    return 3;
}

int SetElementPositionRope(lua_State* L) {
    Rope* rope = Find(ElementIdOf(L, 1));
    if (!rope) {
        if (!lua_isfunction(L, lua_upvalueindex(1))) return Fail(L);
        const int count = lua_gettop(L);
        lua_pushvalue(L, lua_upvalueindex(1));
        lua_insert(L, 1);
        lua_call(L, count, LUA_MULTRET);
        return lua_gettop(L);
    }
    float x = 0, y = 0, z = 0;
    if (!Finite(L, 2, x) || !Finite(L, 3, y) || !Finite(L, 4, z)) return Fail(L);
    rope->x = x;
    rope->y = y;
    rope->z = z;
    lua_pushboolean(L, 1);
    return 1;
}

void Register(lua_State* L) {
    g_ropes.clear();
    const struct { const char* name; lua_CFunction fn; } binds[] = {
        {"createRope", CreateRope},
        {"getRopeType", GetRopeType},
        {"setRopeType", SetRopeType},
        {"getRopeDuration", GetRopeDuration},
        {"setRopeDuration", SetRopeDuration},
        {"getRopeRemainingTime", GetRopeRemainingTime},
        {"setRopeRemainingTime", SetRopeRemainingTime},
        {"getRopeHolder", GetRopeHolder},
        {"setRopeHolder", SetRopeHolder},
        {"getRopeHolderOffset", GetRopeHolderOffset},
        {"setRopeHolderOffset", SetRopeHolderOffset},
        {"getRopeTopVelocity", GetRopeTopVelocity},
        {"setRopeTopVelocity", SetRopeTopVelocity},
        {"getRopeWinchHeight", GetRopeWinchHeight},
        {"setRopeWinchHeight", SetRopeWinchHeight},
        {"getRopeLength", GetRopeLength},
        {"setRopeLength", SetRopeLength},
        {"getRopeFixedNode", GetRopeFixedNode},
        {"setRopeFixedNode", SetRopeFixedNode},
        {"isRopeOnGround", IsRopeOnGround},
        {"setRopeOnGround", SetRopeOnGround},
        {"isRopePhysicsEnabled", IsRopePhysicsEnabled},
        {"setRopePhysicsEnabled", SetRopePhysicsEnabled},
        {"getRopeCarriedElement", GetRopeCarriedElement},
        {"attachElementToRope", AttachElementToRope},
        {"detachElementFromRope", DetachElementFromRope},
        {"getRopePositionAt", GetRopePositionAt},
        {"isRopeActive", IsRopeActive},
    };
    for (const auto& bind : binds) {
        lua_pushcfunction(L, bind.fn);
        lua_setglobal(L, bind.name);
    }
    lua_getglobal(L, "getElementPosition");
    lua_pushcclosure(L, GetElementPositionRope, 1);
    lua_setglobal(L, "getElementPosition");
    lua_getglobal(L, "setElementPosition");
    lua_pushcclosure(L, SetElementPositionRope, 1);
    lua_setglobal(L, "setElementPosition");
}

}  // namespace

void RopeRelease(uint32_t id) { g_ropes.erase(id); }
void InstallRopeLua(lua_State* L) { Register(L); }

}  // namespace WorldRPG::Script
