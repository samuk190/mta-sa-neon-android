#include "LuaVmSet.h"

#include <cstdint>
#include <unordered_map>

extern "C" {
#include "lauxlib.h"
#include "lua.h"
#include "LuaVm.h"
}

#include "LuaApiExtra.h"
#include "../net/raknet/RakNetWrapper.h"

#include <algorithm>
#include <vector>

namespace WorldRPG::Script {
bool PushResourceInstance(lua_State* L, uint16_t netId, uint32_t generation);
}

namespace WorldRPG::Script::Vms {
namespace {

// Refs of the 5.1 VM carry this bit; luaL_ref never returns it (registry
// array parts stay far below 2^30 entries).
constexpr int kRef51 = 1 << 30;
constexpr int kMaxCopyDepth = 64;  // desktop CLuaArguments stops nesting too

lua_State* g_primary = nullptr;
lua_State* g_lua51 = nullptr;

bool IsRealRef(int ref) { return ref > 0; }

struct CopyContext {
    lua_State* from;
    lua_State* to;
    int scratch = 0;  // index on `to` of a table holding every copy made so far
    int count = 0;
    std::unordered_map<const void*, int> copied;  // source table -> its key in scratch
    int depth = 0;
};

// Raw t[name] (no metamethods: copying never runs code in the idle VM).
int RawField(lua_State* L, int idx, const char* name) {
    lua_pushstring(L, name);
    return lua_rawget(L, idx);
}

// The element or resource a table stands for, when it is the object
// PushElement / PushResource made for it (not just a table with those fields).
bool PushSameObject(CopyContext& c, int idx) {
    lua_State* from = c.from;
    RawField(from, idx, "element");
    const bool element = lua_isboolean(from, -1) && lua_toboolean(from, -1);
    lua_pop(from, 1);
    if (element) {
        RawField(from, idx, "id");
        const bool hasId = lua_isinteger(from, -1);
        const auto id = static_cast<uint32_t>(lua_tointeger(from, -1));
        lua_pop(from, 1);
        if (!hasId || id == 0 || !lua_checkstack(from, 4)) return false;
        PushElement(from, id);  // the source VM's object for that id
        const bool same = lua_rawequal(from, -1, idx) != 0;
        lua_pop(from, 1);
        if (!same) return false;
        PushElement(c.to, id);
        return true;
    }
    RawField(from, idx, "resource");
    const bool resource = lua_isboolean(from, -1) && lua_toboolean(from, -1);
    lua_pop(from, 1);
    if (!resource) return false;
    RawField(from, idx, "netId");
    RawField(from, idx, "gen");
    const bool shaped = lua_isinteger(from, -2) && lua_isinteger(from, -1);
    const auto netId = static_cast<uint16_t>(lua_tointeger(from, -2));
    const auto gen = static_cast<uint32_t>(lua_tointeger(from, -1));
    lua_pop(from, 2);
    if (!shaped || !lua_checkstack(from, 4) || !PushResourceInstance(from, netId, gen)) return false;
    const bool same = lua_rawequal(from, -1, idx) != 0;
    lua_pop(from, 1);
    if (!same) return false;
    if (!PushResourceInstance(c.to, netId, gen)) lua_pushnil(c.to);
    return true;
}

void CopyOne(CopyContext& c, int idx) {
    lua_State* from = c.from;
    lua_State* to = c.to;
    // Never raise here: one of the two VMs is idle. Out of stack, the value
    // arrives as nil (pushed into the slack every Lua stack keeps).
    if (!lua_checkstack(to, 4)) {
        lua_pushnil(to);
        return;
    }
    switch (lua_type(from, idx)) {
        case LUA_TBOOLEAN: lua_pushboolean(to, lua_toboolean(from, idx)); return;
        case LUA_TNUMBER:
            if (lua_isinteger(from, idx)) lua_pushinteger(to, lua_tointeger(from, idx));
            else lua_pushnumber(to, lua_tonumber(from, idx));
            return;
        case LUA_TSTRING: {
            size_t len = 0;
            const char* s = lua_tolstring(from, idx, &len);
            lua_pushlstring(to, s, len);
            return;
        }
        case LUA_TLIGHTUSERDATA: lua_pushlightuserdata(to, lua_touserdata(from, idx)); return;
        case LUA_TTABLE: break;
        default: lua_pushnil(to); return;  // nil, functions, threads, full userdata
    }
    idx = lua_absindex(from, idx);
    if (PushSameObject(c, idx)) return;
    const void* key = lua_topointer(from, idx);
    if (auto seen = c.copied.find(key); seen != c.copied.end()) {
        lua_rawgeti(to, c.scratch, seen->second);  // shared or cyclic: the same copy
        return;
    }
    if (c.depth >= kMaxCopyDepth) {
        lua_pushnil(to);
        return;
    }
    lua_newtable(to);
    const int copy = lua_gettop(to);
    lua_pushvalue(to, copy);
    lua_rawseti(to, c.scratch, ++c.count);
    c.copied[key] = c.count;
    ++c.depth;
    if (!lua_checkstack(from, 3)) return;  // the empty copy is on `to`
    lua_pushnil(from);
    while (lua_next(from, idx)) {
        CopyOne(c, -2);  // key
        CopyOne(c, -1);  // value
        if (lua_isnil(to, -2)) lua_pop(to, 2);  // a key that does not cross
        else lua_rawset(to, copy);
        lua_pop(from, 1);
    }
    --c.depth;
}

}  // namespace

void SetPrimary(lua_State* L) { g_primary = L; }
lua_State* Primary() { return g_primary; }
void SetLua51(lua_State* L) { g_lua51 = L; }
lua_State* Lua51() { return g_lua51; }

bool Is51(lua_State* L) { return neon_lua_is51(L) != 0; }
lua_State* MainOf(lua_State* L) { return Is51(L) ? g_lua51 : g_primary; }

int Ref(lua_State* L) {
    const int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    if (!IsRealRef(ref)) return ref;
    return Is51(L) ? ref | kRef51 : ref;
}

lua_State* RefVm(int ref) {
    if (!IsRealRef(ref)) return nullptr;
    return (ref & kRef51) ? g_lua51 : g_primary;
}

void Unref(int ref) {
    lua_State* vm = RefVm(ref);
    if (!vm) return;
    luaL_unref(vm, LUA_REGISTRYINDEX, ref & ~kRef51);
}

int PushRef(lua_State* L, int ref) {
    if (ref == LUA_REFNIL || !IsRealRef(ref) || RefVm(ref) != MainOf(L)) {
        lua_pushnil(L);
        return LUA_TNIL;
    }
    return lua_rawgeti(L, LUA_REGISTRYINDEX, ref & ~kRef51);
}

void PushRefCopy(lua_State* L, int ref) {
    lua_State* vm = RefVm(ref);
    if (!vm || vm == MainOf(L)) {
        PushRef(L, ref);
        return;
    }
    PushRef(vm, ref);
    Copy(vm, -1, L);
    lua_pop(vm, 1);
}

void Copy(lua_State* from, int idx, lua_State* to) {
    if (MainOf(from) == MainOf(to)) {
        lua_pushvalue(from, idx);
        if (from != to) lua_xmove(from, to, 1);
        return;
    }
    idx = lua_absindex(from, idx);
    if (!lua_checkstack(to, 6)) {  // never raise: `to` may be the idle VM
        lua_pushnil(to);
        return;
    }
    lua_newtable(to);
    CopyContext c{from, to, lua_gettop(to)};
    CopyOne(c, idx);
    lua_remove(to, c.scratch);
}

int LoadScriptChunk(lua_State* L, const char* buf, size_t len, const char* name) {
    if (!Is51(L)) return luaL_loadbufferx(L, buf, len, name, "t");
    // Compiled chunks (0x1B, or 0x1C encrypted) pass through netc first, as
    // on desktop; Lua 5.1 does not verify bytecode, so none loads without it.
    const unsigned char first = len > 0 ? static_cast<unsigned char>(buf[0]) : 0;
    std::vector<uint8_t> plain;
    const char* mode = "t";
    if (first == 0x1B || first == 0x1C) {
        if (!WorldRPG::Net::DeobfuscateScriptForSession(reinterpret_cast<const uint8_t*>(buf), len, name ? name : "", plain)) {
            lua_pushstring(L, "compiled (luac.mtasa.com) chunks load only with PC Network compatibility");
            return LUA_ERRSYNTAX;
        }
        buf = reinterpret_cast<const char*>(plain.data());
        len = plain.size();
        mode = "bt";
    }
    const int status = luaL_loadbufferx(L, buf, len, name, mode);
    std::fill(plain.begin(), plain.end(), 0);
    if (status != LUA_OK) return status;
    // The chunk gets the calling function's environment (desktop: one VM per resource).
    lua_Debug ar{};
    if (lua_getstack(L, 1, &ar) && lua_getinfo(L, "f", &ar)) {
        neon_lua51_getfenv(L, -1);
        lua_remove(L, -2);
        if (lua_istable(L, -1)) neon_lua51_setfenv(L, -2);
        else lua_pop(L, 1);
    }
    return LUA_OK;
}

int ProxyToPrimary(lua_State* L, int (*fn)(lua_State*)) {
    lua_State* P = g_primary;
    if (!Is51(L) || !P) return fn(L);
    const int nargs = lua_gettop(L);
    const int base = lua_gettop(P);
    if (!lua_checkstack(P, nargs + 2)) return luaL_error(L, "stack overflow (calling into the Lua 5.3 VM)");
    lua_pushcfunction(P, fn);
    CopyRange(L, 1, nargs, P);
    if (lua_pcall(P, nargs, LUA_MULTRET, 0) != LUA_OK) {
        lua_settop(L, 0);
        Copy(P, -1, L);
        lua_settop(P, base);
        return lua_error(L);
    }
    const int nresults = lua_gettop(P) - base;
    if (!lua_checkstack(L, nresults + 1)) {
        lua_settop(P, base);
        return luaL_error(L, "stack overflow (returning from the Lua 5.3 VM)");
    }
    CopyRange(P, base + 1, nresults, L);
    lua_settop(P, base);
    return nresults;
}

void CopyRange(lua_State* from, int first, int count, lua_State* to) {
    first = lua_absindex(from, first);
    for (int i = 0; i < count; ++i) Copy(from, first + i, to);
}

}  // namespace WorldRPG::Script::Vms
