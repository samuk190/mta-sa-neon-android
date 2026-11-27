#!/usr/bin/env bash
# One command for the host-side suites that need no device and no network.
# Usage: tests/run_all_host_tests.sh [suite ...]   (default: all)
# Suites: cipher offline reliability join headless
set -uo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
build="${NEON_HOST_TEST_BUILD_DIR:-$root/outputs/host-tests}"
jobs="${NEON_TEST_JOBS:-2}"

suite_cipher()      { bash "$root/tests/run_mta_cipher_tests.sh"; }
suite_offline()     { bash "$root/tests/run_mta_offline_tests.sh"; }
suite_reliability() { bash "$root/tests/run_mta_reliability_tests.sh"; }
suite_join()        { NEON_HOST_TEST_BUILD_DIR="$build" bash "$root/tests/run_host_tests.sh"; }
suite_headless() {
    cmake -S "$root/tools/headless" -B "$build/headless" >/dev/null || return 1
    cmake --build "$build/headless" -j "$jobs" || return 1
    local t rc=0
    for t in "$build"/headless/neon_*_tests; do
        [[ -x "$t" ]] || continue
        echo "== $(basename "$t")"
        "$t" || rc=1
    done
    # Pure Java checks exist only when cmake found a JDK.
    local help; help="$(cmake --build "$build/headless" --target help 2>/dev/null)"
    for t in neon_dx_text_tests neon_dx_state_tests neon_control_toggle_state_tests; do
        grep -q "\\b$t\\b" <<<"$help" || continue
        echo "== $t"
        cmake --build "$build/headless" --target "$t" || rc=1
    done
    return $rc
}

all=(cipher offline reliability join headless)
if [[ $# -gt 0 ]]; then suites=("$@"); else suites=("${all[@]}"); fi

failed=()
for s in "${suites[@]}"; do
    if ! declare -F "suite_$s" >/dev/null; then
        echo "unknown suite: $s (known: ${all[*]})" >&2
        exit 2
    fi
    echo "===== suite: $s"
    if "suite_$s"; then echo "----- $s: PASS"; else echo "----- $s: FAIL"; failed+=("$s"); fi
done

if [[ ${#failed[@]} -gt 0 ]]; then
    echo "FAILED: ${failed[*]}"
    exit 1
fi
echo "ALL PASSED: ${suites[*]}"
