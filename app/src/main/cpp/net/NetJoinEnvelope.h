#pragma once
#include "JoinDataCodec.h"
#include <array>
#include <string>

namespace WorldRPG::Net {
// The network module consumes this before forwarding logical JOINDATA to CGame.
// Verified reader: ARM64 net.so SHA256 e6d7ea540ff223927d2ab39474050b6747eb2f64d79422bd11eb683b3d4a7338,
// 0x13d9c0..0x13e4c8: version, encoded serial/extra, version fields, build checksum,
// optional module-data vector, alignment, then the game payload. No runtime patches.
inline bool BuildNetJoinEnvelope(const BitStreamWriter& game, const std::string& serial, BitStreamWriter& out) {
    if (game.GetBitLength() < 8 || game.GetData()[0] != NEON_JOIN_DATA_ID || serial.size() != 32) return false;
    for (const char c : serial) if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F'))) return false;
    constexpr std::array<uint8_t, 32> mask{
        0xd0,0xd2,0xd7,0xda,0xc5,0xf4,0x97,0x56,0xd8,0xda,0xdf,0xd2,0xcd,0xfc,0x9f,0x5e,
        0xc0,0xc2,0xc7,0xca,0xd5,0xe4,0x87,0x46,0xc8,0xca,0xcf,0xc2,0xdd,0xec,0x8f,0x4e};
    out.Write(NEON_JOIN_DATA_ID);
    out.Write(static_cast<uint16_t>(0x0030));
    std::array<uint8_t, 32> encodedSerial{};
    for (size_t i=0; i<32; ++i) encodedSerial[i] = static_cast<uint8_t>(serial[i]) ^ mask[i];
    out.WriteBytes(encodedSerial.data(), encodedSerial.size());
    // Android installation identity, not a fabricated Windows hardware attestation.
    const std::string extra = serial + serial;
    out.Write(static_cast<uint16_t>(extra.size()));
    for (size_t i=0; i<extra.size(); ++i)
        out.Write(static_cast<uint8_t>(static_cast<uint8_t>(extra[i]) ^ (1u << (i%8)) ^ (0xd4u ^ i)));
    for (const uint8_t component : {1,7,0,9}) out.WriteBits(&component,7);
    const uint32_t build = 29999;
    out.WriteBits(reinterpret_cast<const uint8_t*>(&build),23);
    out.WriteCompressed(0,2); // network revision
    const uint32_t check = build ^ 0x54ABCDu;
    out.WriteBits(reinterpret_cast<const uint8_t*>(&check),23);
    out.Write(static_cast<uint16_t>(0)); // no proprietary AC module report on Android
    const uint8_t zero = 0;
    while (out.GetBitLength()%8) out.WriteBits(&zero,1);
    // Serialized streams store their final partial byte MSB-aligned, while
    // WriteBits expects its partial input byte right-aligned. Preserve that
    // last bit: JWT signatures end in an odd ASCII character and losing it
    // changes the signature although an anonymous join still succeeds.
    const size_t bodyBits = game.GetBitLength()-8;
    out.WriteBytes(game.GetData()+1,bodyBits/8);
    if (const auto tailBits = bodyBits%8) {
        const uint8_t tail = game.GetData()[1+bodyBits/8] >> (8-tailBits);
        out.WriteBits(&tail,tailBits);
    }
    return true;
}
} // namespace WorldRPG::Net
