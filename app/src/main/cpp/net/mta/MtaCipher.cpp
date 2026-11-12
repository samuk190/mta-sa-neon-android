#include "MtaCipher.h"
#include <algorithm>
#include <numeric>

namespace WorldRPG::Net::Mta {
std::optional<Cipher> Cipher::Create(std::span<const std::uint8_t> key) {
    if (key.size() > 64) return std::nullopt;
    Cipher cipher;
    std::iota(cipher.table_.begin(), cipher.table_.end(), std::uint8_t{0});
    if (!key.empty()) {
        std::uint8_t cursor = 0;
        for (std::size_t i = 0; i < 256; ++i) {
            cursor = static_cast<std::uint8_t>(cursor + cipher.table_[i] + key[i % key.size()]);
            std::swap(cipher.table_[i], cipher.table_[cursor]);
        }
        cursor = 0;
        for (std::size_t i = 0; i < 1000; ++i) {
            const auto index = i & 255;
            cursor = static_cast<std::uint8_t>(cursor + cipher.table_[index]);
            std::swap(cipher.table_[index], cipher.table_[cursor]);
        }
    }
    return cipher;
}

std::uint16_t Cipher::Checksum(std::span<const std::uint8_t> bytes) {
    std::uint16_t crc = 0;
    for (const auto byte : bytes) {
        crc ^= static_cast<std::uint16_t>(byte) << 8;
        for (int bit = 0; bit < 8; ++bit) {
            crc = static_cast<std::uint16_t>((crc << 1) ^ ((crc & 0x8000) ? 0x1021 : 0));
        }
    }
    return crc;
}

Bytes Cipher::Transform(std::span<const std::uint8_t> bytes, std::uint8_t selector) const {
    Bytes result(bytes.size());
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        const auto cell = table_[i & 255];
        selector = static_cast<std::uint8_t>(selector + cell);
        result[i] = bytes[i] ^ table_[static_cast<std::uint8_t>(cell + table_[selector])];
    }
    return result;
}

Bytes Cipher::Seal(std::span<const std::uint8_t> plain, std::uint8_t selector) const {
    auto result = Transform(plain, selector);
    const auto crc = Checksum(result);
    result.insert(result.begin(), {static_cast<std::uint8_t>(crc), static_cast<std::uint8_t>(crc >> 8), selector});
    return result;
}

std::variant<Bytes, CipherError> Cipher::Open(std::span<const std::uint8_t> datagram,
                                           std::span<const std::uint8_t> prefix) const {
    if (datagram.size() > 65507) return CipherError::TooLarge;
    if (prefix.size() > 16 || datagram.size() < prefix.size() + 3) return CipherError::Truncated;
    if (!std::equal(prefix.begin(), prefix.end(), datagram.begin())) return CipherError::PrefixMismatch;
    datagram = datagram.subspan(prefix.size());
    const auto crc = static_cast<std::uint16_t>(datagram[0] | (datagram[1] << 8));
    if (Checksum(datagram.subspan(3)) != crc) return CipherError::ChecksumMismatch;
    return Transform(datagram.subspan(3), datagram[2]);
}
} // namespace WorldRPG::Net::Mta
