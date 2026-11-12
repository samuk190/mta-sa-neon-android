Port map for these gaps: `docs/MTA_SYNC_ANDROID_PORT.md`.
That plan converts the existing PC `CPlayerPed`, `CNetAPI`, and `CPedSync` methods onto `libGTASA`.
This file stays the inventory of what the launcher still fakes.
The list below is not a new sync protocol.
Port order and acceptance live in the port plan.

# Neon launcher: ped and traffic sync

What the phone does after the on-foot and minimum ped/traffic pass, and what is still not the desktop Neon client.

## What now spawns

- Server peds from a full `ENTITY_ADD` type 14. A Lua table in custom data no longer drops the rest of the packet.
- Server vehicles from `ENTITY_ADD` type 2, built with `CAutomobile` and added to `CWorld`. Custom handling or a siren block still ends that packet.
- A ped this client hosts (`PED_STARTSYNC` 61) is not slid every tick. `setPedWander` builds `CTaskComplexWanderStandard` the way Neon does (`CTask::operator new`, base wander ctor, standard vtable, `AddTaskPrimaryMaybeInGroup`). `PED_STOPSYNC` 62 clears that task.
- Other clients' unoccupied cars move from packet 56. Cars this phone hosts stay where they were created.
- Ambient `CPopulation` stays off.

## What the on-foot player does

- Outgoing keysync and puresync use the ped's facing axis and the MTA stick basis (camera = `-heading` until a verified camera yaw exists). Forward stick is negative Y along that camera.
- Punch sends `ButtonCircle`. Crouch sends `ShockButtonL` and the duck flag. A received punch is slot 0 or 1, the attacker facing us inside 1.6 m, one hit each 650 ms. Damage is still the victim's next `PLAYER_PURESYNC` health drop.
- Remote on-foot peds get `SetMoveState` / `SetMoveAnim`, a duck task, and `CTaskSimpleFight`. They are not a context-switched `CPad` on a `CPlayerPed`.

## What the HUD does

- `worldrpg_hud` still draws itself. `getElementHealth` reads the local ped. The screen size is applied before the script captures `guiGetScreenSize`.

## Still not 1:1

- No verified `TheCamera` yaw, so strafe while the camera looks away from the ped heading is not the desktop orbit.
- Melee damage is the facing cone above, not GTA's strike spheres.
- Hosted cars are not driven and this client does not write unoccupied-vehicle sync. No driver task, no `PLAYER_VEHICLE_PURESYNC`.
- Ped clothes, weapons, and alpha from `ENTITY_ADD` are consumed and not applied to the clump.
- `getAmbientPedPopulationProfile` stays missing on purpose so the server keeps the mobile fallback. Zone profiles, couples, groups, cops, and dealers are not native tasks.
- `setPedUseNativeWalkingStyle`, path nodes, and interior/dimension streaming are not bound. A wander task with collision still unloaded can stand still.
