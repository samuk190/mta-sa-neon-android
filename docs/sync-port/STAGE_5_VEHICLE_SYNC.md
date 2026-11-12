# Stage S5 - Occupied and unoccupied vehicle sync

Roadmap: Step C. Depends on S3 (vehicle bodies in ENTITY_ADD).

## Goal
- PLAYER_VEHICLE_PURESYNC (33): the occupant's vehicle state (position, rotation, velocity, health, seat, controls) is decoded and applied to the remote vehicle.
- UNOCCUPIED_VEHICLE_SYNC (56): unoccupied vehicles get position, rotation, velocity and health from the server.

## Reference (authoritative)
- mta_neon_source/Client/mods/deathmatch/logic/CPacketHandler.cpp: Packet_PlayerVehiclePuresync, Packet_UnoccupiedVehicleSync.
- mta_neon_source/Client/mods/deathmatch/logic/CVehicleSync.cpp (or the equivalent sync file if named differently: grep for "UnoccupiedVehicle" and "VehiclePuresync").
- mta_neon_source/Shared/sdk/net/SyncStructures.h: vehicle sync structs.
- mta_neon_source/Shared/sdk/net/Packets.h: PLAYER_VEHICLE_PURESYNC = 33, UNOCCUPIED_VEHICLE_SYNC = 56.

## Files you own
- app/src/main/cpp/shared/VehicleSync.h and VehicleSync.cpp (new): decoders for both packets.
- app/src/main/cpp/shared/PacketHandler.cpp: handler registration for 33 and 56 only.
- app/src/main/cpp/gtasa/RemoteVehicle.h and RemoteVehicle.cpp (new): store and apply the decoded state to the remote vehicle (engine calls via the existing GameEngine pattern).

Do not edit ServerWorld.cpp, EntityAddCore.cpp, LuaVMStub.cpp, or any ped file.

## Work items
1. Port Packet_UnoccupiedVehicleSync read order exactly, including the version gates the reference uses (use bitstream 0x0030 as the peer version, as elsewhere).
2. Port Packet_PlayerVehiclePuresync read order exactly.
3. Apply: unoccupied vehicles get target position/rotation (interpolated like S4), velocity, health. Occupied vehicles get the occupant's controls and seat.
4. DIAG logs on each failure path and on each applied update (vehicle id, position).

## Acceptance (device)
- DIAG lines for packets 33 and 56 appear when the server sends them.
- Vehicles driven by other players move at their server positions.

## Host check
g++ -std=c++20 -fsyntax-only -Wall -I app/src/main/cpp <each changed .cpp>
