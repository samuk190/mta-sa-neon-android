# Stage S3 - Vehicles and objects in ENTITY_ADD

Starts after S1 is merged (S1 creates the stub files this stage replaces). Runs in parallel with S2.

## Goal

Server vehicles (type 2) and objects/weapons (types 3 and the weapon case) parse in ENTITY_ADD, so the packet keeps its alignment and these elements spawn at their server positions.

## Files you own

- `app/src/main/cpp/shared/entity/EntityAddVehicle.cpp` (replace stub)
- `app/src/main/cpp/shared/entity/EntityAddObject.cpp` (replace stub)

Do not edit `ServerWorld.cpp`, `EntityAddCore.cpp`, `PacketHandler.cpp`, or the ped file.

## Reference

- `CPacketHandler.cpp` `case CClientGame::VEHICLE:` and `case CClientGame::OBJECT:` / `WEAPON:` (object case starts around line 3138 in the `Packet_EntityAdd` switch).
- `SyncStructures.h`: `SVehicleHealthSync` (12-bit float), `SLowPrecisionVehicleHealthSync` (8-bit), `SRotationRadiansSync`, `SEntityAlphaSync`, `SPositionSync` (widths depend on `ExtendedWorldPositions`), vehicle color, door/window/tire fields.

## Work items

1. **VEHICLE (2)**: port the reference case in full: model (`ReadCompressed`), position, rotation, health (version-gated: `SVehicleHealthSync` vs `SLowPrecisionVehicleHealthSync` if the reference gates it), colors, paintjob, upgrades, handling, doors/tires/panels/lights, and any custom handling or siren block. The current `ReadVehicleTail` returns early on custom handling and siren; port those blocks too. Log `DIAG vehicle stored id=... model=... pos=...`.
2. **OBJECT (3) and WEAPON**: port the case from line ~3138: position, rotation, `ReadCompressed(usObjectID)`, alpha, low-LOD flag and id, double-sided, breakable, scale, and anything after. Log `DIAG object stored id=... model=...`.
3. Keep the `Visual` fields (`model`, `x/y/z`, `heading`, health) filled. Do not change the struct.

## Acceptance (device)

- `DIAG vehicle stored` and `DIAG object stored` lines appear for server elements.
- `ENTITY_ADD` packets containing vehicles finish with `stored N` equal to the element count.
- Server vehicles appear at their server positions (the existing traffic resource may still spawn its own cars; check server ids in the logs).

## Host check

```
g++ -std=c++20 -fsyntax-only -Wall -I app/src/main/cpp app/src/main/cpp/shared/entity/EntityAddVehicle.cpp
g++ -std=c++20 -fsyntax-only -Wall -I app/src/main/cpp app/src/main/cpp/shared/entity/EntityAddObject.cpp
```
