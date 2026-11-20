# World, Objects & Environment

3D objects, world weather, game time, water levels, gravity, minute duration, heat haze, explosions, fires, garage doors, searchlights, and foliage.

## Binding Coverage

- **Total Functions:** 227
- **Nonconstant handlers:** 227 (100.0%)
- **Stubs:** 0
- **Missing:** 0

> **Technical Requirement:** Weather, clock, and clip distances are stored. Objects, water, fire, effects, zones, buildings, and sight queries follow the desktop argument checks. Ambient population calls return the desktop failure while zone lookup, popcycle, model info, and the camera are not hooked. isWorldRPGMobilePopulation keeps the phone polyfills until those hooks succeed.

## Function List

| Lua Function | Status | Desktop Source Ref | Desktop Handler | Conversion Requirement / Implementation Notes |
|---|---|---|---|---|
| `acquireGarageControl` | **Implemented** | `CLuaWorldDefs.cpp:131` | `ArgumentParser<AcquireGarageControl>` | Operational on Android. (AcquireGarageControl) |
| `acquireObjectGangTag` | **Implemented** | `CLuaObjectDefs.cpp:46` | `ArgumentParser<AcquireObjectGangTag>` | Follows CLuaObjectDefs.cpp and the object writes in CStaticFunctionDefinitions.cpp. createObject needs a resource and a stock object model; peds, vehicles, clothes, and the cutscene range are rejected. The element stores scale, mass, properties, frozen, respawn, movement, and materials. CObject is not created, so breakObject and setObjectGangTagAlpha return false and a texture replace cannot resolve. The host has no model table. Not device-verified. |
| `areTrafficLightsLocked` | **Implemented** | `CLuaWorldDefs.cpp:200` | `AreTrafficLightsLocked` | Operational on Android. (l_areTrafficLightsLocked) |
| `breakObject` | **Implemented** | `CLuaObjectDefs.cpp:38` | `BreakObject` | Follows CLuaObjectDefs.cpp and the object writes in CStaticFunctionDefinitions.cpp. createObject needs a resource and a stock object model; peds, vehicles, clothes, and the cutscene range are rejected. The element stores scale, mass, properties, frozen, respawn, movement, and materials. CObject is not created, so breakObject and setObjectGangTagAlpha return false and a texture replace cannot resolve. The host has no model table. Not device-verified. |
| `createBuilding` | **Implemented** | `CLuaBuildingDefs.cpp:17` | `ArgumentParser<CreateBuilding>` | Operational on Android. (CreateBuilding) |
| `createEffect` | **Implemented** | `CLuaEffectDefs.cpp:32` | `CreateEffect` | Operational on Android. (l_createEffect) |
| `createExplosion` | **Implemented** | `CLuaWorldDefs.cpp:175` | `CreateExplosion` | Operational on Android. (CreateExplosion) |
| `createFire` | **Implemented** | `CLuaFireDefs.cpp:149` | `CreateFire` | Operational on Android. (CreateFire) |
| `createFoliage` | **Implemented** | `CLuaFoliageDefs.cpp:42` | `CreateFoliage` | Operational on Android. (l_createFoliage) |
| `createObject` | **Implemented** | `CLuaObjectDefs.cpp:21` | `CreateObject` | Follows CLuaObjectDefs.cpp and the object writes in CStaticFunctionDefinitions.cpp. createObject needs a resource and a stock object model; peds, vehicles, clothes, and the cutscene range are rejected. The element stores scale, mass, properties, frozen, respawn, movement, and materials. CObject is not created, so breakObject and setObjectGangTagAlpha return false and a texture replace cannot resolve. The host has no model table. Not device-verified. |
| `createSWATRope` | **Implemented** | `CLuaWorldDefs.cpp:174` | `CreateSWATRope` | Operational on Android. (CreateSWATRope) |
| `createWater` | **Implemented** | `CLuaWaterDefs.cpp:18` | `CreateWater` | Operational on Android. (CreateWater) |
| `engineGetPedPathGraphRevision` | **Implemented** | `CLuaWorldDefs.cpp:60` | `EngineGetPedPathGraphRevision` | Operational on Android. (EngineGetPedPathGraphRevision) |
| `engineGetPedPathPatchRevision` | **Implemented** | `CLuaWorldDefs.cpp:59` | `EngineGetPedPathPatchRevision` | Operational on Android. (EngineGetPedPathPatchRevision) |
| `engineLoadPedPathPatch` | **Implemented** | `CLuaWorldDefs.cpp:57` | `EngineLoadPedPathPatch` | Operational on Android. (EngineLoadPedPathPatch) |
| `engineUnloadPedPathPatch` | **Implemented** | `CLuaWorldDefs.cpp:58` | `EngineUnloadPedPathPatch` | Operational on Android. (EngineUnloadPedPathPatch) |
| `extinguishFire` | **Implemented** | `CLuaFireDefs.cpp:150` | `ExtinguishFire` | Operational on Android. (ExtinguishFire) |
| `fxAddBlood` | **Implemented** | `CLuaEffectDefs.cpp:18` | `fxAddBlood` | Operational on Android. (FxAddBlood) |
| `fxAddBulletImpact` | **Implemented** | `CLuaEffectDefs.cpp:22` | `fxAddBulletImpact` | Operational on Android. (FxAddBulletImpact) |
| `fxAddBulletSplash` | **Implemented** | `CLuaEffectDefs.cpp:30` | `fxAddBulletSplash` | Operational on Android. (FxAddBulletSplash) |
| `fxAddDebris` | **Implemented** | `CLuaEffectDefs.cpp:24` | `fxAddDebris` | Operational on Android. (FxAddDebris) |
| `fxAddFootSplash` | **Implemented** | `CLuaEffectDefs.cpp:31` | `fxAddFootSplash` | Operational on Android. (FxAddFootSplash) |
| `fxAddGlass` | **Implemented** | `CLuaEffectDefs.cpp:25` | `fxAddGlass` | Operational on Android. (FxAddGlass) |
| `fxAddGunshot` | **Implemented** | `CLuaEffectDefs.cpp:27` | `fxAddGunshot` | Operational on Android. (FxAddGunshot) |
| `fxAddPunchImpact` | **Implemented** | `CLuaEffectDefs.cpp:23` | `fxAddPunchImpact` | Operational on Android. (FxAddPunchImpact) |
| `fxAddSparks` | **Implemented** | `CLuaEffectDefs.cpp:20` | `fxAddSparks` | Operational on Android. (FxAddSparks) |
| `fxAddTankFire` | **Implemented** | `CLuaEffectDefs.cpp:28` | `fxAddTankFire` | Operational on Android. (FxAddTankFire) |
| `fxAddTyreBurst` | **Implemented** | `CLuaEffectDefs.cpp:21` | `fxAddTyreBurst` | Operational on Android. (FxAddTyreBurst) |
| `fxAddWaterHydrant` | **Implemented** | `CLuaEffectDefs.cpp:26` | `fxAddWaterHydrant` | Operational on Android. (FxAddWaterHydrant) |
| `fxAddWaterSplash` | **Implemented** | `CLuaEffectDefs.cpp:29` | `fxAddWaterSplash` | Operational on Android. (FxAddWaterSplash) |
| `fxAddWood` | **Implemented** | `CLuaEffectDefs.cpp:19` | `fxAddWood` | Operational on Android. (FxAddWood) |
| `fxCreateParticle` | **Implemented** | `CLuaEffectDefs.cpp:37` | `ArgumentParser<FxCreateParticle>` | Operational on Android. (FxCreateParticle) |
| `getAircraftMaxHeight` | **Implemented** | `CLuaWorldDefs.cpp:94` | `GetAircraftMaxHeight` | Operational on Android. (l_getAircraftMaxHeight) |
| `getAircraftMaxVelocity` | **Implemented** | `CLuaWorldDefs.cpp:95` | `GetAircraftMaxVelocity` | Operational on Android. (l_getAircraftMaxVelocity) |
| `getAmbientPedCivilianCoupleCandidate` | **Implemented** | `CLuaWorldDefs.cpp:110` | `GetAmbientPedCivilianCoupleCandidate` | Operational on Android. (GetAmbientPedCivilianCoupleCandidate) |
| `getAmbientPedGangGroupCandidate` | **Implemented** | `CLuaWorldDefs.cpp:109` | `GetAmbientPedGangGroupCandidate` | Operational on Android. (GetAmbientPedGangGroupCandidate) |
| `getAmbientPedPopulationProfile` | **Implemented** | `CLuaWorldDefs.cpp:106` | `GetAmbientPedPopulationProfile` | Operational on Android. (GetAmbientPedPopulationProfile) |
| `getAmbientPedSpawnCandidate` | **Implemented** | `CLuaWorldDefs.cpp:108` | `GetAmbientPedSpawnCandidate` | Operational on Android. (GetAmbientPedSpawnCandidate) |
| `getAmbientVehicleModelCandidate` | **Implemented** | `CLuaWorldDefs.cpp:112` | `GetAmbientVehicleModelCandidate` | Operational on Android. (GetAmbientVehicleModelCandidate) |
| `getAmbientVehicleOccupantModelCandidate` | **Implemented** | `CLuaWorldDefs.cpp:113` | `GetAmbientVehicleOccupantModelCandidate` | Operational on Android. (GetAmbientVehicleOccupantModelCandidate) |
| `getAmbientVehicleSpawnCandidate` | **Implemented** | `CLuaWorldDefs.cpp:111` | `GetAmbientVehicleSpawnCandidate` | Operational on Android. (GetAmbientVehicleSpawnCandidate) |
| `getBirdsEnabled` | **Implemented** | `CLuaWorldDefs.cpp:101` | `GetBirdsEnabled` | Operational on Android. (l_getBirdsEnabled) |
| `getBlurLevel` | **Implemented** | `CLuaWorldDefs.cpp:79` | `GetBlurLevel` | Operational on Android. (l_getBlurLevel) |
| `getCloudsEnabled` | **Implemented** | `CLuaWorldDefs.cpp:97` | `GetCloudsEnabled` | Operational on Android. (l_getCloudsEnabled) |
| `getColorFilter` | **Implemented** | `CLuaWorldDefs.cpp:63` | `ArgumentParser<GetColorFilter>` | Operational on Android. (l_getColorFilter) |
| `getCoronaReflectionsEnabled` | **Implemented** | `CLuaWorldDefs.cpp:114` | `ArgumentParser<GetCoronaReflectionsEnabled>` | Operational on Android. (l_getCoronaReflectionsEnabled) |
| `getEffectDensity` | **Implemented** | `CLuaEffectDefs.cpp:36` | `GetEffectDensity` | Operational on Android. (l_getEffectDensity) |
| `getEffectSpeed` | **Implemented** | `CLuaEffectDefs.cpp:34` | `GetEffectSpeed` | Operational on Android. (l_getEffectSpeed) |
| `getFPSLimit` | **Implemented** | `CLuaWorldDefs.cpp:100` | `GetFPSLimit` | Operational on Android. (l_getFPSLimit) |
| `getFarClipDistance` | **Implemented** | `CLuaWorldDefs.cpp:87` | `GetFarClipDistance` | Operational on Android. (l_getFarClipDistance) |
| `getFireDamageTargets` | **Implemented** | `CLuaFireDefs.cpp:159` | `GetFireDamageTargets` | Operational on Android. (GetFireDamageTargets) |
| `getFireDuration` | **Implemented** | `CLuaFireDefs.cpp:151` | `GetFireDuration` | Operational on Android. (GetFireDuration) |
| `getFireMaxGenerations` | **Implemented** | `CLuaFireDefs.cpp:163` | `GetFireMaxGenerations` | Operational on Android. (GetFireMaxGenerations) |
| `getFireRemainingTime` | **Implemented** | `CLuaFireDefs.cpp:153` | `GetFireRemainingTime` | Operational on Android. (GetFireRemainingTime) |
| `getFireSource` | **Implemented** | `CLuaFireDefs.cpp:165` | `GetFireSource` | Operational on Android. (GetFireSource) |
| `getFireStrength` | **Implemented** | `CLuaFireDefs.cpp:155` | `GetFireStrength` | Operational on Android. (GetFireStrength) |
| `getFireTarget` | **Implemented** | `CLuaFireDefs.cpp:167` | `GetFireTarget` | Operational on Android. (GetFireTarget) |
| `getFogDistance` | **Implemented** | `CLuaWorldDefs.cpp:91` | `GetFogDistance` | Operational on Android. (l_getFogDistance) |
| `getFoliageDensity` | **Implemented** | `CLuaFoliageDefs.cpp:44` | `GetFoliageDensity` | Operational on Android. (l_getFoliageDensity) |
| `getFoliageSurface` | **Implemented** | `CLuaFoliageDefs.cpp:42` | `GetFoliageSurface` | Operational on Android. (l_getFoliageSurface) |
| `getFoliageVertices` | **Implemented** | `CLuaFoliageDefs.cpp:43` | `GetFoliageVertices` | Operational on Android. (l_getFoliageVertices) |
| `getGameSpeed` | **Implemented** | `CLuaWorldDefs.cpp:73` | `GetGameSpeed` | Operational on Android. (l_getGameSpeed) |
| `getGarageBoundingBox` | **Implemented** | `CLuaWorldDefs.cpp:78` | `GetGarageBoundingBox` | Operational on Android. (GetGarageBoundingBox) |
| `getGaragePosition` | **Implemented** | `CLuaWorldDefs.cpp:76` | `GetGaragePosition` | Operational on Android. (GetGaragePosition) |
| `getGarageSize` | **Implemented** | `CLuaWorldDefs.cpp:77` | `GetGarageSize` | Operational on Android. (GetGarageSize) |
| `getGravity` | **Implemented** | `CLuaWorldDefs.cpp:72` | `GetGravity` | Operational on Android. (l_getGravity) |
| `getGroundPosition` | **Implemented** | `CLuaWorldDefs.cpp:65` | `GetGroundPosition` | Operational on Android. (l_getGroundPosition) |
| `getHeatHaze` | **Implemented** | `CLuaWorldDefs.cpp:82` | `GetHeatHaze` | Operational on Android. (l_getHeatHaze) |
| `getInteriorFurnitureEnabled` | **Implemented** | `CLuaWorldDefs.cpp:86` | `GetInteriorFurnitureEnabled` | Operational on Android. (l_getInteriorFurnitureEnabled) |
| `getInteriorSoundsEnabled` | **Implemented** | `CLuaWorldDefs.cpp:85` | `GetInteriorSoundsEnabled` | Operational on Android. (l_getInteriorSoundsEnabled) |
| `getJetpackMaxHeight` | **Implemented** | `CLuaWorldDefs.cpp:83` | `GetJetpackMaxHeight` | Operational on Android. (l_getJetpackMaxHeight) |
| `getMinuteDuration` | **Implemented** | `CLuaWorldDefs.cpp:74` | `GetMinuteDuration` | Operational on Android. (l_getMinuteDuration) |
| `getMoonSize` | **Implemented** | `CLuaWorldDefs.cpp:99` | `GetMoonSize` | Operational on Android. (l_getMoonSize) |
| `getNearClipDistance` | **Implemented** | `CLuaWorldDefs.cpp:88` | `GetNearClipDistance` | Operational on Android. (l_getNearClipDistance) |
| `getObjectGangTagProgress` | **Implemented** | `CLuaObjectDefs.cpp:48` | `ArgumentParser<GetObjectGangTagProgress>` | Follows CLuaObjectDefs.cpp and the object writes in CStaticFunctionDefinitions.cpp. createObject needs a resource and a stock object model; peds, vehicles, clothes, and the cutscene range are rejected. The element stores scale, mass, properties, frozen, respawn, movement, and materials. CObject is not created, so breakObject and setObjectGangTagAlpha return false and a texture replace cannot resolve. The host has no model table. Not device-verified. |
| `getObjectMass` | **Implemented** | `CLuaObjectDefs.cpp:27` | `GetObjectMass` | Follows CLuaObjectDefs.cpp and the object writes in CStaticFunctionDefinitions.cpp. createObject needs a resource and a stock object model; peds, vehicles, clothes, and the cutscene range are rejected. The element stores scale, mass, properties, frozen, respawn, movement, and materials. CObject is not created, so breakObject and setObjectGangTagAlpha return false and a texture replace cannot resolve. The host has no model table. Not device-verified. |
| `getObjectProperty` | **Implemented** | `CLuaObjectDefs.cpp:28` | `GetObjectProperty` | Follows CLuaObjectDefs.cpp and the object writes in CStaticFunctionDefinitions.cpp. createObject needs a resource and a stock object model; peds, vehicles, clothes, and the cutscene range are rejected. The element stores scale, mass, properties, frozen, respawn, movement, and materials. CObject is not created, so breakObject and setObjectGangTagAlpha return false and a texture replace cannot resolve. The host has no model table. Not device-verified. |
| `getObjectScale` | **Implemented** | `CLuaObjectDefs.cpp:25` | `GetObjectScale` | Follows CLuaObjectDefs.cpp and the object writes in CStaticFunctionDefinitions.cpp. createObject needs a resource and a stock object model; peds, vehicles, clothes, and the cutscene range are rejected. The element stores scale, mass, properties, frozen, respawn, movement, and materials. CObject is not created, so breakObject and setObjectGangTagAlpha return false and a texture replace cannot resolve. The host has no model table. Not device-verified. |
| `getOcclusionsEnabled` | **Implemented** | `CLuaWorldDefs.cpp:96` | `GetOcclusionsEnabled` | Operational on Android. (l_getOcclusionsEnabled) |
| `getPedsLODDistance` | **Implemented** | `CLuaWorldDefs.cpp:90` | `GetPedsLODDistance` | Operational on Android. (l_getPedsLODDistance) |
| `getRainLevel` | **Implemented** | `CLuaWorldDefs.cpp:98` | `GetRainLevel` | Operational on Android. (l_getRainLevel) |
| `getRoofPosition` | **Implemented** | `CLuaWorldDefs.cpp:64` | `GetRoofPosition` | Operational on Android. (GetRoofPosition) |
| `getScreenFromWorldPosition` | **Implemented** | `CLuaWorldDefs.cpp:69` | `GetScreenFromWorldPosition` | Operational on Android. (GetScreenFromWorldPosition) |
| `getSkyGradient` | **Implemented** | `CLuaWorldDefs.cpp:81` | `GetSkyGradient` | Operational on Android. (l_getSkyGradient) |
| `getSunColor` | **Implemented** | `CLuaWorldDefs.cpp:92` | `GetSunColor` | Operational on Android. (l_getSunColor) |
| `getSunSize` | **Implemented** | `CLuaWorldDefs.cpp:93` | `GetSunSize` | Operational on Android. (l_getSunSize) |
| `getTime` | **Implemented** | `CLuaWorldDefs.cpp:62` | `GetTime` | Operational on Android. (l_getTime) |
| `getTrafficLightState` | **Implemented** | `CLuaWorldDefs.cpp:80` | `GetTrafficLightState` | Operational on Android. (l_getTrafficLightState) |
| `getVehiclesLODDistance` | **Implemented** | `CLuaWorldDefs.cpp:89` | `GetVehiclesLODDistance` | Operational on Android. (l_getVehiclesLODDistance) |
| `getWaterColor` | **Implemented** | `CLuaWaterDefs.cpp:28` | `GetWaterColor` | Operational on Android. (l_getWaterColor) |
| `getWaterLevel` | **Implemented** | `CLuaWaterDefs.cpp:29` | `GetWaterLevel` | Operational on Android. (GetWaterLevel) |
| `getWaterVertexPosition` | **Implemented** | `CLuaWaterDefs.cpp:30` | `GetWaterVertexPosition` | Operational on Android. (GetWaterVertexPosition) |
| `getWaveHeight` | **Implemented** | `CLuaWorldDefs.cpp:75` | `GetWaveHeight` | Operational on Android. (l_getWaveHeight) |
| `getWeather` | **Implemented** | `CLuaWorldDefs.cpp:70` | `GetWeather` | Operational on Android. (l_getWeather) |
| `getWindVelocity` | **Implemented** | `CLuaWorldDefs.cpp:84` | `GetWindVelocity` | Operational on Android. (l_getWindVelocity) |
| `getWorldFromScreenPosition` | **Implemented** | `CLuaWorldDefs.cpp:68` | `GetWorldFromScreenPosition` | Operational on Android. (GetWorldFromScreenPosition) |
| `getWorldProperty` | **Implemented** | `CLuaWorldDefs.cpp:115` | `ArgumentParser<GetWorldProperty>` | Operational on Android. (l_getWorldProperty) |
| `getZoneName` | **Implemented** | `CLuaWorldDefs.cpp:71` | `GetZoneName` | Operational on Android. (GetZoneName) |
| `isAmbientPedSphereVisible` | **Implemented** | `CLuaWorldDefs.cpp:107` | `ArgumentParser<IsAmbientPedSphereVisible>` | Operational on Android. (IsAmbientPedSphereVisible) |
| `isDynamicPedShadowsEnabled` | **Implemented** | `CLuaWorldDefs.cpp:207` | `ArgumentParser<IsDynamicPedShadowsEnabled>` | Operational on Android. (l_isDynamicPedShadowsEnabled) |
| `isFireDamageEnabled` | **Implemented** | `CLuaFireDefs.cpp:157` | `IsFireDamageEnabled` | Operational on Android. (IsFireDamageEnabled) |
| `isFireSpreadEnabled` | **Implemented** | `CLuaFireDefs.cpp:161` | `IsFireSpreadEnabled` | Operational on Android. (IsFireSpreadEnabled) |
| `isGarageOpen` | **Implemented** | `CLuaWorldDefs.cpp:204` | `IsGarageOpen` | Operational on Android. (l_isGarageOpen) |
| `isLineOfSightClear` | **Implemented** | `CLuaWorldDefs.cpp:202` | `IsLineOfSightClear` | Operational on Android. (IsLineOfSightClear) |
| `isObjectBreakable` | **Implemented** | `CLuaObjectDefs.cpp:26` | `IsObjectBreakable` | Follows CLuaObjectDefs.cpp and the object writes in CStaticFunctionDefinitions.cpp. createObject needs a resource and a stock object model; peds, vehicles, clothes, and the cutscene range are rejected. The element stores scale, mass, properties, frozen, respawn, movement, and materials. CObject is not created, so breakObject and setObjectGangTagAlpha return false and a texture replace cannot resolve. The host has no model table. Not device-verified. |
| `isObjectMoving` | **Implemented** | `CLuaObjectDefs.cpp:29` | `ArgumentParser<IsObjectMoving>` | Follows CLuaObjectDefs.cpp and the object writes in CStaticFunctionDefinitions.cpp. createObject needs a resource and a stock object model; peds, vehicles, clothes, and the cutscene range are rejected. The element stores scale, mass, properties, frozen, respawn, movement, and materials. CObject is not created, so breakObject and setObjectGangTagAlpha return false and a texture replace cannot resolve. The host has no model table. Not device-verified. |
| `isObjectRespawnable` | **Implemented** | `CLuaObjectDefs.cpp:30` | `ArgumentParser<IsObjectRespawnable>` | Follows CLuaObjectDefs.cpp and the object writes in CStaticFunctionDefinitions.cpp. createObject needs a resource and a stock object model; peds, vehicles, clothes, and the cutscene range are rejected. The element stores scale, mass, properties, frozen, respawn, movement, and materials. CObject is not created, so breakObject and setObjectGangTagAlpha return false and a texture replace cannot resolve. The host has no model table. Not device-verified. |
| `isObjectStatic` | **Implemented** | `CLuaObjectDefs.cpp:24` | `IsObjectStatic` | Follows CLuaObjectDefs.cpp and the object writes in CStaticFunctionDefinitions.cpp. createObject needs a resource and a stock object model; peds, vehicles, clothes, and the cutscene range are rejected. The element stores scale, mass, properties, frozen, respawn, movement, and materials. CObject is not created, so breakObject and setObjectGangTagAlpha return false and a texture replace cannot resolve. The host has no model table. Not device-verified. |
| `isPedTargetingMarkerEnabled` | **Implemented** | `CLuaWorldDefs.cpp:201` | `IsPedTargetingMarkerEnabled` | Operational on Android. (l_isPedTargetingMarkerEnabled) |
| `isTimeFrozen` | **Implemented** | `CLuaWorldDefs.cpp:205` | `ArgumentParser<IsTimeFrozen>` | Operational on Android. (l_isTimeFrozen) |
| `isVolumetricShadowsEnabled` | **Implemented** | `CLuaWorldDefs.cpp:206` | `ArgumentParser<IsVolumetricShadowsEnabled>` | Operational on Android. (l_isVolumetricShadowsEnabled) |
| `isWaterDrawnLast` | **Implemented** | `CLuaWaterDefs.cpp:31` | `IsWaterDrawnLast` | Operational on Android. (l_isWaterDrawnLast) |
| `isWorldSpecialPropertyEnabled` | **Implemented** | `CLuaWorldDefs.cpp:203` | `ArgumentParserWarn<false, IsWorldSpecialPropertyEnabled>` | Operational on Android. (l_isWorldSpecialPropertyEnabled) |
| `moveObject` | **Implemented** | `CLuaObjectDefs.cpp:33` | `MoveObject` | Follows CLuaObjectDefs.cpp and the object writes in CStaticFunctionDefinitions.cpp. createObject needs a resource and a stock object model; peds, vehicles, clothes, and the cutscene range are rejected. The element stores scale, mass, properties, frozen, respawn, movement, and materials. CObject is not created, so breakObject and setObjectGangTagAlpha return false and a texture replace cannot resolve. The host has no model table. Not device-verified. |
| `processLineAgainstMesh` | **Implemented** | `CLuaWorldDefs.cpp:66` | `ArgumentParser<ProcessLineAgainstMesh>` | Operational on Android. (ProcessLineAgainstMesh) |
| `processLineOfSight` | **Implemented** | `CLuaWorldDefs.cpp:67` | `ProcessLineOfSight` | Operational on Android. (ProcessLineOfSight) |
| `releaseGarageControl` | **Implemented** | `CLuaWorldDefs.cpp:132` | `ArgumentParser<ReleaseGarageControl>` | Operational on Android. (ReleaseGarageControl) |
| `releaseObjectGangTag` | **Implemented** | `CLuaObjectDefs.cpp:49` | `ArgumentParser<ReleaseObjectGangTag>` | Follows CLuaObjectDefs.cpp and the object writes in CStaticFunctionDefinitions.cpp. createObject needs a resource and a stock object model; peds, vehicles, clothes, and the cutscene range are rejected. The element stores scale, mass, properties, frozen, respawn, movement, and materials. CObject is not created, so breakObject and setObjectGangTagAlpha return false and a texture replace cannot resolve. The host has no model table. Not device-verified. |
| `removeAllGameBuildings` | **Implemented** | `CLuaBuildingDefs.cpp:18` | `ArgumentParser<RemoveAllGameBuildings>` | Operational on Android. (RemoveGameWorld) |
| `removeGameWorld` | **Implemented** | `CLuaWorldDefs.cpp:166` | `ArgumentParser<RemoveGameWorld>` | Operational on Android. (RemoveGameWorld) |
| `removeObjectMaterial` | **Implemented** | `CLuaObjectDefs.cpp:44` | `RemoveObjectMaterial` | Follows CLuaObjectDefs.cpp and the object writes in CStaticFunctionDefinitions.cpp. createObject needs a resource and a stock object model; peds, vehicles, clothes, and the cutscene range are rejected. The element stores scale, mass, properties, frozen, respawn, movement, and materials. CObject is not created, so breakObject and setObjectGangTagAlpha return false and a texture replace cannot resolve. The host has no model table. Not device-verified. |
| `removeWorldModel` | **Implemented** | `CLuaWorldDefs.cpp:163` | `RemoveWorldBuilding` | Operational on Android. (RemoveWorldModel) |
| `resetAmbientPedPopulationModels` | **Implemented** | `CLuaWorldDefs.cpp:103` | `ArgumentParser<ResetAmbientPedPopulationModels>` | Operational on Android. (ResetAmbientPedPopulationModels) |
| `resetAmbientPedPopulationZonesToBootstrap` | **Implemented** | `CLuaWorldDefs.cpp:104` | `ResetAmbientPedPopulationZonesToBootstrap` | Operational on Android. (ResetAmbientPedPopulationZonesToBootstrap) |
| `resetBlurLevel` | **Implemented** | `CLuaWorldDefs.cpp:192` | `ResetBlurLevel` | Operational on Android. (l_resetBlurLevel) |
| `resetColorFilter` | **Implemented** | `CLuaWorldDefs.cpp:178` | `ArgumentParser<ResetColorFilter>` | Operational on Android. (l_resetColorFilter) |
| `resetCoronaReflectionsEnabled` | **Implemented** | `CLuaWorldDefs.cpp:179` | `ArgumentParser<ResetCoronaReflectionsEnabled>` | Operational on Android. (l_resetCoronaReflectionsEnabled) |
| `resetDynamicPedShadows` | **Implemented** | `CLuaWorldDefs.cpp:197` | `ArgumentParser<ResetDynamicPedShadows>` | Operational on Android. (l_resetDynamicPedShadows) |
| `resetFarClipDistance` | **Implemented** | `CLuaWorldDefs.cpp:184` | `ResetFarClipDistance` | Operational on Android. (l_resetFarClipDistance) |
| `resetFogDistance` | **Implemented** | `CLuaWorldDefs.cpp:188` | `ResetFogDistance` | Operational on Android. (l_resetFogDistance) |
| `resetHeatHaze` | **Implemented** | `CLuaWorldDefs.cpp:181` | `ResetHeatHaze` | Operational on Android. (l_resetHeatHaze) |
| `resetMoonSize` | **Implemented** | `CLuaWorldDefs.cpp:191` | `ResetMoonSize` | Operational on Android. (l_resetMoonSize) |
| `resetNearClipDistance` | **Implemented** | `CLuaWorldDefs.cpp:185` | `ResetNearClipDistance` | Operational on Android. (l_resetNearClipDistance) |
| `resetOcclusionRegion` | **Implemented** | `CLuaWorldDefs.cpp:154` | `ResetOcclusionRegion` | Operational on Android. (l_resetOcclusionRegion) |
| `resetPedsLODDistance` | **Implemented** | `CLuaWorldDefs.cpp:187` | `ResetPedsLODDistance` | Operational on Android. (l_resetPedsLODDistance) |
| `resetRainLevel` | **Implemented** | `CLuaWorldDefs.cpp:183` | `ResetRainLevel` | Operational on Android. (l_resetRainLevel) |
| `resetSkyGradient` | **Implemented** | `CLuaWorldDefs.cpp:180` | `ResetSkyGradient` | Operational on Android. (l_resetSkyGradient) |
| `resetSunColor` | **Implemented** | `CLuaWorldDefs.cpp:189` | `ResetSunColor` | Operational on Android. (l_resetSunColor) |
| `resetSunSize` | **Implemented** | `CLuaWorldDefs.cpp:190` | `ResetSunSize` | Operational on Android. (l_resetSunSize) |
| `resetTimeFrozen` | **Implemented** | `CLuaWorldDefs.cpp:194` | `ArgumentParser<ResetTimeFrozen>` | Operational on Android. (l_resetTimeFrozen) |
| `resetVehiclesLODDistance` | **Implemented** | `CLuaWorldDefs.cpp:186` | `ResetVehiclesLODDistance` | Operational on Android. (l_resetVehiclesLODDistance) |
| `resetVolumetricShadows` | **Implemented** | `CLuaWorldDefs.cpp:195` | `ArgumentParser<ResetVolumetricShadows>` | Operational on Android. (l_resetVolumetricShadows) |
| `resetWaterColor` | **Implemented** | `CLuaWaterDefs.cpp:20` | `ResetWaterColor` | Operational on Android. (l_resetWaterColor) |
| `resetWaterLevel` | **Implemented** | `CLuaWaterDefs.cpp:21` | `ResetWaterLevel` | Operational on Android. (ResetWaterLevel) |
| `resetWindVelocity` | **Implemented** | `CLuaWorldDefs.cpp:182` | `ResetWindVelocity` | Operational on Android. (l_resetWindVelocity) |
| `resetWorldProperties` | **Implemented** | `CLuaWorldDefs.cpp:196` | `ArgumentParser<ResetWorldProperties>` | Operational on Android. (l_resetWorldProperties) |
| `resetWorldProperty` | **Implemented** | `CLuaWorldDefs.cpp:193` | `ArgumentParserWarn<false, ResetWorldProperty>` | Operational on Android. (l_resetWorldProperty) |
| `respawnObject` | **Implemented** | `CLuaObjectDefs.cpp:39` | `RespawnObject` | Follows CLuaObjectDefs.cpp and the object writes in CStaticFunctionDefinitions.cpp. createObject needs a resource and a stock object model; peds, vehicles, clothes, and the cutscene range are rejected. The element stores scale, mass, properties, frozen, respawn, movement, and materials. CObject is not created, so breakObject and setObjectGangTagAlpha return false and a texture replace cannot resolve. The host has no model table. Not device-verified. |
| `restoreAllGameBuildings` | **Implemented** | `CLuaBuildingDefs.cpp:19` | `ArgumentParser<RestoreGameBuildings>` | Operational on Android. (RestoreGameWorld) |
| `restoreAllWorldModels` | **Implemented** | `CLuaWorldDefs.cpp:164` | `RestoreWorldBuildings` | Operational on Android. (RestoreAllWorldModels) |
| `restoreGameWorld` | **Implemented** | `CLuaWorldDefs.cpp:167` | `ArgumentParser<RestoreGameWorld>` | Operational on Android. (RestoreGameWorld) |
| `restoreWorldModel` | **Implemented** | `CLuaWorldDefs.cpp:165` | `RestoreWorldBuilding` | Operational on Android. (RestoreWorldModel) |
| `setAircraftMaxHeight` | **Implemented** | `CLuaWorldDefs.cpp:150` | `SetAircraftMaxHeight` | Operational on Android. (l_setAircraftMaxHeight) |
| `setAircraftMaxVelocity` | **Implemented** | `CLuaWorldDefs.cpp:151` | `SetAircraftMaxVelocity` | Operational on Android. (l_setAircraftMaxVelocity) |
| `setAmbientPedPopulationZoneState` | **Implemented** | `CLuaWorldDefs.cpp:105` | `SetAmbientPedPopulationZoneState` | Operational on Android. (SetAmbientPedPopulationZoneState) |
| `setBirdsEnabled` | **Implemented** | `CLuaWorldDefs.cpp:155` | `SetBirdsEnabled` | Operational on Android. (l_setBirdsEnabled) |
| `setBlurLevel` | **Implemented** | `CLuaWorldDefs.cpp:134` | `SetBlurLevel` | Operational on Android. (l_setBlurLevel) |
| `setCloudsEnabled` | **Implemented** | `CLuaWorldDefs.cpp:136` | `SetCloudsEnabled` | Operational on Android. (l_setCloudsEnabled) |
| `setColorFilter` | **Implemented** | `CLuaWorldDefs.cpp:119` | `ArgumentParser<SetColorFilter>` | Operational on Android. (l_setColorFilter) |
| `setCoronaReflectionsEnabled` | **Implemented** | `CLuaWorldDefs.cpp:159` | `ArgumentParser<SetCoronaReflectionsEnabled>` | Operational on Android. (l_setCoronaReflectionsEnabled) |
| `setDynamicPedShadowsEnabled` | **Implemented** | `CLuaWorldDefs.cpp:171` | `ArgumentParser<SetDynamicPedShadowsEnabled>` | Operational on Android. (l_setDynamicPedShadowsEnabled) |
| `setEffectDensity` | **Implemented** | `CLuaEffectDefs.cpp:35` | `SetEffectDensity` | Operational on Android. (l_setEffectDensity) |
| `setEffectSpeed` | **Implemented** | `CLuaEffectDefs.cpp:33` | `SetEffectSpeed` | Operational on Android. (l_setEffectSpeed) |
| `setFPSLimit` | **Implemented** | `CLuaWorldDefs.cpp:158` | `SetFPSLimit` | Operational on Android. (l_setFPSLimit) |
| `setFarClipDistance` | **Implemented** | `CLuaWorldDefs.cpp:143` | `SetFarClipDistance` | Operational on Android. (l_setFarClipDistance) |
| `setFireDamageEnabled` | **Implemented** | `CLuaFireDefs.cpp:158` | `SetFireDamageEnabled` | Operational on Android. (SetFireDamageEnabled) |
| `setFireDamageTargets` | **Implemented** | `CLuaFireDefs.cpp:160` | `SetFireDamageTargets` | Operational on Android. (SetFireDamageTargets) |
| `setFireDuration` | **Implemented** | `CLuaFireDefs.cpp:152` | `SetFireDuration` | Operational on Android. (SetFireDuration) |
| `setFireMaxGenerations` | **Implemented** | `CLuaFireDefs.cpp:164` | `SetFireMaxGenerations` | Operational on Android. (SetFireMaxGenerations) |
| `setFireRemainingTime` | **Implemented** | `CLuaFireDefs.cpp:154` | `SetFireRemainingTime` | Operational on Android. (SetFireRemainingTime) |
| `setFireSource` | **Implemented** | `CLuaFireDefs.cpp:166` | `SetFireSource` | Operational on Android. (SetFireSource) |
| `setFireSpreadEnabled` | **Implemented** | `CLuaFireDefs.cpp:162` | `SetFireSpreadEnabled` | Operational on Android. (SetFireSpreadEnabled) |
| `setFireStrength` | **Implemented** | `CLuaFireDefs.cpp:156` | `SetFireStrength` | Operational on Android. (SetFireStrength) |
| `setFireTarget` | **Implemented** | `CLuaFireDefs.cpp:168` | `SetFireTarget` | Operational on Android. (SetFireTarget) |
| `setFogDistance` | **Implemented** | `CLuaWorldDefs.cpp:147` | `SetFogDistance` | Operational on Android. (l_setFogDistance) |
| `setFoliageDensity` | **Implemented** | `CLuaFoliageDefs.cpp:45` | `SetFoliageDensity` | Operational on Android. (l_setFoliageDensity) |
| `setFoliageSurface` | **Implemented** | `CLuaFoliageDefs.cpp:43` | `SetFoliageSurface` | Operational on Android. (l_setFoliageSurface) |
| `setFoliageVertices` | **Implemented** | `CLuaFoliageDefs.cpp:44` | `SetFoliageVertices` | Operational on Android. (l_setFoliageVertices) |
| `setGameSpeed` | **Implemented** | `CLuaWorldDefs.cpp:127` | `SetGameSpeed` | Operational on Android. (l_setGameSpeed) |
| `setGarageOpen` | **Implemented** | `CLuaWorldDefs.cpp:130` | `SetGarageOpen` | Operational on Android. (l_setGarageOpen) |
| `setGrainLevel` | **Implemented** | `CLuaWorldDefs.cpp:121` | `ArgumentParser<SetGrainLevel>` | Operational on Android. (l_setGrainLevel) |
| `setGrainMultiplier` | **Implemented** | `CLuaWorldDefs.cpp:120` | `ArgumentParser<SetGrainMultiplier>` | Operational on Android. (l_setGrainMultiplier) |
| `setGravity` | **Implemented** | `CLuaWorldDefs.cpp:126` | `SetGravity` | Operational on Android. (l_setGravity) |
| `setHeatHaze` | **Implemented** | `CLuaWorldDefs.cpp:123` | `SetHeatHaze` | Operational on Android. (l_setHeatHaze) |
| `setInteriorFurnitureEnabled` | **Implemented** | `CLuaWorldDefs.cpp:141` | `SetInteriorFurnitureEnabled` | Operational on Android. (l_setInteriorFurnitureEnabled) |
| `setInteriorSoundsEnabled` | **Implemented** | `CLuaWorldDefs.cpp:140` | `SetInteriorSoundsEnabled` | Operational on Android. (l_setInteriorSoundsEnabled) |
| `setJetpackMaxHeight` | **Implemented** | `CLuaWorldDefs.cpp:135` | `SetJetpackMaxHeight` | Operational on Android. (l_setJetpackMaxHeight) |
| `setMinuteDuration` | **Implemented** | `CLuaWorldDefs.cpp:129` | `SetMinuteDuration` | Operational on Android. (l_setMinuteDuration) |
| `setMoonSize` | **Implemented** | `CLuaWorldDefs.cpp:157` | `SetMoonSize` | Operational on Android. (l_setMoonSize) |
| `setNearClipDistance` | **Implemented** | `CLuaWorldDefs.cpp:144` | `SetNearClipDistance` | Operational on Android. (l_setNearClipDistance) |
| `setObjectBreakable` | **Implemented** | `CLuaObjectDefs.cpp:37` | `SetObjectBreakable` | Follows CLuaObjectDefs.cpp and the object writes in CStaticFunctionDefinitions.cpp. createObject needs a resource and a stock object model; peds, vehicles, clothes, and the cutscene range are rejected. The element stores scale, mass, properties, frozen, respawn, movement, and materials. CObject is not created, so breakObject and setObjectGangTagAlpha return false and a texture replace cannot resolve. The host has no model table. Not device-verified. |
| `setObjectGangTagAlpha` | **Implemented** | `CLuaObjectDefs.cpp:45` | `ArgumentParser<SetObjectGangTagAlpha>` | Follows CLuaObjectDefs.cpp and the object writes in CStaticFunctionDefinitions.cpp. createObject needs a resource and a stock object model; peds, vehicles, clothes, and the cutscene range are rejected. The element stores scale, mass, properties, frozen, respawn, movement, and materials. CObject is not created, so breakObject and setObjectGangTagAlpha return false and a texture replace cannot resolve. The host has no model table. Not device-verified. |
| `setObjectGangTagProgress` | **Implemented** | `CLuaObjectDefs.cpp:47` | `ArgumentParser<SetObjectGangTagProgress>` | Follows CLuaObjectDefs.cpp and the object writes in CStaticFunctionDefinitions.cpp. createObject needs a resource and a stock object model; peds, vehicles, clothes, and the cutscene range are rejected. The element stores scale, mass, properties, frozen, respawn, movement, and materials. CObject is not created, so breakObject and setObjectGangTagAlpha return false and a texture replace cannot resolve. The host has no model table. Not device-verified. |
| `setObjectMass` | **Implemented** | `CLuaObjectDefs.cpp:41` | `SetObjectMass` | Follows CLuaObjectDefs.cpp and the object writes in CStaticFunctionDefinitions.cpp. createObject needs a resource and a stock object model; peds, vehicles, clothes, and the cutscene range are rejected. The element stores scale, mass, properties, frozen, respawn, movement, and materials. CObject is not created, so breakObject and setObjectGangTagAlpha return false and a texture replace cannot resolve. The host has no model table. Not device-verified. |
| `setObjectMaterial` | **Implemented** | `CLuaObjectDefs.cpp:43` | `SetObjectMaterial` | Follows CLuaObjectDefs.cpp and the object writes in CStaticFunctionDefinitions.cpp. createObject needs a resource and a stock object model; peds, vehicles, clothes, and the cutscene range are rejected. The element stores scale, mass, properties, frozen, respawn, movement, and materials. CObject is not created, so breakObject and setObjectGangTagAlpha return false and a texture replace cannot resolve. The host has no model table. Not device-verified. |
| `setObjectProperty` | **Implemented** | `CLuaObjectDefs.cpp:42` | `SetObjectProperty` | Follows CLuaObjectDefs.cpp and the object writes in CStaticFunctionDefinitions.cpp. createObject needs a resource and a stock object model; peds, vehicles, clothes, and the cutscene range are rejected. The element stores scale, mass, properties, frozen, respawn, movement, and materials. CObject is not created, so breakObject and setObjectGangTagAlpha return false and a texture replace cannot resolve. The host has no model table. Not device-verified. |
| `setObjectScale` | **Implemented** | `CLuaObjectDefs.cpp:35` | `SetObjectScale` | Follows CLuaObjectDefs.cpp and the object writes in CStaticFunctionDefinitions.cpp. createObject needs a resource and a stock object model; peds, vehicles, clothes, and the cutscene range are rejected. The element stores scale, mass, properties, frozen, respawn, movement, and materials. CObject is not created, so breakObject and setObjectGangTagAlpha return false and a texture replace cannot resolve. The host has no model table. Not device-verified. |
| `setObjectStatic` | **Implemented** | `CLuaObjectDefs.cpp:36` | `SetObjectStatic` | Follows CLuaObjectDefs.cpp and the object writes in CStaticFunctionDefinitions.cpp. createObject needs a resource and a stock object model; peds, vehicles, clothes, and the cutscene range are rejected. The element stores scale, mass, properties, frozen, respawn, movement, and materials. CObject is not created, so breakObject and setObjectGangTagAlpha return false and a texture replace cannot resolve. The host has no model table. Not device-verified. |
| `setOcclusionRegion` | **Implemented** | `CLuaWorldDefs.cpp:153` | `SetOcclusionRegion` | Operational on Android. (l_setOcclusionRegion) |
| `setOcclusionsEnabled` | **Implemented** | `CLuaWorldDefs.cpp:152` | `SetOcclusionsEnabled` | Operational on Android. (l_setOcclusionsEnabled) |
| `setPedTargetingMarkerEnabled` | **Implemented** | `CLuaWorldDefs.cpp:156` | `SetPedTargetingMarkerEnabled` | Operational on Android. (l_setPedTargetingMarkerEnabled) |
| `setPedsLODDistance` | **Implemented** | `CLuaWorldDefs.cpp:146` | `SetPedsLODDistance` | Operational on Android. (l_setPedsLODDistance) |
| `setRainLevel` | **Implemented** | `CLuaWorldDefs.cpp:142` | `SetRainLevel` | Operational on Android. (l_setRainLevel) |
| `setSkyGradient` | **Implemented** | `CLuaWorldDefs.cpp:122` | `SetSkyGradient` | Operational on Android. (l_setSkyGradient) |
| `setSunColor` | **Implemented** | `CLuaWorldDefs.cpp:148` | `SetSunColor` | Operational on Android. (l_setSunColor) |
| `setSunSize` | **Implemented** | `CLuaWorldDefs.cpp:149` | `SetSunSize` | Operational on Android. (l_setSunSize) |
| `setTime` | **Implemented** | `CLuaWorldDefs.cpp:118` | `SetTime` | Operational on Android. (l_setTime) |
| `setTimeFrozen` | **Implemented** | `CLuaWorldDefs.cpp:169` | `ArgumentParser<SetTimeFrozen>` | Operational on Android. (l_setTimeFrozen) |
| `setTrafficLightState` | **Implemented** | `CLuaWorldDefs.cpp:137` | `SetTrafficLightState` | Operational on Android. (l_setTrafficLightState) |
| `setTrafficLightsLocked` | **Implemented** | `CLuaWorldDefs.cpp:138` | `SetTrafficLightsLocked` | Operational on Android. (l_setTrafficLightsLocked) |
| `setVehiclesLODDistance` | **Implemented** | `CLuaWorldDefs.cpp:145` | `SetVehiclesLODDistance` | Operational on Android. (l_setVehiclesLODDistance) |
| `setVolumetricShadowsEnabled` | **Implemented** | `CLuaWorldDefs.cpp:170` | `ArgumentParser<SetVolumetricShadowsEnabled>` | Operational on Android. (l_setVolumetricShadowsEnabled) |
| `setWaterColor` | **Implemented** | `CLuaWaterDefs.cpp:23` | `SetWaterColor` | Operational on Android. (l_setWaterColor) |
| `setWaterDrawnLast` | **Implemented** | `CLuaWaterDefs.cpp:26` | `SetWaterDrawnLast` | Operational on Android. (l_setWaterDrawnLast) |
| `setWaterLevel` | **Implemented** | `CLuaWaterDefs.cpp:24` | `SetWaterLevel` | Operational on Android. (SetWaterLevel) |
| `setWaterVertexPosition` | **Implemented** | `CLuaWaterDefs.cpp:25` | `SetWaterVertexPosition` | Operational on Android. (SetWaterVertexPosition) |
| `setWaveHeight` | **Implemented** | `CLuaWorldDefs.cpp:128` | `SetWaveHeight` | Operational on Android. (l_setWaveHeight) |
| `setWeather` | **Implemented** | `CLuaWorldDefs.cpp:124` | `SetWeather` | Operational on Android. (l_setWeather) |
| `setWeatherBlended` | **Implemented** | `CLuaWorldDefs.cpp:125` | `SetWeatherBlended` | Operational on Android. (l_setWeatherBlended) |
| `setWindVelocity` | **Implemented** | `CLuaWorldDefs.cpp:139` | `SetWindVelocity` | Operational on Android. (l_setWindVelocity) |
| `setWorldProperty` | **Implemented** | `CLuaWorldDefs.cpp:160` | `ArgumentParser<SetWorldProperty>` | Operational on Android. (l_setWorldProperty) |
| `setWorldSpecialPropertyEnabled` | **Implemented** | `CLuaWorldDefs.cpp:133` | `ArgumentParser<SetWorldSpecialPropertyEnabled>` | Operational on Android. (l_setWorldSpecialPropertyEnabled) |
| `stopObject` | **Implemented** | `CLuaObjectDefs.cpp:34` | `StopObject` | Follows CLuaObjectDefs.cpp and the object writes in CStaticFunctionDefinitions.cpp. createObject needs a resource and a stock object model; peds, vehicles, clothes, and the cutscene range are rejected. The element stores scale, mass, properties, frozen, respawn, movement, and materials. CObject is not created, so breakObject and setObjectGangTagAlpha return false and a texture replace cannot resolve. The host has no model table. Not device-verified. |
| `testLineAgainstWater` | **Implemented** | `CLuaWaterDefs.cpp:19` | `TestLineAgainstWater` | Operational on Android. (TestLineAgainstWater) |
| `testSphereAgainstWorld` | **Implemented** | `CLuaWorldDefs.cpp:208` | `ArgumentParser<TestSphereAgainstWorld>` | Operational on Android. (TestSphereAgainstWorld) |
| `toggleObjectRespawn` | **Implemented** | `CLuaObjectDefs.cpp:40` | `ToggleObjectRespawn` | Follows CLuaObjectDefs.cpp and the object writes in CStaticFunctionDefinitions.cpp. createObject needs a resource and a stock object model; peds, vehicles, clothes, and the cutscene range are rejected. The element stores scale, mass, properties, frozen, respawn, movement, and materials. CObject is not created, so breakObject and setObjectGangTagAlpha return false and a texture replace cannot resolve. The host has no model table. Not device-verified. |
| `updateAmbientPedPopulationModels` | **Implemented** | `CLuaWorldDefs.cpp:102` | `ArgumentParser<UpdateAmbientPedPopulationModels>` | Operational on Android. (UpdateAmbientPedPopulationModels) |
