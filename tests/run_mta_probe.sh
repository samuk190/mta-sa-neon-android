#!/usr/bin/env bash
# Execute on the Oracle ARM64 VM, never on the local development machine.
set -euo pipefail
test_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
codec_dir="$test_dir/../app/src/main/cpp/net/mta"
build_dir="$(mktemp -d)"
trap 'if [[ -f "$build_dir/probe" ]]; then unlink "$build_dir/probe"; fi; rmdir "$build_dir"' EXIT
"${CXX:-c++}" -std=c++20 -Wall -Wextra -Wpedantic -Werror -O1 -g \
    -I "$codec_dir" "$test_dir/probe_mta_native.cpp" \
    "$codec_dir/MtaOfflineCodec.cpp" "$codec_dir/MtaUdpSocket.cpp" \
    "$codec_dir/MtaCipher.cpp" "$codec_dir/MtaReliabilityCodec.cpp" -o "$build_dir/probe"
"$build_dir/probe" "$@"
