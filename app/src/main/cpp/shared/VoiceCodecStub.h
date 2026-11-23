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

enum class VoiceCodec { Opus, Speex };

/// Codec is explicit: packets carry no reliable codec tag; never auto-detect.
struct VoiceConfig {
    uint32_t sampleRate = 48000;   ///< Audio sample rate in Hz
    uint32_t frameSizeMs = 20;     ///< Frame duration in milliseconds
    uint32_t bitrate = 24000;      ///< Target bitrate in bps
    uint8_t channels = 1;          ///< Mono for voice
    float captureGain = 1.0f;      ///< Microphone input gain multiplier
    float playbackGain = 1.0f;     ///< Speaker output gain multiplier
    VoiceCodec codec = VoiceCodec::Opus; // Preserve existing Opus callers.
    uint8_t quality = 8; // Speex quality, 0..10; bitrate 0 uses quality alone.
};

// CVoiceRecorder.cpp:55-124,165-191: stock MTA handshake always selects Speex.
VoiceConfig DesktopVoiceConfig(uint8_t rateCode, uint8_t quality, uint32_t bitrate);

/// Callback for encoded voice packet ready to send over network
using VoicePacketCallback = std::function<void(const uint8_t* data, size_t length, uint32_t timestamp)>;

/// Callback for decoded audio samples ready for playback
using AudioOutputCallback = std::function<void(const int16_t* samples, size_t sampleCount, uint32_t sampleRate)>;

/// Speex/Opus encoder and decoder. Codec selection is connection configuration,
/// not packet sniffing. Capture, playback and transport are separate consumers.
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
    /// @param sampleCount Number of interleaved samples (not bytes); exactly one configured frame
    /// @return Encoded packet data (empty if not ready)
    std::vector<uint8_t> encode(const int16_t* pcmSamples, size_t sampleCount);

    /// Decode compressed voice packet back to PCM samples
    /// @param data Compressed packet data
    /// @param length Packet length in bytes
    /// @return Decoded PCM samples (empty if not ready)
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