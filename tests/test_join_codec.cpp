// Host wire-equivalence tests for the Neon join codec.
//
// Reference semantics are ported from backend_mta/scripts/mobile_handshake.hpp
// and mobile_resources.hpp — the production-proven bit-level parser/rewriter
// that interoperated with real MTA server packets (MSB-first stream packing,
// little-endian integer bytes, RakNet right-aligned partial fields, RakNet
// unsigned compressed integers).
// Pre-include std headers, then expose the pImpl for white-box wire tests.
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>
#define private public
#include "../app/src/main/cpp/net/NeonJoinProtocol.cpp"
#undef private

#include <cstdio>
#include <cstring>
#include <vector>

using namespace WorldRPG::Net;

static int g_failures = 0;

#define CHECK(cond, msg)                                           \
    do {                                                           \
        if (!(cond)) {                                             \
            printf("FAIL: %s (line %d)\n", msg, __LINE__);         \
            ++g_failures;                                          \
        }                                                          \
    } while (0)

// --- reference (ported from backend_mta/scripts) ---------------------------

static bool ref_bit_at(const uint8_t* data, uint32_t bit) {
    return (data[bit / 8] >> (7 - bit % 8)) & 1;
}

struct RefReader {
    const uint8_t* data_;
    uint32_t bits_;
    uint32_t off_ = 0;
    bool read(uint32_t count, uint32_t& value) {
        if (count == 0 || count > 32 || count > bits_ - off_) return false;
        uint8_t bytes[4] = {0, 0, 0, 0};
        for (uint32_t i = 0; i < count; ++i)
            if (ref_bit_at(data_, off_ + i)) bytes[i / 8] |= 0x80u >> (i % 8);
        if (count % 8) bytes[count / 8] >>= 8 - count % 8;
        value = 0;
        for (unsigned i = 0; i < 4; ++i) value |= uint32_t(bytes[i]) << (i * 8);
        off_ += count;
        return true;
    }
    bool skip(uint32_t count) {
        if (count > bits_ - off_) return false;
        off_ += count;
        return true;
    }
    bool compressed_u32(uint32_t& value) {
        uint32_t flag = 0;
        for (unsigned bytes = 4; bytes > 1; --bytes) {
            if (!read(1, flag)) return false;
            if (!flag) return read(bytes * 8, value);
        }
        if (!read(1, flag)) return false;
        return read(flag ? 4 : 8, value);
    }
    uint32_t offset() const { return off_; }
};

struct RefWriter {
    std::vector<uint8_t> data;
    uint32_t bits = 0;
    bool append_bits(uint32_t value, unsigned count) {
        data.resize((bits + count + 7) / 8, 0);
        for (unsigned i = 0; i < count; ++i) {
            if ((value >> (count - 1 - i)) & 1) data[bits / 8] |= 0x80u >> (bits % 8);
            ++bits;
        }
        return true;
    }
    bool append_bytes(const uint8_t* src, size_t n) {
        for (size_t i = 0; i < n; ++i) append_bits(src[i], 8);
        return true;
    }
    bool append_compressed_u32(uint32_t v) {
        uint8_t bytes[4];
        memcpy(bytes, &v, 4);
        for (int cb = 3; cb > 0; --cb) {
            if (bytes[cb] == 0) {
                append_bits(1, 1);
            } else {
                append_bits(0, 1);
                append_bytes(bytes, cb + 1);
                return true;
            }
        }
        if ((bytes[0] & 0xF0) == 0) {
            append_bits(1, 1);
            append_bits(bytes[0] & 0x0F, 4);
        } else {
            append_bits(0, 1);
            append_bits(bytes[0], 8);
        }
        return true;
    }
    // RakNet WriteBits(rightAligned) for an integer field of `count` bits
    // stored little-endian: full bytes flow as-is; the trailing partial byte
    // contributes its low (count%8) bits, most significant first.
    bool append_field(uint32_t v, unsigned count) {
        uint8_t bytes[4];
        memcpy(bytes, &v, 4);
        const unsigned full = count / 8, rem = count % 8;
        append_bytes(bytes, full);
        if (rem) append_bits(v >> (full * 8), rem);
        return true;
    }
    bool append_string(const std::string& s) {
        uint16_t len = static_cast<uint16_t>(s.size());
        append_bytes(reinterpret_cast<const uint8_t*>(&len), 2);
        append_bytes(reinterpret_cast<const uint8_t*>(s.data()), s.size());
        return true;
    }
};

// --- tests ------------------------------------------------------------------

static void TestU16LittleEndian() {
    BitStreamWriter w;
    w.Write(static_cast<uint16_t>(NEON_NETCODE_VERSION));
    CHECK(w.GetByteLength() == 2, "u16 is 2 bytes");
    CHECK(w.GetData()[0] == 0xE6 && w.GetData()[1] == 0x41, "u16 LE on the wire (0x41E6 -> E6 41)");
    RefReader r{w.GetData(), static_cast<uint32_t>(w.GetBitLength())};
    uint32_t v = 0;
    CHECK(r.read(16, v) && v == 0x41E6, "reference reader decodes our u16");
}

static void TestSingleBit() {
    for (uint8_t bit : {0, 1}) {
        BitStreamWriter w;
        w.WriteBits(&bit, 1);
        CHECK(w.GetBitLength() == 1, "1-bit write length");
        CHECK(ref_bit_at(w.GetData(), 0) == (bit ? 1 : 0), "1-bit value matches reference");
    }
}

static void TestCompressedU32() {
    const uint32_t values[] = {0, 1, 5, 15, 127, 128, 255, 256, 1000, 65535,
                               65536, 16777215, 16777216, 0x7FFFFFFF, 0xFFFFFFFF};
    for (uint32_t v : values) {
        RefWriter ref;
        ref.append_compressed_u32(v);

        BitStreamWriter w;
        WriteCompressedU32(w, v);
        CHECK(w.GetBitLength() == ref.bits, "compressed bit length matches reference");
        const size_t nb = (ref.bits + 7) / 8;
        CHECK(w.GetByteLength() == nb && memcmp(w.GetData(), ref.data.data(), nb) == 0,
              "compressed bytes match reference writer");

        RefReader rr{w.GetData(), static_cast<uint32_t>(w.GetBitLength())};
        uint32_t decoded = 999;
        CHECK(rr.compressed_u32(decoded) && decoded == v, "reference reader decodes our compressed u32");

        BitStreamReader our(ref.data.data(), nb);
        uint32_t decoded2 = 999;
        CHECK(ReadCompressedU32(our, decoded2) && decoded2 == v, "our reader decodes reference compressed u32");
    }
}

static void TestElementId17Bits() {
    const uint32_t ids[] = {0, 1, 352, 373, 1000, 131070, 131071 /*INVALID*/};
    for (uint32_t id : ids) {
        // Our writer -> reference reader
        uint8_t le[4] = {0, 0, 0, 0};
        memcpy(le, &id, 4);
        BitStreamWriter w;
        w.WriteBits(le, NEON_ELEMENT_ID_BITS);
        RefReader rr{w.GetData(), static_cast<uint32_t>(w.GetBitLength())};
        uint32_t v = 7;
        CHECK(rr.read(NEON_ELEMENT_ID_BITS, v), "reference reads 17-bit field");
        CHECK(v == id, "17-bit LE field matches reference semantics");

        // Reference writer -> our reader
        RefWriter ref;
        ref.append_field(id, NEON_ELEMENT_ID_BITS);
        std::vector<uint8_t> buf((ref.bits + 7) / 8 + 1, 0);
        memcpy(buf.data(), ref.data.data(), (ref.bits + 7) / 8);
        BitStreamReader our(buf.data(), buf.size());
        uint32_t out = 7;
        CHECK(ReadElementID(our, out), "our ElementID read succeeds");
        CHECK(out == (id == 131071 ? 0xFFFFFFFF : id), "our ElementID matches (all-ones -> INVALID)");
    }
}

static void TestStringLayout() {
    const std::string s = "1.7.0-1.00000.0";
    RefWriter ref;
    ref.append_string(s);
    BitStreamWriter w;
    WriteStringRaw(w, s);
    const size_t nb = (ref.bits + 7) / 8;
    CHECK(w.GetByteLength() == nb && memcmp(w.GetData(), ref.data.data(), nb) == 0,
          "WriteString = [u16 LE len][chars] matches reference");
}

static void TestSmallFields() {
    // SIntegerSync<uchar,2> then SIntegerSync<uchar,4> back to back
    RefWriter ref;
    ref.append_bits(2, 2);   // sampleRate
    ref.append_bits(11, 4);  // quality
    std::vector<uint8_t> buf(2, 0);
    memcpy(buf.data(), ref.data.data(), (ref.bits + 7) / 8);
    BitStreamReader r(buf.data(), buf.size());
    uint8_t a = 0, b = 0;
    CHECK(ReadSmallUInt(r, 2, a) && a == 2, "2-bit sync value");
    CHECK(ReadSmallUInt(r, 4, b) && b == 11, "4-bit sync value");

    // our writer must produce the same bits
    BitStreamWriter w;
    uint8_t va = 2, vb = 11;
    w.WriteBits(&va, 2);
    w.WriteBits(&vb, 4);
    CHECK(w.GetBitLength() == 6 && memcmp(w.GetData(), ref.data.data(), 1) == 0,
          "partial-byte writes match reference (right-aligned, MSB-first)");
}

static void TestFixedPadding() {
    BitStreamWriter w;
    WriteFixedRaw(w, "Tester", NEON_MAX_PLAYER_NICK_LENGTH);
    CHECK(w.GetByteLength() == NEON_MAX_PLAYER_NICK_LENGTH, "fixed field is exactly N bytes");
    CHECK(memcmp(w.GetData(), "Tester", 6) == 0, "fixed field starts with the value");
    bool zeroed = true;
    for (size_t i = 6; i < NEON_MAX_PLAYER_NICK_LENGTH; ++i) zeroed &= (w.GetData()[i] == 0);
    CHECK(zeroed, "fixed field zero-padded");
}

// Builds a SERVER_JOINEDGAME body exactly per
// Server/mods/deathmatch/logic/packets/CPlayerJoinCompletePacket.cpp and
// checks the full parse through the real join state machine.
static void TestServerJoinedGameParse() {
    RefWriter ref;
    ref.append_field(352, NEON_ELEMENT_ID_BITS);   // player id
    ref.append_bits(1, 8);                          // numPlayers (compat byte)
    ref.append_field(373, NEON_ELEMENT_ID_BITS);   // root element id
    ref.append_field(1, 32);                        // enableClientChecks (int32 LE)
    ref.append_bits(1, 1);                          // voice enabled
    ref.append_bits(2, 2);                          // sampleRate SIntegerSync<uchar,2>
    ref.append_bits(11, 4);                         // quality SIntegerSync<uchar,4>
    ref.append_compressed_u32(65536);               // bitrate
    ref.append_bits(0, 1);                          // fakelag
    ref.append_field(4, 32);                        // httpMaxConnections (int32 LE)
    ref.append_bits(1, 8);                          // httpDownloadType = PORT
    ref.append_field(22005, 16);                    // http port (u16 LE)
    ref.append_string("WorldRPG Neon");             // server name

    const size_t nb = (ref.bits + 7) / 8;

    NeonJoinClient::Impl impl;  // unity include grants access
    impl.net = nullptr;         // sends fail -> proves parse completed first
    impl.HandleServerJoinedGame(ref.data.data(), nb);

    CHECK(impl.info.localPlayerId == 352, "player id parsed");
    CHECK(impl.info.rootElementId == 373, "root element id parsed");
    CHECK(impl.info.enableClientChecks == 1, "enableClientChecks parsed");
    CHECK(impl.info.voiceEnabled, "voice flag parsed");
    CHECK(impl.info.voiceSampleRate == 2, "voice sample rate parsed");
    CHECK(impl.info.voiceQuality == 11, "voice quality parsed");
    CHECK(impl.info.voiceBitrate == 65536, "compressed bitrate parsed");
    CHECK(!impl.info.fakeLagCommand, "fakelag flag parsed");
    CHECK(impl.info.httpMaxConnections == 4, "http max connections parsed");
    CHECK(impl.info.httpDownloadType == 1 && impl.info.httpPort == 22005, "http port parsed");
    CHECK(impl.info.serverName == "WorldRPG Neon", "server name parsed");
    CHECK(impl.info.lastError == "failed to send RPC INITIAL_DATA_STREAM",
          "parse reached the RPC send step");
}

static void TestModNameParse() {
    RefWriter ref;
    ref.append_field(NEON_BITSTREAM_VERSION, 16);
    ref.append_string("deathmatch");
    ref.append_bits(0, 8);  // ENeonAuthMode::Disabled

    NeonJoinClient::Impl impl;
    impl.net = nullptr;
    impl.HandleModName(ref.data.data(), (ref.bits + 7) / 8);

    CHECK(impl.info.modName == "deathmatch", "mod name parsed");
    CHECK(impl.info.serverBitStreamVersion == NEON_BITSTREAM_VERSION, "server bitstream version parsed");
    CHECK(impl.info.authMode == NeonAuthMode::Disabled, "auth mode parsed");
    CHECK(impl.state == JoinState::Failed &&
              impl.info.lastError == "failed to send PLAYER_JOINDATA",
          "MOD_NAME accepted; flow advanced to JOINDATA send");
}

static void TestModNameRequiredAuthRefused() {
    RefWriter ref;
    ref.append_field(NEON_BITSTREAM_VERSION, 16);
    ref.append_string("deathmatch");
    ref.append_bits(2, 8);  // ENeonAuthMode::Required
    ref.append_string("server-id-123");

    NeonJoinClient::Impl impl;
    impl.net = nullptr;
    impl.HandleModName(ref.data.data(), (ref.bits + 7) / 8);
    CHECK(impl.state == JoinState::Failed, "Required auth without identity fails fast");
    CHECK(impl.info.lastError.find("SA:O") != std::string::npos, "failure reason mentions SA:O identity");
}

static void TestTransportFailureEndsAttempt() {
    RakNetWrapper net;
    NeonJoinClient join;
    int failures = 0;
    join.SetStateCallback([&](JoinState state, const char*) {
        if (state == JoinState::Failed) ++failures;
    });
    join.OnTransportFailure("no attempt");
    CHECK(join.State() == JoinState::Idle && failures == 0, "idle transport has no join to fail");
    CHECK(join.Begin(net, "Tester", ""), "begin transport failure scenario");
    join.OnTransportFailure("Connection attempt failed");
    CHECK(join.State() == JoinState::Failed, "transport failure ends waiting join immediately");
    join.pImpl_->deadlineMs = 0;  // the old join would now report another timeout
    join.Pulse();
    join.OnTransportFailure("Connection lost");
    join.ProcessGamePacket(static_cast<uint8_t>(NeonPacketID::SERVER_DISCONNECTED), nullptr, 0);
    CHECK(failures == 1 && join.Info().lastError == "Connection attempt failed",
          "timeout and late packets preserve one terminal cause");
    CHECK(join.Begin(net, "Tester", ""), "retry starts a fresh attempt");
    join.OnTransportFailure("retry failure");
    CHECK(failures == 2 && join.Info().lastError == "retry failure", "retry has its own failure");
    CHECK(join.Begin(net, "Tester", ""), "begin session loss scenario");
    join.pImpl_->state = JoinState::Complete;
    join.OnTransportFailure("Connection lost");
    CHECK(failures == 3 && join.Info().lastError == "Connection lost", "session loss still reaches UI");
}

int main() {
    TestU16LittleEndian();
    TestSingleBit();
    TestCompressedU32();
    TestElementId17Bits();
    TestStringLayout();
    TestSmallFields();
    TestFixedPadding();
    TestServerJoinedGameParse();
    TestModNameParse();
    TestModNameRequiredAuthRefused();
    TestTransportFailureEndsAttempt();
    if (g_failures == 0) {
        printf("ALL JOIN CODEC WIRE TESTS PASSED\n");
        return 0;
    }
    printf("%d FAILURES\n", g_failures);
    return 1;
}
