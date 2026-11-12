# Android Native MTA Neon Porting Roadmap & Milestones

The [full architectural plan](PLANO_PORT_NEON_ANDROID.md) details audits, dependencies, and parity criteria. Each milestone must pass the standard APK build pipeline and provide verifiable evidence. Completion of an individual item does not imply the entire phase is complete.

---

## 1. Milestone Tracking Table

| Milestone | Depends On | Verifiable Acceptance Criteria | Status |
|---|---|---|---|
| **P0.1 Technical Inventory** | — | Identify APK, native engine, sources, server module, and asset revision. | Audited |
| **P0.2 Desktop Reference** | P0.1 | Capture session and resources from functional desktop Neon client in staging. Catalog target APIs and script invocations. | Pending |
| **P1A.1 Game Data Setup** | P0.1 | Local asset folder / `.env` pipeline, size/SHA-256 validation, staging generations, recovery without wiping previous generation, Play button gate. | Completed |
| **P1A.2 ARM64 APK** | P0.1 | Oracle/local ARM64 build, verified APK with 16KB-aligned LOAD segments, valid packaging. Verified on Samsung Galaxy S25. | Completed (4KB pages verified; 16KB device verification pending) |
| **P1A.3 Kotlin/JNI Boundary** | P1A.1 | Compatible method signatures, explicit failure propagation to launcher UI, touch pointer tracking, teardown cleanup. | Completed |
| **P1A.4 GTA Native Boot** | P1A.1–3 | WarMedia/NvEventQueue host, assets, OpenGL rendering, touch input, and lifecycle. | Offline Checkpoint PASSED on S25: 3D world, CJ, HUD, touch controls, pause/resume, and clean exit. |
| **P1A.5 Engine/Session Integration**| P1A.4, P1B.2 | Single unified session managing engine, network, and mutation queue executed on the GTA main thread. | In Progress |
| **P1B.1 Native Transport** | P0.2 | Headless ARM64 harness, OPEN51 datagram, cipher handshake, and reliability layer. | Codec implemented; server handshake verified |
| **P1B.2 Native Session** | P1B.1 | Protocol versioning, join data, and reconnect sequence against MTA Neon server. | In Progress |
| **P2. Resources & Game Session** | P1A.4, P1B.2 | Root element, resource start/stop, HTTP asset cache, diagnostic Lua, and client-server events. | Pending |
| **P3. World & Entities Sync** | P2 | Peds, vehicles, and objects visible across both clients. Streaming, ownership, movement, health, and respawn tested. | In Progress (basic peds/movement working with bugs) |
| **P4. Lua, DX, GUI & Browser** | P2, P3 | RenderWare 2D DX draw calls, custom UI interactions, CEGUI/WebView bindings. | Pending |
| **P5. Neon Parity Matrix** | P3, P4 | Complete API matrix, events, and elements matching MTA Neon desktop capabilities. | Pending |
| **P6. Release Distribution** | P5 | Reproducible release builds, multi-device hardware validation, automated updates. | Pending |

P1A (Engine/Platform) and P1B (Network Transport) are independent parallel tracks. The approval of the port requires a fully native Android ARM64 session connecting to a live server.

---

## 2. Historical Execution Log

### 2026-11-11: Local Asset & Environment Decoupling
- Decoupled game assets from external Hugging Face cloud dependencies.
- Added `.env` configuration (`GAME_ASSETS_DIR`, `DEVICE_ASSETS_PATH`, `CDN_ROOT_URL`) allowing offline asset loading directly from local storage.
- Added `findLocalAssetsFolder()` and `syncFromLocalFolder()` in `NativeAssetBridge.kt`.
- Created `tools/sync_local_assets.sh` for automated asset validation and ADB transfer.
- Clarified prerequisites in `README.md`: Original GTA San Andreas Mobile v2.10 files required.

### 2026-11-11: Engine Teardown & Custom MTA Transport
- **Process Isolation**: `EngineHostActivity` operates inside a dedicated `:engine` process. Exiting the game terminates this process completely, ensuring static GTA singletons are re-created fresh on subsequent runs without memory corruption.
- **Back Navigation**: Android Back gesture cleanly signals engine exit while Surface remains valid; prevents deadlocks during teardown.
- **Handshake Verification**: S25 received `CONNECTION_REQUEST_ACCEPTED` and `MOD_NAME` via native `MtaClientTransport`.

### 2026-11-10: Asset Bootstrap & Wire Format Alignment
- **Bitstream Order**: Aligned bitstream serialization to MSB-first per byte, little-endian integers, and right-aligned partial bitfields matching MTA server expectations.
- **Join Protocol (`NeonJoinProtocol.{h,cpp}`)**: State machine handling `PLAYER_JOIN` → `MOD_NAME` → `PLAYER_JOINDATA` → `SERVER_JOIN_COMPLETE` → `SERVER_JOINEDGAME`.
- **Boot Assets**: Bundled critical boot assets (`mainV1.scm`, `american.gxt`, `Textures/fonts/`, `menu.pvr.dat`) to resolve initial engine crashes.
