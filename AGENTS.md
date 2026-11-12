# Agent rules for the MTA Neon Android port

## Rule: build every sync function exactly as the desktop client does

The reference is the desktop client source in `mta_neon_source` (Client/mods/deathmatch/logic, Shared/sdk/net). Before writing or changing a sync function on Android:

1. Find the desktop function by name and read it in full (for example `CPedSync::Packet_PedStartSync`, `CPacketHandler::Packet_EntityAdd`, `CNetAPI::ReadVehiclePuresync`).
2. Port the same read order, the same field widths (from `SyncStructures.h`), the same state changes, and the same calls, in the same order. The function name on Android should match the desktop name, for example `HandlePedStartSync` maps to `Packet_PedStartSync`.
3. Do not add behavior the desktop function does not have. In particular, do not start or stop AI tasks (wander, fight, drive) unless the desktop function calls the matching task API.
4. Do not remove behavior the desktop function has. If a step cannot run on the phone yet (for example a GTA native that is not wired), keep the step and mark it with a `DIAG` log line and a comment naming the missing hook.
5. Known wire differences stay as they are until verified: the phone is pinned to bitstream 0x0030, and the server strips some fields for mobile peers (for example PED STARTSYNC is 305 bits). Every such deviation must be in a comment that cites the device evidence.
6. Each converted function gets a line in CHANGELOG.md with the desktop file and line range it follows.

## Status of conversions

| Function | Desktop reference | Android | State |
|---|---|---|---|
| `CPedSync::Packet_PedStartSync` | CPedSync.cpp:211-266 | `HandlePedStartSync` | Converted: no wander task; syncing flag only. Health and armor apply still missing (no engine hook). |
| `CPedSync::Packet_PedStopSync` | CPedSync.cpp:269-287 | `HandlePedStopSync` | Converted: lock current health and armor, release sync. No wander clear. |
| `CPedSync::AddPed` / `RemovePed` | CPedSync.cpp:167-204 | `PedSyncState::BeginHosted` / `EndHosted` | Converted: bookkeeping only. |
| `CUnoccupiedVehicleSync::Packet_UnoccupiedVehicleSync` | CUnoccupiedVehicleSync.cpp:178-208 | `HandleUnoccupiedVehicleSync` / `RemoteVehicle::ApplyUnoccupied` | Converted: desktop order (target position, target rotation, move speed, turn speed, health, engine on, derailed for trains). Removed the invented hosted and derailed gates. Gap: `CanUpdateSync` time context. Engine effects log only. |

Everything else in the roadmap is not yet converted. Do not mark a row done until its desktop function was read and compared line by line.
