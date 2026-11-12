# MTA Neon Android Parity Matrix

**Source Audit Date:** 2026-11-11.
**Desktop Reference:** `mta_neon_source`, revision `c3fe13e6af5824608a5127f86e936c58e37eb997`.
**Android Target:** Evaluated across `app/src/main/cpp` and Kotlin/Java entry points.

See [VALIDACAO_ANDROID_2026-11-11.md](VALIDACAO_ANDROID_2026-11-11.md) for execution logs on real hardware.

---

## 1. Classification Scheme

| Status | Definition |
|---|---|
| **Implemented** | Functional executable logic exists in source code and is connected to the execution path. |
| **Partial** | Functional logic exists, but lacks certain contracts, integration, or edge-case validations. |
| **Stub** | Method logs the call, returns a default value, or acts as a placeholder without underlying operations. |
| **Missing** | Android runtime does not yet implement this contract or interface. |

---

## 2. Bootstrap, Transport, and Resource Architecture

| Subsystem | Android Status | Source Evidence & Integration | Requirements for Full Parity |
|---|---|---|---|
| **Game Asset Storage & Verification** | **Implemented** | `NativeAssetBridge.syncAssets`, `findLocalAssetsFolder`, generations, SHA-256 checks, atomic activation of `active`, and install gate. `getReadyStoragePath` verifies core required files. [A1] | Ensure semantic data integrity; retain active generation during gameplay. |
| **Resident GTA Engine Library** | **Implemented** | `GameEngine::initialize` executes `dlopen`, locates base address, populates function table. `nativeBootEngine` exposes this stage. [A2] | Complete boot of GTA application and verification of all ARM64 ABI calling conventions. |
| **Native Android Host (Surface/EGL)** | **Partial** | `EngineHostActivity` extends `GTASA`, loads native library, utilizes NVIDIA/WarMedia wrappers. Debug entry via **Settings → Test GTA**. [A3] | Playable 3D world, touch controls, audio, pause, resume, and lifecycle management. |
| **Multiplayer Bootstrap Path** | **In Progress** | `GameActivity` manages network loop and session join. Engine host activity runs in `:engine` process. [A2, A3] | Unify network session with engine surface loop, dispatching mutation queues onto the GTA main thread. |
| **RakNet Transport & Neon Join** | **Partial** | `MtaClientTransport` and `NeonJoinClient` manage state machine. Join supports 17-bit ElementIDs. [A4] | Validate end-to-end against live server with verified nonces and session handshake. |
| **Game Packet Envelope** | **Partial** | `PacketHandler::handlePacket` unpacks `[99][gameId][body]`. `MTAPacketID` defines internal IDs. [A5] | Derive packet schemas from `Shared/sdk/net/Packets.h` matching bitstream versions. |
| **`ENTITY_ADD`, Removal & Roots** | **Stub / Partial** | Basic `ENTITY_ADD` handler instantiates player peds and basic automobiles. [A5, D1] | Full entity decoder, 17-bit ElementIDs, server/resource/GUI roots, initial properties, and rollback on malformed packets. |
| **Server Resource Lifecycle** | **Missing** | Server `RESOURCE_START` / `RESOURCE_STOP` handlers not yet bound to `CResourceManager` equivalent. [A5, D1, D2] | Parse resource metadata, manage file download priorities, initialize resource root elements. |
| **Server Resource File Transfer** | **Missing** | GTA data is handled locally. Dynamic HTTP resource downloads are not yet wired to `LuaVMStub`. [A1, A6, D2] | HTTP download client, checksum validation, per-resource disk caching, `autoDownload`, and download completion events. |

---

## 3. Elements, Engine, Synchronization, and Health

| Subsystem | Android Status | Source Evidence & Integration | Requirements for Full Parity |
|---|---|---|---|
| **Local Entity Pools** | **Implemented** | `ElementManager` manages pools, creation, deletion, and sync data copy for Players, Peds, Vehicles, and Objects. [A7] | Bind ElementIDs to live GTA game entities on the main rendering thread. |
| **Element Identity (17-bit)** | **Partial** | Join protocol reads 17 bits; internal storage uses `uint16_t` (`0xFFFF` invalid). [A4, A7] | Unify element representation to support full 17-bit ID space and invalid entity sentinel values. |
| **Visible Peds & Players** | **Partial** | Remote players spawned via `CPlayerPed` with basic pure sync position updates. [A8] | Full limb animation mapping, cloth textures, weapon holding, and accurate ground height snapping. |
| **Vehicles & Objects** | **Partial** | Server vehicles spawned with `CAutomobile` in `CWorld`. [A8, D3] | Collision streaming, interior/dimension checks, driver/passenger sync (`PLAYER_VEHICLE_PURESYNC`). |
| **Model Streaming & Requests** | **Partial** | `loadAllModels` and `hasModelLoaded` mapped in `EngineOffsets`. `requestModel` requires hook. [A8] | Implement asynchronous model streaming requests, reference counting, and fallback placeholders. |
| **Player / Vehicle Pure Sync** | **Partial** | Basic coordinates and stick heading synchronized; flags and keysync partially mapped. [A9, D4] | Full bit-packed serialization matching `SyncStructures.h`, quaternion compression, and velocity smoothing. |
| **Interpolation & Extrapolation** | **Stub** | `SyncManager` calculates global alpha; timestamp comparison in progress. [A2, A9] | Per-entity position history buffers, network clock synchronization, and dead-reckoning extrapolation. |
| **Health, Armor & Damage** | **Partial** | Basic health reading hooked; damage registered via pure sync packets. [A6, A8, A9, D4] | Accurate damage mapping, weapon damage types, fall damage, and synchronized death/respawn states. |

---

## 4. Lua Scripting, Events, and Interfaces

| Subsystem | Android Status | Source Evidence & Integration | Requirements for Full Parity |
|---|---|---|---|
| **Lua Interpreter** | **Implemented** | `LuaVMStub` embeds Lua 5.3.6 runtime with buffer/file execution wrappers. [A6] | Bind interpreter to active server resources and manage execution limits. |
| **MTA Lua Bindings** | **Stub** | 31 MTA API functions registered as stubs in `LuaVMStub::initialize`. [A6] | Implement parameter parsing, return value packing, userdata handles, and error logging. |
| **Element Hierarchy (`root`, `localPlayer`)** | **Missing** | Virtual entity tree and event bubbling not yet instantiated in Lua state. [A6, A7] | Construct global element variables (`root`, `resourceRoot`, `localPlayer`) and event propagation. |
| **Custom 2D Rendering (DX)** | **Partial** | `RenderWareHooks` provides GLES draw calls; `BitmapFont` renders ASCII text. [A10] | Expose `dxDrawText`, `dxDrawImage`, and `dxDrawRectangle` to Lua scripts with clipping and scaling. |
| **GUI Framework** | **Stub** | GUI function stubs (`guiCreateWindow`, etc.) registered. [A6, D7] | Bridge touch gestures to GUI widgets or provide HTML/WebView overlays. |
| **Spatial 3D Audio & Voice** | **Partial** | Opus audio codec embedded in `VoiceCodecStub`. [A11] | Wire Opus decoder to audio output thread; support 3D positioned voice streams. |

---

## 5. Source Reference Mappings

- **A1**: `NativeAssetBridge.kt` — local asset detection, SHA-256 verification, generation staging.
- **A2**: `JNI_OnLoad.cpp` — native initialization, lifecycle dispatch, asset path configuration.
- **A3**: `EngineHostActivity.java` — GTA SA Android host, SurfaceHolder, EGL initialization.
- **A4**: `NeonJoinProtocol.h/.cpp` — state machine, packet serialization, 17-bit ElementIDs.
- **A5**: `PacketHandler.h/.cpp` — packet routing and demuxing.
- **A6**: `LuaVMStub.h/.cpp` — embedded Lua 5.3 runtime.
- **A7**: `ElementManager.h/.cpp` — in-memory entity registry.
- **A8**: `GameEngine.h/.cpp` — GTA engine function pointers, hooks, and entity spawning.
- **A9**: `SyncTypes.h`, `SyncManager.h/.cpp` — position synchronization and extrapolation.
- **A10**: `RenderWareHooks.h/.cpp`, `BitmapFont.h/.cpp` — OpenGL ES 2.0 rendering primitives.
- **A11**: `VoiceCodecStub.h/.cpp` — Opus encoder/decoder integration.
