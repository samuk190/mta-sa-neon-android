# MTA SA ANDROID ( for San Andreas Online/MTA NEON )

[![Discord](https://img.shields.io/badge/Discord-Join%20Community-5865F2?style=for-the-badge&logo=discord&logoColor=white)](https://discord.gg/DKrpU3e4pF)

A high-performance, native ARM64 (aarch64) Android client port of **Multi Theft Auto: San Andreas** (MTA:SA) powered by the MTA Neon architecture and Grand Theft Auto: San Andreas Mobile engine.

---

## Important Notice & Prerequisites

> [!IMPORTANT]
> **LEGALLY OWNED GTA SAN ANDREAS MOBILE v2.10 REQUIRED**
>
> This repository contains **NO proprietary assets, game data, or copyrighted binaries** from Rockstar Games or War Drum Studios.
>
> To compile, run, and play this project, **you MUST provide your own original, legally owned copy of GTA San Andreas Mobile v2.10** for Android (ARM64 / aarch64).

---

## Architecture Highlights

- **Platform:** Android 8.0+ (API level 26 minimum, Target API 34).
- **ABI:** `arm64-v8a` (aarch64) exclusively.
- **Native Engine:** C++20 standard, ARM NEON acceleration, custom Lua 5.3 runtime, Opus audio codec, and RenderWare graphics pipeline.
- **Networking:** MTA Neon transport protocol with native handshake, cipher, and bitstream serialization.

---

## Current Status & Feature Checklist

- [x] **Game Log-in** — Working (handshake, OPEN51 datagram, cipher negotiation, and session init)
- [x] **Character Sync** — Working *(with bugs)* (remote `CPlayerPed` creation, pure sync, keysync, health updates)
- [x] **Peds** — Working *(with bugs)* (`ENTITY_ADD`, `PED_SYNC`, wander tasks; pathfinding and collision tuning in progress)
- [x] **Sound / Chat** — Working (Opus voice stream, text chat send/receive, and on-screen overlay)

---

## Server Setup & Anti-Cheat Notice

### Compatible Server Engine
This mobile client is designed to connect to servers running the **MTA Neon** / **San Andreas Online** architecture:
- **Server Repository:** [Dryxio/san-andreas-online](https://github.com/Dryxio/san-andreas-online)

To set up and host your own compatible game server, clone and build the San Andreas Online server repository following its instructions.

> [!WARNING]
> **MTA Anti-Cheat Compatibility Notice**
>
> Standard Multi Theft Auto proprietary Anti-Cheat (`net.dll` / MTA AC server checks) **does NOT work** with this mobile client.
>
> Because this client uses a custom packet architecture, mobile-tailored bitstreams, and an ARM64-optimized native cipher for network transport, traditional desktop MTA anti-cheat modules will reject the connection. The server must be run using [San Andreas Online (MTA Neon)](https://github.com/Dryxio/san-andreas-online) or have standard anti-cheat verification modules disabled.

---

## Project Roadmap: MTA Sync Porting Plan (PC to Android)

> Blueprint: [`docs/MTA_SYNC_ANDROID_PORT.md`](docs/MTA_SYNC_ANDROID_PORT.md) and the stage specs in [`docs/sync-port/`](docs/sync-port/MASTER_PLAN.md).
> Reference for behavior, names and wire layouts: the desktop client `mta_neon_source` (`Client/mods/deathmatch/logic/CPacketHandler.cpp`, `CPedSync.cpp`, `CClientPed.cpp`, `CNetAPI.cpp`, `CUnoccupiedVehicleSync.cpp`) and `Shared/sdk/net/` (`Packets.h`, `SyncStructures.h`, `bitstream.h`).
> Peer bitstream: the phone is pinned to `0x0030`. Items that only exist from `0x0031` or `0x003D` are marked. Verified on device means logged on a real phone against the live server.

**Legend:** `[x]` verified on device. `[~]` implemented in code, not verified on device. `[ ]` not started or log-only.

### 0. Session, join and world state (prerequisite)
- [x] **Join handshake** (`NeonJoinProtocol`): RakNet connect, `MOD_NAME`, `PLAYER_JOINDATA`, `PLAYER_INGAME_NOTICE`, `SERVER_JOINEDGAME` (reference `CNetAPI`, join path).
- [x] **Puresync out** (`PLAYER_PURESYNC` 32 sent by the phone).
- [~] **Puresync in** (`PLAYER_PURESYNC` 32 from others): normal-vector encoding matches `ReadMtaNorm`; not re-checked against a second player on device.
- [x] **Element list at join** (`ENTITY_ADD` 50, all join packets store fully: 4/4, 84/84, 41/41).
- [~] **Element streaming after join**: `ENTITY_ADD` for elements created while connected. Not observed; server creates civilian peds for the account but none reach the phone (open).
- [~] **Element removal**: `ENTITY_REMOVE` not handled on the phone; `ForgetRemote` not wired (open).
- [~] **Element data** (`SET_ELEMENT_DATA` family): not verified on device.
- [ ] **Sync settings** (`SYNC_SETTINGS` 99, seen as unhandled): read and apply server sync rates.
- [ ] **Map info** (`MAP_INFO` 46, seen as unhandled): read and apply server map state.
- [ ] **Server info** (`SERVER_INFO_SYNC` 106, seen as unhandled).
- [ ] **Return sync** (`RETURN_SYNC` 36, the server's replies to our sync, 160 packets in one session, unhandled): decode and use for latency and acknowledgement.
- [ ] **Lua event packets** (`LUA` 76 and 77, `triggerServerEvent` 81, seen): full bidirectional event dispatch with element arguments.
- [~] **Chat echo** (`CHAT_ECHO` 43): mobile chat strip (drop ClientID and msgType) is on the server; the phone display path is not verified.

### 1. Step A — Remote player as `CPlayerPed` & vehicle entry/exit/jack
- [ ] **Native `CPlayerPed` instantiation** for remote players (`CPlayerPedSA`, `CPed::operator new`, `CPlayerPed(1, false)`, matrix copy).
- [ ] **Remove civilian mid-air crash** (`CPopulation::AddPed` / `CCivilianPed` path, `CTaskComplexInAirAndLand`).
- [ ] **Driver entry**: `CTaskComplexEnterCarAsDriver`.
- [ ] **Carjack**: drag-out subtask when the driver seat is occupied; `VEHICLE_INOUT` 58.
- [ ] **Passenger entry**: `CTaskComplexEnterCarAsPassenger`.
- [ ] **Exit**: `CTaskComplexLeaveCar`.
- [ ] **Warp seating on stream-in**: `CTaskSimpleCarSetPedInAsDriver` / `CTaskSimpleCarSetPedInAsPassenger`.
- [~] **Ped occupancy from `ENTITY_ADD`** (type 14 vehicle and seat fields, S2): parsed, not bound to the vehicle.

### 2. Step B — Pedestrian host, wander sync and interpolation
- [x] **`PED_STARTSYNC` (61) parse** at the 305-bit mobile layout (ped 182 at 1706, -1806, 14, observed).
- [~] **Start-sync apply**: sets the ped as hosted and calls the wander hook (`SetPedWander` logs; the GTA wander task is not started).
- [ ] **Host wander task**: `CTaskComplexWanderStandard` (`0x48E4F0`) via `CPedIntelligence::AddTaskPrimaryMaybeInGroup`.
- [ ] **Outgoing `PED_SYNC` (63) from the host**: publish position, rotation, velocity, health and camera deltas. Not observed (no `PED_SYNC` from the server for ped 182 either).
- [ ] **Incoming `PED_SYNC` (63) for observed peds**: parse at 0x0030 and apply.
- [~] **Observer interpolation** (`SetTargetPosition`, `SetTargetRotation`, `UpdateTargetPosition`, `GetOffsetRadians`): implemented in `PedInterpolator` and driven per frame from `ApplyRemotePeds`; not verified on device.
- [ ] **Sync termination** (`PED_STOPSYNC` 62): decoded and wired; wander removal not observed on device.
- [ ] **Sync time context** (`CanUpdateSync`): per-ped time context gate not ported.
- [ ] **Spatial sync rate** (`CPedSync.cpp:408-421`): ported as a clamp; not verified on device.
- [ ] **Ped natives used by the population client** (about 60 names, S-series Lua work): defined with desktop return types; defaults only, not engine-backed.
- [ ] **Ped clothes, weapons, alpha and walking style** from `ENTITY_ADD` type 14: parsed, not applied.

### 3. Step C — Occupied and unoccupied vehicle sync
- [~] **Vehicle `ENTITY_ADD` parse** (type 2) is implemented in S3; no vehicle element has reached the phone on device yet, so this is unverified.
- [~] **Occupied vehicle sync** `PLAYER_VEHICLE_PURESYNC` (33): decoders match `CNetAPI.cpp:1306-1610`; occupancy is not bound, so packets are ignored (`SetOccupant` has no caller). Engine apply is log-only.
- [~] **Unoccupied vehicle sync** `UNOCCUPIED_VEHICLE_SYNC` (56): decoder matches `CUnoccupiedVehicleSync.cpp:178-208`; engine apply is log-only (no `VehicleHandle` for remote cars).
- [ ] **Driver drives the vehicle**: target position, rotation, velocity and turn speed through `CClientVehicle::SetTargetPosition` (interpolated).
- [ ] **Passenger state**: controller, weapon aim and health from packet 33.
- [ ] **Hosted empty cars** publish deltas in outgoing packet 56 (not started on the phone).
- [ ] **Trailers, trains and aircraft look bits**: decoded, not applied.
- [~] **Vehicle normal vectors**: MTA encoding (`ReadMtaNorm`) now used for vehicles as well as players.

### 4. Step D — Combat, bullet sync and damage
- [~] **Player bullet sync** `PLAYER_BULLETSYNC` (98): decoder and duplicate filter match `CNetAPI.cpp:2318-2366`; tracers and discharge only log.
- [~] **Weapon bullet sync** `WEAPON_BULLETSYNC` (100): decoder matches `CNetAPI.cpp:2368-2389`; no weapon-type gate, log-only.
- [ ] **`DischargeWeapon` and tracer rendering** through the engine.
- [~] **Victim health from pure sync** (`SPlayerHealthSync`, 8 bits): read after velocity; the read order depends on the unverified puresync reader.
- [ ] **Authoritative damage registration**: victim's health from its own puresync, never fabricated on the shooter (rule enforced in code, engine effect not applied).
- [~] **Melee**: keysync `ButtonCircle` (0x10) detected; `StartRemoteMelee` logs only. The 8-damage fallback was removed; no replacement yet.
- [ ] **`CTaskSimpleFight`** for remote melee.
- [ ] **Keysync fields beyond buttons**: analog bytes, aim and camera (currently only the first button byte is used).

### 5. Step E — Scripting engine, touch UI and optimization
- [~] **Lua 5.3 client VM**: `addEventHandler`, `triggerServerEvent`, timers and about 60 ped natives defined; `getAmbientPedPopulationProfile` shim relies on them.
- [ ] **DirectX / RenderWare 2D GUI**: DX primitives, radar blips and HUD overlays for remote entities.
- [ ] **Touch control customization**: virtual joystick sensitivity and touch button mapping.
- [ ] **Custom asset streaming**: server-side DFF/TXD download and RenderWare memory streaming.
- [ ] **Engine optimization**: frame pacing, memory compaction and ARM64 anti-crash guardrails.
- [ ] **16 KB ELF alignment** for `libc++_shared.so` (Samsung compatibility warning on install; repo audit fails).

### 6. Verification and hygiene
- [ ] **Remove `DIAG` logging** from `staging` before any merge to `main` (kept for now; see `ServerWorld.cpp`, `PacketHandler.cpp`, `GameEngine.cpp`).
- [ ] **Host syntax checks** for every changed file in CI or a pre-merge script (currently manual).
- [ ] **Device acceptance per stage**: each stage's acceptance in `docs/sync-port/` run on a phone against the live server, with logs saved under `outputs/`.
- [ ] **Two-player test**: remote player puresync, vehicle sync and ped ownership between two phones or a phone and a desktop client.


---

## Game Assets & Folder Location

All game assets are configured locally via environment variables (`.env`) rather than downloading from external cloud repositories.

### 1. Configure `.env`

Copy the template file `.env.example` to `.env`:

```bash
cp .env.example .env
```

Edit `.env` to define your asset locations:

```env
# Launcher UI Customization
LAUNCHER_TITLE=MTA:SA Neon Android
LAUNCHER_PLAY_BUTTON_TEXT=Play

# Default Server Connection Settings
DEFAULT_SERVER_NAME=MTA Neon Server
DEFAULT_SERVER_IP=127.0.0.1
DEFAULT_SERVER_PORT=22003
DEFAULT_PLAYER_NICK=Player

# Path to your local extracted GTA San Andreas Mobile 2.10 assets
# You can use the repository's ./game_assets folder or specify an absolute path:
GAME_ASSETS_DIR=./game_assets

# Storage path on your Android device (where assets will be loaded from):
DEVICE_ASSETS_PATH=/sdcard/Android/data/com.worldrpg.neonlauncher/files/game_assets

# Optional: Custom CDN or Local HTTP Server URL if serving assets over network
CDN_ROOT_URL=
```

### 2. Assets Folder Structure

Place your extracted GTA San Andreas Mobile v2.10 files into the folder defined in `GAME_ASSETS_DIR` (by default `./game_assets/` in this repository):

```text
mta-sa-neon-android/
├── .env                              <-- Configured asset paths
├── game_assets/                      <-- Put your extracted GTA 2.10 files here
│   ├── anim/
│   │   └── anim.img
│   ├── audio/
│   │   ├── CONFIG/
│   │   └── streams/
│   ├── data/
│   │   ├── gta.dat                   <-- Required
│   │   ├── handling.cfg              <-- Required
│   │   ├── default.dat
│   │   ├── surface.dat
│   │   └── script/
│   │       └── mainV1.scm
│   ├── models/
│   │   └── coll/
│   ├── texdb/
│   │   ├── gta3.img                  <-- Required
│   │   ├── gta3.txt
│   │   ├── gta_int.img
│   │   └── menu/
│   │       ├── menu.pvr.dat          <-- Required
│   │       └── menu.pvr.toc
│   └── Text/
│       └── american.gxt
└── app/
    └── src/main/jniLibs/arm64-v8a/
        └── libGTASA.so               <-- Required (extract from APK)
```

### 3. Extracting `libGTASA.so`

1. Open your GTA SA Mobile 2.10 APK as a ZIP archive.
2. Navigate to `lib/arm64-v8a/`.
3. Copy `libGTASA.so` (and optional companion libraries `libSCAnd.so`, `libOpenAL64.so`) into:
   ```text
   app/src/main/jniLibs/arm64-v8a/libGTASA.so
   ```

### 4. Transferring Assets to Device

We provide an automated script to verify and push assets directly to a connected Android phone or emulator:

```bash
# Verify assets and push via ADB:
bash tools/sync_local_assets.sh

# Or via Gradle:
./gradlew pushAssetsToDevice
```

Alternatively, copy the contents of `game_assets/` to your device storage manually:
```bash
adb push game_assets/. /sdcard/Android/data/com.worldrpg.neonlauncher/files/game_assets/
```

---

## Building the Project

### Prerequisites

- **Android Studio** (Hedgehog, Iguana, Jellyfish or newer).
- **Android SDK** API 34.
- **NDK** `25.1.8937393`.
- **CMake** `3.22.1`.

### Build via Gradle CLI

```bash
# Debug build (generates APK in app/build/outputs/apk/debug/)
./gradlew assembleDebug

# Install directly to connected device
./gradlew installDebug
```

---

## Technical Documentation

Detailed architectural and protocol references are preserved in the `docs/` directory:

- [Protocol Reference](docs/PROTOCOL_REFERENCE.md)
- [Transport Engine & Handshake](docs/TRANSPORTE_NEON.md)
- [Network Packet Contract](docs/CONTRATO_TRANSPORTE_MTA.md)
- [Sync Engine Specifications](docs/MTA_SYNC_ANDROID_PORT.md)
- [Bootstrap Assets Breakdown](docs/BOOTSTRAP_ASSETS.md)

---

## Community & Support

Join our community to discuss development, report findings, test builds, and collaborate:

[![Discord](https://img.shields.io/badge/Discord-Join%20Community-5865F2?style=for-the-badge&logo=discord&logoColor=white)](https://discord.gg/DKrpU3e4pF)

👉 **Discord Server:** [https://discord.gg/DKrpU3e4pF](https://discord.gg/DKrpU3e4pF)

---

## Credits & Acknowledgments

This project is built upon decades of collective community research, open-source netcode development, and GTA engine reverse engineering. Special thanks and credits to:

- **[Multi Theft Auto (MTA)](https://multitheftauto.com)**: Huge credits and recognition to the **[Multi Theft Auto Team](https://github.com/multitheftauto/mtasa-blue)** for developing the premier open-source multiplayer modification for Grand Theft Auto: San Andreas. Their pioneering netcode architecture (`CNetAPI`, `CPlayerPedSA`, RakNet integration, synchronization algorithms, and element hierarchy) serves as the foundational blueprint for this client.
- **[San Andreas Online (MTA Neon)](https://github.com/Dryxio/san-andreas-online)**: Credits to [Dryxio](https://github.com/Dryxio) and contributors for the lightweight C++ server rewrite, clean packet definitions, and modernized multiplayer backend.
- **[The Lua Project](https://www.lua.org)**: For the embedded Lua 5.3 scripting engine powering client-side game logic and interfaces.
- **[Xiph.Org Foundation](https://opus-codec.org)**: For the low-latency Opus audio codec powering spatial voice chat.
- **Rockstar Games & War Drum Studios**: Original creators and mobile porters of Grand Theft Auto: San Andreas.

---

## License & Attribution

- **Multi Theft Auto (MTA)**: Netcode concepts and algorithms derived from [Multi Theft Auto: Blue](https://github.com/multitheftauto/mtasa-blue) are subject to GPLv3 / BSD licensing.
- **Lua 5.3**: Copyright © 1994–2020 Lua.org, PUC-Rio. Licensed under the [MIT License](https://www.lua.org/license.html).
- **Opus Codec**: Copyright © 2001–2011 Xiph.Org Foundation, Skype Limited. Licensed under the [BSD-3-Clause License](https://opus-codec.org/license/).
- **Rockstar Games**: Grand Theft Auto and Grand Theft Auto: San Andreas are registered trademarks of Rockstar Games / Take-Two Interactive.
- This open-source research and educational project is not affiliated with, endorsed by, or sponsored by Rockstar Games or Take-Two Interactive.
