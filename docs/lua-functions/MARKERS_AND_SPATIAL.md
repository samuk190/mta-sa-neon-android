# Markers, Blips, ColShapes & Spatial

Checkpoints, radar map blips, collision shapes (spheres, cubes, polygons), radar areas, pickups, and 3D searchlights.

## Parity Status

- **Total Functions:** 145
- **Implemented:** 0 (0.0%)
- **Stubs:** 3
- **Missing:** 142

> **Technical Requirement:** Requires spatial overlap checks (AABB / SAT collision algorithms) and radar texture rendering hooks.

## Function List

| Lua Function | Status | Desktop Source Ref | Desktop Handler | Conversion Requirement / Implementation Notes |
|---|---|---|---|---|
| `addColPolygonPoint` | Missing | `CLuaColShapeDefs.cpp:31` | `AddColPolygonPoint` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `attachElementToRope` | Missing | `CLuaRopeDefs.cpp:230` | `AttachElementToRope` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `clearNativeText` | Missing | `CLuaNativeUIDefs.cpp:261` | `ClearNativeText` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `countPlayersInTeam` | Missing | `CLuaTeamDefs.cpp:18` | `ArgumentParserWarn<false, CountPlayersInTeam>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `createBird` | Missing | `CLuaBirdDefs.cpp:149` | `CreateBird` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `createBlip` | *Stub* | `CLuaBlipDefs.cpp:141` | `ArgumentParserWarn<false, CreateBlip>` | Currently stubbed: `FalseResult: lua_pushboolean(L, 0)`. Needs native engine wiring. |
| `createBlipAttachedTo` | Missing | `CLuaBlipDefs.cpp:142` | `ArgumentParserWarn<false, CreateBlipAttachedTo>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `createColCircle` | Missing | `CLuaColShapeDefs.cpp:17` | `CreateColCircle` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `createColCuboid` | Missing | `CLuaColShapeDefs.cpp:18` | `CreateColCuboid` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `createColPolygon` | Missing | `CLuaColShapeDefs.cpp:21` | `CreateColPolygon` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `createColRectangle` | Missing | `CLuaColShapeDefs.cpp:20` | `CreateColRectangle` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `createColSphere` | *Stub* | `CLuaColShapeDefs.cpp:19` | `CreateColSphere` | Currently stubbed: `FalseResult: lua_pushboolean(L, 0)`. Needs native engine wiring. |
| `createColTube` | Missing | `CLuaColShapeDefs.cpp:22` | `CreateColTube` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `createLight` | Missing | `CLuaPointLightDefs.cpp:19` | `CreateLight` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `createMarker` | *Stub* | `CLuaMarkerDefs.cpp:20` | `CreateMarker` | Currently stubbed: `FalseResult: lua_pushboolean(L, 0)`. Needs native engine wiring. |
| `createNativeUI` | Missing | `CLuaNativeUIDefs.cpp:260` | `CreateNativeUI` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `createPickup` | Missing | `CLuaPickupDefs.cpp:17` | `CreatePickup` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `createRadarArea` | Missing | `CLuaRadarAreaDefs.cpp:18` | `CreateRadarArea` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `createRope` | Missing | `CLuaRopeDefs.cpp:218` | `CreateRope` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `createSearchLight` | Missing | `CLuaSearchLightDefs.cpp:17` | `CreateSearchLight` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `destroyNativeUI` | Missing | `CLuaNativeUIDefs.cpp:260` | `DestroyNativeUI` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `detachElementFromRope` | Missing | `CLuaRopeDefs.cpp:230` | `DetachElementFromRope` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getBirdColors` | Missing | `CLuaBirdDefs.cpp:151` | `GetBirdColors` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getBirdRenderDistance` | Missing | `CLuaBirdDefs.cpp:153` | `GetBirdRenderDistance` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getBirdSize` | Missing | `CLuaBirdDefs.cpp:151` | `GetBirdSize` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getBirdTargetVelocity` | Missing | `CLuaBirdDefs.cpp:150` | `GetBirdTargetVelocity` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getBirdVelocity` | Missing | `CLuaBirdDefs.cpp:149` | `GetBirdVelocity` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getBirdWingBeatTime` | Missing | `CLuaBirdDefs.cpp:152` | `GetBirdWingBeatTime` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getBlipColor` | Missing | `CLuaBlipDefs.cpp:147` | `ArgumentParserWarn<false, GetBlipColor>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getBlipIcon` | Missing | `CLuaBlipDefs.cpp:145` | `ArgumentParserWarn<false, GetBlipIcon>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getBlipOrdering` | Missing | `CLuaBlipDefs.cpp:148` | `ArgumentParserWarn<false, GetBlipOrdering>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getBlipSize` | Missing | `CLuaBlipDefs.cpp:146` | `ArgumentParserWarn<false, GetBlipSize>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getBlipVisibleDistance` | Missing | `CLuaBlipDefs.cpp:149` | `ArgumentParserWarn<false, GetBlipVisibleDistance>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getColPolygonHeight` | Missing | `CLuaColShapeDefs.cpp:37` | `ArgumentParser<GetColPolygonHeight>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getColPolygonPointPosition` | Missing | `CLuaColShapeDefs.cpp:29` | `GetColPolygonPointPosition` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getColPolygonPoints` | Missing | `CLuaColShapeDefs.cpp:28` | `GetColPolygonPoints` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getColShapeRadius` | Missing | `CLuaColShapeDefs.cpp:24` | `GetColShapeRadius` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getColShapeSize` | Missing | `CLuaColShapeDefs.cpp:26` | `GetColShapeSize` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getColShapeType` | Missing | `CLuaColShapeDefs.cpp:35` | `GetColShapeType` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getLightColor` | Missing | `CLuaPointLightDefs.cpp:20` | `GetLightColor` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getLightDirection` | Missing | `CLuaPointLightDefs.cpp:20` | `GetLightDirection` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getLightRadius` | Missing | `CLuaPointLightDefs.cpp:19` | `GetLightRadius` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getLightType` | Missing | `CLuaPointLightDefs.cpp:19` | `GetLightType` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getMarkerColor` | Missing | `CLuaMarkerDefs.cpp:27` | `GetMarkerColor` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getMarkerCount` | Missing | `CLuaMarkerDefs.cpp:23` | `GetMarkerCount` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getMarkerIcon` | Missing | `CLuaMarkerDefs.cpp:29` | `GetMarkerIcon` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getMarkerLimitStats` | Missing | `CLuaMarkerDefs.cpp:24` | `GetMarkerLimitStats` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getMarkerSize` | Missing | `CLuaMarkerDefs.cpp:26` | `GetMarkerSize` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getMarkerTarget` | Missing | `CLuaMarkerDefs.cpp:28` | `GetMarkerTarget` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getMarkerTargetArrowProperties` | Missing | `CLuaMarkerDefs.cpp:30` | `ArgumentParser<GetMarkerTargetArrowProperties>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getMarkerType` | Missing | `CLuaMarkerDefs.cpp:25` | `GetMarkerType` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getNativeUIState` | Missing | `CLuaNativeUIDefs.cpp:260` | `GetNativeUIState` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getPickupAmmo` | Missing | `CLuaPickupDefs.cpp:19` | `GetPickupAmmo` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getPickupAmount` | Missing | `CLuaPickupDefs.cpp:19` | `GetPickupAmount` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getPickupType` | Missing | `CLuaPickupDefs.cpp:19` | `GetPickupType` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getPickupWeapon` | Missing | `CLuaPickupDefs.cpp:19` | `GetPickupWeapon` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getPlayersInTeam` | Missing | `CLuaTeamDefs.cpp:18` | `ArgumentParserWarn<false, GetPlayersInTeam>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getRadarAreaColor` | Missing | `CLuaRadarAreaDefs.cpp:18` | `GetRadarAreaColor` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getRadarAreaSize` | Missing | `CLuaRadarAreaDefs.cpp:18` | `GetRadarAreaSize` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getRopeCarriedElement` | Missing | `CLuaRopeDefs.cpp:230` | `GetRopeCarriedElement` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getRopeDuration` | Missing | `CLuaRopeDefs.cpp:220` | `GetRopeDuration` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getRopeFixedNode` | Missing | `CLuaRopeDefs.cpp:227` | `GetRopeFixedNode` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getRopeHolder` | Missing | `CLuaRopeDefs.cpp:222` | `GetRopeHolder` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getRopeHolderOffset` | Missing | `CLuaRopeDefs.cpp:223` | `GetRopeHolderOffset` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getRopeLength` | Missing | `CLuaRopeDefs.cpp:226` | `GetRopeLength` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getRopePositionAt` | Missing | `CLuaRopeDefs.cpp:231` | `GetRopePositionAt` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getRopeRemainingTime` | Missing | `CLuaRopeDefs.cpp:221` | `GetRopeRemainingTime` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getRopeTopVelocity` | Missing | `CLuaRopeDefs.cpp:224` | `GetRopeTopVelocity` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getRopeType` | Missing | `CLuaRopeDefs.cpp:219` | `GetRopeType` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getRopeWinchHeight` | Missing | `CLuaRopeDefs.cpp:225` | `GetRopeWinchHeight` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getSearchLightColor` | Missing | `CLuaSearchLightDefs.cpp:23` | `ArgumentParser<GetSearchLightColor>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getSearchLightEndPosition` | Missing | `CLuaSearchLightDefs.cpp:20` | `GetSearchLightEndPosition` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getSearchLightEndRadius` | Missing | `CLuaSearchLightDefs.cpp:22` | `GetSearchLightEndRadius` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getSearchLightStartPosition` | Missing | `CLuaSearchLightDefs.cpp:19` | `GetSearchLightStartPosition` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getSearchLightStartRadius` | Missing | `CLuaSearchLightDefs.cpp:21` | `GetSearchLightStartRadius` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getTeamColor` | Missing | `CLuaTeamDefs.cpp:17` | `ArgumentParserWarn<false, GetTeamColor>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getTeamFriendlyFire` | Missing | `CLuaTeamDefs.cpp:17` | `ArgumentParserWarn<false, GetTeamFriendlyFire>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getTeamFromName` | Missing | `CLuaTeamDefs.cpp:16` | `ArgumentParserWarn<false, GetTeamFromName>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getTeamName` | Missing | `CLuaTeamDefs.cpp:16` | `ArgumentParserWarn<false, GetTeamName>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `isBirdCurvedFlightEnabled` | Missing | `CLuaBirdDefs.cpp:154` | `IsBirdCurvedFlightEnabled` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `isBirdMovementEnabled` | Missing | `CLuaBirdDefs.cpp:156` | `IsBirdMovementEnabled` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `isBirdShootable` | Missing | `CLuaBirdDefs.cpp:155` | `IsBirdShootable` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `isCoronaReflectionEnabled` | Missing | `CLuaMarkerDefs.cpp:40` | `ArgumentParser<IsCoronaReflectionEnabled>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `isInsideColShape` | Missing | `CLuaColShapeDefs.cpp:34` | `IsInsideColShape` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `isInsideRadarArea` | Missing | `CLuaRadarAreaDefs.cpp:20` | `IsInsideRadarArea` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `isRadarAreaFlashing` | Missing | `CLuaRadarAreaDefs.cpp:19` | `IsRadarAreaFlashing` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `isRopeActive` | Missing | `CLuaRopeDefs.cpp:231` | `IsRopeActive` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `isRopeOnGround` | Missing | `CLuaRopeDefs.cpp:228` | `IsRopeOnGround` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `isRopePhysicsEnabled` | Missing | `CLuaRopeDefs.cpp:229` | `IsRopePhysicsEnabled` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `isShowCollisionsEnabled` | Missing | `CLuaColShapeDefs.cpp:40` | `ArgumentParser<IsShowCollisionsEnabled>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `processBirdGunShot` | Missing | `CLuaBirdDefs.cpp:157` | `ProcessBirdGunShot` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `releaseNativeUI` | Missing | `CLuaNativeUIDefs.cpp:261` | `ReleaseNativeUI` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `removeColPolygonPoint` | Missing | `CLuaColShapeDefs.cpp:32` | `RemoveColPolygonPoint` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `renderScriptImportantArea` | Missing | `CLuaMarkerDefs.cpp:21` | `ArgumentParser<RenderScriptImportantArea>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setBirdColors` | Missing | `CLuaBirdDefs.cpp:151` | `SetBirdColors` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setBirdCurvedFlightEnabled` | Missing | `CLuaBirdDefs.cpp:154` | `SetBirdCurvedFlightEnabled` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setBirdMovementEnabled` | Missing | `CLuaBirdDefs.cpp:156` | `SetBirdMovementEnabled` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setBirdRenderDistance` | Missing | `CLuaBirdDefs.cpp:153` | `SetBirdRenderDistance` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setBirdShootable` | Missing | `CLuaBirdDefs.cpp:155` | `SetBirdShootable` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setBirdSize` | Missing | `CLuaBirdDefs.cpp:151` | `SetBirdSize` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setBirdTargetVelocity` | Missing | `CLuaBirdDefs.cpp:150` | `SetBirdTargetVelocity` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setBirdVelocity` | Missing | `CLuaBirdDefs.cpp:149` | `SetBirdVelocity` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setBirdWingBeatTime` | Missing | `CLuaBirdDefs.cpp:152` | `SetBirdWingBeatTime` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setBlipColor` | Missing | `CLuaBlipDefs.cpp:154` | `ArgumentParserWarn<false, SetBlipColor>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setBlipIcon` | Missing | `CLuaBlipDefs.cpp:152` | `ArgumentParserWarn<false, SetBlipIcon>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setBlipOrdering` | Missing | `CLuaBlipDefs.cpp:155` | `ArgumentParserWarn<false, SetBlipOrdering>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setBlipSize` | Missing | `CLuaBlipDefs.cpp:153` | `ArgumentParserWarn<false, SetBlipSize>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setBlipVisibleDistance` | Missing | `CLuaBlipDefs.cpp:156` | `ArgumentParserWarn<false, SetBlipVisibleDistance>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setColPolygonHeight` | Missing | `CLuaColShapeDefs.cpp:36` | `ArgumentParser<SetColPolygonHeight>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setColPolygonPointPosition` | Missing | `CLuaColShapeDefs.cpp:30` | `SetColPolygonPointPosition` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setColShapeRadius` | Missing | `CLuaColShapeDefs.cpp:25` | `SetColShapeRadius` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setColShapeSize` | Missing | `CLuaColShapeDefs.cpp:27` | `SetColShapeSize` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setCoronaReflectionEnabled` | Missing | `CLuaMarkerDefs.cpp:39` | `ArgumentParser<SetCoronaReflectionEnabled>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setLightColor` | Missing | `CLuaPointLightDefs.cpp:20` | `SetLightColor` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setLightDirection` | Missing | `CLuaPointLightDefs.cpp:20` | `SetLightDirection` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setLightRadius` | Missing | `CLuaPointLightDefs.cpp:19` | `SetLightRadius` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setMarkerColor` | Missing | `CLuaMarkerDefs.cpp:34` | `SetMarkerColor` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setMarkerIcon` | Missing | `CLuaMarkerDefs.cpp:36` | `SetMarkerIcon` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setMarkerSize` | Missing | `CLuaMarkerDefs.cpp:33` | `SetMarkerSize` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setMarkerTarget` | Missing | `CLuaMarkerDefs.cpp:35` | `SetMarkerTarget` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setMarkerTargetArrowProperties` | Missing | `CLuaMarkerDefs.cpp:37` | `ArgumentParser<SetMarkerTargetArrowProperties>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setMarkerType` | Missing | `CLuaMarkerDefs.cpp:32` | `SetMarkerType` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setPickupType` | Missing | `CLuaPickupDefs.cpp:21` | `SetPickupType` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setRadarAreaColor` | Missing | `CLuaRadarAreaDefs.cpp:19` | `SetRadarAreaColor` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setRadarAreaFlashing` | Missing | `CLuaRadarAreaDefs.cpp:19` | `SetRadarAreaFlashing` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setRadarAreaSize` | Missing | `CLuaRadarAreaDefs.cpp:20` | `SetRadarAreaSize` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setRopeDuration` | Missing | `CLuaRopeDefs.cpp:220` | `SetRopeDuration` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setRopeFixedNode` | Missing | `CLuaRopeDefs.cpp:227` | `SetRopeFixedNode` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setRopeHolder` | Missing | `CLuaRopeDefs.cpp:222` | `SetRopeHolder` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setRopeHolderOffset` | Missing | `CLuaRopeDefs.cpp:223` | `SetRopeHolderOffset` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setRopeLength` | Missing | `CLuaRopeDefs.cpp:226` | `SetRopeLength` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setRopeOnGround` | Missing | `CLuaRopeDefs.cpp:228` | `SetRopeOnGround` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setRopePhysicsEnabled` | Missing | `CLuaRopeDefs.cpp:229` | `SetRopePhysicsEnabled` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setRopeRemainingTime` | Missing | `CLuaRopeDefs.cpp:221` | `SetRopeRemainingTime` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setRopeTopVelocity` | Missing | `CLuaRopeDefs.cpp:224` | `SetRopeTopVelocity` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setRopeType` | Missing | `CLuaRopeDefs.cpp:219` | `SetRopeType` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setRopeWinchHeight` | Missing | `CLuaRopeDefs.cpp:225` | `SetRopeWinchHeight` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setSearchLightColor` | Missing | `CLuaSearchLightDefs.cpp:29` | `ArgumentParser<SetSearchLightColor>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setSearchLightEndPosition` | Missing | `CLuaSearchLightDefs.cpp:26` | `SetSearchLightEndPosition` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setSearchLightEndRadius` | Missing | `CLuaSearchLightDefs.cpp:28` | `SetSearchLightEndRadius` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setSearchLightStartPosition` | Missing | `CLuaSearchLightDefs.cpp:25` | `SetSearchLightStartPosition` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setSearchLightStartRadius` | Missing | `CLuaSearchLightDefs.cpp:27` | `SetSearchLightStartRadius` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `showCol` | Missing | `CLuaColShapeDefs.cpp:39` | `ArgumentParser<SetShowCollision>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `showNativeText` | Missing | `CLuaNativeUIDefs.cpp:261` | `ShowNativeText` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `updateNativeUI` | Missing | `CLuaNativeUIDefs.cpp:260` | `UpdateNativeUI` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
