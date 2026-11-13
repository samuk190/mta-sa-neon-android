# Audio & Voice System (CLuaAudioDefs)

2D audio playback, 3D spatial sounds, internet radio streams, sound effects, volume/pitch/pan, sound tags, and Opus voice chat.

## Parity Status

- **Total Functions:** 57
- **Implemented:** 0 (0.0%)
- **Stubs:** 8
- **Missing:** 49

> **Technical Requirement:** Desktop uses BASS library. Mobile has Opus 1.4 compiled; requires OpenSL ES / AudioTrack output mixer, URL streaming, and 3D listener attenuation math.

## Function List

| Lua Function | Status | Desktop Source Ref | Desktop Handler | Conversion Requirement / Implementation Notes |
|---|---|---|---|---|
| `getRadioChannel` | Missing | `CLuaAudioDefs.cpp:227` | `GetRadioChannel` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getRadioChannelName` | Missing | `CLuaAudioDefs.cpp:228` | `GetRadioChannelName` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getSFXStatus` | Missing | `CLuaAudioDefs.cpp:180` | `GetSFXStatus` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getSoundBPM` | Missing | `CLuaAudioDefs.cpp:210` | `GetSoundBPM` | Needs AudioTrack / OpenSL ES audio playback mixer. |
| `getSoundBufferLength` | Missing | `CLuaAudioDefs.cpp:196` | `GetSoundBufferLength` | Needs AudioTrack / OpenSL ES audio playback mixer. |
| `getSoundEffectParameters` | Missing | `CLuaAudioDefs.cpp:221` | `GetSoundEffectParameters` | Needs AudioTrack / OpenSL ES audio playback mixer. |
| `getSoundEffects` | Missing | `CLuaAudioDefs.cpp:219` | `GetSoundEffects` | Needs AudioTrack / OpenSL ES audio playback mixer. |
| `getSoundFFTData` | Missing | `CLuaAudioDefs.cpp:207` | `GetSoundFFTData` | Needs AudioTrack / OpenSL ES audio playback mixer. |
| `getSoundLength` | Missing | `CLuaAudioDefs.cpp:195` | `GetSoundLength` | Needs AudioTrack / OpenSL ES audio playback mixer. |
| `getSoundLevelData` | Missing | `CLuaAudioDefs.cpp:209` | `GetSoundLevelData` | Needs AudioTrack / OpenSL ES audio playback mixer. |
| `getSoundMaxDistance` | Missing | `CLuaAudioDefs.cpp:216` | `GetSoundMaxDistance` | Needs AudioTrack / OpenSL ES audio playback mixer. |
| `getSoundMetaTags` | Missing | `CLuaAudioDefs.cpp:217` | `GetSoundMetaTags` | Needs AudioTrack / OpenSL ES audio playback mixer. |
| `getSoundMinDistance` | Missing | `CLuaAudioDefs.cpp:214` | `GetSoundMinDistance` | Needs AudioTrack / OpenSL ES audio playback mixer. |
| `getSoundPan` | Missing | `CLuaAudioDefs.cpp:223` | `GetSoundPan` | Needs AudioTrack / OpenSL ES audio playback mixer. |
| `getSoundPosition` | Missing | `CLuaAudioDefs.cpp:194` | `GetSoundPosition` | Needs AudioTrack / OpenSL ES audio playback mixer. |
| `getSoundProperties` | Missing | `CLuaAudioDefs.cpp:206` | `GetSoundProperties` | Needs AudioTrack / OpenSL ES audio playback mixer. |
| `getSoundSpeed` | Missing | `CLuaAudioDefs.cpp:204` | `GetSoundSpeed` | Needs AudioTrack / OpenSL ES audio playback mixer. |
| `getSoundVolume` | Missing | `CLuaAudioDefs.cpp:202` | `GetSoundVolume` | Needs AudioTrack / OpenSL ES audio playback mixer. |
| `getSoundWaveData` | Missing | `CLuaAudioDefs.cpp:208` | `GetSoundWaveData` | Needs AudioTrack / OpenSL ES audio playback mixer. |
| `isAmbientSoundEnabled` | Missing | `CLuaAudioDefs.cpp:173` | `IsAmbientSoundEnabled` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `isMissionAudioFinished` | Missing | `CLuaAudioDefs.cpp:184` | `ArgumentParser<IsMissionAudioFinished>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `isMissionAudioLoaded` | Missing | `CLuaAudioDefs.cpp:182` | `ArgumentParser<IsMissionAudioLoaded>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `isShowSoundEnabled` | Missing | `CLuaAudioDefs.cpp:232` | `ArgumentParser<IsShowSoundEnabled>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `isSoundLooped` | Missing | `CLuaAudioDefs.cpp:198` | `ArgumentParser<IsSoundLooped>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `isSoundPanningEnabled` | Missing | `CLuaAudioDefs.cpp:212` | `IsSoundPanEnabled` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `isSoundPaused` | Missing | `CLuaAudioDefs.cpp:200` | `IsSoundPaused` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `isWorldSoundEnabled` | Missing | `CLuaAudioDefs.cpp:176` | `IsWorldSoundEnabled` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `playMissionAudio` | Missing | `CLuaAudioDefs.cpp:183` | `ArgumentParser<PlayMissionAudio>` | Needs AudioTrack / OpenSL ES audio playback mixer. |
| `playMissionPassedTune` | Missing | `CLuaAudioDefs.cpp:171` | `ArgumentParser<PlayMissionPassedTune>` | Needs AudioTrack / OpenSL ES audio playback mixer. |
| `playSFX` | Missing | `CLuaAudioDefs.cpp:178` | `PlaySFX` | Needs AudioTrack / OpenSL ES audio playback mixer. |
| `playSFX3D` | Missing | `CLuaAudioDefs.cpp:179` | `PlaySFX3D` | Needs AudioTrack / OpenSL ES audio playback mixer. |
| `playSound` | *Stub* | `CLuaAudioDefs.cpp:190` | `PlaySound` | Currently stubbed: `DefaultValue(kFalse)`. Needs native engine wiring. |
| `playSound3D` | *Stub* | `CLuaAudioDefs.cpp:191` | `PlaySound3D` | Currently stubbed: `DefaultValue(kFalse)`. Needs native engine wiring. |
| `playSoundFrontEnd` | *Stub* | `CLuaAudioDefs.cpp:170` | `PlaySoundFrontEnd` | Currently stubbed: `DefaultValue(kFalse)`. Needs native engine wiring. |
| `releaseMissionAudio` | Missing | `CLuaAudioDefs.cpp:185` | `ArgumentParser<ReleaseMissionAudio>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `reportPositionMissionAudioEvent` | Missing | `CLuaAudioDefs.cpp:186` | `ArgumentParser<ReportPositionMissionAudioEvent>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `reportVehicleMissionAudioEvent` | Missing | `CLuaAudioDefs.cpp:187` | `ArgumentParser<ReportVehicleMissionAudioEvent>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `requestMissionAudio` | Missing | `CLuaAudioDefs.cpp:181` | `ArgumentParser<RequestMissionAudio>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `resetAmbientSounds` | Missing | `CLuaAudioDefs.cpp:174` | `ResetAmbientSounds` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `resetWorldSounds` | Missing | `CLuaAudioDefs.cpp:177` | `ResetWorldSounds` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setAmbientSoundEnabled` | Missing | `CLuaAudioDefs.cpp:172` | `SetAmbientSoundEnabled` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setRadioChannel` | Missing | `CLuaAudioDefs.cpp:226` | `SetRadioChannel` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setSoundEffectEnabled` | Missing | `CLuaAudioDefs.cpp:218` | `SetSoundEffectEnabled` | Needs AudioTrack / OpenSL ES audio playback mixer. |
| `setSoundEffectParameter` | Missing | `CLuaAudioDefs.cpp:220` | `SetSoundEffectParameter` | Needs AudioTrack / OpenSL ES audio playback mixer. |
| `setSoundLooped` | Missing | `CLuaAudioDefs.cpp:197` | `ArgumentParser<SetSoundLooped>` | Needs AudioTrack / OpenSL ES audio playback mixer. |
| `setSoundMaxDistance` | *Stub* | `CLuaAudioDefs.cpp:215` | `SetSoundMaxDistance` | Currently stubbed: `DefaultValue(kTrue)`. Needs native engine wiring. |
| `setSoundMinDistance` | *Stub* | `CLuaAudioDefs.cpp:213` | `SetSoundMinDistance` | Currently stubbed: `DefaultValue(kTrue)`. Needs native engine wiring. |
| `setSoundPan` | Missing | `CLuaAudioDefs.cpp:222` | `SetSoundPan` | Needs AudioTrack / OpenSL ES audio playback mixer. |
| `setSoundPanningEnabled` | Missing | `CLuaAudioDefs.cpp:211` | `SetSoundPanEnabled` | Needs AudioTrack / OpenSL ES audio playback mixer. |
| `setSoundPaused` | Missing | `CLuaAudioDefs.cpp:199` | `SetSoundPaused` | Needs AudioTrack / OpenSL ES audio playback mixer. |
| `setSoundPosition` | *Stub* | `CLuaAudioDefs.cpp:193` | `SetSoundPosition` | Currently stubbed: `DefaultValue(kTrue)`. Needs native engine wiring. |
| `setSoundProperties` | Missing | `CLuaAudioDefs.cpp:205` | `SetSoundProperties` | Needs AudioTrack / OpenSL ES audio playback mixer. |
| `setSoundSpeed` | Missing | `CLuaAudioDefs.cpp:203` | `SetSoundSpeed` | Needs AudioTrack / OpenSL ES audio playback mixer. |
| `setSoundVolume` | *Stub* | `CLuaAudioDefs.cpp:201` | `SetSoundVolume` | Currently stubbed: `DefaultValue(kTrue)`. Needs native engine wiring. |
| `setWorldSoundEnabled` | Missing | `CLuaAudioDefs.cpp:175` | `SetWorldSoundEnabled` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `showSound` | Missing | `CLuaAudioDefs.cpp:231` | `ArgumentParser<ShowSound>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `stopSound` | *Stub* | `CLuaAudioDefs.cpp:192` | `StopSound` | Currently stubbed: `DefaultValue(kTrue)`. Needs native engine wiring. |
