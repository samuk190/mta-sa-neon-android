# Vehicles (CLuaVehicleDefs)

Vehicle physics, handling, sirens, doors, colors, lights, engine state, trailers, nitro, gears, and occupant seats.

## Parity Status

- **Total Functions:** 165
- **Implemented:** 0 (0.0%)
- **Stubs:** 23
- **Missing:** 142

> **Technical Requirement:** Requires hooking libGTASA.so `CVehicle` vtable / offsets (`0x3A0000+` in ARM64), handling structures (`tHandlingData`), and vehicle damage packets (33, 56, 58).

## Function List

| Lua Function | Status | Desktop Source Ref | Desktop Handler | Conversion Requirement / Implementation Notes |
|---|---|---|---|---|
| `addVehicleSirens` | Missing | `CLuaVehicleDefs.cpp:166` | `ArgumentParser<AddVehicleSirens>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `addVehicleUpgrade` | Missing | `CLuaVehicleDefs.cpp:165` | `AddVehicleUpgrade` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `areVehicleLightsOn` | Missing | `CLuaVehicleDefs.cpp:84` | `ArgumentParser<AreVehicleLightsOn>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `attachTrailerToVehicle` | Missing | `CLuaVehicleDefs.cpp:174` | `AttachTrailerToVehicle` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `blowVehicle` | Missing | `CLuaVehicleDefs.cpp:153` | `ArgumentParserWarn<false, BlowVehicle>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `createVehicle` | *Stub* | `CLuaVehicleDefs.cpp:151` | `CreateVehicle` | Currently stubbed: `FalseResult: lua_pushboolean(L, 0)`. Needs native engine wiring. |
| `detachTrailerFromVehicle` | Missing | `CLuaVehicleDefs.cpp:175` | `DetachTrailerFromVehicle` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `fixVehicle` | Missing | `CLuaVehicleDefs.cpp:152` | `FixVehicle` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getHeliBladeCollisionsEnabled` | Missing | `CLuaVehicleDefs.cpp:123` | `GetHeliBladeCollisionsEnabled` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getHelicopterRotorSpeed` | Missing | `CLuaVehicleDefs.cpp:100` | `GetHelicopterRotorSpeed` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getOriginalHandling` | Missing | `CLuaVehicleDefs.cpp:115` | `GetOriginalHandling` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getTrainDirection` | Missing | `CLuaVehicleDefs.cpp:105` | `GetTrainDirection` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getTrainPosition` | Missing | `CLuaVehicleDefs.cpp:107` | `GetTrainPosition` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getTrainSpeed` | Missing | `CLuaVehicleDefs.cpp:106` | `GetTrainSpeed` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getVehicleAdjustableProperty` | Missing | `CLuaVehicleDefs.cpp:99` | `GetVehicleAdjustableProperty` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `getVehicleAudioSettings` | Missing | `CLuaVehicleDefs.cpp:141` | `ArgumentParser<GetVehicleAudioSettings>` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `getVehicleColor` | Missing | `CLuaVehicleDefs.cpp:67` | `GetVehicleColor` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `getVehicleCompatibleUpgrades` | Missing | `CLuaVehicleDefs.cpp:80` | `GetVehicleCompatibleUpgrades` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `getVehicleComponentPosition` | Missing | `CLuaVehicleDefs.cpp:125` | `GetVehicleComponentPosition` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `getVehicleComponentRotation` | Missing | `CLuaVehicleDefs.cpp:126` | `GetVehicleComponentRotation` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `getVehicleComponentScale` | Missing | `CLuaVehicleDefs.cpp:127` | `GetVehicleComponentScale` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `getVehicleComponentVisible` | Missing | `CLuaVehicleDefs.cpp:128` | `GetVehicleComponentVisible` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `getVehicleComponents` | Missing | `CLuaVehicleDefs.cpp:129` | `GetVehicleComponents` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `getVehicleController` | *Stub* | `CLuaVehicleDefs.cpp:72` | `GetVehicleController` | Currently stubbed: `DefaultValue(kFalse)`. Needs native engine wiring. |
| `getVehicleCurrentGear` | Missing | `CLuaVehicleDefs.cpp:113` | `GetVehicleCurrentGear` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `getVehicleDoorLockMode` | Missing | `CLuaVehicleDefs.cpp:145` | `ArgumentParser<GetVehicleDoorLockMode>` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `getVehicleDoorOpenRatio` | Missing | `CLuaVehicleDefs.cpp:116` | `GetVehicleDoorOpenRatio` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `getVehicleDoorState` | Missing | `CLuaVehicleDefs.cpp:81` | `GetVehicleDoorState` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `getVehicleDummyPosition` | Missing | `CLuaVehicleDefs.cpp:133` | `ArgumentParser<GetVehicleDummyPosition>` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `getVehicleEngineState` | *Stub* | `CLuaVehicleDefs.cpp:102` | `GetVehicleEngineState` | Currently stubbed: `DefaultValue(kFalse)`. Needs native engine wiring. |
| `getVehicleEntryPoints` | Missing | `CLuaVehicleDefs.cpp:137` | `ArgumentParser<GetVehicleEntryPoints>` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `getVehicleGravity` | Missing | `CLuaVehicleDefs.cpp:109` | `GetVehicleGravity` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `getVehicleHandling` | Missing | `CLuaVehicleDefs.cpp:114` | `GetVehicleHandling` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `getVehicleHeadLightColor` | Missing | `CLuaVehicleDefs.cpp:112` | `GetVehicleHeadLightColor` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `getVehicleLandingGearDown` | Missing | `CLuaVehicleDefs.cpp:68` | `GetVehicleLandingGearDown` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `getVehicleLightState` | Missing | `CLuaVehicleDefs.cpp:82` | `GetVehicleLightState` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `getVehicleMaxPassengers` | Missing | `CLuaVehicleDefs.cpp:69` | `GetVehicleMaxPassengers` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `getVehicleModelAudioSettings` | Missing | `CLuaVehicleDefs.cpp:140` | `ArgumentParser<GetVehicleModelAudioSettings>` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `getVehicleModelDummyDefaultPosition` | Missing | `CLuaVehicleDefs.cpp:132` | `ArgumentParser<GetVehicleModelDummyDefaultPosition>` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `getVehicleModelDummyPosition` | Missing | `CLuaVehicleDefs.cpp:131` | `GetVehicleModelDummyPosition` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `getVehicleModelExhaustFumesPosition` | Missing | `CLuaVehicleDefs.cpp:130` | `GetVehicleModelExhaustFumesPosition` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `getVehicleModelFromName` | Missing | `CLuaVehicleDefs.cpp:65` | `GetVehicleModelFromName` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `getVehicleModelWheelSize` | Missing | `CLuaVehicleDefs.cpp:135` | `ArgumentParser<GetVehicleModelWheelSize>` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `getVehicleName` | Missing | `CLuaVehicleDefs.cpp:97` | `GetVehicleName` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `getVehicleNameFromModel` | Missing | `CLuaVehicleDefs.cpp:98` | `GetVehicleNameFromModel` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `getVehicleNativeAutoPilotDiagnostic` | Missing | `CLuaVehicleDefs.cpp:148` | `GetVehicleNativeAutoPilotDiagnostic` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `getVehicleNitroCount` | Missing | `CLuaVehicleDefs.cpp:121` | `GetVehicleNitroCount` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `getVehicleNitroLevel` | Missing | `CLuaVehicleDefs.cpp:122` | `GetVehicleNitroLevel` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `getVehicleOccupant` | *Stub* | `CLuaVehicleDefs.cpp:70` | `GetVehicleOccupant` | Currently stubbed: `DefaultValue(kFalse)`. Needs native engine wiring. |
| `getVehicleOccupants` | *Stub* | `CLuaVehicleDefs.cpp:71` | `GetVehicleOccupants` | Currently stubbed: `DefaultValue(kEmptyTable)`. Needs native engine wiring. |
| `getVehicleOverrideLights` | *Stub* | `CLuaVehicleDefs.cpp:85` | `GetVehicleOverrideLights` | Currently stubbed: `DefaultValue(kZero)`. Needs native engine wiring. |
| `getVehiclePaintjob` | Missing | `CLuaVehicleDefs.cpp:88` | `GetVehiclePaintjob` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `getVehiclePanelState` | Missing | `CLuaVehicleDefs.cpp:83` | `GetVehiclePanelState` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `getVehiclePlateText` | Missing | `CLuaVehicleDefs.cpp:89` | `GetVehiclePlateText` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `getVehicleRotorSpeed` | Missing | `CLuaVehicleDefs.cpp:101` | `ArgumentParser<GetVehicleRotorSpeed>` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `getVehicleRotorState` | Missing | `CLuaVehicleDefs.cpp:139` | `ArgumentParser<GetVehicleRotorState>` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `getVehicleSirenParams` | Missing | `CLuaVehicleDefs.cpp:118` | `GetVehicleSirenParams` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `getVehicleSirens` | Missing | `CLuaVehicleDefs.cpp:117` | `GetVehicleSirens` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `getVehicleSirensOn` | Missing | `CLuaVehicleDefs.cpp:73` | `GetVehicleSirensOn` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `getVehicleStraightLineDistance` | *Stub* | `CLuaVehicleDefs.cpp:147` | `ArgumentParser<GetVehicleStraightLineDistance>` | Currently stubbed: `DefaultValue(kZero)`. Needs native engine wiring. |
| `getVehicleTowedByVehicle` | Missing | `CLuaVehicleDefs.cpp:86` | `GetVehicleTowedByVehicle` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `getVehicleTowingVehicle` | Missing | `CLuaVehicleDefs.cpp:87` | `GetVehicleTowingVehicle` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `getVehicleTurnVelocity` | Missing | `CLuaVehicleDefs.cpp:74` | `GetVehicleTurnVelocity` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `getVehicleTurretPosition` | Missing | `CLuaVehicleDefs.cpp:75` | `GetVehicleTurretPosition` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `getVehicleType` | *Stub* | `CLuaVehicleDefs.cpp:64` | `GetVehicleType` | Currently stubbed: `l_num0: lua_pushinteger(L, 0)`. Needs native engine wiring. |
| `getVehicleTyresCanBurst` | Missing | `CLuaVehicleDefs.cpp:146` | `ArgumentParser<GetVehicleTyresCanBurst>` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `getVehicleUpgradeOnSlot` | Missing | `CLuaVehicleDefs.cpp:77` | `GetVehicleUpgradeOnSlot` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `getVehicleUpgradeSlotName` | Missing | `CLuaVehicleDefs.cpp:79` | `GetVehicleUpgradeSlotName` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `getVehicleUpgrades` | Missing | `CLuaVehicleDefs.cpp:78` | `GetVehicleUpgrades` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `getVehicleVariant` | Missing | `CLuaVehicleDefs.cpp:66` | `GetVehicleVariant` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `getVehicleWheelFrictionState` | Missing | `CLuaVehicleDefs.cpp:136` | `ArgumentParser<GetVehicleWheelFrictionState>` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `getVehicleWheelScale` | Missing | `CLuaVehicleDefs.cpp:134` | `ArgumentParser<GetVehicleWheelScale>` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `getVehicleWheelStates` | Missing | `CLuaVehicleDefs.cpp:90` | `GetVehicleWheelStates` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `isTrainChainEngine` | Missing | `CLuaVehicleDefs.cpp:108` | `IsTrainChainEngine` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `isTrainDerailable` | Missing | `CLuaVehicleDefs.cpp:104` | `IsTrainDerailable` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `isTrainDerailed` | Missing | `CLuaVehicleDefs.cpp:103` | `IsTrainDerailed` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `isVehicleBlown` | *Stub* | `CLuaVehicleDefs.cpp:110` | `ArgumentParserWarn<false, IsVehicleBlown>` | Currently stubbed: `DefaultValue(kFalse)`. Needs native engine wiring. |
| `isVehicleDamageProof` | Missing | `CLuaVehicleDefs.cpp:92` | `IsVehicleDamageProof` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `isVehicleFrozen` | Missing | `CLuaVehicleDefs.cpp:94` | `IsVehicleFrozen` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `isVehicleFuelTankExplodable` | Missing | `CLuaVehicleDefs.cpp:93` | `IsVehicleFuelTankExplodable` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `isVehicleLocked` | *Stub* | `CLuaVehicleDefs.cpp:76` | `IsVehicleLocked` | Currently stubbed: `DefaultValue(kFalse)`. Needs native engine wiring. |
| `isVehicleNitroActivated` | Missing | `CLuaVehicleDefs.cpp:120` | `IsVehicleNitroActivated` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `isVehicleNitroRecharging` | Missing | `CLuaVehicleDefs.cpp:119` | `IsVehicleNitroRecharging` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `isVehicleOnAllWheels` | Missing | `CLuaVehicleDefs.cpp:96` | `IsVehicleOnAllWheels` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `isVehicleOnGround` | *Stub* | `CLuaVehicleDefs.cpp:95` | `IsVehicleOnGround` | Currently stubbed: `DefaultValue(kFalse)`. Needs native engine wiring. |
| `isVehicleParkingBrakeEnabled` | Missing | `CLuaVehicleDefs.cpp:144` | `ArgumentParser<IsVehicleParkingBrakeEnabled>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `isVehiclePlaybackActive` | *Stub* | `CLuaVehicleDefs.cpp:143` | `ArgumentParser<IsVehiclePlaybackActive>` | Currently stubbed: `DefaultValue(kFalse)`. Needs native engine wiring. |
| `isVehicleRecordingLoaded` | *Stub* | `CLuaVehicleDefs.cpp:142` | `ArgumentParser<IsVehicleRecordingLoaded>` | Currently stubbed: `DefaultValue(kFalse)`. Needs native engine wiring. |
| `isVehicleSmokeTrailEnabled` | Missing | `CLuaVehicleDefs.cpp:138` | `ArgumentParser<IsSmokeTrailEnabled>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `isVehicleTaxiLightOn` | Missing | `CLuaVehicleDefs.cpp:111` | `IsVehicleTaxiLightOn` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `isVehicleWheelOnGround` | Missing | `CLuaVehicleDefs.cpp:91` | `IsVehicleWheelCollided` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `isVehicleWindowOpen` | Missing | `CLuaVehicleDefs.cpp:124` | `IsVehicleWindowOpen` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `loadVehicleRecording` | Missing | `CLuaVehicleDefs.cpp:226` | `ArgumentParser<LoadVehicleRecording>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `removeVehicleSirens` | Missing | `CLuaVehicleDefs.cpp:168` | `ArgumentParser<RemoveVehicleSirens>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `removeVehicleUpgrade` | Missing | `CLuaVehicleDefs.cpp:167` | `RemoveVehicleUpgrade` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `requestVehicleRecording` | *Stub* | `CLuaVehicleDefs.cpp:225` | `ArgumentParser<RequestVehicleRecording>` | Currently stubbed: `DefaultValue(kFalse)`. Needs native engine wiring. |
| `resetVehicleAudioSettings` | Missing | `CLuaVehicleDefs.cpp:224` | `ArgumentParser<ResetVehicleAudioSettings>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `resetVehicleComponentPosition` | Missing | `CLuaVehicleDefs.cpp:201` | `ResetVehicleComponentPosition` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `resetVehicleComponentRotation` | Missing | `CLuaVehicleDefs.cpp:202` | `ResetVehicleComponentRotation` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `resetVehicleComponentScale` | Missing | `CLuaVehicleDefs.cpp:203` | `ResetVehicleComponentScale` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `resetVehicleDummyPositions` | Missing | `CLuaVehicleDefs.cpp:213` | `ArgumentParser<ResetVehicleDummyPositions>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `resetVehicleModelAudioSettings` | Missing | `CLuaVehicleDefs.cpp:222` | `ArgumentParser<ResetVehicleModelAudioSettings>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setHeliBladeCollisionsEnabled` | Missing | `CLuaVehicleDefs.cpp:209` | `SetHeliBladeCollisionsEnabled` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setHelicopterRotorSpeed` | Missing | `CLuaVehicleDefs.cpp:183` | `SetHelicopterRotorSpeed` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setTrainDerailable` | Missing | `CLuaVehicleDefs.cpp:187` | `SetTrainDerailable` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setTrainDerailed` | Missing | `CLuaVehicleDefs.cpp:186` | `SetTrainDerailed` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setTrainDirection` | Missing | `CLuaVehicleDefs.cpp:188` | `SetTrainDirection` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setTrainPosition` | Missing | `CLuaVehicleDefs.cpp:190` | `SetTrainPosition` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setTrainSpeed` | Missing | `CLuaVehicleDefs.cpp:189` | `SetTrainSpeed` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setVehicleAdjustableProperty` | Missing | `CLuaVehicleDefs.cpp:182` | `SetVehicleAdjustableProperty` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `setVehicleAudioSetting` | Missing | `CLuaVehicleDefs.cpp:223` | `ArgumentParser<SetVehicleAudioSetting>` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `setVehicleColor` | *Stub* | `CLuaVehicleDefs.cpp:155` | `SetVehicleColor` | Currently stubbed: `DefaultValue(kTrue)`. Needs native engine wiring. |
| `setVehicleComponentPosition` | Missing | `CLuaVehicleDefs.cpp:198` | `SetVehicleComponentPosition` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `setVehicleComponentRotation` | Missing | `CLuaVehicleDefs.cpp:199` | `SetVehicleComponentRotation` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `setVehicleComponentScale` | Missing | `CLuaVehicleDefs.cpp:200` | `SetVehicleComponentScale` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `setVehicleComponentVisible` | Missing | `CLuaVehicleDefs.cpp:204` | `SetVehicleComponentVisible` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `setVehicleDamageProof` | Missing | `CLuaVehicleDefs.cpp:178` | `SetVehicleDamageProof` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `setVehicleDirtLevel` | Missing | `CLuaVehicleDefs.cpp:177` | `SetVehicleDirtLevel` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `setVehicleDoorLockMode` | Missing | `CLuaVehicleDefs.cpp:158` | `ArgumentParser<SetVehicleDoorLockMode>` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `setVehicleDoorOpenRatio` | Missing | `CLuaVehicleDefs.cpp:195` | `SetVehicleDoorOpenRatio` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `setVehicleDoorState` | Missing | `CLuaVehicleDefs.cpp:169` | `SetVehicleDoorState` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `setVehicleDoorsUndamageable` | Missing | `CLuaVehicleDefs.cpp:163` | `SetVehicleDoorsUndamageable` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `setVehicleDummyPosition` | Missing | `CLuaVehicleDefs.cpp:214` | `ArgumentParser<SetVehicleDummyPosition>` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `setVehicleEngineState` | *Stub* | `CLuaVehicleDefs.cpp:176` | `SetVehicleEngineState` | Currently stubbed: `DefaultValue(kTrue)`. Needs native engine wiring. |
| `setVehicleFrozen` | Missing | `CLuaVehicleDefs.cpp:181` | `SetVehicleFrozen` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `setVehicleFuelTankExplodable` | Missing | `CLuaVehicleDefs.cpp:180` | `SetVehicleFuelTankExplodable` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `setVehicleGravity` | Missing | `CLuaVehicleDefs.cpp:192` | `SetVehicleGravity` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `setVehicleHandling` | Missing | `CLuaVehicleDefs.cpp:196` | `SetVehicleHandling` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `setVehicleHeadLightColor` | Missing | `CLuaVehicleDefs.cpp:193` | `SetVehicleHeadLightColor` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `setVehicleLandingGearDown` | Missing | `CLuaVehicleDefs.cpp:156` | `SetVehicleLandingGearDown` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `setVehicleLightState` | Missing | `CLuaVehicleDefs.cpp:171` | `SetVehicleLightState` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `setVehicleLoadCollisionFlag` | *Stub* | `CLuaVehicleDefs.cpp:161` | `ArgumentParser<SetVehicleLoadCollisionFlag>` | Currently stubbed: `DefaultValue(kTrue)`. Needs native engine wiring. |
| `setVehicleLocked` | Missing | `CLuaVehicleDefs.cpp:157` | `SetVehicleLocked` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `setVehicleModelAudioSetting` | Missing | `CLuaVehicleDefs.cpp:221` | `ArgumentParser<SetVehicleModelAudioSetting>` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `setVehicleModelDummyPosition` | Missing | `CLuaVehicleDefs.cpp:212` | `SetVehicleModelDummyPosition` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `setVehicleModelExhaustFumesPosition` | Missing | `CLuaVehicleDefs.cpp:211` | `SetVehicleModelExhaustFumesPosition` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `setVehicleModelWheelSize` | Missing | `CLuaVehicleDefs.cpp:217` | `ArgumentParser<SetVehicleModelWheelSize>` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `setVehicleNitroActivated` | Missing | `CLuaVehicleDefs.cpp:205` | `SetVehicleNitroActivated` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `setVehicleNitroCount` | Missing | `CLuaVehicleDefs.cpp:206` | `SetVehicleNitroCount` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `setVehicleNitroLevel` | Missing | `CLuaVehicleDefs.cpp:207` | `SetVehicleNitroLevel` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `setVehicleOverrideLights` | *Stub* | `CLuaVehicleDefs.cpp:173` | `SetVehicleOverrideLights` | Currently stubbed: `DefaultValue(kTrue)`. Needs native engine wiring. |
| `setVehiclePaintjob` | Missing | `CLuaVehicleDefs.cpp:179` | `SetVehiclePaintjob` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `setVehiclePanelState` | Missing | `CLuaVehicleDefs.cpp:172` | `SetVehiclePanelState` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `setVehicleParkingBrake` | Missing | `CLuaVehicleDefs.cpp:230` | `ArgumentParser<SetVehicleParkingBrake>` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `setVehiclePhysicalProofs` | Missing | `CLuaVehicleDefs.cpp:160` | `ArgumentParser<SetVehiclePhysicalProofs>` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `setVehiclePlateText` | Missing | `CLuaVehicleDefs.cpp:208` | `SetVehiclePlateText` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `setVehiclePlaybackSpeed` | *Stub* | `CLuaVehicleDefs.cpp:229` | `ArgumentParser<SetVehiclePlaybackSpeed>` | Currently stubbed: `DefaultValue(kTrue)`. Needs native engine wiring. |
| `setVehicleRotorSpeed` | Missing | `CLuaVehicleDefs.cpp:184` | `ArgumentParser<SetVehicleRotorSpeed>` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `setVehicleRotorState` | Missing | `CLuaVehicleDefs.cpp:220` | `ArgumentParser<SetVehicleRotorState>` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `setVehicleSirens` | Missing | `CLuaVehicleDefs.cpp:197` | `SetVehicleSirens` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `setVehicleSirensOn` | Missing | `CLuaVehicleDefs.cpp:164` | `SetVehicleSirensOn` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `setVehicleSmokeTrailEnabled` | Missing | `CLuaVehicleDefs.cpp:219` | `ArgumentParser<SetSmokeTrailEnabled>` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `setVehicleStraightLineDistance` | *Stub* | `CLuaVehicleDefs.cpp:162` | `ArgumentParser<SetVehicleStraightLineDistance>` | Currently stubbed: `DefaultValue(kTrue)`. Needs native engine wiring. |
| `setVehicleTaxiLightOn` | Missing | `CLuaVehicleDefs.cpp:191` | `SetVehicleTaxiLightOn` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `setVehicleTurnVelocity` | Missing | `CLuaVehicleDefs.cpp:154` | `SetVehicleTurnVelocity` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `setVehicleTurretPosition` | Missing | `CLuaVehicleDefs.cpp:194` | `SetVehicleTurretPosition` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `setVehicleTyresCanBurst` | *Stub* | `CLuaVehicleDefs.cpp:159` | `ArgumentParser<SetVehicleTyresCanBurst>` | Currently stubbed: `DefaultValue(kTrue)`. Needs native engine wiring. |
| `setVehicleVariant` | Missing | `CLuaVehicleDefs.cpp:215` | `ArgumentParser<SetVehicleVariant>` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `setVehicleWheelScale` | Missing | `CLuaVehicleDefs.cpp:216` | `ArgumentParser<SetVehicleWheelScale>` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `setVehicleWheelStates` | Missing | `CLuaVehicleDefs.cpp:170` | `SetVehicleWheelStates` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `setVehicleWheelsRotation` | Missing | `CLuaVehicleDefs.cpp:185` | `ArgumentParser<SetVehicleWheelsRotation>` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `setVehicleWindowOpen` | Missing | `CLuaVehicleDefs.cpp:210` | `SetVehicleWindowOpen` | Needs reading/writing CVehicle struct fields in libGTASA.so. |
| `spawnVehicleFlyingComponent` | Missing | `CLuaVehicleDefs.cpp:218` | `ArgumentParser<SpawnVehicleFlyingComponent>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `startVehiclePlayback` | *Stub* | `CLuaVehicleDefs.cpp:227` | `ArgumentParser<StartVehiclePlayback>` | Currently stubbed: `DefaultValue(kFalse)`. Needs native engine wiring. |
| `stopVehiclePlayback` | *Stub* | `CLuaVehicleDefs.cpp:228` | `ArgumentParser<StopVehiclePlayback>` | Currently stubbed: `DefaultValue(kTrue)`. Needs native engine wiring. |
