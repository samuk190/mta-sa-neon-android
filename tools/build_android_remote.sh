#!/usr/bin/env bash
set -euo pipefail
project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
oracle_host="${ORACLE_HOST:-ubuntu@146.235.24.222}"
ssh_key="${ORACLE_SSH_KEY:-$HOME/.ssh/oracle_worldrpg_rsa}"
remote_dir=worldrpg-neon-android-build
if [[ ! -f "$ssh_key" ]]; then
    echo 'Set ORACLE_SSH_KEY to the Oracle SSH key.' >&2
    exit 1
fi
printf -v rsync_ssh 'ssh -o IPQoS=none -o BatchMode=yes -o ConnectTimeout=10 -o ServerAliveInterval=10 -o ServerAliveCountMax=3 -i %q' "$ssh_key"
# Keep this workspace separate from the earlier Android project and the MTA server.
rsync -a --timeout=45 --delete --exclude='.git' --exclude='.gradle' --exclude='.cxx' \
    --exclude='app/build' --exclude='outputs' --exclude='local.properties' \
    --exclude='*.apk' --exclude='*.idsig' --exclude='*.jks' --exclude='*.keystore' \
    --exclude='libGTASA_v210.so' --exclude='nul' \
    -e "$rsync_ssh" "$project_dir/" "$oracle_host:$remote_dir/workspace/"
ssh -o IPQoS=none -o BatchMode=yes -o ConnectTimeout=10 -o ServerAliveInterval=10 -o ServerAliveCountMax=3 -i "$ssh_key" "$oracle_host" "set -e
    root=\"\$HOME/$remote_dir\"
    sdk=\"\$HOME/worldrpg-android-build/android-sdk\"
    test -d \"\$sdk/ndk/25.1.8937393\"
    mkdir -p \"\$root/gradle-cache\" \"\$root/debug-signing\"
    printf 'sdk.dir=/android-sdk\\n' > \"\$root/workspace/local.properties\"
    docker run --rm --name worldrpg-neon-android-build --platform linux/arm64 \\
        --memory=6g --cpus=2 \\
        -v \"\$root/workspace:/workspace\" \\
        -v \"\$sdk:/android-sdk\" \\
        -v \"\$root/gradle-cache:/gradle-cache\" \\
        -v \"\$root/debug-signing:/root/.android\" \\
        worldrpg-android-builder:17-native bash ./gradlew --no-daemon --max-workers=2 :app:assembleDebug
"
mkdir -p "$project_dir/outputs"
candidate="$(mktemp "$project_dir/outputs/.worldrpg-neon-debug.XXXXXX.apk")"
trap 'unlink "$candidate" 2>/dev/null || true' EXIT
scp -q -o IPQoS=none -o ConnectTimeout=10 -o BatchMode=yes -i "$ssh_key" \
    "$oracle_host:$remote_dir/workspace/app/build/outputs/apk/debug/app-debug.apk" "$candidate"
python3 "$project_dir/tools/audit_android_apk.py" "$candidate" >/dev/null
mv -- "$candidate" "$project_dir/outputs/worldrpg-neon-debug.apk"
python3 "$project_dir/tools/audit_android_apk.py" "$project_dir/outputs/worldrpg-neon-debug.apk"
