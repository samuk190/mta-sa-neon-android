#include "ResourcePackets.h"

#include "../net/raknet/RakNetWrapper.h"

namespace WorldRPG::Script::ResourcePackets {

void BuildPlayerResourceStart(WorldRPG::Net::BitStreamWriter& out, uint16_t netId, uint32_t startCounter) {
    out.Write(kPlayerResourceStartId);
    out.Write(netId);
    out.Write(startCounter);
}

bool ParseResourceStop(const uint8_t* body, size_t length, size_t bits, uint16_t& netId) {
    WorldRPG::Net::BitStreamReader reader(body, length, bits);
    netId = 0;
    return reader.Read(netId);
}

} // namespace WorldRPG::Script::ResourcePackets
