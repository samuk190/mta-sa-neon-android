# Neon Android: Pedestrian and Traffic Sync Status

> Implementation reference: `docs/MTA_SYNC_ANDROID_PORT.md`.
> Converts desktop PC `CPlayerPed`, `CNetAPI`, and `CPedSync` routines onto Android `libGTASA`.

---

## 1. Currently Implemented & Spawning

- **Server Peds**: Instantiated from `ENTITY_ADD` (type 14). Lua custom data tables no longer discard trailing packet content.
- **Server Vehicles**: Instantiated from `ENTITY_ADD` (type 2), initialized via `CAutomobile` and registered into `CWorld`.
- **Hosted Pedestrians**: Peds hosted by this client (`PED_STARTSYNC` ID 61) no longer teleport every tick. `setPedWander` constructs `CTaskComplexWanderStandard` (`CTask::operator new`, base wander constructor, standard vtable, `AddTaskPrimaryMaybeInGroup`). `PED_STOPSYNC` (ID 62) clears the active task.
- **Unoccupied Vehicle Sync**: Remote unoccupied vehicles update from packet 56. Unoccupied vehicles hosted locally stay stationary.
- **Ambient Population**: Native `CPopulation` remains disabled in favor of server-synchronized entities.

---

## 2. On-Foot Player Mechanics

- **Keysync & Pure Sync**: Outgoing packets utilize the ped's facing vector and MTA stick coordinates (camera angle = `-heading` until verified camera yaw is hooked). Forward stick applies negative Y along the camera orientation.
- **Combat & Movement States**:
  - Punch maps to `ButtonCircle`.
  - Crouch maps to `ShockButtonL` and the duck flag.
  - Received punches validate slot 0/1 within a 1.6m cone facing the target, rate-limited to 650ms per strike.
  - Damage applies via the victim's subsequent `PLAYER_PURESYNC` health report.
- **Remote On-Foot Peds**: Managed via `SetMoveState` / `SetMoveAnim`, duck tasks, and `CTaskSimpleFight`.

---

## 3. Discrepancies & Pending Features (Non 1:1)

1. **Camera Yaw**: Lacks verified `TheCamera` yaw hook; strafing while panning camera independently differs from desktop MTA orbit.
2. **Hit Detection**: Melee uses directional facing cones rather than full GTA 3D strike spheres.
3. **Vehicle Driver Sync**: Local client cannot yet drive hosted vehicles or serialize unoccupied vehicle sync (`PLAYER_VEHICLE_PURESYNC` in progress).
4. **Ped Customization**: Clothes, weapons, and alpha properties from `ENTITY_ADD` are read but not yet visually bound to character clumps.
5. **Pathfinding & Collisions**: `setPedUseNativeWalkingStyle`, path nodes, and interior/dimension streaming are not fully hooked. Peds in unloaded collision zones may idle.
