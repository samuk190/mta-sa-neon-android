# Stage S2 - Peds (ENTITY_ADD type 14 and PED_SYNC)

Starts after S1 is merged into `staging` (S1 creates the stub `EntityAddPed.cpp` this stage replaces).

## Goal

Server peds (ENTITY_ADD type 14) parse, get stored, and spawn on the phone. The ped sync packet (`PED_SYNC`, game 63, wire 162) parses at the `0x0030` layout, for both the native-task-aware and non-native-task paths.

## Files you own

- `app/src/main/cpp/shared/entity/EntityAddPed.cpp` (replace the stub body)
- `app/src/main/cpp/shared/PacketHandler.cpp` function `HandlePedSync` only
- NEW `app/src/main/cpp/shared/entity/PedWire.h` if you need shared helpers

Do not edit `ServerWorld.cpp`, `EntityAddCore.cpp`, or any vehicle/object file.

## Reference

- `CPacketHandler.cpp` `case CClientGame::PED:` (around line 3196 onward, in `Packet_EntityAdd`).
- `CPedSync.cpp` `Packet_PedSync` (client, around line 289 onward), for the PED_SYNC layout.
- `SyncStructures.h`: `SPedRotationSync` (line ~638), `SPlayerHealthSync` (8-bit float), `SPlayerArmorSync` (8-bit float), `SOccupiedSeatSync`, `SEntityAlphaSync`, `SPositionSync`.

## Work items

1. **ENTITY_ADD type 14**, in the reference order:
   - Position (`Read(&position)`).
   - Model: `ReadCompressed(usModel)` (ushort). The reference validates it with `ResolveModelID` and `IsValidModel`; we cannot resolve against the game, so store the raw model and log it. Use `DIAG` to log the model.
   - Rotation: `SPedRotationSync` (16 bits, `SFloatAsBitsSync<16>(-pi, pi)`).
   - Health: `SPlayerHealthSync` (8 bits). Armor: `SPlayerArmorSync` (8 bits).
   - Vehicle bit; if set, vehicle ElementID and `SOccupiedSeatSync`.
   - Flags: jetpack, synced, headless, frozen (4 single bits).
   - Alpha: `SEntityAlphaSync`.
   - Move anim: uchar (8 bits).
   - Clothes: follow the reference exactly (count, then per clothing item, texture and model names and slot). Our current `ReadPedTail` reads clothes count as one byte and skips names; verify against the reference loop and fix any width.
   - Continue to the end of the reference PED case: anything after the move anim that the reference reads (walking style, stat, etc.). Port it all. Do not stop early.
2. **Store the ped**: keep the existing `Visual` fields (`x`, `y`, `z`, `heading`, `model`, health, armor). Log `DIAG ped stored id=... model=... pos=...`.
3. **PED_SYNC** (`HandlePedSync`): read the layout the reference `Packet_PedSync` reads for the current peer version. At `0x0030` there are no native-task blocks, and the existing `HandlePedSync` already matches that. Check: ID, sync time context (byte, only at `0x0031+`), flags, optional position (24-bit fixed and Z), heading, velocity (`ReadMtaVelocity`), health, armor, camera rotation (flags2 bit 0), onfire and inwater bits. Fix any width that differs from the reference. Keep the native-task blocks gated as they are.

## Acceptance (device)

- `DIAG ped stored id=...` lines appear for server peds.
- `ENTITY_ADD` packets that contain peds finish with `stored N` equal to the element count.
- `DIAG PED_SYNC id=...` lines appear once peds are in range, and there are no `ped sync` parse breaks.
- Peds are visible in the world and stand still (wander and movement come in S4).

## Host check

```
g++ -std=c++20 -fsyntax-only -Wall -I app/src/main/cpp app/src/main/cpp/shared/entity/EntityAddPed.cpp
g++ -std=c++20 -fsyntax-only -Wall -I app/src/main/cpp app/src/main/cpp/shared/PacketHandler.cpp
```
