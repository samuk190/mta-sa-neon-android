#!/usr/bin/env bash
# Lua 5.1 VM host test (app/src/main/cpp/lua51). The bytecode is dumped on
# this 64-bit host and rewritten to the 32-bit layout luac.mtasa.com uses.
set -euo pipefail
here="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cpp="$here/../../app/src/main/cpp"
out="${NEON_HOST_TEST_BUILD_DIR:-$here/../../outputs/lua51-tests}"
mkdir -p "$out"
inc51="-I$cpp/lua51"
gcc -O1 -w $inc51 -c "$cpp/lua51/lua51_all.c" -o "$out/lua51_all.o"
gcc -O1 -w $inc51 -c "$cpp/lua51/lua51_utf8.c" -o "$out/lua51_utf8.o"
# The tool needs lua_dump, which MTA (and the app) build out (WITH_STRING_DUMP).
gcc -O1 -w $inc51 -DWITH_STRING_DUMP -c "$cpp/lua51/lua51_all.c" -o "$out/lua51_all_dump.o"
gcc -O1 -w $inc51 "$here/mkbytecode.c" "$out/lua51_all_dump.o" "$out/lua51_utf8.o" -lm -o "$out/mkbytecode"
"$out/mkbytecode" "$here/sample.lua" "$out/sample64.luac"
python3 -I "$here/to32.py" "$out/sample64.luac" "$out/sample.luac"
lua53=()
for f in "$cpp"/lua53/src/*.c; do
    case "$(basename "$f")" in lua.c|luac.c) continue ;; esac
    lua53+=("$f")
done
(cd "$out" && gcc -O1 -w -DLUA_USE_POSIX -c "${lua53[@]}")
g++ -std=c++17 -O1 -I"$cpp/lua53/src" "$here/test_lua51.cpp" "$out/lua51_all.o" "$out/lua51_utf8.o" \
    $(for f in "${lua53[@]}"; do b=$(basename "$f" .c); echo "$out/$b.o"; done) -lm -o "$out/test_lua51"
"$out/test_lua51" "$here/sample.lua" "$out/sample.luac" "$out/sample64.luac"
