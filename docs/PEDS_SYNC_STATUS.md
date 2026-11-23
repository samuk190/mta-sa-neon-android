# Non-traffic ped sync status

Remote players and script/synced peds. Native ped traffic (`native-ped-traffic`) is a separate system; its status stays in [`PEDS_TRAFFIC_SYNC_GAPS.md`](PEDS_TRAFFIC_SYNC_GAPS.md).

## Open

After the work below was installed, non-traffic peds are still bugged: they do not move. This pass did not verify a cause. Earlier, the same peds spawned half in the ground.

Two pieces are still unfinished. This pass did not show that either one is why they do not move:

- Position still uses the 0.35 blend. `CClientPed::UpdateTargetPosition` (`CClientPed.cpp` 8657-8822) is not wired (`ApplyRemotePeds` in `gtasa/GameEngine.cpp`).
- `UpdateRemoteReplicaPhysicsFence` (`CClientPed.cpp` 7140-7152) is not ported (`HoldPedCollisionFence` in `gtasa/GameEngine.cpp`).

## Already in the tree

### Upright yaw

`PlaceRemotePlayerPed` / `SetRemotePedHeading` (`gtasa/PlayerPedPort.cpp`). The phone had stored a pitched matrix (forward straight up), so bodies lay through the pavement. It now writes the desktop Z-only yaw: right `(cos, sin, 0)`, forward `(-sin, cos, 0)`, up `(0, 0, 1)`, the network Z, and `fCurrentRotation` / `fTargetRotation` at `CPed` +0x6C4 / +0x6C8.

Desktop: `CClientPed::SetCurrentRotation` (`CClientPed.cpp` 5298-5321), `_CreateModel` (`CClientPed.cpp` 5772-5776), `Packet_EntityAdd` `SetPosition` (`CPacketHandler.cpp` 4054, `CClientPed::SetPosition` 1064-1098).

A new body is held until collision is loaded or 1500 ms (`ArmPedCollisionFence` / `HoldPedCollisionFence`; minimum 100 ms; `CClientPed.cpp` 62-63 and 5914-5945). The hold sets `bIsStaticWaitingForCollision` and calls `CColStore::RequestCollision`.

### Freeze and interior

`SetPhysicalFrozen` writes `bDontApplySpeed` and `bDisableFriction` (`CPhysicalSA::SetFrozen`; flag word at +0x64, mass at +0xB0). `ApplyPedInterior` writes the area byte at +0x4B (`CClientPed::SetInterior`, `CClientPed.cpp` 1101-1119). The element-interior event and `CWorld::SetCurrentArea` log only. The hold clears the freeze bits on release. `UpdateRemoteReplicaPhysicsFence` is not ported.

### Locked health and armor

`HandlePedSync` locks health and armor when those flags are set (`CPedSync::Packet_PedSync`, including a packet with no position, `CPedSync.cpp` 439-442). `ApplySyncedPlayerFields` writes a locked health and armor to `CPed` +0x6AC / +0x6B4 on each game tick (`CClientPed::StreamedInPulse`, `CClientPed.cpp` 4738-4748). The dead-to-alive `ReCreateModel` path logs only.

### On-foot player sync

`SampleLocalPadAndCamera` reads the stick from `CPad::GetPad(0)`. `CopyOnFoot` sends a released stick as 0. `SendPureSync` leaves the walk bit clear on that release and writes one zero `SVelocitySync` on the falling edge. Camera yaw is `GetCameraRotation` (`TheCamera` +0x154). `HandlePlayerPureSync` / `NoteRemotePlayerSync` and `HandleKeySync` / `NoteRemoteAim` apply stick, speed, health, armor, and heading. Position still uses the 0.35 blend because `CClientPed::UpdateTargetPosition` is not wired.

### Join

`NeonJoinClient::ProcessGamePacket` consumes inbound packets 31–36, 54–56, 61–63, 98, and 100 in `joindata-sent` and `ingame-notice-sent` and does not fail the session (`IgnoredInboundSyncWhileJoining` in `net/NeonJoinProtocol.cpp`). The body is not applied until join completes.
