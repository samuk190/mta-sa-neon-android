#include "VoiceCodecStub.h"
#include <opus.h>
#include <speex/speex.h>
#include <speex/speex_bits.h>
#include <cmath>
#include <cstdio>
#include <limits>

int main() {
    using namespace WorldRPG::Audio;
    int checks = 0, failures = 0;
    auto check = [&](bool ok, const char* what) {
        ++checks; if (!ok) { ++failures; std::printf("FAIL %s\n", what); }
    };
    for (uint8_t mode = 0; mode < 3; ++mode) {
        const auto config = DesktopVoiceConfig(mode, 8, 0);
        VoiceCodecStub codec;
        check(codec.initialize(config), "Speex init");
        const size_t frames = config.sampleRate / 50;
        std::vector<int16_t> pcm(frames);
        for (size_t i = 0; i < frames; ++i) pcm[i] = static_cast<int16_t>(10000 * std::sin(i * 0.1));
        const auto packet = codec.encode(pcm.data(), pcm.size());
        check(!packet.empty() && packet.size() <= 2048, "Speex wire payload size");
        const SpeexMode* nativeMode = speex_lib_get_mode(mode);
        void* decoder = speex_decoder_init(nativeMode);
        SpeexBits bits;
        speex_bits_init(&bits);
        speex_bits_read_from(&bits, reinterpret_cast<const char*>(packet.data()), packet.size());
        std::vector<int16_t> nativePcm(frames);
        check(speex_decode_int(decoder, &bits, nativePcm.data()) == 0, "payload decodes with desktop Speex API");
        speex_bits_destroy(&bits);
        speex_decoder_destroy(decoder);
        // Produce a packet with the desktop CVoiceRecorder API, without a codec tag.
        void* encoder = speex_encoder_init(nativeMode);
        int quality = 8;
        speex_encoder_ctl(encoder, SPEEX_SET_QUALITY, &quality);
        speex_bits_init(&bits);
        speex_encode_int(encoder, pcm.data(), &bits);
        std::vector<uint8_t> desktop(2048);
        desktop.resize(speex_bits_write(&bits, reinterpret_cast<char*>(desktop.data()), desktop.size()));
        check(codec.decode(desktop.data(), desktop.size()).size() == frames, "desktop packet decodes at negotiated rate");
        speex_bits_destroy(&bits);
        speex_encoder_destroy(encoder);
        int16_t one = 0;
        check(codec.encode(&one, 1).empty(), "short input rejected before native encoder reads it");
        check(codec.encode(pcm.data(), pcm.size() - 1).empty(), "partial frame refused");
        check(codec.decode(desktop.data(), 0).empty() && codec.decode(nullptr, 10).empty(), "empty input");
        check(codec.decode(desktop.data(), 2049).empty(), "oversized Speex packet rejected before reading it");
        uint8_t invalid = 0xff;
        check(codec.decode(&invalid, 1).empty(), "malformed Speex frame");
        auto bad = config; bad.channels = 2;
        check(!codec.initialize(bad) && codec.isReady(), "failed reinit preserves existing codec");
        codec.reset();
        uint32_t timestamp = 0;
        codec.setPacketCallback([&](const uint8_t*, size_t, uint32_t t) { timestamp = t; });
        codec.encode(pcm.data(), pcm.size());
        check(timestamp == 20, "reset timestamp");
        codec.shutdown();
        check(!codec.isReady() && codec.encode(pcm.data(), pcm.size()).empty(), "shutdown");
    }
    for (uint8_t channels : {1, 2}) {
        VoiceConfig config; config.channels = channels;
        VoiceCodecStub codec;
        check(codec.initialize(config), "Opus init");
        std::vector<int16_t> pcm(960 * channels, 1000);
        auto packet = codec.encode(pcm.data(), pcm.size());
        int error = 0;
        OpusDecoder* decoder = opus_decoder_create(48000, channels, &error);
        std::vector<int16_t> decoded(5760 * channels);
        check(!packet.empty() && opus_decode(decoder, packet.data(), packet.size(), decoded.data(), 5760, 0) == 960,
              "Opus wire compatibility");
        opus_decoder_destroy(decoder);
        check(codec.decode(packet.data(), packet.size()).size() == pcm.size(), "Opus roundtrip");
        check(codec.encode(pcm.data(), pcm.size() - 1).empty(), "Opus partial frame refused");
        check(codec.decode(packet.data(), 4001).empty(), "Opus size guard");
        auto bad = config; bad.captureGain = std::numeric_limits<float>::quiet_NaN();
        check(!codec.initialize(bad), "nonfinite gain refused");
        bad = config; bad.sampleRate = 44100;
        check(!codec.initialize(bad), "invalid Opus rate");
    }
    std::printf("neon_voice_tests: %d checks, %d failure(s)\n", checks, failures);
    return failures ? 1 : 0;
}
