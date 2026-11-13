# Camera & Viewport (CLuaCameraDefs)

Camera matrix (pos/lookAt), camera target, field of view (FOV), screen shake, fade transitions, clipping planes, and interior viewing.

## Parity Status

- **Total Functions:** 47
- **Implemented:** 1 (2.1%)
- **Stubs:** 5
- **Missing:** 41

> **Technical Requirement:** Requires hooking libGTASA.so `TheCamera` (`CCamera`) at `0x71...` in ARM64, and matrix interpolation routines.

## Function List

| Lua Function | Status | Desktop Source Ref | Desktop Handler | Conversion Requirement / Implementation Notes |
|---|---|---|---|---|
| `acquireScriptCamera` | Missing | `CLuaCameraDefs.cpp:94` | `ArgumentParser<AcquireScriptCamera>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `fadeCamera` | *Stub* | `CLuaCameraDefs.cpp:82` | `FadeCamera` | Currently stubbed: `DefaultValue(kTrue)`. Needs native engine wiring. |
| `fadeFileCutscene` | Missing | `CLuaCameraDefs.cpp:113` | `ArgumentParser<FadeFileCutscene>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `fadeScriptCamera` | Missing | `CLuaCameraDefs.cpp:102` | `ArgumentParser<FadeScriptCamera>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getCamera` | Missing | `CLuaCameraDefs.cpp:68` | `ArgumentParserWarn<false, GetCamera>` | Needs reading/writing CCamera singleton in libGTASA.so. |
| `getCameraAimDirection` | Missing | `CLuaCameraDefs.cpp:91` | `GetCameraAimDirection` | Needs reading/writing CCamera singleton in libGTASA.so. |
| `getCameraClip` | Missing | `CLuaCameraDefs.cpp:84` | `GetCameraClip` | Needs reading/writing CCamera singleton in libGTASA.so. |
| `getCameraDrunkLevel` | Missing | `CLuaCameraDefs.cpp:75` | `ArgumentParserWarn<false, GetCameraDrunkLevel>` | Needs reading/writing CCamera singleton in libGTASA.so. |
| `getCameraFieldOfView` | Missing | `CLuaCameraDefs.cpp:74` | `GetCameraFieldOfView` | Needs reading/writing CCamera singleton in libGTASA.so. |
| `getCameraGoggleEffect` | Missing | `CLuaCameraDefs.cpp:73` | `ArgumentParserWarn<false, GetCameraGoggleEffect>` | Needs reading/writing CCamera singleton in libGTASA.so. |
| `getCameraInterior` | Missing | `CLuaCameraDefs.cpp:72` | `ArgumentParserWarn<false, GetCameraInterior>` | Needs reading/writing CCamera singleton in libGTASA.so. |
| `getCameraMatrix` | **Implemented** | `CLuaCameraDefs.cpp:70` | `ArgumentParserWarn<false, GetCameraMatrix>` | Operational on Android. (GetCameraMatrix) |
| `getCameraTarget` | Missing | `CLuaCameraDefs.cpp:71` | `ArgumentParserWarn<false, GetCameraTarget>` | Needs reading/writing CCamera singleton in libGTASA.so. |
| `getCameraViewMode` | Missing | `CLuaCameraDefs.cpp:69` | `ArgumentParserWarn<false, GetCameraViewMode>` | Needs reading/writing CCamera singleton in libGTASA.so. |
| `isFileCutsceneFading` | Missing | `CLuaCameraDefs.cpp:114` | `ArgumentParser<IsFileCutsceneFading>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `isFileCutsceneFinished` | Missing | `CLuaCameraDefs.cpp:115` | `ArgumentParser<IsFileCutsceneFinished>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `isFileCutsceneLeaseActive` | Missing | `CLuaCameraDefs.cpp:110` | `ArgumentParser<IsFileCutsceneLeaseActive>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `isFileCutsceneLoaded` | Missing | `CLuaCameraDefs.cpp:111` | `ArgumentParser<IsFileCutsceneLoaded>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `isFileCutsceneSkipInputPressed` | Missing | `CLuaCameraDefs.cpp:116` | `ArgumentParser<IsFileCutsceneSkipInputPressed>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `isScriptCameraFading` | Missing | `CLuaCameraDefs.cpp:103` | `ArgumentParser<IsScriptCameraFading>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `isScriptCameraLeaseActive` | Missing | `CLuaCameraDefs.cpp:96` | `ArgumentParser<IsScriptCameraLeaseActive>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `isScriptCameraMoveRunning` | Missing | `CLuaCameraDefs.cpp:104` | `ArgumentParser<IsScriptCameraMoveRunning>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `isScriptCameraTrackRunning` | Missing | `CLuaCameraDefs.cpp:105` | `ArgumentParser<IsScriptCameraTrackRunning>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `moveScriptCamera` | Missing | `CLuaCameraDefs.cpp:98` | `ArgumentParser<MoveScriptCamera>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `releaseFileCutscene` | Missing | `CLuaCameraDefs.cpp:119` | `ArgumentParser<ReleaseFileCutscene>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `releaseScriptCamera` | Missing | `CLuaCameraDefs.cpp:95` | `ArgumentParser<ReleaseScriptCamera>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `requestFileCutscene` | Missing | `CLuaCameraDefs.cpp:109` | `ArgumentParser<RequestFileCutscene>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `resetScriptCamera` | Missing | `CLuaCameraDefs.cpp:101` | `ArgumentParser<ResetScriptCamera>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `resetShakeCamera` | Missing | `CLuaCameraDefs.cpp:90` | `ArgumentParser<ResetShakeCamera>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setCameraAimDirection` | Missing | `CLuaCameraDefs.cpp:92` | `SetCameraAimDirection` | Needs reading/writing CCamera singleton in libGTASA.so. |
| `setCameraClip` | Missing | `CLuaCameraDefs.cpp:83` | `SetCameraClip` | Needs reading/writing CCamera singleton in libGTASA.so. |
| `setCameraDrunkLevel` | Missing | `CLuaCameraDefs.cpp:87` | `ArgumentParserWarn<false, SetCameraDrunkLevel>` | Needs reading/writing CCamera singleton in libGTASA.so. |
| `setCameraFieldOfView` | *Stub* | `CLuaCameraDefs.cpp:79` | `SetCameraFieldOfView` | Currently stubbed: `DefaultValue(kTrue)`. Needs native engine wiring. |
| `setCameraGoggleEffect` | Missing | `CLuaCameraDefs.cpp:86` | `SetCameraGoggleEffect` | Needs reading/writing CCamera singleton in libGTASA.so. |
| `setCameraInterior` | *Stub* | `CLuaCameraDefs.cpp:81` | `SetCameraInterior` | Currently stubbed: `DefaultValue(kTrue)`. Needs native engine wiring. |
| `setCameraMatrix` | *Stub* | `CLuaCameraDefs.cpp:78` | `SetCameraMatrix` | Currently stubbed: `l_true: lua_pushboolean(L, 1)`. Needs native engine wiring. |
| `setCameraTarget` | *Stub* | `CLuaCameraDefs.cpp:80` | `SetCameraTarget` | Currently stubbed: `DefaultValue(kTrue)`. Needs native engine wiring. |
| `setCameraViewMode` | Missing | `CLuaCameraDefs.cpp:85` | `ArgumentParserWarn<false, SetCameraViewMode>` | Needs reading/writing CCamera singleton in libGTASA.so. |
| `setScriptCameraFixed` | Missing | `CLuaCameraDefs.cpp:97` | `ArgumentParser<SetScriptCameraFixed>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setScriptCameraNearClip` | Missing | `CLuaCameraDefs.cpp:107` | `ArgumentParser<SetScriptCameraNearClip>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setScriptCameraPersist` | Missing | `CLuaCameraDefs.cpp:100` | `ArgumentParser<SetScriptCameraPersist>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setScriptCameraWidescreen` | Missing | `CLuaCameraDefs.cpp:106` | `ArgumentParser<SetScriptCameraWidescreen>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `shakeCamera` | Missing | `CLuaCameraDefs.cpp:89` | `ArgumentParser<ShakeCamera>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `skipFileCutscene` | Missing | `CLuaCameraDefs.cpp:118` | `ArgumentParser<SkipFileCutscene>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `startFileCutscene` | Missing | `CLuaCameraDefs.cpp:112` | `ArgumentParser<StartFileCutscene>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `trackScriptCamera` | Missing | `CLuaCameraDefs.cpp:99` | `ArgumentParser<TrackScriptCamera>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `wasFileCutsceneSkipped` | Missing | `CLuaCameraDefs.cpp:117` | `ArgumentParser<WasFileCutsceneSkipped>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
