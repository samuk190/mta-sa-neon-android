# Core System, Networking & Utilities

Timers, event handling, network latent transfers, file I/O, XML parsing, JSON serialization, UTF strings, and cryptography.

## Binding Coverage

- **Total Functions:** 195
- **Nonconstant handlers:** 195 (100.0%)
- **Stubs:** 0
- **Missing:** 0

> **Technical Requirement:** Pure CPU/platform logic. Mostly ported or needs standard library wrappers (e.g. tinyxml2, sha256, zlib).

## Function List

| Lua Function | Status | Desktop Source Ref | Desktop Handler | Conversion Requirement / Implementation Notes |
|---|---|---|---|---|
| `abortRemoteRequest` | **Implemented** | `CLuaNetworkDefs.cpp:29` | `AbortRemoteRequest` | Operational on Android. (LuaAbortRemoteRequest) |
| `addClothingModel` | **Implemented** | `CLuaManager.cpp:193` | `CLuaFunctionDefs::AddClothingModel` | Operational on Android. (l_addClothingModel) |
| `addCommandHandler` | **Implemented** | `CLuaManager.cpp:225` | `CLuaFunctionDefs::AddCommandHandler` | Operational on Android. (l_addCommandHandler) |
| `addDebugHook` | **Implemented** | `CLuaManager.cpp:236` | `CLuaFunctionDefs::AddDebugHook` | Operational on Android. (l_addDebugHook) |
| `addEvent` | **Implemented** | `CLuaManager.cpp:163` | `CLuaFunctionDefs::AddEvent` | Operational on Android. (l_addEvent) |
| `addEventHandler` | **Implemented** | `CLuaManager.cpp:164` | `CLuaFunctionDefs::AddEventHandler` | Operational on Android. (l_addEventHandler) |
| `base64Decode` | **Implemented** | `CLuaCryptDefs.cpp:28` | `ArgumentParserWarn<false, Base64decode>` | Operational on Android. (l_base64Decode) |
| `base64Encode` | **Implemented** | `CLuaCryptDefs.cpp:27` | `ArgumentParserWarn<false, Base64encode>` | Operational on Android. (l_base64Encode) |
| `bindKey` | **Implemented** | `CLuaManager.cpp:209` | `CLuaFunctionDefs::BindKey` | Operational on Android. (BindKey) |
| `bitAnd` | **Implemented** | `CLuaBitDefs.cpp:18` | `bitAnd` | Operational on Android. (BitAnd) |
| `bitArShift` | **Implemented** | `CLuaBitDefs.cpp:20` | `bitArShift` | Operational on Android. (BitArShift) |
| `bitExtract` | **Implemented** | `CLuaBitDefs.cpp:22` | `bitExtract` | Operational on Android. (BitExtract) |
| `bitLRotate` | **Implemented** | `CLuaBitDefs.cpp:20` | `bitLRotate` | Operational on Android. (BitLRotate) |
| `bitLShift` | **Implemented** | `CLuaBitDefs.cpp:20` | `bitLShift` | Operational on Android. (BitLShift) |
| `bitNot` | **Implemented** | `CLuaBitDefs.cpp:18` | `bitNot` | Operational on Android. (BitNot) |
| `bitOr` | **Implemented** | `CLuaBitDefs.cpp:18` | `bitOr` | Operational on Android. (BitOr) |
| `bitRRotate` | **Implemented** | `CLuaBitDefs.cpp:20` | `bitRRotate` | Operational on Android. (BitRRotate) |
| `bitRShift` | **Implemented** | `CLuaBitDefs.cpp:20` | `bitRShift` | Operational on Android. (BitRShift) |
| `bitReplace` | **Implemented** | `CLuaBitDefs.cpp:22` | `bitReplace` | Operational on Android. (BitReplace) |
| `bitTest` | **Implemented** | `CLuaBitDefs.cpp:18` | `bitTest` | Operational on Android. (BitTest) |
| `bitXor` | **Implemented** | `CLuaBitDefs.cpp:18` | `bitXor` | Operational on Android. (BitXor) |
| `call` | **Implemented** | `CLuaResourceDefs.cpp:20` | `Call` | Resolves the function in the target resource environment, requires a declared export, and runs it with the target owner and caller source fields. |
| `cancelEvent` | **Implemented** | `CLuaManager.cpp:169` | `CLuaFunctionDefs::CancelEvent` | Operational on Android. (l_cancelEvent) |
| `cancelLatentEvent` | **Implemented** | `CLuaManager.cpp:174` | `CLuaFunctionDefs::CancelLatentEvent` | Operational on Android. (l_cancelLatentEvent) |
| `clearChatBox` | **Implemented** | `CLuaManager.cpp:182` | `CLuaFunctionDefs::ClearChatBox` | Operational on Android. (ClearChatBox) |
| `clearDebugBox` | **Implemented** | `CLuaClientDefs.cpp:25` | `ArgumentParser<ClearDebug>` | clearDebugBox follows CLuaClientDefs.cpp:72-76 and returns true. There is no CDebugView, so nothing is cleared. isMTAWindowFocused reads m_bFocused. The activity reports foreground focus; the VM copies it on the next pump and fires onClientMTAFocusChange. Host-tested. Not device-verified. |
| `createTrayNotification` | **Implemented** | `CLuaManager.cpp:185` | `CLuaFunctionDefs::CreateTrayNotification` | setWindowFlashing follows CStaticFunctionDefinitions.cpp:308-323. A focused window or a disabled server_can_flash_window cvar returns false. FlashWindowEx is not hooked, so an allowed call returns true and does not flash. createTrayNotification follows CStaticFunctionDefinitions.cpp:325-333 and CTrayIcon.cpp:101-137. allow_tray_notifications defaults to true. Shell_NotifyIcon is not hooked, so the call returns false. Host-tested. Not device-verified. |
| `debugSleep` | **Implemented** | `CLuaUtilDefs.cpp:70` | `DebugSleep` | Follows CLuaUtilDefs.cpp:611-648. A bad number returns false. Outside development mode it logs an error and returns false. In development mode it delivers finished fetchRemote calls, then sleeps. Host-tested. |
| `decodeString` | **Implemented** | `CLuaCryptDefs.cpp:33` | `DecodeString` | Operational on Android. (DecodeString) |
| `deref` | **Implemented** | `CLuaUtilDefs.cpp:44` | `Dereference` | Operational on Android. (l_deref) |
| `dofile` | **Implemented** | `CLuaMain.cpp:86` | `CLuaUtilDefs::DisabledFunction` | Disabled in the client. Returns false. |
| `downloadFile` | **Implemented** | `CLuaManager.cpp:206` | `CLuaFunctionDefs::DownloadFile` | Queues a declared resource file and fires `onClientFileDownloadComplete` after the cache or download check. |
| `encodeString` | **Implemented** | `CLuaCryptDefs.cpp:32` | `EncodeString` | Operational on Android. (EncodeString) |
| `executeCommandHandler` | **Implemented** | `CLuaManager.cpp:227` | `CLuaFunctionDefs::ExecuteCommandHandler` | Operational on Android. (l_executeCommandHandler) |
| `fetchRemote` | **Implemented** | `CLuaNetworkDefs.cpp:26` | `FetchRemote` | Operational on Android. (LuaFetchRemote) |
| `fileClose` | **Implemented** | `CLuaFileDefs.cpp:47` | `fileClose` | Operational on Android. (l_fileClose) |
| `fileCopy` | **Implemented** | `CLuaFileDefs.cpp:44` | `fileCopy` | Operational on Android. (l_fileCopy) |
| `fileCreate` | **Implemented** | `CLuaFileDefs.cpp:42` | `fileCreate` | Operational on Android. (l_fileCreate) |
| `fileDelete` | **Implemented** | `CLuaFileDefs.cpp:46` | `fileDelete` | Operational on Android. (l_fileDelete) |
| `fileExists` | **Implemented** | `CLuaFileDefs.cpp:43` | `fileExists` | Operational on Android. (l_fileExists) |
| `fileFlush` | **Implemented** | `CLuaFileDefs.cpp:48` | `fileFlush` | Operational on Android. (l_fileFlush) |
| `fileGetContents` | **Implemented** | `CLuaFileDefs.cpp:56` | `ArgumentParser<fileGetContents>` | Operational on Android. (l_fileGetContents) |
| `fileGetHash` | **Implemented** | `CLuaFileDefs.cpp:57` | `ArgumentParser<fileGetHash>` | Operational on Android. (l_fileGetHash) |
| `fileGetPath` | **Implemented** | `CLuaFileDefs.cpp:53` | `fileGetPath` | Operational on Android. (l_fileGetPath) |
| `fileGetPos` | **Implemented** | `CLuaFileDefs.cpp:51` | `fileGetPos` | Operational on Android. (l_fileGetPos) |
| `fileGetSize` | **Implemented** | `CLuaFileDefs.cpp:52` | `fileGetSize` | Operational on Android. (l_fileGetSize) |
| `fileIsEOF` | **Implemented** | `CLuaFileDefs.cpp:54` | `fileIsEOF` | Operational on Android. (l_fileIsEOF) |
| `fileOpen` | **Implemented** | `CLuaFileDefs.cpp:41` | `fileOpen` | Operational on Android. (l_fileOpen) |
| `fileRead` | **Implemented** | `CLuaFileDefs.cpp:49` | `fileRead` | Operational on Android. (l_fileRead) |
| `fileRename` | **Implemented** | `CLuaFileDefs.cpp:45` | `fileRename` | Operational on Android. (l_fileRename) |
| `fileSetPos` | **Implemented** | `CLuaFileDefs.cpp:55` | `fileSetPos` | Operational on Android. (l_fileSetPos) |
| `fileWrite` | **Implemented** | `CLuaFileDefs.cpp:50` | `fileWrite` | Operational on Android. (l_fileWrite) |
| `fromJSON` | **Implemented** | `CLuaUtilDefs.cpp:62` | `fromJSON` | Operational on Android. (FromJson) |
| `generateKeyPair` | **Implemented** | `CLuaCryptDefs.cpp:30` | `ArgumentParser<GenerateKeyPair>` | Operational on Android. (GenerateKeyPair) |
| `getAnalogControlState` | **Implemented** | `CLuaManager.cpp:212` | `CLuaFunctionDefs::GetAnalogControlState` | Operational on Android. (GetAnalogControlState) |
| `getBodyPartName` | **Implemented** | `CLuaManager.cpp:189` | `CLuaFunctionDefs::GetBodyPartName` | Operational on Android. (l_getBodyPartName) |
| `getBoundKeys` | **Implemented** | `CLuaManager.cpp:215` | `CLuaFunctionDefs::GetBoundKeys` | Queries command bindings across resources; no physical GTA-control keyboard mappings on mobile yet. Host-tested; not device-verified. (GetBoundKeys) |
| `getClothesByTypeIndex` | **Implemented** | `CLuaManager.cpp:190` | `CLuaFunctionDefs::GetClothesByTypeIndex` | Operational on Android. (l_getClothesByTypeIndex) |
| `getClothesTypeName` | **Implemented** | `CLuaManager.cpp:192` | `CLuaFunctionDefs::GetClothesTypeName` | Operational on Android. (l_getClothesTypeName) |
| `getColorFromString` | **Implemented** | `CLuaUtilDefs.cpp:52` | `GetColorFromString` | Operational on Android. (l_getColorFromString) |
| `getCommandHandlers` | **Implemented** | `CLuaManager.cpp:228` | `CLuaFunctionDefs::GetCommandHandlers` | Operational on Android. (l_getCommandHandlers) |
| `getCommandsBoundToKey` | **Implemented** | `CLuaManager.cpp:218` | `CLuaFunctionDefs::GetCommandsBoundToKey` | Operational on Android. (GetCommandsBoundToKey) |
| `getCursorAlpha` | **Implemented** | `CLuaManager.cpp:201` | `CLuaFunctionDefs::GetCursorAlpha` | Operational on Android. (l_getCursorAlpha) |
| `getCursorPosition` | **Implemented** | `CLuaManager.cpp:197` | `CLuaFunctionDefs::GetCursorPosition` | Operational on Android. (l_getCursorPosition) |
| `getDevelopmentMode` | **Implemented** | `CLuaManager.cpp:235` | `CLuaFunctionDefs::GetDevelopmentMode` | Operational on Android. (l_getDevelopmentMode) |
| `getDistanceBetweenPoints2D` | **Implemented** | `CLuaUtilDefs.cpp:55` | `GetDistanceBetweenPoints2D` | Operational on Android. (l_getDistanceBetweenPoints2D) |
| `getDistanceBetweenPoints3D` | **Implemented** | `CLuaUtilDefs.cpp:56` | `GetDistanceBetweenPoints3D` | Operational on Android. (l_getDistanceBetweenPoints3D) |
| `getEasingValue` | **Implemented** | `CLuaUtilDefs.cpp:57` | `GetEasingValue` | Operational on Android. (l_getEasingValue) |
| `getEventHandlers` | **Implemented** | `CLuaManager.cpp:166` | `CLuaFunctionDefs::GetEventHandlers` | Operational on Android. (l_getEventHandlers) |
| `getFunctionsBoundToKey` | **Implemented** | `CLuaManager.cpp:216` | `CLuaFunctionDefs::GetFunctionsBoundToKey` | Operational on Android. (GetFunctionsBoundToKey) |
| `getKeyBoundToCommand` | **Implemented** | `CLuaManager.cpp:219` | `CLuaFunctionDefs::GetKeyBoundToCommand` | Operational on Android. (GetKeyBoundToCommand) |
| `getKeyBoundToFunction` | **Implemented** | `CLuaManager.cpp:217` | `CLuaFunctionDefs::GetKeyBoundToFunction` | Operational on Android. (GetKeyBoundToFunction) |
| `getKeyState` | **Implemented** | `CLuaManager.cpp:211` | `CLuaFunctionDefs::GetKeyState` | Operational on Android. (GetKeyState) |
| `getKeyboardLayout` | **Implemented** | `CLuaManager.cpp:244` | `CLuaFunctionDefs::GetKeyboardLayout` | Operational on Android. (l_getKeyboardLayout) |
| `getLatentEventHandles` | **Implemented** | `CLuaManager.cpp:172` | `CLuaFunctionDefs::GetLatentEventHandles` | Operational on Android. (l_getLatentEventHandles) |
| `getLatentEventStatus` | **Implemented** | `CLuaManager.cpp:173` | `CLuaFunctionDefs::GetLatentEventStatus` | Operational on Android. (l_getLatentEventStatus) |
| `getLocalization` | **Implemented** | `CLuaManager.cpp:243` | `CLuaFunctionDefs::GetLocalization` | Operational on Android. (l_getLocalization) |
| `getNeonClientSetting` | **Implemented** | `CLuaClientDefs.cpp:28` | `ArgumentParser<GetNeonClientSetting>` | Operational on Android. (l_getNeonClientSetting) |
| `getNetworkStats` | **Implemented** | `CLuaManager.cpp:232` | `CLuaFunctionDefs::GetNetworkStats` | Operational on Android. (l_getNetworkStats) |
| `getNetworkUsageData` | **Implemented** | `CLuaManager.cpp:231` | `CLuaFunctionDefs::GetNetworkUsageData` | Operational on Android. (l_getNetworkUsageData) |
| `getPerformanceStats` | **Implemented** | `CLuaManager.cpp:233` | `CLuaFunctionDefs::GetPerformanceStats` | Operational on Android. (l_getPerformanceStats) |
| `getProcessMemoryStats` | **Implemented** | `CLuaUtilDefs.cpp:75` | `ArgumentParser<GetProcessMemoryStats>` | Operational on Android. (l_getProcessMemoryStats) |
| `getRealTime` | **Implemented** | `CLuaUtilDefs.cpp:47` | `GetCTime` | Operational on Android. (l_getRealTime) |
| `getRemoteRequestInfo` | **Implemented** | `CLuaNetworkDefs.cpp:28` | `GetRemoteRequestInfo` | Operational on Android. (LuaGetRemoteRequestInfo) |
| `getRemoteRequests` | **Implemented** | `CLuaNetworkDefs.cpp:27` | `GetRemoteRequests` | Operational on Android. (LuaGetRemoteRequests) |
| `getResourceConfig` | **Implemented** | `CLuaResourceDefs.cpp:22` | `GetResourceConfig` | Operational on Android. (l_getResourceConfig) |
| `getResourceDynamicElementRoot` | **Implemented** | `CLuaResourceDefs.cpp:27` | `GetResourceDynamicElementRoot` | Operational on Android. (l_getResourceDynamicElementRoot) |
| `getResourceExportedFunctions` | **Implemented** | `CLuaResourceDefs.cpp:28` | `GetResourceExportedFunctions` | Operational on Android. (l_getResourceExportedFunctions) |
| `getResourceFromName` | **Implemented** | `CLuaResourceDefs.cpp:24` | `GetResourceFromName` | Operational on Android. (l_getResourceFromName) |
| `getResourceGUIElement` | **Implemented** | `CLuaResourceDefs.cpp:26` | `GetResourceGUIElement` | Operational on Android. (l_getResourceGUIElement) |
| `getResourceName` | **Implemented** | `CLuaResourceDefs.cpp:23` | `ArgumentParserWarn<false, GetResourceName>` | Operational on Android. (l_getResourceName) |
| `getResourceRootElement` | **Implemented** | `CLuaResourceDefs.cpp:25` | `GetResourceRootElement` | Operational on Android. (l_getResourceRootElement) |
| `getResourceState` | **Implemented** | `CLuaResourceDefs.cpp:30` | `GetResourceState` | Operational on Android. (l_getResourceState) |
| `getResources` | **Implemented** | `CLuaResourceDefs.cpp:29` | `ArgumentParser<GetResources>` | Operational on Android. (l_getResources) |
| `getServerIp` | **Implemented** | `CLuaNetworkDefs.cpp:30` | `ArgumentParser<GetServerIp>` | Operational on Android. (LuaGetServerIp) |
| `getThisResource` | **Implemented** | `CLuaResourceDefs.cpp:21` | `GetThisResource` | Operational on Android. (l_getThisResource) |
| `getTickCount` | **Implemented** | `CLuaUtilDefs.cpp:46` | `GetTickCount_` | Operational on Android. (l_getTickCount) |
| `getTimerDetails` | **Implemented** | `CLuaTimerDefs.cpp:25` | `GetTimerDetails` | Operational on Android. (l_getTimerDetails) |
| `getTimers` | **Implemented** | `CLuaTimerDefs.cpp:23` | `GetTimers` | Operational on Android. (l_getTimers) |
| `getTypeIndexFromClothes` | **Implemented** | `CLuaManager.cpp:191` | `CLuaFunctionDefs::GetTypeIndexFromClothes` | Operational on Android. (l_getTypeIndexFromClothes) |
| `getUserdataType` | **Implemented** | `CLuaUtilDefs.cpp:50` | `GetUserdataType` | Operational on Android. (l_getUserdataType) |
| `getValidPedModels` | **Implemented** | `CLuaManager.cpp:205` | `CLuaFunctionDefs::GetValidPedModels` | Stock models only (0..312 excluding 74, 149, 208); custom allocation is not ported. Host-tested; not device-verified. (GetValidPedModels) |
| `getVersion` | **Implemented** | `CLuaManager.cpp:240` | `CLuaFunctionDefs::GetVersion` | Operational on Android. (l_getVersion) |
| `getfenv` | **Implemented** | `CLuaMain.cpp:90` | `CLuaUtilDefs::DisabledFunction` | Disabled in the client. Returns false. |
| `gettok` | **Implemented** | `CLuaUtilDefs.cpp:73` | `GetTok` | Operational on Android. (l_gettok) |
| `hash` | **Implemented** | `CLuaCryptDefs.cpp:24` | `ArgumentParserWarn<false, Hash>` | Operational on Android. (l_hash) |
| `interpolateBetween` | **Implemented** | `CLuaUtilDefs.cpp:58` | `InterpolateBetween` | Operational on Android. (l_interpolateBetween) |
| `isCapsLockEnabled` | **Implemented** | `CLuaClientDefs.cpp:27` | `ArgumentParser<IsCapsLockEnabled>` | Operational on Android. (l_isCapsLockEnabled) |
| `isChatInputBlocked` | **Implemented** | `CLuaClientDefs.cpp:24` | `ArgumentParser<IsChatInputBlocked>` | Operational on Android. (l_isChatInputBlocked) |
| `isChatVisible` | **Implemented** | `CLuaClientDefs.cpp:23` | `ArgumentParserWarn<false, IsChatVisible>` | Operational on Android. (IsChatVisible) |
| `isControlEnabled` | **Implemented** | `CLuaManager.cpp:214` | `CLuaFunctionDefs::IsControlEnabled` | Stores per-control enabled state; only the radar control currently changes Android behavior. Other GTA control actions are not wired by this handler. Registration coverage does not mean complete native control compatibility. |
| `isCursorShowing` | **Implemented** | `CLuaManager.cpp:199` | `CLuaFunctionDefs::IsCursorShowing` | Operational on Android. (IsCursorShowing) |
| `isMTAWindowFocused` | **Implemented** | `CLuaClientDefs.cpp:26` | `ArgumentParser<IsMTAWindowFocused>` | clearDebugBox follows CLuaClientDefs.cpp:72-76 and returns true. There is no CDebugView, so nothing is cleared. isMTAWindowFocused reads m_bFocused. The activity reports foreground focus; the VM copies it on the next pump and fires onClientMTAFocusChange. Host-tested. Not device-verified. |
| `isOOPEnabled` | **Implemented** | `CLuaUtilDefs.cpp:49` | `IsOOPEnabled` | Operational on Android. (l_isOOPEnabled) |
| `isTimer` | **Implemented** | `CLuaTimerDefs.cpp:24` | `IsTimer` | Operational on Android. (l_isTimer) |
| `isTimerPaused` | **Implemented** | `CLuaTimerDefs.cpp:22` | `ArgumentParser<IsTimerPaused>` | Operational on Android. (l_isTimerPaused) |
| `isTransferBoxAlwaysVisible` | **Implemented** | `CLuaClientDefs.cpp:21` | `ArgumentParser<IsTransferBoxAlwaysVisible>` | Operational on Android. (l_isTransferBoxAlwaysVisible) |
| `isTransferBoxVisible` | **Implemented** | `CLuaClientDefs.cpp:20` | `ArgumentParser<IsTransferBoxVisible>` | Operational on Android. (l_isTransferBoxVisible) |
| `isTrayNotificationEnabled` | **Implemented** | `CLuaManager.cpp:186` | `CLuaFunctionDefs::IsTrayNotificationEnabled` | setWindowFlashing follows CStaticFunctionDefinitions.cpp:308-323. A focused window or a disabled server_can_flash_window cvar returns false. FlashWindowEx is not hooked, so an allowed call returns true and does not flash. createTrayNotification follows CStaticFunctionDefinitions.cpp:325-333 and CTrayIcon.cpp:101-137. allow_tray_notifications defaults to true. Shell_NotifyIcon is not hooked, so the call returns false. Host-tested. Not device-verified. |
| `isVoiceEnabled` | **Implemented** | `CLuaManager.cpp:247` | `CLuaFunctionDefs::IsVoiceEnabled` | Operational on Android. (l_isVoiceEnabled) |
| `killTimer` | **Implemented** | `CLuaTimerDefs.cpp:19` | `KillTimer` | Operational on Android. (l_killTimer) |
| `load` | **Implemented** | `CLuaResourceDefs.cpp:32` | `Load` | Compiles Lua source text. Binary chunks are rejected. |
| `loadfile` | **Implemented** | `CLuaMain.cpp:87` | `CLuaUtilDefs::DisabledFunction` | Disabled in the client. Returns false. |
| `loadlib` | **Implemented** | `CLuaMain.cpp:89` | `CLuaUtilDefs::DisabledFunction` | Disabled in the client. Returns false. |
| `loadstring` | **Implemented** | `CLuaResourceDefs.cpp:31` | `LoadString` | Compiles Lua source text. Binary chunks are rejected. |
| `md5` | **Implemented** | `CLuaCryptDefs.cpp:22` | `ArgumentParserWarn<false, Md5>` | Operational on Android. (l_md5) |
| `newproxy` | **Implemented** | `CLuaMain.cpp:91` | `CLuaUtilDefs::DisabledFunction` | Disabled in the client. Returns false. |
| `outputChatBox` | **Implemented** | `CLuaManager.cpp:178` | `CLuaFunctionDefs::OutputChatBox` | Operational on Android. (OutputChatBox) |
| `outputConsole` | **Implemented** | `CLuaManager.cpp:177` | `CLuaFunctionDefs::OutputConsole` | Operational on Android. (l_outputConsole) |
| `outputDebugString` | **Implemented** | `CLuaManager.cpp:179` | `CLuaFunctionDefs::OutputClientDebugString` | Operational on Android. (l_debug) |
| `passwordHash` | **Implemented** | `CLuaCryptDefs.cpp:29` | `ArgumentParserWarn<false, PasswordHash>` | Operational on Android. (PasswordHash) |
| `passwordVerify` | **Implemented** | `CLuaCryptDefs.cpp:31` | `PasswordVerify` | Operational on Android. (PasswordVerify) |
| `pathIsDirectory` | **Implemented** | `CLuaPathDefs.cpp:28` | `ArgumentParser<pathIsDirectory>` | Operational on Android. (l_pathIsDirectory) |
| `pathIsFile` | **Implemented** | `CLuaPathDefs.cpp:27` | `ArgumentParser<pathIsFile>` | Operational on Android. (l_pathIsFile) |
| `pathListDir` | **Implemented** | `CLuaPathDefs.cpp:26` | `ArgumentParser<pathListDir>` | Operational on Android. (l_pathListDir) |
| `pregFind` | **Implemented** | `CLuaUtilDefs.cpp:65` | `PregFind` | Operational on Android. (l_pregFind) |
| `pregMatch` | **Implemented** | `CLuaUtilDefs.cpp:67` | `PregMatch` | Operational on Android. (l_pregMatch) |
| `pregReplace` | **Implemented** | `CLuaUtilDefs.cpp:66` | `PregReplace` | Operational on Android. (l_pregReplace) |
| `print` | **Implemented** | `CLuaUtilDefs.cpp:51` | `luaB_print` | Operational on Android. (l_print) |
| `ref` | **Implemented** | `CLuaUtilDefs.cpp:45` | `Reference` | Operational on Android. (l_ref) |
| `removeClothingModel` | **Implemented** | `CLuaManager.cpp:194` | `CLuaFunctionDefs::RemoveClothingModel` | Operational on Android. (l_removeClothingModel) |
| `removeCommandHandler` | **Implemented** | `CLuaManager.cpp:226` | `CLuaFunctionDefs::RemoveCommandHandler` | Operational on Android. (l_removeCommandHandler) |
| `removeDebugHook` | **Implemented** | `CLuaManager.cpp:237` | `CLuaFunctionDefs::RemoveDebugHook` | Operational on Android. (l_removeDebugHook) |
| `removeEventHandler` | **Implemented** | `CLuaManager.cpp:165` | `CLuaFunctionDefs::RemoveEventHandler` | Operational on Android. (l_removeEventHandler) |
| `require` | **Implemented** | `CLuaMain.cpp:88` | `CLuaUtilDefs::DisabledFunction` | Disabled in the client. Returns false. |
| `resetNeonClientSettings` | **Implemented** | `CLuaClientDefs.cpp:30` | `ArgumentParser<ResetNeonClientSettings>` | Operational on Android. (l_resetNeonClientSettings) |
| `resetTimer` | **Implemented** | `CLuaTimerDefs.cpp:20` | `ResetTimer` | Operational on Android. (l_resetTimer) |
| `setAnalogControlState` | **Implemented** | `CLuaManager.cpp:213` | `CLuaFunctionDefs::SetAnalogControlState` | Operational on Android. (SetAnalogControlState) |
| `setClipboard` | **Implemented** | `CLuaManager.cpp:180` | `CLuaFunctionDefs::SetClipboard` | Operational on Android. (l_setClipboard) |
| `setCursorAlpha` | **Implemented** | `CLuaManager.cpp:202` | `CLuaFunctionDefs::SetCursorAlpha` | Operational on Android. (l_setCursorAlpha) |
| `setCursorPosition` | **Implemented** | `CLuaManager.cpp:198` | `CLuaFunctionDefs::SetCursorPosition` | Operational on Android. (l_setCursorPosition) |
| `setDevelopmentMode` | **Implemented** | `CLuaManager.cpp:234` | `CLuaFunctionDefs::SetDevelopmentMode` | Operational on Android. (l_setDevelopmentMode) |
| `setNeonClientSetting` | **Implemented** | `CLuaClientDefs.cpp:29` | `ArgumentParser<SetNeonClientSetting>` | Operational on Android. (l_setNeonClientSetting) |
| `setTimer` | **Implemented** | `CLuaTimerDefs.cpp:18` | `SetTimer` | Operational on Android. (l_setTimer) |
| `setTimerPaused` | **Implemented** | `CLuaTimerDefs.cpp:21` | `ArgumentParser<SetTimerPaused>` | Operational on Android. (l_setTimerPaused) |
| `setTransferBoxVisible` | **Implemented** | `CLuaClientDefs.cpp:19` | `ArgumentParser<SetTransferBoxVisible>` | Operational on Android. (l_setTransferBoxVisible) |
| `setWindowFlashing` | **Implemented** | `CLuaManager.cpp:181` | `CLuaFunctionDefs::SetWindowFlashing` | setWindowFlashing follows CStaticFunctionDefinitions.cpp:308-323. A focused window or a disabled server_can_flash_window cvar returns false. FlashWindowEx is not hooked, so an allowed call returns true and does not flash. createTrayNotification follows CStaticFunctionDefinitions.cpp:325-333 and CTrayIcon.cpp:101-137. allow_tray_notifications defaults to true. Shell_NotifyIcon is not hooked, so the call returns false. Host-tested. Not device-verified. |
| `sha256` | **Implemented** | `CLuaCryptDefs.cpp:23` | `ArgumentParserWarn<false, Sha256>` | Operational on Android. (l_sha256) |
| `showChat` | **Implemented** | `CLuaClientDefs.cpp:22` | `ArgumentParserWarn<false, ShowChat>` | Operational on Android. (ShowChat) |
| `showCursor` | **Implemented** | `CLuaManager.cpp:200` | `CLuaFunctionDefs::ShowCursor` | Operational on Android. (ShowCursor) |
| `split` | **Implemented** | `CLuaUtilDefs.cpp:48` | `Split` | Operational on Android. (l_split) |
| `teaDecode` | **Implemented** | `CLuaCryptDefs.cpp:26` | `ArgumentParserWarn<false, TeaDecode>` | Operational on Android. (l_teaDecode) |
| `teaEncode` | **Implemented** | `CLuaCryptDefs.cpp:25` | `ArgumentParserWarn<false, TeaEncode>` | Operational on Android. (l_teaEncode) |
| `toJSON` | **Implemented** | `CLuaUtilDefs.cpp:61` | `toJSON` | Operational on Android. (ToJson) |
| `tocolor` | **Implemented** | `CLuaUtilDefs.cpp:74` | `tocolor` | Operational on Android. (l_tocolor) |
| `toggleAllControls` | **Implemented** | `CLuaManager.cpp:222` | `CLuaFunctionDefs::ToggleAllControls` | Operational on Android. (ToggleAllControls) |
| `toggleControl` | **Implemented** | `CLuaManager.cpp:221` | `CLuaFunctionDefs::ToggleControl` | Stores per-control enabled state; only the radar control currently changes Android behavior. Other GTA control actions are not wired by this handler. Registration coverage does not mean complete native control compatibility. |
| `triggerEvent` | **Implemented** | `CLuaManager.cpp:167` | `CLuaFunctionDefs::TriggerEvent` | Operational on Android. (l_triggerEvent) |
| `triggerLatentServerEvent` | **Implemented** | `CLuaManager.cpp:171` | `CLuaFunctionDefs::TriggerLatentServerEvent` | Operational on Android. (l_triggerLatentServerEvent) |
| `triggerServerEvent` | **Implemented** | `CLuaManager.cpp:168` | `CLuaFunctionDefs::TriggerServerEvent` | Operational on Android. (l_triggerServerEvent) |
| `unbindKey` | **Implemented** | `CLuaManager.cpp:210` | `CLuaFunctionDefs::UnbindKey` | Operational on Android. (UnbindKey) |
| `utfChar` | **Implemented** | `CLuaUTFDefs.cpp:95` | `ArgumentParserWarn<nullptr, UtfChar>` | Operational on Android. (l_utfChar) |
| `utfCode` | **Implemented** | `CLuaUTFDefs.cpp:95` | `ArgumentParserWarn<false, UtfCode>` | Operational on Android. (l_utfCode) |
| `utfLen` | **Implemented** | `CLuaUTFDefs.cpp:94` | `ArgumentParserWarn<false, UtfLen>` | Operational on Android. (l_utfLen) |
| `utfSeek` | **Implemented** | `CLuaUTFDefs.cpp:94` | `ArgumentParserWarn<false, UtfSeek>` | Operational on Android. (l_utfSeek) |
| `utfSub` | **Implemented** | `CLuaUTFDefs.cpp:94` | `ArgumentParserWarn<false, UtfSub>` | Operational on Android. (l_utfSub) |
| `wasEventCancelled` | **Implemented** | `CLuaManager.cpp:170` | `CLuaFunctionDefs::WasEventCancelled` | Operational on Android. (l_wasEventCancelled) |
| `xmlCopyFile` | **Implemented** | `CLuaXMLDefs.cpp:21` | `xmlCopyFile` | Operational on Android. (l_xmlCopyFile) |
| `xmlCreateChild` | **Implemented** | `CLuaXMLDefs.cpp:24` | `xmlCreateChild` | Operational on Android. (l_xmlCreateChild) |
| `xmlCreateFile` | **Implemented** | `CLuaXMLDefs.cpp:18` | `xmlCreateFile` | Operational on Android. (l_xmlCreateFile) |
| `xmlDestroyNode` | **Implemented** | `CLuaXMLDefs.cpp:25` | `xmlDestroyNode` | Operational on Android. (l_xmlDestroyNode) |
| `xmlFindChild` | **Implemented** | `CLuaXMLDefs.cpp:26` | `xmlNodeFindChild` | Operational on Android. (l_xmlFindChild) |
| `xmlLoadFile` | **Implemented** | `CLuaXMLDefs.cpp:19` | `xmlLoadFile` | Operational on Android. (l_xmlLoadFile) |
| `xmlLoadString` | **Implemented** | `CLuaXMLDefs.cpp:20` | `xmlLoadString` | Operational on Android. (l_xmlLoadString) |
| `xmlNodeGetAttribute` | **Implemented** | `CLuaXMLDefs.cpp:32` | `xmlNodeGetAttribute` | Operational on Android. (l_xmlNodeGetAttribute) |
| `xmlNodeGetAttributes` | **Implemented** | `CLuaXMLDefs.cpp:31` | `xmlNodeGetAttributes` | Operational on Android. (l_xmlNodeGetAttributes) |
| `xmlNodeGetChildren` | **Implemented** | `CLuaXMLDefs.cpp:28` | `xmlNodeGetChildren` | Operational on Android. (l_xmlNodeGetChildren) |
| `xmlNodeGetName` | **Implemented** | `CLuaXMLDefs.cpp:33` | `xmlNodeGetName` | Operational on Android. (l_xmlNodeGetName) |
| `xmlNodeGetParent` | **Implemented** | `CLuaXMLDefs.cpp:29` | `xmlNodeGetParent` | Operational on Android. (l_xmlNodeGetParent) |
| `xmlNodeGetValue` | **Implemented** | `CLuaXMLDefs.cpp:30` | `xmlNodeGetValue` | Operational on Android. (l_xmlNodeGetValue) |
| `xmlNodeSetAttribute` | **Implemented** | `CLuaXMLDefs.cpp:36` | `xmlNodeSetAttribute` | Operational on Android. (l_xmlNodeSetAttribute) |
| `xmlNodeSetName` | **Implemented** | `CLuaXMLDefs.cpp:37` | `xmlNodeSetName` | Operational on Android. (l_xmlNodeSetName) |
| `xmlNodeSetValue` | **Implemented** | `CLuaXMLDefs.cpp:35` | `xmlNodeSetValue` | Operational on Android. (l_xmlNodeSetValue) |
| `xmlSaveFile` | **Implemented** | `CLuaXMLDefs.cpp:22` | `xmlSaveFile` | Operational on Android. (l_xmlSaveFile) |
| `xmlUnloadFile` | **Implemented** | `CLuaXMLDefs.cpp:23` | `xmlUnloadFile` | Operational on Android. (l_xmlUnloadFile) |
