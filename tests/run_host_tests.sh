#!/usr/bin/env bash
# The join now shares resource/sync code with the headless target.
# Reuse its production dependencies instead of compiling an incomplete stub.
set -euo pipefail
neon_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
neon_build="${NEON_HOST_TEST_BUILD_DIR:-$neon_root/outputs/host-join-tests}"
cmake -S "$neon_root/tools/headless" -B "$neon_build"
cmake --build "$neon_build" --target neon_join_codec_tests -j "${NEON_TEST_JOBS:-2}"
"$neon_build/neon_join_codec_tests"
