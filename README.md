# MTA SA ANDROID ( for San Andreas Online/MTA NEON )

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

> Implementation blueprint derived from [`docs/MTA_SYNC_ANDROID_PORT.md`](docs/MTA_SYNC_ANDROID_PORT.md).
> Translates desktop MTA:SA (Neon / Blue) `CPlayerPedSA`, `CNetAPI`, and `CPedSync` routines directly onto mobile `libGTASA` following the PC client order without inventing new wire protocols.

### 1. Step A — Remote Player as `CPlayerPed` & Vehicle Entry/Exit/Jack
- [ ] **Native `CPlayerPed` Instantiation**: Remote players instantiate as true `CPlayerPed` via `CPed::operator new` and `CPlayerPed(1, false)` with direct matrix copy, exactly as MTA desktop constructs them (`CPlayerPedSA`).
- [ ] **Eliminate Mid-Air Civilian Crash**: Remove civilian `CPopulation::AddPed` / `CCivilianPed`, preventing spawning in mid-air and eliminating the null-pointer crash in `CTaskComplexInAirAndLand::CreateFirstSubTask`.
- [ ] **Native Vehicle Entry/Exit Tasks**:
  - **Driver Entry**: Use native GTA task `CTaskComplexEnterCarAsDriver` (`_ZN28CTaskComplexEnterCarAsDriverC1EP8CVehicle`).
  - **Carjack**: When the driver seat is occupied, `CTaskComplexEnterCarAsDriver` natively spawns the drag-out subtask, setting `m_bIsJackingVehicle` on packet 58 (`VEHICLE_INOUT`).
  - **Passenger Entry**: Use native `CTaskComplexEnterCarAsPassenger` (`_ZN31CTaskComplexEnterCarAsPassengerC1EP8CVehicleib`).
  - **Exit**: Use native `CTaskComplexLeaveCar` (`_ZN19CTaskComplexLeaveCarC1EP8CVehicleiibb`).
  - **Warp Seating**: Instant warp on stream-in via `CTaskSimpleCarSetPedInAsDriver` / `CTaskSimpleCarSetPedInAsPassenger`.

### 2. Step B — Pedestrian Host & Wander Sync (`CTaskComplexWanderStandard`)
- [ ] **Host Wander Task**: The phone acting as ped host (`PED_STARTSYNC` ID 61) runs native `CTaskComplexWanderStandard` (`0x48E4F0`) added via `CPedIntelligence::AddTaskPrimaryMaybeInGroup`.
- [ ] **Eliminate Tick Teleports**: The host does not teleport the ped every tick; the GTA engine simulates the task naturally, and the host publishes position deltas via outgoing `PED_SYNC` (ID 63).
- [ ] **Observer Interpolation**: Remote clients observing the ped only interpolate incoming `PED_SYNC` updates (`SetTargetPosition`, `SetTargetRotation`).
- [ ] **Sync Termination**: `PED_STOPSYNC` (ID 62) cleanly disengages the primary wander task.

### 3. Step C — Occupied & Unoccupied Vehicle Sync
- [ ] **Occupied Vehicle Sync (`PLAYER_VEHICLE_PURESYNC` 33)**: 
  - Driver (seat 0) drives the vehicle and updates target position, rotation, velocity, and turn speed via `CClientVehicle::SetTargetPosition`.
  - Passengers sync controller state, weapon aiming, and player health.
- [ ] **Unoccupied Vehicle Sync (`UNOCCUPIED_VEHICLE_SYNC` 56)**:
  - Empty cars hosted by this phone write position and rotation deltas in outgoing packet 56.
  - Remote empty cars update and interpolate position smoothly from incoming packet 56 without freezing.

### 4. Step D — Combat, Bullet Sync & Hit Registration
- [ ] **Bullet Sync (`PLAYER_BULLETSYNC` 98)**: Tracers and weapon discharge handled via native `DischargeWeapon`.
- [ ] **Authoritative Damage Registration**: Victim's own `WritePlayerPuresync` reports health changes and damager IDs (`SPlayerHealthSync`). The shooter does not fabricate victim health.
- [ ] **Melee Combat**: Combat strikes driven by native `CTaskSimpleFight` from `ButtonCircle` keysync.

### 5. Step E — Scripting Engine, Touch UI & Optimization
- [ ] **Lua 5.3 Client VM**: Complete client-side MTA event bindings (`addEventHandler`, `triggerServerEvent`).
- [ ] **DirectX / RenderWare 2D GUI**: Render custom DX elements, radar blips, and HUD overlays.
- [ ] **Touch Control Customization**: Virtual joystick sensitivity calibration and touch button mapping.
- [ ] **Custom Asset Streaming**: Server-to-client dynamic DFF/TXD model downloading and RenderWare memory streaming.
- [ ] **Engine Optimization**: 60 FPS frame pacing, memory compaction, and anti-crash guardrails on ARM64.


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

## License & Attribution

- Multi Theft Auto (MTA) is licensed under the GPLv3 / BSD licenses.
- Lua 5.3 is licensed under the MIT License.
- Opus audio codec is licensed under the BSD-3-Clause License.
- Grand Theft Auto and Grand Theft Auto: San Andreas are registered trademarks of Rockstar Games / Take-Two Interactive.
- This open-source research project is not affiliated with, endorsed by, or sponsored by Rockstar Games or Take-Two Interactive.
