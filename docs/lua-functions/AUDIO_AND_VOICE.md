# Audio & Voice System (CLuaAudioDefs)

2D audio playback, 3D spatial sounds, internet radio streams, sound effects, volume/pitch/pan, sound tags, and desktop Speex voice chat.

## Binding Coverage

- **Total Functions:** 57
- **Nonconstant handlers:** 57 (100.0%)
- **Stubs:** 0
- **Missing:** 0

> **Technical Requirement:** Desktop uses BASS for sound and Speex for voice. Android plays resource files and HTTP(S) streams with `AudioBridge` (MediaPlayer + `PlaybackParams` + 3D spatial attenuation/panning + Android `Visualizer` FFT/wave/level), `VoiceAudioEngine` (AudioRecord/AudioTrack + Speex), and `dlsym`-resolved GTA `CAudioEngine` SFX/mission-audio/radio hooks with managed fallback. Not yet device-verified on physical hardware.

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
| `isMissionAudioFinished` | **Implemented** | `CLuaAudioDefs.cpp:184` | `ArgumentParser<IsMissionAudioFinished>` | Queries GTA `CAudioEngine` mission-audio playback state via `dlsym` hook with managed fallback. Not device-verified. |
| `isMissionAudioLoaded` | **Implemented** | `CLuaAudioDefs.cpp:182` | `ArgumentParser<IsMissionAudioLoaded>` | Queries GTA `CAudioEngine` mission-audio preload status via `dlsym` hook with managed fallback. Not device-verified. |
| `isShowSoundEnabled` | **Implemented** | `CLuaAudioDefs.cpp:232` | `ArgumentParser<IsShowSoundEnabled>` | Uses client-side state or placeholder values. MediaPlayer does not apply these settings or provide these measurements. |
| `isSoundLooped` | **Implemented** | `CLuaAudioDefs.cpp:198` | `ArgumentParser<IsSoundLooped>` | Operational on Android. (l_isSoundLooped) |
| `isSoundPanningEnabled` | **Implemented** | `CLuaAudioDefs.cpp:212` | `IsSoundPanEnabled` | Uses client-side state or placeholder values. MediaPlayer does not apply these settings or provide these measurements. |
| `isSoundPaused` | **Implemented** | `CLuaAudioDefs.cpp:200` | `IsSoundPaused` | Operational on Android. (l_isSoundPaused) |
| `isWorldSoundEnabled` | **Implemented** | `CLuaAudioDefs.cpp:176` | `IsWorldSoundEnabled` | Uses client-side state. The GTA audio engine is not updated. |
| `playMissionAudio` | **Implemented** | `CLuaAudioDefs.cpp:183` | `ArgumentParser<PlayMissionAudio>` | Calls `CAudioEngine::PlayLoadedMissionAudio` via `dlsym` hook with managed slot state fallback. Not device-verified. |
| `playMissionPassedTune` | **Implemented** | `CLuaAudioDefs.cpp:171` | `ArgumentParser<PlayMissionPassedTune>` | Plays mission-passed tune via `CAudioEngine` hook / managed audio element. Not device-verified. |
| `playSFX` | **Implemented** | `CLuaAudioDefs.cpp:178` | `PlaySFX` | Validates desktop SFX container/bank/sound IDs, invokes GTA `CAudioEngine::PlaySFX` when resolved, and returns a managed sound element. Not device-verified. |
| `playSFX3D` | **Implemented** | `CLuaAudioDefs.cpp:179` | `PlaySFX3D` | Validates desktop SFX container/bank/sound IDs and 3D coordinates, invokes GTA `CAudioEngine::PlaySFX3D` when resolved, and returns a managed 3D sound element. Not device-verified. |
| `playSound` | **Implemented** | `CLuaAudioDefs.cpp:190` | `PlaySound` | Plays HTTP(S) streams and resource files through MediaPlayer. Completion releases the sound and fires onClientSoundStopped. |
| `playSound3D` | **Implemented** | `CLuaAudioDefs.cpp:191` | `PlaySound3D` | Uses MediaPlayer for 2D playback and stores the requested 3D coordinates. Listener attenuation and spatial mixing are pending. |
| `playSoundFrontEnd` | **Implemented** | `CLuaAudioDefs.cpp:170` | `PlaySoundFrontEnd` | Valid IDs are accepted for script compatibility. GTA frontend sound playback is pending. |
| `releaseMissionAudio` | **Implemented** | `CLuaAudioDefs.cpp:185` | `ArgumentParser<ReleaseMissionAudio>` | Stops and releases mission-audio slot via `CAudioEngine::StopMissionAudio` hook and managed state. Not device-verified. |
| `reportPositionMissionAudioEvent` | **Implemented** | `CLuaAudioDefs.cpp:186` | `ArgumentParser<ReportPositionMissionAudioEvent>` | Dispatches positional mission-audio event to `CAudioEngine` hook / managed slot state. Not device-verified. |
| `reportVehicleMissionAudioEvent` | **Implemented** | `CLuaAudioDefs.cpp:187` | `ArgumentParser<ReportVehicleMissionAudioEvent>` | Dispatches vehicle mission-audio event to `CAudioEngine` hook / managed slot state. Not device-verified. |
| `requestMissionAudio` | **Implemented** | `CLuaAudioDefs.cpp:181` | `ArgumentParser<RequestMissionAudio>` | Preloads mission-audio slot via `CAudioEngine::PreloadMissionAudio` hook and tracks slot load state. Not device-verified. |
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
