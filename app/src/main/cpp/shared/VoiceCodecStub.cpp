#include "VoiceCodecStub.h"

#include <opus.h>

#include <cstring>
#include <algorithm>

namespace WorldRPG::Audio {

struct VoiceCodecStub::Impl {
    bool initialized = false;
    VoiceConfig config{};
    VoicePacketCallback packetCallback;
    AudioOutputCallback outputCallback;
    uint64_t encodeCallCount = 0;
    uint64_t decodeCallCount = 0;

    OpusEncoder* encoder = nullptr;
    OpusDecoder* decoder = nullptr;
};

VoiceCodecStub::VoiceCodecStub() : pImpl_(std::make_unique<Impl>()) {}
VoiceCodecStub::~VoiceCodecStub() { shutdown(); }

bool VoiceCodecStub::initialize(const VoiceConfig& config) {
    if (config.sampleRate == 0 || config.frameSizeMs == 0 || config.channels == 0) {
        VOICE_LOGE("VoiceCodec: invalid config (rate=%u frame=%u ch=%u)",
                   config.sampleRate, config.frameSizeMs, config.channels);
        return false;
    }

    int err = 0;

    // Create Opus encoder
    pImpl_->encoder = opus_encoder_create(
        static_cast<opus_int32>(config.sampleRate),
        static_cast<int>(config.channels),
        OPUS_APPLICATION_VOIP,
        &err);

    if (err != OPUS_OK || !pImpl_->encoder) {
        VOICE_LOGE("VoiceCodec: failed to create Opus encoder (error=%d)", err);
        return false;
    }

    // Configure encoder bitrate
    opus_encoder_ctl(pImpl_->encoder, OPUS_SET_BITRATE(static_cast<opus_int32>(config.bitrate)));
    opus_encoder_ctl(pImpl_->encoder, OPUS_SET_COMPLEXITY(5)); // Medium complexity for mobile
    opus_encoder_ctl(pImpl_->encoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
    opus_encoder_ctl(pImpl_->encoder, OPUS_SET_VBR(1)); // Variable bitrate enabled

    // Create Opus decoder
    pImpl_->decoder = opus_decoder_create(
        static_cast<opus_int32>(config.sampleRate),
        static_cast<int>(config.channels),
        &err);

    if (err != OPUS_OK || !pImpl_->decoder) {
        VOICE_LOGE("VoiceCodec: failed to create Opus decoder (error=%d)", err);
        opus_encoder_destroy(pImpl_->encoder);
        pImpl_->encoder = nullptr;
        return false;
    }

    pImpl_->config = config;
    pImpl_->initialized = true;
    pImpl_->encodeCallCount = 0;
    pImpl_->decodeCallCount = 0;

    VOICE_LOGI("VoiceCodec: initialized with Opus 1.4 (real codec)");
    VOICE_LOGI("  sampleRate=%u frameSizeMs=%u bitrate=%u channels=%u",
               config.sampleRate, config.frameSizeMs, config.bitrate, config.channels);
    return true;
}

bool VoiceCodecStub::isReady() const {
    return pImpl_->initialized && pImpl_->encoder && pImpl_->decoder;
}

std::vector<uint8_t> VoiceCodecStub::encode(const int16_t* pcmSamples, size_t sampleCount) {
    if (!pImpl_->initialized || !pImpl_->encoder || !pcmSamples || sampleCount == 0) {
        return {};
    }

    ++pImpl_->encodeCallCount;

    // Calculate frame size in samples per channel
    int frameSamples = static_cast<int>(pImpl_->config.sampleRate * pImpl_->config.frameSizeMs / 1000);

    // Maximum compressed packet size (Opus can produce up to this)
    constexpr int MAX_PACKET_SIZE = 4000;
    std::vector<uint8_t> packet(MAX_PACKET_SIZE);

    int encodedLen = opus_encode(
        pImpl_->encoder,
        pcmSamples,
        frameSamples,
        packet.data(),
        MAX_PACKET_SIZE);

    if (encodedLen < 0) {
        VOICE_LOGE("VoiceCodec: encode error #%llu (opus error=%d)",
                   (unsigned long long)pImpl_->encodeCallCount, encodedLen);
        return {};
    }

    packet.resize(static_cast<size_t>(encodedLen));

    // Invoke packet callback if registered
    if (pImpl_->packetCallback) {
        uint32_t timestamp = static_cast<uint32_t>(pImpl_->encodeCallCount * pImpl_->config.frameSizeMs);
        pImpl_->packetCallback(packet.data(), packet.size(), timestamp);
    }

    return packet;
}

std::vector<int16_t> VoiceCodecStub::decode(const uint8_t* data, size_t length) {
    if (!pImpl_->initialized || !pImpl_->decoder || !data || length == 0) {
        return {};
    }

    ++pImpl_->decodeCallCount;

    // Maximum decoded frame size (48kHz * 120ms max frame = 5760 samples per channel)
    constexpr int MAX_FRAME_SAMPLES = 5760;
    std::vector<int16_t> pcm(MAX_FRAME_SAMPLES * pImpl_->config.channels);

    int decodedSamples = opus_decode(
        pImpl_->decoder,
        data,
        static_cast<opus_int32>(length),
        pcm.data(),
        MAX_FRAME_SAMPLES,
        0); // No FEC

    if (decodedSamples < 0) {
        VOICE_LOGE("VoiceCodec: decode error #%llu (opus error=%d)",
                   (unsigned long long)pImpl_->decodeCallCount, decodedSamples);
        return {};
    }

    pcm.resize(static_cast<size_t>(decodedSamples) * pImpl_->config.channels);

    // Apply playback gain if not unity
    if (pImpl_->config.playbackGain != 1.0f) {
        for (auto& sample : pcm) {
            int32_t amplified = static_cast<int32_t>(sample * pImpl_->config.playbackGain);
            // Clamp to int16 range
            sample = static_cast<int16_t>(std::max(-32768, std::min(32767, amplified)));
        }
    }

    // Invoke output callback if registered
    if (pImpl_->outputCallback) {
        pImpl_->outputCallback(pcm.data(), pcm.size() / pImpl_->config.channels,
                               pImpl_->config.sampleRate);
    }

    return pcm;
}

void VoiceCodecStub::setPacketCallback(VoicePacketCallback callback) {
    pImpl_->packetCallback = std::move(callback);
}

void VoiceCodecStub::setOutputCallback(AudioOutputCallback callback) {
    pImpl_->outputCallback = std::move(callback);
}

void VoiceCodecStub::updateConfig(const VoiceConfig& config) {
    if (!pImpl_->initialized || !pImpl_->encoder) return;

    // Update bitrate at runtime
    if (config.bitrate != pImpl_->config.bitrate) {
        opus_encoder_ctl(pImpl_->encoder, OPUS_SET_BITRATE(static_cast<opus_int32>(config.bitrate)));
        pImpl_->config.bitrate = config.bitrate;
    }

    // Update gains
    pImpl_->config.captureGain = config.captureGain;
    pImpl_->config.playbackGain = config.playbackGain;

    VOICE_LOGI("VoiceCodec: config updated (gain=%.2f/%.2f bitrate=%u)",
               config.captureGain, config.playbackGain, config.bitrate);
}

VoiceConfig VoiceCodecStub::getConfig() const {
    return pImpl_->config;
}

void VoiceCodecStub::reset() {
    if (!pImpl_->initialized) return;

    // Reset encoder/decoder state without destroying them
    if (pImpl_->encoder) {
        opus_encoder_ctl(pImpl_->encoder, OPUS_RESET_STATE);
    }
    if (pImpl_->decoder) {
        opus_decoder_ctl(pImpl_->decoder, OPUS_RESET_STATE);
    }

    pImpl_->encodeCallCount = 0;
    pImpl_->decodeCallCount = 0;

    VOICE_LOGI("VoiceCodec: reset (cleared counters and codec state)");
}

void VoiceCodecStub::shutdown() {
    if (!pImpl_->initialized) return;

    if (pImpl_->encoder) {
        opus_encoder_destroy(pImpl_->encoder);
        pImpl_->encoder = nullptr;
    }
    if (pImpl_->decoder) {
        opus_decoder_destroy(pImpl_->decoder);
        pImpl_->decoder = nullptr;
    }

    pImpl_->packetCallback = nullptr;
    pImpl_->outputCallback = nullptr;
    pImpl_->initialized = false;

    VOICE_LOGI("VoiceCodec: shutdown (encoded=%llu decoded=%llu)",
               (unsigned long long)pImpl_->encodeCallCount,
               (unsigned long long)pImpl_->decodeCallCount);
}

} // namespace WorldRPG::Audio