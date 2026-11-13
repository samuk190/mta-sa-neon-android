# Web Browser & Compatibility (CEF / Discord)

Embedded Chromium web pages, browser mouse/keyboard input, Discord Rich Presence, and backwards compatibility helpers.

## Parity Status

- **Total Functions:** 103
- **Implemented:** 0 (0.0%)
- **Stubs:** 0
- **Missing:** 103

> **Technical Requirement:** Desktop bundles CEF (Chromium Embedded Framework). On Android, this requires an off-screen Android `WebView` rendering to OpenGL texture or native popup.

## Function List

| Lua Function | Status | Desktop Source Ref | Desktop Handler | Conversion Requirement / Implementation Notes |
|---|---|---|---|---|
| `attachElementToElement` | Missing | `CLuaCompatibilityDefs.cpp:60` | `CLuaElementDefs::AttachElements` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `canBrowserNavigateBack` | Missing | `CLuaBrowserDefs.cpp:44` | `CanBrowserNavigateBack` | Needs Android offscreen WebView render-to-texture bridge. |
| `canBrowserNavigateForward` | Missing | `CLuaBrowserDefs.cpp:45` | `CanBrowserNavigateForward` | Needs Android offscreen WebView render-to-texture bridge. |
| `canPlayerBeKnockedOffBike` | Missing | `CLuaCompatibilityDefs.cpp:18` | `CLuaPedDefs::CanPedBeKnockedOffBike` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `createBrowser` | Missing | `CLuaBrowserDefs.cpp:20` | `CreateBrowser` | Needs Android offscreen WebView render-to-texture bridge. |
| `detachElementFromElement` | Missing | `CLuaCompatibilityDefs.cpp:61` | `CLuaElementDefs::DetachElements` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `doesPlayerHaveJetPack` | Missing | `CLuaCompatibilityDefs.cpp:21` | `CLuaPedDefs::DoesPedHaveJetPack` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `executeBrowserJavascript` | Missing | `CLuaBrowserDefs.cpp:33` | `ExecuteBrowserJavascript` | Needs Android offscreen WebView render-to-texture bridge. |
| `focusBrowser` | Missing | `CLuaBrowserDefs.cpp:37` | `FocusBrowser` | Needs Android offscreen WebView render-to-texture bridge. |
| `getBrowserProperty` | Missing | `CLuaBrowserDefs.cpp:40` | `GetBrowserProperty` | Needs Android offscreen WebView render-to-texture bridge. |
| `getBrowserSettings` | Missing | `CLuaBrowserDefs.cpp:41` | `GetBrowserSettings` | Needs Android offscreen WebView render-to-texture bridge. |
| `getBrowserSource` | Missing | `CLuaBrowserDefs.cpp:42` | `GetBrowserSource` | Needs Android offscreen WebView render-to-texture bridge. |
| `getBrowserTitle` | Missing | `CLuaBrowserDefs.cpp:29` | `GetBrowserTitle` | Needs Android offscreen WebView render-to-texture bridge. |
| `getBrowserURL` | Missing | `CLuaBrowserDefs.cpp:30` | `GetBrowserURL` | Needs Android offscreen WebView render-to-texture bridge. |
| `getBrowserVolume` | Missing | `CLuaBrowserDefs.cpp:34` | `GetBrowserVolume` | Needs Android offscreen WebView render-to-texture bridge. |
| `getCameraShakeLevel` | Missing | `CLuaCompatibilityDefs.cpp:73` | `ArgumentParserWarn<false, CLuaCameraDefs::GetCameraDrunkLevel>` | Needs reading/writing CCamera singleton in libGTASA.so. |
| `getControlState` | Missing | `CLuaCompatibilityDefs.cpp:71` | `ArgumentParserWarn<false, CLuaPedDefs::GetPedControlState>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getDiscordRichPresenceUserID` | Missing | `CLuaDiscordDefs.cpp:30` | `ArgumentParser<GetDiscordUserID>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getObjectRotation` | Missing | `CLuaCompatibilityDefs.cpp:53` | `CLuaElementDefs::GetElementRotation` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getPedSkin` | Missing | `CLuaCompatibilityDefs.cpp:51` | `CLuaElementDefs::GetElementModel` | Needs reading/writing CPed / CPlayerPed struct fields in libGTASA.so. |
| `getPedWeaponMuzzlePosition` | Missing | `CLuaCompatibilityDefs.cpp:38` | `CLuaPedDefs::GetPedWeaponMuzzlePosition` | Needs reading/writing CPed / CPlayerPed struct fields in libGTASA.so. |
| `getPlayerAmmoInClip` | Missing | `CLuaCompatibilityDefs.cpp:36` | `CLuaPedDefs::GetPedAmmoInClip` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getPlayerArmor` | Missing | `CLuaCompatibilityDefs.cpp:40` | `ArgumentParserWarn<false, CLuaPedDefs::GetPedArmor>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getPlayerContactElement` | Missing | `CLuaCompatibilityDefs.cpp:19` | `CLuaPedDefs::GetPedContactElement` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getPlayerOccupiedVehicle` | Missing | `CLuaCompatibilityDefs.cpp:39` | `CLuaPedDefs::GetPedOccupiedVehicle` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getPlayerRotation` | Missing | `CLuaCompatibilityDefs.cpp:17` | `CLuaPedDefs::GetPedRotation` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getPlayerSimplestTask` | Missing | `CLuaCompatibilityDefs.cpp:28` | `CLuaPedDefs::GetPedSimplestTask` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getPlayerSkin` | Missing | `CLuaCompatibilityDefs.cpp:41` | `CLuaElementDefs::GetElementModel` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getPlayerStat` | Missing | `CLuaCompatibilityDefs.cpp:44` | `CLuaPedDefs::GetPedStat` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getPlayerTarget` | Missing | `CLuaCompatibilityDefs.cpp:30` | `CLuaPedDefs::GetPedTarget` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getPlayerTargetCollision` | Missing | `CLuaCompatibilityDefs.cpp:33` | `CLuaPedDefs::GetPedTargetCollision` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getPlayerTargetEnd` | Missing | `CLuaCompatibilityDefs.cpp:32` | `CLuaPedDefs::GetPedTargetEnd` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getPlayerTargetStart` | Missing | `CLuaCompatibilityDefs.cpp:31` | `CLuaPedDefs::GetPedTargetStart` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getPlayerTask` | Missing | `CLuaCompatibilityDefs.cpp:27` | `CLuaPedDefs::GetPedTask` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getPlayerTotalAmmo` | Missing | `CLuaCompatibilityDefs.cpp:37` | `CLuaPedDefs::GetPedTotalAmmo` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getPlayerWeapon` | Missing | `CLuaCompatibilityDefs.cpp:35` | `CLuaPedDefs::GetPedWeapon` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getPlayerWeaponSlot` | Missing | `CLuaCompatibilityDefs.cpp:34` | `CLuaPedDefs::GetPedWeaponSlot` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getVehicleID` | Missing | `CLuaCompatibilityDefs.cpp:56` | `CLuaElementDefs::GetElementModel` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `getVehicleIDFromName` | Missing | `CLuaCompatibilityDefs.cpp:55` | `CLuaVehicleDefs::GetVehicleModelFromName` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `getVehicleModel` | Missing | `CLuaCompatibilityDefs.cpp:50` | `CLuaElementDefs::GetElementModel` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `getVehicleNameFromID` | Missing | `CLuaCompatibilityDefs.cpp:58` | `CLuaVehicleDefs::GetVehicleNameFromModel` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `getVehicleRotation` | Missing | `CLuaCompatibilityDefs.cpp:57` | `CLuaElementDefs::GetElementRotation` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `guiCreateBrowser` | Missing | `CLuaBrowserDefs.cpp:51` | `GUICreateBrowser` | Needs Android widget state and input handler in LuaGuiApi. |
| `guiEditSetCaratIndex` | Missing | `CLuaCompatibilityDefs.cpp:68` | `CLuaGUIDefs::GUIEditSetCaretIndex` | Needs Android widget state and input handler in LuaGuiApi. |
| `guiGetBrowser` | Missing | `CLuaBrowserDefs.cpp:52` | `GUIGetBrowser` | Needs Android widget state and input handler in LuaGuiApi. |
| `guiMemoSetCaratIndex` | Missing | `CLuaCompatibilityDefs.cpp:69` | `CLuaGUIDefs::GUIMemoSetCaretIndex` | Needs Android widget state and input handler in LuaGuiApi. |
| `injectBrowserMouseDown` | Missing | `CLuaBrowserDefs.cpp:26` | `InjectBrowserMouseDown` | Needs Android offscreen WebView render-to-texture bridge. |
| `injectBrowserMouseMove` | Missing | `CLuaBrowserDefs.cpp:25` | `InjectBrowserMouseMove` | Needs Android offscreen WebView render-to-texture bridge. |
| `injectBrowserMouseUp` | Missing | `CLuaBrowserDefs.cpp:27` | `InjectBrowserMouseUp` | Needs Android offscreen WebView render-to-texture bridge. |
| `injectBrowserMouseWheel` | Missing | `CLuaBrowserDefs.cpp:28` | `InjectBrowserMouseWheel` | Needs Android offscreen WebView render-to-texture bridge. |
| `isBrowserDomainBlocked` | Missing | `CLuaBrowserDefs.cpp:36` | `IsBrowserDomainBlocked` | Needs Android offscreen WebView render-to-texture bridge. |
| `isBrowserFocused` | Missing | `CLuaBrowserDefs.cpp:38` | `IsBrowserFocused` | Needs Android offscreen WebView render-to-texture bridge. |
| `isBrowserGPUEnabled` | Missing | `CLuaBrowserDefs.cpp:53` | `ArgumentParser<IsBrowserGPUEnabled>` | Needs Android offscreen WebView render-to-texture bridge. |
| `isBrowserLoading` | Missing | `CLuaBrowserDefs.cpp:24` | `IsBrowserLoading` | Needs Android offscreen WebView render-to-texture bridge. |
| `isBrowserRenderingPaused` | Missing | `CLuaBrowserDefs.cpp:32` | `ArgumentParser<IsBrowserRenderingPaused>` | Needs Android offscreen WebView render-to-texture bridge. |
| `isDiscordRichPresenceConnected` | Missing | `CLuaDiscordDefs.cpp:29` | `ArgumentParser<IsDiscordRPCConnected>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `isPedInWater` | Missing | `CLuaCompatibilityDefs.cpp:23` | `CLuaElementDefs::IsElementInWater` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `isPedOnFire` | Missing | `CLuaCompatibilityDefs.cpp:24` | `CLuaPedDefs::IsPedOnFire` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `isPlayerChoking` | Missing | `CLuaCompatibilityDefs.cpp:42` | `CLuaPedDefs::IsPedChoking` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `isPlayerDead` | Missing | `CLuaCompatibilityDefs.cpp:67` | `CLuaPedDefs::IsPedDead` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `isPlayerDoingTask` | Missing | `CLuaCompatibilityDefs.cpp:29` | `CLuaPedDefs::IsPedDoingTask` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `isPlayerDucked` | Missing | `CLuaCompatibilityDefs.cpp:43` | `CLuaPedDefs::IsPedDucked` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `isPlayerInVehicle` | Missing | `CLuaCompatibilityDefs.cpp:20` | `CLuaPedDefs::IsPedInVehicle` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `isPlayerInWater` | Missing | `CLuaCompatibilityDefs.cpp:22` | `CLuaElementDefs::IsElementInWater` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `isPlayerOnGround` | Missing | `CLuaCompatibilityDefs.cpp:26` | `CLuaPedDefs::IsPedOnGround` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `loadBrowserURL` | Missing | `CLuaBrowserDefs.cpp:23` | `LoadBrowserURL` | Needs Android offscreen WebView render-to-texture bridge. |
| `navigateBrowserBack` | Missing | `CLuaBrowserDefs.cpp:46` | `NavigateBrowserBack` | Needs Android offscreen WebView render-to-texture bridge. |
| `navigateBrowserForward` | Missing | `CLuaBrowserDefs.cpp:47` | `NavigateBrowserForward` | Needs Android offscreen WebView render-to-texture bridge. |
| `openExternalURL` | Missing | `CLuaBrowserDefs.cpp:22` | `OpenExternalURL` | Needs Android offscreen WebView render-to-texture bridge. |
| `reloadBrowserPage` | Missing | `CLuaBrowserDefs.cpp:48` | `ReloadBrowserPage` | Needs Android offscreen WebView render-to-texture bridge. |
| `requestBrowserDomains` | Missing | `CLuaBrowserDefs.cpp:21` | `RequestBrowserDomains` | Needs Android offscreen WebView render-to-texture bridge. |
| `resetDiscordRichPresenceData` | Missing | `CLuaDiscordDefs.cpp:28` | `ArgumentParser<ResetData>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `resizeBrowser` | Missing | `CLuaBrowserDefs.cpp:50` | `ResizeBrowser` | Needs Android offscreen WebView render-to-texture bridge. |
| `setBrowserAjaxHandler` | Missing | `CLuaBrowserDefs.cpp:43` | `SetBrowserAjaxHandler` | Needs Android offscreen WebView render-to-texture bridge. |
| `setBrowserProperty` | Missing | `CLuaBrowserDefs.cpp:39` | `SetBrowserProperty` | Needs Android offscreen WebView render-to-texture bridge. |
| `setBrowserRenderingPaused` | Missing | `CLuaBrowserDefs.cpp:31` | `SetBrowserRenderingPaused` | Needs Android offscreen WebView render-to-texture bridge. |
| `setBrowserVolume` | Missing | `CLuaBrowserDefs.cpp:35` | `SetBrowserVolume` | Needs Android offscreen WebView render-to-texture bridge. |
| `setCameraShakeLevel` | Missing | `CLuaCompatibilityDefs.cpp:72` | `ArgumentParserWarn<false, CLuaCameraDefs::SetCameraDrunkLevel>` | Needs reading/writing CCamera singleton in libGTASA.so. |
| `setControlState` | Missing | `CLuaCompatibilityDefs.cpp:70` | `ArgumentParserWarn<false, CLuaPedDefs::SetPedControlState>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setDiscordApplicationID` | Missing | `CLuaDiscordDefs.cpp:19` | `ArgumentParser<SetAppID>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setDiscordRichPresenceAsset` | Missing | `CLuaDiscordDefs.cpp:22` | `ArgumentParser<SetLargeAsset>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setDiscordRichPresenceButton` | Missing | `CLuaDiscordDefs.cpp:24` | `ArgumentParser<SetButtons>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setDiscordRichPresenceDetails` | Missing | `CLuaDiscordDefs.cpp:20` | `ArgumentParser<SetDetails>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setDiscordRichPresenceEndTime` | Missing | `CLuaDiscordDefs.cpp:26` | `ArgumentParser<SetEndTime>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setDiscordRichPresencePartySize` | Missing | `CLuaDiscordDefs.cpp:27` | `ArgumentParser<SetPartySize>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setDiscordRichPresenceSmallAsset` | Missing | `CLuaDiscordDefs.cpp:23` | `ArgumentParser<SetSmallAsset>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setDiscordRichPresenceStartTime` | Missing | `CLuaDiscordDefs.cpp:25` | `ArgumentParser<SetStartTime>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setDiscordRichPresenceState` | Missing | `CLuaDiscordDefs.cpp:21` | `ArgumentParser<SetState>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setObjectRotation` | Missing | `CLuaCompatibilityDefs.cpp:54` | `CLuaElementDefs::SetElementRotation` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setPedOnFire` | Missing | `CLuaCompatibilityDefs.cpp:25` | `CLuaPedDefs::SetPedOnFire` | Needs reading/writing CPed / CPlayerPed struct fields in libGTASA.so. |
| `setPedSkin` | Missing | `CLuaCompatibilityDefs.cpp:52` | `CLuaElementDefs::SetElementModel` | Needs reading/writing CPed / CPlayerPed struct fields in libGTASA.so. |
| `setPlayerCanBeKnockedOffBike` | Missing | `CLuaCompatibilityDefs.cpp:48` | `CLuaPedDefs::SetPedCanBeKnockedOffBike` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setPlayerRotation` | Missing | `CLuaCompatibilityDefs.cpp:47` | `CLuaPedDefs::SetPedRotation` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setPlayerSkin` | Missing | `CLuaCompatibilityDefs.cpp:46` | `CLuaElementDefs::SetElementModel` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setPlayerWeaponSlot` | Missing | `CLuaCompatibilityDefs.cpp:45` | `CLuaPedDefs::SetPedWeaponSlot` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setVehicleModel` | Missing | `CLuaCompatibilityDefs.cpp:49` | `CLuaElementDefs::SetElementModel` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `setVehicleRotation` | Missing | `CLuaCompatibilityDefs.cpp:59` | `CLuaElementDefs::SetElementRotation` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `toggleBrowserDevTools` | Missing | `CLuaBrowserDefs.cpp:49` | `ToggleBrowserDevTools` | Needs Android offscreen WebView render-to-texture bridge. |
| `xmlCreateSubNode` | Missing | `CLuaCompatibilityDefs.cpp:65` | `CLuaXMLDefs::xmlCreateChild` | Needs TinyXML2 client-side parser integration. |
| `xmlFindSubNode` | Missing | `CLuaCompatibilityDefs.cpp:62` | `CLuaXMLDefs::xmlNodeFindChild` | Needs TinyXML2 client-side parser integration. |
| `xmlNodeFindChild` | Missing | `CLuaCompatibilityDefs.cpp:66` | `CLuaXMLDefs::xmlNodeFindChild` | Needs TinyXML2 client-side parser integration. |
| `xmlNodeFindSubNode` | Missing | `CLuaCompatibilityDefs.cpp:64` | `CLuaXMLDefs::xmlNodeFindChild` | Needs TinyXML2 client-side parser integration. |
| `xmlNodeGetSubNodes` | Missing | `CLuaCompatibilityDefs.cpp:63` | `CLuaXMLDefs::xmlNodeGetChildren` | Needs TinyXML2 client-side parser integration. |
