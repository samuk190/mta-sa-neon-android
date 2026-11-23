# Camera & Viewport (CLuaCameraDefs)

Camera matrix (pos/lookAt), camera target, field of view (FOV), screen shake, fade transitions, clipping planes, and interior viewing.

## Binding Coverage

- **Total Functions:** 47
- **Nonconstant handlers:** 47 (100.0%)
- **Stubs:** 0
- **Missing:** 0

> **Technical Requirement:** Requires hooking libGTASA.so `TheCamera` (`CCamera`) at `0x71...` in ARM64, and matrix interpolation routines.

## Function List

| Lua Function | Status | Desktop Source Ref | Desktop Handler | Conversion Requirement / Implementation Notes |
|---|---|---|---|---|
| `acquireScriptCamera` | **Implemented** | `CLuaCameraDefs.cpp:94` | `ArgumentParser<AcquireScriptCamera>` | Operational on Android. (l_acquireScriptCamera) |
| `fadeCamera` | **Implemented** | `CLuaCameraDefs.cpp:82` | `FadeCamera` | Operational on Android. (l_fadeCamera) |
| `fadeFileCutscene` | **Implemented** | `CLuaCameraDefs.cpp:113` | `ArgumentParser<FadeFileCutscene>` | Operational on Android. (l_fadeFileCutscene) |
| `fadeScriptCamera` | **Implemented** | `CLuaCameraDefs.cpp:102` | `ArgumentParser<FadeScriptCamera>` | Operational on Android. (l_fadeScriptCamera) |
| `getCamera` | **Implemented** | `CLuaCameraDefs.cpp:68` | `ArgumentParserWarn<false, GetCamera>` | Operational on Android. (l_getCamera) |
| `getCameraAimDirection` | **Implemented** | `CLuaCameraDefs.cpp:91` | `GetCameraAimDirection` | Operational on Android. (l_getCameraAimDirection) |
| `getCameraClip` | **Implemented** | `CLuaCameraDefs.cpp:84` | `GetCameraClip` | Operational on Android. (l_getCameraClip) |
| `getCameraDrunkLevel` | **Implemented** | `CLuaCameraDefs.cpp:75` | `ArgumentParserWarn<false, GetCameraDrunkLevel>` | Operational on Android. (l_getCameraDrunkLevel) |
| `getCameraFieldOfView` | **Implemented** | `CLuaCameraDefs.cpp:74` | `GetCameraFieldOfView` | Operational on Android. (l_getCameraFieldOfView) |
| `getCameraGoggleEffect` | **Implemented** | `CLuaCameraDefs.cpp:73` | `ArgumentParserWarn<false, GetCameraGoggleEffect>` | Operational on Android. (l_getCameraGoggleEffect) |
| `getCameraInterior` | **Implemented** | `CLuaCameraDefs.cpp:72` | `ArgumentParserWarn<false, GetCameraInterior>` | Operational on Android. (l_getCameraInterior) |
| `getCameraMatrix` | **Implemented** | `CLuaCameraDefs.cpp:70` | `ArgumentParserWarn<false, GetCameraMatrix>` | Operational on Android. (l_getCameraMatrix) |
| `getCameraTarget` | **Implemented** | `CLuaCameraDefs.cpp:71` | `ArgumentParserWarn<false, GetCameraTarget>` | Operational on Android. (l_getCameraTarget) |
| `getCameraViewMode` | **Implemented** | `CLuaCameraDefs.cpp:69` | `ArgumentParserWarn<false, GetCameraViewMode>` | Operational on Android. (l_getCameraViewMode) |
| `isFileCutsceneFading` | **Implemented** | `CLuaCameraDefs.cpp:114` | `ArgumentParser<IsFileCutsceneFading>` | Operational on Android. (l_isFileCutsceneFading) |
| `isFileCutsceneFinished` | **Implemented** | `CLuaCameraDefs.cpp:115` | `ArgumentParser<IsFileCutsceneFinished>` | Operational on Android. (l_isFileCutsceneFinished) |
| `isFileCutsceneLeaseActive` | **Implemented** | `CLuaCameraDefs.cpp:110` | `ArgumentParser<IsFileCutsceneLeaseActive>` | Operational on Android. (l_isFileCutsceneLeaseActive) |
| `isFileCutsceneLoaded` | **Implemented** | `CLuaCameraDefs.cpp:111` | `ArgumentParser<IsFileCutsceneLoaded>` | Operational on Android. (l_isFileCutsceneLoaded) |
| `isFileCutsceneSkipInputPressed` | **Implemented** | `CLuaCameraDefs.cpp:116` | `ArgumentParser<IsFileCutsceneSkipInputPressed>` | Operational on Android. (l_isFileCutsceneSkipInputPressed) |
| `isScriptCameraFading` | **Implemented** | `CLuaCameraDefs.cpp:103` | `ArgumentParser<IsScriptCameraFading>` | Operational on Android. (l_isScriptCameraFading) |
| `isScriptCameraLeaseActive` | **Implemented** | `CLuaCameraDefs.cpp:96` | `ArgumentParser<IsScriptCameraLeaseActive>` | Operational on Android. (l_isScriptCameraLeaseActive) |
| `isScriptCameraMoveRunning` | **Implemented** | `CLuaCameraDefs.cpp:104` | `ArgumentParser<IsScriptCameraMoveRunning>` | Operational on Android. (l_isScriptCameraMoveRunning) |
| `isScriptCameraTrackRunning` | **Implemented** | `CLuaCameraDefs.cpp:105` | `ArgumentParser<IsScriptCameraTrackRunning>` | Operational on Android. (l_isScriptCameraTrackRunning) |
| `moveScriptCamera` | **Implemented** | `CLuaCameraDefs.cpp:98` | `ArgumentParser<MoveScriptCamera>` | Operational on Android. (l_moveScriptCamera) |
| `releaseFileCutscene` | **Implemented** | `CLuaCameraDefs.cpp:119` | `ArgumentParser<ReleaseFileCutscene>` | Operational on Android. (l_releaseFileCutscene) |
| `releaseScriptCamera` | **Implemented** | `CLuaCameraDefs.cpp:95` | `ArgumentParser<ReleaseScriptCamera>` | Operational on Android. (l_releaseScriptCamera) |
| `requestFileCutscene` | **Implemented** | `CLuaCameraDefs.cpp:109` | `ArgumentParser<RequestFileCutscene>` | Operational on Android. (l_requestFileCutscene) |
| `resetScriptCamera` | **Implemented** | `CLuaCameraDefs.cpp:101` | `ArgumentParser<ResetScriptCamera>` | Operational on Android. (l_resetScriptCamera) |
| `resetShakeCamera` | **Implemented** | `CLuaCameraDefs.cpp:90` | `ArgumentParser<ResetShakeCamera>` | Operational on Android. (l_resetShakeCamera) |
| `setCameraAimDirection` | **Implemented** | `CLuaCameraDefs.cpp:92` | `SetCameraAimDirection` | Operational on Android. (l_setCameraAimDirection) |
| `setCameraClip` | **Implemented** | `CLuaCameraDefs.cpp:83` | `SetCameraClip` | Operational on Android. (l_setCameraClip) |
| `setCameraDrunkLevel` | **Implemented** | `CLuaCameraDefs.cpp:87` | `ArgumentParserWarn<false, SetCameraDrunkLevel>` | Operational on Android. (l_setCameraDrunkLevel) |
| `setCameraFieldOfView` | **Implemented** | `CLuaCameraDefs.cpp:79` | `SetCameraFieldOfView` | Operational on Android. (l_setCameraFieldOfView) |
| `setCameraGoggleEffect` | **Implemented** | `CLuaCameraDefs.cpp:86` | `SetCameraGoggleEffect` | Operational on Android. (l_setCameraGoggleEffect) |
| `setCameraInterior` | **Implemented** | `CLuaCameraDefs.cpp:81` | `SetCameraInterior` | Operational on Android. (l_setCameraInterior) |
| `setCameraMatrix` | **Implemented** | `CLuaCameraDefs.cpp:78` | `SetCameraMatrix` | Operational on Android. (l_setCameraMatrix) |
| `setCameraTarget` | **Implemented** | `CLuaCameraDefs.cpp:80` | `SetCameraTarget` | Operational on Android. (l_setCameraTarget) |
| `setCameraViewMode` | **Implemented** | `CLuaCameraDefs.cpp:85` | `ArgumentParserWarn<false, SetCameraViewMode>` | Operational on Android. (l_setCameraViewMode) |
| `setScriptCameraFixed` | **Implemented** | `CLuaCameraDefs.cpp:97` | `ArgumentParser<SetScriptCameraFixed>` | Operational on Android. (l_setScriptCameraFixed) |
| `setScriptCameraNearClip` | **Implemented** | `CLuaCameraDefs.cpp:107` | `ArgumentParser<SetScriptCameraNearClip>` | Operational on Android. (l_setScriptCameraNearClip) |
| `setScriptCameraPersist` | **Implemented** | `CLuaCameraDefs.cpp:100` | `ArgumentParser<SetScriptCameraPersist>` | Operational on Android. (l_setScriptCameraPersist) |
| `setScriptCameraWidescreen` | **Implemented** | `CLuaCameraDefs.cpp:106` | `ArgumentParser<SetScriptCameraWidescreen>` | Operational on Android. (l_setScriptCameraWidescreen) |
| `shakeCamera` | **Implemented** | `CLuaCameraDefs.cpp:89` | `ArgumentParser<ShakeCamera>` | Operational on Android. (l_shakeCamera) |
| `skipFileCutscene` | **Implemented** | `CLuaCameraDefs.cpp:118` | `ArgumentParser<SkipFileCutscene>` | Operational on Android. (l_skipFileCutscene) |
| `startFileCutscene` | **Implemented** | `CLuaCameraDefs.cpp:112` | `ArgumentParser<StartFileCutscene>` | Operational on Android. (l_startFileCutscene) |
| `trackScriptCamera` | **Implemented** | `CLuaCameraDefs.cpp:99` | `ArgumentParser<TrackScriptCamera>` | Operational on Android. (l_trackScriptCamera) |
| `wasFileCutsceneSkipped` | **Implemented** | `CLuaCameraDefs.cpp:117` | `ArgumentParser<WasFileCutsceneSkipped>` | Operational on Android. (l_wasFileCutsceneSkipped) |
