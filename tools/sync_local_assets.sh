#!/usr/bin/env bash
# =============================================================================
# MTA SA NEON ANDROID - Local Game Assets Sync Tool
# =============================================================================
# Reads GAME_ASSETS_DIR from .env and pushes assets to the connected Android device.
# Also verifies that required GTA San Andreas Mobile v2.10 files are present.
# =============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd -- "$SCRIPT_DIR/.." && pwd)"

# 1. Load .env
ENV_FILE="$PROJECT_ROOT/.env"
if [[ -f "$ENV_FILE" ]]; then
    echo "[MTA] Loading configuration from .env..."
    # Read variables ignoring comments and empty lines
    while IFS='=' read -r key val || [[ -n "$key" ]]; do
        key="$(echo "$key" | xargs)"
        [[ -z "$key" || "$key" =~ ^# ]] && continue
        # Strip quotes if present
        val="$(echo "$val" | sed -e 's/^"//' -e 's/"$//' -e "s/^'//" -e "s/'$//")"
        case "$key" in
            GAME_ASSETS_DIR)   GAME_ASSETS_DIR="$val" ;;
            DEVICE_ASSETS_PATH) DEVICE_ASSETS_PATH="$val" ;;
            CDN_ROOT_URL)      CDN_ROOT_URL="$val" ;;
        esac
    done < "$ENV_FILE"
else
    echo "[MTA] .env not found. Creating from .env.example..."
    cp "$PROJECT_ROOT/.env.example" "$ENV_FILE"
fi

GAME_ASSETS_DIR="${GAME_ASSETS_DIR:-$PROJECT_ROOT/game_assets}"
DEVICE_ASSETS_PATH="${DEVICE_ASSETS_PATH:-/sdcard/Android/data/com.worldrpg.neonlauncher/files/game_assets}"

# Resolve relative path if needed
if [[ "$GAME_ASSETS_DIR" != /* ]]; then
    GAME_ASSETS_DIR="$PROJECT_ROOT/$GAME_ASSETS_DIR"
fi

echo "[MTA] Local assets folder : $GAME_ASSETS_DIR"
echo "[MTA] Target device path  : $DEVICE_ASSETS_PATH"

# 2. Check if assets folder exists
if [[ ! -d "$GAME_ASSETS_DIR" ]]; then
    echo "[!] ERROR: Local game assets directory does not exist: $GAME_ASSETS_DIR"
    echo "    Please create this directory and place your extracted GTA San Andreas Mobile 2.10 files there,"
    echo "    or edit GAME_ASSETS_DIR in .env to point to your existing GTA Mobile 2.10 folder."
    exit 1
fi

# 3. Check for essential files
echo "[MTA] Checking for required GTA San Andreas Mobile 2.10 files..."
MISSING=0
for req in "data/gta.dat" "texdb/gta3.img"; do
    if [[ ! -f "$GAME_ASSETS_DIR/$req" ]]; then
        echo "    [MISSING] $req"
        MISSING=1
    else
        echo "    [OK]      $req"
    fi
done

if [[ $MISSING -eq 1 ]]; then
    echo "[!] WARNING: Some critical GTA SA Mobile 2.10 files are missing in $GAME_ASSETS_DIR."
    echo "    Make sure you have extracted the original GTA San Andreas Mobile v2.10 OBB and data files."
fi

# 4. Check for libGTASA.so and copy to jniLibs if present
JNILIBS_DIR="$PROJECT_ROOT/app/src/main/jniLibs/arm64-v8a"
mkdir -p "$JNILIBS_DIR"

if [[ -f "$GAME_ASSETS_DIR/libGTASA.so" && ! -f "$JNILIBS_DIR/libGTASA.so" ]]; then
    echo "[MTA] Found libGTASA.so in assets folder! Copying to $JNILIBS_DIR/libGTASA.so..."
    cp "$GAME_ASSETS_DIR/libGTASA.so" "$JNILIBS_DIR/libGTASA.so"
elif [[ ! -f "$JNILIBS_DIR/libGTASA.so" ]]; then
    echo "[!] NOTICE: libGTASA.so not found in $JNILIBS_DIR."
    echo "    Remember to extract libGTASA.so from GTA Mobile 2.10 APK (lib/arm64-v8a/libGTASA.so)"
    echo "    and place it into: $JNILIBS_DIR/libGTASA.so"
fi

# 5. Push to Android device via ADB if available
if command -v adb >/dev/null 2>&1; then
    DEVICE_COUNT="$(adb devices | grep -v "List of devices" | grep "device$" | wc -l || true)"
    if [[ "$DEVICE_COUNT" -gt 0 ]]; then
        echo "[MTA] Connected Android device detected. Pushing assets to $DEVICE_ASSETS_PATH..."
        adb shell "mkdir -p '$DEVICE_ASSETS_PATH'"
        adb push "$GAME_ASSETS_DIR/." "$DEVICE_ASSETS_PATH/"
        echo "[MTA] Asset transfer complete!"
    else
        echo "[MTA] No Android device or emulator currently connected via ADB."
        echo "    To push manually later, run: adb push \"$GAME_ASSETS_DIR/.\" \"$DEVICE_ASSETS_PATH/\""
    fi
else
    echo "[MTA] ADB CLI not found in PATH."
    echo "    Once ADB is installed, you can push assets using: adb push \"$GAME_ASSETS_DIR/.\" \"$DEVICE_ASSETS_PATH/\""
fi

echo "[MTA] Done."
