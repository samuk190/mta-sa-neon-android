#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <variant>
#include <vector>

namespace WorldRPG::Net::Mta {

using Block16 = std::array<std::uint8_t, 16>;
using OpenRequest = std::array<std::uint8_t, 51>;

inline constexpr Block16 OfflineMagic{
    0x00, 0xff, 0x00, 0xff, 0xfe, 0xfe, 0xfe, 0xfe,
    0xfd, 0xfd, 0xfd, 0xfd, 0x78, 0x56, 0x34, 0x17,
};

// The fixed block is still unverified against a real OPEN51 capture. Both
// blocks are mandatory inputs. This function generates no nonce and sends
// nothing. Supplying synthetic data does not establish interoperability.
OpenRequest BuildOpenRequest(const Block16& nonce, const Block16& fixedBlock);

struct ReplyField {
    std::uint8_t tag{};
    std::vector<std::uint8_t> value;
    bool operator==(const ReplyField&) const = default;
};

struct OpenReply {
    // No nonce-echo or identity semantics have been established for this block.
    Block16 opaqueBlock{};
    // Preserve unknown and duplicate tags, their order and their raw values.
    // Semantic interpretation and required-field rules belong to a later stage.
    std::vector<ReplyField> fields;
    bool operator==(const OpenReply&) const = default;
};

enum class ReplyError {
    TruncatedHeader,
    WrongPacketId,
    WrongMagic,
    MissingExtensionMarker,
    TruncatedFieldHeader,
    TruncatedFieldValue,
    InvalidKnownFieldLength,
    MissingTerminator,
    TrailingData,
};

struct ReplyParseError {
    ReplyError code;
    std::size_t offset;
};

using OpenReplyResult = std::variant<OpenReply, ReplyParseError>;

// Parses one entire datagram. DE must terminate it exactly. This strict
// trailing-data policy is deliberate until a real vector establishes otherwise.
// Success establishes structural validity only, not an authenticated session.
OpenReplyResult ParseOpenReply(std::span<const std::uint8_t> datagram);

} // namespace WorldRPG::Net::Mta
