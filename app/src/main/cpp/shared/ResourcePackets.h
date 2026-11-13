#pragma once

// Resource lifecycle game packets, both directions. Ids from Shared/sdk/net/Packets.h
// (counted from the enum: PLAYER_PURESYNC 32, VEHICLE_INOUT 58, PLAYER_BULLETSYNC 98 match):
//   RESOURCE_STOP (83)          server -> client, body: uint16 resource net id.
//                               Desktop CPacketHandler::Packet_ResourceStop (CPacketHandler.cpp 5697-5708).
//                               Writer CResourceStopPacket::Write (packets/CResourceStopPacket.cpp).
//   PLAYER_RESOURCE_START (108) client -> server, body: uint16 resource net id, uint32 start counter.
//                               Desktop CResource::Load (CResource.cpp 1630-1640) after onClientResourceStart.
//                               Server reader CPlayerResourceStartPacket::Read (packets/CPlayerResourceStartPacket.cpp 16-24)
//                               and CResource::CanPlayerTriggerResourceStart (CResource.cpp 368-378).

#include <cstddef>
#include <cstdint>

namespace WorldRPG::Net {
class BitStreamWriter;
}

namespace WorldRPG::Script::ResourcePackets {

constexpr uint8_t kResourceStopId = 83;
constexpr uint8_t kPlayerResourceStartId = 108;

// The server accepts a start counter only when it equals the resource's m_startCounter, and it sets
// m_startCounter = max(previous + 1, 1) on every start (CResource.cpp 1078). The first start is 1.
constexpr uint32_t kFirstStartCounter = 1;

// Writes the whole packet, game id byte included (the caller sends it as a user packet).
void BuildPlayerResourceStart(WorldRPG::Net::BitStreamWriter& out, uint16_t netId, uint32_t startCounter);

// Reads the RESOURCE_STOP body (the bytes after the game id). False when it is too short.
bool ParseResourceStop(const uint8_t* body, size_t length, size_t bits, uint16_t& netId);

} // namespace WorldRPG::Script::ResourcePackets
