#include "Checksums.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace WorldRPG::Net {

namespace {

constexpr std::array<uint32_t, 64> kMd5K = {
    0xd76aa478u, 0xe8c7b756u, 0x242070dbu, 0xc1bdceeeu, 0xf57c0fafu, 0x4787c62au, 0xa8304613u, 0xfd469501u,
    0x698098d8u, 0x8b44f7afu, 0xffff5bb1u, 0x895cd7beu, 0x6b901122u, 0xfd987193u, 0xa679438eu, 0x49b40821u,
    0xf61e2562u, 0xc040b340u, 0x265e5a51u, 0xe9b6c7aau, 0xd62f105du, 0x02441453u, 0xd8a1e681u, 0xe7d3fbc8u,
    0x21e1cde6u, 0xc33707d6u, 0xf4d50d87u, 0x455a14edu, 0xa9e3e905u, 0xfcefa3f8u, 0x676f02d9u, 0x8d2a4c8au,
    0xfffa3942u, 0x8771f681u, 0x6d9d6122u, 0xfde5380cu, 0xa4beea44u, 0x4bdecfa9u, 0xf6bb4b60u, 0xbebfbc70u,
    0x289b7ec6u, 0xeaa127fau, 0xd4ef3085u, 0x04881d05u, 0xd9d4d039u, 0xe6db99e5u, 0x1fa27cf8u, 0xc4ac5665u,
    0xf4292244u, 0x432aff97u, 0xab9423a7u, 0xfc93a039u, 0x655b59c3u, 0x8f0ccc92u, 0xffeff47du, 0x85845dd1u,
    0x6fa87e4fu, 0xfe2ce6e0u, 0xa3014314u, 0x4e0811a1u, 0xf7537e82u, 0xbd3af235u, 0x2ad7d2bbu, 0xeb86d391u,
};

constexpr std::array<uint32_t, 64> kMd5S = {
    7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20,
    4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21,
};

constexpr std::array<uint32_t, 256> MakeCrcTable() {
    std::array<uint32_t, 256> table{};
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t c = i;
        for (int k = 0; k < 8; ++k) c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        table[i] = c;
    }
    return table;
}

constexpr std::array<uint32_t, 256> kCrcTable = MakeCrcTable();

inline uint32_t RotL(uint32_t x, uint32_t c) { return (x << c) | (x >> (32 - c)); }

} // namespace

void Md5::Transform(const uint8_t block[64]) {
    uint32_t m[16];
    for (int i = 0; i < 16; ++i) {
        m[i] = static_cast<uint32_t>(block[i * 4]) | (static_cast<uint32_t>(block[i * 4 + 1]) << 8) |
               (static_cast<uint32_t>(block[i * 4 + 2]) << 16) | (static_cast<uint32_t>(block[i * 4 + 3]) << 24);
    }
    uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
    for (uint32_t i = 0; i < 64; ++i) {
        uint32_t f = 0;
        uint32_t g = 0;
        if (i < 16) {
            f = (b & c) | (~b & d);
            g = i;
        } else if (i < 32) {
            f = (d & b) | (~d & c);
            g = (5 * i + 1) % 16;
        } else if (i < 48) {
            f = b ^ c ^ d;
            g = (3 * i + 5) % 16;
        } else {
            f = c ^ (b | ~d);
            g = (7 * i) % 16;
        }
        const uint32_t next = d;
        d = c;
        c = b;
        b = b + RotL(a + f + kMd5K[i] + m[g], kMd5S[i]);
        a = next;
    }
    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
}

void Md5::Update(const void* data, size_t length) {
    const auto* in = static_cast<const uint8_t*>(data);
    bitCount_ += static_cast<uint64_t>(length) * 8u;
    if (bufferUsed_ > 0) {
        const size_t take = std::min(length, sizeof(buffer_) - bufferUsed_);
        std::memcpy(buffer_ + bufferUsed_, in, take);
        bufferUsed_ += take;
        in += take;
        length -= take;
        if (bufferUsed_ < sizeof(buffer_)) return;
        Transform(buffer_);
        bufferUsed_ = 0;
    }
    while (length >= 64) {
        Transform(in);
        in += 64;
        length -= 64;
    }
    if (length > 0) {
        std::memcpy(buffer_, in, length);
        bufferUsed_ = length;
    }
}

void Md5::Finish(uint8_t digest[16]) {
    const uint64_t bits = bitCount_;
    const uint8_t pad = 0x80;
    Update(&pad, 1);
    const uint8_t zero = 0;
    while (bufferUsed_ != 56) Update(&zero, 1);
    uint8_t lengthBytes[8];
    for (int i = 0; i < 8; ++i) lengthBytes[i] = static_cast<uint8_t>(bits >> (8 * i));
    Update(lengthBytes, 8);
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) digest[i * 4 + j] = static_cast<uint8_t>(state_[i] >> (8 * j));
    }
}

uint32_t Crc32Update(uint32_t crc, const uint8_t* data, size_t length) {
    crc = ~crc;
    for (size_t i = 0; i < length; ++i) crc = kCrcTable[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    return ~crc;
}

void FileHasher::Update(const void* data, size_t length) {
    crc_ = Crc32Update(crc_, static_cast<const uint8_t*>(data), length);
    md5_.Update(data, length);
    size_ += length;
}

void FileHasher::Md5Digest(uint8_t digest[16]) { md5_.Finish(digest); }

std::string ToHex(const uint8_t* bytes, size_t length) {
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string out;
    out.reserve(length * 2);
    for (size_t i = 0; i < length; ++i) {
        out.push_back(kDigits[bytes[i] >> 4]);
        out.push_back(kDigits[bytes[i] & 0x0F]);
    }
    return out;
}

} // namespace WorldRPG::Net
