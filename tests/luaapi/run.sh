#!/usr/bin/env bash
# Host test for luaapi/: the Lua 5.3 C API on 5.3 and 5.1 states.
set -euo pipefail
here="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cpp="$here/../../app/src/main/cpp"
out="${NEON_HOST_TEST_BUILD_DIR:-$here/../../outputs/luaapi-tests}"
mkdir -p "$out"
objs=()
for f in $(sed -n '/LUA_CORE_SOURCES/,/^)/p' "$cpp/lua53/CMakeLists.txt" | grep -oE 'src/[a-z0-9]+\.c'); do
    o="$out/lua53_$(basename "$f" .c).o"
    gcc -O1 -w -DLUA_USE_LINUX -include "$cpp/lua53/lua53_prefix.h" -c "$cpp/lua53/$f" -o "$o"
    objs+=("$o")
done
gcc -O1 -w -I"$cpp/lua51" -c "$cpp/lua51/lua51_all.c" -o "$out/lua51_all.o"
gcc -O1 -w -I"$cpp/lua51" -c "$cpp/lua51/lua51_utf8.c" -o "$out/lua51_utf8.o"
for f in LuaVmDispatch LuaVm51; do
    gcc -std=c11 -O1 -Wall -Wno-unused-function -c "$cpp/luaapi/$f.c" -o "$out/$f.o"
done
gcc -O1 -w -I"$cpp/lua53/src" -c "$cpp/luaapi/LuaAuxLib.c" -o "$out/LuaAuxLib.o"
gcc -O1 -Wall -I"$cpp/lua53/src" -I"$cpp/luaapi" "$here/test_luaapi.c" "$out/LuaVmDispatch.o" "$out/LuaVm51.o" \
    "$out/LuaAuxLib.o" "$out/lua51_all.o" "$out/lua51_utf8.o" "${objs[@]}" -lm -ldl -o "$out/test_luaapi"
"$out/test_luaapi"
