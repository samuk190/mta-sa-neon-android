# Core System, Networking & Utilities

Timers, event handling, network latent transfers, file I/O, XML parsing, JSON serialization, UTF strings, and cryptography.

## Parity Status

- **Total Functions:** 195
- **Implemented:** 23 (11.8%)
- **Stubs:** 15
- **Missing:** 157

> **Technical Requirement:** Pure CPU/platform logic. Mostly ported or needs standard library wrappers (e.g. tinyxml2, sha256, zlib).

## Function List

| Lua Function | Status | Desktop Source Ref | Desktop Handler | Conversion Requirement / Implementation Notes |
|---|---|---|---|---|
| `abortRemoteRequest` | **Implemented** | `CLuaNetworkDefs.cpp:29` | `AbortRemoteRequest` | Operational on Android. Drops the call so a late completion skips the callback. (LuaAbortRemoteRequest) |
| `addClothingModel` | Missing | `CLuaManager.cpp:193` | `CLuaFunctionDefs::AddClothingModel` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `addCommandHandler` | *Stub* | `CLuaManager.cpp:225` | `CLuaFunctionDefs::AddCommandHandler` | Currently stubbed: `l_true: lua_pushboolean(L, 1)`. Needs native engine wiring. |
| `addDebugHook` | Missing | `CLuaManager.cpp:236` | `CLuaFunctionDefs::AddDebugHook` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `addEvent` | *Stub* | `CLuaManager.cpp:163` | `CLuaFunctionDefs::AddEvent` | Currently stubbed: `l_addEvent: lua_pushboolean(L, 1)`. Needs native engine wiring. |
| `addEventHandler` | **Implemented** | `CLuaManager.cpp:164` | `CLuaFunctionDefs::AddEventHandler` | Operational on Android. (l_addEventHandler) |
| `base64Decode` | Missing | `CLuaCryptDefs.cpp:28` | `ArgumentParserWarn<false, Base64decode>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `base64Encode` | Missing | `CLuaCryptDefs.cpp:27` | `ArgumentParserWarn<false, Base64encode>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `bindKey` | *Stub* | `CLuaManager.cpp:209` | `CLuaFunctionDefs::BindKey` | Currently stubbed: `l_true: lua_pushboolean(L, 1)`. Needs native engine wiring. |
| `bitAnd` | Missing | `CLuaBitDefs.cpp:18` | `bitAnd` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `bitArShift` | Missing | `CLuaBitDefs.cpp:20` | `bitArShift` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `bitExtract` | Missing | `CLuaBitDefs.cpp:22` | `bitExtract` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `bitLRotate` | Missing | `CLuaBitDefs.cpp:20` | `bitLRotate` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `bitLShift` | Missing | `CLuaBitDefs.cpp:20` | `bitLShift` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `bitNot` | Missing | `CLuaBitDefs.cpp:18` | `bitNot` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `bitOr` | Missing | `CLuaBitDefs.cpp:18` | `bitOr` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `bitRRotate` | Missing | `CLuaBitDefs.cpp:20` | `bitRRotate` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `bitRShift` | Missing | `CLuaBitDefs.cpp:20` | `bitRShift` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `bitReplace` | Missing | `CLuaBitDefs.cpp:22` | `bitReplace` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `bitTest` | Missing | `CLuaBitDefs.cpp:18` | `bitTest` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `bitXor` | Missing | `CLuaBitDefs.cpp:18` | `bitXor` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `call` | Missing | `CLuaResourceDefs.cpp:20` | `Call` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `cancelEvent` | Missing | `CLuaManager.cpp:169` | `CLuaFunctionDefs::CancelEvent` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `cancelLatentEvent` | Missing | `CLuaManager.cpp:174` | `CLuaFunctionDefs::CancelLatentEvent` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `clearChatBox` | Missing | `CLuaManager.cpp:182` | `CLuaFunctionDefs::ClearChatBox` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `clearDebugBox` | Missing | `CLuaClientDefs.cpp:25` | `ArgumentParser<ClearDebug>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `createTrayNotification` | Missing | `CLuaManager.cpp:185` | `CLuaFunctionDefs::CreateTrayNotification` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `debugSleep` | Missing | `CLuaUtilDefs.cpp:70` | `DebugSleep` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `decodeString` | Missing | `CLuaCryptDefs.cpp:33` | `DecodeString` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `deref` | Missing | `CLuaUtilDefs.cpp:44` | `Dereference` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `dofile` | Missing | `CLuaMain.cpp:86` | `CLuaUtilDefs::DisabledFunction` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `downloadFile` | Missing | `CLuaManager.cpp:206` | `CLuaFunctionDefs::DownloadFile` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `encodeString` | Missing | `CLuaCryptDefs.cpp:32` | `EncodeString` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `executeCommandHandler` | *Stub* | `CLuaManager.cpp:227` | `CLuaFunctionDefs::ExecuteCommandHandler` | Currently stubbed: `DefaultValue(kFalse)`. Needs native engine wiring. |
| `fetchRemote` | **Implemented** | `CLuaNetworkDefs.cpp:26` | `FetchRemote` | Operational on Android. Options-table callback reports the HTTP statusCode. https fails in the callback (code 1). Completions run from ResourceLoader::Pump. (LuaFetchRemote) |
| `fileClose` | *Stub* | `CLuaFileDefs.cpp:47` | `fileClose` | Currently stubbed: `DefaultValue(kTrue)`. Needs native engine wiring. |
| `fileCopy` | Missing | `CLuaFileDefs.cpp:44` | `fileCopy` | Needs local sandbox file I/O operations. |
| `fileCreate` | Missing | `CLuaFileDefs.cpp:42` | `fileCreate` | Needs local sandbox file I/O operations. |
| `fileDelete` | Missing | `CLuaFileDefs.cpp:46` | `fileDelete` | Needs local sandbox file I/O operations. |
| `fileExists` | Missing | `CLuaFileDefs.cpp:43` | `fileExists` | Needs local sandbox file I/O operations. |
| `fileFlush` | Missing | `CLuaFileDefs.cpp:48` | `fileFlush` | Needs local sandbox file I/O operations. |
| `fileGetContents` | Missing | `CLuaFileDefs.cpp:56` | `ArgumentParser<fileGetContents>` | Needs local sandbox file I/O operations. |
| `fileGetHash` | Missing | `CLuaFileDefs.cpp:57` | `ArgumentParser<fileGetHash>` | Needs local sandbox file I/O operations. |
| `fileGetPath` | Missing | `CLuaFileDefs.cpp:53` | `fileGetPath` | Needs local sandbox file I/O operations. |
| `fileGetPos` | Missing | `CLuaFileDefs.cpp:51` | `fileGetPos` | Needs local sandbox file I/O operations. |
| `fileGetSize` | *Stub* | `CLuaFileDefs.cpp:52` | `fileGetSize` | Currently stubbed: `DefaultValue(kZero)`. Needs native engine wiring. |
| `fileIsEOF` | Missing | `CLuaFileDefs.cpp:54` | `fileIsEOF` | Needs local sandbox file I/O operations. |
| `fileOpen` | *Stub* | `CLuaFileDefs.cpp:41` | `fileOpen` | Currently stubbed: `FileOpen: lua_pushnil(L)`. Needs native engine wiring. |
| `fileRead` | *Stub* | `CLuaFileDefs.cpp:49` | `fileRead` | Currently stubbed: `DefaultValue(kEmptyString)`. Needs native engine wiring. |
| `fileRename` | Missing | `CLuaFileDefs.cpp:45` | `fileRename` | Needs local sandbox file I/O operations. |
| `fileSetPos` | Missing | `CLuaFileDefs.cpp:55` | `fileSetPos` | Needs local sandbox file I/O operations. |
| `fileWrite` | Missing | `CLuaFileDefs.cpp:50` | `fileWrite` | Needs local sandbox file I/O operations. |
| `fromJSON` | **Implemented** | `CLuaUtilDefs.cpp:62` | `fromJSON` | Operational on Android. (FromJson) |
| `generateKeyPair` | Missing | `CLuaCryptDefs.cpp:30` | `ArgumentParser<GenerateKeyPair>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getAnalogControlState` | Missing | `CLuaManager.cpp:212` | `CLuaFunctionDefs::GetAnalogControlState` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getBodyPartName` | Missing | `CLuaManager.cpp:189` | `CLuaFunctionDefs::GetBodyPartName` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getBoundKeys` | Missing | `CLuaManager.cpp:215` | `CLuaFunctionDefs::GetBoundKeys` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getClothesByTypeIndex` | Missing | `CLuaManager.cpp:190` | `CLuaFunctionDefs::GetClothesByTypeIndex` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getClothesTypeName` | Missing | `CLuaManager.cpp:192` | `CLuaFunctionDefs::GetClothesTypeName` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getColorFromString` | Missing | `CLuaUtilDefs.cpp:52` | `GetColorFromString` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getCommandHandlers` | Missing | `CLuaManager.cpp:228` | `CLuaFunctionDefs::GetCommandHandlers` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getCommandsBoundToKey` | Missing | `CLuaManager.cpp:218` | `CLuaFunctionDefs::GetCommandsBoundToKey` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getCursorAlpha` | Missing | `CLuaManager.cpp:201` | `CLuaFunctionDefs::GetCursorAlpha` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getCursorPosition` | **Implemented** | `CLuaManager.cpp:197` | `CLuaFunctionDefs::GetCursorPosition` | Operational on Android. (l_getCursorPosition) |
| `getDevelopmentMode` | Missing | `CLuaManager.cpp:235` | `CLuaFunctionDefs::GetDevelopmentMode` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getDistanceBetweenPoints2D` | **Implemented** | `CLuaUtilDefs.cpp:55` | `GetDistanceBetweenPoints2D` | Operational on Android. (l_getDistanceBetweenPoints2D) |
| `getDistanceBetweenPoints3D` | **Implemented** | `CLuaUtilDefs.cpp:56` | `GetDistanceBetweenPoints3D` | Operational on Android. (l_getDistanceBetweenPoints3D) |
| `getEasingValue` | Missing | `CLuaUtilDefs.cpp:57` | `GetEasingValue` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getEventHandlers` | Missing | `CLuaManager.cpp:166` | `CLuaFunctionDefs::GetEventHandlers` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getFunctionsBoundToKey` | Missing | `CLuaManager.cpp:216` | `CLuaFunctionDefs::GetFunctionsBoundToKey` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getKeyBoundToCommand` | Missing | `CLuaManager.cpp:219` | `CLuaFunctionDefs::GetKeyBoundToCommand` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getKeyBoundToFunction` | Missing | `CLuaManager.cpp:217` | `CLuaFunctionDefs::GetKeyBoundToFunction` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getKeyState` | Missing | `CLuaManager.cpp:211` | `CLuaFunctionDefs::GetKeyState` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getKeyboardLayout` | Missing | `CLuaManager.cpp:244` | `CLuaFunctionDefs::GetKeyboardLayout` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getLatentEventHandles` | Missing | `CLuaManager.cpp:172` | `CLuaFunctionDefs::GetLatentEventHandles` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getLatentEventStatus` | Missing | `CLuaManager.cpp:173` | `CLuaFunctionDefs::GetLatentEventStatus` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getLocalization` | Missing | `CLuaManager.cpp:243` | `CLuaFunctionDefs::GetLocalization` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getNeonClientSetting` | Missing | `CLuaClientDefs.cpp:28` | `ArgumentParser<GetNeonClientSetting>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getNetworkStats` | **Implemented** | `CLuaManager.cpp:232` | `CLuaFunctionDefs::GetNetworkStats` | Operational on Android from RakNetWrapper. packetloss, messagesInSendBuffer, and both limit flags stay 0. encryptionStatus is 1. (l_getNetworkStats) |
| `getNetworkUsageData` | **Implemented** | `CLuaManager.cpp:231` | `CLuaFunctionDefs::GetNetworkUsageData` | Operational on Android. Slots are the wire packet id. bits are totalBytes * 8. (l_getNetworkUsageData) |
| `getPerformanceStats` | Missing | `CLuaManager.cpp:233` | `CLuaFunctionDefs::GetPerformanceStats` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getProcessMemoryStats` | Missing | `CLuaUtilDefs.cpp:75` | `ArgumentParser<GetProcessMemoryStats>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getRealTime` | Missing | `CLuaUtilDefs.cpp:47` | `GetCTime` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getRemoteRequestInfo` | **Implemented** | `CLuaNetworkDefs.cpp:28` | `GetRemoteRequestInfo` | Operational on Android. resource is false. (LuaGetRemoteRequestInfo) |
| `getRemoteRequests` | **Implemented** | `CLuaNetworkDefs.cpp:27` | `GetRemoteRequests` | Operational on Android. Handles are {id, remoteRequest=true}. (LuaGetRemoteRequests) |
| `getResourceConfig` | Missing | `CLuaResourceDefs.cpp:22` | `GetResourceConfig` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getResourceDynamicElementRoot` | Missing | `CLuaResourceDefs.cpp:27` | `GetResourceDynamicElementRoot` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getResourceExportedFunctions` | Missing | `CLuaResourceDefs.cpp:28` | `GetResourceExportedFunctions` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getResourceFromName` | Missing | `CLuaResourceDefs.cpp:24` | `GetResourceFromName` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getResourceGUIElement` | Missing | `CLuaResourceDefs.cpp:26` | `GetResourceGUIElement` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getResourceName` | Missing | `CLuaResourceDefs.cpp:23` | `ArgumentParserWarn<false, GetResourceName>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getResourceRootElement` | Missing | `CLuaResourceDefs.cpp:25` | `GetResourceRootElement` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getResourceState` | Missing | `CLuaResourceDefs.cpp:30` | `GetResourceState` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getResources` | Missing | `CLuaResourceDefs.cpp:29` | `ArgumentParser<GetResources>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getServerIp` | **Implemented** | `CLuaNetworkDefs.cpp:30` | `ArgumentParser<GetServerIp>` | Operational on Android against the live RakNetWrapper. The headless wrapper stays on the stack, so that build returns an empty string. (LuaGetServerIp) |
| `getThisResource` | Missing | `CLuaResourceDefs.cpp:21` | `GetThisResource` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getTickCount` | **Implemented** | `CLuaUtilDefs.cpp:46` | `GetTickCount_` | Operational on Android. (l_getTickCount) |
| `getTimerDetails` | Missing | `CLuaTimerDefs.cpp:25` | `GetTimerDetails` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getTimers` | Missing | `CLuaTimerDefs.cpp:23` | `GetTimers` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getTypeIndexFromClothes` | Missing | `CLuaManager.cpp:191` | `CLuaFunctionDefs::GetTypeIndexFromClothes` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getUserdataType` | Missing | `CLuaUtilDefs.cpp:50` | `GetUserdataType` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getValidPedModels` | Missing | `CLuaManager.cpp:205` | `CLuaFunctionDefs::GetValidPedModels` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getVersion` | Missing | `CLuaManager.cpp:240` | `CLuaFunctionDefs::GetVersion` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getfenv` | Missing | `CLuaMain.cpp:90` | `CLuaUtilDefs::DisabledFunction` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `gettok` | Missing | `CLuaUtilDefs.cpp:73` | `GetTok` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `hash` | Missing | `CLuaCryptDefs.cpp:24` | `ArgumentParserWarn<false, Hash>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `interpolateBetween` | Missing | `CLuaUtilDefs.cpp:58` | `InterpolateBetween` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `isCapsLockEnabled` | Missing | `CLuaClientDefs.cpp:27` | `ArgumentParser<IsCapsLockEnabled>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `isChatInputBlocked` | Missing | `CLuaClientDefs.cpp:24` | `ArgumentParser<IsChatInputBlocked>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `isChatVisible` | Missing | `CLuaClientDefs.cpp:23` | `ArgumentParserWarn<false, IsChatVisible>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `isControlEnabled` | *Stub* | `CLuaManager.cpp:214` | `CLuaFunctionDefs::IsControlEnabled` | Currently stubbed: `DefaultValue(kTrue)`. Needs native engine wiring. |
| `isCursorShowing` | **Implemented** | `CLuaManager.cpp:199` | `CLuaFunctionDefs::IsCursorShowing` | Operational on Android. (IsCursorShowing) |
| `isMTAWindowFocused` | Missing | `CLuaClientDefs.cpp:26` | `ArgumentParser<IsMTAWindowFocused>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `isOOPEnabled` | Missing | `CLuaUtilDefs.cpp:49` | `IsOOPEnabled` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `isTimer` | *Stub* | `CLuaTimerDefs.cpp:24` | `IsTimer` | Currently stubbed: `l_false: lua_pushboolean(L, 0)`. Needs native engine wiring. |
| `isTimerPaused` | Missing | `CLuaTimerDefs.cpp:22` | `ArgumentParser<IsTimerPaused>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `isTransferBoxAlwaysVisible` | Missing | `CLuaClientDefs.cpp:21` | `ArgumentParser<IsTransferBoxAlwaysVisible>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `isTransferBoxVisible` | Missing | `CLuaClientDefs.cpp:20` | `ArgumentParser<IsTransferBoxVisible>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `isTrayNotificationEnabled` | Missing | `CLuaManager.cpp:186` | `CLuaFunctionDefs::IsTrayNotificationEnabled` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `isVoiceEnabled` | Missing | `CLuaManager.cpp:247` | `CLuaFunctionDefs::IsVoiceEnabled` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `killTimer` | *Stub* | `CLuaTimerDefs.cpp:19` | `KillTimer` | Currently stubbed: `l_true: lua_pushboolean(L, 1)`. Needs native engine wiring. |
| `load` | Missing | `CLuaResourceDefs.cpp:32` | `Load` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `loadfile` | Missing | `CLuaMain.cpp:87` | `CLuaUtilDefs::DisabledFunction` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `loadlib` | Missing | `CLuaMain.cpp:89` | `CLuaUtilDefs::DisabledFunction` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `loadstring` | Missing | `CLuaResourceDefs.cpp:31` | `LoadString` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `md5` | Missing | `CLuaCryptDefs.cpp:22` | `ArgumentParserWarn<false, Md5>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `newproxy` | Missing | `CLuaMain.cpp:91` | `CLuaUtilDefs::DisabledFunction` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `outputChatBox` | **Implemented** | `CLuaManager.cpp:178` | `CLuaFunctionDefs::OutputChatBox` | Operational on Android. (OutputChatBox) |
| `outputConsole` | Missing | `CLuaManager.cpp:177` | `CLuaFunctionDefs::OutputConsole` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `outputDebugString` | **Implemented** | `CLuaManager.cpp:179` | `CLuaFunctionDefs::OutputClientDebugString` | Operational on Android. (l_debug) |
| `passwordHash` | Missing | `CLuaCryptDefs.cpp:29` | `ArgumentParserWarn<false, PasswordHash>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `passwordVerify` | Missing | `CLuaCryptDefs.cpp:31` | `PasswordVerify` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `pathIsDirectory` | Missing | `CLuaPathDefs.cpp:28` | `ArgumentParser<pathIsDirectory>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `pathIsFile` | Missing | `CLuaPathDefs.cpp:27` | `ArgumentParser<pathIsFile>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `pathListDir` | Missing | `CLuaPathDefs.cpp:26` | `ArgumentParser<pathListDir>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `pregFind` | Missing | `CLuaUtilDefs.cpp:65` | `PregFind` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `pregMatch` | Missing | `CLuaUtilDefs.cpp:67` | `PregMatch` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `pregReplace` | Missing | `CLuaUtilDefs.cpp:66` | `PregReplace` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `print` | **Implemented** | `CLuaUtilDefs.cpp:51` | `luaB_print` | Operational on Android. (lua_custom_print) |
| `ref` | Missing | `CLuaUtilDefs.cpp:45` | `Reference` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `removeClothingModel` | Missing | `CLuaManager.cpp:194` | `CLuaFunctionDefs::RemoveClothingModel` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `removeCommandHandler` | Missing | `CLuaManager.cpp:226` | `CLuaFunctionDefs::RemoveCommandHandler` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `removeDebugHook` | Missing | `CLuaManager.cpp:237` | `CLuaFunctionDefs::RemoveDebugHook` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `removeEventHandler` | **Implemented** | `CLuaManager.cpp:165` | `CLuaFunctionDefs::RemoveEventHandler` | Operational on Android. (l_removeEventHandler) |
| `require` | Missing | `CLuaMain.cpp:88` | `CLuaUtilDefs::DisabledFunction` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `resetNeonClientSettings` | Missing | `CLuaClientDefs.cpp:30` | `ArgumentParser<ResetNeonClientSettings>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `resetTimer` | Missing | `CLuaTimerDefs.cpp:20` | `ResetTimer` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setAnalogControlState` | Missing | `CLuaManager.cpp:213` | `CLuaFunctionDefs::SetAnalogControlState` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setClipboard` | Missing | `CLuaManager.cpp:180` | `CLuaFunctionDefs::SetClipboard` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setCursorAlpha` | Missing | `CLuaManager.cpp:202` | `CLuaFunctionDefs::SetCursorAlpha` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setCursorPosition` | Missing | `CLuaManager.cpp:198` | `CLuaFunctionDefs::SetCursorPosition` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setDevelopmentMode` | Missing | `CLuaManager.cpp:234` | `CLuaFunctionDefs::SetDevelopmentMode` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setNeonClientSetting` | Missing | `CLuaClientDefs.cpp:29` | `ArgumentParser<SetNeonClientSetting>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setTimer` | **Implemented** | `CLuaTimerDefs.cpp:18` | `SetTimer` | Operational on Android. (l_setTimer) |
| `setTimerPaused` | Missing | `CLuaTimerDefs.cpp:21` | `ArgumentParser<SetTimerPaused>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setTransferBoxVisible` | Missing | `CLuaClientDefs.cpp:19` | `ArgumentParser<SetTransferBoxVisible>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setWindowFlashing` | Missing | `CLuaManager.cpp:181` | `CLuaFunctionDefs::SetWindowFlashing` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `sha256` | Missing | `CLuaCryptDefs.cpp:23` | `ArgumentParserWarn<false, Sha256>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `showChat` | *Stub* | `CLuaClientDefs.cpp:22` | `ArgumentParserWarn<false, ShowChat>` | Currently stubbed: `DefaultValue(kTrue)`. Needs native engine wiring. |
| `showCursor` | **Implemented** | `CLuaManager.cpp:200` | `CLuaFunctionDefs::ShowCursor` | Operational on Android. (ShowCursor) |
| `split` | Missing | `CLuaUtilDefs.cpp:48` | `Split` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `teaDecode` | Missing | `CLuaCryptDefs.cpp:26` | `ArgumentParserWarn<false, TeaDecode>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `teaEncode` | Missing | `CLuaCryptDefs.cpp:25` | `ArgumentParserWarn<false, TeaEncode>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `toJSON` | **Implemented** | `CLuaUtilDefs.cpp:61` | `toJSON` | Operational on Android. (ToJson) |
| `tocolor` | **Implemented** | `CLuaUtilDefs.cpp:74` | `tocolor` | Operational on Android. (l_tocolor) |
| `toggleAllControls` | Missing | `CLuaManager.cpp:222` | `CLuaFunctionDefs::ToggleAllControls` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `toggleControl` | *Stub* | `CLuaManager.cpp:221` | `CLuaFunctionDefs::ToggleControl` | Currently stubbed: `DefaultValue(kTrue)`. Needs native engine wiring. |
| `triggerEvent` | Missing | `CLuaManager.cpp:167` | `CLuaFunctionDefs::TriggerEvent` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `triggerLatentServerEvent` | Missing | `CLuaManager.cpp:171` | `CLuaFunctionDefs::TriggerLatentServerEvent` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `triggerServerEvent` | **Implemented** | `CLuaManager.cpp:168` | `CLuaFunctionDefs::TriggerServerEvent` | Operational on Android. (l_triggerServerEvent) |
| `unbindKey` | *Stub* | `CLuaManager.cpp:210` | `CLuaFunctionDefs::UnbindKey` | Currently stubbed: `l_true: lua_pushboolean(L, 1)`. Needs native engine wiring. |
| `utfChar` | Missing | `CLuaUTFDefs.cpp:95` | `ArgumentParserWarn<nullptr, UtfChar>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `utfCode` | Missing | `CLuaUTFDefs.cpp:95` | `ArgumentParserWarn<false, UtfCode>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `utfLen` | Missing | `CLuaUTFDefs.cpp:94` | `ArgumentParserWarn<false, UtfLen>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `utfSeek` | Missing | `CLuaUTFDefs.cpp:94` | `ArgumentParserWarn<false, UtfSeek>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `utfSub` | Missing | `CLuaUTFDefs.cpp:94` | `ArgumentParserWarn<false, UtfSub>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `wasEventCancelled` | *Stub* | `CLuaManager.cpp:170` | `CLuaFunctionDefs::WasEventCancelled` | Currently stubbed: `DefaultValue(kFalse)`. Needs native engine wiring. |
| `xmlCopyFile` | Missing | `CLuaXMLDefs.cpp:21` | `xmlCopyFile` | Needs TinyXML2 client-side parser integration. |
| `xmlCreateChild` | Missing | `CLuaXMLDefs.cpp:24` | `xmlCreateChild` | Needs TinyXML2 client-side parser integration. |
| `xmlCreateFile` | Missing | `CLuaXMLDefs.cpp:18` | `xmlCreateFile` | Needs TinyXML2 client-side parser integration. |
| `xmlDestroyNode` | Missing | `CLuaXMLDefs.cpp:25` | `xmlDestroyNode` | Needs TinyXML2 client-side parser integration. |
| `xmlFindChild` | Missing | `CLuaXMLDefs.cpp:26` | `xmlNodeFindChild` | Needs TinyXML2 client-side parser integration. |
| `xmlLoadFile` | Missing | `CLuaXMLDefs.cpp:19` | `xmlLoadFile` | Needs TinyXML2 client-side parser integration. |
| `xmlLoadString` | Missing | `CLuaXMLDefs.cpp:20` | `xmlLoadString` | Needs TinyXML2 client-side parser integration. |
| `xmlNodeGetAttribute` | Missing | `CLuaXMLDefs.cpp:32` | `xmlNodeGetAttribute` | Needs TinyXML2 client-side parser integration. |
| `xmlNodeGetAttributes` | Missing | `CLuaXMLDefs.cpp:31` | `xmlNodeGetAttributes` | Needs TinyXML2 client-side parser integration. |
| `xmlNodeGetChildren` | Missing | `CLuaXMLDefs.cpp:28` | `xmlNodeGetChildren` | Needs TinyXML2 client-side parser integration. |
| `xmlNodeGetName` | Missing | `CLuaXMLDefs.cpp:33` | `xmlNodeGetName` | Needs TinyXML2 client-side parser integration. |
| `xmlNodeGetParent` | Missing | `CLuaXMLDefs.cpp:29` | `xmlNodeGetParent` | Needs TinyXML2 client-side parser integration. |
| `xmlNodeGetValue` | Missing | `CLuaXMLDefs.cpp:30` | `xmlNodeGetValue` | Needs TinyXML2 client-side parser integration. |
| `xmlNodeSetAttribute` | Missing | `CLuaXMLDefs.cpp:36` | `xmlNodeSetAttribute` | Needs TinyXML2 client-side parser integration. |
| `xmlNodeSetName` | Missing | `CLuaXMLDefs.cpp:37` | `xmlNodeSetName` | Needs TinyXML2 client-side parser integration. |
| `xmlNodeSetValue` | Missing | `CLuaXMLDefs.cpp:35` | `xmlNodeSetValue` | Needs TinyXML2 client-side parser integration. |
| `xmlSaveFile` | Missing | `CLuaXMLDefs.cpp:22` | `xmlSaveFile` | Needs TinyXML2 client-side parser integration. |
| `xmlUnloadFile` | Missing | `CLuaXMLDefs.cpp:23` | `xmlUnloadFile` | Needs TinyXML2 client-side parser integration. |
