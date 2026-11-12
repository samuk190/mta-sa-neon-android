# Stages S4-S6 (later, not started)

Written after S1-S3 pass their acceptance on the phone. Scope so far, from the README roadmap (Steps B-D):

- **S4 - Ped host wander and observer interpolation (Step B).** Port `CPedSync` host behavior and `CTaskComplexWanderStandard` (README Step B). Remote observers interpolate with `SetTargetPosition` / `SetTargetRotation` (reference `CClientPed.cpp`). `PED_STOPSYNC` (62) clears the wander task. Start from the existing `NativeTaskSync.h` and `PedNativeTaskState.cpp`, which decode the blocks but do not apply them to GTA peds yet.
- **S5 - Vehicle sync (Step C).** Occupied vehicle sync (`PLAYER_VEHICLE_PURESYNC`, 33) and unoccupied vehicle sync (`UNOCCUPIED_VEHICLE_SYNC`, 56), reference `CPacketHandler` and `CClientVehicle` sync paths.
- **S6 - Combat (Step D).** Bullet sync (98), authoritative damage from the victim's `PLAYER_PURESYNC` health (`SPlayerHealthSync`), and melee via `CTaskSimpleFight` from the `ButtonCircle` keysync.

Each of these gets its own stage doc (STAGE_4_..., STAGE_5_..., STAGE_6_...) when it starts, with the same structure as S1-S3.
