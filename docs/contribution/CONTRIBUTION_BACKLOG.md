# Contribution backlog (evidence-based)

Priorities: P0 blocker, P1 very high, P2 high, P3 complementary, P4 experimental.
Every item names the evidence found in the `staging` checkout (`12973bd`).

## P0 — Blockers

### P0.1 Desktop reference tree is absent
`mta_neon_source/` is not in the repo, yet `AGENTS.md` requires reading it before every sync
change. No sync contribution can be certified faithful until the tree is available (submodule,
documented clone step, or a pinned tarball).
Evidence: `docs/MTA_SYNC_ANDROID_PORT.md:5`, `docs/PARITY_MATRIX.md:7`, missing directory.
Impact: correctness of every future port. Complexity: low (process), risk: licence/redistribution.

### P0.2 No device validation loop here
`adb devices` is empty and no Android SDK/NDK is installed in this environment. Every `[~]` row in
the README remains unverified by definition.
Impact: acceptance criteria cannot be closed. Complexity: environment only.

## P1 — Very high

### P1.1 Unoccupied vehicle motion, health, engine, derailed
Packet 56 pose is now applied (this branch). `SetMoveSpeed`, `SetTurnSpeed`, `SetHealth`,
`SetEngineOn`, `SetDerailed` still only log.
Evidence: `gtasa/RemoteVehicle.cpp` `SetMoveSpeed`/`SetTurnSpeed`/`SetHealth`/`SetEngineOn`.
Note: `Game::NoteRemoteVehicleMotion` exists and `ApplyCarMotion` writes `CPhysical +0x68/+0x74`,
but it takes move and turn together, so a per-flag port needs two separate setters or a stored
"pending flags" record.
Impact: cars slide instead of driving between pose samples; damage and engine state invisible.

### P1.2 `CanUpdateSync` time context
`PED_SYNC` reads `ucSyncTimeContext` and ignores it; packet 56 reads `timeContext` and ignores it.
Desktop drops stale contexts.
Evidence: `docs/ROADMAP_PEDS_SYNC.md:37`, `AGENTS.md` rows 25 and 35.
Impact: out-of-order packets can overwrite newer state.

### P1.3 Non-traffic peds still do not move
Documented open bug. Two candidate causes listed in `docs/PEDS_SYNC_STATUS.md`; the code now has
`UpdateRemoteReplicaPhysicsFence` and `UpdateTargetPosition`, but server ped observers still use
the 0.35 blend.
Evidence: `gtasa/GameEngine.cpp` `ApplyRemotePeds`, `AGENTS.md` row 40.
Impact: the headline gameplay bug. Needs a device and a second client to diagnose.

## P2 — High

### P2.1 Vehicle `ENTITY_ADD` acceptance on device
S3 code exists; no device log has ever shown a server vehicle spawning.
Evidence: README Step C first bullet; `CHANGELOG.md` "Not yet run on a device".

### P2.2 Packet 33 occupant binding
`SetOccupant` is called from `VEHICLE_INOUT` only. Until the server sends that packet, packet 33
is ignored by design (`CNetAPI.cpp:1306` gate). Confirm on device that the gate opens.

### P2.3 Lua server events and resource lifecycle
`LUA` 76/77 and `triggerServerEvent` 81 were seen unhandled in earlier device logs.
Evidence: README Step 0 bullets.

### P2.4 Combat effects
Bullet decoders and melee detection exist; `DischargeWeapon`, tracers and `CTaskSimpleFight` are
log-only.
Evidence: README Step D.

## P3 — Complementary

- Fix the stale README/doc rows listed in `CONTRIBUTION_AUDIT.md` (small, evidence-only).
- Add a `tools/syntax_check_changed.sh` that runs the host `-fsyntax-only` command on every changed
  `.cpp` (README "Verification and hygiene" still says manual).
- `neon_dx_conversion_tests`: guard `eglGetPlatformDisplayEXT == nullptr` and skip with a clear
  message instead of SIGSEGV.
- Add host tests for the `ENTITY_ADD` vehicle body and packet 33 decoders (only ped/object coverage
  exists today).

## P4 — Experimental

- Fuzz the `ENTITY_ADD`/`PED_SYNC` readers with truncated and reordered bodies (host-only).
- Property test `WriteUnoccupiedEntry`/`ReadUnoccupiedEntry` round-trips over random flags.

## Recommended next three

1. Fix the stale README rows (P3, no code risk, immediately reduces maintainer confusion).
2. Port per-flag vehicle motion/health for packet 56 (P1.1) once the desktop source is available.
3. `CanUpdateSync` for packet 56 and `PED_SYNC` (P1.2), with host tests for stale contexts.
