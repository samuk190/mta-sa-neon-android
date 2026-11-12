# MTA sync: port the PC methods to Android

Convert the desktop Neon client onto `libGTASA`. The packets stay the ones `CNetAPI`, `CPedSync`, and `CUnoccupiedVehicleSync` already read and write. This plan replaces the gap list in `PEDS_TRAFFIC_SYNC_GAPS.md` with the PC function that owns each behavior.

Sources for the PC side are in this repo under `mta_neon_source/`. They match the San Andreas Online tree at `Client/game_sa` (`CPlayerPedSA`, `CPedSA`, `CVehicleSA`, `TaskCarSA`, `TaskGoToSA`, `TaskSA`, `CTaskManagerSA`) and `Client/mods/deathmatch/logic` (`CNetAPI`, `CClientPed`, `CPedSync`, `CUnoccupiedVehicleSync`) plus `Client/multiplayer_sa/multiplayer_keysync.cpp`. PC addresses below are GTA SA 1.0 US, the same binary those wrappers call. Android names are the Itanium symbols for arm64 (`size_t` is `unsigned long`). Resolve every one with `dlsym`. A missing symbol stops that step. It does not get a stand-in.

## What the launcher already fakes

Leave the packet decoders where they are. Stop the apply side when the matching PC call exists.

| Launcher function | What it does instead of the PC call |
| --- | --- |
| `GameEngine.cpp` `ApplyRemotePeds` | `CPopulation::AddPed` (`_ZN11CPopulation6AddPedE8ePedTypejRK7CVectorb`) or `CCivilianPed::CCivilianPed` (`_ZN12CCivilianPedC1E8ePedTypej`), then `CPed::Teleport` (`_ZN4CPed8TeleportE7CVectorh`) and a 0.35 matrix lerp |
| `GameEngine.cpp` `PresentOnFoot` / `PlayPunch` | `CPed::SetMoveState` + `SetMoveAnim`, `CPedIntelligence::SetTaskDuckSecondary`, and a hand-built `CTaskSimpleFight` |
| `GameEngine.cpp` `StartWander` | `CTask::operator new`, base `CTaskComplexWander::CTaskComplexWander` (`_ZN18CTaskComplexWanderC2Eihbf`), then a poke of `_ZTV26CTaskComplexWanderStandard` |
| `PacketHandler.cpp` `HandlePlayerPureSync` / `HandleKeySync` | Stores position, stick, and duck/fire for that lerp and for `PresentOnFoot` |
| `PacketHandler.cpp` `HandleBulletSync` | Applies a local damage number, then the next pure sync reports health |
| `NeonJoinProtocol.cpp` outgoing keysync | Writes on-foot keys with camera basis `-heading` |
| `PacketHandler.cpp` `HandlePedSync` / `HandleUnoccupiedVehicleSync` | Feeds the same ped lerp, and moves cars this phone does not host |

`ApplyRemotePeds` is also the crash. Several `CCivilianPed` bodies are created in the air and `Teleport` runs `CTaskComplexInAirAndLand::CreateFirstSubTask` with `this == 0` (`CTask::operator new` returned no slot). `CPlayerPedSA` never does that.

## 1. What not to reinvent

Each row is already owned. Port the function. Do not add a second sync format.

| Behavior | PC owner |
| --- | --- |
| Create a remote player or a script ped | `CClientPed::_CreateModel` → `CPoolsSA::AddPed` → `CPlayerPedSA::CPlayerPed(unsigned model)` |
| Create the local player | `CClientPed::_CreateLocalModel` → `CGame::InitLocalPlayer` → `CPlayerPed::SetupPlayerPed` (`_ZN10CPlayerPed14SetupPlayerPedEi`) and `FindPlayerPed(0)` (`_Z13FindPlayerPedi`) |
| On-foot pure sync, read and write | `CNetAPI::ReadPlayerPuresync` / `WritePlayerPuresync` |
| Keysync between pure-sync samples | `CNetAPI::ReadKeysync` / `WriteKeysync`, `ReadSmallKeysync` / `WriteSmallKeysync`, `ReadFullKeysync` / `WriteFullKeysync` |
| Controller the on-foot task actually reads | `CClientPed::SetControllerState`, applied inside `SwitchContext` from `HOOK_CPlayerPed__ProcessControl` |
| Camera basis for the left stick | `CCameraSA` `VAR_CameraRotation` (`0xB6F178`), written by `SwitchContext` from `CRemoteDataStorageSA::m_fCameraRotation` |
| Crouch | `CClientPed::Duck` → `CTasksSA::CreateTaskSimpleDuck(DUCK_TASK_CONTROLLED)` → `CTaskSA::SetAsSecondaryPedTask` |
| Punch / melee | The fight task `CPlayerPed::ProcessControl` starts from `ButtonCircle` on the switched pad. `WritePlayerPuresync` keeps `ButtonCircle` for `FIRETYPE_MELEE` |
| Interpolation | `CClientPed::SetTargetPosition`, `UpdateTargetPosition`, `SetTargetRotation`, `Interpolate` |
| Enter as driver, including a ped's car (jack) | `CClientPed::EnterVehicle` → `_GetIntoVehicle` → `CTasksSA::CreateTaskComplexEnterCarAsDriver` → `CTaskSA::SetAsPedTask` |
| Enter as passenger | `_GetIntoVehicle` → `CTasksSA::CreateTaskComplexEnterCarAsPassenger` |
| Exit | `CClientPed::ExitVehicle` → `GetOutOfVehicle` → `CTasksSA::CreateTaskComplexLeaveCar` |
| Already inside on stream-in | `CClientPed::WarpIntoVehicle` → `InternalWarpIntoVehicle` → `CTaskSimpleCarSetPedInAsDriver::ProcessPed` with the warp flag |
| Who hosts a ped | `CPedSync::Packet_PedStartSync` / `Packet_PedStopSync` → `CClientPed::SetSyncing` |
| Wander while hosted | `CTasksSA::CreateTaskComplexWanderStandard` on that ped, added with `CPedIntelligence::AddTaskPrimaryMaybeInGroup` (`0x600E20`) |
| Ped sync wire | `CPedSync::WritePedInformation` / `Packet_PedSync` |
| Stop sliding the host | Host: `Packet_PedStartSync` calls `SetPosition` once, then GTA integrates the task and `WritePedInformation` publishes deltas. Observer: `SetTargetPosition`, never `Teleport` each tick |
| Occupied vehicle | `CNetAPI::ReadVehiclePuresync` / `WriteVehiclePuresync` → `CClientVehicle::SetTargetPosition` / `SetTargetRotation` for the driver seat |
| Empty vehicle | `CUnoccupiedVehicleSync::Packet_UnoccupiedVehicleStartSync` / `StopSync` / `WriteVehicleInformation` / `Packet_UnoccupiedVehicleSync` |
| Vehicle body | `CPoolsSA::AddVehicle` → `CCarCtrl::CreateCarForScript` (`0x431F80`), then the class wrapper (`CAutomobileSA` and the other subclasses). Direct `CAutomobile::CAutomobile` is only the fallback the launcher uses today |
| Bullet | `CNetAPI::ReadBulletsync` → `CClientPlayer::DischargeWeapon`. The victim's next `WritePlayerPuresync` carries health and the damage bit (`GetDamagerID`, weapon, bodypart) |
| Task ownership | The ped's `CTaskManager` (`CTaskManagerSA::SetTask`, `0x681AF0`). Context switch lends pad 0 for one `ProcessControl`. It does not move the task onto another ped |

`CTaskSA::CreateTaskInterface` calls `CTask::operator new` (`0x61A5A0`, Android `_ZN5CTasknwEm`) and aborts the constructor when the pointer is null. Any Android call does the same. A null task is not invoked.

## 2. Port map

`same C++` means compile the named `game_sa` / `logic` method against the resolved symbol, and keep its control flow. Do not re-encode the packet.

### Player on foot

| PC method | Android | Packet |
| --- | --- | --- |
| `CPlayerPedSA::CPlayerPed(unsigned)` in `CPlayerPedSA.cpp`: `CPed::operator new(SIZEOF_CPLAYERPED)` then `CPlayerPed::CPlayerPed(1, false)` (`push 0` then `push 1`; the `0` is "behave like an AI ped", and the ped is not player slot 0) | `_ZN4CPednwEm` then `_ZN10CPlayerPedC1Eib`. Size is the Android `CPlayerPed`, taken from the local ped `SetupPlayerPed` already built. PC `1956` and the launcher's `0x2000` scratch buffer are the wrong sizes | `ENTITY_ADD` type for the player element, then `PLAYER_PURESYNC` (32) once the body exists |
| `CPlayerPedSA` sets `bIsLanding = false`, `fRotationSpeed = 7.5`, `bStreamingDontDelete`, `bDontStream`, its own `CPlayerPedData` copied from the local ped, and its own `CPedClothesDesc`. It does not call `SetIsStanding(true)`. The comment in that constructor says a standing flag makes an airborne ped take floor friction | Same flag writes on the new ped, after the constructor and before the first `ProcessControl`. Clothes descriptor is a new object. Sharing the local descriptor is the leak `CPlayerPedSA` removed | none; this is construction |
| `CWorldSA::Add` at the end of that constructor | `_ZN6CWorld3AddEP7CEntity` (already resolved) | none |
| `CPed::SetCharCreatedBy(2)` so population will not delete the ped. Population density stays 0 via the existing `CPopulation::ManagePopulation` stop | `_ZN4CPed16SetCharCreatedByEh` | none |
| `CClientPed::_CreateModel`: `SetLanding(false)`, `CEntitySA::SetMatrix` (copies the matrix; it does not call `CPed::Teleport`), `SetCurrentRotation`, `SetTargetRotation`, `SetMoveSpeed` | Matrix copy as in `CEntitySA::SetMatrix`. `SetLanding` is the `bIsLanding` flag, not a call. `CPedSA::SetTargetRotation` writes `fTargetRotation` on the ped interface | First position comes from the create packet or the latest pure sync held until the model exists |
| `CNetAPI::ReadPlayerPuresync` | same C++ apply calls: `SetMoveSpeed`, `LockHealth` / `LockArmor`, `SetTargetPosition(..., TICK_RATE)`, `SetTargetRotation`, `SetControllerState`, `SetCameraRotation`, `Duck`, `SetChoking`, `SetOnFire` | `PLAYER_PURESYNC` (32) |
| `CNetAPI::ReadKeysync` | same C++: `SetControllerState`, `SetCameraRotation`, `SetTargetRotation`, `Duck` when not in a vehicle. Crouch clears `ShockButtonL` before the pad is stored so it does not fight `Duck` | `PLAYER_KEYSYNC` (31) |
| `CNetAPI::WritePlayerPuresync` / `WriteKeysync` | same C++ reading `GetControllerState`, `GetCurrentRotation`, `GetCameraRotation`, `GetHealth`, `IsDucked` | outgoing 32 and 31 |
| `SwitchContext(CPedSAInterface*)` from `HOOK_CPlayerPed__ProcessControl` | Hook `_ZN10CPlayerPed14ProcessControlEv`. For a remote `CPlayerPed`, copy `CPad::GetPad(0)` (`_ZN4CPad6GetPadEi`), write that ped's controller, set the camera-rotation float, call the original, restore the pad and the float on every return. `CClientGame::StaticPostContextSwitchHandler` then runs `ApplyNativeTaskLocomotionVelocityLimit` | Fed by 31 and 32 after `SetControllerState` |
| `CCameraSA::GetCameraRotation` | The float at PC `0xB6F178`. On Android, resolve the float the on-foot task reads. Publish that value in `SCameraRotationSync`. The launcher's `-heading` stops being the basis once this float is resolved | 31 and 32, field `SCameraRotationSync` |
| `CClientPed::Duck` | `_ZN16CTaskSimpleDuckC1E11eDuckControlTypess` (`0x691FC0`) via `_ZN5CTasknwEm`, then `_ZN12CTaskManager16SetTaskSecondaryEP5CTaski` (already resolved, `0x681B60`) | duck flag in 31 (`SKeysyncFlags`) and 32 (`SPlayerPuresyncFlags.bIsDucked`) |
| Melee | `ButtonCircle` on the switched pad. `WritePlayerPuresync` leaves that bit set when the weapon is `FIRETYPE_MELEE` and the secondary attack task is the fight task. GTA creates `CTaskSimpleFight`. `PlayPunch` stops | `ButtonCircle` inside 31 / 32 full keysync |
| `CClientPed::SetTargetPosition` / `UpdateTargetPosition` / `Interpolate` | same C++. A step inside the interval writes the interpolated matrix. `CPed::Teleport` is only the existing warp branch inside `SetTargetPosition` (`PED_INTERPOLATION_WARP_THRESHOLD`), after the ped has already had a landing-cleared `ProcessControl` | 32 position; 31 updates rotation between samples |
| `CClientPed::SetTargetRotation(float)` and the timed overload | writes `fCurrentRotation` / `fTargetRotation` (`CPedSA`, not `CPlaceable::SetHeading` every tick) | 32 `SPedRotationSync`; 31 `SKeysyncRotation` |

### Player enters or leaves a vehicle, including a ped's car

The request packet is `VEHICLE_INOUT` (58). The animation is a GTA task on the `CPlayerPed`. Jack is the driver-enter task when that seat has an occupant. `CTaskComplexEnterCar` builds the drag-out subtask. There is no separate jack task to invent.

| PC method | Android | Packet |
| --- | --- | --- |
| `CClientPed::EnterVehicle` | same C++ gates (not already inside, not mid enter/exit, not jacked, vehicle enterable). Local entity calls `GetIntoVehicle` directly. A networked ped sends `VEHICLE_REQUEST_IN` | outgoing `VEHICLE_INOUT` (58), action in 4 bits, seat in 4 bits, on-water bit, door in 3 bits |
| `CClientPed::_GetIntoVehicle` seat 0 | `_ZN28CTaskComplexEnterCarAsDriverC1EP8CVehicle` (`0x6402F0`) after `_ZN5CTasknwEm`. `SetTargetDoor`, then `CTaskSA::SetAsPedTask` → `_ZN12CTaskManager7SetTaskEP5CTaskib` (`0x681AF0`, primary, force) | server accept on 58, then this task |
| same, seat != 0 | `_ZN31CTaskComplexEnterCarAsPassengerC1EP8CVehicleib` (`0x640340`: vehicle, game seat, carry-on-after-falling = false) and the same `SetTask` | 58 with passenger seat |
| Occupant in the driver seat | The same driver task. GTA jacks. `CClientPed` sets `m_bIsJackingVehicle` and writes `vehicle_notify_jack` while that task runs | 58 notify bits the PC already writes from the task state |
| `CClientPed::ExitVehicle` / `GetOutOfVehicle` | `_ZN19CTaskComplexLeaveCarC1EP8CVehicleiibb` (`0x63B8C0`: vehicle, door node, delay, sensible leave, force). Door from `CCarEnterExit::ComputeTargetDoorToExit`, which `ExitVehicle` already calls | 58 exit request, then this task |
| `CClientPed::InternalWarpIntoVehicle` | `_ZN31CTaskSimpleCarSetPedInAsDriverC1EP8CVehicleP28CTaskUtilityLineUpPedWithCar` (`0x6470E0`), set the warp flag, `ProcessPed`, destroy. Passenger warp uses `CTaskSimpleCarSetPedInAsPassenger` (`0x646FE0`). Out uses `CTaskSimpleCarSetPedOut` (`0x6478B0`) | create / respawn when the element is already seated. Not the enter animation |
| After the ped is driving | `CNetAPI::ReadVehiclePuresync` applies the pad with `SetControllerState` and, for seat 0, `CClientVehicle::SetTargetPosition` / `SetTargetRotation` / `SetMoveSpeed` / `SetTurnSpeed` | `PLAYER_VEHICLE_PURESYNC` (33) |

A ped's car is the same path: the vehicle is the `CVehicle` from `CPoolsSA::AddVehicle`, the ped who owns it is a `CPlayerPed`, and seat 0 with an occupant runs `CTaskComplexEnterCarAsDriver`.

### Ped sync

| PC method | Android | Packet |
| --- | --- | --- |
| `CPedSync::Packet_PedStartSync` | `SetPosition`, `SetCurrentRotation`, `SetMoveSpeed`, `UnlockHealth`, `SetHealth`, `SetCameraRotation`, `SetSyncing(true)`. One placement. Then the task runs | `PED_STARTSYNC` (61) |
| `CPedSync::Packet_PedStopSync` | `LockHealth` / `LockArmor`, `SetSyncing(false)`. `SetSyncing(false)` removes only `TASK_COMPLEX_JUMP`, `TASK_COMPLEX_IN_AIR_AND_LAND`, and `TASK_SIMPLE_CLIMB` so the old host stops integrating them | `PED_STOPSYNC` (62) |
| `CTasksSA::CreateTaskComplexWanderStandard` | `_ZN26CTaskComplexWanderStandardC1Eihb` (`0x48E4F0`: move state, direction 0..7, wander sensibly). Allocated with `_ZN5CTasknwEm`. Added with `_ZN16CPedIntelligence26AddTaskPrimaryMaybeInGroupEP5CTaskb` (already resolved). The base wander ctor plus a vtable poke stops | Lua `setPedWander` on the syncer, which is how Neon starts the task. The task is not synthesized from `PED_SYNC` |
| `CPedSync::WritePedInformation` | same C++ delta flags. Position, heading, and velocity come from `GetPosition` / `GetCurrentRotation` / `GetMoveSpeed` on the live `CPlayerPed` | outgoing `PED_SYNC` (63) |
| `CPedSync::Packet_PedSync` | observer only: `UpdateRemoteAuthoritativeTransform`, `SetTargetPosition` at the measured gap clamped to 50..`PED_SYNC_RATE`, `SetTargetRotation`, `SetMoveSpeed`, `LockHealth`. The host ignores spatial echo of its own ped | incoming 63 |
| `CClientPed::_CreateModel` for a script ped | same `CPlayerPed` path as a player. `SetNativeJumpUsesNonPlayerBehavior` / `SetNativeChokingUsesNonPlayerBehavior` when the element is a ped, so jump and choke stay on the `CPed` path | `ENTITY_ADD` type 14 |

The host is the client that received `PED_STARTSYNC` for that element id. Everyone else is an observer and only interpolates. `ApplyRemotePeds` stops calling `Teleport` on `ped.hosted`.

### Vehicle sync

| PC method | Android | Packet |
| --- | --- | --- |
| `CPoolsSA::AddVehicle` | `_ZN8CCarCtrl21CreateCarForScriptEi7CVectorh` (`0x431F80`) after the collision model is loaded. Wrapper class follows `VehicleClass`, including `CAutomobileSA` | `ENTITY_ADD` type 2 |
| `CNetAPI::ReadVehiclePuresync` / `WriteVehiclePuresync` | same C++. Driver (seat 0) interpolates the car. Passenger still applies the pad and the player's health | `PLAYER_VEHICLE_PURESYNC` (33) |
| `CUnoccupiedVehicleSync::Packet_UnoccupiedVehicleStartSync` / `StopSync` | same membership list. The host writes; losing the host stops writing | 54 and 55 |
| `CUnoccupiedVehicleSync::WriteVehicleInformation` | same delta flags from the live `CVehicle` (`GetPosition`, `GetRotationDegrees`, `GetMoveSpeed`, `GetTurnSpeed`, `GetHealth`) | outgoing `UNOCCUPIED_VEHICLE_SYNC` (56) |
| `CUnoccupiedVehicleSync::Packet_UnoccupiedVehicleSync` | `CClientVehicle::SetTargetPosition` / `SetTargetRotation` / `SetMoveSpeed` / `SetTurnSpeed` at `UNOCCUPIED_VEHICLE_SYNC_RATE` | incoming 56 |

### Bulletsync and damage

| PC method | Android | Packet |
| --- | --- | --- |
| `CNetAPI::ReadBulletsync` | `DischargeWeapon` on the shooter for the tracer. Duplicate start/end/order is dropped first, as that function already does | `PLAYER_BULLETSYNC` (98) |
| `CNetAPI::SendBulletSyncFire` | same write: weapon, start, end, order, optional damage, zone, damaged player id | outgoing 98 |
| Victim health | The damaged ped's own `WritePlayerPuresync` (`SPlayerHealthSync`, and the damage bit with damager id, weapon, bodypart). The shooter does not invent the victim's health | next outgoing 32, or 33 when seated |
| Melee damage | The fight task hits. The victim reports the health drop. The 1.6 m / 650 ms cone in `PresentOnFoot` stops | health field of 32 |

## 3. Order of porting

Do one step, check its observable, then the next. Each step adds a file and stops a fake. It does not add a packet.

### Step A — `CPlayerPed` and enter/exit

The in-air crash is this step. Civilians plus `Teleport` are the path `CPlayerPedSA` refuses.

Step 1 of this pass creates the remote ped with `CPed::operator new` and `CPlayerPed(1, false)`, then copies the matrix. It does not call `SetupPlayerPed`. Disassembly of `_ZN10CPlayerPedC2Eib` writes `ped+0x540` to `&CWorld::Players[index] + 8` and does not store the ped pointer. `SetupPlayerPed` is what stores `Players[n].pPed`, so index 1 does not replace `FindPlayerPed(0)`. A private `CPlayerPedData` was not required. Enter, jack, passenger, and exit are not wired: the phone does not yet decode `PLAYER_VEHICLE_PURESYNC` (33) or `VEHICLE_INOUT` (58) onto a live `CVehicle`. Enter-vehicle waits on `PLAYER_VEHICLE_PURESYNC` / `VEHICLE_INOUT`.

Add `worldrpg_neon_launcher/app/src/main/cpp/gtasa/PlayerPedPort.cpp` (and a header). It contains the port of:

- `CPlayerPedSA::CPlayerPed(unsigned)` and the placement block of `CClientPed::_CreateModel` (`SetLanding(false)`, matrix copy, rotations, move speed, `CWorld::Add`, `SetCharCreatedBy(2)`)
- `HOOK_CPlayerPed__ProcessControl` / `SwitchContext` / `ReturnContextToLocalPlayer` for remote peds only. The local ped (`FindPlayerPed(0)`) calls the original and returns
- `CClientPed::Duck`, `_GetIntoVehicle`, `GetOutOfVehicle`, `InternalWarpIntoVehicle`
- `CClientPed::SetTargetPosition` / `UpdateTargetPosition` / `SetTargetRotation` for remote players

`GameEngine.cpp` `ApplyRemotePeds` stops creating players and peds with `g_addPed` / `g_civilianCtor` and stops `g_teleport` on create. `PresentOnFoot` and `PlayPunch` stop for players. `PacketHandler.cpp` `HandlePlayerPureSync` and `HandleKeySync` keep their bit layout and pass the decoded controller, rotation, camera, duck flag, and position into `PlayerPedPort`. `NeonJoinProtocol.cpp` keeps the keysync bit layout and fills the camera field from the resolved camera float once step A has it. Until that float is resolved, outgoing keysync stays on the current basis and is marked as still fake in the gaps doc.

Acceptance: one remote desktop player stands on the ground as a `CPlayerPed`. Logcat does not contain `CTaskComplexInAirAndLand::CreateFirstSubTask`. Holding walk on the desktop plays a walk cycle on the phone. Entering a car a ped is driving plays the enter/jack animation from `CTaskComplexEnterCarAsDriver`.

### Step B — ped host and wander

Add `worldrpg_neon_launcher/app/src/main/cpp/gtasa/PedSyncPort.cpp`. It contains:

- `CPedSync::Packet_PedStartSync` / `Packet_PedStopSync` / `SetSyncing`
- `CTaskComplexWanderStandardSA` constructor (`0x48E4F0`) and `AddTaskPrimaryMaybeInGroup`
- `CPedSync::WritePedInformation` for peds this client hosts
- `CPedSync::Packet_PedSync` observer apply (`SetTargetPosition`, not `Teleport`)

Script peds use the `CPlayerPed` constructor from step A. `GameEngine.cpp` `StartWander` is deleted as a call. The vtable poke goes with it. `ApplyRemotePeds` stops teleporting `ped.hosted`. `PacketHandler.cpp` `HandlePedStartSync`, `HandlePedStopSync`, and `HandlePedSync` keep their bit layout and call `PedSyncPort`.

Acceptance: the phone that receives `PED_STARTSYNC` leaves that ped on the wander task. The ped moves without a per-tick teleport. `PED_STOPSYNC` clears the primary task. A second client sees that motion from `PED_SYNC` interpolation only.

### Step C — occupied and unoccupied vehicles

Add `worldrpg_neon_launcher/app/src/main/cpp/gtasa/VehicleSyncPort.cpp`. It contains:

- `CPoolsSA::AddVehicle` via `CCarCtrl::CreateCarForScript` for `ENTITY_ADD` type 2
- `CNetAPI::ReadVehiclePuresync` / `WriteVehiclePuresync` for seat 0
- `CUnoccupiedVehicleSync::WriteVehicleInformation` and `Packet_UnoccupiedVehicleSync`

`GameEngine.cpp` stops constructing remote cars with a raw `CAutomobile` ctor when `CreateCarForScript` resolves, and stops writing car matrices from the ped lerp. `PacketHandler.cpp` `HandleUnoccupiedVehicleSync` (56) and the vehicle pure-sync dispatch (33) call `VehicleSyncPort`. Hosted empty cars start being written. They stay still today.

Acceptance: a desktop driver in a car is followed by that car on the phone through `CClientVehicle::SetTargetPosition`. An empty car this phone hosts is the one whose position changes in outgoing packet 56. An empty car hosted by the desktop moves from incoming 56.

Bulletsync stays on `CNetAPI::ReadBulletsync` / the victim health field. It is wired in step A for the local player (the victim report already leaves in pure sync) and is not a fourth protocol. `HandleBulletSync` stops choosing a damage number from a weapon table once `DischargeWeapon` and the victim's `GetHealth` are the values on the wire.

## 4. What the Dryxio notes change

`ENTITY_PERFORMANCE.md` and `docs/development/native-ai-telemetry.md` are measurement notes for the Windows client. They do not replace `CNetAPI`.

Copy these rules into the port:

- Pool size is capacity. The crash and the frame cost come from how the ped is simulated (near streamed `ProcessControl`, tasks, collision), which is why step A creates the PC ped type and then stops.
- Do not frequency-scale collision, the sync owner's tasks, or `CPlayerPed::ProcessControl`. Interpolation of a remote observer is the part that may run at the packet interval (`TICK_RATE`, `PED_SYNC_RATE`, `UNOCCUPIED_VEHICLE_SYNC_RATE`).
- A frozen or overlapping pile of vehicles still pays `CPhysical::ProcessCollision` retries. Debug cars are placed apart. Turning collision off is a measurement, not the sync fix.
- The rejected world-AABB prefilter stays rejected. Do not add one on Android without a new measurement that shows repeated collision pairs.
- One model is a poor stand-in for traffic. Varied vehicle models cost more when separated and can cost less when deeply overlapped, because identical cars stay in the unresolved contact state.
- Telemetry, if added later, correlates one sample by element id, owner epoch, and sequence. `rotation_post_process` records three headings (wrapper current, GTA target, matrix) so a bad decode is distinct from a ped that has not finished interpolating. Find that first divergence before changing the task, the packet, and the interpolator in one edit.
- When telemetry is off, the sync path reads one cached flag. It adds no bytes to packets 31, 32, 33, 56, 61, 62, 63, or 98.

Leave these on the desktop. They are SAO-specific:

- The Windows profile tables, VSync 120, and the `test-resources/entity-performance-test` stages.
- Hook addresses inside `CWorld::Process` (`0x5684A0`), `CRenderer::PreRender` (`0x553910`), `CPhysical::ProcessCollision` (`0x54DFB0`), `CPed::PreRenderAfterTest` (`0x5E65A0`), and the byte patches `SwitchContext` writes (`0x60D850`, `0x609C80`, `0xB6EC2E`, and the rest). Those bytes are GTA SA 1.0 US. An Android port identifies the same calls by symbol, or it skips the patch until the symbol is found.
- The JSONL writer, `mta/logs/native-ai-telemetry*.enable`, the 32 MiB rotation, the cop-locomotion oracle, group-decision hooks, and `native-ai-trace-analyzer.py`. `sample_key` is not on the wire. The server re-quantizes spatial records, so a different spatial hash is not a dropped packet.
- Raising Neon renderer lists, or raising the ped pool to get past `CTask::operator new` returning null. The PC fix is to stop spawning `CCivilianPed` through `AddPed` and to refuse a null task.

`flags2` locomotion and animation blocks inside `CPedSync::Packet_PedSync` are part of this PC client once the bitstream version allows them. They are ported with `Packet_PedSync` in step B. They are not a new protocol, and they are not required to finish step A.

## 5. Acceptance

Each step matches PC when this one thing is true. The packet bytes stay the PC layout.

| Step | Observable |
| --- | --- |
| A | A remote player is a `CPlayerPed`, stands, walks from the switched pad, and enters an occupied ped car through `CTaskComplexEnterCarAsDriver`, with no `CTaskComplexInAirAndLand::CreateFirstSubTask` crash |
| B | The `PED_STARTSYNC` host wanders on `CTaskComplexWanderStandard` and is not teleported; the other client only interpolates `PED_SYNC` |
| C | The driver's car follows `PLAYER_VEHICLE_PURESYNC`, and an empty hosted car is the source of outgoing `UNOCCUPIED_VEHICLE_SYNC` |

A shot or a punch matches PC when the victim's next pure sync carries the health `GetHealth` returned after GTA applied the hit.
