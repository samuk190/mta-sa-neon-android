#include "VoiceCodecStub.h"

#include <opus.h>
#include <speex/speex.h>
#include <speex/speex_bits.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace WorldRPG::Audio {
namespace {
bool Valid(const VoiceConfig& c) {
    if (!std::isfinite(c.captureGain) || !std::isfinite(c.playbackGain) || c.captureGain < 0 || c.playbackGain < 0)
        return false;
    if (c.codec == VoiceCodec::Speex)
        return c.channels == 1 && c.frameSizeMs == 20 && c.quality <= 10 &&
            (c.sampleRate == 8000 || c.sampleRate == 16000 || c.sampleRate == 32000) && c.bitrate <= 512000;
    if (c.codec != VoiceCodec::Opus || (c.channels != 1 && c.channels != 2) || c.bitrate < 500 || c.bitrate > 512000)
        return false;
    return (c.sampleRate == 8000 || c.sampleRate == 12000 || c.sampleRate == 16000 || c.sampleRate == 24000 || c.sampleRate == 48000) &&
        (c.frameSizeMs == 5 || c.frameSizeMs == 10 || c.frameSizeMs == 20 || c.frameSizeMs == 40 || c.frameSizeMs == 60);
}
int16_t Gain(int16_t value, float gain) {
    return static_cast<int16_t>(std::clamp(static_cast<double>(value) * gain, -32768.0, 32767.0));
}
struct Bits {
    SpeexBits value{};
    Bits() { speex_bits_init(&value); }
    ~Bits() { speex_bits_destroy(&value); }
};
}

VoiceConfig DesktopVoiceConfig(uint8_t rateCode, uint8_t quality, uint32_t bitrate) {
    VoiceConfig c;
    c.codec = VoiceCodec::Speex;
    c.sampleRate = rateCode == 0 ? 8000 : rateCode == 2 ? 32000 : 16000;
    c.frameSizeMs = 20;
    c.channels = 1;
    c.quality = quality;
    c.bitrate = bitrate;
    return c;
}

struct VoiceCodecStub::Impl {
    VoiceConfig config{};
    VoicePacketCallback packetCallback;
    AudioOutputCallback outputCallback;
    OpusEncoder* opusEncoder = nullptr;
    OpusDecoder* opusDecoder = nullptr;
    void* speexEncoder = nullptr;
    void* speexDecoder = nullptr;
    int frameSamples = 0;
    uint64_t encodedFrames = 0;
    bool ready = false;
    void Release() {
        if (opusEncoder) opus_encoder_destroy(opusEncoder);
        if (opusDecoder) opus_decoder_destroy(opusDecoder);
        if (speexEncoder) speex_encoder_destroy(speexEncoder);
        if (speexDecoder) speex_decoder_destroy(speexDecoder);
        opusEncoder = nullptr; opusDecoder = nullptr;
        speexEncoder = nullptr; speexDecoder = nullptr;
        ready = false;
    }
    ~Impl() { Release(); }
};

VoiceCodecStub::VoiceCodecStub() : pImpl_(std::make_unique<Impl>()) {}
VoiceCodecStub::~VoiceCodecStub() = default;

bool VoiceCodecStub::initialize(const VoiceConfig& config) {
    if (!Valid(config)) return false;
    auto next = std::make_unique<Impl>();
    next->config = config;
    next->frameSamples = static_cast<int>(config.sampleRate * config.frameSizeMs / 1000);
    if (config.codec == VoiceCodec::Speex) {
        const int modeId = config.sampleRate == 8000 ? SPEEX_MODEID_NB : config.sampleRate == 16000 ? SPEEX_MODEID_WB : SPEEX_MODEID_UWB;
        const SpeexMode* mode = speex_lib_get_mode(modeId);
        next->speexEncoder = speex_encoder_init(mode);
        next->speexDecoder = speex_decoder_init(mode);
        if (!next->speexEncoder || !next->speexDecoder) return false;
        int quality = config.quality, rate = config.sampleRate, frame = 0, bitrate = config.bitrate;
        if (speex_encoder_ctl(next->speexEncoder, SPEEX_SET_QUALITY, &quality) != 0 ||
            speex_encoder_ctl(next->speexEncoder, SPEEX_SET_SAMPLING_RATE, &rate) != 0 ||
            speex_decoder_ctl(next->speexDecoder, SPEEX_SET_SAMPLING_RATE, &rate) != 0 ||
            speex_encoder_ctl(next->speexEncoder, SPEEX_GET_FRAME_SIZE, &frame) != 0 || frame != next->frameSamples)
            return false;
        if (bitrate && speex_encoder_ctl(next->speexEncoder, SPEEX_SET_BITRATE, &bitrate) != 0) return false;
    } else {
        int error = 0;
        next->opusEncoder = opus_encoder_create(config.sampleRate, config.channels, OPUS_APPLICATION_VOIP, &error);
        if (error != OPUS_OK || !next->opusEncoder) return false;
        next->opusDecoder = opus_decoder_create(config.sampleRate, config.channels, &error);
        if (error != OPUS_OK || !next->opusDecoder) return false;
        if (opus_encoder_ctl(next->opusEncoder, OPUS_SET_BITRATE(config.bitrate)) != OPUS_OK) return false;
        opus_encoder_ctl(next->opusEncoder, OPUS_SET_COMPLEXITY(5));
        opus_encoder_ctl(next->opusEncoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
        opus_encoder_ctl(next->opusEncoder, OPUS_SET_VBR(1));
    }
    next->packetCallback = pImpl_->packetCallback;
    next->outputCallback = pImpl_->outputCallback;
    next->ready = true;
    pImpl_.swap(next); // Reinitialization releases the previous codec only after success.
    return true;
}

bool VoiceCodecStub::isReady() const { return pImpl_->ready; }

std::vector<uint8_t> VoiceCodecStub::encode(const int16_t* samples, size_t count) {
    auto& p = *pImpl_;
    if (!p.ready || !samples || count != static_cast<size_t>(p.frameSamples) * p.config.channels) return {};
    // Speex is allowed to modify its input. Also apply capture gain to a copy.
    std::vector<int16_t> input(samples, samples + count);
    for (auto& value : input) value = Gain(value, p.config.captureGain);
    std::vector<uint8_t> packet(p.config.codec == VoiceCodec::Speex ? 2048 : 4000);
    int length = 0;
    if (p.config.codec == VoiceCodec::Speex) {
        Bits bits;
        if (speex_encode_int(p.speexEncoder, input.data(), &bits.value) < 0) return {};
        length = speex_bits_write(&bits.value, reinterpret_cast<char*>(packet.data()), packet.size());
    } else {
        length = opus_encode(p.opusEncoder, input.data(), p.frameSamples, packet.data(), packet.size());
    }
    if (length <= 0) return {};
    packet.resize(length);
    const uint32_t timestamp = static_cast<uint32_t>(++p.encodedFrames * p.config.frameSizeMs);
    if (p.packetCallback) p.packetCallback(packet.data(), packet.size(), timestamp);
    return packet;
}

std::vector<int16_t> VoiceCodecStub::decode(const uint8_t* data, size_t length) {
    auto& p = *pImpl_;
    const size_t limit = p.config.codec == VoiceCodec::Speex ? 2048 : 4000;
    if (!p.ready || !data || !length || length > limit) return {};
    const int capacity = p.config.codec == VoiceCodec::Speex ? p.frameSamples : static_cast<int>(p.config.sampleRate * 120 / 1000);
    std::vector<int16_t> output(static_cast<size_t>(capacity) * p.config.channels);
    int frames = 0;
    if (p.config.codec == VoiceCodec::Speex) {
        Bits bits;
        speex_bits_read_from(&bits.value, reinterpret_cast<const char*>(data), static_cast<int>(length));
        if (speex_decode_int(p.speexDecoder, &bits.value, output.data()) < 0 || speex_bits_remaining(&bits.value) < 0) return {};
        frames = p.frameSamples;
    } else {
        frames = opus_decode(p.opusDecoder, data, static_cast<opus_int32>(length), output.data(), capacity, 0);
        if (frames <= 0) return {};
    }
    output.resize(static_cast<size_t>(frames) * p.config.channels);
    for (auto& value : output) value = Gain(value, p.config.playbackGain);
    if (p.outputCallback) p.outputCallback(output.data(), frames, p.config.sampleRate);
    return output;
}

void VoiceCodecStub::setPacketCallback(VoicePacketCallback cb) { pImpl_->packetCallback = std::move(cb); }
void VoiceCodecStub::setOutputCallback(AudioOutputCallback cb) { pImpl_->outputCallback = std::move(cb); }
VoiceConfig VoiceCodecStub::getConfig() const { return pImpl_->config; }

void VoiceCodecStub::updateConfig(const VoiceConfig& config) {
    auto& p = *pImpl_;
    if (!p.ready || !Valid(config) || config.codec != p.config.codec || config.sampleRate != p.config.sampleRate ||
        config.channels != p.config.channels || config.frameSizeMs != p.config.frameSizeMs) return;
    if (config.codec == VoiceCodec::Opus) {
        if (opus_encoder_ctl(p.opusEncoder, OPUS_SET_BITRATE(config.bitrate)) != OPUS_OK) return;
    } else {
        int quality = config.quality, bitrate = config.bitrate;
        if (speex_encoder_ctl(p.speexEncoder, SPEEX_SET_QUALITY, &quality) != 0) return;
        if (bitrate && speex_encoder_ctl(p.speexEncoder, SPEEX_SET_BITRATE, &bitrate) != 0) return;
    }
    p.config = config;
}

void VoiceCodecStub::reset() {
    auto& p = *pImpl_;
    if (!p.ready) return;
    if (p.config.codec == VoiceCodec::Opus) {
        opus_encoder_ctl(p.opusEncoder, OPUS_RESET_STATE);
        opus_decoder_ctl(p.opusDecoder, OPUS_RESET_STATE);
    } else {
        speex_encoder_ctl(p.speexEncoder, SPEEX_RESET_STATE, nullptr);
        speex_decoder_ctl(p.speexDecoder, SPEEX_RESET_STATE, nullptr);
    }
    p.encodedFrames = 0;
}
void VoiceCodecStub::shutdown() {
    pImpl_->Release();
    pImpl_->packetCallback = nullptr;
    pImpl_->outputCallback = nullptr;
    pImpl_->encodedFrames = 0;
}
} // namespace WorldRPG::Audio
