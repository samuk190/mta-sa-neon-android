# Element Tree & Data (CLuaElementDefs)

Core element hierarchy (parent/child), element data (synced/local), collisions, dimensions, alpha, positions, velocities, matrix, and spatial streaming.

## Parity Status

- **Total Functions:** 81
- **Implemented:** 10 (12.3%)
- **Stubs:** 24
- **Missing:** 47

> **Technical Requirement:** Implemented via `ElementManager.cpp` and `ServerWorld.cpp`. Gaps relate to spatial octree lookup, element matrix math, and custom attachments.

## Function List

| Lua Function | Status | Desktop Source Ref | Desktop Handler | Conversion Requirement / Implementation Notes |
|---|---|---|---|---|
| `acquireElementStreamingLease` | Missing | `CLuaElementDefs.cpp:96` | `AcquireElementStreamingLease` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `attachElements` | *Stub* | `CLuaElementDefs.cpp:89` | `AttachElements` | Currently stubbed: `DefaultValue(kTrue)`. Needs native engine wiring. |
| `createElement` | Missing | `CLuaElementDefs.cpp:76` | `CreateElement` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `destroyElement` | **Implemented** | `CLuaElementDefs.cpp:77` | `DestroyElement` | Operational on Android. (DestroyElement) |
| `detachElements` | *Stub* | `CLuaElementDefs.cpp:90` | `DetachElements` | Currently stubbed: `DefaultValue(kTrue)`. Needs native engine wiring. |
| `getAllElementData` | Missing | `CLuaElementDefs.cpp:27` | `ArgumentParserWarn<false, GetAllElementData>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getAttachedElements` | Missing | `CLuaElementDefs.cpp:50` | `GetAttachedElements` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getElementAlpha` | *Stub* | `CLuaElementDefs.cpp:55` | `GetElementAlpha` | Currently stubbed: `DefaultValue(kAlpha255)`. Needs native engine wiring. |
| `getElementAngularVelocity` | *Stub* | `CLuaElementDefs.cpp:32` | `GetElementTurnVelocity` | Currently stubbed: `DefaultValue(kZero3)`. Needs native engine wiring. |
| `getElementAttachedOffsets` | Missing | `CLuaElementDefs.cpp:54` | `GetElementAttachedOffsets` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getElementAttachedTo` | Missing | `CLuaElementDefs.cpp:49` | `GetElementAttachedTo` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getElementBoundingBox` | Missing | `CLuaElementDefs.cpp:46` | `GetElementBoundingBox` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getElementByID` | Missing | `CLuaElementDefs.cpp:24` | `GetElementByID` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getElementByIndex` | Missing | `CLuaElementDefs.cpp:25` | `GetElementByIndex` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getElementChild` | Missing | `CLuaElementDefs.cpp:35` | `GetElementChild` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getElementChildren` | Missing | `CLuaElementDefs.cpp:34` | `GetElementChildren` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getElementChildrenCount` | Missing | `CLuaElementDefs.cpp:36` | `GetElementChildrenCount` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getElementColShape` | Missing | `CLuaElementDefs.cpp:62` | `GetElementColShape` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getElementCollisionsEnabled` | *Stub* | `CLuaElementDefs.cpp:67` | `GetElementCollisionsEnabled` | Currently stubbed: `DefaultValue(kTrue)`. Needs native engine wiring. |
| `getElementData` | **Implemented** | `CLuaElementDefs.cpp:26` | `GetElementData` | Operational on Android. (GetElementData) |
| `getElementDimension` | *Stub* | `CLuaElementDefs.cpp:45` | `GetElementDimension` | Currently stubbed: `l_num0: lua_pushinteger(L, 0)`. Needs native engine wiring. |
| `getElementDistanceFromCentreOfMassToBaseOfModel` | *Stub* | `CLuaElementDefs.cpp:51` | `GetElementDistanceFromCentreOfMassToBaseOfModel` | Currently stubbed: `DefaultValue(kZero)`. Needs native engine wiring. |
| `getElementHealth` | **Implemented** | `CLuaElementDefs.cpp:58` | `GetElementHealth` | Operational on Android. (l_getElementHealth) |
| `getElementID` | Missing | `CLuaElementDefs.cpp:37` | `GetElementID` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getElementInterior` | *Stub* | `CLuaElementDefs.cpp:40` | `GetElementInterior` | Currently stubbed: `l_num0: lua_pushinteger(L, 0)`. Needs native engine wiring. |
| `getElementLighting` | Missing | `CLuaElementDefs.cpp:56` | `ArgumentParser<GetElementLighting>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getElementMatrix` | *Stub* | `CLuaElementDefs.cpp:28` | `GetElementMatrix` | Currently stubbed: `DefaultValue(kNil)`. Needs native engine wiring. |
| `getElementModel` | *Stub* | `CLuaElementDefs.cpp:59` | `GetElementModel` | Currently stubbed: `l_num0: lua_pushinteger(L, 0)`. Needs native engine wiring. |
| `getElementParent` | Missing | `CLuaElementDefs.cpp:38` | `GetElementParent` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getElementPosition` | **Implemented** | `CLuaElementDefs.cpp:29` | `GetElementPosition` | Operational on Android. (l_getElementPosition) |
| `getElementRadius` | Missing | `CLuaElementDefs.cpp:47` | `GetElementRadius` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getElementRotation` | *Stub* | `CLuaElementDefs.cpp:30` | `GetElementRotation` | Currently stubbed: `l_getElementRotation: lua_pushnumber(L, 0.0)`. Needs native engine wiring. |
| `getElementType` | **Implemented** | `CLuaElementDefs.cpp:33` | `GetElementType` | Operational on Android. (l_getElementType) |
| `getElementVelocity` | *Stub* | `CLuaElementDefs.cpp:31` | `GetElementVelocity` | Currently stubbed: `l_getElementVelocity: lua_pushnumber(L, 0.0)`. Needs native engine wiring. |
| `getElementsByType` | **Implemented** | `CLuaElementDefs.cpp:39` | `GetElementsByType` | Operational on Android. (l_getElementsByType) |
| `getElementsWithinColShape` | Missing | `CLuaElementDefs.cpp:43` | `GetElementsWithinColShape` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getElementsWithinRange` | Missing | `CLuaElementDefs.cpp:44` | `ArgumentParserWarn<false, GetElementsWithinRange>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getLowLODElement` | Missing | `CLuaElementDefs.cpp:69` | `GetLowLodElement` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getRootElement` | Missing | `CLuaElementDefs.cpp:22` | `GetRootElement` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `hasElementData` | Missing | `CLuaElementDefs.cpp:53` | `HasElementData` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `isElement` | **Implemented** | `CLuaElementDefs.cpp:23` | `IsElement` | Operational on Android. (l_isElement) |
| `isElementAttached` | Missing | `CLuaElementDefs.cpp:48` | `IsElementAttached` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `isElementCallPropagationEnabled` | Missing | `CLuaElementDefs.cpp:71` | `IsElementCallPropagationEnabled` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `isElementCollidableWith` | Missing | `CLuaElementDefs.cpp:65` | `IsElementCollidableWith` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `isElementDoubleSided` | Missing | `CLuaElementDefs.cpp:66` | `IsElementDoubleSided` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `isElementFrozen` | *Stub* | `CLuaElementDefs.cpp:68` | `IsElementFrozen` | Currently stubbed: `l_false: lua_pushboolean(L, 0)`. Needs native engine wiring. |
| `isElementInWater` | Missing | `CLuaElementDefs.cpp:63` | `IsElementInWater` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `isElementLocal` | Missing | `CLuaElementDefs.cpp:52` | `IsElementLocal` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `isElementLowLOD` | Missing | `CLuaElementDefs.cpp:70` | `IsElementLowLod` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `isElementOnFire` | Missing | `CLuaElementDefs.cpp:73` | `ArgumentParser<IsElementOnFire>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `isElementOnScreen` | *Stub* | `CLuaElementDefs.cpp:57` | `ArgumentParserWarn<nullptr, IsElementOnScreen>` | Currently stubbed: `l_false: lua_pushboolean(L, 0)`. Needs native engine wiring. |
| `isElementStreamable` | Missing | `CLuaElementDefs.cpp:61` | `IsElementStreamable` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `isElementStreamedIn` | **Implemented** | `CLuaElementDefs.cpp:60` | `IsElementStreamedIn` | Operational on Android. (l_isElement) |
| `isElementSyncer` | **Implemented** | `CLuaElementDefs.cpp:64` | `IsElementSyncer` | Operational on Android. (l_isElementSyncer) |
| `isElementWaitingForGroundToLoad` | Missing | `CLuaElementDefs.cpp:72` | `IsElementWaitingForGroundToLoad` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `isElementWithinColShape` | Missing | `CLuaElementDefs.cpp:41` | `IsElementWithinColShape` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `isElementWithinMarker` | Missing | `CLuaElementDefs.cpp:42` | `IsElementWithinMarker` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `releaseElementStreamingLease` | Missing | `CLuaElementDefs.cpp:97` | `ReleaseElementStreamingLease` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setElementAlpha` | *Stub* | `CLuaElementDefs.cpp:92` | `SetElementAlpha` | Currently stubbed: `l_true: lua_pushboolean(L, 1)`. Needs native engine wiring. |
| `setElementAngularVelocity` | *Stub* | `CLuaElementDefs.cpp:86` | `SetElementAngularVelocity` | Currently stubbed: `DefaultValue(kTrue)`. Needs native engine wiring. |
| `setElementAttachedOffsets` | Missing | `CLuaElementDefs.cpp:91` | `SetElementAttachedOffsets` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setElementCallPropagationEnabled` | Missing | `CLuaElementDefs.cpp:103` | `SetElementCallPropagationEnabled` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setElementCollidableWith` | Missing | `CLuaElementDefs.cpp:99` | `SetElementCollidableWith` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setElementCollisionsEnabled` | *Stub* | `CLuaElementDefs.cpp:98` | `SetElementCollisionsEnabled` | Currently stubbed: `DefaultValue(kTrue)`. Needs native engine wiring. |
| `setElementData` | **Implemented** | `CLuaElementDefs.cpp:80` | `SetElementData` | Operational on Android. (SetElementData) |
| `setElementDimension` | *Stub* | `CLuaElementDefs.cpp:88` | `SetElementDimension` | Currently stubbed: `l_true: lua_pushboolean(L, 1)`. Needs native engine wiring. |
| `setElementDoubleSided` | Missing | `CLuaElementDefs.cpp:100` | `SetElementDoubleSided` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setElementFrozen` | *Stub* | `CLuaElementDefs.cpp:101` | `SetElementFrozen` | Currently stubbed: `DefaultValue(kTrue)`. Needs native engine wiring. |
| `setElementHealth` | *Stub* | `CLuaElementDefs.cpp:93` | `SetElementHealth` | Currently stubbed: `TrueResult: lua_pushboolean(L, 1)`. Needs native engine wiring. |
| `setElementID` | Missing | `CLuaElementDefs.cpp:78` | `SetElementID` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setElementInterior` | *Stub* | `CLuaElementDefs.cpp:87` | `SetElementInterior` | Currently stubbed: `l_true: lua_pushboolean(L, 1)`. Needs native engine wiring. |
| `setElementLighting` | Missing | `CLuaElementDefs.cpp:104` | `ArgumentParser<SetElementLighting>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setElementMatrix` | Missing | `CLuaElementDefs.cpp:82` | `SetElementMatrix` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setElementModel` | Missing | `CLuaElementDefs.cpp:94` | `SetElementModel` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setElementOnFire` | Missing | `CLuaElementDefs.cpp:105` | `ArgumentParser<SetElementOnFire>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setElementParent` | Missing | `CLuaElementDefs.cpp:79` | `SetElementParent` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setElementPosition` | *Stub* | `CLuaElementDefs.cpp:83` | `SetElementPosition` | Currently stubbed: `TrueResult: lua_pushboolean(L, 1)`. Needs native engine wiring. |
| `setElementRotation` | *Stub* | `CLuaElementDefs.cpp:84` | `SetElementRotation` | Currently stubbed: `DefaultValue(kTrue)`. Needs native engine wiring. |
| `setElementStreamable` | Missing | `CLuaElementDefs.cpp:95` | `SetElementStreamable` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setElementVelocity` | *Stub* | `CLuaElementDefs.cpp:85` | `SetElementVelocity` | Currently stubbed: `DefaultValue(kTrue)`. Needs native engine wiring. |
| `setLowLODElement` | Missing | `CLuaElementDefs.cpp:102` | `ArgumentParser<SetLowLodElement>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
