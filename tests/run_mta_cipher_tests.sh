#!/usr/bin/env bash
# Execute only on Oracle ARM64.
set -euo pipefail
test_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
codec_dir="$test_dir/../app/src/main/cpp/net/mta"
build_dir="$(mktemp -d)"
trap 'if [[ -f "$build_dir/test" ]]; then unlink "$build_dir/test"; fi; rmdir "$build_dir"' EXIT
"${CXX:-c++}" -std=c++20 -Wall -Wextra -Wpedantic -Werror -O1 -g \
    -fsanitize=address,undefined -fno-omit-frame-pointer -I "$codec_dir" \
    "$test_dir/test_mta_cipher.cpp" "$codec_dir/MtaCipher.cpp" -o "$build_dir/test"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$build_dir/test"
