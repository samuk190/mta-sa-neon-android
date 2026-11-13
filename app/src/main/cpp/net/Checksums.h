#pragma once

// Streaming MD5 (RFC 1321) and CRC-32 (zlib / IEEE 802.3, the value MTA's
// CRCGenerator::GetCRCFromBuffer returns). Used to verify resource files
// downloaded over HTTP against the RESOURCE_START descriptor.

#include <cstddef>
#include <cstdint>
#include <string>

namespace WorldRPG::Net {

class Md5 {
public:
    void Update(const void* data, size_t length);
    void Finish(uint8_t digest[16]);

private:
    void Transform(const uint8_t block[64]);

    uint32_t state_[4] = {0x67452301u, 0xefcdab89u, 0x98badcfeu, 0x10325476u};
    uint64_t bitCount_ = 0;
    uint8_t buffer_[64] = {};
    size_t bufferUsed_ = 0;
};

// Running CRC-32 (zlib convention: start from 0, feed chunks, read the result).
uint32_t Crc32Update(uint32_t crc, const uint8_t* data, size_t length);

// Hashes everything fed to it: total byte count, CRC-32 and MD5 in one pass.
class FileHasher {
public:
    void Update(const void* data, size_t length);
    uint64_t Size() const { return size_; }
    uint32_t Crc() const { return crc_; }
    void Md5Digest(uint8_t digest[16]);

private:
    Md5 md5_;
    uint32_t crc_ = 0;
    uint64_t size_ = 0;
};

// Lowercase hex of a 16-byte digest.
std::string ToHex(const uint8_t* bytes, size_t length);

} // namespace WorldRPG::Net
