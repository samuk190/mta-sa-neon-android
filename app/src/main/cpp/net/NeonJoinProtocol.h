#pragma once

#include <cstdint>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>

#include "raknet/RakNetWrapper.h"

namespace WorldRPG::Net {

// ---------------------------------------------------------------------------
// Neon (MTA 1.7 fork) application-layer join protocol.
//
// Wire application header: [uint8(99 + ePacketID)][body...].
// The wrapper callback uses [ePacketID][body...] as a local representation.
//
// Evidence (mta_neon_source checkout c3fe13e6):
//   - ePacketID values:            Shared/sdk/net/Packets.h
//   - version constants:           Shared/sdk/version.h (netcode/MTA),
//                                  Shared/sdk/net/bitstream.h (bitstream)
//   - PLAYER_JOIN trigger:         Server/mods/deathmatch/logic/CGame.cpp
//                                  (MessageHandler + Packet_PlayerJoin)
//   - MOD_NAME layout:             CGame.cpp Packet_PlayerJoin (write) and
//                                  Client/core/CConnectManager.cpp (read)
//   - PLAYER_JOINDATA layout:      Client/mods/deathmatch/logic/CClientGame.cpp
//                                  StartGame (write) and Server
//                                  CPlayerJoinDataPacket::Read (read)
//   - SERVER_JOIN_COMPLETE layout: Server packets/CPlayerConnectCompletePacket.cpp
//   - SERVER_JOINEDGAME layout:    Server packets/CPlayerJoinCompletePacket.cpp
//   - RPC envelope:                Client logic/CNetAPI.cpp RPC() and Server
//                                  logic/CRPCFunctions.cpp ProcessPacket
// ---------------------------------------------------------------------------

// Values computed entry-by-entry from the ePacketID enum order in
// Shared/sdk/net/Packets.h (cross-checked against captured packets:
// RESOURCE_START=82, RESOURCE_CLIENT_SCRIPTS=95, ENTITY_ADD=50).
enum class NeonPacketID : uint8_t {
    SERVER_JOIN = 0,
    SERVER_JOIN_DATA = 1,
    SERVER_JOIN_COMPLETE = 2,
    PLAYER_JOIN = 3,
    PLAYER_JOINDATA = 4,
    PLAYER_QUIT = 5,
    PLAYER_TIMEOUT = 6,
    MOD_NAME = 7,
    PACKET_PROGRESS = 8,
    SERVER_JOINEDGAME = 22,
    SERVER_DISCONNECTED = 23,
    RPC = 24,
    PLAYER_LIST = 25,
    ENTITY_ADD = 50,
    ENTITY_REMOVE = 51,
    RESOURCE_START = 82,
    RESOURCE_STOP = 83,
    RESOURCE_CLIENT_SCRIPTS = 95,
};
static_assert(static_cast<uint8_t>(NeonPacketID::SERVER_JOINEDGAME) == 22, "ePacketID drift");
static_assert(static_cast<uint8_t>(NeonPacketID::RPC) == 24, "ePacketID drift");

enum class NeonServerRPC : uint8_t {
    PLAYER_INGAME_NOTICE = 0,
    INITIAL_DATA_STREAM = 1,
};

// version.h: _NETCODE_VERSION 0x1E6 + (_NETCODE_VERSION_BRANCH_ID 0x4 << 12)
constexpr uint16_t NEON_NETCODE_VERSION = 0x41E6;
// version.h: MTA_DM_VERSION = (1 << 8) | (7 << 4) | 0
constexpr uint16_t NEON_MTA_VERSION = 0x170;
// bitstream.h: eBitStreamVersion::Latest (static_assert == 0x3D)
constexpr uint16_t NEON_BITSTREAM_VERSION = 0x3D;
// Client/sdk/game/CGame.h eGameVersion::VERSION_20. Provisional for the
// mobile engine until a reference capture proves the value the mobile
// clients announce; the server stores it without gating the join on it.
constexpr uint8_t NEON_GAME_VERSION = 20;

constexpr size_t NEON_MAX_PLAYER_NICK_LENGTH = 22;  // Shared/sdk/Common.h
constexpr size_t NEON_MAX_SERIAL_LENGTH = 32;       // Shared/sdk/Common.h
constexpr uint32_t NEON_ELEMENT_ID_BITS = 17;       // 131072 element slots

enum class NeonAuthMode : uint8_t {
    Disabled = 0,
    Optional = 1,
    Required = 2,
};

enum class JoinState : uint8_t {
    Idle = 0,
    WaitingRakNet,        // transport connecting
    WaitingModName,       // NIC causes the server-side PLAYER_JOIN event
    JoinDataSent,         // waiting SERVER_JOIN_COMPLETE
    ServerJoinComplete,   // waiting nothing; sends RPC PLAYER_INGAME_NOTICE
    InGameNoticeSent,     // waiting SERVER_JOINEDGAME
    JoinedGame,           // SERVER_JOINEDGAME parsed; sends RPC INITIAL_DATA_STREAM
    Complete,             // full join finished
    Failed,
};

const char* JoinStateName(JoinState state);

struct NeonJoinInfo {
    std::string modName;
    uint16_t serverBitStreamVersion = 0;
    NeonAuthMode authMode = NeonAuthMode::Disabled;
    std::string neonServerId;
    std::string serverVersionText;
    std::string serverVersionSortable;
    uint32_t localPlayerId = 0xFFFFFFFF;
    uint32_t rootElementId = 0xFFFFFFFF;
    bool voiceEnabled = false;
    uint8_t voiceSampleRate = 0;
    uint8_t voiceQuality = 0;
    uint32_t voiceBitrate = 0;
    int32_t enableClientChecks = 0;
    bool fakeLagCommand = false;
    int32_t httpMaxConnections = 0;
    uint8_t httpDownloadType = 0;
    uint16_t httpPort = 0;
    std::string httpUrl;
    std::string serverName;
    uint8_t disconnectType = 0xFF;  // raw 5-bit type from SERVER_DISCONNECTED
    std::string disconnectReason;
    std::string lastError;
};

/// Drives the MTA Neon join handshake on top of RakNetWrapper.
/// Owns no transport; the caller feeds received application packets and
/// pulses once per frame after RakNetWrapper::DoPulse().
class NeonJoinClient {
public:
    using StateCallback = std::function<void(JoinState state, const char* detail)>;

    NeonJoinClient();
    ~NeonJoinClient();

    NeonJoinClient(const NeonJoinClient&) = delete;
    NeonJoinClient& operator=(const NeonJoinClient&) = delete;

    /// Start the join sequence. Requires an empty password for now; a
    /// password-protected server needs an MD5 implementation (tracked as a
    /// follow-up) and Begin() refuses explicitly instead of sending zeros.
    bool Begin(RakNetWrapper& net, const std::string& nickname, const std::string& password);

    /// Frame tick: fires pending sends once the transport is up and enforces
    /// per-state timeouts.
    void Pulse();

    /// End the attempt immediately when the transport fails. Duplicate/late
    /// failures preserve the first cause; Begin() starts a fresh attempt.
    void OnTransportFailure(const std::string& reason);

    /// Feed one application packet body: gameId is the ePacketID byte that
    /// follows RID_USER_PACKET_ENUM, body/length are the remaining bytes.
    /// Returns true when the join flow consumed the packet.
    bool ProcessGamePacket(uint8_t gameId, const uint8_t* body, size_t length, size_t bitLength = 0);

    void SetStateCallback(StateCallback callback);

    JoinState State() const;
    bool IsActive() const;
    const NeonJoinInfo& Info() const;

    void Reset();

    /// Desktop chatbox: a normal line is sent as "say <text>". A line that
    /// starts with / is a command with the slash removed. PACKET_ID_COMMAND
    /// is the raw string and nothing else.
    bool SendChat(const std::string& command);

private:
    struct Impl;
    std::unique_ptr<Impl> pImpl_;
};

/// Sends one game packet (the byte already in the stream is the ePacketID)
/// on the joined connection. Lua events use this from the net thread.
bool SendGamePacket(BitStreamWriter& body, bool reliable);

} // namespace WorldRPG::Net
