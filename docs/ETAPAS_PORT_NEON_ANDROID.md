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
| **P2. Resources & Game Session** | P1A.4, P1B.2 | Root element, resource start/stop, HTTP asset cache, diagnostic Lua, and client-server events. | In Progress (RESOURCE_START file list parsed for both live layouts; HTTP client-file and CLIENT_SCRIPT download with size/CRC check verified live with the headless client: 1482 files, 0 failures; ready resources run their scripts (headless: 30 of 30 started, 0 Lua errors and 0 unimplemented calls in 90 s); device run pending. Done since: PLAYER_RESOURCE_START sent after each start (live: 30 of 30; the 9 mobile-shim starts send an assumed counter, acceptance unverified), RESOURCE_STOP and its handler/timer cleanup (host test only), fromJSON/toJSON (host test on the real catalog; the live loader calls it only on a map request), ServerWorld reads the parsed descriptor. Open: RESOURCE_STOP run live, and a device run of the whole P2 path) |
| **P3. World & Entities Sync** | P2 | Peds, vehicles, and objects visible across both clients. Streaming, ownership, movement, health, and respawn tested. | In Progress (server peds created on the S25 after the desktop `0x60D64D` group patch; pool vehicles and the unoccupied sync sender (packet 56) built, device run pending; driver puresync send, local enter/exit handshake (packet 58) verified live with the headless client; driver sync (33) relayed to another player verified live (positions, vehicle and player health); driver puresync send (packet 33) with pad keys and vehicle health built, device run pending; SYNC_SETTINGS weapon list parsed; bullet sync send (packet 98) built, device run pending; remote driver and passenger seats and driver motion built, device run pending; jacking and enter/exit animations not ported) |
| **P3.1 Disconnect Recovery** | P1B.2, P3 | When the server drops the session, show a dialog with **Retry** and **Quit** (same flow as the MTA desktop client). Retry runs a fresh join (`PLAYER_JOIN` → `MOD_NAME` → `PLAYER_JOINDATA` → `SERVER_JOIN_COMPLETE`) and spawns the character in the world as on a first connection: remote peds, vehicles and the local player are rebuilt, nothing is kept from the old session. Quit returns to the server browser. Also handles `ForceReconnect` (server RPC). | Implemented and verified on the S25: `/disconnect` shows the dialog and Retry rejoins with a fresh spawn |
| **P3.2 Chat Layout** | P2 | The chat does not match the MTA desktop client: lines are spaced too far apart and the layout differs. Match the desktop chat box (line height, padding, font, colour codes, and the line limit) so both clients show the same chat. Verify with a screenshot of the desktop and the phone side by side. | Implemented (line step = font height, one background box); side-by-side desktop screenshot pending |
| **P4. Lua, DX, GUI & Browser** | P2, P3 | RenderWare 2D DX draw calls, custom UI interactions, CEGUI/WebView bindings. | Pending |
| **P5. Neon Parity Matrix** | P3, P4 | Complete API matrix, events, and elements matching MTA Neon desktop capabilities. | Pending |
| **P6. Release Distribution** | P5 | Reproducible release builds, multi-device hardware validation, automated updates. | Pending |

P1A (Engine/Platform) and P1B (Network Transport) are independent parallel tracks. The approval of the port requires a fully native Android ARM64 session connecting to a live server.

---

## 2. Historical Execution Log

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
