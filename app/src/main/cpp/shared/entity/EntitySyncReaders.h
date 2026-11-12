#pragma once

// Shared wire readers for ENTITY_ADD bodies (vehicles and objects), 0x0030 layout.
// Each reader mirrors one reference sync struct in mta_neon_source Shared/sdk/net/SyncStructures.h.

#include <cstddef>
#include <cstdint>

#include "EntityAddTypes.h"

namespace WorldRPG::Entity {

// SFloatAsBitsSync<bits>(min, max): value = min + (max - min) * raw / (2^bits - 1). Bits read LSB first.
inline bool ReadFloatAsBits(Net::BitStreamReader& reader, unsigned bits, float minValue, float maxValue, float& out) {
    uint8_t raw[4] = {};
    if (!reader.ReadBits(raw, bits)) return false;
    uint32_t value = static_cast<uint32_t>(raw[0]) | (static_cast<uint32_t>(raw[1]) << 8) |
                     (static_cast<uint32_t>(raw[2]) << 16) | (static_cast<uint32_t>(raw[3]) << 24);
    const float maxRaw = static_cast<float>((1ull << bits) - 1);
    const float alpha = static_cast<float>(value) / maxRaw;
    out = (maxRaw > 0.f) ? (maxValue - minValue) * alpha + minValue : minValue;
    return true;
}

// SVehicleHealthSync: 12 bits, 0 - 2047.5.
inline bool ReadVehicleHealth(Net::BitStreamReader& reader, float& out) {
    return ReadFloatAsBits(reader, 12, 0.f, 2047.5f, out);
}

// SObjectHealthSync: 11 bits, 0 - 1023.5.
inline bool ReadObjectHealth(Net::BitStreamReader& reader, float& out) {
    return ReadFloatAsBits(reader, 11, 0.f, 1023.5f, out);
}

// SRotationDegreesSync(false): three uint16 values, 360 / 65536 degrees per step.
inline bool ReadRotationDegrees(Net::BitStreamReader& reader, float& x, float& y, float& z) {
    uint16_t rx = 0, ry = 0, rz = 0;
    if (!reader.Read(rx) || !reader.Read(ry) || !reader.Read(rz)) return false;
    x = static_cast<float>(rx) * (360.f / 65536.f);
    y = static_cast<float>(ry) * (360.f / 65536.f);
    z = static_cast<float>(rz) * (360.f / 65536.f);
    return true;
}

// SRotationRadiansSync(false): three uint16 values, 2 * pi / 65536 radians per step.
inline bool ReadRotationRadians(Net::BitStreamReader& reader, float& x, float& y, float& z) {
    uint16_t rx = 0, ry = 0, rz = 0;
    if (!reader.Read(rx) || !reader.Read(ry) || !reader.Read(rz)) return false;
    constexpr float kStep = 6.283185307f / 65536.f;
    x = static_cast<float>(rx) * kStep;
    y = static_cast<float>(ry) * kStep;
    z = static_cast<float>(rz) * kStep;
    return true;
}

// SEntityAlphaSync: ReadCompressed(uchar), stored as 255 - value. Only the parse matters here.
inline bool ReadEntityAlpha(Net::BitStreamReader& reader, uint32_t& outAlpha) {
    uint32_t raw = 0;
    if (!ReadCompressed(reader, 1, raw)) return false;
    outAlpha = 255u - (raw & 0xFFu);
    return true;
}

// SVehiclePartStateSync with m_bDeltaSync = false: `count` elements of `bits` each, no change flags.
// Values are consumed; nothing stores them on this side yet.
inline bool SkipPartStates(Net::BitStreamReader& reader, unsigned count, unsigned bits) {
    for (unsigned i = 0; i < count; ++i) {
        uint8_t raw[1] = {};
        if (!reader.ReadBits(raw, bits)) return false;
    }
    return true;
}

} // namespace WorldRPG::Entity
