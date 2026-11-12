#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <variant>
#include <vector>

namespace WorldRPG::Net::Mta {
using Bytes = std::vector<std::uint8_t>;
enum class CipherError { Truncated, PrefixMismatch, ChecksumMismatch, TooLarge };

// MTA's modified cipher, not the conventional RC4 PRGA. Server net.so:
// init 166c20, transforms 166b20/166ba0, initial iota at 3f0d50.
// CRC is corruption detection, not a proof of authentication.
class Cipher {
public:
    static std::optional<Cipher> Create(std::span<const std::uint8_t> key);
    static std::uint16_t Checksum(std::span<const std::uint8_t> bytes);
    Bytes Transform(std::span<const std::uint8_t> bytes, std::uint8_t selector) const;
    // Client output has no prefix. The negotiated prefix applies to input.
    Bytes Seal(std::span<const std::uint8_t> plain, std::uint8_t selector) const;
    std::variant<Bytes, CipherError> Open(std::span<const std::uint8_t> datagram,
                                       std::span<const std::uint8_t> receivePrefix = {}) const;
private:
    Cipher() = default;
    std::array<std::uint8_t, 256> table_{};
};
} // namespace WorldRPG::Net::Mta
