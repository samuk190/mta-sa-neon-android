# Audio & Voice System (CLuaAudioDefs)

2D audio playback, 3D spatial sounds, internet radio streams, sound effects, volume/pitch/pan, sound tags, and desktop Speex voice chat.

## Binding Coverage

- **Total Functions:** 57
- **Nonconstant handlers:** 47 (82.5%)
- **Stubs:** 10
- **Missing:** 0

> **Technical Requirement:** Desktop uses BASS for sound and Speex for voice. Android plays resource files and HTTP(S) streams with MediaPlayer. Spatial mixing, GTA sound banks, mission audio, and voice capture/playback require additional engine integration.

## Function List

| Lua Function | Status | Desktop Source Ref | Desktop Handler | Conversion Requirement / Implementation Notes |
|---|---|---|---|---|
| `getRadioChannel` | **Implemented** | `CLuaAudioDefs.cpp:227` | `GetRadioChannel` | Uses client-side state. The GTA audio engine is not updated. |
| `getRadioChannelName` | **Implemented** | `CLuaAudioDefs.cpp:228` | `GetRadioChannelName` | Desktop station-name lookup (IDs 0..12); does not tune the GTA radio. Host-tested. |
| `getSFXStatus` | **Implemented** | `CLuaAudioDefs.cpp:180` | `GetSFXStatus` | Returns false for recognized banks and nil for unknown banks. GTA sound-bank playback is unavailable. |
| `getSoundBPM` | **Implemented** | `CLuaAudioDefs.cpp:210` | `GetSoundBPM` | Uses client-side state or placeholder values. MediaPlayer does not apply these settings or provide these measurements. |
| `getSoundBufferLength` | **Implemented** | `CLuaAudioDefs.cpp:196` | `GetSoundBufferLength` | Uses client-side state or placeholder values. MediaPlayer does not apply these settings or provide these measurements. |
| `getSoundEffectParameters` | **Implemented** | `CLuaAudioDefs.cpp:221` | `GetSoundEffectParameters` | Uses client-side state or placeholder values. MediaPlayer does not apply these settings or provide these measurements. |
| `getSoundEffects` | **Implemented** | `CLuaAudioDefs.cpp:219` | `GetSoundEffects` | Uses client-side state or placeholder values. MediaPlayer does not apply these settings or provide these measurements. |
| `getSoundFFTData` | **Implemented** | `CLuaAudioDefs.cpp:207` | `GetSoundFFTData` | Uses client-side state or placeholder values. MediaPlayer does not apply these settings or provide these measurements. |
| `getSoundLength` | **Implemented** | `CLuaAudioDefs.cpp:195` | `GetSoundLength` | 2D MediaPlayer duration in seconds, reported asynchronously; 0 while unknown. Player voice overload is not ported. Host-tested bridge; not device-verified. |
| `getSoundLevelData` | **Implemented** | `CLuaAudioDefs.cpp:209` | `GetSoundLevelData` | Uses client-side state or placeholder values. MediaPlayer does not apply these settings or provide these measurements. |
| `getSoundMaxDistance` | **Implemented** | `CLuaAudioDefs.cpp:216` | `GetSoundMaxDistance` | Uses client-side state or placeholder values. MediaPlayer does not apply these settings or provide these measurements. |
| `getSoundMetaTags` | **Implemented** | `CLuaAudioDefs.cpp:217` | `GetSoundMetaTags` | Uses client-side state or placeholder values. MediaPlayer does not apply these settings or provide these measurements. |
| `getSoundMinDistance` | **Implemented** | `CLuaAudioDefs.cpp:214` | `GetSoundMinDistance` | Uses client-side state or placeholder values. MediaPlayer does not apply these settings or provide these measurements. |
| `getSoundPan` | **Implemented** | `CLuaAudioDefs.cpp:223` | `GetSoundPan` | 2D sound pan is applied to MediaPlayer left/right gains. Player voice overload is not ported. Android compiles; not device-verified. |
| `getSoundPosition` | **Implemented** | `CLuaAudioDefs.cpp:194` | `GetSoundPosition` | Operational on Android. (l_getSoundPosition) |
| `getSoundProperties` | **Implemented** | `CLuaAudioDefs.cpp:206` | `GetSoundProperties` | Uses client-side state or placeholder values. MediaPlayer does not apply these settings or provide these measurements. |
| `getSoundSpeed` | **Implemented** | `CLuaAudioDefs.cpp:204` | `GetSoundSpeed` | Uses client-side state or placeholder values. MediaPlayer does not apply these settings or provide these measurements. |
| `getSoundVolume` | **Implemented** | `CLuaAudioDefs.cpp:202` | `GetSoundVolume` | Operational on Android. (l_getSoundVolume) |
| `getSoundWaveData` | **Implemented** | `CLuaAudioDefs.cpp:208` | `GetSoundWaveData` | Uses client-side state or placeholder values. MediaPlayer does not apply these settings or provide these measurements. |
| `isAmbientSoundEnabled` | **Implemented** | `CLuaAudioDefs.cpp:173` | `IsAmbientSoundEnabled` | Uses client-side state. The GTA audio engine is not updated. |
| `isMissionAudioFinished` | *Stub* | `CLuaAudioDefs.cpp:184` | `ArgumentParser<IsMissionAudioFinished>` | Returns false. Requires integration with GTA sound banks or mission audio. |
| `isMissionAudioLoaded` | *Stub* | `CLuaAudioDefs.cpp:182` | `ArgumentParser<IsMissionAudioLoaded>` | Returns false. Requires integration with GTA sound banks or mission audio. |
| `isShowSoundEnabled` | **Implemented** | `CLuaAudioDefs.cpp:232` | `ArgumentParser<IsShowSoundEnabled>` | Uses client-side state or placeholder values. MediaPlayer does not apply these settings or provide these measurements. |
| `isSoundLooped` | **Implemented** | `CLuaAudioDefs.cpp:198` | `ArgumentParser<IsSoundLooped>` | Operational on Android. (l_isSoundLooped) |
| `isSoundPanningEnabled` | **Implemented** | `CLuaAudioDefs.cpp:212` | `IsSoundPanEnabled` | Uses client-side state or placeholder values. MediaPlayer does not apply these settings or provide these measurements. |
| `isSoundPaused` | **Implemented** | `CLuaAudioDefs.cpp:200` | `IsSoundPaused` | Operational on Android. (l_isSoundPaused) |
| `isWorldSoundEnabled` | **Implemented** | `CLuaAudioDefs.cpp:176` | `IsWorldSoundEnabled` | Uses client-side state. The GTA audio engine is not updated. |
| `playMissionAudio` | *Stub* | `CLuaAudioDefs.cpp:183` | `ArgumentParser<PlayMissionAudio>` | Returns false. Requires integration with GTA sound banks or mission audio. |
| `playMissionPassedTune` | *Stub* | `CLuaAudioDefs.cpp:171` | `ArgumentParser<PlayMissionPassedTune>` | Returns false. Requires integration with GTA sound banks or mission audio. |
| `playSFX` | *Stub* | `CLuaAudioDefs.cpp:178` | `PlaySFX` | Returns false. Requires integration with GTA sound banks or mission audio. |
| `playSFX3D` | *Stub* | `CLuaAudioDefs.cpp:179` | `PlaySFX3D` | Returns false. Requires integration with GTA sound banks or mission audio. |
| `playSound` | **Implemented** | `CLuaAudioDefs.cpp:190` | `PlaySound` | Plays HTTP(S) streams and resource files through MediaPlayer. Completion releases the sound and fires onClientSoundStopped. |
| `playSound3D` | **Implemented** | `CLuaAudioDefs.cpp:191` | `PlaySound3D` | Uses MediaPlayer for 2D playback and stores the requested 3D coordinates. Listener attenuation and spatial mixing are pending. |
| `playSoundFrontEnd` | **Implemented** | `CLuaAudioDefs.cpp:170` | `PlaySoundFrontEnd` | Valid IDs are accepted for script compatibility. GTA frontend sound playback is pending. |
| `releaseMissionAudio` | *Stub* | `CLuaAudioDefs.cpp:185` | `ArgumentParser<ReleaseMissionAudio>` | Returns false. Requires integration with GTA sound banks or mission audio. |
| `reportPositionMissionAudioEvent` | *Stub* | `CLuaAudioDefs.cpp:186` | `ArgumentParser<ReportPositionMissionAudioEvent>` | Returns false. Requires integration with GTA sound banks or mission audio. |
| `reportVehicleMissionAudioEvent` | *Stub* | `CLuaAudioDefs.cpp:187` | `ArgumentParser<ReportVehicleMissionAudioEvent>` | Returns false. Requires integration with GTA sound banks or mission audio. |
| `requestMissionAudio` | *Stub* | `CLuaAudioDefs.cpp:181` | `ArgumentParser<RequestMissionAudio>` | Returns false. Requires integration with GTA sound banks or mission audio. |
| `resetAmbientSounds` | **Implemented** | `CLuaAudioDefs.cpp:174` | `ResetAmbientSounds` | Uses client-side state. The GTA audio engine is not updated. |
| `resetWorldSounds` | **Implemented** | `CLuaAudioDefs.cpp:177` | `ResetWorldSounds` | Uses client-side state. The GTA audio engine is not updated. |
| `setAmbientSoundEnabled` | **Implemented** | `CLuaAudioDefs.cpp:172` | `SetAmbientSoundEnabled` | Uses client-side state. The GTA audio engine is not updated. |
| `setRadioChannel` | **Implemented** | `CLuaAudioDefs.cpp:226` | `SetRadioChannel` | Uses client-side state. The GTA audio engine is not updated. |
| `setSoundEffectEnabled` | **Implemented** | `CLuaAudioDefs.cpp:218` | `SetSoundEffectEnabled` | Uses client-side state or placeholder values. MediaPlayer does not apply these settings or provide these measurements. |
| `setSoundEffectParameter` | **Implemented** | `CLuaAudioDefs.cpp:220` | `SetSoundEffectParameter` | Uses client-side state or placeholder values. MediaPlayer does not apply these settings or provide these measurements. |
| `setSoundLooped` | **Implemented** | `CLuaAudioDefs.cpp:197` | `ArgumentParser<SetSoundLooped>` | Operational on Android. (l_setSoundLooped) |
| `setSoundMaxDistance` | **Implemented** | `CLuaAudioDefs.cpp:215` | `SetSoundMaxDistance` | Uses client-side state or placeholder values. MediaPlayer does not apply these settings or provide these measurements. |
| `setSoundMinDistance` | **Implemented** | `CLuaAudioDefs.cpp:213` | `SetSoundMinDistance` | Uses client-side state or placeholder values. MediaPlayer does not apply these settings or provide these measurements. |
| `setSoundPan` | **Implemented** | `CLuaAudioDefs.cpp:222` | `SetSoundPan` | 2D sound pan is applied to MediaPlayer left/right gains. Player voice overload is not ported. Android compiles; not device-verified. |
| `setSoundPanningEnabled` | **Implemented** | `CLuaAudioDefs.cpp:211` | `SetSoundPanEnabled` | Uses client-side state or placeholder values. MediaPlayer does not apply these settings or provide these measurements. |
| `setSoundPaused` | **Implemented** | `CLuaAudioDefs.cpp:199` | `SetSoundPaused` | Operational on Android. (l_setSoundPaused) |
| `setSoundPosition` | **Implemented** | `CLuaAudioDefs.cpp:193` | `SetSoundPosition` | Operational on Android. (l_setSoundPosition) |
| `setSoundProperties` | **Implemented** | `CLuaAudioDefs.cpp:205` | `SetSoundProperties` | Uses client-side state or placeholder values. MediaPlayer does not apply these settings or provide these measurements. |
| `setSoundSpeed` | **Implemented** | `CLuaAudioDefs.cpp:203` | `SetSoundSpeed` | Uses client-side state or placeholder values. MediaPlayer does not apply these settings or provide these measurements. |
| `setSoundVolume` | **Implemented** | `CLuaAudioDefs.cpp:201` | `SetSoundVolume` | Operational on Android. (l_setSoundVolume) |
| `setWorldSoundEnabled` | **Implemented** | `CLuaAudioDefs.cpp:175` | `SetWorldSoundEnabled` | Uses client-side state. The GTA audio engine is not updated. |
| `showSound` | **Implemented** | `CLuaAudioDefs.cpp:231` | `ArgumentParser<ShowSound>` | Uses client-side state or placeholder values. MediaPlayer does not apply these settings or provide these measurements. |
| `stopSound` | **Implemented** | `CLuaAudioDefs.cpp:192` | `StopSound` | Operational on Android. (l_stopSound) |
