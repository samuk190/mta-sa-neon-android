/* The Lua 5.3 C API, as the script API uses it, on a 5.3 state and on a 5.1
 * state through luaapi/ (LuaVmDispatch.c, LuaVm51.c, LuaAuxLib.c). */
#include <stdio.h>
#include <string.h>

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include "LuaVm.h"

static int failures = 0;
static const char *vm = "";
#define CHECK(cond, what) do { if (!(cond)) { printf("FAIL [%s]: %s\n", vm, what); ++failures; } } while (0)

static int CurrentLine(lua_State *L) {
    lua_Debug ar;
    memset(&ar, 0, sizeof(ar));
    const int ok = lua_getstack(L, 1, &ar) && lua_getinfo(L, "Sl", &ar);
    lua_pushinteger(L, ok ? ar.currentline : -1);
    return 1;
}

static int Upvalue(lua_State *L) {
    lua_pushvalue(L, lua_upvalueindex(1));
    return 1;
}

static int Fails(lua_State *L) { return luaL_error(L, "bad %s %d", "thing", 7); }

static int IsVm51(lua_State *L) {
    lua_pushboolean(L, neon_lua_is51(L));
    return 1;
}

static int hookCalls = 0;
static void CountHook(lua_State *L, lua_Debug *ar) { (void)L; (void)ar; ++hookCalls; }

static int Nothing(lua_State *L) { (void)L; return 0; }

static int otherHookCalls = 0;
static void OtherHook(lua_State *L, lua_Debug *ar) { (void)L; (void)ar; ++otherHookCalls; }

static int Run(lua_State *L, const char *code) {
    if (luaL_loadbufferx(L, code, strlen(code), "=test", "t") != LUA_OK || lua_pcall(L, 0, 1, 0) != LUA_OK) {
        printf("FAIL [%s]: %s -> %s\n", vm, code, lua_tostring(L, -1));
        ++failures;
        lua_settop(L, 0);
        return 0;
    }
    return 1;
}

static void Exercise(lua_State *L, int is51) {
    CHECK(neon_lua_is51(L) == is51, "VM tag");

    /* globals, getters returning the type */
    lua_pushglobaltable(L);
    CHECK(lua_istable(L, -1), "pushglobaltable");
    lua_pushinteger(L, 42);
    lua_setfield(L, -2, "answer");
    CHECK(lua_getfield(L, -1, "answer") == LUA_TNUMBER, "getfield type");
    CHECK(lua_isinteger(L, -1) && lua_tointeger(L, -1) == 42, "integer");
    lua_settop(L, 0);
    CHECK(lua_getglobal(L, "answer") == LUA_TNUMBER, "getglobal type");
    lua_settop(L, 0);

    /* registry refs start after the fixed slots */
    lua_pushstring(L, "ref me");
    const int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    CHECK(ref > LUA_RIDX_GLOBALS, "ref after fixed slots");
    CHECK(lua_rawgeti(L, LUA_REGISTRYINDEX, ref) == LUA_TSTRING, "ref get");
    luaL_unref(L, LUA_REGISTRYINDEX, ref);
    lua_settop(L, 0);

    /* pushfstring with 5.3-only %I */
    lua_pushfstring(L, "%s=%d/%I/%%", "n", 5, (lua_Integer)9007199254740993LL);
    CHECK(strcmp(lua_tostring(L, -1), "n=5/9007199254740993/%") == 0, "pushfstring");
    lua_settop(L, 0);

    /* C functions: upvalues, line info, errors with location */
    lua_pushstring(L, "up");
    lua_pushcclosure(L, Upvalue, 1);
    lua_setglobal(L, "upv");
    lua_register(L, "line", CurrentLine);
    lua_register(L, "fails", Fails);
    lua_register(L, "is51", IsVm51);
    CHECK(Run(L, "return upv()") && strcmp(lua_tostring(L, -1), "up") == 0, "upvalueindex");
    lua_settop(L, 0);
    CHECK(Run(L, "\n\nreturn line()") && lua_tointeger(L, -1) == 3, "getstack/getinfo line");
    lua_settop(L, 0);
    CHECK(Run(L, "local ok, e = pcall(function() fails() end) return e") && strcmp(lua_tostring(L, -1), "test:1: bad thing 7") == 0, "luaL_error where");
    lua_settop(L, 0);
    CHECK(Run(L, "return coroutine.wrap(function() return is51() end)()") && lua_toboolean(L, -1) == is51, "coroutine keeps VM");
    lua_settop(L, 0);

    /* pcall status, load modes */
    luaL_loadstring(L, "error('x')");
    CHECK(lua_pcall(L, 0, 0, 0) == LUA_ERRRUN, "pcall status");
    lua_settop(L, 0);
    const char bin[] = "\x1bLua";
    CHECK(luaL_loadbufferx(L, bin, 4, "=bin", "t") == LUA_ERRSYNTAX &&
              strstr(lua_tostring(L, -1), "attempt to load a binary chunk") != NULL, "text-only mode refuses bytecode");
    lua_settop(L, 0);

    /* stack shuffles */
    lua_pushinteger(L, 1); lua_pushinteger(L, 2); lua_pushinteger(L, 3);
    lua_insert(L, 1);   /* 3 1 2 */
    lua_remove(L, 2);   /* 3 2 */
    CHECK(lua_gettop(L) == 2 && lua_tointeger(L, 1) == 3 && lua_tointeger(L, 2) == 2, "insert/remove");
    lua_rotate(L, 1, 1); /* 2 3 */
    CHECK(lua_tointeger(L, 1) == 2 && lua_tointeger(L, 2) == 3, "rotate");
    lua_copy(L, 1, 2);
    CHECK(lua_tointeger(L, 2) == 2, "copy");
    CHECK(lua_absindex(L, -1) == 2, "absindex");
    lua_settop(L, 0);

    /* tables: geti/seti, next, len, rawlen */
    lua_newtable(L);
    lua_pushstring(L, "a"); lua_seti(L, 1, 1);
    lua_pushstring(L, "b"); lua_rawseti(L, 1, 2);
    CHECK(lua_geti(L, 1, 1) == LUA_TSTRING, "geti");
    lua_pop(L, 1);
    CHECK(lua_rawlen(L, 1) == 2, "rawlen");
    lua_len(L, 1);
    CHECK(lua_tointeger(L, -1) == 2, "len");
    lua_pop(L, 1);
    int keys = 0;
    lua_pushnil(L);
    while (lua_next(L, 1)) { ++keys; lua_pop(L, 1); }
    CHECK(keys == 2, "next");
    lua_settop(L, 0);

    /* compare and arith */
    lua_pushinteger(L, 2); lua_pushinteger(L, 3);
    CHECK(lua_compare(L, 1, 2, LUA_OPLE) && lua_compare(L, 1, 2, LUA_OPLT) && !lua_compare(L, 1, 2, LUA_OPEQ), "compare");
    lua_arith(L, LUA_OPADD);
    CHECK(lua_gettop(L) == 1 && lua_tointeger(L, -1) == 5, "arith");
    lua_settop(L, 0);

    /* userdata with a metatable, tolstring */
    luaL_newmetatable(L, "neon.test");
    lua_pop(L, 1);
    void *u = lua_newuserdata(L, 8);
    luaL_setmetatable(L, "neon.test");
    CHECK(luaL_checkudata(L, -1, "neon.test") == u, "checkudata");
    lua_settop(L, 0);
    lua_pushnumber(L, 1.5);
    CHECK(strcmp(luaL_tolstring(L, -1, NULL), "1.5") == 0, "tolstring");
    lua_settop(L, 0);

    /* traceback and count hook */
    luaL_traceback(L, L, "tb", 0);
    CHECK(strncmp(lua_tostring(L, -1), "tb\nstack traceback:", 19) == 0, "traceback");
    lua_settop(L, 0);
    hookCalls = 0;
    lua_sethook(L, CountHook, LUA_MASKCOUNT, 10);
    Run(L, "local x = 0 for i = 1, 100 do x = x + i end return x");
    lua_sethook(L, NULL, 0, 0);
    CHECK(hookCalls > 0 && lua_tointeger(L, -1) == 5050, "count hook");
    lua_settop(L, 0);

    /* review fixes: luaL_newlib (checkversion), user values, many-piece
       pushfstring, rawlen on a number, floor division, per-thread hooks */
    static const luaL_Reg funcs[] = {{"nothing", Nothing}, {NULL, NULL}};
    luaL_newlib(L, funcs);
    CHECK(lua_istable(L, -1), "luaL_newlib");
    lua_settop(L, 0);
    lua_newuserdata(L, 4);
    CHECK(lua_getuservalue(L, 1) == LUA_TNIL, "fresh userdata has no user value");
    lua_pop(L, 1);
    lua_newtable(L);
    lua_setuservalue(L, 1);
    CHECK(lua_getuservalue(L, 1) == LUA_TTABLE, "user value set");
    lua_pop(L, 1);
    lua_pushnil(L);
    lua_setuservalue(L, 1);
    CHECK(lua_getuservalue(L, 1) == LUA_TNIL, "user value cleared");
    lua_settop(L, 0);
    for (int i = 0; i < 18; ++i) lua_pushinteger(L, i);  /* near LUA_MINSTACK */
    lua_pushfstring(L, "%s%s%s%s%s%s%s%s%s%s%d", "a", "b", "c", "d", "e", "f", "g", "h", "i", "j", 1);
    CHECK(strcmp(lua_tostring(L, -1), "abcdefghij1") == 0 && lua_gettop(L) == 19, "pushfstring stack use");
    lua_settop(L, 0);
    lua_pushinteger(L, 12);
    CHECK(lua_rawlen(L, 1) == 0 && lua_type(L, 1) == LUA_TNUMBER, "rawlen leaves a number");
    lua_settop(L, 0);
    lua_pushinteger(L, 7); lua_pushinteger(L, 2);
    lua_arith(L, LUA_OPIDIV);
    CHECK(lua_tointeger(L, -1) == 3, "floor division");
    lua_settop(L, 0);
    hookCalls = otherHookCalls = 0;
    lua_sethook(L, CountHook, LUA_MASKCOUNT, 10);
    lua_State *co = lua_newthread(L);
    lua_sethook(co, OtherHook, LUA_MASKCOUNT, 10);
    lua_sethook(co, NULL, 0, 0);
    CHECK(lua_gethook(L) == CountHook, "main thread keeps its hook");
    Run(L, "local x = 0 for i = 1, 100 do x = x + i end return x");
    CHECK(hookCalls > 0 && otherHookCalls == 0, "hooks are per thread");
    lua_sethook(L, NULL, 0, 0);
    lua_settop(L, 0);

    /* version-specific behaviour stays */
    Run(L, "return unpack ~= nil");
    CHECK(lua_toboolean(L, -1) == is51, "unpack only in 5.1");
    lua_settop(L, 0);
}

int main(void) {
    vm = "5.3";
    lua_State *L = luaL_newstate();
    luaL_openlibs(L);
    Exercise(L, 0);
    lua_close(L);

    vm = "5.1";
    L = neon_lua_newstate51();
    luaL_openlibs(L);
    Exercise(L, 1);
    /* 5.1 environments */
    luaL_loadstring(L, "return y");
    lua_newtable(L);
    lua_pushinteger(L, 11);
    lua_setfield(L, -2, "y");
    CHECK(neon_lua51_setfenv(L, -2) == 1, "setfenv");
    CHECK(lua_pcall(L, 0, 1, 0) == LUA_OK && lua_tointeger(L, -1) == 11, "chunk runs in its environment");
    lua_close(L);

    if (failures) return 1;
    printf("ALL LUA API DISPATCH TESTS PASSED\n");
    return 0;
}
