# Plan 22 — Reimplementation of the MTA Neon Client for Android

**Created:** 2026-11-10 · **Updated:** 2026-11-11 · **Scope:** Complete native port of MTA Neon for Android ARM64, achieving verifiable functional parity with target MTA Neon desktop revisions.

This project delivers a native Android client implementation. Emulation of Windows x86 PE DLLs and legacy translation gateways to MobileSA 1.6 are explicitly excluded.

Full reimplementation requires two distinct pillars:
1. **The GTA Mobile engine must boot an interactive, controllable 3D world.**
2. **A native network client must complete a valid MTA Neon multiplayer session.**

These streams converge into shared entities, Lua scripting, and UI systems.

---

## 1. Verified Architecture & State (2026-11-11)

The offline GTA engine boots 3D world, CJ character, HUD, and touch controls on the Galaxy S25. Exit, reopening, and task resumption are operational with process isolation. The multiplayer path relies on the native MTA transport protocol.

| Subsystem | Verified Evidence | Production Roadmap |
|---|---|---|
| **Android Launcher** | Modern Kotlin UI, server list, settings, JNI bindings, and ARM64 debug APK. | Server browser connecting to real servers. Game session initializes engine rendering within `:engine` process. |
| **Android Game Engine** | Packaged `libGTASA.so`, `libOpenAL64.so`, and `libSCAnd.so` (War Drum Studios 2.10). | Java/JNI host bound to active generation. Entity creation, streaming, and mutation queues running on the GTA main thread. |
| **16KB Memory Page APK** | `libWorldRPGNeon.so` configured with 16KB LOAD segment alignment. Prebuilts have 64KB alignment. | Remote build passes packaging audit: static C++ runtime, compressed native libraries extracted upon installation. |
| **Network Transport** | `MtaClientTransport` implements OPEN51 datagram, cipher handshake, and bitstream serialization. | Fully replace legacy RakNet stub; end-to-end handshake verified on live servers. |
| **Server Network Module** | AArch64 Linux `net.so` server binary. | Server reference for packet verification and handshake negotiation. |
| **Desktop Reference** | `mta_neon_source` repository (reference for packet formats, Lua APIs, and sync structures). | Desktop client acts as ground-truth oracle for network packet captures. |
| **Scripting & Audio** | Lua 5.3.6 and Opus codecs embedded into native shared library. | Wire Lua runtime to server-downloaded resources and render custom DX/CEGUI elements. |

---

## 2. Local Asset Pipeline & Environment Configuration

Game assets are configured via environment variables (`.env`) and loaded from local storage:

- **Local Storage Pipeline**: `NativeAssetBridge.kt` detects local assets in `GAME_ASSETS_DIR` or `/sdcard/Android/data/...`, validates integrity against expected files, and stages them atomically into generation directories.
- **Boot-Critical Bundled Assets**: Minimal boot assets (`mainV1.scm`, `american.gxt`, `Textures/fonts/`, repaired `menu.pvr.dat`) are embedded in `app/src/main/assets/` to ensure immediate offline booting.
- **Decoupled CDN**: External cloud dependencies (such as Hugging Face) are optional and disabled by default. Custom HTTP servers can be specified via `CDN_ROOT_URL` in `.env`.

---

## 3. Native Network Transport Strategy

The base networking contract is derived from MTA Neon desktop architecture:

1. **Custom 51-Byte Handshake (OPEN51)**:
   - Header: `0x09 0x04`
   - Client Nonce (16 bytes)
   - MTA Magic: `00 ff 00 ff fe fe fe fe fd fd fd fd 78 56 34 17`
   - Fixed negotiation block and terminator `0xDE`.
2. **Dynamic Stream Cipher**:
   - Nonce-based key derivation.
   - S-box permutation and XOR stream encryption applied to subsequent packets.
3. **Session Join Sequence**:
   - `PLAYER_JOIN` → `MOD_NAME` → `PLAYER_JOINDATA` → `SERVER_JOIN_COMPLETE` → `SERVER_JOINEDGAME`.
4. **Reliability Layer**:
   - Sequence numbering, ACK/NAK tracking, bit-level packing, and packet reassembly.

---

## 4. Phase-by-Phase Roadmap

### Phase 0: Baseline & Contract
- Freeze reference commit hashes for desktop source, server binaries, APK, and game assets.
- Map API calls, packet schemas, and bitstream versioning.

### Phase 1A: Engine Boot & Platform Layer
- Single unified EGL surface loop.
- Local asset generation verification and case-insensitive file mapping (`EngineCaseAliases`).
- Stable touch input, lifecycle pause/resume, and process isolation (`:engine`).

### Phase 1B: Native Network Handshake
- Headless connection harness.
- Verification of OPEN51 datagram, cipher negotiation, and join packets (`PLAYER_JOINDATA`).
- Handshake confirmation with `CONNECTION_REQUEST_ACCEPTED` and `MOD_NAME`.

### Phase 2: Session & Resource Management
- In-game session initialization (`SERVER_JOINEDGAME`).
- Resource loading lifecycle (`onClientResourceStart`/`Stop`).
- HTTP file caching and script validation.

### Phase 3: World Entities & Synchronization
- Synchronized creation of remote players (`CPlayerPed`), vehicles (`CAutomobile`), and peds.
- Pure sync serialization: positions, velocities, orientations, health, and armor.
- Interpolation smoothing and dead-reckoning extrapolation.

### Phase 4: Scripting Engine & UI
- Lua 5.3 runtime bound to client-side MTA event system.
- RenderWare 2D DX rendering (`dxDrawText`, `dxDrawImage`, `dxDrawRectangle`).
- Touch-friendly on-screen controls, virtual analog stick, and customizable HUD elements.

### Phase 5: Optimization & Hardware Compatibility
- Custom DFF/TXD asset streaming.
- Bluetooth / USB gamepad input mapping.
- 60 FPS pacing and anti-crash memory protections on ARM64.
