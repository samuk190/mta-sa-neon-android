# Loading progress: engine, then server resources

Goal: one loading screen from Play to the first frame of gameplay. Its bar moves on real numbers only.

Status: steps 1-4 are implemented (phases, engine percentage, weighted bar, Lua transfer events), not yet run on a device. Step 5 is not done: both the Neon desktop and the phone draw a native screen above any server dx screen, so the useful server work is download order (`download_priority_group`), not another screen.

## What the desktop client does

### Upstream MTA (what the community builds on)

- The engine shows the **transfer box** while resource files download: "Download Progress: X of Y" (`CTransferBox.cpp`). It is fed by `CResourceFileDownloadManager::DoPulse` (`Client/mods/deathmatch/logic/CResourceFileDownloadManager.cpp:126-238`):
  - The total is the sum of `GetDownloadSize()` of every queued file (`AddDownloadSize`, 245-269).
  - The done value is `GetDownloadSizeNow()` of each HTTP channel.
- Every pulse fires `onClientTransferBoxProgressChange(downloadedBytes, totalBytes)` on root (line 210). Show and hide fire `onClientTransferBoxVisibilityChange(isVisible)` (225, 263). These are registered in `CClientGame.cpp:2912-2913`.
- How servers usually build their own loading screens (MTA wiki, meta.xml / transfer box pages):
  1. A small loading-screen resource with `<download_priority_group>` above 0 in its meta.xml. The default is 0, and a higher group is downloaded **and started** first. It is up and drawing before the big resources download.
  2. It calls `setTransferBoxVisible(false)` and draws its own bar from `onClientTransferBoxProgressChange`, as `downloaded / total`.
  3. Big optional files are marked `<file download="false">` and fetched later with `downloadFile` (`onClientFileDownloadComplete`). Alternatively they go in a resource with a negative priority group, so they do not block joining.
- Downloads run in priority-group order. A group finishes, its resources start ("Processing resources"), and then the next group starts (`UpdatePendingDownloads`, 49-91).

### Neon desktop fork (`mta_neon_source`)

It adds a native full-screen connection loading screen with these phases:

| Phase text | Where | Progress |
|---|---|---|
| Starting San Andreas | `CPacketHandler.cpp:344` (`BeginConnectionLoading`) | indeterminate |
| Preparing server data | `CPacketHandler.cpp:399` | indeterminate |
| Preparing world | `CPacketHandler.cpp:2989` (entity add) | real %: `CCore::UpdateDummyProgress` with type `"%"` (`CCore.cpp:3870-3890`) |
| Downloading resources | `CResourceFileDownloadManager.cpp:202, 250` | bytes done / total bytes |
| Processing resources | `CResourceFileDownloadManager.cpp:231`, `CPacketHandler.cpp:5255` | indeterminate (script load can stall after 100% network) |
| (end) | `CClientGame::PulseConnectionLoading` (`CClientGame.cpp:3657-3672`) | ends after 1500 ms with no new loading activity, once joined, game loaded and no initial files left |

`UpdateConnectionLoading` only works while the initial connection window is open (`CClientGame.cpp:3637-3646`). Resource starts during normal play do not bring the screen back.

## What the phone has today

- **Engine boot:** GTA's own loading screen runs inside `libGTASA.so`. Its real counters are exported:
  - `CLoadingScreen::m_PercentLoaded` (B `0xc20f04`)
  - `CLoadingScreen::m_nChunksToLoad` (D `0x886b50`)
  - `CLoadingScreen::NewChunkLoaded()`, `SetChunksToLoad(int, bool)`, `SetLoadingBarMsg(const char*, const char*)`
  - `LoadingScreen(const char*, const char*, const char*)`

  The launcher does not read any of them.
- **Connect:** `EngineHostActivity.startMtaSession` shows "Connecting to server" at 0%. Only one kind-2 line ever reaches it: `"100|"`, from `PacketHandler.cpp:502` on the local spawn. The phases in between are never reported.
- **Downloads:** `HttpDownloader` keeps `g_transferTotal` and `g_transferDone`, the same meaning as the desktop transfer box. `JNI_OnLoad.cpp:625` forwards them as kind-3 lines to `MtaTransferBox`.
- **Lua:** `setTransferBoxVisible` and `isTransferBoxVisible` exist (`LuaUtilApi.cpp:1048-1058`). `onClientTransferBoxProgressChange` and `onClientTransferBoxVisibilityChange` are **not fired**, so a server's own loading-screen resource cannot draw progress.
- **Priority groups:** `ResourceFiles` parses `downloadPriorityGroup` (`ResourceFiles.h:50`, desktop layout only). Check that the download queue and resource start honor it before relying on it.

## Proposed design

One overlay on `EngineHostActivity`, made with the launcher's Material 3 views (server logo, phase text, one `LinearProgressIndicator`). It is fed by native phase events.

1. **New JNI line kind, `LOADING` (`phase|progress|text`).** Progress is 0..1, or -1 for indeterminate. Native sends it at the same points as the Neon desktop fork:
   - `BeginConnectionLoading("Starting San Andreas")` when the join starts.
   - "Preparing server data" and "Preparing world" with the entity count percent from `EntityAdd*`.
   - "Downloading resources" from `HttpDownloader::GetTransfer` (bytes).
   - "Processing resources" when a download group finishes, with Lua resource start.
   - End with the desktop rule: joined, game loaded, no initial files left, then 1500 ms without loading activity (`PulseConnectionLoading`).
2. **Engine phase.** Before the server phases, poll `CLoadingScreen::m_PercentLoaded` (and `m_nChunksToLoad`) through `dlsym` on the game thread. Map it to the bar while GTA loads. This is real engine progress, not a timer. Confirm on the device that the field moves during boot before trusting it. If it stays 0, hook `NewChunkLoaded` and count against `m_nChunksToLoad`.
3. **One bar, weighted segments.** Example: engine 0-30%, world 30-40%, downloads 40-90%, processing 90-100%. Each segment advances only from its own real counter. Indeterminate phases show the indeterminate style instead of a fake value.
4. **Fire the transfer events in Lua.** Each pulse while downloading, fire `onClientTransferBoxProgressChange(done, total)` on root. Fire `onClientTransferBoxVisibilityChange(true/false)` when the transfer starts and ends. Then servers can use the usual setup: a loading-screen resource with a high `download_priority_group` and `setTransferBoxVisible(false)`. When a script hid the transfer box, the native overlay also hides its download segment.
5. **World RPG server side (optional).** A small `[worldrpg]` loading resource with `download_priority_group` 100 that draws the server's own screen. Heavy mods move to `download="false"` or a negative group, so players reach the login sooner.

Order of work: 1 (phases and overlay), then 4 (Lua events: small, with a desktop reference), then 2 (engine counter, needs device verification), then 5.
