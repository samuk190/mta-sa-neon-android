// Native ARM64 transport probe. Run only on Oracle, with the production peer.
// OPEN fields are recovered from netc_pc.dll 102AB9A0 and net.so 167948.
#include "MtaOfflineCodec.h"
#include "MtaUdpSocket.h"
#include "MtaCipher.h"
#include "MtaReliabilityCodec.h"

#include <chrono>
#include <iostream>
#include <set>
#include <sys/random.h>

using namespace WorldRPG::Net::Mta;

static std::uint64_t Now() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

static int ProbeConnection(UdpSocket& socket, const OpenReply& reply, const Block16& nonce) {
    Bytes key, prefix;
    bool keyFound = false;
    for (const auto& field : reply.fields) {
        if (field.tag == 0x6e) { key = field.value; keyFound = true; }
        if (field.tag == 0x1a) prefix = field.value;
    }
    const auto cipher = Cipher::Create(key);
    if (!keyFound || key.empty() || !cipher) { std::cerr << "No usable negotiated cipher key\n"; return 1; }
    std::uint8_t selector = 0;
    if (::getrandom(&selector, 1, 0) != 1) return 1;
    auto sendRaw = [&](std::span<const std::uint8_t> bytes) {
        return socket.Send(cipher->Seal(bytes, selector++));
    };
    auto sendFrame = [&](const ReliableFrame& frame) {
        const auto bytes = EncodeFrame(frame);
        if (auto* error = std::get_if<FrameError>(&bytes)) { std::cerr << error->reason << '\n'; return false; }
        return sendRaw(std::get<Bytes>(bytes));
    };
    Bytes request{4};
    request.insert(request.end(), OfflineMagic.begin(), OfflineMagic.end());
    request.insert(request.end(), nonce.begin(), nonce.end());
    ReliableMessage initial; initial.number = 0; initial.payload = request; initial.bitLength = request.size() * 8;
    ReliableFrame pending; pending.remoteTime = Now(); pending.messages = {initial};
    if (!sendFrame(pending)) return 1;
    Bytes clock{0x26}; clock.insert(clock.end(), OfflineMagic.begin(), OfflineMagic.end());
    clock.insert(clock.end(), {1,0,0,0,0});
    if (!sendRaw(clock)) return 1;
    const auto deadline = Now() + 8000;
    auto nextRetry = Now() + 300;
    std::set<std::uint32_t> seen;
    bool accepted = false, acknowledged = false;
    unsigned encryptedPackets = 0;
    while (Now() < deadline) {
        if (!acknowledged && Now() >= nextRetry) {
            pending.remoteTime = Now();
            if (!sendFrame(pending)) return 1;
            nextRetry = Now() + 300;
        }
        Bytes datagram;
        const int received = socket.Receive(datagram, 100);
        if (received < 0) { std::cerr << socket.Error() << '\n'; return 1; }
        if (!received) continue;
        const auto plain = cipher->Open(datagram, prefix);
        if (auto* error = std::get_if<CipherError>(&plain)) {
            std::cout << "{\"event\":\"cipher_error\",\"error\":" << int(*error) << ",\"bytes\":" << datagram.size() << "}\n";
            continue;
        }
        ++encryptedPackets;
        const auto& bytes = std::get<Bytes>(plain);
        if (bytes.size() >= 18 && bytes[0] == 0x26 && std::equal(OfflineMagic.begin(), OfflineMagic.end(), bytes.begin() + 1)) continue;
        const auto decoded = DecodeFrame(bytes);
        if (auto* error = std::get_if<FrameError>(&decoded)) {
            std::cout << "{\"event\":\"frame_error\",\"reason\":\"" << error->reason << "\",\"bit\":" << error->bitOffset << "}\n";
            continue;
        }
        const auto& frame = std::get<ReliableFrame>(decoded);
        for (const auto& ack : frame.acknowledgments)
            if (ack.first <= pending.messages[0].number && ack.last >= pending.messages[0].number) acknowledged = true;
        ReliableFrame ack; ack.ackTime = frame.remoteTime.value_or(0);
        for (const auto& message : frame.messages) if (message.reliability >= 2) ack.acknowledgments.push_back({message.number, message.number});
        if (!ack.acknowledgments.empty() && !sendFrame(ack)) return 1;
        for (const auto& message : frame.messages) {
            if (!seen.insert(message.number).second || message.payload.empty()) continue;
            const auto& payload = message.payload;
            const auto id = payload[0];
            std::cout << "{\"event\":\"message\",\"id\":" << unsigned(id)
                      << ",\"bytes\":" << payload.size() << ",\"reliability\":" << unsigned(message.reliability)
                      << ",\"channel\":" << unsigned(message.channel) << ",\"order\":" << message.order << "}\n";
            if (id == 14 && !accepted) {
                if (payload.size() < 15) { std::cerr << "Truncated CRA\n"; return 1; }
                accepted = true;
                // Android native D1E45C..D1E484 and E151A0: copy the two
                // six-byte addresses from CRA, then nine absent interfaces.
                Bytes nic{17}; nic.insert(nic.end(), payload.begin()+9, payload.begin()+15);
                nic.insert(nic.end(), payload.begin()+1, payload.begin()+7);
                for (int i = 0; i < 9; ++i) nic.insert(nic.end(), {0,0,0,0,0xff,0xff});
                ReliableMessage next; next.number = 1; next.reliability = 3;
                next.payload = std::move(nic); next.bitLength = next.payload.size()*8;
                pending.messages = {next}; pending.remoteTime = Now(); acknowledged = false;
                if (!sendFrame(pending)) return 1;
                nextRetry = Now() + 300;
            }
            if (id == 106) {
                if (payload.size() < 5) return 1;
                const auto length = std::size_t(payload[3] | (payload[4] << 8));
                if (length > 64 || length > payload.size()-5) return 1;
                const std::string mod(payload.begin()+5, payload.begin()+5+length);
                if (mod != "deathmatch") { std::cerr << "Unexpected mod\n"; return 1; }
                std::cout << "{\"outcome\":\"mod_name_received\",\"mod\":\"deathmatch\",\"bitstream\":"
                          << unsigned(payload[1] | (payload[2] << 8)) << ",\"encrypted_packets\":" << encryptedPackets
                          << ",\"scope\":\"Transport only; authentication and game join not attempted\"}\n";
                ReliableMessage disconnect; disconnect.number = 2; disconnect.reliability = 3;
                disconnect.order = 1; disconnect.payload = {19}; disconnect.bitLength = 8;
                ReliableFrame closing; closing.remoteTime = Now(); closing.messages = {disconnect};
                if (!sendFrame(closing)) return 1;
                return 0;
            }
        }
    }
    std::cout << "{\"outcome\":\"connection_timeout\",\"cra_received\":" << (accepted ? "true" : "false")
              << ",\"encrypted_packets\":" << encryptedPackets << "}\n";
    return 1;
}

int main(int argc, char** argv) {
    const std::string host = argc >= 2 ? argv[1] : "worldrpgbroficial.duckdns.org";
    const bool connect = argc == 3 && std::string(argv[2]) == "--connect";
    if (argc > 3 || (argc == 3 && !connect)) { std::cerr << "Usage: probe_mta_native [host] [--connect]\n"; return 2; }
    // ED, CA(length1, algorithm1), A8(length4,1000LE), C8(length4,1000LE).
    // These are a capability offer, not random/session data or cipher keys.
    constexpr Block16 offer{
        0xed,0xca,0x01,0x01,0xa8,0x04,0xe8,0x03,
        0x00,0x00,0xc8,0x04,0xe8,0x03,0x00,0x00,
    };
    Block16 nonce{};
    if (::getrandom(nonce.data(), nonce.size(), 0) != static_cast<ssize_t>(nonce.size())) {
        std::cerr << "Cannot generate connection nonce\n"; return 1;
    }
    const auto open = BuildOpenRequest(nonce, offer);
    UdpSocket socket;
    if (!socket.Connect(host, 22003)) { std::cerr << socket.Error() << '\n'; return 1; }
    for (int attempt = 1; attempt <= 2; ++attempt) {
        if (!socket.Send(open)) { std::cerr << socket.Error() << '\n'; return 1; }
        std::vector<std::uint8_t> bytes;
        const int received = socket.Receive(bytes, 1500);
        if (received < 0) { std::cerr << socket.Error() << '\n'; return 1; }
        if (received == 0) continue;
        auto result = ParseOpenReply(bytes);
        if (auto* error = std::get_if<ReplyParseError>(&result)) {
            std::cout << "{\"outcome\":\"invalid_open_reply\",\"reply_bytes\":" << bytes.size()
                      << ",\"first_byte\":" << unsigned(bytes.empty() ? 0 : bytes[0])
                      << ",\"error\":" << int(error->code) << ",\"offset\":" << error->offset << "}\n";
            return 1;
        }
        const auto& reply = std::get<OpenReply>(result);
        std::cout << "{\"outcome\":\"valid_open_reply\",\"sent_bytes\":51,\"attempt\":" << attempt
                  << ",\"reply_bytes\":" << bytes.size() << ",\"fields\":[";
        bool comma = false;
        for (const auto& field : reply.fields) {
            if (comma) std::cout << ',';
            comma = true;
            std::cout << "{\"tag\":" << unsigned(field.tag) << ",\"bytes\":" << field.value.size() << '}';
        }
        std::cout << "],\"scope\":\"OPEN only; no login or session claim\"}\n";
        return connect ? ProbeConnection(socket, reply, nonce) : 0;
    }
    std::cout << "{\"outcome\":\"open_timeout\",\"attempts\":2,\"sent_bytes\":51}\n";
    return 1;
}
