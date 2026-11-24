// Lua 5.1 (lua51_*) and Lua 5.3 (lua_*) in one binary: runs 5.1 source,
// loads 32-bit bytecode made by mkbytecode (-m32), and checks that the
// extraspace tag set on a state reaches its coroutines.
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>

extern "C" {
#include "lua.h"  // Lua 5.3, as the script API sees it
#include "lauxlib.h"
#include "lualib.h"
}

// Lua 5.1 entry points (prefixed); declared by hand so the 5.1 headers stay
// out of this translation unit, as they will in the dispatcher.
extern "C" {
struct lua51_State;
lua51_State* lua51_luaL_newstate();
void lua51_luaL_openlibs(lua51_State* L);
int lua51_luaopen_utf8(lua51_State* L);
int lua51_luaL_loadbuffer(lua51_State* L, const char* b, size_t n, const char* name);
int lua51_lua_pcall(lua51_State* L, int nargs, int nresults, int errfunc);
double lua51_lua_tonumber(lua51_State* L, int idx);
const char* lua51_lua_tolstring(lua51_State* L, int idx, size_t* len);
lua51_State* lua51_lua_newthread(lua51_State* L);
void lua51_lua_settop(lua51_State* L, int idx);
void lua51_lua_close(lua51_State* L);
void* lua51_extraspace(lua51_State* L);
}

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { std::printf("FAIL: " __VA_ARGS__); std::printf("\n"); ++failures; } } while (0)

static double Run51(lua51_State* L, const std::string& chunk, const char* name) {
    if (lua51_luaL_loadbuffer(L, chunk.data(), chunk.size(), name) != 0 || lua51_lua_pcall(L, 0, 1, 0) != 0) {
        std::printf("FAIL: %s: %s\n", name, lua51_lua_tolstring(L, -1, nullptr));
        ++failures;
        lua51_lua_settop(L, 0);
        return -1;
    }
    const double v = lua51_lua_tonumber(L, -1);
    lua51_lua_settop(L, 0);
    return v;
}

int main(int argc, char** argv) {
    if (argc != 4) return 2;
    std::ifstream src(argv[1]), bc(argv[2], std::ios::binary), bc64(argv[3], std::ios::binary);
    const std::string source((std::istreambuf_iterator<char>(src)), {}), bytecode((std::istreambuf_iterator<char>(bc)), {}),
        native64((std::istreambuf_iterator<char>(bc64)), {});
    // 10+20+30 + 3 + 7 + 3 + 1 + 5
    constexpr double kExpected = 79;

    lua51_State* L = lua51_luaL_newstate();
    lua51_luaL_openlibs(L);
    lua51_luaopen_utf8(L);  // as desktop CLuaMain::InitVM
    lua51_lua_settop(L, 0);
    CHECK(Run51(L, source, "=sample.lua") == kExpected, "5.1 source result");
    CHECK(!bytecode.empty() && bytecode[0] == '\x1b', "bytecode file");
    CHECK(Run51(L, bytecode, "=sample.luac") == kExpected, "32-bit bytecode result");
    // As desktop MTA: only the 32-bit layout loads, and scripts cannot dump.
    CHECK(lua51_luaL_loadbuffer(L, native64.data(), native64.size(), "=native64") != 0, "64-bit chunk rejected");
    lua51_lua_settop(L, 0);
    CHECK(Run51(L, "local ok = pcall(string.dump, function() end) return ok and 0 or 1", "=dump") == 1,
          "string.dump fails");

    static int tag;
    *static_cast<void**>(lua51_extraspace(L)) = &tag;
    lua51_State* co = lua51_lua_newthread(L);
    CHECK(*static_cast<void**>(lua51_extraspace(co)) == &tag, "coroutine keeps the VM tag");
    lua51_lua_close(L);

    // Lua 5.3 beside it: 5.1-only code fails there, 5.3 integers work.
    lua_State* L3 = luaL_newstate();
    luaL_openlibs(L3);
    CHECK(luaL_dostring(L3, "return unpack == nil and 7 // 2 == 3") == LUA_OK && lua_toboolean(L3, -1), "lua 5.3 beside 5.1");
    lua_close(L3);

    if (failures) return 1;
    std::printf("ALL LUA 5.1 TESTS PASSED\n");
    return 0;
}
