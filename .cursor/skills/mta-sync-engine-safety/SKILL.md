---
name: mta-sync-engine-safety
description: >-
  Use when editing Android player, ped, or vehicle sync in mta-sa-contribution
  (GameEngine.cpp, PlayerPedPort.cpp, RemoteVehicle.cpp, PacketHandler.cpp,
  VehicleSync.cpp, PedSyncState.cpp, NeonJoinProtocol.cpp, entity/EntityAdd*),
  or when a sync change is followed by SIGSEGV, SIGBUS, sortLODs,
  CWorld::Remove, CTaskComplexInAirAndLand, or RwEngineStart.
---

# Sync edits that crash libGTASA

## Overview

A sync change crashes the engine when it writes a live `CPed`, `CVehicle`, task, or matrix. The tombstone names a GTA function, not the edited line. `AGENTS.md` requires a faithful desktop port. This skill is the crash gate on that port.

Read this file before editing. The description only says when to open it.

## When to use

Open it before the first edit to the files in the description, and again when logcat shows a fault inside `libGTASA` after a sync change.

Chat, the transfer box, HUD widgets, Lua GUI, and asset loading crash for other reasons. Leave this skill unread for those.

## Before editing

1. Name the desktop function and read it in `mta_neon_source` (file and line range). Copy its read order, the widths in `SyncStructures.h`, and its calls, in that order. The phone bitstream stays `0x0030`. A desktop field the server omits for a mobile peer shifts every later field. Record that deviation in a comment that cites the device evidence.
2. Resolve every engine entry with `dlsym` on `libGTASA.so` (Itanium name; try `C1` and `C2`). A missing symbol is a `DIAG` line and a return. A GTA PC 1.0 US address is not an ARM64 address.
3. Patch engine code only when the 32-bit word at the site is the expected instruction. `EnsurePlayerPedGroupPatch` requires `0x370005d4` at `CPlayerPed` ctor `+0x9c` before writing the branch. Any other word means the ped is not created.
4. Create a remote ped with `CPed::operator new` (pool stride `0x9c8`, size argument `0x998`) and `CPlayerPed(1, false)`, after the group patch. Model `0` is the local CJ clump. Spawn after the model has streamed, at most one new ped body per game frame, cap 48. `CCivilianPed` plus `Teleport` runs `CTaskComplexInAirAndLand` with a null task.
5. Create a remote vehicle with `CCarCtrl::CreateCarForScript` (the pool). Cap 48, counted apart from peds. Leave the `0x2000` heap `CAutomobile` fallback untouched.
6. Clear the seat before destroying a body. When the vtable is `CPlaceable` (`_ZTV10CPlaceable+16`), skip `CWorld::Remove` and the game destructor. `Remove` calls vtable `+0x20`; on that vtable the slot is `MI_CRANE_HARNESS` and the branch is a `SIGBUS`.
7. Orientation is a `CMatrix`: right, forward, up, position at `matrix+0x30`. An `RwMatrix` (up on the second row) lays the ped through the ground. Heading sync is `CPed+0x6C4` and `+0x6C8`. `CPlaceable::SetHeading` is absent from this `libGTASA`.
8. A new fixed-offset store needs a load and a store from libGTASA 2.10 arm64, both cited next to the write. Velocity is `CPhysical+0x68`. Health is `CPed+0x6AC`. The matrix pointer is `CPlaceable+0x18`. Armour at `+0x6B4` is the float `RenderArmorBar` (`0x37d93c`) loads, and `SampleLocalPose` notes that `AccountForPedArmour` (`0x444044`) writes a `CPed*` at the same offset. Add no new store there until both sites are disassembled. PC armour `+0x548` is the wrong slot.
9. Run constructors, `CWorld::Add`, `CWorld::Remove`, `SetMatrix`, `ProcessPed`, and tasks on the game thread (`WorldRPG_NoStoryScript`). The network thread only queues under `g_remoteMutex`.
10. Start wander, fight, or drive only when the desktop function calls that task. Place a hosted ped once and let the task integrate. Interpolate an observer. Keep `Teleport` off the per-tick path. Leave the vehicle the local ped is driving on the game's own integrator.
11. Keep `InstallGameHeap` installed. `noheap` is the switch that brings the RenderWare crash back. Model and collision allocations from a spawn stay inside that heap.

## After editing

- Prove the packet with `tools/headless` before the phone. That target replaces `GameEngine.cpp` with `game_script.cpp`. It catches a bad bitstream. An engine crash still requires the phone.
- On device, keep the fault address, the `libGTASA` base, and the last ped or vehicle `DIAG`. Map the fault to a symbol, then to the last apply in `ApplyRemotePeds` or `ApplyRemoteSeats`. A successful compile is not acceptance.
- Rebuild with `./gradlew :app:assembleDebug` and no `clean`. One changed `.cpp` recompiles and relinks `libWorldRPGNeon.so`. The new library applies on the next process start.
- To test whether remote apply is the fault, restart once with the word `noremote` in `/data/data/com.worldrpg.neonlauncher/files/neon_debug_flags`. The file is read once per process. The other words are `noheap`, `nolua`, `noresources`, `nodx`, and `notrigger`.

## Common misses

| Edit that looks local | What the next frame does |
| --- | --- |
| Extra bitstream field at `0x0030` | Later floats land in position or health; physics faults away from the reader |
| `RwMatrix` passed to `SetMatrix` | Ped pitched into the ground, then a collision fault |
| Heap `CAutomobile` | `sortLODs` / `emu_ArraysGetID` |
| Ninth `CPlayerPed` without the group patch | Ped-group pool fault |
| `CWorld::Remove` on a `CPlaceable` vtable | `SIGBUS` at `MI_CRANE_HARNESS` (`0x8862d6`) |
| Task added so the ped "looks alive" | Null subtask, or the 0.35 blend fighting `ProcessControl` |
| Tombstone inside the renderer | The last spawn or matrix write is still the suspect |

Device dates and the seven mechanisms are in [crashes.md](crashes.md). Read it when a tombstone is already in hand, or before adding a fixed-offset store.

## Changelog

Each converted function still gets one `CHANGELOG.md` line with the desktop file and line range. Say the device has not run it until it has.
