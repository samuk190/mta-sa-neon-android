// Link-time tap on WorldRPG::Net::RakNetWrapper::SendPacket. CMakeLists.txt links with
//   -Wl,--wrap=_ZN8WorldRPG3Net13RakNetWrapper10SendPacketE...
// so every call made from another translation unit (NeonJoinProtocol.cpp: all of the
// PLAYER_PURESYNC / VEHICLE_INOUT / BULLETSYNC senders) comes here first. The real
// function is then called unchanged. Only game packets (RID 99) with ids 58, 33 and 98
// are logged; every game id is counted.

#include "send_tap.h"
#include "packet_log.h"
#include "game_script.h"

#include "net/raknet/RakNetWrapper.h"

#include <cstdio>
#include <map>

namespace {
std::map<uint8_t, uint64_t> g_sent;
}

extern "C" bool __real__ZN8WorldRPG3Net13RakNetWrapper10SendPacketEhRKNS0_15BitStreamWriterENS0_14PacketPriorityENS0_17PacketReliabilityENS0_14PacketOrderingE(
    WorldRPG::Net::RakNetWrapper* self, uint8_t packetId, const WorldRPG::Net::BitStreamWriter& stream,
    WorldRPG::Net::PacketPriority priority, WorldRPG::Net::PacketReliability reliability,
    WorldRPG::Net::PacketOrdering ordering);

extern "C" bool __wrap__ZN8WorldRPG3Net13RakNetWrapper10SendPacketEhRKNS0_15BitStreamWriterENS0_14PacketPriorityENS0_17PacketReliabilityENS0_14PacketOrderingE(
    WorldRPG::Net::RakNetWrapper* self, uint8_t packetId, const WorldRPG::Net::BitStreamWriter& stream,
    WorldRPG::Net::PacketPriority priority, WorldRPG::Net::PacketReliability reliability,
    WorldRPG::Net::PacketOrdering ordering) {
    if (packetId == static_cast<uint8_t>(WorldRPG::Net::PacketID::USER_PACKET_ENUM) &&
        stream.GetByteLength() >= 1) {
        const uint8_t* data = stream.GetData();
        const size_t length = stream.GetByteLength();
        const uint8_t gameId = data[0];
        ++g_sent[gameId];
        const size_t bodyBits = stream.GetBitLength() >= 8 ? stream.GetBitLength() - 8 : (length - 1) * 8;
        const double t = WorldRPG::Headless::ElapsedSeconds();
        if (gameId == 58) {
            WorldRPG::Headless::VehInOutFields f;
            if (WorldRPG::Headless::DecodeVehInOutClient(data + 1, length - 1, bodyBits, f)) {
                std::printf("[%7.3fs] SEND 58 vehinout ped=%u veh=%u action=%u (%s) seat=%u door=%u bytes=%zu\n",
                            t, f.ped, f.veh, static_cast<unsigned>(f.action),
                            WorldRPG::Headless::VehInOutName(f.action), static_cast<unsigned>(f.seat),
                            static_cast<unsigned>(f.door), length - 1);
            } else {
                std::printf("[%7.3fs] SEND 58 vehinout undecodable bytes=%zu\n", t, length - 1);
            }
        } else if (gameId == 33) {
            // Not decoded here: the port's ReadOccupiedSync is the server-relay layout (player id,
            // time context, latency, keys), not the client-written one. game_script.cpp logs the car
            // state the sender reads each second instead.
            std::printf("[%7.3fs] SEND 33 puresync(driver) bytes=%zu\n", t, length - 1);
        } else if (gameId == 98) {
            WorldRPG::Net::BitStreamReader r(data + 1, length - 1, bodyBits);
            uint8_t weapon = 0, order = 0;
            float s[3] = {}, e[3] = {};
            bool ok = r.Read(weapon);
            for (float& v : s) ok = ok && r.Read(v);
            for (float& v : e) ok = ok && r.Read(v);
            ok = ok && r.Read(order);
            if (ok) {
                std::printf("[%7.3fs] SEND 98 bullet weapon=%u order=%u from (%.2f,%.2f,%.2f) to (%.2f,%.2f,%.2f)\n",
                            t, static_cast<unsigned>(weapon), static_cast<unsigned>(order), s[0], s[1], s[2],
                            e[0], e[1], e[2]);
            } else {
                std::printf("[%7.3fs] SEND 98 bullet undecodable bytes=%zu\n", t, length - 1);
            }
        }
    }
    return __real__ZN8WorldRPG3Net13RakNetWrapper10SendPacketEhRKNS0_15BitStreamWriterENS0_14PacketPriorityENS0_17PacketReliabilityENS0_14PacketOrderingE(
        self, packetId, stream, priority, reliability, ordering);
}

namespace WorldRPG::Headless {

uint64_t SentCount(uint8_t gameId) {
    auto it = g_sent.find(gameId);
    return it == g_sent.end() ? 0 : it->second;
}

void PrintSentSummary() {
    std::printf("sent (game packets):\n");
    for (const auto& [id, count] : g_sent) {
        std::printf("  id=%-3u count=%llu\n", static_cast<unsigned>(id), static_cast<unsigned long long>(count));
    }
}

} // namespace WorldRPG::Headless
