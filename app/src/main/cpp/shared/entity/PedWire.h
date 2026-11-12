#pragma once

// Ped wire helpers shared by ENTITY_ADD (EntityAddPed.cpp) and PED_SYNC (PacketHandler.cpp).
// Widths follow mta_neon_source Shared/sdk/net/SyncStructures.h for the 0x0030 peer bitstream.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "../../net/raknet/RakNetWrapper.h"

namespace WorldRPG::Entity::PedWire {

constexpr float kPedPi = 3.14159265f;

// SFloatAsBitsSync<16>(-pi, pi): 16 raw bits mapped linearly onto [-pi, pi].
// Used by SPedRotationSync (ENTITY_ADD ped rotation).
inline bool ReadAngle16(Net::BitStreamReader& reader, float& out) {
    uint8_t raw[2] = {};
    if (!reader.ReadBits(raw, 16)) return false;
    const uint32_t value = static_cast<uint32_t>(raw[0]) | (static_cast<uint32_t>(raw[1]) << 8);
    out = -kPedPi + (static_cast<float>(value) / 65535.f) * (2.f * kPedPi);
    return true;
}

// SFloatAsBitsSync<8>(fMin, fMax): 8 raw bits mapped linearly onto [fMin, fMax].
// Used by SPlayerHealthSync (0..255) and SPlayerArmorSync (0..127.5).
inline bool ReadFloatBits8(Net::BitStreamReader& reader, float fMin, float fMax, float& out) {
    uint8_t raw = 0;
    if (!reader.Read(raw)) return false;
    const float alpha = static_cast<float>(raw) / 255.f;
    out = (fMax - fMin) * alpha + fMin;
    return true;
}

// Reads a raw ushort length (16 bits, not compressed) followed by that many bytes.
// Same wire form as NetBitStream::ReadString(std::string) in the reference.
inline bool ReadLengthPrefixedString(Net::BitStreamReader& reader, std::string& out) {
    uint16_t length = 0;
    out.clear();
    if (!reader.Read(length)) return false;
    if (length == 0) return true;
    std::vector<uint8_t> bytes(length);
    if (!reader.ReadBits(bytes.data(), static_cast<size_t>(length) * 8)) return false;
    out.assign(bytes.begin(), bytes.end());
    return true;
}

} // namespace WorldRPG::Entity::PedWire
