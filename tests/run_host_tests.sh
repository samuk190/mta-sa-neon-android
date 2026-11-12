#!/usr/bin/env bash
# Host wire-format tests for the Neon join protocol (no device/NDK needed).
# Compiles the transport codec + join state machine against the
# production-proven bit semantics ported from backend_mta/scripts.
set -euo pipefail
cd "$(dirname "$0")"
CPP=../app/src/main/cpp
OUT=$(mktemp -d)/test_join_codec
g++ -std=c++20 -Wall -Wextra -Wno-unused-parameter \
    -I "$CPP/net" -I "$CPP/net/raknet" -I "$CPP" \
    test_join_codec.cpp "$CPP/net/raknet/RakNetWrapper.cpp" "$CPP/net/mta/"*.cpp \
    -o "$OUT"
"$OUT"
