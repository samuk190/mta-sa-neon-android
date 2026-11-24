/*
 * The Lua 5.3 C API on top of the Lua 5.1 VM (lua51/). LuaVmDispatch.c sends
 * every call on a Lua 5.1 state here; the script API (shared/Lua*.cpp) keeps
 * using the 5.3 API unchanged. Each function takes and returns what its 5.3
 * counterpart does (see LuaVmShared.h for the 5.3 lua_Debug and the constants).
 *
 * What differs between the two APIs and how it is bridged:
 * - Pseudo-indices: 5.3 has one registry index with upvalues below it, 5.1
 *   has registry, environment and globals indices. ToIndex51 maps them.
 * - The 5.1 registry gets 5.3's fixed slots on creation: [1] main thread and
 *   [2] globals (LUA_RIDX_*), so lua_pushglobaltable works and luaL_ref starts
 *   at 3 as in 5.3.
 * - Integers: 5.1 numbers are doubles. An integral double counts as an
 *   integer (lua_isinteger), which matches how desktop MTA scripts see them.
 * - Getters return the pushed value's type, as in 5.3.
 * - Status codes: 5.1's LUA_ERRERR (5) is 5.3's 6.
 * - lua_Debug: converted to and from 5.3's layout; the 5.1 call-info index
 *   travels in the 5.3 i_ci pointer field.
 * - lua_load modes ("t", "b") are checked on the first chunk byte.
 * - Continuations (callk, pcallk, yieldk) are ignored: 5.1 cannot resume a
 *   C function after a yield.
 */
#include "../lua51/lua51_prefix.h"
#include "../lua51/src/lua.h"
#include "../lua51/src/lauxlib.h"
#include "../lua51/src/lualib.h"

#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "LuaVmShared.h"

/* luautf8 (lua51/src/lutf8lib.c); lualib.h does not declare it. */
LUALIB_API int (luaopen_utf8)(lua_State *L);
void *lua51_extraspace(lua_State *L);

/* ---- indices -------------------------------------------------------- */

static int ToIndex51(int idx) {
    if (idx > N53_REGISTRYINDEX) return idx;                 /* stack index */
    if (idx == N53_REGISTRYINDEX) return LUA_REGISTRYINDEX;
    return lua_upvalueindex(N53_REGISTRYINDEX - idx);          /* upvalue i */
}

static int AbsIndex51(lua_State *L, int idx) {
    return (idx > 0 || idx <= LUA_REGISTRYINDEX) ? idx : lua_gettop(L) + 1 + idx;
}

static int Status53(int status) { return status == LUA_ERRERR ? N53_ERRERR : status; }

/* ---- state ---------------------------------------------------------- */

static const char kHooksKey[] = "neon.lua51.hooks";         /* thread -> 5.3 hook (weak keys) */
static const char kNoUserValueKey[] = "neon.lua51.nouservalue"; /* env of a userdata with no user value */

/* Pushes the table standing for "no user value". */
static void PushNoUserValue(lua_State *L) {
    lua_getfield(L, LUA_REGISTRYINDEX, kNoUserValueKey);
}

lua_State *n51_newstate(lua_Alloc f, void *ud, void *tag) {
    lua_State *L = lua_newstate(f, ud);
    if (!L) return NULL;
    *(void **)lua51_extraspace(L) = tag;
    /* 5.3's fixed registry slots (LUA_RIDX_MAINTHREAD, LUA_RIDX_GLOBALS). */
    lua_pushthread(L);
    lua_rawseti(L, LUA_REGISTRYINDEX, N53_RIDX_MAINTHREAD);
    lua_pushvalue(L, LUA_GLOBALSINDEX);
    lua_rawseti(L, LUA_REGISTRYINDEX, N53_RIDX_GLOBALS);
    lua_newtable(L);
    lua_setfield(L, LUA_REGISTRYINDEX, kNoUserValueKey);
    lua_newtable(L);                         /* hooks, keyed by thread */
    lua_createtable(L, 0, 1);
    lua_pushliteral(L, "k");
    lua_setfield(L, -2, "__mode");
    lua_setmetatable(L, -2);
    lua_setfield(L, LUA_REGISTRYINDEX, kHooksKey);
    return L;
}

void n51_close(lua_State *L) { lua_close(L); }
lua_State *n51_newthread(lua_State *L) { return lua_newthread(L); }
lua_CFunction n51_atpanic(lua_State *L, lua_CFunction f) { return lua_atpanic(L, f); }

/* Not called: LuaVmDispatch.c answers lua_version for both VMs. */
const double *n51_version(lua_State *L) {
    (void)L;
    return NULL;
}

/* ---- stack ---------------------------------------------------------- */

int n51_absindex(lua_State *L, int idx) {
    return (idx > 0 || idx <= N53_REGISTRYINDEX) ? idx : lua_gettop(L) + 1 + idx;
}
int n51_gettop(lua_State *L) { return lua_gettop(L); }
void n51_settop(lua_State *L, int idx) { lua_settop(L, idx); }
void n51_pushvalue(lua_State *L, int idx) { lua_pushvalue(L, ToIndex51(idx)); }

void n51_rotate(lua_State *L, int idx, int n) {
    const int t = lua_gettop(L);
    const int a = AbsIndex51(L, ToIndex51(idx));
    const int len = t - a + 1;
    if (len <= 0) return;
    n %= len;
    if (n < 0) n += len;
    while (n-- > 0) lua_insert(L, a);  /* top element to a: one step right */
}

void n51_copy(lua_State *L, int from, int to) {
    lua_pushvalue(L, ToIndex51(from));
    lua_replace(L, ToIndex51(to));
}

int n51_checkstack(lua_State *L, int n) { return lua_checkstack(L, n); }
void n51_xmove(lua_State *from, lua_State *to, int n) { lua_xmove(from, to, n); }

/* ---- access --------------------------------------------------------- */

int n51_isnumber(lua_State *L, int idx) { return lua_isnumber(L, ToIndex51(idx)); }
int n51_isstring(lua_State *L, int idx) { return lua_isstring(L, ToIndex51(idx)); }
int n51_iscfunction(lua_State *L, int idx) { return lua_iscfunction(L, ToIndex51(idx)); }
int n51_isuserdata(lua_State *L, int idx) { return lua_isuserdata(L, ToIndex51(idx)); }
int n51_type(lua_State *L, int idx) { return lua_type(L, ToIndex51(idx)); }
const char *n51_typename(lua_State *L, int tp) { return lua_typename(L, tp); }

static int IntegralNumber(double n, long long *out) {
    if (!(n == floor(n)) || n < -9223372036854775808.0 || n >= 9223372036854775808.0) return 0;
    if (out) *out = (long long)n;
    return 1;
}

int n51_isinteger(lua_State *L, int idx) {
    idx = ToIndex51(idx);
    return lua_type(L, idx) == LUA_TNUMBER && IntegralNumber(lua_tonumber(L, idx), NULL);
}

double n51_tonumberx(lua_State *L, int idx, int *isnum) {
    idx = ToIndex51(idx);
    const int ok = lua_isnumber(L, idx);
    if (isnum) *isnum = ok;
    return ok ? lua_tonumber(L, idx) : 0;
}

/* Lua 5.1's lua_tointeger truncates a fractional number (lua_number2integer),
 * and desktop MTA's argument reader accepts one, so 5.1 states convert it
 * instead of failing as 5.3 does: setTimer(f, 1000/30) runs every 33 ms as on
 * desktop. Non-finite and out-of-range numbers still fail. lua_isinteger keeps
 * the exact rule. */
long long n51_tointegerx(lua_State *L, int idx, int *isnum) {
    idx = ToIndex51(idx);
    int ok = 0;
    long long v = 0;
    if (lua_isnumber(L, idx)) {
        const double n = lua_tonumber(L, idx);
        if (n == n && n >= -9223372036854775808.0 && n < 9223372036854775808.0) {
            v = (long long)n;
            ok = 1;
        }
    }
    if (isnum) *isnum = ok;
    return ok ? v : 0;
}

int n51_toboolean(lua_State *L, int idx) { return lua_toboolean(L, ToIndex51(idx)); }
const char *n51_tolstring(lua_State *L, int idx, size_t *len) { return lua_tolstring(L, ToIndex51(idx), len); }
size_t n51_rawlen(lua_State *L, int idx) {
    idx = ToIndex51(idx);
    switch (lua_type(L, idx)) {
        /* lua_objlen would turn a number into a string in place; 5.3 gives 0. */
        case LUA_TSTRING: case LUA_TTABLE: case LUA_TUSERDATA: return lua_objlen(L, idx);
        default: return 0;
    }
}
lua_CFunction n51_tocfunction(lua_State *L, int idx) { return lua_tocfunction(L, ToIndex51(idx)); }
void *n51_touserdata(lua_State *L, int idx) { return lua_touserdata(L, ToIndex51(idx)); }
lua_State *n51_tothread(lua_State *L, int idx) { return lua_tothread(L, ToIndex51(idx)); }
const void *n51_topointer(lua_State *L, int idx) { return lua_topointer(L, ToIndex51(idx)); }
int n51_rawequal(lua_State *L, int a, int b) { return lua_rawequal(L, ToIndex51(a), ToIndex51(b)); }

/* Runs a small Lua function on the two values at a and b (b ignored when
 * unary), with metamethods, and leaves its single result on the stack. */
static void RunOperator(lua_State *L, const char *code, int a, int b, int unary) {
    a = AbsIndex51(L, ToIndex51(a));
    if (!unary) b = AbsIndex51(L, ToIndex51(b));
    if (luaL_loadbuffer(L, code, strlen(code), "=(operator)") != 0) lua_error(L);
    lua_pushvalue(L, a);
    if (!unary) lua_pushvalue(L, b);
    lua_call(L, unary ? 1 : 2, 1);
}

int n51_compare(lua_State *L, int a, int b, int op) {
    a = ToIndex51(a);
    b = ToIndex51(b);
    if (lua_type(L, a) == LUA_TNONE || lua_type(L, b) == LUA_TNONE) return 0;
    if (op == N53_OPEQ) return lua_equal(L, a, b);
    if (op == N53_OPLT) return lua_lessthan(L, a, b);
    RunOperator(L, "local a, b = ... return a <= b", a, b, 0);
    const int r = lua_toboolean(L, -1);
    lua_pop(L, 1);
    return r;
}

/* lua_arith pops its operands and pushes the result. Arithmetic runs as Lua
 * code (metamethods apply); the 5.3 integer and bitwise operators work on
 * values with an exact 64-bit integer form. */
void n51_arith(lua_State *L, int op) {
    static const char *const kCode[] = {
        "local a, b = ... return a + b", "local a, b = ... return a - b", "local a, b = ... return a * b",
        "local a, b = ... return a % b", "local a, b = ... return a ^ b", "local a, b = ... return a / b",
    };
    const int unary = op == N53_OPUNM || op == N53_OPBNOT;
    if (op == N53_OPUNM) {
        RunOperator(L, "local a = ... return -a", -1, 0, 1);
        lua_remove(L, -2);
        return;
    }
    if (op == N53_OPIDIV) {
        /* Floor division on numbers, in C (5.1 has no // nor __idiv). */
        if (!lua_isnumber(L, -2) || !lua_isnumber(L, -1))
            luaL_error(L, "attempt to perform arithmetic on a %s value",
                       lua_typename(L, lua_type(L, lua_isnumber(L, -2) ? -1 : -2)));
        const double a = lua_tonumber(L, -2), b = lua_tonumber(L, -1);
        if (b == 0 && IntegralNumber(a, NULL) && IntegralNumber(b, NULL)) luaL_error(L, "attempt to perform 'n//0'");
        lua_pop(L, 2);
        lua_pushnumber(L, floor(a / b));
        return;
    }
    if (op < N53_OPIDIV) {
        RunOperator(L, kCode[op], -2, -1, 0);
        lua_replace(L, -3);
        lua_pop(L, 1);
        return;
    }
    long long a = 0, b = 0;
    if (!IntegralNumber(lua_tonumber(L, unary ? -1 : -2), &a) || (!unary && !IntegralNumber(lua_tonumber(L, -1), &b)))
        luaL_error(L, "number has no integer representation");
    unsigned long long r = 0, ua = (unsigned long long)a, ub = (unsigned long long)b;
    switch (op) {
        case N53_OPBAND: r = ua & ub; break;
        case N53_OPBOR: r = ua | ub; break;
        case N53_OPBXOR: r = ua ^ ub; break;
        case N53_OPSHL: r = b >= 64 || b <= -64 ? 0 : b >= 0 ? ua << b : ua >> -b; break;
        case N53_OPSHR: r = b >= 64 || b <= -64 ? 0 : b >= 0 ? ua >> b : ua << -b; break;
        case N53_OPBNOT: r = ~ua; break;
        default: luaL_error(L, "unknown arithmetic operator %d", op);
    }
    lua_pop(L, unary ? 1 : 2);
    lua_pushnumber(L, (double)(long long)r);
}

/* ---- push ----------------------------------------------------------- */

void n51_pushnil(lua_State *L) { lua_pushnil(L); }
void n51_pushnumber(lua_State *L, double n) { lua_pushnumber(L, n); }
void n51_pushinteger(lua_State *L, long long n) { lua_pushnumber(L, (double)n); }

const char *n51_pushlstring(lua_State *L, const char *s, size_t len) {
    lua_pushlstring(L, s, len);
    return lua_tostring(L, -1);
}

const char *n51_pushstring(lua_State *L, const char *s) {
    if (!s) {
        lua_pushnil(L);
        return NULL;
    }
    lua_pushstring(L, s);
    return lua_tostring(L, -1);
}

/* 5.3's lua_pushvfstring conversions: %% %s %f %I %p %d %c %U. 5.1 lacks %I
 * and %U, so the string is built here, as 5.3's luaO_pushvfstring does. */
/* Each piece is joined to the result at once, so the stack grows by two at
 * most (5.1 does not grow it on push). */
const char *n51_pushvfstring(lua_State *L, const char *fmt, va_list argp) {
    const char *e;
    char buf[64];
    luaL_checkstack(L, 3, "pushfstring");
    lua_pushliteral(L, "");
    while ((e = strchr(fmt, '%')) != NULL) {
        lua_pushlstring(L, fmt, (size_t)(e - fmt));
        lua_concat(L, 2);
        switch (e[1]) {
            case 's': {
                const char *s = va_arg(argp, char *);
                lua_pushstring(L, s ? s : "(null)");
                break;
            }
            case 'c': buf[0] = (char)va_arg(argp, int); lua_pushlstring(L, buf, 1); break;
            case 'd': snprintf(buf, sizeof(buf), "%d", va_arg(argp, int)); lua_pushstring(L, buf); break;
            case 'I': snprintf(buf, sizeof(buf), "%lld", (long long)va_arg(argp, long long)); lua_pushstring(L, buf); break;
            case 'f': lua_pushnumber(L, (double)va_arg(argp, double)); lua_tostring(L, -1); break;
            case 'p': snprintf(buf, sizeof(buf), "%p", va_arg(argp, void *)); lua_pushstring(L, buf); break;
            case 'U': {
                unsigned long x = (unsigned long)va_arg(argp, long);
                char u[8];
                int n = 0;
                if (x < 0x80) u[n++] = (char)x;
                else if (x < 0x800) { u[n++] = (char)(0xC0 | (x >> 6)); u[n++] = (char)(0x80 | (x & 0x3F)); }
                else if (x < 0x10000) { u[n++] = (char)(0xE0 | (x >> 12)); u[n++] = (char)(0x80 | ((x >> 6) & 0x3F)); u[n++] = (char)(0x80 | (x & 0x3F)); }
                else { u[n++] = (char)(0xF0 | (x >> 18)); u[n++] = (char)(0x80 | ((x >> 12) & 0x3F)); u[n++] = (char)(0x80 | ((x >> 6) & 0x3F)); u[n++] = (char)(0x80 | (x & 0x3F)); }
                lua_pushlstring(L, u, (size_t)n);
                break;
            }
            case '%': lua_pushliteral(L, "%"); break;
            default: luaL_error(L, "invalid option '%%%c' to 'lua_pushfstring'", e[1]);
        }
        lua_concat(L, 2);
        fmt = e + 2;
    }
    lua_pushstring(L, fmt);
    lua_concat(L, 2);
    return lua_tostring(L, -1);
}

void n51_pushcclosure(lua_State *L, lua_CFunction fn, int n) { lua_pushcclosure(L, fn, n); }
void n51_pushboolean(lua_State *L, int b) { lua_pushboolean(L, b); }
void n51_pushlightuserdata(lua_State *L, void *p) { lua_pushlightuserdata(L, p); }
int n51_pushthread(lua_State *L) { return lua_pushthread(L); }

/* ---- get ------------------------------------------------------------ */

int n51_getglobal(lua_State *L, const char *name) {
    lua_getfield(L, LUA_GLOBALSINDEX, name);
    return lua_type(L, -1);
}
int n51_gettable(lua_State *L, int idx) {
    lua_gettable(L, ToIndex51(idx));
    return lua_type(L, -1);
}
int n51_getfield(lua_State *L, int idx, const char *k) {
    lua_getfield(L, ToIndex51(idx), k);
    return lua_type(L, -1);
}
int n51_geti(lua_State *L, int idx, long long n) {
    idx = AbsIndex51(L, ToIndex51(idx));
    lua_pushnumber(L, (double)n);
    lua_gettable(L, idx);
    return lua_type(L, -1);
}
int n51_rawget(lua_State *L, int idx) {
    lua_rawget(L, ToIndex51(idx));
    return lua_type(L, -1);
}
int n51_rawgeti(lua_State *L, int idx, long long n) {
    idx = ToIndex51(idx);
    if (n >= INT32_MIN && n <= INT32_MAX) {
        lua_rawgeti(L, idx, (int)n);
    } else {
        idx = AbsIndex51(L, idx);
        lua_pushnumber(L, (double)n);
        lua_rawget(L, idx);
    }
    return lua_type(L, -1);
}
int n51_rawgetp(lua_State *L, int idx, const void *p) {
    idx = AbsIndex51(L, ToIndex51(idx));
    lua_pushlightuserdata(L, (void *)p);
    lua_rawget(L, idx);
    return lua_type(L, -1);
}
void n51_createtable(lua_State *L, int narr, int nrec) { lua_createtable(L, narr, nrec); }
void *n51_newuserdata(lua_State *L, size_t sz) {
    void *p = lua_newuserdata(L, sz);
    /* 5.1 gives it the running function's environment; 5.3 starts with nil. */
    PushNoUserValue(L);
    lua_setfenv(L, -2);
    return p;
}
int n51_getmetatable(lua_State *L, int idx) { return lua_getmetatable(L, ToIndex51(idx)); }
int n51_getuservalue(lua_State *L, int idx) {
    lua_getfenv(L, ToIndex51(idx));
    PushNoUserValue(L);
    const int none = lua_rawequal(L, -1, -2);
    lua_pop(L, 1);
    if (none) {
        lua_pop(L, 1);
        lua_pushnil(L);
    }
    return lua_type(L, -1);
}

/* ---- set ------------------------------------------------------------ */

void n51_setglobal(lua_State *L, const char *name) { lua_setfield(L, LUA_GLOBALSINDEX, name); }
void n51_settable(lua_State *L, int idx) { lua_settable(L, ToIndex51(idx)); }
void n51_setfield(lua_State *L, int idx, const char *k) { lua_setfield(L, ToIndex51(idx), k); }
void n51_seti(lua_State *L, int idx, long long n) {
    idx = AbsIndex51(L, ToIndex51(idx));
    lua_pushnumber(L, (double)n);
    lua_insert(L, -2);
    lua_settable(L, idx);
}
void n51_rawset(lua_State *L, int idx) { lua_rawset(L, ToIndex51(idx)); }
void n51_rawseti(lua_State *L, int idx, long long n) {
    idx = ToIndex51(idx);
    if (n >= INT32_MIN && n <= INT32_MAX) {
        lua_rawseti(L, idx, (int)n);
    } else {
        idx = AbsIndex51(L, idx);
        lua_pushnumber(L, (double)n);
        lua_insert(L, -2);
        lua_rawset(L, idx);
    }
}
void n51_rawsetp(lua_State *L, int idx, const void *p) {
    idx = AbsIndex51(L, ToIndex51(idx));
    lua_pushlightuserdata(L, (void *)p);
    lua_insert(L, -2);
    lua_rawset(L, idx);
}
int n51_setmetatable(lua_State *L, int idx) { return lua_setmetatable(L, ToIndex51(idx)); }
/* 5.3 allows nil (5.1's lua_setfenv would take any value for a table). */
void n51_setuservalue(lua_State *L, int idx) {
    idx = AbsIndex51(L, ToIndex51(idx));
    if (lua_type(L, -1) != LUA_TTABLE) {
        lua_pop(L, 1);
        PushNoUserValue(L);
    }
    lua_setfenv(L, idx);
}

/* ---- calls ---------------------------------------------------------- */

void n51_call(lua_State *L, int nargs, int nresults) { lua_call(L, nargs, nresults); }
int n51_pcall(lua_State *L, int nargs, int nresults, int errfunc) {
    return Status53(lua_pcall(L, nargs, nresults, errfunc ? ToIndex51(errfunc) : 0));
}

typedef struct {
    lua_Reader reader;
    void *data;
    const char *mode;
    int first;
    int refused;  /* 't' or 'b': the kind of chunk the mode does not allow */
} ModeReader;

static const char *ReadWithMode(lua_State *L, void *ud, size_t *size) {
    ModeReader *m = (ModeReader *)ud;
    const char *chunk = m->reader(L, m->data, size);
    if (m->first && chunk && *size > 0) {
        m->first = 0;
        const int binary = chunk[0] == LUA_SIGNATURE[0];
        if (m->mode && !strchr(m->mode, binary ? 'b' : 't')) {
            m->refused = binary ? 'b' : 't';
            *size = 0;
            return NULL;
        }
    }
    return chunk;
}

int n51_load(lua_State *L, lua_Reader reader, void *data, const char *chunkname, const char *mode) {
    ModeReader m = {reader, data, mode, 1, 0};
    const int status = lua_load(L, ReadWithMode, &m, chunkname);
    if (m.refused) {
        lua_pop(L, 1);
        lua_pushfstring(L, "attempt to load a %s chunk (mode is '%s')", m.refused == 'b' ? "binary" : "text", mode);
        return LUA_ERRSYNTAX;
    }
    return Status53(status);
}

int n51_dump(lua_State *L, lua_Writer writer, void *data, int strip) {
    (void)strip;
    return lua_dump(L, writer, data);
}

int n51_yield(lua_State *L, int nresults) { return lua_yield(L, nresults); }
int n51_resume(lua_State *L, lua_State *from, int narg) {
    (void)from;
    return Status53(lua_resume(L, narg));
}
int n51_status(lua_State *L) { return Status53(lua_status(L)); }

int n51_isyieldable(lua_State *L) {
    lua_rawgeti(L, LUA_REGISTRYINDEX, N53_RIDX_MAINTHREAD);
    const int main = lua_tothread(L, -1) == L;
    lua_pop(L, 1);
    return !main;
}

/* ---- misc ----------------------------------------------------------- */

int n51_gc(lua_State *L, int what, int data) {
    if (what == N53_GCISRUNNING) return 1;
    return lua_gc(L, what, data);
}
int n51_error(lua_State *L) { return lua_error(L); }
int n51_next(lua_State *L, int idx) { return lua_next(L, ToIndex51(idx)); }
void n51_concat(lua_State *L, int n) { lua_concat(L, n); }

void n51_len(lua_State *L, int idx) {
    idx = AbsIndex51(L, ToIndex51(idx));
    const int t = lua_type(L, idx);
    if (t == LUA_TSTRING) {
        lua_pushnumber(L, (double)lua_objlen(L, idx));
    } else if (luaL_callmeta(L, idx, "__len")) {
        /* the metamethod's result is on the stack */
    } else if (t == LUA_TTABLE) {
        lua_pushnumber(L, (double)lua_objlen(L, idx));
    } else {
        luaL_error(L, "attempt to get length of a %s value", lua_typename(L, t));
    }
}

size_t n51_stringtonumber(lua_State *L, const char *s) {
    lua_pushstring(L, s);
    if (!lua_isnumber(L, -1)) {
        lua_pop(L, 1);
        return 0;
    }
    const double n = lua_tonumber(L, -1);
    lua_pop(L, 1);
    lua_pushnumber(L, n);
    return strlen(s) + 1;
}

lua_Alloc n51_getallocf(lua_State *L, void **ud) { return lua_getallocf(L, ud); }
void n51_setallocf(lua_State *L, lua_Alloc f, void *ud) { lua_setallocf(L, f, ud); }

/* ---- debug ---------------------------------------------------------- */

static void To53(const lua_Debug *a, N53Debug *b) {
    b->event = a->event;
    b->name = a->name;
    b->namewhat = a->namewhat;
    b->what = a->what;
    b->source = a->source;
    b->currentline = a->currentline;
    b->linedefined = a->linedefined;
    b->lastlinedefined = a->lastlinedefined;
    b->nups = (unsigned char)a->nups;
    memcpy(b->short_src, a->short_src, sizeof(b->short_src));
    b->short_src[sizeof(b->short_src) - 1] = '\0';
    b->i_ci = (void *)(intptr_t)a->i_ci;
}

static void From53(const N53Debug *b, lua_Debug *a) {
    memset(a, 0, sizeof(*a));
    a->event = b->event;
    a->i_ci = (int)(intptr_t)b->i_ci;
}

int n51_getstack(lua_State *L, int level, N53Debug *ar) {
    lua_Debug d;
    memset(&d, 0, sizeof(d));
    if (!lua_getstack(L, level, &d)) return 0;
    To53(&d, ar);
    return 1;
}

int n51_getinfo(lua_State *L, const char *what, N53Debug *ar) {
    /* 5.3's 't' (istailcall) does not exist in 5.1. */
    char opts[16];
    size_t n = 0;
    for (const char *c = what; *c && n + 1 < sizeof(opts); ++c)
        if (*c != 't') opts[n++] = *c;
    opts[n] = '\0';
    lua_Debug d;
    From53(ar, &d);
    const int ok = lua_getinfo(L, opts, &d);
    const N53Debug keep = *ar;
    To53(&d, ar);
    if (what[0] != '>') ar->i_ci = keep.i_ci;
    ar->nparams = 0;   /* 5.1 does not report it */
    ar->isvararg = 1;
    ar->istailcall = 0;
    return ok;
}

const char *n51_getlocal(lua_State *L, const N53Debug *ar, int n) {
    if (!ar) return NULL;  /* 5.3's parameter-name form has no 5.1 equivalent */
    lua_Debug d;
    From53(ar, &d);
    return lua_getlocal(L, &d, n);
}

const char *n51_setlocal(lua_State *L, const N53Debug *ar, int n) {
    lua_Debug d;
    From53(ar, &d);
    return lua_setlocal(L, &d, n);
}

const char *n51_getupvalue(lua_State *L, int f, int n) { return lua_getupvalue(L, ToIndex51(f), n); }
const char *n51_setupvalue(lua_State *L, int f, int n) { return lua_setupvalue(L, ToIndex51(f), n); }

void *n51_upvalueid(lua_State *L, int f, int n) {
    (void)L; (void)f; (void)n;
    return NULL;  /* not in 5.1 */
}

void n51_upvaluejoin(lua_State *L, int f1, int n1, int f2, int n2) {
    (void)f1; (void)n1; (void)f2; (void)n2;
    luaL_error(L, "lua_upvaluejoin is not available in Lua 5.1");
}

/* The 5.3 hook of each thread is kept in a registry table keyed by the
 * thread; this 5.1 hook looks it up and converts the record. */
static N53Hook StoredHook(lua_State *L) {
    lua_getfield(L, LUA_REGISTRYINDEX, kHooksKey);
    lua_pushthread(L);
    lua_rawget(L, -2);
    N53Hook hook = (N53Hook)(uintptr_t)lua_touserdata(L, -1);
    lua_pop(L, 2);
    return hook;
}

static void Hook51(lua_State *L, lua_Debug *ar) {
    N53Hook hook = StoredHook(L);
    if (!hook) return;
    N53Debug d;
    memset(&d, 0, sizeof(d));
    To53(ar, &d);
    hook(L, &d);
}

void n51_sethook(lua_State *L, N53Hook func, int mask, int count) {
    lua_getfield(L, LUA_REGISTRYINDEX, kHooksKey);
    lua_pushthread(L);
    if (func) lua_pushlightuserdata(L, (void *)(uintptr_t)func);
    else lua_pushnil(L);
    lua_rawset(L, -3);
    lua_pop(L, 1);
    lua_sethook(L, func ? Hook51 : NULL, func ? mask : 0, count);
}

N53Hook n51_gethook(lua_State *L) { return lua_gethook(L) ? StoredHook(L) : NULL; }
int n51_gethookmask(lua_State *L) { return lua_gethookmask(L); }
int n51_gethookcount(lua_State *L) { return lua_gethookcount(L); }

/* ---- libraries and 5.1-only calls ---------------------------------- */

int n51_luaopen(lua_State *L, const char *name) {
    static const struct { const char *name; lua_CFunction open; } kLibs[] = {
        {"base", luaopen_base}, {"table", luaopen_table}, {"string", luaopen_string}, {"math", luaopen_math},
        {"debug", luaopen_debug}, {"os", luaopen_os}, {"io", luaopen_io}, {"package", luaopen_package},
    };
    for (size_t i = 0; i < sizeof(kLibs) / sizeof(kLibs[0]); ++i)
        if (strcmp(kLibs[i].name, name) == 0) return kLibs[i].open(L);
    if (strcmp(name, "utf8") == 0) return luaopen_utf8(L);
    /* coroutine is part of base in 5.1; bit32 does not exist there. */
    if (strcmp(name, "coroutine") == 0) {
        lua_getfield(L, LUA_GLOBALSINDEX, "coroutine");
        return 1;
    }
    return luaL_error(L, "library '%s' does not exist in Lua 5.1", name);
}

void n51_openlibs(lua_State *L) { luaL_openlibs(L); luaopen_utf8(L); lua_settop(L, 0); }

int n51_setfenv(lua_State *L, int idx) {
    if (lua_type(L, -1) != LUA_TTABLE) {  /* lua_setfenv does not check */
        lua_pop(L, 1);
        return 0;
    }
    return lua_setfenv(L, ToIndex51(idx));
}
void n51_getfenv(lua_State *L, int idx) { lua_getfenv(L, ToIndex51(idx)); }
