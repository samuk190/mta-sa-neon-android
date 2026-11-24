/* Lua 5.3's lauxlib.c, unprefixed, on top of LuaVmDispatch.c: every luaL_*
 * function the script API calls works on 5.3 and 5.1 states with 5.3
 * behaviour. Lua 5.3's own libraries use the lua53_ copy built in lua53/. */
#include "../lua53/src/lauxlib.c"
