# Lua C API over two VMs

The client runs scripts in two VMs:

- **Lua 5.3** (`../lua53`): resources written for the Android client.
- **Lua 5.1** (`../lua51`): resources written for desktop MTA, which runs Lua 5.1
  and loads luac.mtasa.com bytecode.

The script API (`shared/Lua*.cpp` and the other callers) is written once,
against the Lua 5.3 C API, and works on states of either VM. This directory
makes that possible.

## Pieces

| File | Role |
| --- | --- |
| `LuaVmDispatch.c` | Defines the public `lua_*` core functions, `luaopen_*` and `luaL_openlibs`. Each call goes to the VM that owns the state. |
| `LuaVm51.c` | The Lua 5.3 core API implemented on Lua 5.1 (`n51_*`). |
| `LuaAuxLib.c` | Lua 5.3's `lauxlib.c`, compiled unprefixed over the dispatcher, so every `luaL_*` function behaves as in 5.3 on both VMs. |
| `LuaVmShared.h` | The 5.3 constants and `lua_Debug` layout that `LuaVm51.c` needs without the 5.3 header. `LuaVmDispatch.c` checks them against the header at compile time. |
| `LuaVm.h` | The calls that differ: `neon_lua_newstate51`, `neon_lua_is51`, and 5.1 function environments. |

Both VMs are built with every global symbol renamed (`lua53_prefix.h` and
`lua51_prefix.h`, generated from `nm` of their objects into `symbols.txt`),
so the only public `lua_*` names in the library are the ones defined here.
Lua 5.3's own standard libraries call `lua53_*` directly.

## Which VM owns a state

Each VM keeps a pointer-sized area in front of every `lua_State` (5.3
`LUA_EXTRASPACE`, 5.1 `LUAI_EXTRASPACE`), and new threads copy it. The
dispatcher stores a tag there when it creates the state: `lua_newstate` and
`luaL_newstate` create 5.3 states, and `neon_lua_newstate51` creates 5.1 states.

## Differences bridged for 5.1 states

- **Pseudo-indices.** 5.3 has the registry index with upvalues below it. 5.1
  has separate registry, environment and globals indices.
- **Registry slots.** A new 5.1 state gets 5.3's fixed registry slots:
  `[1]` is the main thread and `[2]` is the globals table
  (`LUA_RIDX_MAINTHREAD`, `LUA_RIDX_GLOBALS`). So `lua_pushglobaltable` works,
  and `luaL_ref` starts at 3, as in 5.3.
- **Integers.** 5.1 numbers are doubles. An integral double is an integer
  (`lua_isinteger`, `lua_tointegerx`), as desktop MTA scripts see numbers.
  64-bit values are pushed as doubles.
- **Getter results.** Getters (`lua_getfield`, `lua_rawgeti` and the others)
  return the type of the pushed value.
- **Status codes.** 5.1's `LUA_ERRERR` (5) becomes 5.3's 6.
- **`lua_pushfstring`.** It understands 5.3's `%I` and `%U`.
- **`lua_load` modes.** The `"t"` and `"b"` modes are checked against the
  chunk's first byte, with 5.3's error message.
- **`lua_Debug` and hooks.** Records are converted between the two layouts,
  and the 5.1 call-info index is carried in `i_ci`. The 5.3 hook is kept in the
  registry and called by a 5.1 hook.
- **Operators.** `lua_compare` with `LUA_OPLE`, and `lua_arith`, run as small
  Lua functions, so metamethods apply. The bitwise operators need values with
  an exact integer form.
- **Missing in 5.1.**
  - Continuations (`lua_callk`, `lua_pcallk`, `lua_yieldk`) are dropped.
  - `lua_upvalueid` returns NULL, and `lua_upvaluejoin` raises an error.
  - `lua_getlocal` with a NULL record returns NULL.
  - `luaopen_bit32` raises an error.
  - `lua_version` reports 503, because the API seen through it is 5.3's.

## What stays VM-specific

The language and its standard library are not translated. For example, 5.1
states have `unpack`, `setfenv` and no `//` operator. A chunk's environment
is also set differently:

- **5.3:** set the first upvalue (`_ENV`) with `lua_setupvalue`.
- **5.1:** use `neon_lua51_setfenv`.

Callers that set environments branch on `neon_lua_is51`.

## Tests

- `tests/luaapi/run.sh`: the same 5.3 API calls on a 5.3 state and a 5.1 state.
- `tests/lua51/run.sh`: the 5.1 VM itself.
