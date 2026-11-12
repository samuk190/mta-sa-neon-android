# Stage S6 - Combat: bullet sync, damage registration, melee

Roadmap: Step D. Depends on S2 (ped bodies) and S5 (vehicle sync).

## Goal
- PLAYER_BULLETSYNC (98) and WEAPON_BULLETSYNC: decode and show tracers and discharge.
- Damage: health changes come from the victim's PLAYER_PURESYNC health (SPlayerHealthSync), not from the shooter.
- Melee: ButtonCircle keysync drives CTaskSimpleFight on the remote ped (names from the desktop client).

## Reference (authoritative)
- mta_neon_source/Client/mods/deathmatch/logic/CPacketHandler.cpp: Packet_PlayerBulletsync, Packet_WeaponBulletsync, the keysync melee path.
- mta_neon_source/Shared/sdk/net/Packets.h: PLAYER_BULLETSYNC, WEAPON_BULLETSYNC ids.

## Files you own
- app/src/main/cpp/shared/CombatSync.h and CombatSync.cpp (new): bullet decoders and damage registration.
- app/src/main/cpp/shared/PacketHandler.cpp: handler registration for the bullet packets only.
- app/src/main/cpp/gtasa/RemoteCombat.h and RemoteCombat.cpp (new): tracer and melee engine hooks.

Do not edit ServerWorld.cpp, EntityAddCore.cpp, LuaVMStub.cpp, or any vehicle or ped-sync file.

## Work items
1. Port the bullet packet read orders exactly.
2. Damage: store the victim's health from PLAYER_PURESYNC; do not fabricate victim health on the shooter.
3. Melee: a remote ped with ButtonCircle keysync gets CTaskSimpleFight (engine hook).
4. DIAG logs on each failure path.

## Acceptance (device)
- DIAG lines for bullet packets appear when shots happen nearby.
- Hits reduce the victim's health on both sides.

## Host check
g++ -std=c++20 -fsyntax-only -Wall -I app/src/main/cpp <each changed .cpp>
