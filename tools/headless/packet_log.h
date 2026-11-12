// Field readers for the headless logs. Bit layouts mirror the port's own readers:
//   ElementID          PacketHandler.cpp ReadElementId (17 bits, all ones = invalid)
//   VEHICLE_INOUT      server-written: CVehicleInOutPacket::Write (seat before action)
//                      client-written: CVehicleInOutPacket::Read  (action, then seat for request_in)
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "net/raknet/RakNetWrapper.h"

namespace WorldRPG::Headless {

constexpr uint32_t kElementIdBits = 17;
constexpr uint32_t kNoElement = 0xFFFFFFFFu;

inline bool ReadElementIdBits(WorldRPG::Net::BitStreamReader& r, uint32_t& out) {
    uint8_t bytes[4] = {};
    if (!r.ReadBits(bytes, kElementIdBits)) return false;
    uint32_t value = 0;
    std::memcpy(&value, bytes, 4);
    out = value == (1u << kElementIdBits) - 1 ? kNoElement : value;
    return true;
}

inline bool ReadSmall(WorldRPG::Net::BitStreamReader& r, size_t bits, uint8_t& out) {
    uint8_t value = 0;
    if (!r.ReadBits(&value, bits)) return false;
    out = value;
    return true;
}

inline const char* VehInOutName(uint8_t action) {
    static const char* const kNames[] = {
        "request_in_confirmed", "notify_in_return", "notify_in_abort_return", "request_out_confirmed",
        "notify_out_return", "notify_out_abort_return", "notify_fell_off_return", "request_jack_confirmed",
        "notify_jack_return", "attempt_failed"};
    return action < 10 ? kNames[action] : "unknown";
}

struct VehInOutFields {
    uint32_t ped = kNoElement;
    uint32_t veh = kNoElement;
    uint8_t seat = 0;
    uint8_t action = 0;
    uint8_t door = 0xFF;
    uint8_t reason = 0;
};

// Body after the game id byte, as received from the server.
inline bool DecodeVehInOutServer(const uint8_t* body, size_t length, size_t bits, VehInOutFields& f) {
    WorldRPG::Net::BitStreamReader r(body, length, bits);
    if (!ReadElementIdBits(r, f.ped) || !ReadElementIdBits(r, f.veh) ||
        !ReadSmall(r, 4, f.seat) || !ReadSmall(r, 4, f.action)) return false;
    switch (f.action) {
        case 0: case 2: case 7:
            return ReadSmall(r, 3, f.door);
        case 3:
            if (!ReadSmall(r, 2, f.door)) f.door = 0xFF;
            return true;
        case 9:
            return ReadSmall(r, 8, f.reason);
        default:
            return true;
    }
}

// Body after the game id byte, as sent by the client.
inline bool DecodeVehInOutClient(const uint8_t* body, size_t length, size_t bits, VehInOutFields& f) {
    WorldRPG::Net::BitStreamReader r(body, length, bits);
    if (!ReadElementIdBits(r, f.ped) || !ReadElementIdBits(r, f.veh) || !ReadSmall(r, 4, f.action)) return false;
    if (f.action == 0) {
        uint8_t water = 0;
        return ReadSmall(r, 4, f.seat) && ReadSmall(r, 1, water) && ReadSmall(r, 3, f.door);
    }
    return true;
}

} // namespace WorldRPG::Headless
