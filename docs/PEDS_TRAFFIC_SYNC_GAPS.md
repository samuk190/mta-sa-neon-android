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

---

## 4. Traffic peds: cause, cross-client model and correction plan (2026-11-13)

### 4.1 Why phones saw no traffic peds

Traffic peds are not GTA's ambient population. The server resource `native-ped-traffic` creates them as MTA peds after a
handshake with each client: world state, population profile, spawn candidate, visibility probe, then owner assignment.
Vehicle traffic (`native-vehicle-traffic`) is a separate resource with its own handshake, which is why cars appeared and
peds did not.

The client script takes its mobile fallback only when the Neon population natives are absent (`client.lua` 406-407,
`mobile_puppet_client.lua` 17). The phone bound four of them as constant-result stubs, so the script ran the desktop
path, sent a `false` profile (`engine-hook-missing`), and the server rejected it (`population-profile-rejected`). Step 0
removes those four bindings. Host check with the real resource scripts in the phone VM: before, the profile was `false`;
after, it is the script's polyfill table.

Spawning is only the first lane. A ped must look and behave the same on every client, phone or desktop, whoever owns it.

### 4.2 How a ped stays in sync in MTA (desktop reference)

One client is the **syncer** (owner) of each ped; the server picks it (`PED_STARTSYNC` 61 / `PED_STOPSYNC` 62). Only the
owner runs the ped's AI and physics. Everyone else is an **observer**.

| Lane | Owner sends | Observer applies | Desktop reference |
|---|---|---|---|
| Spatial | `PED_SYNC` 63: position, rotation (16 bit), velocity, flags `ucFlags`/`flags2` | `SetTargetPosition` / `SetTargetRotation` interpolation | `CPedSync::WritePedInformation` (CPedSync.cpp 924-1130), `Packet_PedSync` (289-478) |
| Walk animation | `PED_SYNC` flags2 `0x02` `SNativeTaskLocomotionSync` (mode + stick) | Observer plays the same locomotion (walk/run cycle) | `UpdateNativeTaskLocomotionBurst` (479), `WriteNativeTaskLocomotionBurst` (654); gated by bitstream `0x3C` |
| Weapon / animation | `flags2` `0x04` / `0x08` presentation blocks | Same task presentation | gated by bitstream `0x3D` |
| Health, armour | `PED_SYNC` `0x08` / `0x10` as raw floats, whenever they change | `LockHealth` / `LockArmor`: observer health is pinned to the owner's value | `WritePedInformation` 1063-1073 |
| Death | `PED_WASTED` 64 from the owner only (`IsSyncing()`) | Server relays 64 to all; each client plays the death | `CClientGame::SendPedWastedPacket` (CClientGame.cpp 6531-6576), `ApplyPedDamageFromGame` 4677-4835 |
| Gunshot hit | Shooter sends `PLAYER_BULLETSYNC` 98 (start, end) | Every client replays the shot from the shooter's model (`DischargeWeapon`). On the owner the bullet hits the ped in GTA, damage is applied there, then reported by the health lane | `CNetAPI::ReadBulletsync` (CNetAPI.cpp 2318-2366) |
| Melee hit | Attacker's keysync (fire button) | Every client makes the attacker's model punch; on the owner the punch hits the ped in GTA | `CNetAPI::ReadKeysync` |
| Behaviour | Resource events: `damageObserved`, `gunAimObserved`, `combatContext` to server; `damageResponse`, `gunAimedAt` to the owner | Owner injects GTA's native event (flee, cower, fight) | `native-ped-traffic` server.lua 1381-1393, 4936-5065; client.lua 2539-2775 |

Two rules follow. Damage is decided **only on the owner**; observers never subtract health themselves. And the owner can
only decide it if it **replays other players' shots and punches into its own game**.

### 4.3 What the phone does today

| Lane | As owner (phone hosts the ped) | As observer (desktop hosts it) |
|---|---|---|
| Spatial | `SendPedSync` follows `CPedSync::Update` + `WritePedInformation` (`CPedSync.cpp` 733-767, 924-1133): every 400 ms, flags only when the field changed, sync time context from `ENTITY_ADD`. Not yet run on a device. | `HandlePedSync` (PacketHandler.cpp 294-420) reads every field; observer interpolation through `PedSyncState`. Works. |
| Walk animation | Not sent: the phone is pinned to bitstream `0x0030`, below `0x3C`. Desktop observers get position only, so the ped glides. | Not received for the same reason. `SetPedNativeTaskLocomotion` only stores the mode (PedNativeTaskState.cpp 47). |
| Health, armour | Sent as raw floats when they change (`0x08` / `0x10`), read from the body at +0x6AC / +0x6B4. Not yet run on a device. | `LockPedHealth` stores the value. Each game tick writes it to `CPed` +0x6AC / +0x6B4, as `CClientPed::StreamedInPulse` does (`CClientPed.cpp` 4738-4748). A packet with health and no position still locks (`CPedSync.cpp` 439-442). Dead-to-alive `ReCreateModel` logs only. Not yet run on a device. |
| Death | `PED_WASTED` 64 is never sent. | 64 is not dispatched in `PacketHandler.cpp`; a dead ped keeps standing until `ENTITY_REMOVE`. |
| Gunshot hit | `HandleBulletSync` only damages the local player (`CombatSync.cpp` 180); `ShowRemoteTracer` only logs. A desktop player's shot never reaches the phone-owned ped, so it cannot be hurt. | The phone's own shots go out as packet 98 and the desktop owner replays them. Works since the `DoBulletImpact` end-point fix (08749ef); not yet verified against a ped. |
| Melee hit | `StartRemoteMelee` only logs (RemoteCombat.cpp 39). `PresentOnFoot` plays a punch animation on the attacker's model; whether it hits the ped is not verified. | The phone's fire key goes out in keysync; the desktop owner makes the phone's model punch. Not verified. |
| Behaviour | `onClientPedDamage`, `onClientPedWasted`, `onClientPlayerWeaponFire` are never fired by the phone VM, so the resource cannot report or react. `addPedNativeDamageEvent` and friends are polyfilled as no-ops. | Same: the phone cannot report what it observes. |
| Mover | Two movers fight: the phone's `setPedWander` task and the server's `stepMobileTrafficPeds` (server.lua 1585-1624), which calls `setElementPosition` + `setPedAnimation` on the server every step. Those are RPCs to **every** client, so desktop players see the ped teleported each step. `setPedControlState`, `setPedRotation`, `setPedAnimation` are stubs on the phone, so `MobilePuppet` does nothing. | Not affected. |

### 4.4 Correction plan

Each phase ends with a test that needs **two clients on the same server: one phone and one desktop**, with the ped
owned once by each. The owner of a ped is visible in the server log line `assign id=... owner=...`.

**Phase 0: spawn handshake**
- Done: the four stub bindings are removed (`LuaVMStub.cpp`, `LuaApiExtra.cpp`). Host-checked only.
- Done: spawn height. `getGroundPosition` follows `CLuaWorldDefs::GetGroundPosition` (`CLuaWorldDefs.cpp` 872-893)
  and `CWorldSA::FindGroundZFor3DPosition` (`CWorldSA.cpp` 708-729): three numbers, then
  `CWorld::FindGroundZFor3DCoord(x, y, z, nullptr, nullptr)` on the game thread (`WorldRPG_NoStoryScript`).
  libGTASA 2.10 null-checks both pointers and returns 0 when the line misses. A bad argument returns false.
  If the symbol is missing or the game thread does not run the call, the phone logs `DIAG` and returns false;
  `native-ped-traffic` `client.lua` 42-43 then keeps the player's Z. Not yet run on a device.
- Open, needs a phone on the server (not a client code change): server log `PASS mobile-fallback-ready`,
  `PASS mobile-fallback-profile`, `population-world-mobile-ready`; phone log `DIAG ped actor`. Fix whatever
  event argument fails next in `candidate`, `visibilityProbeResult`, `evidence`.
- Open, needs a phone: `native-vehicle-traffic` now treats every spot as visible and vetoes spawns closer than 30 m
  (its client.lua 846-877). Confirm car density did not drop.
- Done: capacity. `ServerWorld::CopyActors` used to copy 96 peds and cars into one buffer, and ped bodies
  stay capped at 48, so a full car list could be the whole copy. Desktop streams them apart
  (`CClientManager.cpp` 27-28): peds go through the player streamer (`CClientPed.cpp` 591-604) and stop at
  110 (`CClientPlayerManager::IsPlayerLimitReached`, `CClientPlayerManager.cpp` 172-174; `MAX_PEDS_MTA`,
  `Common.h` 17); vehicles use their own streamer (`CClientVehicle.cpp` 81-82) and stop at 64
  (`CClientVehicleManager::IsVehicleLimitReached`, `CClientVehicleManager.cpp` 780-784; `MAX_VEHICLES_MTA`,
  `Common.h` 16). The phone stays at 48 for each. Peds are copied first, up to 48, then vehicles up to 48.
  `getElementsByType("ped")` and `("vehicle")` copy that kind only (`CLuaElementDefs::GetElementsByType`,
  `CLuaElementDefs.cpp` 798-848; still 96, and the start-element and streamed-in arguments are not read).
  Not yet run on a device.

**Phase 1: one mover per ped (server resource + phone)**
- Server resource (outside this repo, needs the server owner): stop `stepMobileTrafficPeds` from calling
  `setElementPosition` / `setPedAnimation` on the server. A server write is broadcast to every client and fights the
  owner. The owner must move the ped and report it through `PED_SYNC`, as desktop owners do.
- Phone: make the owner's movement real. Either keep `setPedWander` (already a real `CTaskComplexWanderStandard`) or bind
  `setPedControlState` / `setPedRotation` for real so `MobilePuppet` works. Pick one; the plan assumes `setPedWander`.
- Test: phone owns a ped; on the desktop the ped walks a smooth path with no jumps; the server's position matches.

**Phase 2: owner `PED_SYNC` as the desktop writes it**
- Done: `SendPedSync` follows `CPedSync::DoPulse` (`CPedSync.cpp` 125-165), `Update` (733-767) and
  `WritePedInformation` (924-1133). One packet every 400 ms (`CTickRateSettings.h` 19). A ped is written only
  when a flag is set. Position, rotation and velocity use `CVector`'s `FLOAT_EPSILON` (0.0001) and an exact
  rotation compare. Position is `SPositionSync(false)`, rotation `SPedRotationSync`, velocity `SVelocitySync`,
  health and armour raw floats. The sync time context is the `ENTITY_ADD` byte (`CPacketHandler.cpp` 4420).
  Health and armour are read from the live body at +0x6AC and +0x6B4 (the offsets `SampleLocalPose` already
  checked against `RenderHealthBar` / `RenderArmorBar`). The first sample is sent even when the value is 0,
  because desktop's first compare is against an uninitialized `SLastSyncedPedData`.
- Left clear, with one `DIAG` line, because the read is not hooked: camera `flags2 0x01`
  (`GetCameraRotation`; `TheCamera` yaw), on fire `0x20` (`CPedSA::IsOnFire`, `pFireOnPed`, `CPedSA.h` 488),
  in water `0x40` (`CPedSA::IsInWater`, `CPedSA.cpp` 818-821), reloading `0x60`, synced-animation kill `0x80`
  (`HasSyncedAnim`). `fCurrentRotation` is not located, so rotation stays the matrix heading `ReadBodyPos`
  samples. Later `SetSyncTimeContext` calls (`CPedRPCs`, `CElementRPCs`) are not applied. The tick-rate RPC
  is not read, so the interval stays 400 ms. Locomotion and weapon blocks stay off: the phone is bitstream
  `0x0030`, and those flags need `0x3C` and `0x3D` (`bitstream.h`). An empty packet is not sent
  (`CPedSyncPacket::Read` fails and `CPacketTranslator.cpp` 251-254 drops it). Not yet run on a device.
- Open, needs a phone and a desktop on the server (after phase 4): a desktop player damages a phone-owned ped;
  the desktop shows the same health (`getElementHealth` in a debug script) within one sync.

**Phase 3: walk animation for observers (bitstream decision)**
- Desktop observers only animate a walking ped when they receive the locomotion lane (`flags2 0x02`, bitstream `0x3C`).
  At `0x0030` they get positions only. Two options:
  - A. Raise the phone's negotiated bitstream to `0x3C` for ped sync. This needs every `0x31`-`0x3C` wire change ported
    first (extended world positions at `0x31`, PED STARTSYNC camera rotation and physical takeover, the native-world
    packs). Large, but it is the parity path.
  - B. Keep `0x0030` and accept gliding peds on desktop for phone-owned peds; say so in the resource.
  - Recommendation: B now, A as its own project. Phase 3 then only adds the observer side that works at `0x0030`: drive
    the walk/run animation on the phone from the interpolated speed (`CPed::SetMoveState`, already used by
    `PresentOnFoot`), so desktop-owned peds do not glide on the phone.
- Test: desktop owns a walking ped; on the phone it walks with the walk cycle, not gliding.

**Phase 4: hits on a phone-owned ped**
- Bullets: in `HandleBulletSync`, replay every accepted remote shot into the phone's world, as desktop
  `CClientPlayer::DischargeWeapon` does: fire the shooter's weapon from `start` to `end` so GTA's `CWeapon` code hits the
  ped and raises its damage event. Then `ShowRemoteTracer` becomes real too. Keep the local-player path as it is.
- Melee: make the remote attacker's punch a real fight task (`StartRemoteMelee`, desktop keysync replay), so GTA's
  strike sphere hits the ped on the owner. Verify whether the current `PresentOnFoot` punch already damages.
- Damage authority: the observer lock is written each game tick (`CClientPed::StreamedInPulse`, `CClientPed.cpp` 4738-4748).
  Damage is still applied only when the phone owns the ped. The dead-to-alive recreate logs only.
- Test (both directions): desktop shoots / punches a phone-owned ped, and phone shoots / punches a desktop-owned ped.
  Health drops on the owner and matches on the other client.

**Phase 5: death**
- Owner side: when a phone-owned ped's health reaches 0 with health above 0 before, send `PED_WASTED` 64 exactly as
  `SendPedWastedPacket`: compressed anim group and id, damager id, weapon, body part, position, ped id, ammo.
- Observer side: dispatch 64 in `PacketHandler.cpp` (desktop `CPacketHandler::Packet_PedWasted`) and kill the body with
  the given animation; stop applying sync to it.
- Test: kill a ped owned by each client; it dies once, on both screens, with the same animation, and the server fires
  `onPedWasted` once.

**Phase 6: behaviour events for the resource**
- Fire `onClientPedDamage(attacker, weapon, bodypart)` and `onClientPedWasted` from the phone's damage hook, and
  `onClientPlayerWeaponFire` from the bullet hook, so the resource's `damageObserved` / `gunAimObserved` relays work.
- Replace the no-op polyfills by real natives in order of effect: `addPedNativeDamageEvent` (flee/cower on the owner),
  `addPedNativeGunAimedAtEvent`, `killPedTask`.
- Test: a desktop player aims at and hits a phone-owned ped; it flees or cowers on both screens.

**Phase 7: visibility and remaining stubs**
- Port desktop `CGameSA::IsAmbientPedSphereVisible` (camera sphere test) and bind `isAmbientPedSphereVisible` and
  `isElementOnScreen` for real (no pop-in, no removal in view).
- Bind `getElementDimension`, `getElementInterior`, `isPedDead`, `getElementModel` from the element table and the body.

**Phase 8: return to the desktop path**
- Port the desktop population natives (`GetAmbientPedPopulationProfile`, `GetAmbientPedSpawnCandidate` on path nodes,
  zone state, model residency) and bind the four names again. The phone then leaves the mobile fallback and peds
  spawn on pavements with GTA's zone models. Server log shows `population-world-ready` instead of the mobile line.

### 4.5 Test matrix (run after each phase)

| # | Owner | Action by | Expected on both clients |
|---|---|---|---|
| T1 | phone | nobody | ped walks smoothly, same place (±1 m) |
| T2 | desktop | nobody | same |
| T3 | phone | desktop shoots | health drops on both, ped reacts |
| T4 | phone | desktop punches | same |
| T5 | desktop | phone shoots | same |
| T6 | desktop | phone punches | same |
| T7 | each | kill | one death, same animation, one `onPedWasted` |
| T8 | phone | phone disconnects | ownership moves to the desktop with no teleport (`PED_STOPSYNC` / `STARTSYNC` handoff) |

### 4.6 Spawn transform (compared 2026-11-13)

Desktop `Packet_EntityAdd` for a ped calls `SetPosition` then `SetCurrentRotation(rotation, true)` (`CPacketHandler.cpp` 4054-4056). `SetPosition` (`CClientPed.cpp` 1064-1098) writes that Z with no ground offset. `SetCurrentRotation` (`CClientPed.cpp` 5298-5321) stores `fCurrentRotation` and `fTargetRotation` and puts a Z-only `CMatrix::SetRotation` on the element (`CMatrix.h` 253-279): right `(cos, sin, 0)`, forward `(-sin, cos, 0)`, up `(0, 0, 1)`. `_CreateModel` copies that matrix onto the new `CPlayerPed` (`CClientPed.cpp` 5772-5776). For a remote ped that is not the syncer, `ArmRemoteStreamInTransformFence` (`CClientPed.cpp` 5914-5945) then sets `bIsStaticWaitingForCollision`, freezes the physical, and calls `CColStore::RequestCollision`. The hold lasts at least 100 ms and at most 1500 ms (`CClientPed.cpp` 62-63).

The phone was writing an RwMatrix into `CPlaceable::SetMatrix` (libGTASA 2.10 `0x4ebf5c`): the second row was treated as up and the third as forward. `up.z` at `+0x28` was 0, so the game kept a full matrix with forward along Z. The ped lay through the pavement. `CPlaceable::SetHeading` is not in this library, so the later heading call did nothing. `fCurrentRotation` is `CPed` +0x6C4 (`CPed::UpdatePosition` `0x597b3c` passes it to `CMatrix::SetRotateZOnly` at `0x537958`, which is the same upright yaw). `CPed::TurnBody` (`0x59dd58`) keeps the desired heading at +0x6C8.

The phone now writes that upright matrix and those two floats, and uses the network Z. A new body sets `bIsStaticWaitingForCollision` (bit 18 of the flag word at +0x28; `CEntity::SetIsStatic` at `0x33ad14` writes bit 2 of the same word) and calls `CColStore::RequestCollision` with the `ENTITY_ADD` interior, until `HasCollisionLoaded` or 1500 ms. `CPhysicalSA::SetFrozen` (`CPhysicalSA.cpp` 389-396) sets `bDontApplySpeed` (bit 13) and `bDisableFriction` (bit 2) in the physical flag word at +0x64, using the mass float at +0xB0 (`PHYSICAL_MAXMASS` 99999). The hold clears those bits when it releases, because `UpdateRemoteReplicaPhysicsFence` (`CClientPed.cpp` 7140-7152) is not ported and this fence also covers a hosted ped. `CClientPed::SetInterior` (`CClientPed.cpp` 1101-1119) writes the area byte at +0x4B. `onClientElementInteriorChange` and the local-player `CWorld::SetCurrentArea` log only. Not yet run on a device.
