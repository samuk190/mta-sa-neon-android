#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct lua_State;

namespace WorldRPG::Script {

struct SoundLifecycleEvent {
    enum class Kind : uint8_t { Started, Stream, Stopped, ChangedMeta, FinishedDownload, Beat };
    Kind kind = Kind::Stopped;
    uint32_t id = 0;
    bool success = true;
    double length = 0.0;
    std::string reasonOrName;
    std::string errorMessage;
};

/// Installs the client audio Lua bindings.
void InstallAudioLua(lua_State* L);

/// Sound element query and lifecycle hooks for LuaVMStub and ElementTree.
bool AudioSoundAlive(uint32_t id);
bool AudioStopSound(uint32_t id);
void AudioNoteSoundEnded(uint32_t id);
void AudioNoteSoundLength(uint32_t id, double seconds);
void AudioNoteSoundStarted(uint32_t id, const char* reason = "play");
void AudioNoteSoundStream(uint32_t id, bool success, double length, const char* streamName = "",
                          const char* errorMsg = "");
void AudioNoteSoundChangedMeta(uint32_t id, const char* streamTitle);
void AudioNoteSoundFinishedDownload(uint32_t id, double length);
void AudioNoteSoundBeat(uint32_t id, double bpm);
void AudioNoteSoundAnalysis(uint32_t id, int leftLevel, int rightLevel,
                            const float* fft, int fftCount,
                            const float* wave, int waveCount);
void AudioUpdateSpatial3D(float listenerX, float listenerY, float listenerZ,
                          float rightX, float rightY, float rightZ);
bool AudioGetEffectiveMix(uint32_t id, float& effectiveVol, float& effectivePan);
void AudioResetSession();
void AudioStopOwnedSounds(const std::string* owner = nullptr, uint16_t netId = 0, uint32_t generation = 0);
std::vector<uint32_t> AudioCollectEndedSounds();
std::vector<SoundLifecycleEvent> AudioCollectEvents();
void AudioPlayFrontEndSound(int soundId);
void AudioSetNativeBackendForTests(bool enabled);

}  // namespace WorldRPG::Script
