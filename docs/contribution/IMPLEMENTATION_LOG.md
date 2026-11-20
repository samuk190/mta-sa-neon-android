# Implementation log

## 2026-11-20 — S5: UNOCCUPIED_VEHICLE_SYNC pose reaches the remote car

### Problem

Packet 56 was decoded correctly (`VehicleSync::ReadUnoccupiedEntry`) and then applied to a private
table in `gtasa/RemoteVehicle.cpp` that nothing on the game thread reads. `SetTargetPosition` and
`SetTargetRotation` printed a `DIAG` line and returned, so an unoccupied car the server moved stayed
at its spawn pose. The engine path for remote cars already existed (`Game::NoteRemoteVehicle` →
`g_remoteCars` → `ApplyRemoteCars`), and packets 33 and `VEHICLE_STARTSYNC` already use it.

### Change

- `gtasa/RemoteVehicle.cpp`: new `PublishPose(vehicleId)`. It copies the stored position/rotation
  under `g_mutex`, releases the lock, then calls `ServerWorld::ElementPose` only when no position was
  ever stored (rotation-only update) and `Game::NoteRemoteVehicle` with the model and the Z rotation
  converted to radians. `SetTargetPosition` and `SetTargetRotation` call it after their existing
  `DIAG` line.
- `tools/headless/vehicle_sync_tests.cpp`: new host test. It drives the real `PacketHandler` with
  packet 56 bodies and asserts the pose recorded by the game layer.
- `tools/headless/game_script.{h,cpp}`: test-only `Headless::CopySeenVehicle` accessor over the
  existing `NoteRemoteVehicle` record.
- `tools/headless/CMakeLists.txt`: new `neon_vehicle_sync_tests` target, plus a vendored-Opus
  fallback so the suite configures without `libopus-dev`.
- `tests/run_host_tests.sh`: builds and runs the new target.
- `CHANGELOG.md`, `AGENTS.md`, `README.md`: status updated. Device verification is still pending.

### Desktop reference

`CUnoccupiedVehicleSync::Packet_UnoccupiedVehicleSync` (`CUnoccupiedVehicleSync.cpp` 178-208), which
calls `CClientVehicle::SetTargetPosition` / `SetTargetRotation` on the vehicle element. The full
desktop source is not in this checkout; the port follows the behaviour already recorded in
`AGENTS.md` and `docs/MTA_SYNC_ANDROID_PORT.md:114`. See `QUESTIONS.md` item 1.

### Verification performed

| Check | Result |
|---|---|
| `tests/run_host_tests.sh` (join codec + new vehicle test) | pass, exit 0 |
| `neon_vehicle_sync_tests` | 15 checks, 0 failures |
| Negative control: publish commented out, test rebuilt | fails at "packet 56 pose reached the game layer" (expected) |
| `neon_resource_tests`, `neon_spatial_tests`, `neon_ped_query_tests`, `neon_projectile_tests`, `neon_voice_tests` | pass |
| `g++ -std=c++20 -fsyntax-only -Wall -I app/src/main/cpp app/src/main/cpp/gtasa/RemoteVehicle.cpp` | pass |
| `g++ -std=c++20 -fsyntax-only -Wall` on `vehicle_sync_tests.cpp` and `game_script.cpp` | pass |
| `neon_dx_conversion_tests` | SIGSEGV in this container (pre-existing EGL environment issue; not touched) |
| Android build / device | not run (no SDK/NDK, no adb device) |

### Files changed

- `app/src/main/cpp/gtasa/RemoteVehicle.cpp`
- `tools/headless/CMakeLists.txt`
- `tools/headless/game_script.cpp`
- `tools/headless/game_script.h`
- `tools/headless/vehicle_sync_tests.cpp` (new)
- `tests/run_host_tests.sh`
- `CHANGELOG.md`
- `AGENTS.md`
- `README.md`
- `docs/contribution/*` (this record)

### What is still missing

- Device acceptance (see `DEVICE_TEST_REPORT.md`).
- Move speed, turn speed, health, engine and derailed state for packet 56 are still log-only.
- `CanUpdateSync` time context is still ignored.
