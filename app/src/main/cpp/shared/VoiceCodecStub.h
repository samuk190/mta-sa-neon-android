#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#ifdef __ANDROID__
#include <android/log.h>
#define VOICE_LOG_TAG "WorldRPGNeon"
#define VOICE_LOGI(...) __android_log_print(ANDROID_LOG_INFO, VOICE_LOG_TAG, __VA_ARGS__)
#define VOICE_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, VOICE_LOG_TAG, __VA_ARGS__)
#else
#include <cstdio>
#define VOICE_LOGI(...) do { printf(__VA_ARGS__); printf("\n"); } while(0)
#define VOICE_LOGE(...) do { fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } while(0)
#endif

namespace WorldRPG::Audio {

/// Voice codec configuration matching MTA's Opus-based voice chat parameters
struct VoiceConfig {
    uint32_t sampleRate = 48000;   ///< Audio sample rate in Hz
    uint32_t frameSizeMs = 20;     ///< Frame duration in milliseconds
    uint32_t bitrate = 24000;      ///< Target bitrate in bps
    uint8_t channels = 1;          ///< Mono for voice
    float captureGain = 1.0f;      ///< Microphone input gain multiplier
    float playbackGain = 1.0f;     ///< Speaker output gain multiplier
};

/// Callback for encoded voice packet ready to send over network
using VoicePacketCallback = std::function<void(const uint8_t* data, size_t length, uint32_t timestamp)>;

/// Callback for decoded audio samples ready for playback
using AudioOutputCallback = std::function<void(const int16_t* samples, size_t sampleCount, uint32_t sampleRate)>;

/// Stub voice codec interface for MTA-compatible voice chat.
/// Provides the API surface that will be backed by libopus in a future phase.
/// Currently logs all encode/decode calls for protocol validation without processing audio.
class VoiceCodecStub {
public:
    VoiceCodecStub();
    ~VoiceCodecStub();

    VoiceCodecStub(const VoiceCodecStub&) = delete;
    VoiceCodecStub& operator=(const VoiceCodecStub&) = delete;

    /// Initialize codec with given configuration
    bool initialize(const VoiceConfig& config);

    /// Check if codec is initialized and ready
    bool isReady() const;

    /// Encode raw PCM audio samples into compressed voice packet
    /// @param pcmSamples Interleaved 16-bit PCM samples
    /// @param sampleCount Number of samples (not bytes)
    /// @return Encoded packet data (empty if stub/not ready)
    std::vector<uint8_t> encode(const int16_t* pcmSamples, size_t sampleCount);

    /// Decode compressed voice packet back to PCM samples
    /// @param data Compressed packet data
    /// @param length Packet length in bytes
    /// @return Decoded PCM samples (empty if stub/not ready)
    std::vector<int16_t> decode(const uint8_t* data, size_t length);

    /// Set callback for when encoded packets are ready (push mode)
    void setPacketCallback(VoicePacketCallback callback);

    /// Set callback for when decoded audio is ready for playback (push mode)
    void setOutputCallback(AudioOutputCallback callback);

    /// Update runtime configuration (gain, bitrate) without reinitializing
    void updateConfig(const VoiceConfig& config);

    /// Get current configuration
    VoiceConfig getConfig() const;

    /// Reset codec state (clear buffers, reset encoder/decoder)
    void reset();

    /// Shutdown and release resources
    void shutdown();

private:
    struct Impl;
    std::unique_ptr<Impl> pImpl_;
};

} // namespace WorldRPG::Audio