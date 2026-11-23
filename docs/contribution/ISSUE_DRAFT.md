# Issue draft — staging documentation drift and missing reference tree

**Title:** staging: docs and code disagree on several sync rows; `mta_neon_source` is not in the repo

## Summary

While auditing `staging` at `12973bd`, several README/roadmap rows were contradicted by the code.
Reporting them together so each can be confirmed or closed deliberately.

## Confirmed contradictions

| Doc | Code |
|---|---|
| README: `ENTITY_REMOVE` not handled, `ForgetRemote` not wired | `shared/ServerWorld.cpp:783-810` loops every id and calls `ForgetRemote` |
| README: `SetOccupant` has no caller | `shared/PacketHandler.cpp:1218` calls it from `VEHICLE_INOUT` |
| README: outgoing packet 56 “not started” | `net/NeonJoinProtocol.cpp:876` `SendUnoccupiedVehicleSync` |
| README + `PEDS_SYNC_STATUS`: `UpdateTargetPosition` not wired / 0.35 blend | wired for keysync remote players in `GameEngine.cpp`; server ped observers still blend |
| `PEDS_SYNC_STATUS`: `UpdateRemoteReplicaPhysicsFence` not ported | implemented in `gtasa/GameEngine.cpp:1276-1280` |
| `ROADMAP_PEDS_SYNC.md:41`: `ENTITY_REMOVE` reads one id | `ServerWorld.cpp:787` loops |
| README: 16 KB ELF “repo audit fails” | static STL + 16 KB LOAD alignment already configured |

## Missing reference

`AGENTS.md` requires reading `mta_neon_source` before changing a sync function, but the directory is
not in the checkout and the docs point at a machine-local path
(`/home/samuel/projects/world-rpg-br-en-eu/mta_neon_source`). Should it be a submodule, a documented
clone step, or external?

## Why it matters

The 1,581-function parity figure and the conversion table cannot be reviewed against the reference
from a fresh clone, and stale “not handled” rows hide work that already exists.

Full evidence and the backlog are in `docs/contribution/CONTRIBUTION_AUDIT.md` on
`staging-stage-5`.
