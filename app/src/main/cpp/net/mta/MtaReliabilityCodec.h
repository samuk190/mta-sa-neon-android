#pragma once
#include "MtaCipher.h"
#include <optional>
#include <string>

namespace WorldRPG::Net::Mta {
// Recovered from Android FUN_010b2598 (ELF FB2598). This is the
// message-number/ACK-range protocol, not RakNet4's datagram-sequence format.
struct AckRange { std::uint32_t first{}, last{}; bool operator==(const AckRange&) const = default; };
struct SplitInfo {
    std::uint16_t id{};
    std::uint32_t index{}, count{};
    bool operator==(const SplitInfo&) const = default;
};
struct ReliableMessage {
    std::uint32_t number{};
    std::uint8_t reliability = 2;
    std::uint8_t channel{};
    std::uint32_t order{};
    std::optional<SplitInfo> split;
    std::uint32_t bitLength{}; // 16-bit on each wire fragment; wider after assembly
    Bytes payload;
    bool operator==(const ReliableMessage&) const = default;
};
struct ReliableFrame {
    std::optional<std::uint64_t> ackTime;
    std::vector<AckRange> acknowledgments;
    std::optional<std::uint64_t> remoteTime;
    std::vector<ReliableMessage> messages;
    bool operator==(const ReliableFrame&) const = default;
};
struct FrameError { std::string reason; std::size_t bitOffset{}; };
std::variant<Bytes, FrameError> EncodeFrame(const ReliableFrame& frame);
std::variant<ReliableFrame, FrameError> DecodeFrame(std::span<const std::uint8_t> bytes);
} // namespace WorldRPG::Net::Mta
