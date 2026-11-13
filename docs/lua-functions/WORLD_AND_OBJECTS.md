# World, Objects & Environment

3D objects, world weather, game time, water levels, gravity, minute duration, heat haze, explosions, fires, garage doors, searchlights, and foliage.

## Parity Status

- **Total Functions:** 227
- **Implemented:** 2 (0.9%)
- **Stubs:** 15
- **Missing:** 210

> **Technical Requirement:** Hooks libGTASA.so `CObject`, `CWeather`, `CClock`, `CWaterLevel`, `CWorld`, and particle engine `FxSystem_c`.

## Function List

| Lua Function | Status | Desktop Source Ref | Desktop Handler | Conversion Requirement / Implementation Notes |
|---|---|---|---|---|
| `acquireGarageControl` | Missing | `CLuaWorldDefs.cpp:131` | `ArgumentParser<AcquireGarageControl>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `acquireObjectGangTag` | Missing | `CLuaObjectDefs.cpp:46` | `ArgumentParser<AcquireObjectGangTag>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `areTrafficLightsLocked` | Missing | `CLuaWorldDefs.cpp:200` | `AreTrafficLightsLocked` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `breakObject` | Missing | `CLuaObjectDefs.cpp:38` | `BreakObject` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `createBuilding` | Missing | `CLuaBuildingDefs.cpp:17` | `ArgumentParser<CreateBuilding>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `createEffect` | Missing | `CLuaEffectDefs.cpp:32` | `CreateEffect` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `createExplosion` | Missing | `CLuaWorldDefs.cpp:175` | `CreateExplosion` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `createFire` | Missing | `CLuaFireDefs.cpp:149` | `CreateFire` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `createFoliage` | Missing | `CLuaFoliageDefs.cpp:42` | `CreateFoliage` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `createObject` | *Stub* | `CLuaObjectDefs.cpp:21` | `CreateObject` | Currently stubbed: `FalseResult: lua_pushboolean(L, 0)`. Needs native engine wiring. |
| `createSWATRope` | Missing | `CLuaWorldDefs.cpp:174` | `CreateSWATRope` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `createWater` | Missing | `CLuaWaterDefs.cpp:18` | `CreateWater` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `engineGetPedPathGraphRevision` | Missing | `CLuaWorldDefs.cpp:60` | `EngineGetPedPathGraphRevision` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `engineGetPedPathPatchRevision` | Missing | `CLuaWorldDefs.cpp:59` | `EngineGetPedPathPatchRevision` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `engineLoadPedPathPatch` | Missing | `CLuaWorldDefs.cpp:57` | `EngineLoadPedPathPatch` | Needs hooking WarDrum RenderWare streaming clump/texture loader. |
| `engineUnloadPedPathPatch` | Missing | `CLuaWorldDefs.cpp:58` | `EngineUnloadPedPathPatch` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `extinguishFire` | Missing | `CLuaFireDefs.cpp:150` | `ExtinguishFire` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `fxAddBlood` | *Stub* | `CLuaEffectDefs.cpp:18` | `fxAddBlood` | Currently stubbed: `DefaultValue(kTrue)`. Needs native engine wiring. |
| `fxAddBulletImpact` | Missing | `CLuaEffectDefs.cpp:22` | `fxAddBulletImpact` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `fxAddBulletSplash` | Missing | `CLuaEffectDefs.cpp:30` | `fxAddBulletSplash` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `fxAddDebris` | Missing | `CLuaEffectDefs.cpp:24` | `fxAddDebris` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `fxAddFootSplash` | Missing | `CLuaEffectDefs.cpp:31` | `fxAddFootSplash` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `fxAddGlass` | Missing | `CLuaEffectDefs.cpp:25` | `fxAddGlass` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `fxAddGunshot` | Missing | `CLuaEffectDefs.cpp:27` | `fxAddGunshot` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `fxAddPunchImpact` | Missing | `CLuaEffectDefs.cpp:23` | `fxAddPunchImpact` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `fxAddSparks` | Missing | `CLuaEffectDefs.cpp:20` | `fxAddSparks` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `fxAddTankFire` | Missing | `CLuaEffectDefs.cpp:28` | `fxAddTankFire` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `fxAddTyreBurst` | Missing | `CLuaEffectDefs.cpp:21` | `fxAddTyreBurst` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `fxAddWaterHydrant` | Missing | `CLuaEffectDefs.cpp:26` | `fxAddWaterHydrant` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `fxAddWaterSplash` | Missing | `CLuaEffectDefs.cpp:29` | `fxAddWaterSplash` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `fxAddWood` | Missing | `CLuaEffectDefs.cpp:19` | `fxAddWood` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `fxCreateParticle` | Missing | `CLuaEffectDefs.cpp:37` | `ArgumentParser<FxCreateParticle>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getAircraftMaxHeight` | Missing | `CLuaWorldDefs.cpp:94` | `GetAircraftMaxHeight` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getAircraftMaxVelocity` | Missing | `CLuaWorldDefs.cpp:95` | `GetAircraftMaxVelocity` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getAmbientPedCivilianCoupleCandidate` | Missing | `CLuaWorldDefs.cpp:110` | `GetAmbientPedCivilianCoupleCandidate` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getAmbientPedGangGroupCandidate` | Missing | `CLuaWorldDefs.cpp:109` | `GetAmbientPedGangGroupCandidate` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getAmbientPedPopulationProfile` | **Implemented** | `CLuaWorldDefs.cpp:106` | `GetAmbientPedPopulationProfile` | Operational on Android. (GetAmbientPedPopulationProfile) |
| `getAmbientPedSpawnCandidate` | Missing | `CLuaWorldDefs.cpp:108` | `GetAmbientPedSpawnCandidate` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getAmbientVehicleModelCandidate` | Missing | `CLuaWorldDefs.cpp:112` | `GetAmbientVehicleModelCandidate` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getAmbientVehicleOccupantModelCandidate` | Missing | `CLuaWorldDefs.cpp:113` | `GetAmbientVehicleOccupantModelCandidate` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getAmbientVehicleSpawnCandidate` | Missing | `CLuaWorldDefs.cpp:111` | `GetAmbientVehicleSpawnCandidate` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getBirdsEnabled` | Missing | `CLuaWorldDefs.cpp:101` | `GetBirdsEnabled` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getBlurLevel` | Missing | `CLuaWorldDefs.cpp:79` | `GetBlurLevel` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getCloudsEnabled` | Missing | `CLuaWorldDefs.cpp:97` | `GetCloudsEnabled` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getColorFilter` | Missing | `CLuaWorldDefs.cpp:63` | `ArgumentParser<GetColorFilter>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getCoronaReflectionsEnabled` | Missing | `CLuaWorldDefs.cpp:114` | `ArgumentParser<GetCoronaReflectionsEnabled>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getEffectDensity` | Missing | `CLuaEffectDefs.cpp:36` | `GetEffectDensity` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getEffectSpeed` | Missing | `CLuaEffectDefs.cpp:34` | `GetEffectSpeed` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getFPSLimit` | Missing | `CLuaWorldDefs.cpp:100` | `GetFPSLimit` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getFarClipDistance` | Missing | `CLuaWorldDefs.cpp:87` | `GetFarClipDistance` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getFireDamageTargets` | Missing | `CLuaFireDefs.cpp:159` | `GetFireDamageTargets` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getFireDuration` | Missing | `CLuaFireDefs.cpp:151` | `GetFireDuration` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getFireMaxGenerations` | Missing | `CLuaFireDefs.cpp:163` | `GetFireMaxGenerations` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getFireRemainingTime` | Missing | `CLuaFireDefs.cpp:153` | `GetFireRemainingTime` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getFireSource` | Missing | `CLuaFireDefs.cpp:165` | `GetFireSource` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getFireStrength` | Missing | `CLuaFireDefs.cpp:155` | `GetFireStrength` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getFireTarget` | Missing | `CLuaFireDefs.cpp:167` | `GetFireTarget` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getFogDistance` | Missing | `CLuaWorldDefs.cpp:91` | `GetFogDistance` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getFoliageDensity` | Missing | `CLuaFoliageDefs.cpp:44` | `GetFoliageDensity` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getFoliageSurface` | Missing | `CLuaFoliageDefs.cpp:42` | `GetFoliageSurface` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getFoliageVertices` | Missing | `CLuaFoliageDefs.cpp:43` | `GetFoliageVertices` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getGameSpeed` | Missing | `CLuaWorldDefs.cpp:73` | `GetGameSpeed` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getGarageBoundingBox` | Missing | `CLuaWorldDefs.cpp:78` | `GetGarageBoundingBox` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getGaragePosition` | Missing | `CLuaWorldDefs.cpp:76` | `GetGaragePosition` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getGarageSize` | Missing | `CLuaWorldDefs.cpp:77` | `GetGarageSize` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getGravity` | Missing | `CLuaWorldDefs.cpp:72` | `GetGravity` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getGroundPosition` | *Stub* | `CLuaWorldDefs.cpp:65` | `GetGroundPosition` | Currently stubbed: `l_num0: lua_pushinteger(L, 0)`. Needs native engine wiring. |
| `getHeatHaze` | Missing | `CLuaWorldDefs.cpp:82` | `GetHeatHaze` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getInteriorFurnitureEnabled` | Missing | `CLuaWorldDefs.cpp:86` | `GetInteriorFurnitureEnabled` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getInteriorSoundsEnabled` | Missing | `CLuaWorldDefs.cpp:85` | `GetInteriorSoundsEnabled` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getJetpackMaxHeight` | Missing | `CLuaWorldDefs.cpp:83` | `GetJetpackMaxHeight` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getMinuteDuration` | Missing | `CLuaWorldDefs.cpp:74` | `GetMinuteDuration` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getMoonSize` | Missing | `CLuaWorldDefs.cpp:99` | `GetMoonSize` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getNearClipDistance` | Missing | `CLuaWorldDefs.cpp:88` | `GetNearClipDistance` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getObjectGangTagProgress` | Missing | `CLuaObjectDefs.cpp:48` | `ArgumentParser<GetObjectGangTagProgress>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getObjectMass` | Missing | `CLuaObjectDefs.cpp:27` | `GetObjectMass` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getObjectProperty` | Missing | `CLuaObjectDefs.cpp:28` | `GetObjectProperty` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getObjectScale` | Missing | `CLuaObjectDefs.cpp:25` | `GetObjectScale` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getOcclusionsEnabled` | Missing | `CLuaWorldDefs.cpp:96` | `GetOcclusionsEnabled` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getPedsLODDistance` | Missing | `CLuaWorldDefs.cpp:90` | `GetPedsLODDistance` | Needs reading/writing CPed / CPlayerPed struct fields in libGTASA.so. |
| `getRainLevel` | Missing | `CLuaWorldDefs.cpp:98` | `GetRainLevel` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getRoofPosition` | Missing | `CLuaWorldDefs.cpp:64` | `GetRoofPosition` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getScreenFromWorldPosition` | *Stub* | `CLuaWorldDefs.cpp:69` | `GetScreenFromWorldPosition` | Currently stubbed: `DefaultValue(kNil)`. Needs native engine wiring. |
| `getSkyGradient` | Missing | `CLuaWorldDefs.cpp:81` | `GetSkyGradient` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getSunColor` | Missing | `CLuaWorldDefs.cpp:92` | `GetSunColor` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getSunSize` | Missing | `CLuaWorldDefs.cpp:93` | `GetSunSize` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getTime` | **Implemented** | `CLuaWorldDefs.cpp:62` | `GetTime` | Operational on Android. (l_getTime) |
| `getTrafficLightState` | Missing | `CLuaWorldDefs.cpp:80` | `GetTrafficLightState` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getVehiclesLODDistance` | Missing | `CLuaWorldDefs.cpp:89` | `GetVehiclesLODDistance` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `getWaterColor` | Missing | `CLuaWaterDefs.cpp:28` | `GetWaterColor` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getWaterLevel` | *Stub* | `CLuaWaterDefs.cpp:29` | `GetWaterLevel` | Currently stubbed: `DefaultValue(kNil)`. Needs native engine wiring. |
| `getWaterVertexPosition` | Missing | `CLuaWaterDefs.cpp:30` | `GetWaterVertexPosition` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getWaveHeight` | Missing | `CLuaWorldDefs.cpp:75` | `GetWaveHeight` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getWeather` | Missing | `CLuaWorldDefs.cpp:70` | `GetWeather` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getWindVelocity` | Missing | `CLuaWorldDefs.cpp:84` | `GetWindVelocity` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getWorldFromScreenPosition` | Missing | `CLuaWorldDefs.cpp:68` | `GetWorldFromScreenPosition` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getWorldProperty` | Missing | `CLuaWorldDefs.cpp:115` | `ArgumentParser<GetWorldProperty>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getZoneName` | Missing | `CLuaWorldDefs.cpp:71` | `GetZoneName` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `isAmbientPedSphereVisible` | *Stub* | `CLuaWorldDefs.cpp:107` | `ArgumentParser<IsAmbientPedSphereVisible>` | Currently stubbed: `l_false: lua_pushboolean(L, 0)`. Needs native engine wiring. |
| `isDynamicPedShadowsEnabled` | Missing | `CLuaWorldDefs.cpp:207` | `ArgumentParser<IsDynamicPedShadowsEnabled>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `isFireDamageEnabled` | Missing | `CLuaFireDefs.cpp:157` | `IsFireDamageEnabled` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `isFireSpreadEnabled` | Missing | `CLuaFireDefs.cpp:161` | `IsFireSpreadEnabled` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `isGarageOpen` | Missing | `CLuaWorldDefs.cpp:204` | `IsGarageOpen` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `isLineOfSightClear` | *Stub* | `CLuaWorldDefs.cpp:202` | `IsLineOfSightClear` | Currently stubbed: `DefaultValue(kFalse)`. Needs native engine wiring. |
| `isObjectBreakable` | Missing | `CLuaObjectDefs.cpp:26` | `IsObjectBreakable` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `isObjectMoving` | Missing | `CLuaObjectDefs.cpp:29` | `ArgumentParser<IsObjectMoving>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `isObjectRespawnable` | Missing | `CLuaObjectDefs.cpp:30` | `ArgumentParser<IsObjectRespawnable>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `isObjectStatic` | Missing | `CLuaObjectDefs.cpp:24` | `IsObjectStatic` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `isPedTargetingMarkerEnabled` | Missing | `CLuaWorldDefs.cpp:201` | `IsPedTargetingMarkerEnabled` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `isTimeFrozen` | Missing | `CLuaWorldDefs.cpp:205` | `ArgumentParser<IsTimeFrozen>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `isVolumetricShadowsEnabled` | Missing | `CLuaWorldDefs.cpp:206` | `ArgumentParser<IsVolumetricShadowsEnabled>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `isWaterDrawnLast` | Missing | `CLuaWaterDefs.cpp:31` | `IsWaterDrawnLast` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `isWorldSpecialPropertyEnabled` | Missing | `CLuaWorldDefs.cpp:203` | `ArgumentParserWarn<false, IsWorldSpecialPropertyEnabled>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `moveObject` | Missing | `CLuaObjectDefs.cpp:33` | `MoveObject` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `processLineAgainstMesh` | Missing | `CLuaWorldDefs.cpp:66` | `ArgumentParser<ProcessLineAgainstMesh>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `processLineOfSight` | *Stub* | `CLuaWorldDefs.cpp:67` | `ProcessLineOfSight` | Currently stubbed: `l_false: lua_pushboolean(L, 0)`. Needs native engine wiring. |
| `releaseGarageControl` | Missing | `CLuaWorldDefs.cpp:132` | `ArgumentParser<ReleaseGarageControl>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `releaseObjectGangTag` | Missing | `CLuaObjectDefs.cpp:49` | `ArgumentParser<ReleaseObjectGangTag>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `removeAllGameBuildings` | Missing | `CLuaBuildingDefs.cpp:18` | `ArgumentParser<RemoveAllGameBuildings>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `removeGameWorld` | Missing | `CLuaWorldDefs.cpp:166` | `ArgumentParser<RemoveGameWorld>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `removeObjectMaterial` | Missing | `CLuaObjectDefs.cpp:44` | `RemoveObjectMaterial` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `removeWorldModel` | *Stub* | `CLuaWorldDefs.cpp:163` | `RemoveWorldBuilding` | Currently stubbed: `DefaultValue(kTrue)`. Needs native engine wiring. |
| `resetAmbientPedPopulationModels` | *Stub* | `CLuaWorldDefs.cpp:103` | `ArgumentParser<ResetAmbientPedPopulationModels>` | Currently stubbed: `l_true: lua_pushboolean(L, 1)`. Needs native engine wiring. |
| `resetAmbientPedPopulationZonesToBootstrap` | *Stub* | `CLuaWorldDefs.cpp:104` | `ResetAmbientPedPopulationZonesToBootstrap` | Currently stubbed: `l_true: lua_pushboolean(L, 1)`. Needs native engine wiring. |
| `resetBlurLevel` | Missing | `CLuaWorldDefs.cpp:192` | `ResetBlurLevel` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `resetColorFilter` | Missing | `CLuaWorldDefs.cpp:178` | `ArgumentParser<ResetColorFilter>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `resetCoronaReflectionsEnabled` | Missing | `CLuaWorldDefs.cpp:179` | `ArgumentParser<ResetCoronaReflectionsEnabled>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `resetDynamicPedShadows` | Missing | `CLuaWorldDefs.cpp:197` | `ArgumentParser<ResetDynamicPedShadows>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `resetFarClipDistance` | Missing | `CLuaWorldDefs.cpp:184` | `ResetFarClipDistance` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `resetFogDistance` | Missing | `CLuaWorldDefs.cpp:188` | `ResetFogDistance` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `resetHeatHaze` | Missing | `CLuaWorldDefs.cpp:181` | `ResetHeatHaze` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `resetMoonSize` | Missing | `CLuaWorldDefs.cpp:191` | `ResetMoonSize` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `resetNearClipDistance` | Missing | `CLuaWorldDefs.cpp:185` | `ResetNearClipDistance` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `resetOcclusionRegion` | Missing | `CLuaWorldDefs.cpp:154` | `ResetOcclusionRegion` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `resetPedsLODDistance` | Missing | `CLuaWorldDefs.cpp:187` | `ResetPedsLODDistance` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `resetRainLevel` | Missing | `CLuaWorldDefs.cpp:183` | `ResetRainLevel` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `resetSkyGradient` | Missing | `CLuaWorldDefs.cpp:180` | `ResetSkyGradient` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `resetSunColor` | Missing | `CLuaWorldDefs.cpp:189` | `ResetSunColor` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `resetSunSize` | Missing | `CLuaWorldDefs.cpp:190` | `ResetSunSize` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `resetTimeFrozen` | Missing | `CLuaWorldDefs.cpp:194` | `ArgumentParser<ResetTimeFrozen>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `resetVehiclesLODDistance` | Missing | `CLuaWorldDefs.cpp:186` | `ResetVehiclesLODDistance` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `resetVolumetricShadows` | Missing | `CLuaWorldDefs.cpp:195` | `ArgumentParser<ResetVolumetricShadows>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `resetWaterColor` | Missing | `CLuaWaterDefs.cpp:20` | `ResetWaterColor` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `resetWaterLevel` | Missing | `CLuaWaterDefs.cpp:21` | `ResetWaterLevel` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `resetWindVelocity` | Missing | `CLuaWorldDefs.cpp:182` | `ResetWindVelocity` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `resetWorldProperties` | Missing | `CLuaWorldDefs.cpp:196` | `ArgumentParser<ResetWorldProperties>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `resetWorldProperty` | Missing | `CLuaWorldDefs.cpp:193` | `ArgumentParserWarn<false, ResetWorldProperty>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `respawnObject` | Missing | `CLuaObjectDefs.cpp:39` | `RespawnObject` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `restoreAllGameBuildings` | Missing | `CLuaBuildingDefs.cpp:19` | `ArgumentParser<RestoreGameBuildings>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `restoreAllWorldModels` | Missing | `CLuaWorldDefs.cpp:164` | `RestoreWorldBuildings` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `restoreGameWorld` | Missing | `CLuaWorldDefs.cpp:167` | `ArgumentParser<RestoreGameWorld>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `restoreWorldModel` | *Stub* | `CLuaWorldDefs.cpp:165` | `RestoreWorldBuilding` | Currently stubbed: `DefaultValue(kTrue)`. Needs native engine wiring. |
| `setAircraftMaxHeight` | Missing | `CLuaWorldDefs.cpp:150` | `SetAircraftMaxHeight` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setAircraftMaxVelocity` | Missing | `CLuaWorldDefs.cpp:151` | `SetAircraftMaxVelocity` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setAmbientPedPopulationZoneState` | *Stub* | `CLuaWorldDefs.cpp:105` | `SetAmbientPedPopulationZoneState` | Currently stubbed: `l_true: lua_pushboolean(L, 1)`. Needs native engine wiring. |
| `setBirdsEnabled` | Missing | `CLuaWorldDefs.cpp:155` | `SetBirdsEnabled` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setBlurLevel` | Missing | `CLuaWorldDefs.cpp:134` | `SetBlurLevel` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setCloudsEnabled` | Missing | `CLuaWorldDefs.cpp:136` | `SetCloudsEnabled` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setColorFilter` | Missing | `CLuaWorldDefs.cpp:119` | `ArgumentParser<SetColorFilter>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setCoronaReflectionsEnabled` | Missing | `CLuaWorldDefs.cpp:159` | `ArgumentParser<SetCoronaReflectionsEnabled>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setDynamicPedShadowsEnabled` | Missing | `CLuaWorldDefs.cpp:171` | `ArgumentParser<SetDynamicPedShadowsEnabled>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setEffectDensity` | Missing | `CLuaEffectDefs.cpp:35` | `SetEffectDensity` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setEffectSpeed` | Missing | `CLuaEffectDefs.cpp:33` | `SetEffectSpeed` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setFPSLimit` | Missing | `CLuaWorldDefs.cpp:158` | `SetFPSLimit` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setFarClipDistance` | Missing | `CLuaWorldDefs.cpp:143` | `SetFarClipDistance` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setFireDamageEnabled` | Missing | `CLuaFireDefs.cpp:158` | `SetFireDamageEnabled` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setFireDamageTargets` | Missing | `CLuaFireDefs.cpp:160` | `SetFireDamageTargets` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setFireDuration` | Missing | `CLuaFireDefs.cpp:152` | `SetFireDuration` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setFireMaxGenerations` | Missing | `CLuaFireDefs.cpp:164` | `SetFireMaxGenerations` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setFireRemainingTime` | Missing | `CLuaFireDefs.cpp:154` | `SetFireRemainingTime` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setFireSource` | Missing | `CLuaFireDefs.cpp:166` | `SetFireSource` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setFireSpreadEnabled` | Missing | `CLuaFireDefs.cpp:162` | `SetFireSpreadEnabled` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setFireStrength` | Missing | `CLuaFireDefs.cpp:156` | `SetFireStrength` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setFireTarget` | Missing | `CLuaFireDefs.cpp:168` | `SetFireTarget` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setFogDistance` | Missing | `CLuaWorldDefs.cpp:147` | `SetFogDistance` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setFoliageDensity` | Missing | `CLuaFoliageDefs.cpp:45` | `SetFoliageDensity` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setFoliageSurface` | Missing | `CLuaFoliageDefs.cpp:43` | `SetFoliageSurface` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setFoliageVertices` | Missing | `CLuaFoliageDefs.cpp:44` | `SetFoliageVertices` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setGameSpeed` | Missing | `CLuaWorldDefs.cpp:127` | `SetGameSpeed` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setGarageOpen` | Missing | `CLuaWorldDefs.cpp:130` | `SetGarageOpen` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setGrainLevel` | Missing | `CLuaWorldDefs.cpp:121` | `ArgumentParser<SetGrainLevel>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setGrainMultiplier` | Missing | `CLuaWorldDefs.cpp:120` | `ArgumentParser<SetGrainMultiplier>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setGravity` | Missing | `CLuaWorldDefs.cpp:126` | `SetGravity` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setHeatHaze` | Missing | `CLuaWorldDefs.cpp:123` | `SetHeatHaze` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setInteriorFurnitureEnabled` | Missing | `CLuaWorldDefs.cpp:141` | `SetInteriorFurnitureEnabled` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setInteriorSoundsEnabled` | Missing | `CLuaWorldDefs.cpp:140` | `SetInteriorSoundsEnabled` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setJetpackMaxHeight` | Missing | `CLuaWorldDefs.cpp:135` | `SetJetpackMaxHeight` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setMinuteDuration` | Missing | `CLuaWorldDefs.cpp:129` | `SetMinuteDuration` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setMoonSize` | Missing | `CLuaWorldDefs.cpp:157` | `SetMoonSize` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setNearClipDistance` | Missing | `CLuaWorldDefs.cpp:144` | `SetNearClipDistance` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setObjectBreakable` | Missing | `CLuaObjectDefs.cpp:37` | `SetObjectBreakable` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setObjectGangTagAlpha` | Missing | `CLuaObjectDefs.cpp:45` | `ArgumentParser<SetObjectGangTagAlpha>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setObjectGangTagProgress` | Missing | `CLuaObjectDefs.cpp:47` | `ArgumentParser<SetObjectGangTagProgress>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setObjectMass` | Missing | `CLuaObjectDefs.cpp:41` | `SetObjectMass` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setObjectMaterial` | *Stub* | `CLuaObjectDefs.cpp:43` | `SetObjectMaterial` | Currently stubbed: `DefaultValue(kTrue)`. Needs native engine wiring. |
| `setObjectProperty` | Missing | `CLuaObjectDefs.cpp:42` | `SetObjectProperty` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setObjectScale` | Missing | `CLuaObjectDefs.cpp:35` | `SetObjectScale` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setObjectStatic` | Missing | `CLuaObjectDefs.cpp:36` | `SetObjectStatic` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setOcclusionRegion` | Missing | `CLuaWorldDefs.cpp:153` | `SetOcclusionRegion` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setOcclusionsEnabled` | Missing | `CLuaWorldDefs.cpp:152` | `SetOcclusionsEnabled` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setPedTargetingMarkerEnabled` | Missing | `CLuaWorldDefs.cpp:156` | `SetPedTargetingMarkerEnabled` | Needs reading/writing CPed / CPlayerPed struct fields in libGTASA.so. |
| `setPedsLODDistance` | Missing | `CLuaWorldDefs.cpp:146` | `SetPedsLODDistance` | Needs reading/writing CPed / CPlayerPed struct fields in libGTASA.so. |
| `setRainLevel` | Missing | `CLuaWorldDefs.cpp:142` | `SetRainLevel` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setSkyGradient` | Missing | `CLuaWorldDefs.cpp:122` | `SetSkyGradient` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setSunColor` | Missing | `CLuaWorldDefs.cpp:148` | `SetSunColor` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setSunSize` | Missing | `CLuaWorldDefs.cpp:149` | `SetSunSize` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setTime` | Missing | `CLuaWorldDefs.cpp:118` | `SetTime` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setTimeFrozen` | Missing | `CLuaWorldDefs.cpp:169` | `ArgumentParser<SetTimeFrozen>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setTrafficLightState` | Missing | `CLuaWorldDefs.cpp:137` | `SetTrafficLightState` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setTrafficLightsLocked` | Missing | `CLuaWorldDefs.cpp:138` | `SetTrafficLightsLocked` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setVehiclesLODDistance` | Missing | `CLuaWorldDefs.cpp:145` | `SetVehiclesLODDistance` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `setVolumetricShadowsEnabled` | Missing | `CLuaWorldDefs.cpp:170` | `ArgumentParser<SetVolumetricShadowsEnabled>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setWaterColor` | Missing | `CLuaWaterDefs.cpp:23` | `SetWaterColor` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setWaterDrawnLast` | Missing | `CLuaWaterDefs.cpp:26` | `SetWaterDrawnLast` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setWaterLevel` | Missing | `CLuaWaterDefs.cpp:24` | `SetWaterLevel` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setWaterVertexPosition` | Missing | `CLuaWaterDefs.cpp:25` | `SetWaterVertexPosition` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setWaveHeight` | Missing | `CLuaWorldDefs.cpp:128` | `SetWaveHeight` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setWeather` | Missing | `CLuaWorldDefs.cpp:124` | `SetWeather` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setWeatherBlended` | Missing | `CLuaWorldDefs.cpp:125` | `SetWeatherBlended` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setWindVelocity` | Missing | `CLuaWorldDefs.cpp:139` | `SetWindVelocity` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setWorldProperty` | Missing | `CLuaWorldDefs.cpp:160` | `ArgumentParser<SetWorldProperty>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setWorldSpecialPropertyEnabled` | Missing | `CLuaWorldDefs.cpp:133` | `ArgumentParser<SetWorldSpecialPropertyEnabled>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `stopObject` | Missing | `CLuaObjectDefs.cpp:34` | `StopObject` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `testLineAgainstWater` | Missing | `CLuaWaterDefs.cpp:19` | `TestLineAgainstWater` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `testSphereAgainstWorld` | Missing | `CLuaWorldDefs.cpp:208` | `ArgumentParser<TestSphereAgainstWorld>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `toggleObjectRespawn` | Missing | `CLuaObjectDefs.cpp:40` | `ToggleObjectRespawn` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `updateAmbientPedPopulationModels` | *Stub* | `CLuaWorldDefs.cpp:102` | `ArgumentParser<UpdateAmbientPedPopulationModels>` | Currently stubbed: `l_true: lua_pushboolean(L, 1)`. Needs native engine wiring. |
