#pragma once

#include "Checksums.h"
#include "raknet/RakNetWrapper.h"
#include <algorithm>
#include <array>
#include <string>
#include <string_view>

namespace WorldRPG::Net {

constexpr uint16_t NEON_NETCODE_VERSION = 0x41E7;
constexpr uint8_t NEON_JOIN_DATA_ID = 4;
constexpr uint8_t LEGACY_MOBILE_JOIN_DATA_ID = 19;

enum class JoinProfile : uint8_t { Automatic = 0, Standard = 1, LegacyMobile = 2 };

// The old bridge is selected explicitly, independent of the server address.
constexpr JoinProfile SelectJoinProfile(JoinProfile requested) {
    return requested == JoinProfile::LegacyMobile ? JoinProfile::LegacyMobile : JoinProfile::Standard;
}

// CClientGame::StartGame (desktop CClientGame.cpp:715-769) and
// CPlayerJoinDataPacket::Read: retain the same field order and bit widths.
// Standard peers honestly advertise the 0x0030 layout implemented on Android.
// Legacy keeps the captured 763-byte layout, with the marker at 676 and the body at 668.
inline bool BuildJoinData(JoinProfile profile, const std::string& nick, const std::string& password,
                          const std::string& ticket, BitStreamWriter& out) {
    if ((profile != JoinProfile::Standard && profile != JoinProfile::LegacyMobile) || nick.empty() || nick.size() > 22 || ticket.size() > 4096 ||
        ticket.find('\0') != std::string::npos) return false;
    const bool legacy = profile == JoinProfile::LegacyMobile;
    // The deployed legacy adapter truncates the extracted body to 256 bytes.
    // Never silently truncate an Identity ticket or downgrade authentication.
    if (legacy && !ticket.empty()) return false;
    out.Write(legacy ? LEGACY_MOBILE_JOIN_DATA_ID : NEON_JOIN_DATA_ID);
    if (legacy) {
        const std::array<uint8_t, 667> padding{};
        out.WriteBytes(padding.data(), padding.size());
    }
    out.Write(legacy ? uint16_t{0x41E6} : NEON_NETCODE_VERSION);
    out.Write(static_cast<uint16_t>(0x0170));
    out.Write(static_cast<uint16_t>(legacy ? 0x003D : 0x0030));
    auto string = [&](std::string_view value) {
        out.Write(static_cast<uint16_t>(value.size()));
        if (!value.empty()) out.WriteBytes(reinterpret_cast<const uint8_t*>(value.data()), value.size());
    };
    string(legacy ? "1.6.0-9.29999.0" : "1.7.0-9.29999.0");
    const uint8_t optionalUpdate = 0;
    out.WriteBits(&optionalUpdate, 1);
    out.Write(static_cast<uint8_t>(20));
    std::array<uint8_t, 22> nickname{};
    std::copy(nick.begin(), nick.end(), nickname.begin());
    out.WriteBytes(nickname.data(), nickname.size());
    std::array<uint8_t, 16> digest{};
    if (!password.empty()) {
        Md5 md5;
        md5.Update(password.data(), password.size());
        md5.Finish(digest.data());
    }
    out.WriteBytes(digest.data(), digest.size());
    // Neon reads the platform from the retired community field. Both join
    // formats keep its width so the legacy adapter can forward it as-is.
    // Keep in sync with Shared/sdk/net/NeonClientPlatform.h on desktop/server.
    constexpr char platform[32] = "NEON:ANDROID";
    out.WriteBytes(reinterpret_cast<const uint8_t*>(platform), sizeof(platform));
    if (!legacy) string(ticket);
    return true;
}
} // namespace WorldRPG::Net
