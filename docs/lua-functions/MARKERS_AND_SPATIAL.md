# Markers, Blips, ColShapes & Spatial

Checkpoints, radar map blips, collision shapes (spheres, cubes, polygons), radar areas, pickups, and 3D searchlights.

## Binding Coverage

- **Total Functions:** 145
- **Nonconstant handlers:** 145 (100.0%)
- **Stubs:** 0
- **Missing:** 0

> **Technical Requirement:** Requires spatial overlap checks (AABB / SAT collision algorithms) and radar texture rendering hooks.

## Function List

| Lua Function | Status | Desktop Source Ref | Desktop Handler | Conversion Requirement / Implementation Notes |
|---|---|---|---|---|
| `addColPolygonPoint` | **Implemented** | `CLuaColShapeDefs.cpp:31` | `AddColPolygonPoint` | Operational on Android. (AddColPolygonPoint) |
| `attachElementToRope` | **Implemented** | `CLuaRopeDefs.cpp:230` | `AttachElementToRope` | Operational on Android. (AttachElementToRope) |
| `clearNativeText` | **Implemented** | `CLuaNativeUIDefs.cpp:261` | `ClearNativeText` | Operational on Android. (ClearNativeText) |
| `countPlayersInTeam` | **Implemented** | `CLuaTeamDefs.cpp:18` | `ArgumentParserWarn<false, CountPlayersInTeam>` | Operational on Android. (CountPlayersInTeam) |
| `createBird` | **Implemented** | `CLuaBirdDefs.cpp:149` | `CreateBird` | Operational on Android. (CreateBird) |
| `createBlip` | **Implemented** | `CLuaBlipDefs.cpp:141` | `ArgumentParserWarn<false, CreateBlip>` | Operational on Android. (CreateBlip) |
| `createBlipAttachedTo` | **Implemented** | `CLuaBlipDefs.cpp:142` | `ArgumentParserWarn<false, CreateBlipAttachedTo>` | Operational on Android. (CreateBlipAttachedTo) |
| `createColCircle` | **Implemented** | `CLuaColShapeDefs.cpp:17` | `CreateColCircle` | Operational on Android. (CreateColCircle) |
| `createColCuboid` | **Implemented** | `CLuaColShapeDefs.cpp:18` | `CreateColCuboid` | Operational on Android. (CreateColCuboid) |
| `createColPolygon` | **Implemented** | `CLuaColShapeDefs.cpp:21` | `CreateColPolygon` | Operational on Android. (CreateColPolygon) |
| `createColRectangle` | **Implemented** | `CLuaColShapeDefs.cpp:20` | `CreateColRectangle` | Operational on Android. (CreateColRectangle) |
| `createColSphere` | **Implemented** | `CLuaColShapeDefs.cpp:19` | `CreateColSphere` | Operational on Android. (CreateColSphere) |
| `createColTube` | **Implemented** | `CLuaColShapeDefs.cpp:22` | `CreateColTube` | Operational on Android. (CreateColTube) |
| `createLight` | **Implemented** | `CLuaPointLightDefs.cpp:19` | `CreateLight` | Operational on Android. (CreateLight) |
| `createMarker` | **Implemented** | `CLuaMarkerDefs.cpp:20` | `CreateMarker` | Operational on Android. (CreateMarker) |
| `createNativeUI` | **Implemented** | `CLuaNativeUIDefs.cpp:260` | `CreateNativeUI` | Operational on Android. (CreateNativeUI) |
| `createPickup` | **Implemented** | `CLuaPickupDefs.cpp:17` | `CreatePickup` | Operational on Android. (CreatePickup) |
| `createRadarArea` | **Implemented** | `CLuaRadarAreaDefs.cpp:18` | `CreateRadarArea` | Operational on Android. (CreateRadarArea) |
| `createRope` | **Implemented** | `CLuaRopeDefs.cpp:218` | `CreateRope` | Operational on Android. (CreateRope) |
| `createSearchLight` | **Implemented** | `CLuaSearchLightDefs.cpp:17` | `CreateSearchLight` | Operational on Android. (CreateSearchLight) |
| `destroyNativeUI` | **Implemented** | `CLuaNativeUIDefs.cpp:260` | `DestroyNativeUI` | Operational on Android. (DestroyNativeUI) |
| `detachElementFromRope` | **Implemented** | `CLuaRopeDefs.cpp:230` | `DetachElementFromRope` | Operational on Android. (DetachElementFromRope) |
| `getBirdColors` | **Implemented** | `CLuaBirdDefs.cpp:151` | `GetBirdColors` | Operational on Android. (GetBirdColors) |
| `getBirdRenderDistance` | **Implemented** | `CLuaBirdDefs.cpp:153` | `GetBirdRenderDistance` | Operational on Android. (GetBirdRenderDistance) |
| `getBirdSize` | **Implemented** | `CLuaBirdDefs.cpp:151` | `GetBirdSize` | Operational on Android. (GetBirdSize) |
| `getBirdTargetVelocity` | **Implemented** | `CLuaBirdDefs.cpp:150` | `GetBirdTargetVelocity` | Operational on Android. (GetBirdTargetVelocity) |
| `getBirdVelocity` | **Implemented** | `CLuaBirdDefs.cpp:149` | `GetBirdVelocity` | Operational on Android. (GetBirdVelocity) |
| `getBirdWingBeatTime` | **Implemented** | `CLuaBirdDefs.cpp:152` | `GetBirdWingBeatTime` | Operational on Android. (GetBirdWingBeatTime) |
| `getBlipColor` | **Implemented** | `CLuaBlipDefs.cpp:147` | `ArgumentParserWarn<false, GetBlipColor>` | Operational on Android. (GetBlipColor) |
| `getBlipIcon` | **Implemented** | `CLuaBlipDefs.cpp:145` | `ArgumentParserWarn<false, GetBlipIcon>` | Operational on Android. (GetBlipIcon) |
| `getBlipOrdering` | **Implemented** | `CLuaBlipDefs.cpp:148` | `ArgumentParserWarn<false, GetBlipOrdering>` | Operational on Android. (GetBlipOrdering) |
| `getBlipSize` | **Implemented** | `CLuaBlipDefs.cpp:146` | `ArgumentParserWarn<false, GetBlipSize>` | Operational on Android. (GetBlipSize) |
| `getBlipVisibleDistance` | **Implemented** | `CLuaBlipDefs.cpp:149` | `ArgumentParserWarn<false, GetBlipVisibleDistance>` | Operational on Android. (GetBlipVisibleDistance) |
| `getColPolygonHeight` | **Implemented** | `CLuaColShapeDefs.cpp:37` | `ArgumentParser<GetColPolygonHeight>` | Operational on Android. (GetColPolygonHeight) |
| `getColPolygonPointPosition` | **Implemented** | `CLuaColShapeDefs.cpp:29` | `GetColPolygonPointPosition` | Operational on Android. (GetColPolygonPointPosition) |
| `getColPolygonPoints` | **Implemented** | `CLuaColShapeDefs.cpp:28` | `GetColPolygonPoints` | Operational on Android. (GetColPolygonPoints) |
| `getColShapeRadius` | **Implemented** | `CLuaColShapeDefs.cpp:24` | `GetColShapeRadius` | Operational on Android. (GetColShapeRadius) |
| `getColShapeSize` | **Implemented** | `CLuaColShapeDefs.cpp:26` | `GetColShapeSize` | Operational on Android. (GetColShapeSize) |
| `getColShapeType` | **Implemented** | `CLuaColShapeDefs.cpp:35` | `GetColShapeType` | Operational on Android. (GetColShapeType) |
| `getLightColor` | **Implemented** | `CLuaPointLightDefs.cpp:20` | `GetLightColor` | Operational on Android. (GetLightColor) |
| `getLightDirection` | **Implemented** | `CLuaPointLightDefs.cpp:20` | `GetLightDirection` | Operational on Android. (GetLightDirection) |
| `getLightRadius` | **Implemented** | `CLuaPointLightDefs.cpp:19` | `GetLightRadius` | Operational on Android. (GetLightRadius) |
| `getLightType` | **Implemented** | `CLuaPointLightDefs.cpp:19` | `GetLightType` | Operational on Android. (GetLightType) |
| `getMarkerColor` | **Implemented** | `CLuaMarkerDefs.cpp:27` | `GetMarkerColor` | Operational on Android. (GetMarkerColor) |
| `getMarkerCount` | **Implemented** | `CLuaMarkerDefs.cpp:23` | `GetMarkerCount` | Operational on Android. (GetMarkerCount) |
| `getMarkerIcon` | **Implemented** | `CLuaMarkerDefs.cpp:29` | `GetMarkerIcon` | Operational on Android. (GetMarkerIcon) |
| `getMarkerLimitStats` | **Implemented** | `CLuaMarkerDefs.cpp:24` | `GetMarkerLimitStats` | Operational on Android. (GetMarkerLimitStats) |
| `getMarkerSize` | **Implemented** | `CLuaMarkerDefs.cpp:26` | `GetMarkerSize` | Operational on Android. (GetMarkerSize) |
| `getMarkerTarget` | **Implemented** | `CLuaMarkerDefs.cpp:28` | `GetMarkerTarget` | Operational on Android. (GetMarkerTarget) |
| `getMarkerTargetArrowProperties` | **Implemented** | `CLuaMarkerDefs.cpp:30` | `ArgumentParser<GetMarkerTargetArrowProperties>` | Operational on Android. (GetMarkerTargetArrowProperties) |
| `getMarkerType` | **Implemented** | `CLuaMarkerDefs.cpp:25` | `GetMarkerType` | Operational on Android. (GetMarkerType) |
| `getNativeUIState` | **Implemented** | `CLuaNativeUIDefs.cpp:260` | `GetNativeUIState` | Operational on Android. (GetNativeUIState) |
| `getPickupAmmo` | **Implemented** | `CLuaPickupDefs.cpp:19` | `GetPickupAmmo` | Operational on Android. (GetPickupAmmo) |
| `getPickupAmount` | **Implemented** | `CLuaPickupDefs.cpp:19` | `GetPickupAmount` | Operational on Android. (GetPickupAmount) |
| `getPickupType` | **Implemented** | `CLuaPickupDefs.cpp:19` | `GetPickupType` | Operational on Android. (GetPickupType) |
| `getPickupWeapon` | **Implemented** | `CLuaPickupDefs.cpp:19` | `GetPickupWeapon` | Operational on Android. (GetPickupWeapon) |
| `getPlayersInTeam` | **Implemented** | `CLuaTeamDefs.cpp:18` | `ArgumentParserWarn<false, GetPlayersInTeam>` | Operational on Android. (GetPlayersInTeam) |
| `getRadarAreaColor` | **Implemented** | `CLuaRadarAreaDefs.cpp:18` | `GetRadarAreaColor` | Operational on Android. (GetRadarAreaColor) |
| `getRadarAreaSize` | **Implemented** | `CLuaRadarAreaDefs.cpp:18` | `GetRadarAreaSize` | Operational on Android. (GetRadarAreaSize) |
| `getRopeCarriedElement` | **Implemented** | `CLuaRopeDefs.cpp:230` | `GetRopeCarriedElement` | Operational on Android. (GetRopeCarriedElement) |
| `getRopeDuration` | **Implemented** | `CLuaRopeDefs.cpp:220` | `GetRopeDuration` | Operational on Android. (GetRopeDuration) |
| `getRopeFixedNode` | **Implemented** | `CLuaRopeDefs.cpp:227` | `GetRopeFixedNode` | Operational on Android. (GetRopeFixedNode) |
| `getRopeHolder` | **Implemented** | `CLuaRopeDefs.cpp:222` | `GetRopeHolder` | Operational on Android. (GetRopeHolder) |
| `getRopeHolderOffset` | **Implemented** | `CLuaRopeDefs.cpp:223` | `GetRopeHolderOffset` | Operational on Android. (GetRopeHolderOffset) |
| `getRopeLength` | **Implemented** | `CLuaRopeDefs.cpp:226` | `GetRopeLength` | Operational on Android. (GetRopeLength) |
| `getRopePositionAt` | **Implemented** | `CLuaRopeDefs.cpp:231` | `GetRopePositionAt` | Operational on Android. (GetRopePositionAt) |
| `getRopeRemainingTime` | **Implemented** | `CLuaRopeDefs.cpp:221` | `GetRopeRemainingTime` | Operational on Android. (GetRopeRemainingTime) |
| `getRopeTopVelocity` | **Implemented** | `CLuaRopeDefs.cpp:224` | `GetRopeTopVelocity` | Operational on Android. (GetRopeTopVelocity) |
| `getRopeType` | **Implemented** | `CLuaRopeDefs.cpp:219` | `GetRopeType` | Operational on Android. (GetRopeType) |
| `getRopeWinchHeight` | **Implemented** | `CLuaRopeDefs.cpp:225` | `GetRopeWinchHeight` | Operational on Android. (GetRopeWinchHeight) |
| `getSearchLightColor` | **Implemented** | `CLuaSearchLightDefs.cpp:23` | `ArgumentParser<GetSearchLightColor>` | Operational on Android. (GetSearchLightColor) |
| `getSearchLightEndPosition` | **Implemented** | `CLuaSearchLightDefs.cpp:20` | `GetSearchLightEndPosition` | Operational on Android. (GetSearchLightEndPosition) |
| `getSearchLightEndRadius` | **Implemented** | `CLuaSearchLightDefs.cpp:22` | `GetSearchLightEndRadius` | Operational on Android. (GetSearchLightEndRadius) |
| `getSearchLightStartPosition` | **Implemented** | `CLuaSearchLightDefs.cpp:19` | `GetSearchLightStartPosition` | Operational on Android. (GetSearchLightStartPosition) |
| `getSearchLightStartRadius` | **Implemented** | `CLuaSearchLightDefs.cpp:21` | `GetSearchLightStartRadius` | Operational on Android. (GetSearchLightStartRadius) |
| `getTeamColor` | **Implemented** | `CLuaTeamDefs.cpp:17` | `ArgumentParserWarn<false, GetTeamColor>` | Operational on Android. (GetTeamColor) |
| `getTeamFriendlyFire` | **Implemented** | `CLuaTeamDefs.cpp:17` | `ArgumentParserWarn<false, GetTeamFriendlyFire>` | Operational on Android. (GetTeamFriendlyFire) |
| `getTeamFromName` | **Implemented** | `CLuaTeamDefs.cpp:16` | `ArgumentParserWarn<false, GetTeamFromName>` | Operational on Android. (GetTeamFromName) |
| `getTeamName` | **Implemented** | `CLuaTeamDefs.cpp:16` | `ArgumentParserWarn<false, GetTeamName>` | Operational on Android. (GetTeamName) |
| `isBirdCurvedFlightEnabled` | **Implemented** | `CLuaBirdDefs.cpp:154` | `IsBirdCurvedFlightEnabled` | Operational on Android. (IsBirdCurvedFlightEnabled) |
| `isBirdMovementEnabled` | **Implemented** | `CLuaBirdDefs.cpp:156` | `IsBirdMovementEnabled` | Operational on Android. (IsBirdMovementEnabled) |
| `isBirdShootable` | **Implemented** | `CLuaBirdDefs.cpp:155` | `IsBirdShootable` | Operational on Android. (IsBirdShootable) |
| `isCoronaReflectionEnabled` | **Implemented** | `CLuaMarkerDefs.cpp:40` | `ArgumentParser<IsCoronaReflectionEnabled>` | Operational on Android. (IsCoronaReflectionEnabled) |
| `isInsideColShape` | **Implemented** | `CLuaColShapeDefs.cpp:34` | `IsInsideColShape` | Operational on Android. (IsInsideColShape) |
| `isInsideRadarArea` | **Implemented** | `CLuaRadarAreaDefs.cpp:20` | `IsInsideRadarArea` | Operational on Android. (IsInsideRadarArea) |
| `isRadarAreaFlashing` | **Implemented** | `CLuaRadarAreaDefs.cpp:19` | `IsRadarAreaFlashing` | Operational on Android. (IsRadarAreaFlashing) |
| `isRopeActive` | **Implemented** | `CLuaRopeDefs.cpp:231` | `IsRopeActive` | Operational on Android. (IsRopeActive) |
| `isRopeOnGround` | **Implemented** | `CLuaRopeDefs.cpp:228` | `IsRopeOnGround` | Operational on Android. (IsRopeOnGround) |
| `isRopePhysicsEnabled` | **Implemented** | `CLuaRopeDefs.cpp:229` | `IsRopePhysicsEnabled` | Operational on Android. (IsRopePhysicsEnabled) |
| `isShowCollisionsEnabled` | **Implemented** | `CLuaColShapeDefs.cpp:40` | `ArgumentParser<IsShowCollisionsEnabled>` | Operational on Android. (IsShowCollisionsEnabled) |
| `processBirdGunShot` | **Implemented** | `CLuaBirdDefs.cpp:157` | `ProcessBirdGunShot` | Operational on Android. (ProcessBirdGunShot) |
| `releaseNativeUI` | **Implemented** | `CLuaNativeUIDefs.cpp:261` | `ReleaseNativeUI` | Operational on Android. (ReleaseNativeUI) |
| `removeColPolygonPoint` | **Implemented** | `CLuaColShapeDefs.cpp:32` | `RemoveColPolygonPoint` | Operational on Android. (RemoveColPolygonPoint) |
| `renderScriptImportantArea` | **Implemented** | `CLuaMarkerDefs.cpp:21` | `ArgumentParser<RenderScriptImportantArea>` | Operational on Android. (RenderScriptImportantArea) |
| `setBirdColors` | **Implemented** | `CLuaBirdDefs.cpp:151` | `SetBirdColors` | Operational on Android. (SetBirdColors) |
| `setBirdCurvedFlightEnabled` | **Implemented** | `CLuaBirdDefs.cpp:154` | `SetBirdCurvedFlightEnabled` | Operational on Android. (SetBirdCurvedFlightEnabled) |
| `setBirdMovementEnabled` | **Implemented** | `CLuaBirdDefs.cpp:156` | `SetBirdMovementEnabled` | Operational on Android. (SetBirdMovementEnabled) |
| `setBirdRenderDistance` | **Implemented** | `CLuaBirdDefs.cpp:153` | `SetBirdRenderDistance` | Operational on Android. (SetBirdRenderDistance) |
| `setBirdShootable` | **Implemented** | `CLuaBirdDefs.cpp:155` | `SetBirdShootable` | Operational on Android. (SetBirdShootable) |
| `setBirdSize` | **Implemented** | `CLuaBirdDefs.cpp:151` | `SetBirdSize` | Operational on Android. (SetBirdSize) |
| `setBirdTargetVelocity` | **Implemented** | `CLuaBirdDefs.cpp:150` | `SetBirdTargetVelocity` | Operational on Android. (SetBirdTargetVelocity) |
| `setBirdVelocity` | **Implemented** | `CLuaBirdDefs.cpp:149` | `SetBirdVelocity` | Operational on Android. (SetBirdVelocity) |
| `setBirdWingBeatTime` | **Implemented** | `CLuaBirdDefs.cpp:152` | `SetBirdWingBeatTime` | Operational on Android. (SetBirdWingBeatTime) |
| `setBlipColor` | **Implemented** | `CLuaBlipDefs.cpp:154` | `ArgumentParserWarn<false, SetBlipColor>` | Operational on Android. (SetBlipColor) |
| `setBlipIcon` | **Implemented** | `CLuaBlipDefs.cpp:152` | `ArgumentParserWarn<false, SetBlipIcon>` | Operational on Android. (SetBlipIcon) |
| `setBlipOrdering` | **Implemented** | `CLuaBlipDefs.cpp:155` | `ArgumentParserWarn<false, SetBlipOrdering>` | Operational on Android. (SetBlipOrdering) |
| `setBlipSize` | **Implemented** | `CLuaBlipDefs.cpp:153` | `ArgumentParserWarn<false, SetBlipSize>` | Operational on Android. (SetBlipSize) |
| `setBlipVisibleDistance` | **Implemented** | `CLuaBlipDefs.cpp:156` | `ArgumentParserWarn<false, SetBlipVisibleDistance>` | Operational on Android. (SetBlipVisibleDistance) |
| `setColPolygonHeight` | **Implemented** | `CLuaColShapeDefs.cpp:36` | `ArgumentParser<SetColPolygonHeight>` | Operational on Android. (SetColPolygonHeight) |
| `setColPolygonPointPosition` | **Implemented** | `CLuaColShapeDefs.cpp:30` | `SetColPolygonPointPosition` | Operational on Android. (SetColPolygonPointPosition) |
| `setColShapeRadius` | **Implemented** | `CLuaColShapeDefs.cpp:25` | `SetColShapeRadius` | Operational on Android. (SetColShapeRadius) |
| `setColShapeSize` | **Implemented** | `CLuaColShapeDefs.cpp:27` | `SetColShapeSize` | Operational on Android. (SetColShapeSize) |
| `setCoronaReflectionEnabled` | **Implemented** | `CLuaMarkerDefs.cpp:39` | `ArgumentParser<SetCoronaReflectionEnabled>` | Operational on Android. (SetCoronaReflectionEnabled) |
| `setLightColor` | **Implemented** | `CLuaPointLightDefs.cpp:20` | `SetLightColor` | Operational on Android. (SetLightColor) |
| `setLightDirection` | **Implemented** | `CLuaPointLightDefs.cpp:20` | `SetLightDirection` | Operational on Android. (SetLightDirection) |
| `setLightRadius` | **Implemented** | `CLuaPointLightDefs.cpp:19` | `SetLightRadius` | Operational on Android. (SetLightRadius) |
| `setMarkerColor` | **Implemented** | `CLuaMarkerDefs.cpp:34` | `SetMarkerColor` | Operational on Android. (SetMarkerColor) |
| `setMarkerIcon` | **Implemented** | `CLuaMarkerDefs.cpp:36` | `SetMarkerIcon` | Operational on Android. (SetMarkerIcon) |
| `setMarkerSize` | **Implemented** | `CLuaMarkerDefs.cpp:33` | `SetMarkerSize` | Operational on Android. (SetMarkerSize) |
| `setMarkerTarget` | **Implemented** | `CLuaMarkerDefs.cpp:35` | `SetMarkerTarget` | Operational on Android. (SetMarkerTarget) |
| `setMarkerTargetArrowProperties` | **Implemented** | `CLuaMarkerDefs.cpp:37` | `ArgumentParser<SetMarkerTargetArrowProperties>` | Operational on Android. (SetMarkerTargetArrowProperties) |
| `setMarkerType` | **Implemented** | `CLuaMarkerDefs.cpp:32` | `SetMarkerType` | Operational on Android. (SetMarkerType) |
| `setPickupType` | **Implemented** | `CLuaPickupDefs.cpp:21` | `SetPickupType` | Operational on Android. (SetPickupType) |
| `setRadarAreaColor` | **Implemented** | `CLuaRadarAreaDefs.cpp:19` | `SetRadarAreaColor` | Operational on Android. (SetRadarAreaColor) |
| `setRadarAreaFlashing` | **Implemented** | `CLuaRadarAreaDefs.cpp:19` | `SetRadarAreaFlashing` | Operational on Android. (SetRadarAreaFlashing) |
| `setRadarAreaSize` | **Implemented** | `CLuaRadarAreaDefs.cpp:20` | `SetRadarAreaSize` | Operational on Android. (SetRadarAreaSize) |
| `setRopeDuration` | **Implemented** | `CLuaRopeDefs.cpp:220` | `SetRopeDuration` | Operational on Android. (SetRopeDuration) |
| `setRopeFixedNode` | **Implemented** | `CLuaRopeDefs.cpp:227` | `SetRopeFixedNode` | Operational on Android. (SetRopeFixedNode) |
| `setRopeHolder` | **Implemented** | `CLuaRopeDefs.cpp:222` | `SetRopeHolder` | Operational on Android. (SetRopeHolder) |
| `setRopeHolderOffset` | **Implemented** | `CLuaRopeDefs.cpp:223` | `SetRopeHolderOffset` | Operational on Android. (SetRopeHolderOffset) |
| `setRopeLength` | **Implemented** | `CLuaRopeDefs.cpp:226` | `SetRopeLength` | Operational on Android. (SetRopeLength) |
| `setRopeOnGround` | **Implemented** | `CLuaRopeDefs.cpp:228` | `SetRopeOnGround` | Operational on Android. (SetRopeOnGround) |
| `setRopePhysicsEnabled` | **Implemented** | `CLuaRopeDefs.cpp:229` | `SetRopePhysicsEnabled` | Operational on Android. (SetRopePhysicsEnabled) |
| `setRopeRemainingTime` | **Implemented** | `CLuaRopeDefs.cpp:221` | `SetRopeRemainingTime` | Operational on Android. (SetRopeRemainingTime) |
| `setRopeTopVelocity` | **Implemented** | `CLuaRopeDefs.cpp:224` | `SetRopeTopVelocity` | Operational on Android. (SetRopeTopVelocity) |
| `setRopeType` | **Implemented** | `CLuaRopeDefs.cpp:219` | `SetRopeType` | Operational on Android. (SetRopeType) |
| `setRopeWinchHeight` | **Implemented** | `CLuaRopeDefs.cpp:225` | `SetRopeWinchHeight` | Operational on Android. (SetRopeWinchHeight) |
| `setSearchLightColor` | **Implemented** | `CLuaSearchLightDefs.cpp:29` | `ArgumentParser<SetSearchLightColor>` | Operational on Android. (SetSearchLightColor) |
| `setSearchLightEndPosition` | **Implemented** | `CLuaSearchLightDefs.cpp:26` | `SetSearchLightEndPosition` | Operational on Android. (SetSearchLightEndPosition) |
| `setSearchLightEndRadius` | **Implemented** | `CLuaSearchLightDefs.cpp:28` | `SetSearchLightEndRadius` | Operational on Android. (SetSearchLightEndRadius) |
| `setSearchLightStartPosition` | **Implemented** | `CLuaSearchLightDefs.cpp:25` | `SetSearchLightStartPosition` | Operational on Android. (SetSearchLightStartPosition) |
| `setSearchLightStartRadius` | **Implemented** | `CLuaSearchLightDefs.cpp:27` | `SetSearchLightStartRadius` | Operational on Android. (SetSearchLightStartRadius) |
| `showCol` | **Implemented** | `CLuaColShapeDefs.cpp:39` | `ArgumentParser<SetShowCollision>` | Operational on Android. (ShowCol) |
| `showNativeText` | **Implemented** | `CLuaNativeUIDefs.cpp:261` | `ShowNativeText` | Operational on Android. (ShowNativeText) |
| `updateNativeUI` | **Implemented** | `CLuaNativeUIDefs.cpp:260` | `UpdateNativeUI` | Operational on Android. (UpdateNativeUI) |
