# Sync Port Master Plan (mta_neon -> Android Neon client)

Branch: `staging`. Source of truth for behavior: `mta_neon_source` (desktop MTA client), not our guesses.

## Goal

Finish the character/entity sync port so server peds, vehicles and objects appear on the phone, and remote peds sync like the desktop client. The README roadmap (Steps A-E) is the high-level order; this plan splits it into stages that can be verified on the device.

Honest scope: Steps A-E are multiple weeks of work. Each stage below has its own acceptance check. A stage is "done" only when its check passes on the phone, not when the code compiles.

## Current evidence (2026-11-12 device runs)

- Peer is pinned to bitstream `0x0030` (`NeonJoinProtocol`). The server's MOD_NAME reports `0x3D`. The server writes `ENTITY_ADD` for the `0x0030` layout.
- Common `ENTITY_ADD` header, custom-data block and name block now parse correctly (custom counts are 0 in the logs).
- The call-propagation bit is read only at `0x0031+` (fixed in `ServerWorld.cpp`).
- Element type 0 (DUMMY) reads nothing after the common block (fixed in `ServerWorld.cpp`), but the next element header is still misaligned after it. The per-type bodies are still wrong.
- No `PED_SYNC` seen yet (peds are not yet reached in the packet).
- Ped spawn needs every earlier element in the same `ENTITY_ADD` packet to parse correctly, so per-type bodies for all types the server sends are on the critical path.

## Reference (authoritative)

- `mta_neon_source/Client/mods/deathmatch/logic/CPacketHandler.cpp`: `Packet_EntityAdd` (around lines 2860-3800; `case CClientGame::...` per type), `Packet_PedSync`/`CPedSync.cpp`.
- `mta_neon_source/Shared/sdk/net/SyncStructures.h`: wire sizes of every `S*Sync` struct.
- `mta_neon_source/Shared/sdk/net/bitstream.h`: `eBitStreamVersion` (`Unk`=0x30, `ExtendedWorldPositions`=0x31, `NativeTaskLocomotionPresentation`=0x3C, `NativeWorldStaticWorldV3ServerSelectedSet`=0x3D); `ElementID` = 17 bits.
- Element type enum (`CClientGame.h`): DUMMY=0, PLAYER=1, VEHICLE=2, OBJECT=3, MARKER=4, BLIP=5, PICKUP=6, RADAR_AREA=7, SPAWNPOINT=8, REMOTECLIENT=9, CONSOLE=10, PATH_NODE=11, WORLD_MESH=12, TEAM=13, PED=14, COLSHAPE=15.

Rule: when our code and the reference disagree, the reference wins. Port the read order and the field widths exactly. Do not guess widths.

## Rules for every agent

1. Work only on your stage's files (listed in your stage doc). Do not edit the others, to avoid merge conflicts between parallel agents.
2. Do not run `tools/build_android_remote.sh`. It rsyncs and deletes the shared remote workspace, so only the lead builds, one at a time. Verify your code with `g++ -std=c++20 -fsyntax-only -I app/src/main/cpp <file>` (host, syntax only).
3. Keep the `DIAG` logging helpers. Add a `DIAG` line on every new failure path, with the element id, type and bit offset.
4. No new `#include` of Android-only headers in code that the host syntax check compiles.
5. Commit only on your stage branch (`staging-stage-N`). Do not push. Do not open PRs. Do not touch `main`.
6. Report back: files changed, what the reference says for each case you ported (file and line), any width you were unsure of, and the host syntax-check result.

## S1 result (2026-11-12, S25, build of `ecf58b3`)

- Join `worldrpgbroficial.duckdns.org:22003`: three ENTITY_ADD packets, stored 4/4, 41/41 and 84/84.
- No `ENTITY_ADD fail` lines and no "not implemented" stops. Element types seen: 0, 2, 4, 5. Headers have plausible ids and parents.
- S1 acceptance met. Peds (type 14) did not appear in this join, so S2 is not checked yet.

## Stage map (README Steps mapped)

| Stage | Scope | README step | Depends on | Acceptance on phone |
|---|---|---|---|---|
| S1 | ENTITY_ADD core: common header, custom data, name, dispatch table, DUMMY/MARKER/BLIP/PICKUP and all non-ped/non-vehicle types | prerequisite for B | none | Next element header is aligned for the whole packet: no `ENTITY_ADD fail` lines, `stored N` equals count |
| S2 | PED (type 14) body in ENTITY_ADD + PED_SYNC body at `0x0030` | Step B | S1 | `ped actor id=...` lines appear, peds are visible and standing still |
| S3 | VEHICLE (2) and OBJECT (3) body in ENTITY_ADD | Step C (spawn part) | S1 | Vehicles from the server spawn at their server positions |
| S4 | Ped host wander + PED_STOPSYNC; observer interpolation (`SetTargetPosition`) | Step B | S2 | Peds walk, no tick teleports |
| S5 | Occupied and unoccupied vehicle sync (33, 56) | Step C | S3 | Passenger and driver sync |
| S6 | Combat: bullet sync (98), damage registration, melee `CTaskSimpleFight` | Step D | S2, S5 | Hits register on both sides |
| S7 | Scripting engine, touch UI, optimization | Step E | all | out of scope for this plan's first pass |

S2 and S3 can be written in parallel because they touch different files (see stage docs). Builds and device checks run one at a time, after both are merged into `staging`.

## Process

1. Lead creates a branch per stage from `staging`, agents write code there (Haiku agents, one per stage, in isolated worktrees).
2. Lead merges each finished stage branch into `staging` and runs a single remote build.
3. Lead installs the APK on the phone, collects `DIAG` logs, checks the stage acceptance, and records the result in `CHANGELOG.md`.
4. If acceptance fails, the lead writes the next fix task into the same stage doc and re-runs the stage.
5. Nothing is merged to `main` without the user's approval.
