#include "MtaOfflineCodec.h"

#include <algorithm>

namespace WorldRPG::Net::Mta {
namespace {
constexpr std::size_t OpaqueBlockOffset = 17;
constexpr std::size_t ExtensionMarkerOffset = 33;
constexpr std::size_t FieldsOffset = 34;
constexpr std::uint8_t ExtensionMarker = 0xed;
constexpr std::uint8_t Terminator = 0xde;

bool IsValidKnownLength(std::uint8_t tag, std::size_t length) {
    switch (tag) {
        case 0x6e:
            return length <= 64;
        case 0x1a:
            return length <= 16;
        case 0xa8:
        case 0xc8:
            return length == 4;
        default:
            return true;
    }
}
} // namespace

OpenRequest BuildOpenRequest(const Block16& nonce, const Block16& fixedBlock) {
    OpenRequest request{};
    request[0] = 0x09;
    request[1] = 0x04;
    std::copy(nonce.begin(), nonce.end(), request.begin() + 2);
    std::copy(OfflineMagic.begin(), OfflineMagic.end(), request.begin() + 18);
    std::copy(fixedBlock.begin(), fixedBlock.end(), request.begin() + 34);
    request.back() = Terminator;
    return request;
}

OpenReplyResult ParseOpenReply(std::span<const std::uint8_t> datagram) {
    // Need the fixed header and at least one byte for the terminator. Check
    // before indexing any of them, including on an empty input span.
    if (datagram.size() < FieldsOffset + 1) {
        return ReplyParseError{ReplyError::TruncatedHeader, datagram.size()};
    }
    if (datagram[0] != 0x0a) {
        return ReplyParseError{ReplyError::WrongPacketId, 0};
    }
    for (std::size_t index = 0; index < OfflineMagic.size(); ++index) {
        if (datagram[index + 1] != OfflineMagic[index]) {
            return ReplyParseError{ReplyError::WrongMagic, index + 1};
        }
    }
    if (datagram[ExtensionMarkerOffset] != ExtensionMarker) {
        return ReplyParseError{ReplyError::MissingExtensionMarker, ExtensionMarkerOffset};
    }

    OpenReply reply;
    std::copy_n(datagram.begin() + OpaqueBlockOffset,
                reply.opaqueBlock.size(), reply.opaqueBlock.begin());
    std::size_t offset = FieldsOffset;
    while (offset < datagram.size()) {
        const std::uint8_t tag = datagram[offset];
        if (tag == Terminator) {
            if (offset + 1 != datagram.size()) {
                return ReplyParseError{ReplyError::TrailingData, offset + 1};
            }
            return reply;
        }
        if (datagram.size() - offset < 2) {
            return ReplyParseError{ReplyError::TruncatedFieldHeader, offset};
        }
        const std::size_t length = datagram[offset + 1];
        if (!IsValidKnownLength(tag, length)) {
            return ReplyParseError{ReplyError::InvalidKnownFieldLength, offset + 1};
        }
        offset += 2;
        if (length > datagram.size() - offset) {
            return ReplyParseError{ReplyError::TruncatedFieldValue, offset};
        }
        const auto value = datagram.subspan(offset, length);
        reply.fields.push_back(ReplyField{tag, {value.begin(), value.end()}});
        offset += length;
    }
    return ReplyParseError{ReplyError::MissingTerminator, offset};
}

} // namespace WorldRPG::Net::Mta
