# Server peds: spawn, stay alive, sync (implementation plan)

This plan covers server peds (`ENTITY_ADD` type 14) on the phone. A ped has to spawn the way the desktop client spawns it, never crash `libGTASA`, and look the same on every client whether the phone or a desktop owns it.

**Out of scope:** `native-ped-traffic` and `native-vehicle-traffic` (see `PEDS_TRAFFIC_SYNC_GAPS.md`), the native population, vehicles, and raising the bitstream above `0x0030`. Remote *players* only come in where they share a function with peds (`_CreateModel`, `Kill`).

Reference: `~/projects/world-rpg-br-en-eu/mta_neon_source` (`Client/mods/deathmatch/logic`). The rules in `AGENTS.md` apply, and so does the crash gate in `.cursor/skills/mta-sync-engine-safety/SKILL.md`. Every row below is ported by reading the desktop function in full and copying its read order and its calls in the same order. A step that cannot run yet keeps its place, gets a `DIAG` line and a comment naming the missing hook, and is not marked done.

---

## 1. How desktop handles a ped, start to finish

```
ENTITY_ADD (50)           CPacketHandler.cpp 3997-4187   CClientPed ctor + store every field
  └─ player streamer      CClientManager.cpp 27          stream in within 250 m, cap 110
      └─ StreamIn         CClientPed.cpp 6631-6665       request model → _CreateModel
          └─ _CreateModel CClientPed.cpp 5669-6224       AddPed, matrix, restore state, arm fence
StreamedInPulse           CClientPed.cpp 4631-           locks, fence release, interpolation
PED_STARTSYNC (61)        CPedSync.cpp 211-267           this client becomes owner
PED_SYNC (63) in          CPedSync.cpp 289-477           observer: target pos/rot, locks
PED_SYNC (63) out         CPedSync.cpp 125-165, 733-767, 924-1133
PED_STOPSYNC (62)         CPedSync.cpp 269-287
LUA_ELEMENT_RPC (77)      CRPCFunctions.cpp 125-172      setElementPosition, setPedAnimation, …
PED_WASTED (64)           CPacketHandler.cpp 1277-1366   Kill on every client
  owner side              CClientGame.cpp 4677-4852, 6531-6578
ENTITY_REMOVE (51)        CPacketHandler.cpp 4491-4566   delete; StreamOut → _DestroyModel 6225-6396
```

## 2. What the phone does today (verified in the tree, 2026-11-14)

| Step | Android | Gap |
|---|---|---|
| `ENTITY_ADD` ped | `EntityAddPed.cpp` reads every field in order | Only position, heading and model are kept. Health, armor, `bSynced` locks, frozen, headless, jetpack, alpha, move anim, clothes, weapons, current slot, collisions, the animation block and vehicle warp are read and then dropped. `ResolveModelID` / `IsValidModel` are not run. |
| Stream in | `ApplyRemotePeds` (`GameEngine.cpp` 1532), one body per frame, cap 48 nearest | No distance gate at 250 m. Model 0 becomes model 7 (a deviation; model 0 is a valid CJ on desktop). The `_CreateModel` state restore (health, armor, weapons, move anim, alpha, collision, clothes, frozen, anim) is not run. |
| Collision fence | `ArmPedCollisionFence` / `HoldPedCollisionFence` (`GameEngine.cpp` 1022-1084) | Armed for hosted peds too. Desktop returns early when `m_bIsSyncing` (`CClientPed.cpp` 5917). **On release, `CPhysical::AddToMovingList` is never called** (desktop `CClientPed.cpp` 6036-6048: "a required resume when construction left it out of the list"). The symbol `_ZN9CPhysical15AddToMovingListEv` is exported. `UpdateRemoteReplicaPhysicsFence` (7140-7152) is not ported. |
| `PED_STARTSYNC` | `HandlePedStartSync` (`PacketHandler.cpp` 570) | Health and armor are read and dropped (`(void)fHealth`). Desktop calls `UnlockHealth`, `UnlockArmor`, `SetHealth`, `SetArmor` (`CPedSync.cpp` 256-261). |
| `PED_SYNC` in | `HandlePedSync` (`PacketHandler.cpp` 335) | `ucSyncTimeContext` is read and ignored. Desktop drops the entry unless `CanUpdateSync` passes (`CPedSync.cpp` 376, `CClientEntity.cpp` 180-185). The position step is the 0.35 blend, not `SetTargetPosition` / `UpdateTargetPosition`. |
| `PED_SYNC` out | `SendPedSync` | Ported, not yet run on a device. |
| Element RPCs | Packet 77 is **not dispatched and is excluded from the unhandled log** (`PacketHandler.cpp` 1051). `HandleLuaRPC` (packet 68) handles only `SHOW_CHAT`. | Every server `setElementPosition`, `setPedAnimation`, `setPedRotation`, `setElementHealth`, `giveWeapon`, `setElementModel`, `setElementFrozen` and so on for a ped is dropped. |
| `PED_WASTED` | Not dispatched (64) | A dead ped stays standing on observers. The owner never sends 64. |
| `ENTITY_REMOVE` | `ServerWorld::OnEntityRemove` (`ServerWorld.cpp` 476) | Reads **one** id. Desktop loops `while (bitStream.Read(ID))` (`CPacketHandler.cpp` 4496), so batched removals leave ghost peds behind. |
| Client Lua on peds | `setPedControlState`, `setPedRotation` → `l_true`; `setElementPosition`, `setElementHealth`, `setPedAnimation` → `TrueResult` | Scripts that drive a locally owned ped get success back and nothing happens. |

### Why peds "do not move" (`PEDS_SYNC_STATUS.md` → Open)

These are suspects ranked by evidence. None of them is confirmed yet. Phase 0 tells them apart.

1. **The body never goes back on the moving list after the fence.** The ped was created while static-waiting for collision. Desktop calls `AddToMovingList` when it clears that flag. The phone clears the flag and the freeze bits but does not re-link the ped. GTA does not run `ProcessControl` on a physical body that is off the moving list, so the ped's own tasks and speed never integrate. This affects hosted and observed peds alike.
2. **The server's movement never arrives.** Scripts move server peds with `setElementPosition` and `setPedAnimation` (packet 77), and the phone drops that packet without logging it.
3. **Observer physics fights the blend.** Desktop freezes remote replicas (`UpdateRemoteReplicaPhysicsFence`) so that only interpolation moves them. The phone unfreezes them, so gravity and `ProcessControl` pull against the 0.35 step.

---

## 3. Phases

### Progress (2026-11-14, host build only, no device run yet)

| Step | State |
|---|---|
| 0.1, 0.3, 0.4 | In the tree. Needs a device log. |
| 0.2 | `shared/MtaIds.h` from compiled enums. The 68/120 vs 76/77/119 question waits for the 0.1 log. |
| 1.1-1.9 | Kept in `PedSpawn`, host test `CheckPedEntityBody`. 1.10 (model validation) open. |
| 2.3 health/armor, frozen, 2.10, 2.11, 2.12 | In the tree. |
| 2.8 seat | Reverted to `DIAG`: crashed on device (fault `0x18f`, `RemoveWeaponWhenEnteringVehicle` needs a `CPlayerInfo` the remote body lacks). |
| 2.3 collision, 2.4 weapons, 2.6 alpha, 2.13 animation | In the tree through the Phase 6 RPC queue (ENTITY_ADD record). |
| 2.5, 2.6 headless, 2.7, 2.9 | `DIAG` only. |
| 3.1, 3.2 (sync state and locks) | In the tree. 3.3 open. |
| 4.1 health/armor, 4.2 | In the tree. A PED_STOPSYNC stale-health lock was also fixed. |
| 5.1 | In the tree (with Phase 6). Was held back until 6.2. The phone drops SET_ELEMENT_POSITION, so its stored context goes stale and the check would reject every later PED_SYNC. |
| 5.2-5.5 | 5.3 done for remote players (UpdateTargetPosition); server ped observers keep the 0.35 blend. |
| 6 | In the tree for position, frozen, health, armor, rotation, interior, model (ped and player branches). Extras in the tree too: 6.5 animation (+ progress / speed), 6.7 dimension (stream-out by dimension, PLAYER_SPAWN / PLAYER_LIST / ENTITY_ADD dimension), 6.9 weapons (give / take / take all / ammo / slot), 6.10 alpha and collisions (6.10 velocity open). 6.11 move anim / headless / on fire and 6.12 warp / remove vehicle still open. Notes: packet ids confirmed on device: Lua RPCs on 76 (SHOW_CHAT 119), element RPCs on 77 (`[u8 func][ElementID][args]`). The phone's packet-68 / funcId-120 SHOW_CHAT path never fires; move it to 76 / 119. Read orders taken from desktop: SET_ELEMENT_POSITION (7) x,y,z float, u8 time context, players then an optional u8 warp; SET_ELEMENT_FROZEN (152) 1 bit; SET_ELEMENT_HEALTH (15) float, u8 time context, LockHealth if locked else SetHealth; SET_PED_ARMOR (25) u8 / 1.25, u8 time context; SET_PED_ROTATION (26) SPedRotationSync, u8 time context, optional u8 newWay; SET_ELEMENT_INTERIOR (9) u8 interior, u8 setPosition, then x,y,z if 1; SET_ELEMENT_MODEL (16) u16 model (ResolveModelID, then SetModel). Every time-context write must reach ServerWorld before 5.1 (`CanUpdateSync`) is switched on. A teleport must also reset `PedSyncState`, or `SamplePose` pushes the old target back. |
| 7 | In the tree: observer (27 / 64 -> Kill, respawn) and owner (damage hook -> PED_WASTED). Lua wasted / damage events open (Phase 8). |
| 8 | Not started. |

Run the phases in order. A phase is done only after its test passes on a phone, against the live server, with a desktop client connected. A clean compile or a headless pass does not count.

### Phase 0: measure (logging only, no behavior change)

| # | Work | Done when |
|---|---|---|
| 0.1 | In `PacketHandler::Process`, log the first 8 arrivals of game ids 64, 76, 77 and 101 with their length, plus the first byte (the RPC function id) for 76 and 77. | Device log shows whether the server sends 77 to the mobile peer, and which function ids arrive. |
| 0.2 | Check the packet and RPC numbering. Counting the `ePacketID` enum gives `LUA = 76` and `LUA_ELEMENT_RPC = 77`, yet the phone handles RPCs on 68. Counting `eElementRPCFunctions` gives `SHOW_CHAT = 119`, yet the phone matches 120. Generate both tables from `Packets.h` / `rpc_enums.h` with a `static_assert` against `SHOW_CHAT`, and do not hand-count. | One table, with the offset explained in a comment that cites the device log. |
| 0.3 | In `HoldPedCollisionFence`, log once per ped whether the body is on the moving list after release (read the `CEntity` list-link field, or call `AddToMovingList` behind the debug flag `pedmovelist`). | Suspect 1 is confirmed or ruled out with a device log. |
| 0.4 | Remove the `#region agent log` `fopen` writes in `DebugPedPose` (`GameEngine.cpp` 1489-1530). They open a file on the game thread every 30 ticks for each ped, and one of the paths is a host path. | No file I/O on the game thread. |

### Phase 1: keep every `ENTITY_ADD` field (parser only, no engine writes)

Desktop: `CPacketHandler::Packet_EntityAdd` ped case, `CPacketHandler.cpp` 3997-4187.
Android: `EntityAddPed.cpp`, `EntityAddTypes.h` (`Visual`), `ServerWorld`.

| # | Field | Desktop call (order) | Android store |
|---|---|---|---|
| 1.1 | health, armor | `SetHealth`, `SetArmor` (4057-4058) | `Visual.health/armor` |
| 1.2 | `bSynced` | `LockHealth`, `LockArmor` (4059-4063) | `LockPedHealth/Armor` at spawn |
| 1.3 | vehicle and seat | `WarpIntoVehicle` (4064-4065) | queue a `NoteRemoteSeat` (existing seat path) |
| 1.4 | jetpack, headless, frozen | `SetHasJetPack`, `SetHeadless`, `SetFrozen` (4066-4068) | flags |
| 1.5 | alpha, move anim | `SetAlpha`, `SetMoveAnim` (4073, 4078) | bytes |
| 1.6 | clothes | `AddClothes` × n, then `RebuildModel` (4102-4108) | list of (texture, model, type) |
| 1.7 | weapons, current slot | `GiveWeapon` per slot, `SetCurrentWeaponSlot` (4130-4145) | array of 13 slots |
| 1.8 | collisions | `SetUsesCollision(bCollisonsEnabled)` (4148) | flag, from the common header |
| 1.9 | animation | `SetPedAnimation` + cache start time (4177-4183) | anim record, plus `startTime = now - elapsed` |
| 1.10 | model | `ResolveModelID`, `IsValidModel`, error 51 (4006-4015) | reject the entity as desktop does |

Done when `tools/headless` reads a captured `ENTITY_ADD` and prints every stored field. No engine change ships in this phase.

### Phase 2: stream in exactly as `_CreateModel` does (game thread)

Desktop: `CClientPed::StreamIn` 6631-6665, `_CreateModel` 5669-6224, fence arm 5914-5945, fence release 6036-6048, `UpdateRemoteReplicaPhysicsFence` 7140-7152.
Android: `ApplyRemotePeds`, `CreateRemotePlayerPed`, `ArmPedCollisionFence`, `HoldPedCollisionFence`.

| # | Step (in `_CreateModel` order) | libGTASA entry (dlsym; missing → `DIAG` + skip) |
|---|---|---|
| 2.1 | Create the body: `CPed::operator new` + `CPlayerPed(1,false)` after the group patch. Already in place, do not change. | existing |
| 2.2 | `SetMatrix` (upright `CMatrix`), current and target rotation, move speed, turn speed | existing `PlaceRemotePlayerPed` |
| 2.3 | `SetUsesCollision`, `SetHealth`, `SetArmor` from the Phase 1 record | +0x6AC store exists. **+0x6B4 needs the disassembly check from the skill (step 8) before it gets any new call site.** |
| 2.4 | `GiveWeapon` per slot, `SetCurrentWeaponSlot` | `_ZN4CPed10GiveWeaponE11eWeaponTypejb`, `_ZN4CPed16SetCurrentWeaponEi` |
| 2.5 | `SetMoveAnim` | `_ZN4CPed11SetMoveAnimEv` + the move-anim field (offset to be found from disassembly) |
| 2.6 | `SetInterior` (exists), `SetAlpha`, `SetHeadless` | alpha: log only until the `CClientPed::SetAlpha` path (8516-) is mapped |
| 2.7 | `RebuildModel` when clothes exist (model 0 only) | `CClothes::RebuildPlayer`, already resolved for the wardrobe |
| 2.8 | `WarpIntoVehicle` if seated | existing `ApplyRemoteSeat` |
| 2.9 | `RunAnimationFromCache` | `_ZN23CTaskSimpleRunNamedAnimC1EPKcS1_ifibbbb` (only because desktop calls it here) |
| 2.10 | Arm the fence **only when not syncing** (5917). | existing |
| 2.11 | On release, restore static-waiting, apply the freeze state, then **`AddToMovingList` when not static** (6040-6046). | `_ZN9CPhysical15AddToMovingListEv` |
| 2.12 | Port `UpdateRemoteReplicaPhysicsFence`: freeze an observed ped that is not frozen, not seated, not attached and not in a synced position-anim. Release it when this phone owns the ped. | existing `SetPhysicalFrozen` |
| 2.13 | Streamer: stream in within 250 m and out beyond it, in addition to the cap of 48 (`CClientManager.cpp` 27). Keep 48 and comment the deviation. | none |

Crash gate: everything runs in `WorldRPG_NoStoryScript`. At most one new body per frame. Every engine call is null-checked. A task constructor whose `CTask::operator new` returns null aborts and never calls through the null.

Done when: (T1) a desktop-owned ped walks on the phone and does not sink or float. (T2) a phone-owned ped whose script gives it a task actually moves. Twenty spawn/despawn cycles at the 48 cap produce no tombstone.

### Phase 3: stream out and remove

Desktop: `Packet_EntityRemove` 4491-4566, `StreamOut` 6666-6684, `_DestroyModel` 6225-6396.

| # | Work |
|---|---|
| 3.1 | `OnEntityRemove`: loop `while (ReadElementId)`, and call `ForgetRemote` for each id. |
| 3.2 | Before `DropBody`: clear the seat, drop the ped from the hosted list (`PedSyncState::EndHosted` + `Forget`), and clear the native-task presentation state (as `StreamOut` does). |
| 3.3 | Keep `DropBody`'s `CPlaceable` vtable guard. Run `RemoveFromMovingList` only if `_DestroyModel` does. Read 6225-6396 before adding it. |

Done when a server script that destroys 30 peds in one tick leaves none behind on the phone, and the phone does not crash.

### Phase 4: ownership (`PED_STARTSYNC` / `PED_STOPSYNC`)

Desktop: `CPedSync.cpp` 211-287, `AddPed` / `RemovePed` 167-204.

| # | Work |
|---|---|
| 4.1 | StartSync: `SetPosition`, which also resets interpolation: clear the observer interpolator in `PedSyncState`. Then `SetCurrentRotation`, `SetMoveSpeed`, `UnlockHealth`, `UnlockArmor`, `SetHealth(fHealth)`, `SetArmor(fArmor)`, `AddPed`. Today health and armor are dropped. |
| 4.2 | Release the observer freeze (2.12) at start, and set it again at stop. |
| 4.3 | Keep the 305-bit `0x0030` deviation as it is (camera and takeover are absent). |

Done when (T3) the server moves ownership from the desktop to the phone and back without the ped jumping, and its health matches on both screens.

### Phase 5: observer apply (`PED_SYNC` in)

Desktop: `Packet_PedSync` 289-477, `CanUpdateSync` (`CClientEntity.cpp` 180-185), `UpdateRemoteAuthoritativeTransform` 8710-8764, `SetTargetPosition` 8657-8709, `UpdateTargetPosition` 8765-8822, `SetTargetRotation(delay, …)` 5342-5440, `Interpolate` 5441-.

| # | Work |
|---|---|
| 5.1 | Store `ucSyncTimeContext` per ped from `ENTITY_ADD` (already read) and from RPCs (Phase 6). Drop a `PED_SYNC` entry when `CanUpdateSync` fails. |
| 5.2 | Skip position, rotation and speed when `HasSyncedAnim && !bUpdatePosition` (423-430). |
| 5.3 | **Replace** the 0.35 blend with `UpdateTargetPosition`. Do not run it alongside the blend. Port the error spread over `spatialSyncRate` and the snap threshold. The observer freeze from 2.12 is what makes this safe. |
| 5.4 | Rotation through `SetTargetRotation(delay)` into +0x6C4/+0x6C8. |
| 5.5 | `0x80`: `KillAnimation` + clear `HasSyncedAnim`. `0x20` / `0x40`: log only until `SetOnFire` / `SetInWater` are mapped. |

Done when (T1) a desktop-owned ped walking a circle stays within 1 m of its desktop position and does not jitter.

### Phase 6: server RPCs on peds (packet 77)

Desktop dispatch: `CRPCFunctions::ProcessPacket` 125-172 (function id, then `ElementID` source). Port the handlers in this order, which is roughly how often a ped script uses them. Each one is applied on the game thread through a queued record.

| # | RPC | Desktop | Notes |
|---|---|---|---|
| 6.1 | dispatcher | `CRPCFunctions.cpp` 125-172 | new `HandleLuaElementRPC`. Ids come from the generated table (0.2). Unknown ids log once. |
| 6.2 | `SET_ELEMENT_POSITION` | `CElementRPCs.cpp` 122-170 | Sets the **sync time context** first, then `SetPosition` + `UpdateRemoteAuthoritativeTransform`. This is why 5.1 needs it. |
| 6.3 | `SET_PED_ROTATION` | `CPedRPCs.cpp` 62-90 | upright yaw path |
| 6.4 | `SET_ELEMENT_HEALTH`, `SET_PED_ARMOR` | `CElementRPCs.cpp` 471-535, `CPedRPCs.cpp` 42-60 | through the lock / +0x6AC path |
| 6.5 | `SET_PED_ANIMATION` (+ `_PROGRESS`, `_SPEED`) | `CPedRPCs.cpp` 259-375 | `CTaskSimpleRunNamedAnim`; null-task guard |
| 6.6 | `SET_ELEMENT_FROZEN`, `SET_PED_FROZEN` | `CElementRPCs.cpp` 697-728, `CPedRPCs.cpp` 406-420 | `SetPhysicalFrozen` |
| 6.7 | `SET_ELEMENT_INTERIOR`, `SET_ELEMENT_DIMENSION` | `CElementRPCs.cpp` 252-318 | dimension hides or streams out the ped |
| 6.8 | `SET_ELEMENT_MODEL` | `CElementRPCs.cpp` 536-647 | `_ChangeModel` 6397-6525: drop and recreate the body, one per frame |
| 6.9 | `GIVE_WEAPON`, `TAKE_WEAPON`, `TAKE_ALL_WEAPONS`, `SET_WEAPON_SLOT` | `CWeaponRPCs.cpp` 26-211 | `GiveWeapon`, `ClearWeapons`, `SetCurrentWeapon` |
| 6.10 | `SET_ELEMENT_VELOCITY`, `SET_ELEMENT_ALPHA`, `SET_ELEMENT_COLLISIONS_ENABLED` | `CElementRPCs.cpp` 172-215, 422-454, 658-696 | |
| 6.11 | `SET_PED_MOVE_ANIM`, `SET_PED_HEADLESS`, `SET_PED_ON_FIRE` | `CPedRPCs.cpp` 177-189, 391-405, 376-390 | log only where no hook exists |
| 6.12 | `WARP_PED_INTO_VEHICLE`, `REMOVE_PED_FROM_VEHICLE` | `CPedRPCs.cpp` 190-240 | existing seat path; no new vehicle code |

Done when a server debug script that runs `setElementPosition`, `setPedAnimation("ped","WALK_civi")`, `setElementHealth` and `giveWeapon` on a ped shows the same result on the phone and on the desktop.

### Phase 7: death

Desktop: observers `Packet_PlayerWasted` 1277-1366 (packet 64 is routed here), `CClientPed::Kill` 3817-3887. Owner: `ApplyPedDamageFromGame` 4677-4852, `DeathHandler` 4853-, `SendPedWastedPacket` 6531-6578.

| # | Work | libGTASA |
|---|---|---|
| 7.1 | Dispatch 64: read `ID, KillerID, SWeaponTypeSync, SBodypartSync, stealth bit, time context, compressed animGroup, animID`. Set the time context, then `Kill`, then fire `onClientPedWasted`. | |
| 7.2 | `Kill`: `CTaskComplexDie(weapon, group, id, 4.0, 1.0)` at `EVENT_RESPONSE_NONTEMP`, or `CTaskSimpleDead` when `bSetDirectlyDead`, then `SetHealth(0)`. Stop applying sync to the ped. | `_ZN15CTaskComplexDieC1E…`, `_ZN15CTaskSimpleDeadC1Eib` |
| 7.3 | Owner: when a hosted ped's health goes from above 0 to 0, send 64 in the `SendPedWastedPacket` field order, only while `IsSyncing`. | needs the damage hook (7.4) |
| 7.4 | Damage hook equivalent to `CClientGame::DamageHandler` (4478-4676). Candidate: `CPedDamageResponseCalculator::ComputeDamageResponse`. Read the desktop hook in `multiplayer_sa` before choosing the site. | exported |

Done when (T5) a ped killed on each client dies once, on both screens, with the same animation, and the server fires `onPedWasted` once.

### Phase 8: client Lua on peds (non-traffic scripts only)

Bind these for real, following `CStaticFunctionDefinitions`: `setElementPosition` (1072-), `setPedRotation` (2274-2329), `setPedAnimation` (2330-2381), `setElementHealth` (1487-), `killPed` (2216-2273), `setPedControlState` (2504-), `setPedAimTarget` (2606-). Each one queues to the game thread. A call on a ped the phone does not own changes only the local copy, as on desktop, and the next `PED_SYNC` overwrites it.

Done when a client script that plays an animation and walks a phone-owned ped is visible on the desktop through `PED_SYNC`.

---

## 4. Test matrix (one phone and one desktop on the same server)

The owner of each ped shows in the server log line `assign id=... owner=...`.

| # | Owner | Action | Expected on both |
|---|---|---|---|
| T1 | desktop | ped walks a circle | phone within 1 m, walks, no jitter, no sinking |
| T2 | phone | script gives the ped a task or animation | ped moves on both |
| T3 | handoff | server swaps the owner twice | no jump, health equal |
| T4 | server | `setElementPosition` / `setPedAnimation` | same pose on both, no snap-back from a stale `PED_SYNC` |
| T5 | each | kill the ped | dies once, same animation |
| T6 | server | spawn 60 peds, destroy all in one tick | phone holds the 48 nearest, then none; no tombstone |
| T7 | – | walk 300 m away and back | peds stream out and back in with health, weapon and model kept |
| T8 | – | 10 min session at the cap | no `SIGSEGV`/`SIGBUS`; `noremote` comparison run is clean |

After each phase: run `tools/headless` on a captured stream, then `./gradlew :app:assembleDebug`, then the device test. Keep the fault address, the `libGTASA` base and the last ped `DIAG` for any tombstone.

## 5. Bookkeeping

- Each converted function gets one `CHANGELOG.md` line with the desktop file and line range, marked "not yet run on a device" until it has been.
- After each phase, add or update the matching rows in the `AGENTS.md` conversion table.
- Recorded deviations: cap 48 instead of 110, model 0 mapped to 7, bitstream `0x0030` (`PED_STARTSYNC` at 305 bits), no locomotion or weapon presentation lanes.
- Open before Phase 2.3: the conflicting notes on `CPed+0x6B4` (`RenderArmorBar` reads a float there, `AccountForPedArmour` a `CPed*`). The existing armor store in `ApplySyncedPlayerFields` already writes it. Disassemble both sites before adding any new call to that store.
