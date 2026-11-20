#pragma once

#include <cstdint>
#include <string>

// Error boxes of the desktop client, in its words and codes, for the phone's popup.
//   Server refusal / kick / ban: CPacketHandler::Packet_ServerDisconnected (CPacketHandler.cpp:545-750),
//     title "Disconnected [CDxx]".
//   Connect step: CConnectManager::DoPulse (CConnectManager.cpp:468-560), "Error [CCxx]".
//   After the join: CClientGame::DoPulse (CClientGame.cpp:1426-1531), "Error [CDxx]".
//   Bad packet: RaiseProtocolError (Shared/mods/deathmatch/logic/Utils.cpp:136-146), "Connection error [CD63]".
// _E(code) is " [code]" (Shared/sdk/SharedUtil.h:38).
namespace WorldRPG::Net::DisconnectMessages {

struct Box {
    std::string title;
    std::string text;
};

// Separator between title and text in the string handed to Java (onConnectionStateChange).
constexpr char kSeparator = '\x1f';
std::string Pack(const Box& box);

// ePlayerDisconnectType (CPacketHandler.h:23-47).
enum Type : uint8_t {
    NO_REASON = 0, INVALID_PASSWORD, INVALID_NICKNAME, BANNED_SERIAL, BANNED_IP, BANNED_ACCOUNT,
    VERSION_MISMATCH, JOIN_FLOOD, INCORRECT_PASSWORD, DIFFERENT_BRANCH, BAD_VERSION, SERVER_NEWER,
    SERVER_OLDER, NICK_CLASH, ELEMENT_FAILURE, GENERAL_REFUSED, SERIAL_VERIFICATION, CONNECTION_DESYNC,
    BAN, KICK, CUSTOM, SHUTDOWN, SERIAL_DUPLICATE
};

// True when the packet carries a duration string after the type (BANNED_SERIAL, BANNED_IP, BAN).
bool HasDuration(uint8_t type);
// Packet_ServerDisconnected text for a type, the server message and the duration string.
Box ServerDisconnected(uint8_t type, const std::string& message, const std::string& duration);

// Transport causes, as RakNetWrapper reports them.
enum class Transport { AttemptFailed, Disconnected, Lost, Banned, InvalidPassword, Other };
Transport ClassifyTransport(const std::string& reason);
// Before the join finished (CConnectManager, CCxx) or after it (CClientGame, CDxx).
Box TransportFailure(Transport cause, bool joined);

Box ConnectTimedOut();        // CC23
Box JoinTimedOut();           // CD08
Box ConnectionLost();         // CD09, no transport cause known
Box InvalidNick();            // CC20
Box ProtocolError(unsigned code);  // CD63
Box Plain(const std::string& title, const std::string& text);

} // namespace WorldRPG::Net::DisconnectMessages
