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
#include <thread>
#include <string>
#include <vector>
#define private public
#include "../app/src/main/cpp/net/NeonJoinProtocol.cpp"
#include "../app/src/main/cpp/net/NetJoinEnvelope.h"
#undef private
#include "../tools/headless/send_tap.h"
#include "../app/src/main/cpp/shared/entity/EntityAddTypes.h"

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
    CHECK(w.GetData()[0] == 0xE7 && w.GetData()[1] == 0x41, "u16 LE on the wire (0x41E7 -> E7 41)");
    RefReader r{w.GetData(), static_cast<uint32_t>(w.GetBitLength())};
    uint32_t v = 0;
    CHECK(r.read(16, v) && v == 0x41E7, "reference reader decodes our u16");
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
    impl.HandleServerJoinedGame(ref.data.data(), nb, ref.bits);

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
    CHECK(impl.state == JoinState::Failed && impl.info.lastError == DisconnectMessages::TransportFailure(DisconnectMessages::Transport::Other, false).text,
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
              impl.info.lastError == DisconnectMessages::TransportFailure(DisconnectMessages::Transport::Other, false).text,
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

// Inbound sync that CNetAPI consumes during STATUS_JOINING must not abort the
// join or move it past the packet it is actually waiting for.
static void TestInGameNoticeIgnoresPureSync() {
    const uint8_t body[] = {0};
    NeonJoinClient notice;
    notice.pImpl_->state = JoinState::InGameNoticeSent;
    CHECK(notice.ProcessGamePacket(32, body, sizeof(body), 8),
          "PLAYER_PURESYNC during ingame-notice-sent is consumed");
    CHECK(notice.State() == JoinState::InGameNoticeSent,
          "PLAYER_PURESYNC does not advance or fail ingame-notice-sent");
    CHECK(notice.Info().lastError.empty(), "ignored pure sync leaves no join error");

    CHECK(notice.ProcessGamePacket(35, body, sizeof(body), 8),
          "VEHICLE_RESYNC during ingame-notice-sent is consumed");
    CHECK(notice.State() == JoinState::InGameNoticeSent,
          "VEHICLE_RESYNC does not advance ingame-notice-sent");

    notice.ProcessGamePacket(static_cast<uint8_t>(NeonPacketID::SERVER_JOINEDGAME), body, sizeof(body), 8);
    CHECK(notice.State() == JoinState::Failed &&
              notice.Info().lastError == DisconnectMessages::ProtocolError(3).text,
          "packet 22 is still the packet that advances ingame-notice-sent");

    NeonJoinClient joining;
    joining.pImpl_->state = JoinState::JoinDataSent;
    CHECK(joining.ProcessGamePacket(32, body, sizeof(body), 8),
          "PLAYER_PURESYNC during joindata-sent is consumed");
    CHECK(joining.State() == JoinState::JoinDataSent,
          "PLAYER_PURESYNC does not advance joindata-sent");

    NeonJoinClient connecting;
    connecting.pImpl_->state = JoinState::WaitingModName;
    connecting.ProcessGamePacket(32, body, sizeof(body), 8);
    CHECK(connecting.State() == JoinState::Failed &&
              connecting.Info().lastError == DisconnectMessages::ProtocolError(4).text,
          "pure sync before MOD_NAME still fails");

    NeonJoinClient listed;
    listed.pImpl_->state = JoinState::InGameNoticeSent;
    listed.ProcessGamePacket(25, body, sizeof(body), 8);
    CHECK(listed.State() == JoinState::Failed &&
              listed.Info().lastError == DisconnectMessages::ProtocolError(4).text,
          "PLAYER_LIST before STATUS_JOINED still fails");
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
    CHECK(failures == 1 && join.Info().lastError == DisconnectMessages::TransportFailure(DisconnectMessages::ClassifyTransport("Connection attempt failed"), false).text,
          "timeout and late packets preserve one terminal cause");
    CHECK(join.Begin(net, "Tester", ""), "retry starts a fresh attempt");
    join.OnTransportFailure("retry failure");
    CHECK(failures == 2 && join.Info().lastError == DisconnectMessages::TransportFailure(DisconnectMessages::Transport::Other, false).text, "retry has its own failure");
    CHECK(join.Begin(net, "Tester", ""), "begin session loss scenario");
    join.pImpl_->state = JoinState::Complete;
    join.OnTransportFailure("Connection lost");
    CHECK(failures == 3 && join.Info().lastError == DisconnectMessages::TransportFailure(DisconnectMessages::ClassifyTransport("Connection lost"), true).text, "session loss still reaches UI");
}

static void TestPcTransportErrorDispatch() {
    RakNetWrapper net;
    NeonJoinClient join;
    std::string popup;
    join.SetStateCallback([&](JoinState state, const char* detail) {
        if (state == JoinState::Failed) popup = detail;
    });
    net.SetConnectionStateCallback([&](bool connected, const char* reason) {
        if (!connected) join.OnTransportFailure(reason);
    });
    CHECK(join.Begin(net, "Tester", ""), "start PC loss scenario");
    join.pImpl_->state = JoinState::Complete;
    CHECK(net.StartPcNetwork("localhost", 1, "fixture", "", ""), "load host PC error fixture");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (popup.empty() && std::chrono::steady_clock::now() < deadline) {
        net.DoPulse();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(popup == DisconnectMessages::Pack({"Error [CD16]", "Disconnected: connection to the server was lost"}),
          "PC module loss 20 reaches the joined client's popup as CD16");

    CHECK(join.Begin(net, "Tester", ""), "start explicit server refusal scenario");
    BitStreamWriter refusal;
    const uint8_t type = DisconnectMessages::GENERAL_REFUSED;
    refusal.WriteBits(&type, 5);
    WriteStringRaw(refusal, "Test refusal");
    WriteStringRaw(refusal, "");
    join.ProcessGamePacket(static_cast<uint8_t>(NeonPacketID::SERVER_DISCONNECTED),
                           refusal.GetData(), refusal.GetByteLength(), refusal.GetBitLength());
    CHECK(popup == DisconnectMessages::Pack({"Disconnected [CD43]", "Disconnected: Server refused the connection: Test refusal"}),
          "a server-issued refusal keeps the server's reason and CD43");
}

static void TestNetworkJoinEnvelope() {
    BitStreamWriter game, envelope;
    const std::string serial = "0123456789ABCDEF0123456789ABCDEF";
    CHECK(BuildJoinData(JoinProfile::Standard,"Tester","",std::string(1023,'T')+"Q",game), "standard game body with odd final signature byte");
    CHECK(BuildNetJoinEnvelope(game,serial,envelope), "network join envelope");
    // Independent bit-level reference; this header was accepted by unmodified
    // ARM64 net.so on the Oracle test server on 2026-11-16 (no mobile adapter).
    const std::string reference = "043000e0e3e5e9f1c1a161e0e39e908eb8da18f0f3f5f9e1d1b171f0f38e809ea8ca084000e5e6e0ecf4c4a464e5e69b958bbddf1df5f6f0fce4d4b474f5f68b859badcf0dc5c6c0ccd4e48444c5c6bbb5ab9dff3dd5d6d0dcc4f49454d5d6aba5bb8def2d021c0092f75018716f540000";
    CHECK(ToHex(envelope.GetData(),113) == reference, "network header matches independent accepted vector");
    CHECK(envelope.GetBitLength() == game.GetBitLength()+112*8, "body bit length retained");
    CHECK(std::memcmp(envelope.GetData()+113,game.GetData()+1,game.GetByteLength()-1)==0,
          "unaligned ticket/body bytes retained after network metadata");
    BitStreamWriter invalid;
    CHECK(!BuildNetJoinEnvelope(game,"bad",invalid), "invalid serial rejected");
    BitStreamWriter legacy;
    CHECK(BuildJoinData(JoinProfile::LegacyMobile,"Tester","","",legacy), "legacy body");
    CHECK(!BuildNetJoinEnvelope(legacy,serial,invalid), "legacy is never wrapped as standard");
    RakNetWrapper net;
    CHECK(net.SetSerial(serial), "app-scoped serial accepted");
    char actual[33]{};
    net.GetSerial(actual,sizeof(actual));
    CHECK(serial == actual && !net.SetSerial("invalid"), "serial retained and validated");
}

static void TestJoinProfilesAndData() {
    RakNetWrapper net;
    NeonJoinClient join;
    for (const auto requested : {JoinProfile::Automatic, JoinProfile::LegacyMobile, JoinProfile::Standard,
                                 static_cast<JoinProfile>(255), JoinProfile::Automatic}) {
        // Each attempt resolves its profile anew, including a retry after legacy.
        CHECK(join.Begin(net, "Tester", "secret", requested), "join attempt starts");
        const auto profile = join.Info().joinProfile;
        BitStreamWriter packet;
        const bool legacy = requested == JoinProfile::LegacyMobile;
        CHECK(profile == (legacy ? JoinProfile::LegacyMobile : JoinProfile::Standard), "only explicit legacy selects the adapter");
        const std::string ticket = legacy ? "" : std::string(1024, 'T');
        CHECK(BuildJoinData(profile, "Tester", "secret", ticket, packet), "join data constructed");
        if (legacy) {
            CHECK(packet.GetByteLength() == 763 && packet.GetData()[0] == 19, "legacy retains captured envelope size/id");
            CHECK(std::memcmp(packet.GetData() + 676, "1.6.0-", 6) == 0, "legacy marker offset retained");
            for (size_t i = 1; i <= 667; ++i) CHECK(packet.GetData()[i] == 0, "legacy padding retained");
        }
        RefReader reader{packet.GetData(), static_cast<uint32_t>(packet.GetBitLength())};
        uint32_t value = 0;
        CHECK(reader.read(8, value) && value == (legacy ? 19u : 4u), "logical packet id");
        if (legacy) CHECK(reader.skip(667*8), "skip legacy envelope");
        CHECK(reader.read(16, value) && value == (profile == JoinProfile::Standard ? 0x41E7 : 0x41E6), "netcode");
        CHECK(reader.read(16, value) && value == 0x0170, "MTA version");
        CHECK(reader.read(16, value) && value == (legacy ? 0x3Du : 0x30u), "honest standard bitstream, captured legacy bitstream");
        CHECK(reader.read(16, value) && value == 15, "version string length");
        const std::string version = legacy ? "1.6.0-9.29999.0" : "1.7.0-9.29999.0";
        for (const unsigned char c : version) CHECK(reader.read(8, value) && value == c, "version bytes");
        CHECK(reader.read(1, value) && value == 0, "optional-update flag");
        CHECK(reader.read(8, value) && value == 20, "game version");
        for (size_t i=0; i<22; ++i) CHECK(reader.read(8, value) && value == (i < 6 ? static_cast<unsigned char>("Tester"[i]) : 0u), "fixed nickname bytes");
        const unsigned char expectedMd5[16] = {0x5e,0xbe,0x22,0x94,0xec,0xd0,0xe0,0xf0,0x8e,0xab,0x76,0x90,0xd2,0xa6,0xee,0x69};
        for (const auto byte : expectedMd5) CHECK(reader.read(8, value) && value == byte, "server password MD5 reference vector");
        constexpr char androidPlatform[32] = "NEON:ANDROID";
        for (const unsigned char byte : androidPlatform)
            CHECK(reader.read(8, value) && value == byte, "platform marker and padding");
        if (!legacy) {
            CHECK(reader.read(16, value) && value == ticket.size(), "unaligned trailing ticket prefix");
            for (const unsigned char c : ticket) CHECK(reader.read(8, value) && value == c, "ticket preserved past old 256-byte limit");
        }
        CHECK(reader.offset() == packet.GetBitLength(), "all fields consumed without padding drift");
    }
    BitStreamWriter oversized, legacyAuth, invalid;
    CHECK(!BuildJoinData(JoinProfile::Standard, "Tester", "", std::string(4097, 'T'), oversized), "oversized ticket rejected");
    CHECK(!BuildJoinData(JoinProfile::LegacyMobile, "Tester", "", "ticket", legacyAuth), "legacy never truncates an auth ticket");
    CHECK(!BuildJoinData(JoinProfile::Automatic, "Tester", "", "", invalid), "automatic must be resolved before serializing");
    {
        // Release 189 servers admit only netcode 0x41E6; the rest of the standard body is unchanged.
        BitStreamWriter current, release189, unresolved;
        CHECK(BuildJoinData(JoinProfile::Standard, "Tester", "", "", current, NetcodeRelease::Current), "current release body");
        CHECK(BuildJoinData(JoinProfile::Standard, "Tester", "", "", release189, NetcodeRelease::Release189), "release 189 body");
        CHECK(!BuildJoinData(JoinProfile::Standard, "Tester", "", "", unresolved, NetcodeRelease::Automatic),
              "automatic release must be resolved before serializing");
        RefReader a{current.GetData(), static_cast<uint32_t>(current.GetBitLength())};
        RefReader b{release189.GetData(), static_cast<uint32_t>(release189.GetBitLength())};
        uint32_t id = 0, netcode = 0;
        CHECK(a.read(8, id) && a.read(16, netcode) && netcode == 0x41E7, "current netcode");
        CHECK(b.read(8, id) && b.read(16, netcode) && netcode == 0x41E6, "release 189 netcode");
        CHECK(current.GetBitLength() == release189.GetBitLength() &&
                  std::memcmp(current.GetData() + 3, release189.GetData() + 3, current.GetByteLength() - 3) == 0,
              "only the netcode differs between releases");
    }
}

static void TestIdentityRequests() {
    RakNetWrapper net;
    NeonJoinClient join;
    uint64_t requestId = 0;
    join.SetIdentityCallback([&](uint64_t id, const std::string& server, const std::string&, NeonAuthMode mode) {
        requestId = id;
        CHECK(server == "server-id-123" && mode == NeonAuthMode::Required, "auth request bound to server and mode");
    });
    CHECK(join.Begin(net, "Tester", ""), "begin identity attempt");
    RefWriter name;
    name.append_field(0x3D,16);
    name.append_string("deathmatch");
    name.append_bits(2,8);
    name.append_string("server-id-123");
    join.pImpl_->HandleModName(name.data.data(), name.data.size());
    CHECK(requestId != 0 && join.State() == JoinState::WaitingIdentity, "required login awaits asynchronous ticket");
    CHECK(!join.ProvideIdentityTicket(requestId+1,"ticket",""), "stale response rejected");
    CHECK(join.CancelIdentityRequest(requestId), "browser login cancels the pending handshake");
    CHECK(join.State() == JoinState::Idle && !join.ProvideIdentityTicket(requestId,"ticket",""), "canceled request cannot continue");
    CHECK(join.Begin(net,"Tester",""), "reconnect after browser login");
    const uint64_t oldId = requestId;
    join.pImpl_->HandleModName(name.data.data(), name.data.size());
    CHECK(requestId != oldId, "new connection gets a fresh request id");
    CHECK(!join.ProvideIdentityTicket(oldId,"ticket",""), "old ticket cannot authorize a new connection");
    CHECK(join.ProvideIdentityTicket(requestId,"",""), "empty required ticket handled");
    CHECK(join.State() == JoinState::Failed && join.Info().lastError.find("Sign in") != std::string::npos, "required auth never falls back to anonymous");
}

static void TestWaterEntityWire() {
    using namespace WorldRPG::Entity;
    EntityCommon common{};
    common.type = kTypeWater;
    for (uint8_t count : {3, 4}) {
        for (unsigned flags = 0; flags < 4; ++flags) {
            BitStreamWriter body;
            body.Write(count);
            for (int16_t i = 0; i < count; ++i) {
                body.Write(static_cast<uint16_t>(-i));
                body.Write(static_cast<uint16_t>(i));
                body.Write(static_cast<float>(i) + 0.5f);
            }
            body.WriteBit((flags & 2) != 0);
            body.WriteBit((flags & 1) != 0);
            const size_t bits = 8 + 64 * count + 2;
            BitStreamReader reader(body.GetData(), (bits + 7) / 8, bits);
            Visual visual;
            CHECK(ReadEntityBody(reader, common, visual), "current water body");
            CHECK(visual.water && visual.water->vertices.size() == count &&
                  visual.water->vertices[2].x == -2 && visual.water->vertices[2].z == 2.5f &&
                  visual.water->visible == bool(flags & 1) && visual.water->shallow == bool(flags & 2) &&
                  reader.GetRemainingBits() == 0, "water vertices, flags and cursor");
            BitStreamReader truncated(body.GetData(), (bits + 7) / 8, bits - 1);
            CHECK(!ReadEntityBody(truncated, common, visual), "missing visibility bit rejected");
        }
    }
}

static void TestPassengerTrigger() {
    using namespace WorldRPG;
    static bool accept = false;
    static std::vector<uint8_t> packets;
    Headless::SetTestTransport([](const BitStreamWriter& w) {
        packets.push_back(w.GetData()[0]);
        return accept;
    });
    RakNetWrapper transport;
    NeonJoinClient client;
    auto& sender = *client.pImpl_;
    sender.net = &transport;
    Game::LocalCarState car;
    car.model = 400;
    car.seat = 1;
    car.firePress = 42;  // Touch already released on the game thread before the packet was sampled.
    PlayerWeaponSync::LocalWeapon gun;
    gun.valid = gun.gangDriveby = true;
    gun.type = 28;
    gun.slot = 4;
    gun.clip = gun.total = 10;
    PlayerWeaponSync::SetAimLine(gun.aim, {}, {0, 1, 0});
    CHECK(!sender.SendDriverPuresync(car, gun) && !sender.lastSentDriverKeys.buttonCircle, "failed passenger send preserves input baseline");
    accept = true;
    CHECK(sender.SendDriverPuresync(car, gun) && sender.lastSentDriverKeys.buttonCircle, "packet 33 commits short passenger press");
    car.firePress = 0;
    accept = false;
    sender.SendDriverKeySync(car, gun);
    CHECK(sender.lastSentDriverKeys.buttonCircle, "rejected release remains pending for retry");
    accept = true;
    sender.SendDriverKeySync(car, gun);
    sender.SendDriverKeySync(car, gun);
    CHECK(!sender.lastSentDriverKeys.buttonCircle && packets == std::vector<uint8_t>({33, 33, 31, 31}),
          "packet 31 releases the trigger once, against the accepted packet 33");
    Headless::SetTestTransport(nullptr);
}

int main() {
    TestPassengerTrigger();
    TestWaterEntityWire();
    TestNetworkJoinEnvelope();
    TestJoinProfilesAndData();
    TestIdentityRequests();
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
    TestInGameNoticeIgnoresPureSync();
    TestTransportFailureEndsAttempt();
    TestPcTransportErrorDispatch();
    if (g_failures == 0) {
        printf("ALL JOIN CODEC WIRE TESTS PASSED\n");
        return 0;
    }
    printf("%d FAILURES\n", g_failures);
    return 1;
}
