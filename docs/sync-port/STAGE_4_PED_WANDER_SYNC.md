# Stage S4 - Ped host wander, PED_STARTSYNC/PED_STOPSYNC, observer interpolation

Roadmap: Step B. Depends on S2 (ped bodies) and S1 (ENTITY_ADD core).

## Goal
Match the desktop client for pedestrians the phone hosts or observes:
- PED_STARTSYNC (61) takes ownership of a ped and starts the native wander task.
- PED_STOPSYNC (62) releases ownership and clears the task.
- PED_SYNC (63) for observed peds updates a target position/rotation with interpolation, not a teleport.

## Reference (authoritative)
- mta_neon_source/Client/mods/deathmatch/logic/CPedSync.cpp: Packet_PedStartSync, Packet_PedStopSync, Packet_PedSync, and the interpolation (SetTargetPosition / SetTargetRotation).
- mta_neon_source/Client/mods/deathmatch/logic/CClientPed.cpp: SetTargetPosition, SetTargetRotation, wander task setup.
- mta_neon_source/Shared/sdk/net/Packets.h: PACKET_ID_PED_STARTSYNC (61), PED_STOPSYNC (62), PED_SYNC (63).

## Files you own
- app/src/main/cpp/shared/PacketHandler.cpp: the PED_STARTSYNC, PED_STOPSYNC and PED_SYNC handlers only (register them in the packet table; PED_SYNC body is already in HandlePedSync).
- NEW app/src/main/cpp/shared/entity/PedSyncState.h and PedSyncState.cpp: per-ped sync state (syncer flag, target pose, timestamps).
- NEW app/src/main/cpp/gtasa/PedInterpolator.h and PedInterpolator.cpp: interpolation between target and current pose.
- app/src/main/cpp/gtasa/GameEngine.h declarations only, for the new engine hooks (SetPedWander/ClearPedWander, ApplyPedTarget).

Do not edit ServerWorld.cpp, EntityAddCore.cpp, LuaVMStub.cpp, or any vehicle file.

## Work items
1. PED_STARTSYNC: read the layout from the reference Packet_PedStartSync (ped id, sync time context, native task physical takeover blocks already decoded in NativeTaskSync.h). Mark the ped as hosted by this phone and start the wander task (engine hook; if the engine call is not available on the phone, record the request and log DIAG).
2. PED_STOPSYNC: read the reference layout, clear hosted state, clear the wander task, keep health/armor lock semantics from the reference.
3. PED_SYNC for observed peds: store the target instead of teleporting. Interpolate position and rotation each frame using the reference's time-based approach (see SetTargetPosition in CClientPed.cpp).
4. Keep every DIAG log, with ped id and bit offset on each failure path.

## Acceptance (device)
- DIAG lines for PED_STARTSYNC/STOPSYNC and PED_SYNC appear when the server sends them.
- Observed peds move smoothly between sync updates (no teleport).

## Host check
g++ -std=c++20 -fsyntax-only -Wall -I app/src/main/cpp <each changed .cpp>
