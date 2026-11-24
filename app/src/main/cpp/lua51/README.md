# Lua 5.1 (desktop MTA scripts)

Desktop MTA runs client scripts in Lua 5.1. This VM runs resources written
for it beside the Lua 5.3 VM (`../lua53`) that mobile resources use.

## Sources

- `src/` (except `lutf8lib.c`, `unidata.h`): Lua 5.1.5 from
  https://www.lua.org/ftp/lua-5.1.5.tar.gz
  (SHA-256 `2640fc56a795f29d28ef15e13c34a47e223960b0240e8cb0a82d9b0738695333`).
  MIT license, `COPYRIGHT.lua`.
- `src/lutf8lib.c`, `src/unidata.h`: luautf8 0.1.1 by starwing
  (https://github.com/starwing/luautf8, tag 0.1.1). Same license as Lua.
  Desktop MTA ships an older copy of this library as its `utf8` table.

Upstream Lua is used rather than MTA's `vendor/lua`, whose changes are
part of a GPL-3.0 project; this project is GPL-2.0-only.

## Changes from upstream (marked `NEON` in the code)

1. `luaconf.h`: `LUAI_EXTRASPACE` is one pointer, copied into new threads,
   like Lua 5.3's `LUA_EXTRASPACE`. The C API dispatcher stores a VM tag
   there (`lua51_extraspace`).
2. `lundump.c`: string lengths in a chunk are 4 bytes and the header's
   `size_t` byte must be 4, as in MTA's `lundump.c`
   (`SIZE_T_PRECOMPILED_CHUNK`). That is the layout luac.mtasa.com writes
   for 32-bit MTA; the native 64-bit layout is not accepted, also as in MTA.
3. `lapi.c`: `lua_dump` returns 1 unless `WITH_STRING_DUMP` is defined, as
   in MTA, so `string.dump` fails for scripts. Only host tools define it.
4. `lutf8lib.c`: registers the global `utf8` table without `codepoint`,
   `codes`, `offset` or `charpattern`, which desktop MTA's copy lacks.

## Build

`lua51_all.c` includes every core and library file with `luaall_c`, so
internal functions are `static`. `lua51_utf8.c` builds `lutf8lib.c` apart,
because it reuses names from `lstrlib.c`. `lua51_prefix.h` renames the
remaining global symbols listed in `symbols.txt`. To regenerate it, build
both files with an empty `lua51_prefix.h` and list the defined globals:
`nm -g --defined-only lua51_all.o lua51_utf8.o`.

Both files build without `_FORTIFY_SOURCE`. Lua 5.1 reads a string's bytes
as `(TString*)ts + 1` (`getstr`), which bionic's FORTIFY takes for a read
past the end of the header: the first `strchr` on a Lua string aborts the
process on Android.

Host test: `tests/lua51/run.sh`.
