# Device test report — Redmi Note 13 Pro 5G

## Status: NOT RUN

- `adb devices` on 2026-11-20: no device attached.
- No Android SDK/NDK installed in this environment, so no APK was built here.
- No server session was opened.

Nothing in this contribution is device-verified. The README/AGENTS rows say so explicitly.

## What to run when the phone is connected

1. Build: `./gradlew :app:assembleDebug` (do not `clean`).
2. Install: `./gradlew installDebug` or `adb install -r app/build/outputs/apk/debug/*.apk`.
3. Preserve the existing private assets before reinstalling; uninstalling deletes
   `Android/data/com.worldrpg.neonlauncher/`.
4. Join the authorized server with two clients (phone + desktop or second phone).
5. Drive an empty car with the other client and watch the phone.

## Acceptance for this contribution

- Logcat shows `DIAG veh <id> SetTargetPosition ...` and `SetTargetRotation ...` for packet 56.
- The car body on the phone moves toward the reported pose instead of staying still.
- No new `libGTASA` fault; the pose path only queues under `g_remoteMutex` and writes on the game
  thread in `ApplyRemoteCars`.
- A car that only rotates (rotation-only entry) does not jump to the origin.
- Packet 56 keeps arriving every ~400 ms without a growing DIAG flood.

## Evidence to capture

- `adb logcat -s WorldRPGNeon` around the drive, with the vehicle element id.
- A screen recording of the empty car moving.
- The `libGTASA` base and fault address if the app crashes.
