# PR draft — base `staging`

**Branch:** `staging-stage-5` → `staging`
**Title:** fix(sync): apply unoccupied vehicle pose to the remote car

## Context

Stage S5 (`docs/sync-port/STAGE_5_VEHICLE_SYNC.md`). Packet 56 is decoded by
`VehicleSync::ReadUnoccupiedEntry`, but `RemoteVehicle::SetTargetPosition` / `SetTargetRotation`
stored the pose in a table that the game thread never reads and only logged it. Empty cars moved by
another client stayed still on the phone.

## Evidence

- `gtasa/RemoteVehicle.cpp`: both setters wrote `g_vehicles` and called `DIAG`.
- `gtasa/GameEngine.cpp` `ApplyRemoteCars`: reads `g_remoteCars`, filled by `Game::NoteRemoteVehicle`
  from `ENTITY_ADD`, `VEHICLE_STARTSYNC` and packet 33. Packet 56 never reached it.
- The remote car body path already exists; no new body is created by this change.

## Change

- `PublishPose()` copies the stored pose under `g_mutex`, releases it, then calls
  `Game::NoteRemoteVehicle` (model + Z rotation in radians). `SetTargetPosition` and
  `SetTargetRotation` call it after their existing `DIAG` line.
- Rotation-only updates use `ServerWorld::ElementPose` instead of publishing the origin.
- New host test `tools/headless/vehicle_sync_tests.cpp` drives the real `PacketHandler` with
  packet-56 bodies and asserts the pose recorded by the game layer.
- Host build falls back to the vendored Opus 1.4 tree when `libopus-dev` is absent, so
  `tests/run_host_tests.sh` configures on a clean Ubuntu checkout.
- README / AGENTS / CHANGELOG rows updated; device verification explicitly marked pending.

## Desktop reference

`CUnoccupiedVehicleSync::Packet_UnoccupiedVehicleSync` (`CUnoccupiedVehicleSync.cpp` 178-208)
calling `CClientVehicle::SetTargetPosition` / `SetTargetRotation`. The desktop tree is not in this
checkout, so the port follows the line ranges already recorded in `AGENTS.md` and
`docs/MTA_SYNC_ANDROID_PORT.md:114`.

## Verification

| Check | Result |
|---|---|
| `tests/run_host_tests.sh` | pass (exit 0) |
| `neon_vehicle_sync_tests` | 15 checks, 0 failures |
| Negative control (publish removed) | fails at “pose reached the game layer” |
| `neon_resource/spatial/ped_query/projectile/voice` | pass |
| `g++ -std=c++20 -fsyntax-only -Wall` on changed `.cpp` | pass |
| Android build / device | not run (no SDK/NDK, no adb device) |

## Limitations

- Move speed, turn speed, health, engine and derailed state for packet 56 are still log-only.
- `CanUpdateSync` time context is still ignored.
- No device acceptance yet; the exact test is in `docs/contribution/DEVICE_TEST_REPORT.md`.
- The audit in `docs/contribution/` also lists stale README/roadmap rows not changed here.

## Review guide

1. `app/src/main/cpp/gtasa/RemoteVehicle.cpp` — the whole functional change.
2. `tools/headless/vehicle_sync_tests.cpp` — what is asserted.
3. `tools/headless/CMakeLists.txt` — test target and Opus fallback.
