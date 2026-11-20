# Staging audit — 2026-11-20

Read-only investigation of `samuk190/mta-sa-neon-android`, branch `staging`, before any change.

## Git state

| Item | Value |
|---|---|
| Local branch at start | `main` (clean tree) |
| `origin/main` | `eef1e9d` |
| `origin/staging` | `12973bd` — "Merge pull request #6 from samuk190/android-current-wire" (2026-11-20 01:16 -0300) |
| Divergence | `main` is 0 commits ahead, `staging` is 42 commits ahead |
| Work branch for this contribution | `staging-stage-5`, created from `origin/staging` |
| Local modifications at start | none |

`staging` already contains the `0x41E7` gameplay protocol, mobile bitstream `0x0030`, Neon join,
`ENTITY_ADD`, ped/vehicle sync work, the 1,581-function Lua catalog, the parity matrix and the
S1–S7 sync-port plan. Nothing in this audit was reconstructed from `main`.

## Missing reference tree

`mta_neon_source/` is **not present in the checkout**. `docs/MTA_SYNC_ANDROID_PORT.md` (line 5) and
`docs/PARITY_MATRIX.md` (line 7) refer to it as if it were in the repo; `docs/LUA_MOBILE_AUDIT.md`
and `tools/parity_matrix.py` default to `/home/samuel/projects/world-rpg-br-en-eu/mta_neon_source`.

Consequence: the `AGENTS.md` rule "read the desktop function in full before changing a sync
function" cannot be satisfied from this checkout alone. This contribution follows the desktop
behaviour already recorded in `AGENTS.md`, `docs/MTA_SYNC_ANDROID_PORT.md` and
`docs/sync-port/STAGE_5_VEHICLE_SYNC.md`, and says so explicitly. Line-by-line re-verification
against the real desktop source is an open item (see `QUESTIONS.md`).

## Documentation vs code (verified contradictions)

Each row was checked in the current tree; the code is the evidence.

| Doc claim | Code evidence | Verdict |
|---|---|---|
| README: "Element removal: `ENTITY_REMOVE` not handled; `ForgetRemote` not wired (open)" | `shared/ServerWorld.cpp:783-810` loops `while (ReadElementId)`, erases visuals/data and calls `PedSyncState::Forget`, `Game::ForgetRemote`, `RemoteVehicle::ForgetElement` | stale |
| README: "Occupied vehicle sync … `SetOccupant` has no caller" | `shared/PacketHandler.cpp:1218` calls `SetOccupant` from `VEHICLE_INOUT`; `ClearOccupant` at 1225 | stale |
| README: "Hosted empty cars publish deltas in outgoing packet 56 (not started)" | `net/NeonJoinProtocol.cpp:876` `SendUnoccupiedVehicleSync`; `CHANGELOG.md:608` documents the sender | stale |
| README + `docs/PEDS_SYNC_STATUS.md`: "`UpdateTargetPosition` is not wired; position uses the 0.35 blend" | `gtasa/GameEngine.cpp` wires `UpdateTargetPosition` for keysync remote players (`AGENTS.md` row 40); server ped observers still use the 0.35 blend | partially stale |
| `docs/PEDS_SYNC_STATUS.md`: "`UpdateRemoteReplicaPhysicsFence` is not ported" | `gtasa/GameEngine.cpp:1276-1280` implements `UpdateRemoteReplicaPhysicsFence`; `AGENTS.md` row 39 lists it as converted | stale |
| `docs/ROADMAP_PEDS_SYNC.md:41`: "`ENTITY_REMOVE` reads one id" | `ServerWorld.cpp:787` loops all ids | stale |
| README: "16 KB ELF alignment … repo audit fails" | `app/src/main/cpp/CMakeLists.txt:87-90` sets `max-page-size`/`common-page-size` 16384; `app/build.gradle.kts:57-59` uses the static STL so `libc++_shared.so` is not packaged | likely stale; needs an APK audit run to confirm |

## Documentation vs code (newly stale after this contribution)

| Doc claim | Change |
|---|---|
| README: unoccupied vehicle "engine apply is log-only" | position/rotation now published to the game layer; README and `AGENTS.md` updated in this branch |
| README: "Driver drives the vehicle: target position, rotation, velocity and turn speed … not done" | packet 33 pose and motion already reach `NoteRemoteVehicle` / `NoteRemoteVehicleMotion`; health remains log-only. Left as-is, listed in the backlog |

## Environment

- Host toolchain present: g++ 15.2, CMake 4.2.3, Ninja, Python 3, Java 21.
- No Android SDK/NDK and no `ANDROID_HOME`; `./gradlew assembleDebug` cannot run here.
- `adb devices` shows no attached device, so the Redmi Note 13 Pro 5G was not available.
- System `libopus-dev` absent; `tests/run_host_tests.sh` failed at configure time before this
  contribution. Fixed with a vendored-Opus fallback (see `IMPLEMENTATION_LOG.md`).
- `neon_dx_conversion_tests` crashes (SIGSEGV) in this container. It calls
  `eglGetPlatformDisplayEXT` without a null check (`tools/headless/dx_conversion_tests.cpp:163-164`)
  and this environment has no usable surfaceless EGL display. Unrelated to this contribution;
  recorded in the backlog.

## Sync-port state (code, not README percentages)

- S1 `ENTITY_ADD` core: implemented (`shared/entity/EntityAdd*`), host tests exist.
- S2 ped body + `PED_SYNC`: implemented; `CanUpdateSync` gate missing.
- S3 vehicle/object body: implemented; device acceptance never recorded.
- S4 ped host/interpolation: `PedInterpolator`, `PedSyncState`, `UpdateRemoteReplicaPhysicsFence`
  present; server ped observers still use the 0.35 blend.
- S5 vehicle sync: decoders present; packet 33 pose/motion reach the game layer, packet 56 did not
  until this branch; vehicle health/engine/derailed are log-only.
- S6 combat: decoders present; tracer/discharge and melee are log-only.
- S7 scripting/touch/perf: large Lua surface; not audited line by line here.

## Numbers checked in the tree

- 1,581 Lua functions / 12 categories: asserted by the catalog and `docs/PARITY_MATRIX.md`; not
  re-counted here. The matrix is generated by `tools/parity_matrix.py` and was not edited.
- 48 test files are detected by the repository scan; the host suite built here has 7 executables.
