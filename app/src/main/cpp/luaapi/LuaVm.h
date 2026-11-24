/*
 * The two script VMs (luaapi/README.md). Everything else uses the Lua 5.3 C
 * API on either kind of state; these are the calls that differ.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

struct lua_State;

/* A new Lua 5.1 state (desktop MTA resources), with no libraries open. The
 * 5.3 API works on it; lua_newstate/luaL_newstate still create 5.3 states. */
struct lua_State *neon_lua_newstate51(void);

/* 1 when L (or a thread of it) belongs to the Lua 5.1 VM. */
int neon_lua_is51(struct lua_State *L);

/* Lua 5.1 function environments (5.1 has no _ENV upvalue). setfenv pops a
 * table and returns 0 when the value cannot take one; both do nothing on a
 * 5.3 state (getfenv pushes nil). */
int neon_lua51_setfenv(struct lua_State *L, int idx);
void neon_lua51_getfenv(struct lua_State *L, int idx);

#ifdef __cplusplus
}
#endif
