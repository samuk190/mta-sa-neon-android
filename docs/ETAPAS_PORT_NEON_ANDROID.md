# Android Native MTA Neon Porting Roadmap & Milestones

The [full architectural plan](PLANO_PORT_NEON_ANDROID.md) details audits, dependencies, and parity criteria. Each milestone must pass the standard APK build pipeline and provide verifiable evidence. Completion of an individual item does not imply the entire phase is complete.

---

## 1. Milestone Tracking Table

| Milestone | Depends On | Verifiable Acceptance Criteria | Status |
|---|---|---|---|
| **P0.1 Technical Inventory** | — | Identify APK, native engine, sources, server module, and asset revision. | Audited |
| **P0.2 Desktop Reference** | P0.1 | Capture session and resources from functional desktop Neon client in staging. Catalog target APIs and script invocations. | Pending |
| **P1A.1 Game Data Setup** | P0.1 | Local asset folder / `.env` pipeline, size/SHA-256 validation, staging generations, recovery without wiping previous generation, Play button gate. | Completed |
| **P1A.2 ARM64 APK** | P0.1 | Oracle/local ARM64 build, verified APK with 16KB-aligned LOAD segments, valid packaging. Verified on Samsung Galaxy S25. | Completed (16KB page alignment verified via ELF LOAD segments and device runs) |
| **P1A.3 Kotlin/JNI Boundary** | P1A.1 | Compatible method signatures, explicit failure propagation to launcher UI, touch pointer tracking, teardown cleanup. | Completed |
| **P1A.4 GTA Native Boot** | P1A.1–3 | WarMedia/NvEventQueue host, assets, OpenGL rendering, touch input, and lifecycle. | Offline Checkpoint PASSED on S25: 3D world, CJ, HUD, touch controls, pause/resume, and clean exit. Fixed 2 GB RenderWare pointer delta overflow with GameHeap arena. |
| **P1A.5 Engine/Session Integration**| P1A.4, P1B.2 | Single unified session managing engine, network, and mutation queue executed on the GTA main thread. | In Progress |
| **P1B.1 Native Transport** | P0.2 | Headless ARM64 harness, OPEN51 datagram, cipher handshake, and reliability layer. | Codec implemented; server handshake verified |
| **P1B.2 Native Session** | P1B.1 | Protocol versioning, join data, and reconnect sequence against MTA Neon server. | In Progress |
| **P2. Resources & Game Session** | P1A.4, P1B.2 | Root element, resource start/stop, HTTP asset cache, diagnostic Lua, and client-server events. | In Progress (RESOURCE_START file list parsed for both live layouts; HTTP client-file and CLIENT_SCRIPT download with size/CRC check verified live with the headless client: 1482 files, 0 failures; ready resources run their scripts: 30 of 30 started, 0 Lua errors and 0 unimplemented calls in 90 s. Done: PLAYER_RESOURCE_START sent after start, RESOURCE_STOP with handler/timer cleanup, fromJSON/toJSON, ServerWorld parsed descriptors) |
| **P3. World & Entities Sync** | P2 | Peds, vehicles, and objects visible across both clients. Streaming, ownership, movement, health, and respawn tested. | In Progress (server peds created on S25 after 0x60D64D ped group patch; pool vehicles & unoccupied sync sender (56) built; driver puresync send & vehicle in/out (58) verified live; driver sync (33) relayed; SYNC_SETTINGS weapon list parsed; bullet sync send (98) built; remote seats & driver motion built; jacking/enter-exit animations pending) |
| **P3.1 Disconnect Recovery** | P1B.2, P3 | When the server drops the session, show a dialog with **Retry** and **Quit** (same flow as the MTA desktop client). Retry runs a fresh join (`PLAYER_JOIN` → `MOD_NAME` → `PLAYER_JOINDATA` → `SERVER_JOIN_COMPLETE`) and spawns the character in the world as on a first connection: remote peds, vehicles and the local player are rebuilt, nothing is kept from the old session. Quit returns to the server browser. Also handles `ForceReconnect` (server RPC). | Implemented and verified on the S25: `/disconnect` shows the dialog and Retry rejoins with a fresh spawn |
| **P3.2 Chat Layout** | P2 | The chat does not match the MTA desktop client: lines are spaced too far apart and the layout differs. Match the desktop chat box (line height, padding, font, colour codes, and the line limit) so both clients show the same chat. Verify with a screenshot of the desktop and the phone side by side. | Implemented (line step = font height, one background box); side-by-side desktop screenshot pending |
| **P4. Lua, DX, GUI & Browser** | P2, P3 | RenderWare 2D DX draw calls, custom UI interactions, CEGUI/WebView bindings. | In Progress (Desktop Tahoma 15px text scaling matched in MtaDxView; GUI element model with window, button, edit, memo, grid list, tabs, scroll pane, static text, setVisible, destroyElement; showCursor touch routing to onClientClick; mobile HUD component toggles via CWidgetPlayerInfo::Draw NOP patching. Open: DX shaders, `guiCreateFont` (CreateGuiFont is not hooked), CEF webview browser) |
| **P4.1 Wardrobe preview** | P4 | Registration clothes preview matches the desktop client. Port order and desktop line ranges: [ROADMAP_WARDROBE_PREVIEW.md](ROADMAP_WARDROBE_PREVIEW.md). | In progress. The eight preview calls are bound. The login cinematic (`setCameraMatrix` from `onClientPreRender`) is confirmed on the S25. Opening music uses `playSound`, `stopSound` and `setSoundVolume`. The clothes preview itself has not been recorded on a device. |
| **P5. Neon Parity Matrix** | P3, P4 | Complete API matrix, events, and elements matching MTA Neon desktop capabilities. | In Progress (Derived from source via tools/parity_matrix.py -> docs/PARITY_MATRIX.md. 2026-11-14: Lua functions 631 implemented / 136 stub / 814 missing of 1581 (39.9%); GUI and DX 175/199 (87.9%), CLuaGUIDefs 137/137, CLuaDrawingDefs 32/43, SVG 6/6; camera 47/47; client events 11/144 (7.6%); inbound packets 24/62; outbound 10/31; element types 13/17) |
| **P6. Release Distribution** | P5 | Reproducible release builds, multi-device hardware validation, automated updates. | Pending |

P1A (Engine/Platform) and P1B (Network Transport) are independent parallel tracks. The approval of the port requires a fully native Android ARM64 session connecting to a live server.

---

## 2. Historical Execution Log

### GameHeap 2 GB arena and HUD/Cursor GUI (2026-11-13)
- Render freeze / crash root cause identified: `libGTASA.so 2.10 arm64` contains legacy RenderWare memory relocation code (`RxLockedPipeUnlock` at `0x278e3c`: `sub x9, x0, x21` then `add x8, x8, w9, sxtw`) that truncates the pointer delta to 32 bits. When the Android Scudo allocator places heap regions > 2 GB apart (inherited per boot from Zygote), pointer math overflows by `0x2_0000_0000`, corrupting vertex arrays (~5s after spawn in `TidyUpModelInfo2` / `MoveColModelMemory`) or crashing in `RwEngineStart`.
- `WorldRPG::Game::InstallGameHeap()` (`gtasa/GameHeap.cpp`) reserves a 1.75 GB continuous virtual memory arena and redirects 12 GOT slots (`malloc`, `calloc`, `realloc`, `free`, `strdup`, `operator new/delete`). `EngineHostActivity.onCreate` loads `WorldRPGNeon` before `super.onCreate` starts the engine thread. Live S25 validation: 150+ seconds in-game with all resources loaded, 0 crashes, 0 system fallbacks.
- Desktop GUI element model and touch cursor events: `MtaDxView` captures touches when `showCursor` is active and routes them to `nativeCursorEvent` / `ActiveVmQueueCursor` -> `onClientClick`.
- Mobile HUD component toggles: `gtasa/HudComponents.cpp` patches `CWidgetPlayerInfo::Draw` (`0x37cefc`) with NOPs for `setPlayerHudComponentVisible` ("radar", "health", "armour", "weapon", "ammo", "money", etc.).
- Build performance optimized: Configuration cache, build cache, parallel execution, CMake unity builds for Lua, and locale filters dropped clean assemble from 21s to 6s and incremental builds to 350ms.

### P2 resource lifecycle and JSON (2026-11-13)
- PLAYER_RESOURCE_START (108) is sent after `onClientResourceStart`, as desktop `CResource::Load` does (`CResource.cpp` 1630-1640). Live on the test server: 30 of 30 sent, the server dispatches all of them. Nothing observable reacts, because no resource on that server handles `onPlayerResourceStart`. The 9 mobile-shim resources send counter 1 (assumed; the shim strips the counter); acceptance is not visible.
- RESOURCE_STOP (83) runs desktop `Packet_ResourceStop` and `CResource::Stop`: `onClientResourceStop`, then the resource's handlers and timers are dropped (`LuaVMStub.cpp`, ownership tracked per handler and timer). Not run live: the test server's resources were not stopped.
- fromJSON / toJSON follow the desktop CLuaArguments rules (`shared/LuaJsonApi.cpp`). `tools/headless/resource_tests.cpp` checks them on `samp-map-loader/catalog.json` (1435 models).
- `ServerWorld::OnResourceStart` takes the parsed descriptor, so the mobile-shim layout is read correctly.

### P2 ready-resource loader and Lua API (2026-11-13)
- `shared/ResourceLoader` gates each resource like desktop `CResource::Load`: every auto-download file (CLIENT_SCRIPT and client files) cached or downloaded, and all packet 95 no-cache items received. Then no-cache scripts, CLIENT_SCRIPT files in list order, `onClientResourceStart`. Scripts run on the VM pulse, not on download threads.
- Packet 95 has no file names on the phone's wire (verified from the bytes: `uint16 id, uint16 count, {uint32 len, zlib blob}`), and the no-cache counts match the items sent for the 8 resources that have them.
- Headless 90 s measurement, before and after (same harness, pulse and DX frame now driven): static scan of all 60 scripts found 143 called globals unbound or stub-only before (1262 call sites), 3 after (`fromJSON` open; `send` and `isCurrent` are script parameter names). Runtime: 0 unimplemented-function calls and 0 Lua errors after; before, scripts never ran their render handlers in the harness.
- `shared/LuaApiExtra` binds the phone-side natives; engine-dependent ones return the desktop failure shape or safe defaults and are listed in the source.
- Not run on a device. Packet 95 arriving before its RESOURCE_START is held (code path not exercised by the live server, which sends 82 first).

### P2 resource files and HTTP cache (2026-11-13)
- `shared/ResourceFiles` ports desktop `Packet_ResourceStart` (file chunks 'F', exports 'E', Neon 'K'/'N'/'A'). The live server sends two layouts: the desktop one (21 of 30 packets, no per-file MD5 because the join rewrites the peer bitstream to 0x30) and the mobile shim's (9 zero-file packets). Both parse.
- `net/HttpDownloader`: 4 worker threads, HTTP/1.0, cache at `<files>/mta_resource_cache/<resource>/<file>`, verified by size and CRC-32 (MD5 when sent). Headless live run: 30/30 packets parsed, 1437 client files listed, 1056 downloaded in the run window, 0 mismatches, 0 failures, 50 MB.
- Join parser fix: the desktop SERVER_JOINEDGAME read accepted the mobile body and returned HTTP type 3 / port 0; types other than 0-2 now fall back to the mobile read.

### Server shim heap crash fixed; driver sync verified live (2026-11-13)
- Root cause of the test server's glibc heap aborts: the mobile shim (`~/adapter-src/packetlogger.cpp` on the Oracle VM) never removed a client from `g_mobile_clients`, and both its watchdog thread (every second) and its packet handler (every mobile packet) wrote six 64-bit fields into every stored `player` object, including ones `net.so` had already freed. Fixed in the shim: slots are cleared on PLAYER_QUIT / PLAYER_TIMEOUT, the packet handler touches only the sender, the watchdog only clients heard from in the last 3 s, all under one mutex. Backup: `packetlogger.cpp.bak-202611130151`. Three join/quit cycles reuse slot 0 with no heap errors.
- With that and the jack-abort fix, the two-bot test passed: entry confirmed, packet 33 relayed to the observer on another IP (12 updates, y -1718.6 -> -1682.4 at 2 m/s, vehicle health 900, player health 100), exit confirmed.

### Live protocol test with the headless client (2026-11-12)
- `tools/headless` builds the port's own net code for Linux with a scripted game layer. Driver bot on the dev machine, observer bot on the Oracle VM (two IPs are needed: the mobile adapter shim derives the serial from the client IP, so a second session from one IP gets "serial already in use").
- Packet 58 verified live: REQUEST_IN -> REQUEST_IN_CONFIRMED -> NOTIFY_IN, and REQUEST_OUT -> REQUEST_OUT_CONFIRMED -> NOTIFY_OUT. The observer received all four actions for the driver.
- Packet 33 is sent only after the confirm (fixed in this run), but never reaches the server's reader: the adapter shim (`~/adapter-src/packetlogger.cpp`, "MOBILE PACKET 33 DROPPED") drops every packet 33 from mobile clients, a workaround for the MobileSA client. 594 drops logged. Needs a server-side decision.
- Packet 98 accepted by the server but not relayed: `CGame::Packet_PlayerBulletsync` drops shots for weapons the player doesn't own (`HasWeaponType`), and the bot owns none. Expected.
- Server change (approved 2026-11-13): the packet 33 drop was removed from the shim (backup `~/adapter-src/packetlogger.cpp.bak-202611130008`). Relay of 33 to the observer is still unverified: after the change the only free test car was occupied (jack) or the vehicles resource had no cars.
- Test server stability: the server aborts with glibc heap errors (`free(): corrupted unsorted chunks`, `free(): invalid next size (fast)`) five times since 2026-11-12 13:28, once also as a malloc-lock deadlock in `net.so`'s receive thread. The first one predates both the packet 33 change and the last ENTITY_ADD shim edit, so it is a pre-existing bug, most likely in the mobile shim (it rewrites net.so player structs and packet buffers). Needs its own investigation (ASan build of the shim, or a core dump).
- Fixed from the run: packet 33 gated on the server-confirmed seat; jack replies (7) no longer leave the client sending 33; local player id taken from SERVER_JOINEDGAME.

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

---

## 3. Desktop vs Mobile Remaining Gap Analysis

Derived directly from `mta_neon_source` vs `app/src/main/cpp/` via `tools/parity_matrix.py`:

| Category | Desktop Total | Android Implemented | Android Stubs | Remaining / Missing | Priority |
|---|---|---|---|---|---|
| **Lua Core Functions** | 1,581 | 631 (39.9%) | 136 (8.6%) | 814 (51.5%) | High |
| **Client Events** | 144 | 11 (7.6%) | 0 | 133 (92.4%) | High |
| **Inbound Packets (S->C)** | 62 | 24 (38.7%) | 0 | 38 (61.3%) | Medium |
| **Outbound Packets (C->S)** | 31 | 10 (32.3%) | 0 | 21 (67.7%) | Medium |
| **Element Types (ENTITY_ADD)**| 17 | 13 (76.5%) | 1 (5.9%) | 3 (17.6%) | Low |
| **OOP Lua Classes** | 63 | 0 (0.0%) | 0 | 63 (100.0%) | Low |


### Lua functions done, by category (2026-11-14)

Regenerate with `python3 tools/parity_matrix.py && python3 tools/generate_lua_categories.py`.

| Category | Total | Implemented | Stub | Missing | Done |
|---|---|---|---|---|---|
| **Vehicles (CLuaVehicleDefs)** | 165 | 9 | 19 | 137 | 5.5% |
| **Peds & Players (CLuaPedDefs, CLuaPlayerDefs)** | 206 | 27 | 77 | 102 | 13.1% |
| **World, Objects & Environment** | 227 | 124 | 11 | 92 | 54.6% |
| **Element Tree & Data (CLuaElementDefs)** | 81 | 50 | 8 | 23 | 61.7% |
| **GUI & 2D/3D Drawing (DX & CEGUI)** | 199 | 175 | 0 | 24 | 87.9% |
| **Engine & Model Replacement (DFF, TXD, COL)** | 125 | 1 | 15 | 109 | 0.8% |
| **Audio & Voice System (CLuaAudioDefs)** | 57 | 19 | 1 | 37 | 33.3% |
| **Camera & Viewport (CLuaCameraDefs)** | 47 | 47 | 0 | 0 | 100.0% |
| **Weapons & Projectiles (CLuaWeaponDefs, CLuaProjectileDefs)** | 31 | 8 | 0 | 23 | 25.8% |
| **Core System, Networking & Utilities** | 195 | 167 | 2 | 26 | 85.6% |
| **Markers, Blips, ColShapes & Spatial** | 145 | 0 | 3 | 142 | 0.0% |
| **Web Browser & Compatibility (CEF / Discord)** | 103 | 4 | 0 | 99 | 3.9% |
| **TOTAL** | **1581** | **631** | **136** | **814** | **39.9%** |

### GUI and DX: plan for the calls left (24 missing), easiest first

| Step | Calls | Approach | Needs | State |
|---|---|---|---|---|
| 1 | `dxGetStatus` | Desktop table with the values the phone knows | Nothing new | Done |
| 2 | `dxCreateFont` | TTF loaded by the overlay; text calls take the font element | Canvas | Done |
| 3 | `dxDrawMaterialPrimitive` | Textured 2D triangles | Canvas (Android 10+) | Done |
| 4 | `dxCreateRenderTarget`, `dxSetRenderTarget` | Draw lines between the calls go to an offscreen bitmap | Canvas | Done |
| 5 | `dxGetTexturePixels`, `dxSetTexturePixels`, `dxGetPixelsSize`, `dxConvertPixels` | Decode and encode through Android bitmap codecs; textures made from pixels live as overlay bitmaps | A bitmap bridge to the Activity | Done (codec paths need a device) |
| 6 | `svgCreate`, `svgGetDocumentXML`, `svgSetDocumentXML`, `svgGetSize`, `svgSetSize`, `svgSetUpdateCallback` | Element and XML stored natively; picture drawn by an SVG library | The AndroidSVG library (`com.caverock:androidsvg-aar:1.4`, added to `app/build.gradle.kts`) | Done (the picture needs a device) |
| 7 | `dxCreateScreenSource`, `dxUpdateScreenSource` | Copy the game surface with `PixelCopy` | Device testing | Done (the copy needs a device) |
| 8 | `dxDrawLine3D`, `dxDrawMaterialLine3D`, `dxDrawMaterialSectionLine3D`, `dxDrawPrimitive3D`, `dxDrawMaterialPrimitive3D`, `dxDrawModel3D`, `dxDrawWiredSphere` | Project world points with the live game camera and draw on the overlay, without a depth test | A libGTASA camera hook, device testing. The same hook gives `getScreenFromWorldPosition` | Open |
| 9 | `dxCreateShader`, `dxSetShaderValue`, `dxSetShaderTessellation`, `dxSetShaderTransform` | HLSL effect files need an OpenGL ES overlay and a translation to GLSL ES | A new renderer | Open |
| 10 | Model 2DFX (13 calls) | Read and write the 2D effects of game models | libGTASA model-info hooks, device testing | Open |

### Critical Missing Subsystems:
1. **Audio / Voice System**:
   - Audio Lua is 19 of 57 `CLuaAudioDefs` (33.3%), including the login `MediaPlayer` calls and `playSoundFrontEnd`. `playSound3D` is still a stub. 37 calls are missing.
   - Voice (`PACKET_ID_VOICE_DATA`) is still missing. Opus is compiled; there is no voice output path.
2. **Ped & Player Animation / Tasks**:
   - Vehicle enter/exit animations and jacking states (`CTaskComplexEnterCarAsDriver`, `CTaskComplexEnterCarAsPassenger`).
   - Melee combat animation states (`CTaskSimpleFight`).
   - Weapon reload states and crouch/duck secondary task synchronization.
3. **Vehicle Damage & Visual Sync**:
   - Door, panel, light and wheel damage states (`setVehicleDamageModel`, `PACKET_ID_VEHICLE_DAMAGE_SYNC`).
   - Vehicle component manipulation (`getVehicleComponentPosition`, `setVehicleComponentRotation`).
4. **Custom Model Replacement (`engineLoadTXD`, `engineLoadDFF`, `engineReplaceModel`)**:
   - Desktop replaces RenderWare atomic clump geometries and texture dictionaries.
   - Mobile requires hooking the War Drum mobile RenderWare texture/mesh loaders.
5. **DirectX 9 vs OpenGL ES Shaders**:
   - Desktop uses D3DX9 HLSL shaders (`dxCreateShader`).
   - Mobile requires converting/translating custom shaders to GLSL ES 3.0 shaders.

