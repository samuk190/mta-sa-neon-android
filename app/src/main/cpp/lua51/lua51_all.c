/* MTA's Lua 5.1 (san-andreas-online vendor/lua) as one translation unit.
 * luaall_c makes every internal function static (luaconf.h LUAI_FUNC), and
 * lua51_prefix.h renames the public API to lua51_*, so nothing clashes with
 * the Lua 5.3 build linked beside it. */
#define luaall_c
/* System headers first: ldebug.h defines a getline() macro. */
#include <ctype.h>
#include <errno.h>
#include <locale.h>
#include <math.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "lua51_prefix.h"

#include "src/lapi.c"
#include "src/lcode.c"
#include "src/ldebug.c"
#include "src/ldo.c"
#include "src/ldump.c"
#include "src/lfunc.c"
#include "src/lgc.c"
#include "src/llex.c"
#include "src/lmem.c"
#include "src/lobject.c"
#include "src/lopcodes.c"
#include "src/lparser.c"
#include "src/lstate.c"
#include "src/lstring.c"
#include "src/ltable.c"
#include "src/ltm.c"
#include "src/lundump.c"
#include "src/lvm.c"
#include "src/lzio.c"

#include "src/lauxlib.c"
#include "src/lbaselib.c"
#include "src/ldblib.c"
#include "src/liolib.c"
#include "src/linit.c"
#include "src/lmathlib.c"
#include "src/loadlib.c"
#include "src/loslib.c"
#include "src/lstrlib.c"
#include "src/ltablib.c"
/* lutf8lib.c (MTA's utf8 library) is built separately: it reuses lstrlib's
 * internal names. */

/* The pointer-sized area before L (see LUAI_EXTRASPACE in luaconf.h). */
void *lua51_extraspace(lua_State *L) { return (char *)L - LUAI_EXTRASPACE; }
