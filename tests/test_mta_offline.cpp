// All fixtures in this file are SYNTHETIC structural examples. Neither their
// fixed request block nor their reply field contents came from a real session.
#include "MtaOfflineCodec.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace WorldRPG::Net::Mta;
using Bytes = std::vector<std::uint8_t>;

void Check(bool condition, const std::string& description) {
    if (!condition) throw std::runtime_error(description);
}

Bytes SyntheticReply(const std::vector<ReplyField>& fields = {}) {
    Bytes packet{0x0a};
    packet.insert(packet.end(), OfflineMagic.begin(), OfflineMagic.end());
    for (std::uint8_t index = 0; index < 16; ++index) {
        packet.push_back(static_cast<std::uint8_t>(0xa0 + index));
    }
    packet.push_back(0xed);
    for (const auto& field : fields) {
        Check(field.value.size() <= 255, "invalid synthetic fixture length");
        packet.push_back(field.tag);
        packet.push_back(static_cast<std::uint8_t>(field.value.size()));
        packet.insert(packet.end(), field.value.begin(), field.value.end());
    }
    packet.push_back(0xde);
    return packet;
}

OpenReply ParseSuccessful(const Bytes& bytes) {
    auto result = ParseOpenReply(bytes);
    Check(std::holds_alternative<OpenReply>(result), "synthetic reply rejected");
    return std::get<OpenReply>(result);
}

void ExpectError(const Bytes& bytes, ReplyError expected, const std::string& description) {
    const auto result = ParseOpenReply(bytes);
    const auto* error = std::get_if<ReplyParseError>(&result);
    Check(error != nullptr, description + " was accepted");
    Check(error->code == expected, description + " reported the wrong error");
    Check(error->offset <= bytes.size(), description + " reported an invalid byte offset");
}

void TestRequestLayout() {
    Block16 nonce{};
    Block16 syntheticFixedBlock{};
    for (std::size_t index = 0; index < 16; ++index) {
        nonce[index] = static_cast<std::uint8_t>(index + 1);
        syntheticFixedBlock[index] = static_cast<std::uint8_t>(0x80 + index);
    }
    const auto request = BuildOpenRequest(nonce, syntheticFixedBlock);
    Check(request.size() == 51, "OPEN51 size");
    Check(request[0] == 0x09 && request[1] == 0x04, "OPEN51 fixed prefix");
    Check(std::equal(nonce.begin(), nonce.end(), request.begin() + 2), "nonce location");
    // Independent literal catches accidental reuse of the vendor RakNet4 magic.
    const Block16 expectedMagic{
        0x00, 0xff, 0x00, 0xff, 0xfe, 0xfe, 0xfe, 0xfe,
        0xfd, 0xfd, 0xfd, 0xfd, 0x78, 0x56, 0x34, 0x17,
    };
    Check(std::equal(expectedMagic.begin(), expectedMagic.end(), request.begin() + 18),
          "MTA magic location and value");
    Check(std::equal(syntheticFixedBlock.begin(), syntheticFixedBlock.end(), request.begin() + 34),
          "explicit fixed block location");
    Check(request[50] == 0xde, "OPEN51 terminator");

    nonce.fill(0x5a);
    syntheticFixedBlock.fill(0x6c);
    const auto different = BuildOpenRequest(nonce, syntheticFixedBlock);
    Check(different != request, "request ignored caller-provided blocks");
    Check(different[2] == 0x5a && different[17] == 0x5a, "second nonce retained");
    Check(different[34] == 0x6c && different[49] == 0x6c, "second fixed block retained");
}

void TestPreservesFields() {
    const std::vector<ReplyField> syntheticFields{
        {0x6e, {1, 2, 3}},
        {0x1a, {}},
        {0xa8, {0x78, 0x56, 0x34, 0x12}},
        {0xc8, {0x04, 0x03, 0x02, 0x01}},
        {0x55, {0xde, 0xed, 0x00}}, // Markers within a value are data.
        {0x6e, {4, 5}},             // Duplicate tag retained in order.
        {0x56, {}},                // Unknown empty field retained.
    };
    const auto reply = ParseSuccessful(SyntheticReply(syntheticFields));
    Check(reply.fields == syntheticFields, "fields changed, discarded or reordered");
    for (std::size_t index = 0; index < reply.opaqueBlock.size(); ++index) {
        Check(reply.opaqueBlock[index] == 0xa0 + index, "opaque block changed");
    }
    const auto empty = ParseSuccessful(SyntheticReply());
    Check(empty.fields.empty(), "invented mandatory TLV requirement");
    const auto unknown = ParseSuccessful(SyntheticReply({{0x77, Bytes(255, 0xde)}}));
    Check(unknown.fields[0].value == Bytes(255, 0xde), "unknown maximum-length value");
}

void TestKnownLengthBoundaries() {
    for (const auto& [tag, limit] : std::array<std::pair<std::uint8_t, std::size_t>, 2>{
             {{0x6e, 64}, {0x1a, 16}}}) {
        ParseSuccessful(SyntheticReply({{tag, {}}}));
        ParseSuccessful(SyntheticReply({{tag, Bytes(limit, 0x5a)}}));
        ExpectError(SyntheticReply({{tag, Bytes(limit + 1, 0x5a)}}),
                    ReplyError::InvalidKnownFieldLength, "overlength variable field");
    }
    for (const std::uint8_t tag : {0xa8, 0xc8}) {
        ParseSuccessful(SyntheticReply({{tag, {1, 2, 3, 4}}}));
        for (const std::size_t length : {0, 1, 3, 5, 255}) {
            ExpectError(SyntheticReply({{tag, Bytes(length, 0x5a)}}),
                        ReplyError::InvalidKnownFieldLength, "wrong fixed field length");
        }
    }
}

void TestHeaderAndTermination() {
    auto packet = SyntheticReply();
    packet[0] = 0x09;
    ExpectError(packet, ReplyError::WrongPacketId, "request ID as reply");
    for (std::size_t index = 1; index <= 16; ++index) {
        packet = SyntheticReply();
        packet[index] ^= 1;
        ExpectError(packet, ReplyError::WrongMagic, "mutated magic byte " + std::to_string(index));
    }
    packet = SyntheticReply();
    packet[33] = 0;
    ExpectError(packet, ReplyError::MissingExtensionMarker, "missing ED");

    packet = SyntheticReply();
    packet.push_back(0);
    ExpectError(packet, ReplyError::TrailingData, "bytes after DE");
    packet = SyntheticReply();
    packet.push_back(0xde);
    ExpectError(packet, ReplyError::TrailingData, "second terminator");

    packet = SyntheticReply({{0x55, {1, 2, 3}}});
    packet.pop_back();
    ExpectError(packet, ReplyError::MissingTerminator, "missing DE");
    packet = SyntheticReply();
    packet.back() = 0x55;
    ExpectError(packet, ReplyError::TruncatedFieldHeader, "TLV without length");
    packet = SyntheticReply();
    packet.back() = 0x55;
    packet.push_back(5);
    packet.push_back(1);
    packet.push_back(2);
    ExpectError(packet, ReplyError::TruncatedFieldValue, "declared TLV exceeds datagram");
}

void TestEveryTruncation() {
    const auto packet = SyntheticReply({
        {0x6e, Bytes(64, 0x2c)},
        {0x1a, Bytes(16, 0x3d)},
        {0xa8, {1, 2, 3, 4}},
        {0xc8, {5, 6, 7, 8}},
        {0x55, {0xde, 0xed}},
    });
    for (std::size_t length = 0; length < packet.size(); ++length) {
        const auto result = ParseOpenReply(std::span(packet).first(length));
        const auto* error = std::get_if<ReplyParseError>(&result);
        Check(error != nullptr, "accepted truncated prefix " + std::to_string(length));
        Check(error->offset <= length, "error offset outside truncated prefix");
    }
    ParseSuccessful(packet);
}
} // namespace

int main() {
    try {
        TestRequestLayout();
        TestPreservesFields();
        TestKnownLengthBoundaries();
        TestHeaderAndTermination();
        TestEveryTruncation();
        std::cout << "PASS: MTA offline codec synthetic structural fixtures\n"
                     "No real OPEN51, cipher or server interoperability is asserted.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
