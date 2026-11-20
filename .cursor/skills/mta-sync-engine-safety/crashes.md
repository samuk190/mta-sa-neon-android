# Crash mechanisms behind sync edits

Read this when a tombstone is in hand, or before adding a store at a fixed `CPed` / `CVehicle` offset. The checklist that gates the edit is `SKILL.md`.

The parser (`PacketHandler`, `VehicleSync`, `EntityAdd*`) fills records. `ApplyRemotePeds` and `ApplyRemoteSeats`, called from `WorldRPG_NoStoryScript` on the game thread, write the live objects. A bad record becomes a bad store on the next frame. The faulting PC is inside `libGTASA`.

## 1. Wrong class or constructor

`docs/MTA_SYNC_ANDROID_PORT.md` records the in-air crash: several `CCivilianPed` bodies are created in the air, `Teleport` runs `CTaskComplexInAirAndLand::CreateFirstSubTask`, and `CTask::operator new` returned no slot (`this == 0`). `CPlayerPedSA` does not do that.

`PlayerPedPort.cpp` creates the remote ped with `_ZN4CPednwEm` and `_ZN10CPlayerPedC1Eib`. The pool stride is `0x9c8`. The size argument is `0x998`. The PC size `1956` and a `0x2000` scratch buffer are the wrong sizes.

`CPlayerPed::CPlayerPed` takes one of eight ped groups. Desktop patches that branch at `0x60D64D` ("ALLOW more than 8 players"). On libGTASA 2.10 arm64 the site is ctor `+0x9c`, word `0x370005d4` (`tbnz w20, #0, #+0xb8`), replaced by `0x1400002e` (`b #+0xb8`) only when the original word matches. S25 log: "CPlayerPed group patch applied", four server peds, no group crash (`CHANGELOG.md`).

Vehicles built with `CAutomobile::CAutomobile` in a heap buffer were the suspect for the `sortLODs` `SIGSEGV` after peds appeared. Desktop `CPoolsSA::AddVehicle` uses `CCarCtrl::CreateCarForScript`. The heap path remains only as a fallback when that symbol is missing (`GameEngine.cpp`, the `0x2000` buffer). Do not grow it.

## 2. Destroyed body

`DropBody` in `GameEngine.cpp` ports `IsPlaceableVTBL` from `CVehicleSA::~CVehicleSA` (`CVehicleSA.cpp:400-438`) and `CPlayerPedSA::~CPlayerPedSA` (`CPlayerPedSA.cpp:141-148`). `CWorld::Remove` virtual-calls vtable `+0x20`. A body whose vtable is already `CPlaceable` has two slots there, so the call jumps to `MI_CRANE_HARNESS` (`0x8862d6`) and `SIGBUS`es. Device: 2026-11-13 18:46:58, S25, `libGTASA` base `0x6cfae14000`, fault `0x6cfb69a2d6`, from `DropCarLocked`.

The same shape appears in `DisableEntryExits`: `CEntryExit::TransitionFinished` virtual-calls `CPed::Teleport` at vtable `+0x78` and lands in non-executable data (`SIGSEGV` `SEGV_ACCERR`, `TransitionFinished+1552`, 2026-11-13 18:31:09). Desktop writes a `ret` at `CEntryExitManager::Update` (`CMultiplayerSA.cpp:1415-1416`). That one is an interior transition, not a packet field.

## 3. Matrix and field layout

`PlaceRemotePlayerPed` used to build an `RwMatrix` (second row up, third row at). `CPlaceable::SetMatrix` (libGTASA 2.10 `0x4ebf5c`) stored it, so forward pointed up and the body lay through the pavement. Desktop `CClientPed::SetCurrentRotation` (`CClientPed.cpp:5298-5321`) puts a Z-only `CMatrix` on the element: right `(cos, sin, 0)`, forward `(-sin, cos, 0)`, up `(0, 0, 1)`, position at `matrix+0x30`.

`CWidgetPlayerInfo::RenderHealthBar` (`0x37d770`) loads health with `ldr` at `ped+0x6ac`. `RenderArmorBar` (`0x37d93c`) loads armour at `ped+0x6b4`. `SampleLocalPose` also notes that `CPedDamageResponseCalculator::AccountForPedArmour` (`0x444044`) reads and writes a `CPed*` at that same `+0x6B4`. Those two notes disagree. A new store at `+0x6B4` waits until both instructions are disassembled. The PC armour field `+0x548` is not this slot.

Velocity is `CPhysical+0x68`. `fCurrentRotation` / `fTargetRotation` are `CPed+0x6C4` / `+0x6C8` (`CPed::UpdatePosition` `0x597b3c`, `CPed::TurnBody` `0x59dd58`). `FindPlayerHeading` (`0x4f0720`) uses the forward row at `matrix+0x10`.

## 4. Bitstream 0x0030

`NeonJoinProtocol` pins the peer to `0x0030`. The server's `MOD_NAME` reports `0x3D`. Fields that exist only at a higher version (locomotion presentation `0x3C`, weapon presentation `0x3D`, the train block, full camera orientation) are absent on this wire. Reading them consumes the next field. The reader returns a finite float, and the game thread stores it.

`docs/sync-port/MASTER_PLAN.md` states the rule: when the port and `mta_neon_source` disagree, the reference wins, except for a wire difference already measured on a device and written down.

## 5. Tasks the desktop call does not start

`AGENTS.md` forbids starting wander, fight, or drive unless the desktop function calls that task. The crash form is a null subtask, an empty task pool, or two owners of one body. `ApplyRemotePeds` already skips the 0.35 position blend while `RemoteControlHookActive` drives `CPlayerPed::ProcessControl`, and re-anchors only past 8 m. Putting the blend back on that path fights the stick.

A hosted ped is placed once (`Packet_PedStartSync` / `SetPosition`). The observer uses `SetTargetPosition`. `CClientPed::UpdateTargetPosition` (`CClientPed.cpp:8657-8822`) is not called from a ped task on the phone, so the 0.35 step remains. A second algorithm is a new crash surface.

`CTask::operator new` (`_ZN5CTasknwEm`) returns null when the pool is full. The desktop aborts the constructor. Invoking the null task is the in-air crash.

## 6. Thread, model, and caps

`g_remoteMutex` guards the records the network thread writes. Engine calls stay in `WorldRPG_NoStoryScript` (`t_onGameThread = true`). The reset path already holds that mutex across `DropBody`. A new engine call from a packet handler runs on the wrong thread. A new call also stays off any extension of the time the mutex is held inside `libGTASA`.

Model `0` rebuilds the shared CJ clump and replaces the local player (`ApplyRemotePeds`). Models outside 1..312 become model 7. Cars are models 400..611.

`CopyActors` writes peds first, up to 48, then vehicles, up to 48. One shared buffer used to let a full car list crowd out every ped.

## 7. RenderWare 32-bit pointer delta

`RxLockedPipeUnlock` (local function at `0x278df8`) does `sub x9, x0, x21` then `add x8, x8, w9, sxtw`. The delta is 32 bits. Blocks more than 2 GB apart corrupt vertex arrays or crash `RwEngineStart`. The layout is fixed per boot, so the same APK rendered, lost the world (`LOD entity has no vertex array`, `sortLODs`), or crashed at start on different boots. `InstallGameHeap` redirects the libGTASA `malloc` family into one 1.75 GB reservation. `EngineHostActivity.onCreate` loads `WorldRPGNeon` before the engine thread. S25 with the heap: 150 s in game, no crash. The same build with `noheap` crashes at engine start.

Spawning a ped or a vehicle allocates model and collision memory. Bypassing the heap, or allocating those objects with the process allocator, brings this crash back. `RenderGuard` (hook on `CRenderer::RenderEverythingBarRoads`) drops visible LODs that have instance data and no vertex array. That is a stop-gap. The root cause of a new `sortLODs` fault after a sync edit is still the spawn that dirtied the geometry.

## 8. Seat task on a body with no CPlayerInfo

`SeatRemotePedInCar` runs `CTaskSimpleCarSetPedInAsDriver::ProcessPed`, which calls `CPed::RemoveWeaponWhenEnteringVehicle` (`0x59ba8c`). For a ped whose type word at `+0x720` is 0 or 1 (a `CPlayerPed` body), that function calls `CPlayerPed::GetPlayerInfoForThisPlayerPed` and loads `[x0, #0x18f]` (`0x59bad4`) without a null check. A remote `CPlayerPed(1, false)` has no `CPlayerInfo`, so the call returns null. Device: 2026-11-14 01:51:17 and 01:51:41, S25, `SIGSEGV` fault addr `0x18f`. Trigger: a server ped that `ENTITY_ADD` puts in a vehicle was queued into the seat path on the frame it spawned. That warp is now a `DIAG` (`ApplyPedSpawnRecord`). A new caller of the seat tasks for a body without player info will hit the same fault.

## 9. Destroying a remote ped

A remote body is a pool `CPlayerPed`; only `CWorld::Remove` leaked it with its tasks until `CTask::operator new` returned null (2026-11-14 02:16-02:29). `DestroyRemotePlayerPed` runs `CPed::~CPed` and `CPed::operator delete` and skips `CPlayerPed::~CPlayerPed`, which acts on `pPlayerData`. `CPhysical::~CPhysical` returns the shadow at `+0x190` through `ReturnRealTimeShadow`, which writes `shadow->owner+0x190` without a null check (fault 0x190, 03:32:23); a shadow whose owner is not the ped is cleared first.

## 10. Clothes rebuild with an unknown piece

`CClothes::RebuildPlayer` -> `CClothesBuilder::ConstructGeometryArray` finds each clothing model in `PLAYER.IMG` with only a `VERIFY`; a name that is not there streams nothing and `BlendGeometry` calls `RpClumpForAllAtomics` on a null clump (2026-11-14 04:15:06, x0 = 0). Desktop only accepts pieces listed in `m_DefaultClothes`; `DressRemoteCj` checks the same table. Never pass server clothes strings to `SetTextureAndModel` unchecked. The same crash happens with valid pieces when streaming is busy (04:26:47, during login, right after the local rebuild): `ConstructGeometryArray` assumes `LoadAllRequestedModels` finished. Rebuild only when `CStreaming::IsVeryBusy` is false and `ms_numModelsRequested` is 0.

## Crashes in the same period that are not sync

- `CWidgetRegionGang::Update` stores through `CTouchInterface::m_pWidgets[159]` when the replay widget is null. Fault address `0xb1`. Desktop has no touch widgets.
- Transfer box and chat faults are Java/JNI, not `CPed` stores.

Treat a tombstone in those functions as out of scope for this skill.
