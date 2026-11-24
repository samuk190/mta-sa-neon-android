/*
 * The public Lua C API (lua_*, luaopen_*, luaL_openlibs) for the two VMs in
 * libsaonlineneon: Lua 5.3 (lua53/, built as lua53_*) and Lua 5.1 (lua51/,
 * built as lua51_*). The script API is written against the 5.3 API; each call
 * goes to the VM that owns the state: 5.1 states to LuaVm51.c, which provides
 * the 5.3 API on top of 5.1, the rest to Lua 5.3 unchanged.
 *
 * A state's VM is the tag in the pointer-sized area before it (5.3's
 * LUA_EXTRASPACE, 5.1's LUAI_EXTRASPACE, which threads inherit). Every state
 * is created here (lua_newstate, neon_lua_newstate51), so every state has one.
 *
 * Generated forwarders (one per 5.3 lua.h function) are followed by the
 * hand-written ones. luaapi/README.md has the details.
 */
#include "../lua53/lua53_prefix.h"
#include "../lua53/src/lua.h"
#include "../lua53/src/lauxlib.h"
#include "../lua53/src/lualib.h"

#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>

#include "LuaVmShared.h"
#include "LuaVm.h"

/* The prefixed 5.3 functions stay declared; their public names are defined here. */
#undef lua_newstate
#undef lua_close
#undef lua_newthread
#undef lua_atpanic
#undef lua_version
#undef lua_absindex
#undef lua_gettop
#undef lua_settop
#undef lua_pushvalue
#undef lua_rotate
#undef lua_copy
#undef lua_checkstack
#undef lua_xmove
#undef lua_isnumber
#undef lua_isstring
#undef lua_iscfunction
#undef lua_isinteger
#undef lua_isuserdata
#undef lua_type
#undef lua_typename
#undef lua_tonumberx
#undef lua_tointegerx
#undef lua_toboolean
#undef lua_tolstring
#undef lua_rawlen
#undef lua_tocfunction
#undef lua_touserdata
#undef lua_tothread
#undef lua_topointer
#undef lua_arith
#undef lua_rawequal
#undef lua_compare
#undef lua_pushnil
#undef lua_pushnumber
#undef lua_pushinteger
#undef lua_pushlstring
#undef lua_pushstring
#undef lua_pushvfstring
#undef lua_pushfstring
#undef lua_pushcclosure
#undef lua_pushboolean
#undef lua_pushlightuserdata
#undef lua_pushthread
#undef lua_getglobal
#undef lua_gettable
#undef lua_getfield
#undef lua_geti
#undef lua_rawget
#undef lua_rawgeti
#undef lua_rawgetp
#undef lua_createtable
#undef lua_newuserdata
#undef lua_getmetatable
#undef lua_getuservalue
#undef lua_setglobal
#undef lua_settable
#undef lua_setfield
#undef lua_seti
#undef lua_rawset
#undef lua_rawseti
#undef lua_rawsetp
#undef lua_setmetatable
#undef lua_setuservalue
#undef lua_callk
#undef lua_pcallk
#undef lua_load
#undef lua_dump
#undef lua_yieldk
#undef lua_resume
#undef lua_status
#undef lua_isyieldable
#undef lua_gc
#undef lua_error
#undef lua_next
#undef lua_concat
#undef lua_len
#undef lua_stringtonumber
#undef lua_getallocf
#undef lua_setallocf
#undef lua_getstack
#undef lua_getinfo
#undef lua_getlocal
#undef lua_setlocal
#undef lua_getupvalue
#undef lua_setupvalue
#undef lua_upvalueid
#undef lua_upvaluejoin
#undef lua_sethook
#undef lua_gethook
#undef lua_gethookmask
#undef lua_gethookcount
#undef luaopen_base
#undef luaopen_coroutine
#undef luaopen_table
#undef luaopen_io
#undef luaopen_os
#undef luaopen_string
#undef luaopen_utf8
#undef luaopen_bit32
#undef luaopen_math
#undef luaopen_debug
#undef luaopen_package
#undef luaL_openlibs

/* Their public prototypes, as in lua.h. */
LUA_API lua_State *(lua_newstate) (lua_Alloc f, void *ud);
LUA_API void (lua_close) (lua_State *L);
LUA_API lua_State *(lua_newthread) (lua_State *L);
LUA_API lua_CFunction (lua_atpanic) (lua_State *L, lua_CFunction panicf);
LUA_API const lua_Number *(lua_version) (lua_State *L);
LUA_API int (lua_absindex) (lua_State *L, int idx);
LUA_API int (lua_gettop) (lua_State *L);
LUA_API void (lua_settop) (lua_State *L, int idx);
LUA_API void (lua_pushvalue) (lua_State *L, int idx);
LUA_API void (lua_rotate) (lua_State *L, int idx, int n);
LUA_API void (lua_copy) (lua_State *L, int fromidx, int toidx);
LUA_API int (lua_checkstack) (lua_State *L, int n);
LUA_API void (lua_xmove) (lua_State *from, lua_State *to, int n);
LUA_API int (lua_isnumber) (lua_State *L, int idx);
LUA_API int (lua_isstring) (lua_State *L, int idx);
LUA_API int (lua_iscfunction) (lua_State *L, int idx);
LUA_API int (lua_isinteger) (lua_State *L, int idx);
LUA_API int (lua_isuserdata) (lua_State *L, int idx);
LUA_API int (lua_type) (lua_State *L, int idx);
LUA_API const char *(lua_typename) (lua_State *L, int tp);
LUA_API lua_Number (lua_tonumberx) (lua_State *L, int idx, int *isnum);
LUA_API lua_Integer (lua_tointegerx) (lua_State *L, int idx, int *isnum);
LUA_API int (lua_toboolean) (lua_State *L, int idx);
LUA_API const char *(lua_tolstring) (lua_State *L, int idx, size_t *len);
LUA_API size_t (lua_rawlen) (lua_State *L, int idx);
LUA_API lua_CFunction (lua_tocfunction) (lua_State *L, int idx);
LUA_API void *(lua_touserdata) (lua_State *L, int idx);
LUA_API lua_State *(lua_tothread) (lua_State *L, int idx);
LUA_API const void *(lua_topointer) (lua_State *L, int idx);
LUA_API void (lua_arith) (lua_State *L, int op);
LUA_API int (lua_rawequal) (lua_State *L, int idx1, int idx2);
LUA_API int (lua_compare) (lua_State *L, int idx1, int idx2, int op);
LUA_API void (lua_pushnil) (lua_State *L);
LUA_API void (lua_pushnumber) (lua_State *L, lua_Number n);
LUA_API void (lua_pushinteger) (lua_State *L, lua_Integer n);
LUA_API const char *(lua_pushlstring) (lua_State *L, const char *s, size_t len);
LUA_API const char *(lua_pushstring) (lua_State *L, const char *s);
LUA_API const char *(lua_pushvfstring) (lua_State *L, const char *fmt, va_list argp);
LUA_API const char *(lua_pushfstring) (lua_State *L, const char *fmt, ...);
LUA_API void (lua_pushcclosure) (lua_State *L, lua_CFunction fn, int n);
LUA_API void (lua_pushboolean) (lua_State *L, int b);
LUA_API void (lua_pushlightuserdata) (lua_State *L, void *p);
LUA_API int (lua_pushthread) (lua_State *L);
LUA_API int (lua_getglobal) (lua_State *L, const char *name);
LUA_API int (lua_gettable) (lua_State *L, int idx);
LUA_API int (lua_getfield) (lua_State *L, int idx, const char *k);
LUA_API int (lua_geti) (lua_State *L, int idx, lua_Integer n);
LUA_API int (lua_rawget) (lua_State *L, int idx);
LUA_API int (lua_rawgeti) (lua_State *L, int idx, lua_Integer n);
LUA_API int (lua_rawgetp) (lua_State *L, int idx, const void *p);
LUA_API void (lua_createtable) (lua_State *L, int narr, int nrec);
LUA_API void *(lua_newuserdata) (lua_State *L, size_t sz);
LUA_API int (lua_getmetatable) (lua_State *L, int objindex);
LUA_API int (lua_getuservalue) (lua_State *L, int idx);
LUA_API void (lua_setglobal) (lua_State *L, const char *name);
LUA_API void (lua_settable) (lua_State *L, int idx);
LUA_API void (lua_setfield) (lua_State *L, int idx, const char *k);
LUA_API void (lua_seti) (lua_State *L, int idx, lua_Integer n);
LUA_API void (lua_rawset) (lua_State *L, int idx);
LUA_API void (lua_rawseti) (lua_State *L, int idx, lua_Integer n);
LUA_API void (lua_rawsetp) (lua_State *L, int idx, const void *p);
LUA_API int (lua_setmetatable) (lua_State *L, int objindex);
LUA_API void (lua_setuservalue) (lua_State *L, int idx);
LUA_API void (lua_callk) (lua_State *L, int nargs, int nresults, lua_KContext ctx, lua_KFunction k);
LUA_API int (lua_pcallk) (lua_State *L, int nargs, int nresults, int errfunc, lua_KContext ctx, lua_KFunction k);
LUA_API int (lua_load) (lua_State *L, lua_Reader reader, void *dt, const char *chunkname, const char *mode);
LUA_API int (lua_dump) (lua_State *L, lua_Writer writer, void *data, int strip);
LUA_API int (lua_yieldk) (lua_State *L, int nresults, lua_KContext ctx, lua_KFunction k);
LUA_API int (lua_resume) (lua_State *L, lua_State *from, int narg);
LUA_API int (lua_status) (lua_State *L);
LUA_API int (lua_isyieldable) (lua_State *L);
LUA_API int (lua_gc) (lua_State *L, int what, int data);
LUA_API int (lua_error) (lua_State *L);
LUA_API int (lua_next) (lua_State *L, int idx);
LUA_API void (lua_concat) (lua_State *L, int n);
LUA_API void (lua_len) (lua_State *L, int idx);
LUA_API size_t (lua_stringtonumber) (lua_State *L, const char *s);
LUA_API lua_Alloc (lua_getallocf) (lua_State *L, void **ud);
LUA_API void (lua_setallocf) (lua_State *L, lua_Alloc f, void *ud);
LUA_API int (lua_getstack) (lua_State *L, int level, lua_Debug *ar);
LUA_API int (lua_getinfo) (lua_State *L, const char *what, lua_Debug *ar);
LUA_API const char *(lua_getlocal) (lua_State *L, const lua_Debug *ar, int n);
LUA_API const char *(lua_setlocal) (lua_State *L, const lua_Debug *ar, int n);
LUA_API const char *(lua_getupvalue) (lua_State *L, int funcindex, int n);
LUA_API const char *(lua_setupvalue) (lua_State *L, int funcindex, int n);
LUA_API void *(lua_upvalueid) (lua_State *L, int fidx, int n);
LUA_API void (lua_upvaluejoin) (lua_State *L, int fidx1, int n1, int fidx2, int n2);
LUA_API void (lua_sethook) (lua_State *L, lua_Hook func, int mask, int count);
LUA_API lua_Hook (lua_gethook) (lua_State *L);
LUA_API int (lua_gethookmask) (lua_State *L);
LUA_API int (lua_gethookcount) (lua_State *L);

/* LuaVmShared.h mirrors the 5.3 header; keep them equal. */
_Static_assert(N53_REGISTRYINDEX == LUA_REGISTRYINDEX, "registry index");
_Static_assert(N53_RIDX_MAINTHREAD == LUA_RIDX_MAINTHREAD && N53_RIDX_GLOBALS == LUA_RIDX_GLOBALS, "registry slots");
_Static_assert(N53_ERRERR == LUA_ERRERR && N53_GCISRUNNING == LUA_GCISRUNNING, "codes");
_Static_assert(N53_OPEQ == LUA_OPEQ && N53_OPLT == LUA_OPLT && N53_OPLE == LUA_OPLE, "compare ops");
_Static_assert(N53_OPADD == LUA_OPADD && N53_OPIDIV == LUA_OPIDIV && N53_OPBAND == LUA_OPBAND && N53_OPBOR == LUA_OPBOR &&
               N53_OPBXOR == LUA_OPBXOR && N53_OPSHL == LUA_OPSHL && N53_OPSHR == LUA_OPSHR && N53_OPUNM == LUA_OPUNM &&
               N53_OPBNOT == LUA_OPBNOT, "arith ops");
_Static_assert(N53_IDSIZE == LUA_IDSIZE, "id size");
_Static_assert(sizeof(N53Debug) == sizeof(lua_Debug) && offsetof(N53Debug, short_src) == offsetof(lua_Debug, short_src) &&
               offsetof(N53Debug, i_ci) == offsetof(lua_Debug, i_ci) && offsetof(N53Debug, nups) == offsetof(lua_Debug, nups),
               "lua_Debug layout");
_Static_assert(sizeof(lua_Integer) == sizeof(long long) && sizeof(lua_Number) == sizeof(double), "number types");
_Static_assert(LUA_EXTRASPACE == sizeof(void *), "extraspace holds the VM tag");

void n51_close(lua_State *L);
lua_State * n51_newthread(lua_State *L);
lua_CFunction n51_atpanic(lua_State *L, lua_CFunction panicf);
const lua_Number * n51_version(lua_State *L);
int n51_absindex(lua_State *L, int idx);
int n51_gettop(lua_State *L);
void n51_settop(lua_State *L, int idx);
void n51_pushvalue(lua_State *L, int idx);
void n51_rotate(lua_State *L, int idx, int n);
void n51_copy(lua_State *L, int fromidx, int toidx);
int n51_checkstack(lua_State *L, int n);
void n51_xmove(lua_State *from, lua_State *to, int n);
int n51_isnumber(lua_State *L, int idx);
int n51_isstring(lua_State *L, int idx);
int n51_iscfunction(lua_State *L, int idx);
int n51_isinteger(lua_State *L, int idx);
int n51_isuserdata(lua_State *L, int idx);
int n51_type(lua_State *L, int idx);
const char * n51_typename(lua_State *L, int tp);
lua_Number n51_tonumberx(lua_State *L, int idx, int *isnum);
lua_Integer n51_tointegerx(lua_State *L, int idx, int *isnum);
int n51_toboolean(lua_State *L, int idx);
const char * n51_tolstring(lua_State *L, int idx, size_t *len);
size_t n51_rawlen(lua_State *L, int idx);
lua_CFunction n51_tocfunction(lua_State *L, int idx);
void * n51_touserdata(lua_State *L, int idx);
lua_State * n51_tothread(lua_State *L, int idx);
const void * n51_topointer(lua_State *L, int idx);
void n51_arith(lua_State *L, int op);
int n51_rawequal(lua_State *L, int idx1, int idx2);
int n51_compare(lua_State *L, int idx1, int idx2, int op);
void n51_pushnil(lua_State *L);
void n51_pushnumber(lua_State *L, lua_Number n);
void n51_pushinteger(lua_State *L, lua_Integer n);
const char * n51_pushlstring(lua_State *L, const char *s, size_t len);
const char * n51_pushstring(lua_State *L, const char *s);
const char * n51_pushvfstring(lua_State *L, const char *fmt, va_list argp);
void n51_pushcclosure(lua_State *L, lua_CFunction fn, int n);
void n51_pushboolean(lua_State *L, int b);
void n51_pushlightuserdata(lua_State *L, void *p);
int n51_pushthread(lua_State *L);
int n51_getglobal(lua_State *L, const char *name);
int n51_gettable(lua_State *L, int idx);
int n51_getfield(lua_State *L, int idx, const char *k);
int n51_geti(lua_State *L, int idx, lua_Integer n);
int n51_rawget(lua_State *L, int idx);
int n51_rawgeti(lua_State *L, int idx, lua_Integer n);
int n51_rawgetp(lua_State *L, int idx, const void *p);
void n51_createtable(lua_State *L, int narr, int nrec);
void * n51_newuserdata(lua_State *L, size_t sz);
int n51_getmetatable(lua_State *L, int objindex);
int n51_getuservalue(lua_State *L, int idx);
void n51_setglobal(lua_State *L, const char *name);
void n51_settable(lua_State *L, int idx);
void n51_setfield(lua_State *L, int idx, const char *k);
void n51_seti(lua_State *L, int idx, lua_Integer n);
void n51_rawset(lua_State *L, int idx);
void n51_rawseti(lua_State *L, int idx, lua_Integer n);
void n51_rawsetp(lua_State *L, int idx, const void *p);
int n51_setmetatable(lua_State *L, int objindex);
void n51_setuservalue(lua_State *L, int idx);
int n51_load(lua_State *L, lua_Reader reader, void *dt, const char *chunkname, const char *mode);
int n51_dump(lua_State *L, lua_Writer writer, void *data, int strip);
int n51_resume(lua_State *L, lua_State *from, int narg);
int n51_status(lua_State *L);
int n51_isyieldable(lua_State *L);
int n51_gc(lua_State *L, int what, int data);
int n51_error(lua_State *L);
int n51_next(lua_State *L, int idx);
void n51_concat(lua_State *L, int n);
void n51_len(lua_State *L, int idx);
size_t n51_stringtonumber(lua_State *L, const char *s);
lua_Alloc n51_getallocf(lua_State *L, void **ud);
void n51_setallocf(lua_State *L, lua_Alloc f, void *ud);
int n51_getstack(lua_State *L, int level, N53Debug *ar);
int n51_getinfo(lua_State *L, const char *what, N53Debug *ar);
const char * n51_getlocal(lua_State *L, const N53Debug *ar, int n);
const char * n51_setlocal(lua_State *L, const N53Debug *ar, int n);
const char * n51_getupvalue(lua_State *L, int funcindex, int n);
const char * n51_setupvalue(lua_State *L, int funcindex, int n);
void * n51_upvalueid(lua_State *L, int fidx, int n);
void n51_upvaluejoin(lua_State *L, int fidx1, int n1, int fidx2, int n2);
int n51_gethookmask(lua_State *L);
int n51_gethookcount(lua_State *L);
lua_State *n51_newstate(lua_Alloc f, void *ud, void *tag);
void n51_call(lua_State *L, int nargs, int nresults);
int n51_pcall(lua_State *L, int nargs, int nresults, int errfunc);
int n51_yield(lua_State *L, int nresults);
void n51_sethook(lua_State *L, N53Hook func, int mask, int count);
N53Hook n51_gethook(lua_State *L);
int n51_luaopen(lua_State *L, const char *name);
void n51_openlibs(lua_State *L);
int n51_setfenv(lua_State *L, int idx);
void n51_getfenv(lua_State *L, int idx);

static char g_tag53, g_tag51;

/* lua_version(NULL) (luaL_checkversion) passes NULL. */
static int Is51(lua_State *L) { return L && *(void **)lua_getextraspace(L) == &g_tag51; }

int neon_lua_is51(lua_State *L) { return L && Is51(L); }

static void *Alloc(void *ud, void *ptr, size_t osize, size_t nsize) {
    (void)ud; (void)osize;
    if (nsize == 0) { free(ptr); return NULL; }
    return realloc(ptr, nsize);
}

static int Panic51(lua_State *L) {
    const char *msg = lua_tostring(L, -1);
    fprintf(stderr, "PANIC: unprotected error in call to Lua 5.1 API (%s)\n", msg ? msg : "error object is not a string");
    return 0;
}

lua_State *neon_lua_newstate51(void) {
    lua_State *L = n51_newstate(Alloc, NULL, &g_tag51);
    if (L) lua_atpanic(L, Panic51);
    return L;
}

int neon_lua51_setfenv(lua_State *L, int idx) { return Is51(L) ? n51_setfenv(L, idx) : 0; }
void neon_lua51_getfenv(lua_State *L, int idx) { if (Is51(L)) n51_getfenv(L, idx); else lua_pushnil(L); }

/* ---- generated forwarders ---- */

LUA_API void (lua_close) (lua_State *L) {
    if (Is51(L)) { n51_close(L); return; }
    lua53_lua_close(L);
}

LUA_API lua_State * (lua_newthread) (lua_State *L) {
    if (Is51(L)) { return n51_newthread(L); }
    return lua53_lua_newthread(L);
}

LUA_API lua_CFunction (lua_atpanic) (lua_State *L, lua_CFunction panicf) {
    if (Is51(L)) { return n51_atpanic(L, panicf); }
    return lua53_lua_atpanic(L, panicf);
}

LUA_API const lua_Number * (lua_version) (lua_State *L) {
    /* luaL_checkversion compares a state's version pointer with
     * lua_version(NULL); the API seen on a 5.1 state is 5.3's, so it gets
     * the same one. */
    return lua53_lua_version(Is51(L) ? NULL : L);
}

LUA_API int (lua_absindex) (lua_State *L, int idx) {
    if (Is51(L)) { return n51_absindex(L, idx); }
    return lua53_lua_absindex(L, idx);
}

LUA_API int (lua_gettop) (lua_State *L) {
    if (Is51(L)) { return n51_gettop(L); }
    return lua53_lua_gettop(L);
}

LUA_API void (lua_settop) (lua_State *L, int idx) {
    if (Is51(L)) { n51_settop(L, idx); return; }
    lua53_lua_settop(L, idx);
}

LUA_API void (lua_pushvalue) (lua_State *L, int idx) {
    if (Is51(L)) { n51_pushvalue(L, idx); return; }
    lua53_lua_pushvalue(L, idx);
}

LUA_API void (lua_rotate) (lua_State *L, int idx, int n) {
    if (Is51(L)) { n51_rotate(L, idx, n); return; }
    lua53_lua_rotate(L, idx, n);
}

LUA_API void (lua_copy) (lua_State *L, int fromidx, int toidx) {
    if (Is51(L)) { n51_copy(L, fromidx, toidx); return; }
    lua53_lua_copy(L, fromidx, toidx);
}

LUA_API int (lua_checkstack) (lua_State *L, int n) {
    if (Is51(L)) { return n51_checkstack(L, n); }
    return lua53_lua_checkstack(L, n);
}

LUA_API int (lua_isnumber) (lua_State *L, int idx) {
    if (Is51(L)) { return n51_isnumber(L, idx); }
    return lua53_lua_isnumber(L, idx);
}

LUA_API int (lua_isstring) (lua_State *L, int idx) {
    if (Is51(L)) { return n51_isstring(L, idx); }
    return lua53_lua_isstring(L, idx);
}

LUA_API int (lua_iscfunction) (lua_State *L, int idx) {
    if (Is51(L)) { return n51_iscfunction(L, idx); }
    return lua53_lua_iscfunction(L, idx);
}

LUA_API int (lua_isinteger) (lua_State *L, int idx) {
    if (Is51(L)) { return n51_isinteger(L, idx); }
    return lua53_lua_isinteger(L, idx);
}

LUA_API int (lua_isuserdata) (lua_State *L, int idx) {
    if (Is51(L)) { return n51_isuserdata(L, idx); }
    return lua53_lua_isuserdata(L, idx);
}

LUA_API int (lua_type) (lua_State *L, int idx) {
    if (Is51(L)) { return n51_type(L, idx); }
    return lua53_lua_type(L, idx);
}

LUA_API const char * (lua_typename) (lua_State *L, int tp) {
    if (Is51(L)) { return n51_typename(L, tp); }
    return lua53_lua_typename(L, tp);
}

LUA_API lua_Number (lua_tonumberx) (lua_State *L, int idx, int *isnum) {
    if (Is51(L)) { return n51_tonumberx(L, idx, isnum); }
    return lua53_lua_tonumberx(L, idx, isnum);
}

LUA_API lua_Integer (lua_tointegerx) (lua_State *L, int idx, int *isnum) {
    if (Is51(L)) { return n51_tointegerx(L, idx, isnum); }
    return lua53_lua_tointegerx(L, idx, isnum);
}

LUA_API int (lua_toboolean) (lua_State *L, int idx) {
    if (Is51(L)) { return n51_toboolean(L, idx); }
    return lua53_lua_toboolean(L, idx);
}

LUA_API const char * (lua_tolstring) (lua_State *L, int idx, size_t *len) {
    if (Is51(L)) { return n51_tolstring(L, idx, len); }
    return lua53_lua_tolstring(L, idx, len);
}

LUA_API size_t (lua_rawlen) (lua_State *L, int idx) {
    if (Is51(L)) { return n51_rawlen(L, idx); }
    return lua53_lua_rawlen(L, idx);
}

LUA_API lua_CFunction (lua_tocfunction) (lua_State *L, int idx) {
    if (Is51(L)) { return n51_tocfunction(L, idx); }
    return lua53_lua_tocfunction(L, idx);
}

LUA_API void * (lua_touserdata) (lua_State *L, int idx) {
    if (Is51(L)) { return n51_touserdata(L, idx); }
    return lua53_lua_touserdata(L, idx);
}

LUA_API lua_State * (lua_tothread) (lua_State *L, int idx) {
    if (Is51(L)) { return n51_tothread(L, idx); }
    return lua53_lua_tothread(L, idx);
}

LUA_API const void * (lua_topointer) (lua_State *L, int idx) {
    if (Is51(L)) { return n51_topointer(L, idx); }
    return lua53_lua_topointer(L, idx);
}

LUA_API void (lua_arith) (lua_State *L, int op) {
    if (Is51(L)) { n51_arith(L, op); return; }
    lua53_lua_arith(L, op);
}

LUA_API int (lua_rawequal) (lua_State *L, int idx1, int idx2) {
    if (Is51(L)) { return n51_rawequal(L, idx1, idx2); }
    return lua53_lua_rawequal(L, idx1, idx2);
}

LUA_API int (lua_compare) (lua_State *L, int idx1, int idx2, int op) {
    if (Is51(L)) { return n51_compare(L, idx1, idx2, op); }
    return lua53_lua_compare(L, idx1, idx2, op);
}

LUA_API void (lua_pushnil) (lua_State *L) {
    if (Is51(L)) { n51_pushnil(L); return; }
    lua53_lua_pushnil(L);
}

LUA_API void (lua_pushnumber) (lua_State *L, lua_Number n) {
    if (Is51(L)) { n51_pushnumber(L, n); return; }
    lua53_lua_pushnumber(L, n);
}

LUA_API void (lua_pushinteger) (lua_State *L, lua_Integer n) {
    if (Is51(L)) { n51_pushinteger(L, n); return; }
    lua53_lua_pushinteger(L, n);
}

LUA_API const char * (lua_pushlstring) (lua_State *L, const char *s, size_t len) {
    if (Is51(L)) { return n51_pushlstring(L, s, len); }
    return lua53_lua_pushlstring(L, s, len);
}

LUA_API const char * (lua_pushstring) (lua_State *L, const char *s) {
    if (Is51(L)) { return n51_pushstring(L, s); }
    return lua53_lua_pushstring(L, s);
}

LUA_API void (lua_pushcclosure) (lua_State *L, lua_CFunction fn, int n) {
    if (Is51(L)) { n51_pushcclosure(L, fn, n); return; }
    lua53_lua_pushcclosure(L, fn, n);
}

LUA_API void (lua_pushboolean) (lua_State *L, int b) {
    if (Is51(L)) { n51_pushboolean(L, b); return; }
    lua53_lua_pushboolean(L, b);
}

LUA_API void (lua_pushlightuserdata) (lua_State *L, void *p) {
    if (Is51(L)) { n51_pushlightuserdata(L, p); return; }
    lua53_lua_pushlightuserdata(L, p);
}

LUA_API int (lua_pushthread) (lua_State *L) {
    if (Is51(L)) { return n51_pushthread(L); }
    return lua53_lua_pushthread(L);
}

LUA_API int (lua_getglobal) (lua_State *L, const char *name) {
    if (Is51(L)) { return n51_getglobal(L, name); }
    return lua53_lua_getglobal(L, name);
}

LUA_API int (lua_gettable) (lua_State *L, int idx) {
    if (Is51(L)) { return n51_gettable(L, idx); }
    return lua53_lua_gettable(L, idx);
}

LUA_API int (lua_getfield) (lua_State *L, int idx, const char *k) {
    if (Is51(L)) { return n51_getfield(L, idx, k); }
    return lua53_lua_getfield(L, idx, k);
}

LUA_API int (lua_geti) (lua_State *L, int idx, lua_Integer n) {
    if (Is51(L)) { return n51_geti(L, idx, n); }
    return lua53_lua_geti(L, idx, n);
}

LUA_API int (lua_rawget) (lua_State *L, int idx) {
    if (Is51(L)) { return n51_rawget(L, idx); }
    return lua53_lua_rawget(L, idx);
}

LUA_API int (lua_rawgeti) (lua_State *L, int idx, lua_Integer n) {
    if (Is51(L)) { return n51_rawgeti(L, idx, n); }
    return lua53_lua_rawgeti(L, idx, n);
}

LUA_API int (lua_rawgetp) (lua_State *L, int idx, const void *p) {
    if (Is51(L)) { return n51_rawgetp(L, idx, p); }
    return lua53_lua_rawgetp(L, idx, p);
}

LUA_API void (lua_createtable) (lua_State *L, int narr, int nrec) {
    if (Is51(L)) { n51_createtable(L, narr, nrec); return; }
    lua53_lua_createtable(L, narr, nrec);
}

LUA_API void * (lua_newuserdata) (lua_State *L, size_t sz) {
    if (Is51(L)) { return n51_newuserdata(L, sz); }
    return lua53_lua_newuserdata(L, sz);
}

LUA_API int (lua_getmetatable) (lua_State *L, int objindex) {
    if (Is51(L)) { return n51_getmetatable(L, objindex); }
    return lua53_lua_getmetatable(L, objindex);
}

LUA_API int (lua_getuservalue) (lua_State *L, int idx) {
    if (Is51(L)) { return n51_getuservalue(L, idx); }
    return lua53_lua_getuservalue(L, idx);
}

LUA_API void (lua_setglobal) (lua_State *L, const char *name) {
    if (Is51(L)) { n51_setglobal(L, name); return; }
    lua53_lua_setglobal(L, name);
}

LUA_API void (lua_settable) (lua_State *L, int idx) {
    if (Is51(L)) { n51_settable(L, idx); return; }
    lua53_lua_settable(L, idx);
}

LUA_API void (lua_setfield) (lua_State *L, int idx, const char *k) {
    if (Is51(L)) { n51_setfield(L, idx, k); return; }
    lua53_lua_setfield(L, idx, k);
}

LUA_API void (lua_seti) (lua_State *L, int idx, lua_Integer n) {
    if (Is51(L)) { n51_seti(L, idx, n); return; }
    lua53_lua_seti(L, idx, n);
}

LUA_API void (lua_rawset) (lua_State *L, int idx) {
    if (Is51(L)) { n51_rawset(L, idx); return; }
    lua53_lua_rawset(L, idx);
}

LUA_API void (lua_rawseti) (lua_State *L, int idx, lua_Integer n) {
    if (Is51(L)) { n51_rawseti(L, idx, n); return; }
    lua53_lua_rawseti(L, idx, n);
}

LUA_API void (lua_rawsetp) (lua_State *L, int idx, const void *p) {
    if (Is51(L)) { n51_rawsetp(L, idx, p); return; }
    lua53_lua_rawsetp(L, idx, p);
}

LUA_API int (lua_setmetatable) (lua_State *L, int objindex) {
    if (Is51(L)) { return n51_setmetatable(L, objindex); }
    return lua53_lua_setmetatable(L, objindex);
}

LUA_API void (lua_setuservalue) (lua_State *L, int idx) {
    if (Is51(L)) { n51_setuservalue(L, idx); return; }
    lua53_lua_setuservalue(L, idx);
}

LUA_API int (lua_load) (lua_State *L, lua_Reader reader, void *dt, const char *chunkname, const char *mode) {
    if (Is51(L)) { return n51_load(L, reader, dt, chunkname, mode); }
    return lua53_lua_load(L, reader, dt, chunkname, mode);
}

LUA_API int (lua_dump) (lua_State *L, lua_Writer writer, void *data, int strip) {
    if (Is51(L)) { return n51_dump(L, writer, data, strip); }
    return lua53_lua_dump(L, writer, data, strip);
}

LUA_API int (lua_resume) (lua_State *L, lua_State *from, int narg) {
    if (Is51(L)) { return n51_resume(L, from, narg); }
    return lua53_lua_resume(L, from, narg);
}

LUA_API int (lua_status) (lua_State *L) {
    if (Is51(L)) { return n51_status(L); }
    return lua53_lua_status(L);
}

LUA_API int (lua_isyieldable) (lua_State *L) {
    if (Is51(L)) { return n51_isyieldable(L); }
    return lua53_lua_isyieldable(L);
}

LUA_API int (lua_gc) (lua_State *L, int what, int data) {
    if (Is51(L)) { return n51_gc(L, what, data); }
    return lua53_lua_gc(L, what, data);
}

LUA_API int (lua_error) (lua_State *L) {
    if (Is51(L)) { return n51_error(L); }
    return lua53_lua_error(L);
}

LUA_API int (lua_next) (lua_State *L, int idx) {
    if (Is51(L)) { return n51_next(L, idx); }
    return lua53_lua_next(L, idx);
}

LUA_API void (lua_concat) (lua_State *L, int n) {
    if (Is51(L)) { n51_concat(L, n); return; }
    lua53_lua_concat(L, n);
}

LUA_API void (lua_len) (lua_State *L, int idx) {
    if (Is51(L)) { n51_len(L, idx); return; }
    lua53_lua_len(L, idx);
}

LUA_API size_t (lua_stringtonumber) (lua_State *L, const char *s) {
    if (Is51(L)) { return n51_stringtonumber(L, s); }
    return lua53_lua_stringtonumber(L, s);
}

LUA_API lua_Alloc (lua_getallocf) (lua_State *L, void **ud) {
    if (Is51(L)) { return n51_getallocf(L, ud); }
    return lua53_lua_getallocf(L, ud);
}

LUA_API void (lua_setallocf) (lua_State *L, lua_Alloc f, void *ud) {
    if (Is51(L)) { n51_setallocf(L, f, ud); return; }
    lua53_lua_setallocf(L, f, ud);
}

LUA_API const char * (lua_getupvalue) (lua_State *L, int funcindex, int n) {
    if (Is51(L)) { return n51_getupvalue(L, funcindex, n); }
    return lua53_lua_getupvalue(L, funcindex, n);
}

LUA_API const char * (lua_setupvalue) (lua_State *L, int funcindex, int n) {
    if (Is51(L)) { return n51_setupvalue(L, funcindex, n); }
    return lua53_lua_setupvalue(L, funcindex, n);
}

LUA_API void * (lua_upvalueid) (lua_State *L, int fidx, int n) {
    if (Is51(L)) { return n51_upvalueid(L, fidx, n); }
    return lua53_lua_upvalueid(L, fidx, n);
}

LUA_API void (lua_upvaluejoin) (lua_State *L, int fidx1, int n1, int fidx2, int n2) {
    if (Is51(L)) { n51_upvaluejoin(L, fidx1, n1, fidx2, n2); return; }
    lua53_lua_upvaluejoin(L, fidx1, n1, fidx2, n2);
}

LUA_API int (lua_gethookmask) (lua_State *L) {
    if (Is51(L)) { return n51_gethookmask(L); }
    return lua53_lua_gethookmask(L);
}

LUA_API int (lua_gethookcount) (lua_State *L) {
    if (Is51(L)) { return n51_gethookcount(L); }
    return lua53_lua_gethookcount(L);
}

/* ---- hand-written forwarders ---- */

LUA_API lua_State *(lua_newstate) (lua_Alloc f, void *ud) {
    lua_State *L = lua53_lua_newstate(f, ud);
    if (L) *(void **)lua_getextraspace(L) = &g_tag53;
    return L;
}

LUA_API void (lua_xmove) (lua_State *from, lua_State *to, int n) {
    if (Is51(from)) { n51_xmove(from, to, n); return; }
    lua53_lua_xmove(from, to, n);
}

LUA_API const char *(lua_pushvfstring) (lua_State *L, const char *fmt, va_list argp) {
    if (Is51(L)) return n51_pushvfstring(L, fmt, argp);
    return lua53_lua_pushvfstring(L, fmt, argp);
}

LUA_API const char *(lua_pushfstring) (lua_State *L, const char *fmt, ...) {
    va_list argp;
    va_start(argp, fmt);
    const char *r = lua_pushvfstring(L, fmt, argp);
    va_end(argp);
    return r;
}

/* 5.1 cannot resume a C function after a yield, so continuations are dropped. */
LUA_API void (lua_callk) (lua_State *L, int nargs, int nresults, lua_KContext ctx, lua_KFunction k) {
    if (Is51(L)) { n51_call(L, nargs, nresults); return; }
    lua53_lua_callk(L, nargs, nresults, ctx, k);
}

LUA_API int (lua_pcallk) (lua_State *L, int nargs, int nresults, int errfunc, lua_KContext ctx, lua_KFunction k) {
    if (Is51(L)) return n51_pcall(L, nargs, nresults, errfunc);
    return lua53_lua_pcallk(L, nargs, nresults, errfunc, ctx, k);
}

LUA_API int (lua_yieldk) (lua_State *L, int nresults, lua_KContext ctx, lua_KFunction k) {
    if (Is51(L)) return n51_yield(L, nresults);
    return lua53_lua_yieldk(L, nresults, ctx, k);
}

/* lua_Debug: N53Debug has the same layout (checked above). */
LUA_API int (lua_getstack) (lua_State *L, int level, lua_Debug *ar) {
    if (Is51(L)) return n51_getstack(L, level, (N53Debug *)ar);
    return lua53_lua_getstack(L, level, ar);
}

LUA_API int (lua_getinfo) (lua_State *L, const char *what, lua_Debug *ar) {
    if (Is51(L)) return n51_getinfo(L, what, (N53Debug *)ar);
    return lua53_lua_getinfo(L, what, ar);
}

LUA_API const char *(lua_getlocal) (lua_State *L, const lua_Debug *ar, int n) {
    if (Is51(L)) return n51_getlocal(L, (const N53Debug *)ar, n);
    return lua53_lua_getlocal(L, ar, n);
}

LUA_API const char *(lua_setlocal) (lua_State *L, const lua_Debug *ar, int n) {
    if (Is51(L)) return n51_setlocal(L, (const N53Debug *)ar, n);
    return lua53_lua_setlocal(L, ar, n);
}

LUA_API void (lua_sethook) (lua_State *L, lua_Hook func, int mask, int count) {
    if (Is51(L)) { n51_sethook(L, (N53Hook)func, mask, count); return; }
    lua53_lua_sethook(L, func, mask, count);
}

LUA_API lua_Hook (lua_gethook) (lua_State *L) {
    if (Is51(L)) return (lua_Hook)n51_gethook(L);
    return lua53_lua_gethook(L);
}

/* ---- libraries ---- */

#define NEON_OPEN(lib) \
    LUAMOD_API int (luaopen_##lib) (lua_State *L) { \
        if (Is51(L)) return n51_luaopen(L, #lib); \
        return lua53_luaopen_##lib(L); \
    }
NEON_OPEN(base)
NEON_OPEN(coroutine)
NEON_OPEN(table)
NEON_OPEN(io)
NEON_OPEN(os)
NEON_OPEN(string)
NEON_OPEN(utf8)
NEON_OPEN(bit32)
NEON_OPEN(math)
NEON_OPEN(debug)
NEON_OPEN(package)

LUALIB_API void (luaL_openlibs) (lua_State *L) {
    if (Is51(L)) { n51_openlibs(L); return; }
    lua53_luaL_openlibs(L);
}
