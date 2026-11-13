#include "RakNetWrapper.h"

#include <cstring>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <mutex>
#include <atomic>
#include <vector>

#include "../mta/MtaClientTransport.h"

#ifdef __ANDROID__
#include <android/log.h>
#define LOG_TAG "WorldRPGNeon"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#else
#include <cstdio>
#define LOGI(...) do { printf(__VA_ARGS__); printf("\n"); } while(0)
#define LOGE(...) do { fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } while(0)
#endif

namespace WorldRPG::Net {

// ---------------------------------------------------------------------------
// ASCII Normalization
// ---------------------------------------------------------------------------
std::string NormalizeAscii(const std::string& input) {
    std::string result;
    result.reserve(input.size());
    for (unsigned char ch : input) {
        // Map common UTF-8 accented characters to ASCII equivalents
        // Since we receive already-decoded bytes, handle direct Latin-1 range
        if (ch >= 0xC0 && ch <= 0xC5) { result += 'A'; continue; } // A variants
        if (ch == 0xC7) { result += 'C'; continue; }
        if (ch >= 0xC8 && ch <= 0xCB) { result += 'E'; continue; }
        if (ch >= 0xCC && ch <= 0xCF) { result += 'I'; continue; }
        if (ch == 0xD1) { result += 'N'; continue; }
        if (ch >= 0xD2 && ch <= 0xD6) { result += 'O'; continue; }
        if (ch >= 0xD9 && ch <= 0xDC) { result += 'U'; continue; }
        if (ch >= 0xE0 && ch <= 0xE5) { result += 'a'; continue; }
        if (ch == 0xE7) { result += 'c'; continue; }
        if (ch >= 0xE8 && ch <= 0xEB) { result += 'e'; continue; }
        if (ch >= 0xEC && ch <= 0xEF) { result += 'i'; continue; }
        if (ch == 0xF1) { result += 'n'; continue; }
        if (ch >= 0xF2 && ch <= 0xF6) { result += 'o'; continue; }
        if (ch >= 0xF9 && ch <= 0xFC) { result += 'u'; continue; }
        // Skip continuation bytes of multi-byte UTF-8 sequences
        if (ch >= 0x80 && ch <= 0xBF) continue;
        // Replace bullet and special markers
        if (ch == 0xE2) continue; // Start of bullet/em-dash sequence
        if (ch == 0x80) continue;
        if (ch == 0xA2) { result += '*'; continue; } // bullet -> *
        if (ch == 0x93 || ch == 0x94) { result += '"'; continue; } // smart quotes
        if (ch == 0x91 || ch == 0x92) { result += '\''; continue; }
        if (ch == 0x96 || ch == 0x97) { result += '-'; continue; } // en/em dash
        result += static_cast<char>(ch);
    }
    return result;
}

// ---------------------------------------------------------------------------
// BitStreamWriter Implementation
// ---------------------------------------------------------------------------
struct BitStreamWriter::Impl {
    std::vector<uint8_t> buffer;
    size_t bitOffset = 0;

    void EnsureBits(size_t bitsNeeded) {
        size_t bytesNeeded = (bitOffset + bitsNeeded + 7) / 8;
        if (bytesNeeded > buffer.size()) {
            buffer.resize(bytesNeeded, 0);
        }
    }

    // MTA/RakNet wire bit order, cross-validated against the production-proven
    // reader in backend_mta/scripts/mobile_handshake.hpp and RakNet
    // BitStream::WriteBits(rightAlignedBits=true):
    //  - stream packs bits MSB-first within each byte;
    //  - full source bytes flow as-is (little-endian integers stay LE);
    //  - a trailing partial byte is right-aligned: its low (numBits%8) bits
    //    are written, most significant of them first.
    void WriteBitsInternal(const uint8_t* data, size_t numBits) {
        EnsureBits(numBits);
        const size_t fullBytes = numBits / 8;
        const size_t rem = numBits % 8;
        size_t k = 0;
        auto put = [&](bool bit) {
            const size_t dstByte = (bitOffset + k) / 8;
            const size_t dstBit = 7 - ((bitOffset + k) % 8);
            if (bit)
                buffer[dstByte] |= (1u << dstBit);
            else
                buffer[dstByte] &= ~(1u << dstBit);
            ++k;
        };
        for (size_t b = 0; b < fullBytes; ++b)
            for (size_t j = 0; j < 8; ++j)
                put((data[b] >> (7 - j)) & 1);
        if (rem > 0)
            for (size_t j = 0; j < rem; ++j)
                put((data[fullBytes] >> (rem - 1 - j)) & 1);
        bitOffset += numBits;
    }
};

BitStreamWriter::BitStreamWriter() : pImpl_(std::make_unique<Impl>()) {}
BitStreamWriter::~BitStreamWriter() = default;

void BitStreamWriter::Write(uint8_t value) {
    pImpl_->WriteBitsInternal(&value, 8);
}

void BitStreamWriter::Write(uint16_t value) {
    uint8_t bytes[2];
    memcpy(bytes, &value, 2);
    pImpl_->WriteBitsInternal(bytes, 16);
}

void BitStreamWriter::Write(uint32_t value) {
    uint8_t bytes[4];
    memcpy(bytes, &value, 4);
    pImpl_->WriteBitsInternal(bytes, 32);
}

void BitStreamWriter::Write(int32_t value) {
    uint8_t bytes[4];
    memcpy(bytes, &value, 4);
    pImpl_->WriteBitsInternal(bytes, 32);
}

void BitStreamWriter::Write(float value) {
    uint8_t bytes[4];
    memcpy(bytes, &value, 4);
    pImpl_->WriteBitsInternal(bytes, 32);
}

void BitStreamWriter::Write(const char* str) {
    if (!str) {
        Write(static_cast<uint16_t>(0));
        return;
    }
    std::string normalized = NormalizeAscii(str);
    uint16_t len = static_cast<uint16_t>(std::min(normalized.size(), size_t{512}));
    Write(len);
    pImpl_->WriteBitsInternal(reinterpret_cast<const uint8_t*>(normalized.data()), len * 8);
}

void BitStreamWriter::WriteBytes(const uint8_t* data, size_t length) {
    if (data && length > 0) {
        pImpl_->WriteBitsInternal(data, length * 8);
    }
}

void BitStreamWriter::WriteBits(const uint8_t* data, size_t bitCount) {
    if (data && bitCount > 0) {
        pImpl_->WriteBitsInternal(data, bitCount);
    }
}

const uint8_t* BitStreamWriter::GetData() const {
    return pImpl_->buffer.data();
}

size_t BitStreamWriter::GetByteLength() const {
    return (pImpl_->bitOffset + 7) / 8;
}

size_t BitStreamWriter::GetBitLength() const {
    return pImpl_->bitOffset;
}

void BitStreamWriter::Reset() {
    pImpl_->buffer.clear();
    pImpl_->bitOffset = 0;
}

// ---------------------------------------------------------------------------
// BitStreamReader Implementation
// ---------------------------------------------------------------------------
struct BitStreamReader::Impl {
    const uint8_t* data;
    size_t bitLength;
    size_t readBitOffset = 0;

    // Exact mirror of WriteBitsInternal (RakNet ReadBits with
    // alignBitsToRight=true): full output bytes fill MSB-first; a trailing
    // partial byte receives the stream bits right-aligned (first stream bit
    // lands at bit rem-1).
    bool ReadBitsInternal(uint8_t* out, size_t numBits) {
        const size_t fullBytes = numBits / 8;
        const size_t rem = numBits % 8;
        if (numBits > bitLength - readBitOffset) return false;
        auto get = [&](size_t k) {
            const size_t pos = readBitOffset + k;
            return (data[pos / 8] >> (7 - (pos % 8))) & 1;
        };
        size_t k = 0;
        for (size_t b = 0; b < fullBytes; ++b) {
            uint8_t byte = 0;
            for (size_t j = 0; j < 8; ++j) byte |= static_cast<uint8_t>(get(k++) << (7 - j));
            out[b] = byte;
        }
        if (rem > 0) {
            uint8_t byte = 0;
            for (size_t j = 0; j < rem; ++j) byte |= static_cast<uint8_t>(get(k++) << (rem - 1 - j));
            out[fullBytes] = byte;
        }
        readBitOffset += numBits;
        return true;
    }
};

BitStreamReader::BitStreamReader(const uint8_t* data, size_t byteLength, size_t bitLength)
    : pImpl_(std::make_unique<Impl>()) {
    pImpl_->data = data;
    pImpl_->bitLength = bitLength ? std::min(bitLength,byteLength*8) : byteLength*8;
}

BitStreamReader::~BitStreamReader() = default;

bool BitStreamReader::Read(uint8_t& value) {
    value = 0;
    return pImpl_->ReadBitsInternal(&value, 8);
}

bool BitStreamReader::Read(uint16_t& value) {
    uint8_t bytes[2] = {};
    if (!pImpl_->ReadBitsInternal(bytes, 16)) return false;
    memcpy(&value, bytes, 2);
    return true;
}

bool BitStreamReader::Read(uint32_t& value) {
    uint8_t bytes[4] = {};
    if (!pImpl_->ReadBitsInternal(bytes, 32)) return false;
    memcpy(&value, bytes, 4);
    return true;
}

bool BitStreamReader::Read(int32_t& value) {
    uint8_t bytes[4] = {};
    if (!pImpl_->ReadBitsInternal(bytes, 32)) return false;
    memcpy(&value, bytes, 4);
    return true;
}

bool BitStreamReader::Read(float& value) {
    uint8_t bytes[4] = {};
    if (!pImpl_->ReadBitsInternal(bytes, 32)) return false;
    memcpy(&value, bytes, 4);
    return true;
}

bool BitStreamReader::ReadString(std::string& outStr, size_t maxLen) {
    uint16_t len = 0;
    if (!Read(len)) return false;
    if (len > maxLen) return false;
    if (len == 0) { outStr.clear(); return true; }
    std::vector<uint8_t> buf(len, 0);
    if (len > 0 && !pImpl_->ReadBitsInternal(buf.data(), len * 8)) return false;
    outStr.assign(reinterpret_cast<const char*>(buf.data()), len);
    return true;
}

bool BitStreamReader::ReadBytes(uint8_t* buffer, size_t length) {
    if (!buffer || length == 0) return true;
    return pImpl_->ReadBitsInternal(buffer, length * 8);
}

bool BitStreamReader::ReadBits(uint8_t* buffer, size_t bitCount) {
    if (!buffer || bitCount == 0) return true;
    return pImpl_->ReadBitsInternal(buffer, bitCount);
}

size_t BitStreamReader::GetReadOffsetBits() const {
    return pImpl_->readBitOffset;
}

size_t BitStreamReader::GetRemainingBits() const {
    return pImpl_->bitLength - pImpl_->readBitOffset;
}

void BitStreamReader::Reset() {
    pImpl_->readBitOffset = 0;
}

void BitStreamWriter::WriteBit(bool value) {
    uint8_t bit = value ? 1u : 0u;
    WriteBits(&bit, 1);
}

bool BitStreamReader::ReadBit(bool& out) {
    uint8_t bit = 0;
    if (!ReadBits(&bit, 1)) return false;
    out = (bit & 1u) != 0;
    return true;
}

// Same variable-length integer as the former free helpers: a 1-bit per all-zero
// high byte, then either a 1-bit + 4 low bits or a 0-bit + the whole low byte.
void BitStreamWriter::WriteCompressed(uint32_t value, int bytes) {
    if (bytes < 1 || bytes > 4) return;
    uint8_t raw[4] = {};
    for (int i = 0; i < 4; ++i) raw[i] = static_cast<uint8_t>((value >> (8 * i)) & 0xFFu);
    for (int i = bytes - 1; i > 0; --i) {
        if (raw[i] == 0) {
            WriteBit(true);
        } else {
            WriteBit(false);
            WriteBits(raw, static_cast<size_t>((i + 1) * 8));
            return;
        }
    }
    if ((raw[0] & 0xF0) == 0) {
        WriteBit(true);
        WriteBits(raw, 4);
    } else {
        WriteBit(false);
        Write(raw[0]);
    }
}

bool BitStreamReader::ReadCompressed(uint32_t& out, int bytes) {
    out = 0;
    if (bytes < 1 || bytes > 4) return false;
    for (int i = bytes - 1; i > 0; --i) {
        bool empty = false;
        if (!ReadBit(empty)) return false;
        if (!empty) {
            uint8_t buf[4] = {};
            if (!ReadBits(buf, static_cast<size_t>((i + 1) * 8))) return false;
            for (int b = 0; b <= i; ++b) out |= static_cast<uint32_t>(buf[b]) << (8 * b);
            return true;
        }
    }
    bool highEmpty = false;
    if (!ReadBit(highEmpty)) return false;
    if (!highEmpty) {
        uint8_t low = 0;
        if (!Read(low)) return false;
        out = low;
        return true;
    }
    uint8_t nibble = 0;
    if (!ReadBits(&nibble, 4)) return false;
    out = nibble & 0x0Fu;
    return true;
}

// RakNet BitStream.h 1328-1336. Clamped to [-1, 1], then a truncated uint16.
void BitStreamWriter::WriteCompressed(float value) {
    float varCopy = value;
    if (varCopy < -1.0f) varCopy = -1.0f;
    if (varCopy > 1.0f) varCopy = 1.0f;
    Write(static_cast<uint16_t>((varCopy + 1.0f) * 32767.5f));
}

bool BitStreamReader::ReadCompressed(float& value) {
    uint16_t compressed = 0;
    if (!Read(compressed)) return false;
    value = (static_cast<float>(compressed) / 32767.5f) - 1.0f;
    return true;
}

namespace {
uint16_t QuantizeNormComponent(float c) {
    const double scaled = std::isfinite(c) ? std::clamp(static_cast<double>(c), -1.0, 1.0) : 0.0;
    const long raw = std::lround((scaled + 1.0) * 32767.5);
    return static_cast<uint16_t>(std::clamp(raw, 0L, 65535L));
}
} // namespace

// VehicleSync encoding, moved here so the free functions can delegate.
void BitStreamWriter::WriteNormVector(float x, float y, float z) {
    WriteBit(x < 0.0f);
    const bool yZero = (y == 0.0f);
    WriteBit(yZero);
    if (!yZero) Write(QuantizeNormComponent(y));
    const bool zZero = (z == 0.0f);
    WriteBit(zZero);
    if (!zZero) Write(QuantizeNormComponent(z));
}

bool BitStreamReader::ReadNormVector(float& x, float& y, float& z) {
    bool xNeg = false;
    bool yZero = false;
    bool zZero = false;
    if (!ReadBit(xNeg) || !ReadBit(yZero)) return false;
    float yv = 0.0f;
    float zv = 0.0f;
    if (!yZero) {
        uint16_t raw = 0;
        if (!Read(raw)) return false;
        yv = (static_cast<float>(raw) / 32767.5f) - 1.0f;
    }
    if (!ReadBit(zZero)) return false;
    if (!zZero) {
        uint16_t raw = 0;
        if (!Read(raw)) return false;
        zv = (static_cast<float>(raw) / 32767.5f) - 1.0f;
    }
    const float rest = 1.0f - yv * yv - zv * zv;
    float xv = rest > 0.0f ? std::sqrt(rest) : 0.0f;
    if (xNeg) xv = -xv;
    x = xv;
    y = yv;
    z = zv;
    return true;
}

// RakNet BitStream::WriteVector: magnitude, then compressed unit components.
void BitStreamWriter::WriteVector(float x, float y, float z) {
    const float magnitude = std::sqrt(x * x + y * y + z * z);
    Write(magnitude);
    if (magnitude > 0.00001f) {
        WriteCompressed(x / magnitude);
        WriteCompressed(y / magnitude);
        WriteCompressed(z / magnitude);
    }
}

bool BitStreamReader::ReadVector(float& x, float& y, float& z) {
    float magnitude = 0.0f;
    if (!Read(magnitude)) return false;
    if (magnitude > 0.00001f) {
        float cx = 0.0f;
        float cy = 0.0f;
        float cz = 0.0f;
        ReadCompressed(cx);
        ReadCompressed(cy);
        if (!ReadCompressed(cz)) return false;
        x = cx * magnitude;
        y = cy * magnitude;
        z = cz * magnitude;
    } else {
        x = 0.0f;
        y = 0.0f;
        z = 0.0f;
    }
    return true;
}

// RakNet BitStream::WriteNormQuat: four sign bits, then |x| |y| |z| as uint16. w is rebuilt.
void BitStreamWriter::WriteNormQuat(float w, float x, float y, float z) {
    WriteBit(w < 0.0);
    WriteBit(x < 0.0);
    WriteBit(y < 0.0);
    WriteBit(z < 0.0);
    Write(static_cast<uint16_t>(std::fabs(static_cast<double>(x)) * 65535.0));
    Write(static_cast<uint16_t>(std::fabs(static_cast<double>(y)) * 65535.0));
    Write(static_cast<uint16_t>(std::fabs(static_cast<double>(z)) * 65535.0));
}

bool BitStreamReader::ReadNormQuat(float& w, float& x, float& y, float& z) {
    bool cwNeg = false;
    bool cxNeg = false;
    bool cyNeg = false;
    bool czNeg = false;
    uint16_t cx = 0;
    uint16_t cy = 0;
    uint16_t cz = 0;
    if (!ReadBit(cwNeg) || !ReadBit(cxNeg) || !ReadBit(cyNeg) || !ReadBit(czNeg)) return false;
    if (!Read(cx) || !Read(cy) || !Read(cz)) return false;
    x = static_cast<float>(cx / 65535.0);
    y = static_cast<float>(cy / 65535.0);
    z = static_cast<float>(cz / 65535.0);
    if (cxNeg) x = -x;
    if (cyNeg) y = -y;
    if (czNeg) z = -z;
    float difference = 1.0f - x * x - y * y - z * z;
    if (difference < 0.0f) difference = 0.0f;
    w = std::sqrt(difference);
    if (cwNeg) w = -w;
    return true;
}

// RakNet BitStream::WriteOrthMatrix: matrix to quaternion, then WriteNormQuat.
void BitStreamWriter::WriteOrthMatrix(float m00, float m01, float m02,
                                      float m10, float m11, float m12,
                                      float m20, float m21, float m22) {
    double qw = 0.0;
    double qx = 0.0;
    double qy = 0.0;
    double qz = 0.0;
    float sum = 1.0f + m00 + m11 + m22;
    if (sum < 0.0f) sum = 0.0f;
    qw = std::sqrt(static_cast<double>(sum)) / 2;
    sum = 1.0f + m00 - m11 - m22;
    if (sum < 0.0f) sum = 0.0f;
    qx = std::sqrt(static_cast<double>(sum)) / 2;
    sum = 1.0f - m00 + m11 - m22;
    if (sum < 0.0f) sum = 0.0f;
    qy = std::sqrt(static_cast<double>(sum)) / 2;
    sum = 1.0f - m00 - m11 + m22;
    if (sum < 0.0f) sum = 0.0f;
    qz = std::sqrt(static_cast<double>(sum)) / 2;
    if (qw < 0.0) qw = 0.0;
    if (qx < 0.0) qx = 0.0;
    if (qy < 0.0) qy = 0.0;
    if (qz < 0.0) qz = 0.0;
    qx = std::copysign(qx, static_cast<double>(m21 - m12));
    qy = std::copysign(qy, static_cast<double>(m02 - m20));
    qz = std::copysign(qz, static_cast<double>(m10 - m01));
    WriteNormQuat(static_cast<float>(qw), static_cast<float>(qx),
                  static_cast<float>(qy), static_cast<float>(qz));
}

bool BitStreamReader::ReadOrthMatrix(float& m00, float& m01, float& m02,
                                     float& m10, float& m11, float& m12,
                                     float& m20, float& m21, float& m22) {
    float qw = 0.0f;
    float qx = 0.0f;
    float qy = 0.0f;
    float qz = 0.0f;
    if (!ReadNormQuat(qw, qx, qy, qz)) return false;
    const double sqw = static_cast<double>(qw) * static_cast<double>(qw);
    const double sqx = static_cast<double>(qx) * static_cast<double>(qx);
    const double sqy = static_cast<double>(qy) * static_cast<double>(qy);
    const double sqz = static_cast<double>(qz) * static_cast<double>(qz);
    m00 = static_cast<float>(sqx - sqy - sqz + sqw);
    m11 = static_cast<float>(-sqx + sqy - sqz + sqw);
    m22 = static_cast<float>(-sqx - sqy + sqz + sqw);
    double tmp1 = static_cast<double>(qx) * static_cast<double>(qy);
    double tmp2 = static_cast<double>(qz) * static_cast<double>(qw);
    m10 = static_cast<float>(2.0 * (tmp1 + tmp2));
    m01 = static_cast<float>(2.0 * (tmp1 - tmp2));
    tmp1 = static_cast<double>(qx) * static_cast<double>(qz);
    tmp2 = static_cast<double>(qy) * static_cast<double>(qw);
    m20 = static_cast<float>(2.0 * (tmp1 - tmp2));
    m02 = static_cast<float>(2.0 * (tmp1 + tmp2));
    tmp1 = static_cast<double>(qy) * static_cast<double>(qz);
    tmp2 = static_cast<double>(qx) * static_cast<double>(qw);
    m21 = static_cast<float>(2.0 * (tmp1 + tmp2));
    m12 = static_cast<float>(2.0 * (tmp1 - tmp2));
    return true;
}

namespace {
void WriteNormVectorVehicleSync(BitStreamWriter& w, float x, float y, float z) {
    auto flag = [&](bool on) {
        uint8_t bit = on ? 1u : 0u;
        w.WriteBits(&bit, 1);
    };
    flag(x < 0.0f);
    const bool yZero = (y == 0.0f);
    flag(yZero);
    if (!yZero) w.Write(QuantizeNormComponent(y));
    const bool zZero = (z == 0.0f);
    flag(zZero);
    if (!zZero) w.Write(QuantizeNormComponent(z));
}

bool SameStream(const BitStreamWriter& a, const BitStreamWriter& b) {
    if (a.GetBitLength() != b.GetBitLength() || a.GetByteLength() != b.GetByteLength()) return false;
    const size_t n = a.GetByteLength();
    if (n == 0) return true;
    return std::memcmp(a.GetData(), b.GetData(), n) == 0;
}
} // namespace

bool NormVectorBytesMatchVehicleSync() {
    const float samples[][3] = {
        {0.0f, 0.0f, 0.0f},
        {1.0f, 0.0f, 0.0f},
        {-1.0f, 0.0f, 0.0f},
        {0.0f, 0.5f, 0.0f},
        {0.0f, 0.0f, -0.25f},
        {0.2f, 0.3f, 0.4f},
        {-0.6f, -0.8f, 0.0f},
    };
    for (const auto& sample : samples) {
        BitStreamWriter current;
        BitStreamWriter legacy;
        current.WriteNormVector(sample[0], sample[1], sample[2]);
        WriteNormVectorVehicleSync(legacy, sample[0], sample[1], sample[2]);
        if (!SameStream(current, legacy)) return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// RakNetWrapper Implementation
// ---------------------------------------------------------------------------
namespace {
RakNetWrapper* g_liveWrapper = nullptr;
}

// MTA handshake states
enum class HandshakeState : uint8_t {
    IDLE = 0,
    RAKNET_CONNECTING,
    RAKNET_CONNECTED,       // RakNet CONNECTION_REQUEST_ACCEPTED received
    JOIN_SENT,              // SendJoinRequest dispatched
    HANDSHAKE_COMPLETE,     // Server accepted join (user packet or spawn)
    FAILED
};

struct RakNetWrapper::Impl {
    Mta::ClientTransport adapter;
    PacketHandlerCallback packetHandler;
    ConnectionStateCallback connectionStateCallback;
    std::atomic<bool> ready{false};
    std::atomic<bool> running{false};
    std::atomic<HandshakeState> handshakeState{HandshakeState::IDLE};
    uint32_t timeoutMs = 10000;
    std::string localIP = "0.0.0.0";
    std::string serverHost;
    uint16_t serverPort = 0;
    uint16_t clientPort = 0;
    std::atomic<uint8_t> connectionError{0};
    char serialBuffer[64] = {};
    uint32_t startTime = 0;
    std::string pendingNickname;
    std::string pendingSerial;
    // Wire packet id 0..255. Application packets use 99 + ePacketID.
    std::array<PacketUsage, 256> usageIn{};
    std::array<PacketUsage, 256> usageOut{};
};

RakNetWrapper::RakNetWrapper() : pImpl_(std::make_unique<Impl>()) {}
RakNetWrapper::~RakNetWrapper() {
    Shutdown();
    if (g_liveWrapper == this) g_liveWrapper = nullptr;
}

bool RakNetWrapper::StartNetwork(const char* serverHost, uint16_t serverPort, bool /*packetTag*/) {
    if (pImpl_->running.load()) {
        g_liveWrapper = this;
        return true;
    }

    if (!pImpl_->adapter.startup(1, 1)) {
        LOGE("RakNetWrapper: adapter startup failed");
        return false;
    }

    // connect() replaces the transport, so the per-id slots start with it.
    pImpl_->usageIn = {};
    pImpl_->usageOut = {};
    if (!pImpl_->adapter.connect(serverHost, serverPort)) {
        LOGE("RakNetWrapper: adapter connect failed for %s:%u", serverHost, serverPort);
        return false;
    }

    pImpl_->serverHost = serverHost ? serverHost : "";
    pImpl_->serverPort = serverPort;
    pImpl_->startTime = static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    pImpl_->handshakeState.store(HandshakeState::RAKNET_CONNECTING);
    pImpl_->ready.store(true);
    pImpl_->running.store(true);
    g_liveWrapper = this;
    LOGI("MTA native transport opening %s:%u", serverHost, serverPort);

    return true;
}

void RakNetWrapper::StopNetwork() {
    pImpl_->running.store(false);
    pImpl_->ready.store(false);
    pImpl_->adapter.shutdown();
}

bool RakNetWrapper::IsReady() const {
    return pImpl_->ready.load();
}

bool RakNetWrapper::IsConnected() const {
    return pImpl_->adapter.isConnected();
}

void RakNetWrapper::DoPulse() {
    if (!pImpl_->running.load()) return;

    // Transport-level timeout covers only the RakNet connect phase. Once
    // CONNECTION_REQUEST_ACCEPTED arrives, the NeonJoinClient owns per-step
    // timeouts for the MTA application join (MOD_NAME/JOINDATA/JOINEDGAME).
    auto currentState = pImpl_->handshakeState.load();
    if (currentState == HandshakeState::RAKNET_CONNECTING) {
        uint32_t now = static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
        uint32_t elapsed = now - pImpl_->startTime;
        if (elapsed > pImpl_->timeoutMs) {
            LOGE("RakNetWrapper: connection timeout after %ums to %s:%u (state=%d)",
                 elapsed, pImpl_->serverHost.c_str(), pImpl_->serverPort, static_cast<int>(currentState));
            pImpl_->handshakeState.store(HandshakeState::FAILED);
            pImpl_->connectionError.store(14); // Timeout
            if (pImpl_->connectionStateCallback) {
                pImpl_->connectionStateCallback(false, "Connection timed out");
            }
            return;
        }
    }

    // Resource client-script bundles are reassembled far above one MTU.
    // A 64 KiB stack buffer used to make the transport close the session.
    static thread_local std::vector<uint8_t> recvBuf;
    constexpr size_t kReceiveCap = 4 * 1024 * 1024;
    if (recvBuf.size() < kReceiveCap) recvBuf.resize(kReceiveCap);
    // One frame used to take a single datagram while Pulse() had already
    // queued the rest. The queue then hit its limit and the transport
    // reported connection lost. Drain the burst in this pulse.
    for (int burst = 0; burst < 48; ++burst) {
    size_t receivedBits = 0;
    int received = pImpl_->adapter.receive(recvBuf.data(), static_cast<int>(recvBuf.size()), &receivedBits);
    if (received <= 0) {
        if (burst > 0) break;
        static uint32_t lastLogTime = 0;
        uint32_t now = static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
        if (now - lastLogTime > 2000) {
            LOGI("RakNetWrapper: DoPulse alive, no packets (state=%d, connected=%d)",
                 static_cast<int>(pImpl_->handshakeState.load()),
                 pImpl_->adapter.isConnected() ? 1 : 0);
            lastLogTime = now;
        }
        break;
    }

    const uint8_t rawId = recvBuf[0];
    // Slot is the wire id (99 + ePacketID for application packets), length is the
    // buffer receive() handed back.
    NoteUsage(true, rawId, static_cast<size_t>(received));
    uint8_t packetId = rawId;
    // MTA application IDs are RID_USER_PACKET_ENUM + ePacketID on the wire.
    // Preserve the existing local [gameId][body] callback representation.
    if (rawId >= static_cast<uint8_t>(PacketID::USER_PACKET_ENUM)) {
        packetId = static_cast<uint8_t>(PacketID::USER_PACKET_ENUM);
        recvBuf[0] = rawId - packetId;
    }
    const bool chatty = rawId == 131 || rawId == 132 || rawId == 150 || rawId == 155 || rawId == 162 || rawId == 176;
    if (!chatty) LOGI("MTA native received wire ID=%u bytes=%d bits=%zu", rawId, received, receivedBits);
    const uint8_t* payload = recvBuf.data() + (packetId == 99 ? 0 : 1);
    size_t payloadLen = static_cast<size_t>(received - (packetId == 99 ? 0 : 1));
    size_t payloadBits = packetId == 99 ? receivedBits : (receivedBits >= 8 ? receivedBits - 8 : 0);

    // Handle RakNet internal connection-state packets
    switch (packetId) {
        case static_cast<uint8_t>(PacketID::CONNECTION_REQUEST_ACCEPTED): {
            LOGI("RakNetWrapper: CONNECTION_REQUEST_ACCEPTED from %s:%u",
                 pImpl_->serverHost.c_str(), pImpl_->serverPort);
            pImpl_->handshakeState.store(HandshakeState::RAKNET_CONNECTED);

            // The MTA application join (PLAYER_JOIN -> MOD_NAME ->
            // PLAYER_JOINDATA -> ...) is owned by NeonJoinClient, which polls
            // IsRakNetConnected() in its Pulse(). No join is sent here.
            if (pImpl_->connectionStateCallback) {
                pImpl_->connectionStateCallback(true, "RakNet connected");
            }
            break;
        }
        case static_cast<uint8_t>(PacketID::CONNECTION_ATTEMPT_FAILED): {
            LOGE("RakNetWrapper: CONNECTION_ATTEMPT_FAILED to %s:%u",
                 pImpl_->serverHost.c_str(), pImpl_->serverPort);
            pImpl_->handshakeState.store(HandshakeState::FAILED);
            pImpl_->connectionError.store(15);
            if (pImpl_->connectionStateCallback) {
                pImpl_->connectionStateCallback(false, "Connection attempt failed");
            }
            break;
        }
        case static_cast<uint8_t>(PacketID::DISCONNECTION_NOTIFICATION): {
            LOGI("RakNetWrapper: DISCONNECTION_NOTIFICATION");
            pImpl_->handshakeState.store(HandshakeState::IDLE);
            if (pImpl_->connectionStateCallback) {
                pImpl_->connectionStateCallback(false, "Disconnected by server");
            }
            break;
        }
        case static_cast<uint8_t>(PacketID::CONNECTION_LOST): {
            LOGE("RakNetWrapper: CONNECTION_LOST");
            pImpl_->handshakeState.store(HandshakeState::IDLE);
            if (pImpl_->connectionStateCallback) {
                pImpl_->connectionStateCallback(false, "Connection lost");
            }
            break;
        }
        case static_cast<uint8_t>(PacketID::CONNECTION_BANNED): {
            LOGE("RakNetWrapper: CONNECTION_BANNED");
            pImpl_->handshakeState.store(HandshakeState::FAILED);
            pImpl_->connectionError.store(22);
            if (pImpl_->connectionStateCallback) {
                pImpl_->connectionStateCallback(false, "Banned from server");
            }
            break;
        }
        case static_cast<uint8_t>(PacketID::INVALID_PASSWORD): {
            LOGE("RakNetWrapper: INVALID_PASSWORD");
            pImpl_->handshakeState.store(HandshakeState::FAILED);
            pImpl_->connectionError.store(23);
            if (pImpl_->connectionStateCallback) {
                pImpl_->connectionStateCallback(false, "Invalid password");
            }
            break;
        }
        default: {
            // User/game packets — forward to registered handler
            if (pImpl_->packetHandler) {
                bool handled = pImpl_->packetHandler(packetId, payload, payloadLen, payloadBits);
                (void)handled; // Only NeonJoinClient can accept the session.
            }
            break;
        }
    }
    }
}

void RakNetWrapper::Shutdown() {
    StopNetwork();
}

void RakNetWrapper::SetPacketHandler(PacketHandlerCallback handler) {
    pImpl_->packetHandler = std::move(handler);
}

bool RakNetWrapper::SendPacket(uint8_t packetId, const BitStreamWriter& stream,
                               PacketPriority priority, PacketReliability reliability,
                               PacketOrdering ordering) {
    if (!pImpl_->running.load() || !pImpl_->adapter.isConnected()) return false;

    const size_t dataLen = stream.GetByteLength();
    std::vector<uint8_t> sendBuf;
    size_t bits = stream.GetBitLength();
    if (packetId == static_cast<uint8_t>(PacketID::USER_PACKET_ENUM)) {
        if (bits < 8 || stream.GetData()[0] > 156) return false;
        sendBuf.assign(stream.GetData(),stream.GetData()+dataLen);
        sendBuf[0] += packetId;
    } else {
        sendBuf.push_back(packetId);
        if (dataLen) sendBuf.insert(sendBuf.end(),stream.GetData(),stream.GetData()+dataLen);
        bits += 8;
    }
    const bool sent = pImpl_->adapter.send(sendBuf.data(), static_cast<int>(sendBuf.size()),
                                           static_cast<int>(priority), static_cast<int>(reliability),
                                           static_cast<int>(ordering), bits);
    // sendBuf[0] is the wire id: the packetId argument, or 99 + the game byte
    // when that argument is USER_PACKET_ENUM.
    if (sent && !sendBuf.empty()) NoteUsage(false, sendBuf[0], sendBuf.size());
    return sent;
}

void RakNetWrapper::SetClientPort(uint16_t port) {
    pImpl_->clientPort = port;
}

std::string RakNetWrapper::GetConnectedServer(bool includePort) const {
    if (!pImpl_->adapter.isConnected()) return "";
    if (includePort) {
        return pImpl_->serverHost + ":" + std::to_string(pImpl_->serverPort);
    }
    return pImpl_->serverHost;
}

bool RakNetWrapper::GetNetworkStatistics(NetworkStatistics& stats) const {
    memset(&stats, 0, sizeof(stats));
    const Mta::FrameCounters frames = pImpl_->adapter.frameCounters();
    stats.bytesReceived = frames.bytesReceived;
    stats.bytesSent = frames.bytesSent;
    stats.packetsReceived = frames.packetsReceived;
    stats.packetsSent = frames.packetsSent;
    stats.messagesInResendBuffer = frames.messagesInResendBuffer;
    // packetlossTotal stays 0: the transport does not count lost datagrams.
    // packetlossLastSecond stays 0: same.
    // messagesInSendBuffer stays 0: Message() transmits immediately. There is no
    // unsent queue. ping is tracked by getPing() and is not a getNetworkStats key.
    // isLimitedByCongestionControl stays 0: no congestion controller.
    // isLimitedByOutgoingBandwidthLimit stays 0: no outgoing bandwidth cap.
    return true;
}

void RakNetWrapper::CopyPacketUsage(std::array<PacketUsage, 256>& incoming,
                                    std::array<PacketUsage, 256>& outgoing) const {
    incoming = pImpl_->usageIn;
    outgoing = pImpl_->usageOut;
}

RakNetWrapper* RakNetWrapper::Live() {
    return g_liveWrapper;
}

void RakNetWrapper::NoteUsage(bool incoming, uint8_t id, size_t bytes) {
    PacketUsage& slot = incoming ? pImpl_->usageIn[id] : pImpl_->usageOut[id];
    slot.count += 1;
    slot.totalBytes += static_cast<uint64_t>(bytes);
    g_liveWrapper = this;
}

int RakNetWrapper::GetPing() const {
    return pImpl_->adapter.getPing();
}

uint32_t RakNetWrapper::GetTime() const {
    return static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}

std::string RakNetWrapper::GetLocalIP() const {
    return pImpl_->localIP;
}

void RakNetWrapper::GetSerial(char* buffer, size_t maxLength) const {
    if (!buffer || maxLength == 0) return;
    size_t copyLen = std::min(maxLength - 1, strlen(pImpl_->serialBuffer));
    memcpy(buffer, pImpl_->serialBuffer, copyLen);
    buffer[copyLen] = '\0';
}

uint8_t RakNetWrapper::GetConnectionError() const {
    return pImpl_->connectionError.load();
}

void RakNetWrapper::SetConnectionError(uint8_t error) {
    pImpl_->connectionError.store(error);
}

void RakNetWrapper::Reset() {
    StopNetwork();
    pImpl_->adapter.shutdown();
    pImpl_->packetHandler = nullptr;
    pImpl_->timeoutMs = 10000;
}

void RakNetWrapper::SetTimeoutTime(uint32_t timeoutMs) {
    pImpl_->timeoutMs = timeoutMs;
}

bool RakNetWrapper::SendOpenPacket() {
    BitStreamWriter stream;
    stream.Write(static_cast<uint8_t>(PacketID::OPEN_CONNECTION_REQUEST));
    // MTA open packet contains protocol version and game version
    stream.Write(static_cast<uint32_t>(1)); // protocol version placeholder
    return SendPacket(static_cast<uint8_t>(PacketID::OPEN_CONNECTION_REQUEST),
                      stream, PacketPriority::HIGH, PacketReliability::RELIABLE);
}

bool RakNetWrapper::SendJoinRequest(const std::string& nickname, const std::string& serial) {
    // Store credentials for deferred send if RakNet hasn't connected yet
    pImpl_->pendingNickname = nickname;
    pImpl_->pendingSerial = serial;

    auto state = pImpl_->handshakeState.load();
    if (state != HandshakeState::RAKNET_CONNECTED && state != HandshakeState::JOIN_SENT) {
        LOGI("RakNetWrapper: JoinRequest deferred (state=%d, waiting for RakNet connect)",
             static_cast<int>(state));
        return true; // Will be sent automatically when CONNECTION_REQUEST_ACCEPTED arrives
    }

    // Envelope: SendPacket() writes the RID_USER_PACKET_ENUM (99) prefix byte
    // exactly once; the stream carries only the game packet body. Writing the
    // UPE byte here too would double-prefix the join on the wire.
    // TODO(P1B.2): replace simplified body with real PLAYER_JOINDATA layout
    // from Client/mods/deathmatch/logic/CPacketHandler.cpp.
    BitStreamWriter stream;
    stream.Write(NormalizeAscii(nickname).c_str());
    stream.Write(NormalizeAscii(serial).c_str());
    bool sent = SendPacket(static_cast<uint8_t>(PacketID::USER_PACKET_ENUM),
                           stream, PacketPriority::HIGH, PacketReliability::RELIABLE_ORDERED);
    if (sent) {
        pImpl_->handshakeState.store(HandshakeState::JOIN_SENT);
        LOGI("RakNetWrapper: JoinRequest sent for '%s'", nickname.c_str());
    }
    return sent;
}

void RakNetWrapper::SetConnectionStateCallback(ConnectionStateCallback callback) {
    pImpl_->connectionStateCallback = std::move(callback);
}

bool RakNetWrapper::IsHandshakeComplete() const {
    return pImpl_->handshakeState.load() == HandshakeState::HANDSHAKE_COMPLETE;
}

void RakNetWrapper::MarkSessionAccepted() {
    if (IsRakNetConnected()) pImpl_->handshakeState.store(HandshakeState::HANDSHAKE_COMPLETE);
}

bool RakNetWrapper::IsRakNetConnected() const {
    const auto s = pImpl_->handshakeState.load();
    return s == HandshakeState::RAKNET_CONNECTED ||
           s == HandshakeState::JOIN_SENT ||
           s == HandshakeState::HANDSHAKE_COMPLETE;
}

bool RakNetWrapper::VerifySerialSignature(const uint8_t* data, size_t length) {
    // No signature verifier is linked yet. Never claim a successful check.
    (void)data;
    (void)length;
    return false;
}

} // namespace WorldRPG::Net
