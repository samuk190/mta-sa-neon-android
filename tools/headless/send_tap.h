// Outgoing packet tap. send_tap.cpp wraps RakNetWrapper::SendPacket at link time
// (-Wl,--wrap), so the NeonJoinProtocol send sites are logged without editing app sources.
#pragma once

#include <cstdint>

namespace WorldRPG::Headless {

// Number of application packets sent with this game id (ePacketID) so far.
uint64_t SentCount(uint8_t gameId);
void PrintSentSummary();

} // namespace WorldRPG::Headless
