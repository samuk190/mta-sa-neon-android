#!/usr/bin/env bash
# Execute only on Oracle ARM64. Uses the real server and native transport.
set -euo pipefail
test_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cpp_dir="$test_dir/../app/src/main/cpp"
build_dir="$(mktemp -d)"
trap 'if [[ -f "$build_dir/probe" ]]; then unlink "$build_dir/probe"; fi; rmdir "$build_dir"' EXIT
"${CXX:-c++}" -std=c++20 -Wall -Wextra -Wpedantic -Werror -Wno-unused-parameter -O1 -g \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I "$cpp_dir/net" -I "$cpp_dir/net/raknet" -I "$cpp_dir" \
    "$test_dir/probe_neon_join.cpp" "$cpp_dir/net/NeonJoinProtocol.cpp" \
    "$cpp_dir/net/raknet/RakNetWrapper.cpp" "$cpp_dir/net/mta/"*.cpp -o "$build_dir/probe"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$build_dir/probe"
