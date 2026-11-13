# Engine & Model Replacement (DFF, TXD, COL)

Custom DFF mesh loading, TXD texture dictionaries, COL collision models, model handling overrides, texture filtering, and breakable objects.

## Parity Status

- **Total Functions:** 125
- **Implemented:** 1 (0.8%)
- **Stubs:** 15
- **Missing:** 109

> **Technical Requirement:** Requires hooking War Drum's RenderWare stream loaders (`RwStreamOpen`, `RwTexDictionaryStreamRead`, `RpClumpStreamRead`) and `CModelInfo` array.

## Function List

| Lua Function | Status | Desktop Source Ref | Desktop Handler | Conversion Requirement / Implementation Notes |
|---|---|---|---|---|
| `clearBreakEffectCache` | Missing | `CLuaBreakEffectDefs.cpp:634` | `ClearBreakEffectCache` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `clearObjectBreakProfile` | Missing | `CLuaBreakEffectDefs.cpp:640` | `ClearObjectBreakProfile` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `createObjectBreakEffect` | Missing | `CLuaBreakEffectDefs.cpp:626` | `CreateObjectBreakEffect` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `engineAddClothingModel` | Missing | `CLuaEngineDefs.cpp:157` | `ArgumentParser<EngineAddClothingModel>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `engineAddClothingTXD` | Missing | `CLuaEngineDefs.cpp:153` | `ArgumentParser<EngineAddClothingTXD>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `engineAddImage` | *Stub* | `CLuaEngineDefs.cpp:215` | `ArgumentParser<EngineAddImage>` | Currently stubbed: `DefaultValue(kNil)`. Needs native engine wiring. |
| `engineApplyShaderToWorldTexture` | Missing | `CLuaEngineDefs.cpp:183` | `EngineApplyShaderToWorldTexture` | Needs GLSL ES 3.0 shader pipeline (translating desktop D3DX9 HLSL). |
| `engineCreateCullZone` | Missing | `CLuaEngineDefs.cpp:177` | `EngineCreateCullZone` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `engineFreeModel` | *Stub* | `CLuaEngineDefs.cpp:139` | `EngineFreeModel` | Currently stubbed: `DefaultValue(kTrue)`. Needs native engine wiring. |
| `engineFreeTXD` | *Stub* | `CLuaEngineDefs.cpp:237` | `ArgumentParser<EngineFreeTXD>` | Currently stubbed: `DefaultValue(kTrue)`. Needs native engine wiring. |
| `engineGetCullZones` | Missing | `CLuaEngineDefs.cpp:176` | `EngineGetCullZones` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `engineGetDistantLightStats` | Missing | `CLuaEngineDefs.cpp:175` | `EngineGetDistantLightStats` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `engineGetModelFlag` | Missing | `CLuaEngineDefs.cpp:204` | `ArgumentParser<EngineGetModelFlag>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `engineGetModelFlags` | Missing | `CLuaEngineDefs.cpp:202` | `ArgumentParser<EngineGetModelFlags>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `engineGetModelIDFromName` | Missing | `CLuaEngineDefs.cpp:186` | `EngineGetModelIDFromName` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `engineGetModelLODDistance` | Missing | `CLuaEngineDefs.cpp:166` | `EngineGetModelLODDistance` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `engineGetModelNameFromID` | Missing | `CLuaEngineDefs.cpp:185` | `EngineGetModelNameFromID` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `engineGetModelParent` | Missing | `CLuaEngineDefs.cpp:164` | `EngineGetModelParent` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `engineGetModelPhysicalPropertiesGroup` | Missing | `CLuaEngineDefs.cpp:196` | `EngineGetModelPhysicalPropertiesGroup` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `engineGetModelRuntimeID` | Missing | `CLuaEngineDefs.cpp:162` | `EngineGetModelRuntimeID` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `engineGetModelServerID` | Missing | `CLuaEngineDefs.cpp:163` | `EngineGetModelServerID` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `engineGetModelTXDID` | Missing | `CLuaEngineDefs.cpp:220` | `ArgumentParser<EngineGetModelTXDID>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `engineGetModelTextureNames` | Missing | `CLuaEngineDefs.cpp:187` | `EngineGetModelTextureNames` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `engineGetModelTextures` | Missing | `CLuaEngineDefs.cpp:191` | `EngineGetModelTextures` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `engineGetModelType` | Missing | `CLuaEngineDefs.cpp:165` | `EngineGetModelType` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `engineGetModelVisibleTime` | Missing | `CLuaEngineDefs.cpp:190` | `ArgumentParser<EngineGetModelVisibleTime>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `engineGetObjectGroupPhysicalProperty` | Missing | `CLuaEngineDefs.cpp:200` | `EngineGetObjectGroupPhysicalProperty` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `engineGetObjectStreamingLimits` | Missing | `CLuaEngineDefs.cpp:209` | `ArgumentParser<EngineGetObjectStreamingLimits>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `engineGetPoolCapacity` | Missing | `CLuaEngineDefs.cpp:238` | `ArgumentParser<EngineGetPoolCapacity>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `engineGetPoolDefaultCapacity` | Missing | `CLuaEngineDefs.cpp:239` | `ArgumentParser<EngineGetPoolDefaultCapacity>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `engineGetPoolUsedCapacity` | Missing | `CLuaEngineDefs.cpp:240` | `ArgumentParser<EngineGetPoolUsedCapacity>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `engineGetRadarMapStats` | Missing | `CLuaEngineDefs.cpp:152` | `EngineGetRadarMapStats` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `engineGetRendererStats` | Missing | `CLuaEngineDefs.cpp:169` | `EngineGetRendererStats` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `engineGetSurfaceProperties` | Missing | `CLuaEngineDefs.cpp:193` | `EngineGetSurfaceProperties` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `engineGetTXDIDFromName` | *Stub* | `CLuaEngineDefs.cpp:221` | `ArgumentParser<EngineGetTXDIDFromName>` | Currently stubbed: `DefaultValue(kZero)`. Needs native engine wiring. |
| `engineGetVisibleTextureNames` | Missing | `CLuaEngineDefs.cpp:188` | `EngineGetVisibleTextureNames` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `engineImageGetFile` | Missing | `CLuaEngineDefs.cpp:219` | `ArgumentParser<EngineImageGetFile>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `engineImageGetFiles` | Missing | `CLuaEngineDefs.cpp:218` | `ArgumentParser<EngineImageGetFileList>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `engineImageGetFilesCount` | Missing | `CLuaEngineDefs.cpp:217` | `ArgumentParser<EngineImageGetFilesCount>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `engineImageLinkDFF` | *Stub* | `CLuaEngineDefs.cpp:211` | `ArgumentParser<EngineImageLinkDFF>` | Currently stubbed: `DefaultValue(kTrue)`. Needs native engine wiring. |
| `engineImageLinkTXD` | *Stub* | `CLuaEngineDefs.cpp:212` | `ArgumentParser<EngineImageLinkTXD>` | Currently stubbed: `DefaultValue(kTrue)`. Needs native engine wiring. |
| `engineImportTXD` | Missing | `CLuaEngineDefs.cpp:149` | `EngineImportTXD` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `engineLoadCOL` | *Stub* | `CLuaEngineDefs.cpp:141` | `EngineLoadCOL` | Currently stubbed: `DefaultValue(kNil)`. Needs native engine wiring. |
| `engineLoadDFF` | Missing | `CLuaEngineDefs.cpp:142` | `EngineLoadDFF` | Needs hooking WarDrum RenderWare streaming clump/texture loader. |
| `engineLoadIFP` | Missing | `CLuaEngineDefs.cpp:144` | `EngineLoadIFP` | Needs hooking WarDrum RenderWare streaming clump/texture loader. |
| `engineLoadIMG` | *Stub* | `CLuaEngineDefs.cpp:210` | `ArgumentParser<EngineLoadIMG>` | Currently stubbed: `DefaultValue(kNil)`. Needs native engine wiring. |
| `engineLoadTXD` | Missing | `CLuaEngineDefs.cpp:140` | `EngineLoadTXD` | Needs hooking WarDrum RenderWare streaming clump/texture loader. |
| `engineLoadVehicleAudioConfig` | Missing | `CLuaEngineDefs.cpp:145` | `EngineLoadVehicleAudioConfig` | Needs hooking WarDrum RenderWare streaming clump/texture loader. |
| `engineParseSAMPMap` | *Stub* | `CLuaEngineDefs.cpp:192` | `EngineParseSAMPMap` | Currently stubbed: `DefaultValue(kNil)`. Needs native engine wiring. |
| `enginePlayVehicleAudioBackfire` | Missing | `CLuaEngineDefs.cpp:148` | `EnginePlayVehicleAudioBackfire` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `enginePreloadWorldArea` | Missing | `CLuaEngineDefs.cpp:242` | `ArgumentParser<EnginePreloadWorldArea>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `enginePreloadWorldAreaInDirection` | Missing | `CLuaEngineDefs.cpp:243` | `ArgumentParser<EnginePreloadWorldAreaInDirection>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `engineRebuildDistantLights` | Missing | `CLuaEngineDefs.cpp:174` | `EngineRebuildDistantLights` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `engineReloadVehicleAudioConfig` | Missing | `CLuaEngineDefs.cpp:146` | `EngineReloadVehicleAudioConfig` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `engineRemoveCullZone` | Missing | `CLuaEngineDefs.cpp:180` | `EngineRemoveCullZone` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `engineRemoveImage` | Missing | `CLuaEngineDefs.cpp:216` | `ArgumentParser<EngineRemoveImage>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `engineRemoveShaderFromWorldTexture` | Missing | `CLuaEngineDefs.cpp:184` | `EngineRemoveShaderFromWorldTexture` | Needs GLSL ES 3.0 shader pipeline (translating desktop D3DX9 HLSL). |
| `engineReplaceAnimation` | Missing | `CLuaEngineDefs.cpp:159` | `EngineReplaceAnimation` | Needs hooking WarDrum RenderWare streaming clump/texture loader. |
| `engineReplaceCOL` | *Stub* | `CLuaEngineDefs.cpp:154` | `EngineReplaceCOL` | Currently stubbed: `DefaultValue(kTrue)`. Needs native engine wiring. |
| `engineReplaceEncryptedModel` | Missing | `CLuaEngineDefs.cpp:143` | `EngineReplaceEncryptedModel` | Needs hooking WarDrum RenderWare streaming clump/texture loader. |
| `engineReplaceModel` | Missing | `CLuaEngineDefs.cpp:156` | `EngineReplaceModel` | Needs hooking WarDrum RenderWare streaming clump/texture loader. |
| `engineRequestModel` | *Stub* | `CLuaEngineDefs.cpp:161` | `EngineRequestModel` | Currently stubbed: `DefaultValue(kZero)`. Needs native engine wiring. |
| `engineRequestTXD` | *Stub* | `CLuaEngineDefs.cpp:236` | `ArgumentParser<EngineRequestTXD>` | Currently stubbed: `DefaultValue(kZero)`. Needs native engine wiring. |
| `engineResetModelFlags` | Missing | `CLuaEngineDefs.cpp:206` | `ArgumentParser<EngineResetModelFlags>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `engineResetModelLODDistance` | Missing | `CLuaEngineDefs.cpp:168` | `EngineResetModelLODDistance` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `engineResetModelTXDID` | Missing | `CLuaEngineDefs.cpp:223` | `ArgumentParser<EngineResetModelTXDID>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `engineResetRadarMapTile` | Missing | `CLuaEngineDefs.cpp:151` | `EngineResetRadarMapTile` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `engineResetRendererStats` | Missing | `CLuaEngineDefs.cpp:170` | `EngineResetRendererStats` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `engineResetSurfaceProperties` | Missing | `CLuaEngineDefs.cpp:195` | `EngineResetSurfaceProperties` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `engineRestoreAnimation` | Missing | `CLuaEngineDefs.cpp:160` | `EngineRestoreAnimation` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `engineRestoreCOL` | Missing | `CLuaEngineDefs.cpp:155` | `EngineRestoreCOL` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `engineRestoreCullZone` | Missing | `CLuaEngineDefs.cpp:181` | `EngineRestoreCullZone` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `engineRestoreDFFImage` | Missing | `CLuaEngineDefs.cpp:213` | `ArgumentParser<EngineRestoreDFFImage>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `engineRestoreModel` | Missing | `CLuaEngineDefs.cpp:158` | `EngineRestoreModel` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `engineRestoreModelPhysicalPropertiesGroup` | Missing | `CLuaEngineDefs.cpp:198` | `EngineRestoreModelPhysicalPropertiesGroup` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `engineRestoreObjectGroupPhysicalProperties` | Missing | `CLuaEngineDefs.cpp:201` | `EngineRestoreObjectGroupPhysicalProperties` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `engineRestoreTXDImage` | Missing | `CLuaEngineDefs.cpp:214` | `ArgumentParser<EngineRestoreTXDImage>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `engineRestream` | Missing | `CLuaEngineDefs.cpp:245` | `ArgumentParser<EngineRestream>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `engineRestreamModel` | Missing | `CLuaEngineDefs.cpp:244` | `ArgumentParser<EngineRestreamModel>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `engineRestreamWorld` | Missing | `CLuaEngineDefs.cpp:207` | `ArgumentParser<EngineRestreamWorld>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `engineSetAsynchronousLoading` | Missing | `CLuaEngineDefs.cpp:182` | `EngineSetAsynchronousLoading` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `engineSetCullZone` | Missing | `CLuaEngineDefs.cpp:178` | `EngineSetCullZone` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `engineSetCullZoneEnabled` | Missing | `CLuaEngineDefs.cpp:179` | `EngineSetCullZoneEnabled` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `engineSetDistantLightsAutomaticDrawDistance` | Missing | `CLuaEngineDefs.cpp:173` | `EngineSetDistantLightsAutomaticDrawDistance` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `engineSetDistantLightsDrawDistance` | Missing | `CLuaEngineDefs.cpp:172` | `EngineSetDistantLightsDrawDistance` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `engineSetDistantLightsEnabled` | Missing | `CLuaEngineDefs.cpp:171` | `EngineSetDistantLightsEnabled` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `engineSetModelFlag` | Missing | `CLuaEngineDefs.cpp:205` | `ArgumentParser<EngineSetModelFlag>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `engineSetModelFlags` | *Stub* | `CLuaEngineDefs.cpp:203` | `ArgumentParser<EngineSetModelFlags>` | Currently stubbed: `DefaultValue(kTrue)`. Needs native engine wiring. |
| `engineSetModelLODDistance` | *Stub* | `CLuaEngineDefs.cpp:167` | `EngineSetModelLODDistance` | Currently stubbed: `DefaultValue(kTrue)`. Needs native engine wiring. |
| `engineSetModelPhysicalPropertiesGroup` | Missing | `CLuaEngineDefs.cpp:197` | `EngineSetModelPhysicalPropertiesGroup` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `engineSetModelTXDID` | *Stub* | `CLuaEngineDefs.cpp:222` | `ArgumentParser<EngineSetModelTXDID>` | Currently stubbed: `DefaultValue(kTrue)`. Needs native engine wiring. |
| `engineSetModelVisibleTime` | Missing | `CLuaEngineDefs.cpp:189` | `ArgumentParser<EngineSetModelVisibleTime>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `engineSetObjectGroupPhysicalProperty` | Missing | `CLuaEngineDefs.cpp:199` | `EngineSetObjectGroupPhysicalProperty` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `engineSetObjectStreamingLimits` | Missing | `CLuaEngineDefs.cpp:208` | `ArgumentParser<EngineSetObjectStreamingLimits>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `engineSetPoolCapacity` | Missing | `CLuaEngineDefs.cpp:241` | `ArgumentParser<EngineSetPoolCapacity>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `engineSetRadarMapTile` | Missing | `CLuaEngineDefs.cpp:150` | `EngineSetRadarMapTile` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `engineSetSurfaceProperties` | Missing | `CLuaEngineDefs.cpp:194` | `EngineSetSurfaceProperties` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `engineStreamingFreeUpMemory` | Missing | `CLuaEngineDefs.cpp:224` | `ArgumentParser<EngineStreamingFreeUpMemory>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `engineStreamingGetBufferSize` | Missing | `CLuaEngineDefs.cpp:231` | `ArgumentParser<EngineStreamingGetBufferSize>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `engineStreamingGetMemorySize` | Missing | `CLuaEngineDefs.cpp:227` | `ArgumentParser<EngineStreamingGetMemorySize>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `engineStreamingGetModelLoadState` | **Implemented** | `CLuaEngineDefs.cpp:235` | `ArgumentParser<EngineStreamingGetModelLoadState>` | Operational on Android. (l_engineStreamingGetModelLoadState) |
| `engineStreamingGetUsedMemory` | Missing | `CLuaEngineDefs.cpp:225` | `ArgumentParser<EngineStreamingGetUsedMemory>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `engineStreamingReleaseModel` | Missing | `CLuaEngineDefs.cpp:234` | `ArgumentParser<EngineStreamingReleaseModel>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `engineStreamingRequestModel` | Missing | `CLuaEngineDefs.cpp:233` | `ArgumentParser<EngineStreamingRequestModel>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `engineStreamingRestoreBufferSize` | Missing | `CLuaEngineDefs.cpp:232` | `ArgumentParser<EngineStreamingRestoreBufferSize>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `engineStreamingRestoreMemorySize` | Missing | `CLuaEngineDefs.cpp:229` | `ArgumentParser<EngineStreamingRestoreMemorySize>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `engineStreamingSetBufferSize` | Missing | `CLuaEngineDefs.cpp:230` | `ArgumentParser<EngineStreamingSetBufferSize>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `engineStreamingSetMemorySize` | Missing | `CLuaEngineDefs.cpp:226` | `ArgumentParser<EngineStreamingSetMemorySize>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `engineStreamingSetModelCacheLimits` | Missing | `CLuaEngineDefs.cpp:228` | `ArgumentParser<EngineStreamingSetModelCacheLimits>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `engineUnloadVehicleAudioConfig` | Missing | `CLuaEngineDefs.cpp:147` | `EngineUnloadVehicleAudioConfig` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getBreakEffectCacheHit` | Missing | `CLuaBreakEffectDefs.cpp:630` | `GetBreakEffectCacheHit` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getBreakEffectCacheSize` | Missing | `CLuaBreakEffectDefs.cpp:633` | `GetBreakEffectCacheSize` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getBreakEffectFragmentCount` | Missing | `CLuaBreakEffectDefs.cpp:627` | `GetBreakEffectFragmentCount` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getBreakEffectSleepingFragmentCount` | Missing | `CLuaBreakEffectDefs.cpp:629` | `GetBreakEffectSleepingFragmentCount` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getBreakEffectSourceTriangleCount` | Missing | `CLuaBreakEffectDefs.cpp:628` | `GetBreakEffectSourceTriangleCount` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getObjectBreakHealth` | Missing | `CLuaBreakEffectDefs.cpp:637` | `GetObjectBreakHealth` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getObjectBreakProfile` | Missing | `CLuaBreakEffectDefs.cpp:636` | `GetObjectBreakProfile` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getPostFXMode` | Missing | `CLuaPostfxDefs.cpp:20` | `ArgumentParser<GetPostFXMode>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getPostFXValue` | Missing | `CLuaPostfxDefs.cpp:19` | `ArgumentParser<GetPostFXValue>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `isBreakEffectPaused` | Missing | `CLuaBreakEffectDefs.cpp:631` | `IsBreakEffectPaused` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `isPostFXEnabled` | Missing | `CLuaPostfxDefs.cpp:21` | `ArgumentParser<IsPostFXEnabled>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `resetObjectBreakHealth` | Missing | `CLuaBreakEffectDefs.cpp:639` | `ResetObjectBreakHealth` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setBreakEffectPaused` | Missing | `CLuaBreakEffectDefs.cpp:632` | `SetBreakEffectPaused` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setObjectBreakHealth` | Missing | `CLuaBreakEffectDefs.cpp:638` | `SetObjectBreakHealth` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setObjectBreakProfile` | Missing | `CLuaBreakEffectDefs.cpp:635` | `SetObjectBreakProfile` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
