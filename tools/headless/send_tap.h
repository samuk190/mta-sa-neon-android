// Outgoing packet tap. send_tap.cpp wraps RakNetWrapper::SendPacket at link time
// (-Wl,--wrap), so the NeonJoinProtocol send sites are logged without editing app sources.
#pragma once

#include <cstdint>

namespace WorldRPG::Net { class BitStreamWriter; }
namespace WorldRPG::Headless {

// Tests can exercise the actual sender with an accepting or rejecting local transport.
using TestTransport = bool (*)(const Net::BitStreamWriter&);
void SetTestTransport(TestTransport send);

// Number of application packets sent with this game id (ePacketID) so far.
uint64_t SentCount(uint8_t gameId);
void PrintSentSummary();

} // namespace WorldRPG::Headless
