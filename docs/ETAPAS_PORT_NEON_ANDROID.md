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
| **P3. World & Entities Sync** | P2 | Peds, vehicles, and objects visible across both clients. Streaming, ownership, movement, health, and respawn tested. | In Progress (server peds created on the S25 after the desktop `0x60D64D` group patch; pool vehicles and the unoccupied sync sender (packet 56) built, device run pending; driver puresync send, local enter/exit handshake (packet 58) built, device run pending; driver puresync send, remote seat placement and bullet sync not started) |
| **P3.1 Disconnect Recovery** | P1B.2, P3 | When the server drops the session, show a dialog with **Retry** and **Quit** (same flow as the MTA desktop client). Retry runs a fresh join (`PLAYER_JOIN` → `MOD_NAME` → `PLAYER_JOINDATA` → `SERVER_JOIN_COMPLETE`) and spawns the character in the world as on a first connection: remote peds, vehicles and the local player are rebuilt, nothing is kept from the old session. Quit returns to the server browser. Also handles `ForceReconnect` (server RPC). | Implemented and verified on the S25: `/disconnect` shows the dialog and Retry rejoins with a fresh spawn |
| **P3.2 Chat Layout** | P2 | The chat does not match the MTA desktop client: lines are spaced too far apart and the layout differs. Match the desktop chat box (line height, padding, font, colour codes, and the line limit) so both clients show the same chat. Verify with a screenshot of the desktop and the phone side by side. | Implemented (line step = font height, one background box); side-by-side desktop screenshot pending |
| **P4. Lua, DX, GUI & Browser** | P2, P3 | RenderWare 2D DX draw calls, custom UI interactions, CEGUI/WebView bindings. | Pending |
| **P5. Neon Parity Matrix** | P3, P4 | Complete API matrix, events, and elements matching MTA Neon desktop capabilities. | Pending |
| **P6. Release Distribution** | P5 | Reproducible release builds, multi-device hardware validation, automated updates. | Pending |

P1A (Engine/Platform) and P1B (Network Transport) are independent parallel tracks. The approval of the port requires a fully native Android ARM64 session connecting to a live server.

---

## 2. Historical Execution Log

### S2 peds and vehicles (2026-11-12)
- Server peds: the CPedGroup crash was the 8-group pool running out. Ported the desktop patch at `0x60D64D` to the arm64 `CPlayerPed` constructor (`EnsurePlayerPedGroupPatch`). Verified on the S25: patch applied, 4 server peds created.
- Next crash on the S25: SIGSEGV in `sortLODs` during render. Suspect: heap-built vehicles. Vehicles now come from `CCarCtrl::CreateCarForScript` like desktop `CPoolsSA::AddVehicle`. Not run yet: the phone went offline.
- Unoccupied vehicle sync sender (desktop `CUnoccupiedVehicleSync::UpdateStates` / `WriteVehicleInformation`) ported. Not run yet.
- Android emulator (API 34 x86_64 with ARM translation) tried as a fallback: libGTASA crashes 2 s after boot under translation, so the emulator can't stand in for the phone.

### Step A remainder (partial)
- Fire press: keysync fire writes `ButtonCircle` (`0x26`, pressed = 255) in the controller hook. Built and joined on the S25. Not yet checked with a second player.
- Duck: stays on the existing `PresentOnFoot` task path (`SetTaskDuckSecondary`). The desktop clears `ShockButtonL` so it doesn't fight the duck task; we don't write it.
- Pad copy of the other buttons and enter/leave-car: not started.

### P3.2 Chat Layout (planned)
- Requested 2026-11-12: the phone chat has spaces between lines and does not look like the MTA desktop chat. Compare against the desktop chat code in `mta_neon_source/Client` before changing the phone layout.
- Implemented in `MtaChatView.java`: the per-line 2 dp gap is gone, lines step by the font height, and one background box sits behind the stack (desktop `CChat.cpp` behaviour). Screenshot on the S25 checked.

### P3.1 Disconnect Recovery (planned)
- Requested 2026-11-12: after a server drop, show Retry / Quit like MTA desktop. Retry re-joins and re-spawns the character as a first connection. Source of the desktop behavior: `CPacketHandler` (`Disconnected:` reasons, join flow) and `CInputRPCs::ForceReconnect` in `mta_neon_source/Client/mods/deathmatch/logic/`.
- Implemented: `NeonJoinProtocol` fails a `Complete` session with "Connection lost" when RakNet drops. `GameActivity` shows Retry / Quit after a drop that followed a join. `nativeConnect` stops the old network and calls `RequestRemoteReset()`, which drops every remote body and car on the game thread. Join verified on the S25 after the change.
- Not verified: the drop itself. Testing it means turning Wi-Fi off on the phone, which also carries the ADB link (Tailscale over `wlan0`).

### Step A of P3 (controller switch): hook written, device check pending
- `PlayerPedPort.cpp` hooks `CPlayerPed::ProcessControl` (`_ZN10CPlayerPed14ProcessControlEv`). For a remote player with keysync it sets `CPad::GetPad(0)` stick fields for one call, then restores the pad. The offsets (`GetPad` stride `0x158`, `LeftStickX` at `+0`, `LeftStickY` at `+2`) come from disassembling `CPad::GetPedWalkLeftRight` and `CPad::GetPedWalkUpDown` in the libGTASA 2.10 binary (sha1 `6dd7c360...`, same in all local copies).
- `GameEngine.cpp`: keysync stores `stickX` / `stickY`. The velocity fallback runs only when the hook is missing. For a controlled player the per-tick lerp is skipped and the body is re-anchored only past 8 m.
- Not done: camera-rotation float (`0xB6F178`) for the stick basis, duck via `CTaskSimpleDuck`, enter/leave-car tasks, and the walk-cycle check on the S25. Only `PlayerPedPort.cpp` was syntax-checked on the host; the Android build has not run.

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
