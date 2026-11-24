/*
 * Shared by LuaVmDispatch.c (built against the Lua 5.3 headers) and
 * LuaVm51.c (built against the Lua 5.1 headers): the parts of the 5.3 C API
 * the 5.1 side has to speak, without either side's lua.h. LuaVmDispatch.c
 * checks every value here against the real 5.3 header.
 */
#ifndef NEON_LUA_VM_SHARED_H
#define NEON_LUA_VM_SHARED_H

#include <stdarg.h>
#include <stddef.h>

struct lua_State;

/* Lua 5.3 lua.h values (64-bit LUAI_MAXSTACK = 1000000). */
#define N53_REGISTRYINDEX (-1000000 - 1000)
#define N53_RIDX_MAINTHREAD 1
#define N53_RIDX_GLOBALS 2
#define N53_ERRERR 6
#define N53_GCISRUNNING 9
#define N53_OPEQ 0
#define N53_OPLT 1
#define N53_OPLE 2
#define N53_OPADD 0
#define N53_OPIDIV 6
#define N53_OPBAND 7
#define N53_OPBOR 8
#define N53_OPBXOR 9
#define N53_OPSHL 10
#define N53_OPSHR 11
#define N53_OPUNM 12
#define N53_OPBNOT 13
#define N53_IDSIZE 60

/* Lua 5.3's struct lua_Debug, field for field. */
typedef struct N53Debug {
    int event;
    const char *name;
    const char *namewhat;
    const char *what;
    const char *source;
    int currentline;
    int linedefined;
    int lastlinedefined;
    unsigned char nups;
    unsigned char nparams;
    char isvararg;
    char istailcall;
    char short_src[N53_IDSIZE];
    void *i_ci;
} N53Debug;

typedef void (*N53Hook)(struct lua_State *L, N53Debug *ar);

#endif
