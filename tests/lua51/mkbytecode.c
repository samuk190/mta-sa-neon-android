/* Compiles a Lua 5.1 source file to bytecode in this host's layout;
 * to32.py turns it into the 32-bit layout luac.mtasa.com produces. */
#include <stdio.h>
#include "lua51_prefix.h"
#include "../../app/src/main/cpp/lua51/src/lua.h"
#include "../../app/src/main/cpp/lua51/src/lauxlib.h"

static int writer(lua_State *L, const void *p, size_t n, void *ud) {
    (void)L;
    return fwrite(p, 1, n, (FILE *)ud) == n ? 0 : 1;
}

int main(int argc, char **argv) {
    if (argc != 3) return 2;
    lua_State *L = luaL_newstate();
    if (luaL_loadfile(L, argv[1]) != 0) { fprintf(stderr, "%s\n", lua_tostring(L, -1)); return 1; }
    FILE *out = fopen(argv[2], "wb");
    if (!out || lua_dump(L, writer, out) != 0) return 1;
    fclose(out);
    lua_close(L);
    return 0;
}
