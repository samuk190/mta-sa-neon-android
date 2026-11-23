# Web Browser & Compatibility (CEF / Discord)

Embedded Chromium web pages, browser mouse/keyboard input, Discord Rich Presence, and backwards compatibility helpers.

## Binding Coverage

- **Total Functions:** 103
- **Nonconstant handlers:** 103 (100.0%)
- **Stubs:** 0
- **Missing:** 0

> **Technical Requirement:** Desktop bundles CEF. Android has no web core: createBrowser and guiCreateBrowser still enforce the size errors, getBrowserSettings returns the three flags as false, and page calls return false. Discord checks lengths then returns false. Compatibility names share the modern handlers. Weapon slot, knock-off-bike and control state are stored. Ground height uses FindGroundZFor3DPosition when it returns. Task, target and line-of-sight queries return false because those engine calls are not hooked. setPedRotation applies to a script ped. attachElements and detachElements return false and do not change the server attachment snapshot.

## Function List

| Lua Function | Status | Desktop Source Ref | Desktop Handler | Conversion Requirement / Implementation Notes |
|---|---|---|---|---|
| `attachElementToElement` | **Implemented** | `CLuaCompatibilityDefs.cpp:60` | `CLuaElementDefs::AttachElements` | Operational on Android. (AttachElements) |
| `canBrowserNavigateBack` | **Implemented** | `CLuaBrowserDefs.cpp:44` | `CanBrowserNavigateBack` | Operational on Android. (MissingBrowser) |
| `canBrowserNavigateForward` | **Implemented** | `CLuaBrowserDefs.cpp:45` | `CanBrowserNavigateForward` | Operational on Android. (MissingBrowser) |
| `canPlayerBeKnockedOffBike` | **Implemented** | `CLuaCompatibilityDefs.cpp:18` | `CLuaPedDefs::CanPedBeKnockedOffBike` | Operational on Android. (CanPedBeKnockedOffBike) |
| `createBrowser` | **Implemented** | `CLuaBrowserDefs.cpp:20` | `CreateBrowser` | Operational on Android. (CreateBrowser) |
| `detachElementFromElement` | **Implemented** | `CLuaCompatibilityDefs.cpp:61` | `CLuaElementDefs::DetachElements` | Operational on Android. (DetachElements) |
| `doesPlayerHaveJetPack` | **Implemented** | `CLuaCompatibilityDefs.cpp:21` | `CLuaPedDefs::DoesPedHaveJetPack` | Operational on Android. (DoesPedHaveJetPack) |
| `executeBrowserJavascript` | **Implemented** | `CLuaBrowserDefs.cpp:33` | `ExecuteBrowserJavascript` | Operational on Android. (MissingBrowser) |
| `focusBrowser` | **Implemented** | `CLuaBrowserDefs.cpp:37` | `FocusBrowser` | Operational on Android. (MissingBrowser) |
| `getBrowserProperty` | **Implemented** | `CLuaBrowserDefs.cpp:40` | `GetBrowserProperty` | Operational on Android. (MissingBrowser) |
| `getBrowserSettings` | **Implemented** | `CLuaBrowserDefs.cpp:41` | `GetBrowserSettings` | Operational on Android. (GetBrowserSettings) |
| `getBrowserSource` | **Implemented** | `CLuaBrowserDefs.cpp:42` | `GetBrowserSource` | Operational on Android. (MissingBrowser) |
| `getBrowserTitle` | **Implemented** | `CLuaBrowserDefs.cpp:29` | `GetBrowserTitle` | Operational on Android. (MissingBrowser) |
| `getBrowserURL` | **Implemented** | `CLuaBrowserDefs.cpp:30` | `GetBrowserURL` | Operational on Android. (MissingBrowser) |
| `getBrowserVolume` | **Implemented** | `CLuaBrowserDefs.cpp:34` | `GetBrowserVolume` | Operational on Android. (MissingBrowser) |
| `getCameraShakeLevel` | **Implemented** | `CLuaCompatibilityDefs.cpp:73` | `ArgumentParserWarn<false, CLuaCameraDefs::GetCameraDrunkLevel>` | Operational on Android. (l_getCameraDrunkLevel) |
| `getControlState` | **Implemented** | `CLuaCompatibilityDefs.cpp:71` | `ArgumentParserWarn<false, CLuaPedDefs::GetPedControlState>` | Operational on Android. (GetPedControlState) |
| `getDiscordRichPresenceUserID` | **Implemented** | `CLuaDiscordDefs.cpp:30` | `ArgumentParser<GetDiscordUserID>` | Operational on Android. (GetUserID) |
| `getObjectRotation` | **Implemented** | `CLuaCompatibilityDefs.cpp:53` | `CLuaElementDefs::GetElementRotation` | Operational on Android. (l_getElementRotation) |
| `getPedSkin` | **Implemented** | `CLuaCompatibilityDefs.cpp:51` | `CLuaElementDefs::GetElementModel` | Operational on Android. (l_getElementModel) |
| `getPedWeaponMuzzlePosition` | **Implemented** | `CLuaCompatibilityDefs.cpp:38` | `CLuaPedDefs::GetPedWeaponMuzzlePosition` | Operational on Android. (GetPedWeaponMuzzlePosition) |
| `getPlayerAmmoInClip` | **Implemented** | `CLuaCompatibilityDefs.cpp:36` | `CLuaPedDefs::GetPedAmmoInClip` | Operational on Android. (GetPedAmmoInClip) |
| `getPlayerArmor` | **Implemented** | `CLuaCompatibilityDefs.cpp:40` | `ArgumentParserWarn<false, CLuaPedDefs::GetPedArmor>` | Follows CLuaPedDefs.cpp and the CClientPed fields those calls store. Headless, frozen, bleeding, armor, fighting style, walking style, gang driveby, camera rotation, mission policy and physical proofs are kept on the client element. ENTITY_ADD headless, frozen, armor and walking style are the unset values. Fighting style starts at 15 and only a client-created ped accepts styles 4..16. Script clothes are read back for slots 0-3. Oxygen and foot blood return the unstreamed result. Weapon accuracy and shooting rate return -1 until the ped is streamed, then the stored byte. CPed body bits are not written. Host-tested. Not device-verified. |
| `getPlayerContactElement` | **Implemented** | `CLuaCompatibilityDefs.cpp:19` | `CLuaPedDefs::GetPedContactElement` | Operational on Android. (GetPedContactElement) |
| `getPlayerOccupiedVehicle` | **Implemented** | `CLuaCompatibilityDefs.cpp:39` | `CLuaPedDefs::GetPedOccupiedVehicle` | Operational on Android. (GetPedOccupiedVehicle) |
| `getPlayerRotation` | **Implemented** | `CLuaCompatibilityDefs.cpp:17` | `CLuaPedDefs::GetPedRotation` | Operational on Android. (l_getPedRotation) |
| `getPlayerSimplestTask` | **Implemented** | `CLuaCompatibilityDefs.cpp:28` | `CLuaPedDefs::GetPedSimplestTask` | Operational on Android. (GetPedSimplestTask) |
| `getPlayerSkin` | **Implemented** | `CLuaCompatibilityDefs.cpp:41` | `CLuaElementDefs::GetElementModel` | Operational on Android. (l_getElementModel) |
| `getPlayerStat` | **Implemented** | `CLuaCompatibilityDefs.cpp:44` | `CLuaPedDefs::GetPedStat` | Operational on Android. (GetPedStat) |
| `getPlayerTarget` | **Implemented** | `CLuaCompatibilityDefs.cpp:30` | `CLuaPedDefs::GetPedTarget` | Operational on Android. (GetPedTarget) |
| `getPlayerTargetCollision` | **Implemented** | `CLuaCompatibilityDefs.cpp:33` | `CLuaPedDefs::GetPedTargetCollision` | Operational on Android. (GetPedTargetCollision) |
| `getPlayerTargetEnd` | **Implemented** | `CLuaCompatibilityDefs.cpp:32` | `CLuaPedDefs::GetPedTargetEnd` | Operational on Android. (GetPedTargetEnd) |
| `getPlayerTargetStart` | **Implemented** | `CLuaCompatibilityDefs.cpp:31` | `CLuaPedDefs::GetPedTargetStart` | Operational on Android. (GetPedTargetStart) |
| `getPlayerTask` | **Implemented** | `CLuaCompatibilityDefs.cpp:27` | `CLuaPedDefs::GetPedTask` | Operational on Android. (GetPedTask) |
| `getPlayerTotalAmmo` | **Implemented** | `CLuaCompatibilityDefs.cpp:37` | `CLuaPedDefs::GetPedTotalAmmo` | Operational on Android. (GetPedTotalAmmo) |
| `getPlayerWeapon` | **Implemented** | `CLuaCompatibilityDefs.cpp:35` | `CLuaPedDefs::GetPedWeapon` | Operational on Android. (GetPedWeapon) |
| `getPlayerWeaponSlot` | **Implemented** | `CLuaCompatibilityDefs.cpp:34` | `CLuaPedDefs::GetPedWeaponSlot` | Operational on Android. (GetPedWeaponSlot) |
| `getVehicleID` | **Implemented** | `CLuaCompatibilityDefs.cpp:56` | `CLuaElementDefs::GetElementModel` | Operational on Android. (l_getElementModel) |
| `getVehicleIDFromName` | **Implemented** | `CLuaCompatibilityDefs.cpp:55` | `CLuaVehicleDefs::GetVehicleModelFromName` | Operational on Android. (GetVehicleModelFromName) |
| `getVehicleModel` | **Implemented** | `CLuaCompatibilityDefs.cpp:50` | `CLuaElementDefs::GetElementModel` | Operational on Android. (l_getElementModel) |
| `getVehicleNameFromID` | **Implemented** | `CLuaCompatibilityDefs.cpp:58` | `CLuaVehicleDefs::GetVehicleNameFromModel` | Operational on Android. (GetVehicleNameFromModel) |
| `getVehicleRotation` | **Implemented** | `CLuaCompatibilityDefs.cpp:57` | `CLuaElementDefs::GetElementRotation` | Operational on Android. (l_getElementRotation) |
| `guiCreateBrowser` | **Implemented** | `CLuaBrowserDefs.cpp:51` | `GUICreateBrowser` | Operational on Android. (GuiCreateBrowser) |
| `guiEditSetCaratIndex` | **Implemented** | `CLuaCompatibilityDefs.cpp:68` | `CLuaGUIDefs::GUIEditSetCaretIndex` | Operational on Android. (GuiEditSetCaretIndex) |
| `guiGetBrowser` | **Implemented** | `CLuaBrowserDefs.cpp:52` | `GUIGetBrowser` | Operational on Android. (MissingBrowser) |
| `guiMemoSetCaratIndex` | **Implemented** | `CLuaCompatibilityDefs.cpp:69` | `CLuaGUIDefs::GUIMemoSetCaretIndex` | Operational on Android. (GuiMemoSetCaretIndex) |
| `injectBrowserMouseDown` | **Implemented** | `CLuaBrowserDefs.cpp:26` | `InjectBrowserMouseDown` | Operational on Android. (MissingBrowser) |
| `injectBrowserMouseMove` | **Implemented** | `CLuaBrowserDefs.cpp:25` | `InjectBrowserMouseMove` | Operational on Android. (MissingBrowser) |
| `injectBrowserMouseUp` | **Implemented** | `CLuaBrowserDefs.cpp:27` | `InjectBrowserMouseUp` | Operational on Android. (MissingBrowser) |
| `injectBrowserMouseWheel` | **Implemented** | `CLuaBrowserDefs.cpp:28` | `InjectBrowserMouseWheel` | Operational on Android. (MissingBrowser) |
| `isBrowserDomainBlocked` | **Implemented** | `CLuaBrowserDefs.cpp:36` | `IsBrowserDomainBlocked` | Operational on Android. (IsBrowserDomainBlocked) |
| `isBrowserFocused` | **Implemented** | `CLuaBrowserDefs.cpp:38` | `IsBrowserFocused` | Operational on Android. (MissingBrowser) |
| `isBrowserGPUEnabled` | **Implemented** | `CLuaBrowserDefs.cpp:53` | `ArgumentParser<IsBrowserGPUEnabled>` | Operational on Android. (IsBrowserGPUEnabled) |
| `isBrowserLoading` | **Implemented** | `CLuaBrowserDefs.cpp:24` | `IsBrowserLoading` | Operational on Android. (MissingBrowser) |
| `isBrowserRenderingPaused` | **Implemented** | `CLuaBrowserDefs.cpp:32` | `ArgumentParser<IsBrowserRenderingPaused>` | Operational on Android. (MissingBrowser) |
| `isDiscordRichPresenceConnected` | **Implemented** | `CLuaDiscordDefs.cpp:29` | `ArgumentParser<IsDiscordRPCConnected>` | Operational on Android. (NotReady) |
| `isPedInWater` | **Implemented** | `CLuaCompatibilityDefs.cpp:23` | `CLuaElementDefs::IsElementInWater` | Operational on Android. (IsElementInWater) |
| `isPedOnFire` | **Implemented** | `CLuaCompatibilityDefs.cpp:24` | `CLuaPedDefs::IsPedOnFire` | Operational on Android. (IsPedOnFire) |
| `isPlayerChoking` | **Implemented** | `CLuaCompatibilityDefs.cpp:42` | `CLuaPedDefs::IsPedChoking` | Operational on Android. (IsPedChoking) |
| `isPlayerDead` | **Implemented** | `CLuaCompatibilityDefs.cpp:67` | `CLuaPedDefs::IsPedDead` | Operational on Android. (IsPedDead) |
| `isPlayerDoingTask` | **Implemented** | `CLuaCompatibilityDefs.cpp:29` | `CLuaPedDefs::IsPedDoingTask` | Operational on Android. (IsPedDoingTask) |
| `isPlayerDucked` | **Implemented** | `CLuaCompatibilityDefs.cpp:43` | `CLuaPedDefs::IsPedDucked` | Operational on Android. (IsPedDucked) |
| `isPlayerInVehicle` | **Implemented** | `CLuaCompatibilityDefs.cpp:20` | `CLuaPedDefs::IsPedInVehicle` | Operational on Android. (IsPedInVehicle) |
| `isPlayerInWater` | **Implemented** | `CLuaCompatibilityDefs.cpp:22` | `CLuaElementDefs::IsElementInWater` | Operational on Android. (IsElementInWater) |
| `isPlayerOnGround` | **Implemented** | `CLuaCompatibilityDefs.cpp:26` | `CLuaPedDefs::IsPedOnGround` | Operational on Android. (IsPedOnGround) |
| `loadBrowserURL` | **Implemented** | `CLuaBrowserDefs.cpp:23` | `LoadBrowserURL` | Operational on Android. (MissingBrowser) |
| `navigateBrowserBack` | **Implemented** | `CLuaBrowserDefs.cpp:46` | `NavigateBrowserBack` | Operational on Android. (MissingBrowser) |
| `navigateBrowserForward` | **Implemented** | `CLuaBrowserDefs.cpp:47` | `NavigateBrowserForward` | Operational on Android. (MissingBrowser) |
| `openExternalURL` | **Implemented** | `CLuaBrowserDefs.cpp:22` | `OpenExternalURL` | Operational on Android. (OpenExternalURL) |
| `reloadBrowserPage` | **Implemented** | `CLuaBrowserDefs.cpp:48` | `ReloadBrowserPage` | Operational on Android. (MissingBrowser) |
| `requestBrowserDomains` | **Implemented** | `CLuaBrowserDefs.cpp:21` | `RequestBrowserDomains` | Operational on Android. (RequestBrowserDomains) |
| `resetDiscordRichPresenceData` | **Implemented** | `CLuaDiscordDefs.cpp:28` | `ArgumentParser<ResetData>` | Operational on Android. (NotReady) |
| `resizeBrowser` | **Implemented** | `CLuaBrowserDefs.cpp:50` | `ResizeBrowser` | Operational on Android. (MissingBrowser) |
| `setBrowserAjaxHandler` | **Implemented** | `CLuaBrowserDefs.cpp:43` | `SetBrowserAjaxHandler` | Operational on Android. (MissingBrowser) |
| `setBrowserProperty` | **Implemented** | `CLuaBrowserDefs.cpp:39` | `SetBrowserProperty` | Operational on Android. (MissingBrowser) |
| `setBrowserRenderingPaused` | **Implemented** | `CLuaBrowserDefs.cpp:31` | `SetBrowserRenderingPaused` | Operational on Android. (MissingBrowser) |
| `setBrowserVolume` | **Implemented** | `CLuaBrowserDefs.cpp:35` | `SetBrowserVolume` | Operational on Android. (SetBrowserVolume) |
| `setCameraShakeLevel` | **Implemented** | `CLuaCompatibilityDefs.cpp:72` | `ArgumentParserWarn<false, CLuaCameraDefs::SetCameraDrunkLevel>` | Operational on Android. (l_setCameraDrunkLevel) |
| `setControlState` | **Implemented** | `CLuaCompatibilityDefs.cpp:70` | `ArgumentParserWarn<false, CLuaPedDefs::SetPedControlState>` | Operational on Android. (SetPedControlState) |
| `setDiscordApplicationID` | **Implemented** | `CLuaDiscordDefs.cpp:19` | `ArgumentParser<SetAppID>` | Operational on Android. (SetAppID) |
| `setDiscordRichPresenceAsset` | **Implemented** | `CLuaDiscordDefs.cpp:22` | `ArgumentParser<SetLargeAsset>` | Operational on Android. (SetLargeAsset) |
| `setDiscordRichPresenceButton` | **Implemented** | `CLuaDiscordDefs.cpp:24` | `ArgumentParser<SetButtons>` | Operational on Android. (SetButtons) |
| `setDiscordRichPresenceDetails` | **Implemented** | `CLuaDiscordDefs.cpp:20` | `ArgumentParser<SetDetails>` | Operational on Android. (SetDetails) |
| `setDiscordRichPresenceEndTime` | **Implemented** | `CLuaDiscordDefs.cpp:26` | `ArgumentParser<SetEndTime>` | Operational on Android. (SetEndTime) |
| `setDiscordRichPresencePartySize` | **Implemented** | `CLuaDiscordDefs.cpp:27` | `ArgumentParser<SetPartySize>` | Operational on Android. (SetPartySize) |
| `setDiscordRichPresenceSmallAsset` | **Implemented** | `CLuaDiscordDefs.cpp:23` | `ArgumentParser<SetSmallAsset>` | Operational on Android. (SetSmallAsset) |
| `setDiscordRichPresenceStartTime` | **Implemented** | `CLuaDiscordDefs.cpp:25` | `ArgumentParser<SetStartTime>` | Operational on Android. (SetStartTime) |
| `setDiscordRichPresenceState` | **Implemented** | `CLuaDiscordDefs.cpp:21` | `ArgumentParser<SetState>` | Operational on Android. (SetState) |
| `setObjectRotation` | **Implemented** | `CLuaCompatibilityDefs.cpp:54` | `CLuaElementDefs::SetElementRotation` | Operational on Android. (l_setElementRotation) |
| `setPedOnFire` | **Implemented** | `CLuaCompatibilityDefs.cpp:25` | `CLuaPedDefs::SetPedOnFire` | Operational on Android. (SetPedOnFire) |
| `setPedSkin` | **Implemented** | `CLuaCompatibilityDefs.cpp:52` | `CLuaElementDefs::SetElementModel` | Operational on Android. (SetElementModel) |
| `setPlayerCanBeKnockedOffBike` | **Implemented** | `CLuaCompatibilityDefs.cpp:48` | `CLuaPedDefs::SetPedCanBeKnockedOffBike` | Operational on Android. (SetPedCanBeKnockedOffBike) |
| `setPlayerRotation` | **Implemented** | `CLuaCompatibilityDefs.cpp:47` | `CLuaPedDefs::SetPedRotation` | Operational on Android. (SetPedRotation) |
| `setPlayerSkin` | **Implemented** | `CLuaCompatibilityDefs.cpp:46` | `CLuaElementDefs::SetElementModel` | Operational on Android. (SetElementModel) |
| `setPlayerWeaponSlot` | **Implemented** | `CLuaCompatibilityDefs.cpp:45` | `CLuaPedDefs::SetPedWeaponSlot` | Operational on Android. (SetPedWeaponSlot) |
| `setVehicleModel` | **Implemented** | `CLuaCompatibilityDefs.cpp:49` | `CLuaElementDefs::SetElementModel` | Operational on Android. (SetElementModel) |
| `setVehicleRotation` | **Implemented** | `CLuaCompatibilityDefs.cpp:59` | `CLuaElementDefs::SetElementRotation` | Operational on Android. (l_setElementRotation) |
| `toggleBrowserDevTools` | **Implemented** | `CLuaBrowserDefs.cpp:49` | `ToggleBrowserDevTools` | Operational on Android. (MissingBrowser) |
| `xmlCreateSubNode` | **Implemented** | `CLuaCompatibilityDefs.cpp:65` | `CLuaXMLDefs::xmlCreateChild` | Operational on Android. (l_xmlCreateChild) |
| `xmlFindSubNode` | **Implemented** | `CLuaCompatibilityDefs.cpp:62` | `CLuaXMLDefs::xmlNodeFindChild` | Operational on Android. (l_xmlFindChild) |
| `xmlNodeFindChild` | **Implemented** | `CLuaCompatibilityDefs.cpp:66` | `CLuaXMLDefs::xmlNodeFindChild` | Operational on Android. (l_xmlFindChild) |
| `xmlNodeFindSubNode` | **Implemented** | `CLuaCompatibilityDefs.cpp:64` | `CLuaXMLDefs::xmlNodeFindChild` | Operational on Android. (l_xmlFindChild) |
| `xmlNodeGetSubNodes` | **Implemented** | `CLuaCompatibilityDefs.cpp:63` | `CLuaXMLDefs::xmlNodeGetChildren` | Operational on Android. (l_xmlNodeGetChildren) |
