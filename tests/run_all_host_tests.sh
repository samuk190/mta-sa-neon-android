#!/usr/bin/env bash
# One command for the host-side suites that need no device and no network.
# Usage: tests/run_all_host_tests.sh [suite ...]   (default: all)
# Suites: cipher offline reliability join headless abi
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

suite_abi() {
    local rc=0
    local script
    for script in verify_dx_native_abi.py verify_ped_query_native_abi.py verify_spatial_native_abi.py; do
        echo "== $script"
        python3 "$root/tools/headless/$script" || rc=1
    done
    # Run MtaDxView Android JVM regression test when an android.jar is available locally.
    local sdk="${ANDROID_HOME:-${ANDROID_SDK_ROOT:-}}"
    if [[ -z "$sdk" && -f "$root/local.properties" ]]; then
        sdk="$(grep -E '^sdk\.dir=' "$root/local.properties" | head -n1 | cut -d= -f2-)"
    fi
    local android_jar=""
    if [[ -n "$sdk" && -d "$sdk/platforms" ]]; then
        android_jar="$(ls -1 "$sdk"/platforms/android-*/android.jar 2>/dev/null | sort -V | tail -n1)"
    fi
    if [[ -n "$android_jar" && -f "$android_jar" ]]; then
        local javac_bin="${JAVA_HOME:+$JAVA_HOME/bin/javac}"
        local java_bin="${JAVA_HOME:+$JAVA_HOME/bin/java}"
        [[ -x "${javac_bin:-}" ]] || javac_bin="$(command -v javac || true)"
        [[ -x "${java_bin:-}" ]] || java_bin="$(command -v java || true)"
        if [[ -n "$javac_bin" && -n "$java_bin" ]]; then
            echo "== run_dx_view_tests.py ($android_jar)"
            python3 "$root/tools/headless/run_dx_view_tests.py" \
                --android-jar "$android_jar" \
                --build-dir "$build/dx-view-test" \
                --javac "$javac_bin" \
                --java "$java_bin" || rc=1
        fi
    fi
    return $rc
}

all=(cipher offline reliability join headless abi)
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
