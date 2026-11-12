// State tests inspect delivery queues without opening a socket. Wire vectors
// were produced by an independent Python reference on Oracle. Public MOD_NAME
// data is from the real server; timestamps and IDs in the fixtures are synthetic.
#include <memory>
#include <cstddef>
#define private public
#include "MtaClientTransport.h"
#undef private
#include "../app/src/main/cpp/net/mta/MtaClientTransport.cpp"
#include <iostream>
#include <stdexcept>

using namespace WorldRPG::Net::Mta;
static void Check(bool value, const char* reason) { if (!value) throw std::runtime_error(reason); }
static Bytes Hex(const std::string& hex) {
    Bytes result;
    for (std::size_t i = 0; i < hex.size(); i += 2) result.push_back(std::stoul(hex.substr(i,2),nullptr,16));
    return result;
}
static ReliableMessage Message(std::uint32_t number, std::uint32_t order, std::uint8_t reliability = 3, std::uint8_t channel = 0) {
    ReliableMessage result; result.number = number; result.order = order;
    result.reliability = reliability; result.channel = channel; result.bitLength = 16;
    result.payload = {106,static_cast<std::uint8_t>(number)}; return result;
}
static void WireTests() {
    const auto mod = Hex("503880400000000000800000180000000013c06a3d000a0064656174686d61746368");
    const auto parsed = DecodeFrame(mod);
    Check(std::holds_alternative<ReliableFrame>(parsed), "known MOD frame rejected");
    const auto& frame = std::get<ReliableFrame>(parsed);
    Check(frame.remoteTime == 123456 && frame.messages.size() == 1, "timestamp and record count");
    Check(frame.messages[0].number == 2 && frame.messages[0].reliability == 3 && frame.messages[0].order == 0, "ordered header");
    Check(frame.messages[0].payload == Hex("6a3d000a0064656174686d61746368"), "MOD payload");
    Check(std::get<Bytes>(EncodeFrame(frame)) == mod, "wire re-encode differs from independent reference");
    for (std::size_t size = 0; size < mod.size(); ++size) {
        const auto result = DecodeFrame(std::span(mod).first(size));
        // A complete header with no messages is allowed, as in ACK/ping-only
        // frames. Reject every truncated prefix that began a message header.
        if (size == 1 || size == 9) continue;
        Check(std::holds_alternative<FrameError>(result), "truncated message accepted");
    }
    const auto ackBytes = Hex("a0710080000000006505000000050000000600000000");
    const auto ack = std::get<ReliableFrame>(DecodeFrame(ackBytes));
    Check(ack.ackTime == 123456 && ack.acknowledgments == std::vector<AckRange>{{5,5},{10,12}}, "ACK single and range");
    Check(std::get<Bytes>(EncodeFrame(ack)) == ackBytes, "ACK encoding");
    ReliableFrame invalid; invalid.messages = {Message(0,0)}; invalid.messages[0].bitLength = 11929;
    Check(std::holds_alternative<FrameError>(EncodeFrame(invalid)), "oversize fragment accepted");
    invalid.messages[0] = Message(0,0); invalid.messages[0].split = SplitInfo{1,2,2};
    Check(std::holds_alternative<FrameError>(EncodeFrame(invalid)), "invalid split accepted");
    ReliableFrame partial; auto message = Message(4,3); message.bitLength = 13; partial.messages = {message};
    Check(std::get<ReliableFrame>(DecodeFrame(std::get<Bytes>(EncodeFrame(partial)))) == partial, "partial payload bit count lost");
}
static void StateTests() {
    ClientTransport::Impl client; client.state = ClientTransport::Impl::State::Connected;
    client.Incoming(Message(10,1));
    Check(client.events.empty(), "out of order message delivered too soon");
    client.Incoming(Message(11,0));
    Check(client.events.size() == 2 && client.events[0].bytes[1] == 11 && client.events[1].bytes[1] == 10, "ordered delivery");
    client.Incoming(Message(11,0)); client.Incoming(Message(12,0));
    Check(client.events.size() == 2, "duplicate or stale order delivered");
    client.Incoming(Message(13,4,1,2)); client.Incoming(Message(14,3,1,2));
    Check(client.events.size() == 3, "stale sequenced state delivered");
    Check(client.Order(3,0) == 0 && client.Order(1,0) == 0 && client.Order(3,0) == 1 && client.Order(4,0) == 1,
          "sequenced messages created a gap in ordered channel");
    ClientTransport::Impl fragments; fragments.state = ClientTransport::Impl::State::Connected;
    auto tail = Message(51,0); tail.payload = {0x40}; tail.bitLength = 3; tail.split = SplitInfo{9,1,2};
    auto head = Message(50,0); head.payload = {106}; head.bitLength = 8; head.split = SplitInfo{9,0,2};
    fragments.Incoming(tail); Check(fragments.events.empty(), "partial group delivered");
    fragments.Incoming(head);
    Check(fragments.events.size() == 1 && fragments.events[0].bytes == Bytes({106,0x40}) && fragments.events[0].bits == 11, "fragment assembly and bits");
    Check(fragments.groups.empty() && fragments.assemblyBytes == 0, "fragment storage not released");
    fragments.Incoming(tail); Check(fragments.events.size() == 1, "duplicate fragment delivered");
    ClientTransport::Impl wrap;
    Check(!wrap.Duplicate(0xfffffffeu) && !wrap.Duplicate(0xffffffffu) && !wrap.Duplicate(0) && wrap.Duplicate(0xfffffffeu), "message-number wrap handling");
    ClientTransport::Impl malformed; malformed.state = ClientTransport::Impl::State::Connected;
    head.split->count = 2; malformed.Incoming(head);
    tail.number = 52; tail.split->count = 3; malformed.Incoming(tail);
    Check(malformed.state == ClientTransport::Impl::State::Failed && malformed.error == "Inconsistent MTA fragment count", "inconsistent group not rejected");
}
int main() {
    try { WireTests(); StateTests(); std::cout << "PASS MTA reliability vectors, ordering, deduplication and fragments\n"; return 0; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
