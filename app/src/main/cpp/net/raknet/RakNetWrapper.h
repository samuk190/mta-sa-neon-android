#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <memory>
#include <functional>
#include <atomic>
#include <array>

// Forward declarations for RakNet types to avoid direct header dependency in wrapper interface
namespace RakNet {
    class RakPeerInterface;
    class BitStream;
    struct Packet;
    struct SystemAddress;
}

namespace WorldRPG::Net {

// Neon internal RID space. Native MTA transport emits these IDs directly.
// Application packets use wire ID (99 + ePacketID); the wrapper converts
// them to a local [gameId][body] callback representation, with exact bits.
enum class PacketID : uint16_t {
    // Internal RakNet packets
    INTERNAL_PING = 0,
    PING = 1,
    PING_OPEN_CONNECTIONS = 2,
    CONNECTED_PONG = 3,
    CONNECTION_REQUEST = 4,
    SECURED_CONNECTION_RESPONSE = 5,
    SECURED_CONNECTION_CONFIRMATION = 6,
    RPC_MAPPING = 7,
    DETECT_LOST_CONNECTIONS = 8,
    OPEN_CONNECTION_REQUEST = 9,
    OPEN_CONNECTION_REPLY = 10,
    RPC = 11,
    RPC_REPLY = 12,
    OUT_OF_BAND_INTERNAL = 13,

    // User-facing connection status packets
    CONNECTION_REQUEST_ACCEPTED = 14,
    CONNECTION_ATTEMPT_FAILED = 15,
    ALREADY_CONNECTED = 16,
    NEW_INCOMING_CONNECTION = 17,
    NO_FREE_INCOMING_CONNECTIONS = 18,
    DISCONNECTION_NOTIFICATION = 19,
    CONNECTION_LOST = 20,
    RSA_PUBLIC_KEY_MISMATCH = 21,
    CONNECTION_BANNED = 22,
    INVALID_PASSWORD = 23,
    MODIFIED_PACKET = 24,
    TIMESTAMP = 25,
    PONG = 26,
    ADVERTISE_SYSTEM = 27,
    REMOTE_DISCONNECTION_NOTIFICATION = 28,
    REMOTE_CONNECTION_LOST = 29,
    REMOTE_NEW_INCOMING_CONNECTION = 30,

    // RID_INCOMPATIBLE_PROTOCOL_VERSION = 95 in the Neon RID space
    INCOMPATIBLE_PROTOCOL_VERSION = 95,
    FCM2_ELAPSED_RUNTIME = 96,
    FCM2_NEW_HOST = 97,
    UDP_PROXY_GENERAL = 98,

    // RID_USER_PACKET_ENUM = 99. MTA game packets carry this ID first,
    // followed by the game packet type byte (ePacketTypes).
    USER_PACKET_ENUM = 99
};

enum class PacketPriority : uint8_t {
    HIGH = 0,
    MEDIUM = 1,
    LOW = 2
};

enum class PacketReliability : uint8_t {
    UNRELIABLE = 0,
    UNRELIABLE_SEQUENCED = 1,
    RELIABLE = 2,
    RELIABLE_ORDERED = 3,
    RELIABLE_SEQUENCED = 4
};

enum class PacketOrdering : uint8_t {
    DEFAULT = 0,
    CHAT = 1,
    DATA_TRANSFER = 2,
    VOICE = 3,
    NATIVE_TASK_PRESENTATION = 4
};

struct NetworkStatistics {
    uint64_t bytesReceived;
    uint64_t bytesSent;
    uint32_t packetsReceived;
    uint32_t packetsSent;
    float packetlossTotal;
    float packetlossLastSecond;
    uint32_t messagesInSendBuffer;
    uint32_t messagesInResendBuffer;
    bool isLimitedByCongestionControl;
    bool isLimitedByOutgoingBandwidthLimit;
};

using PacketHandlerCallback = std::function<bool(uint8_t packetId, const uint8_t* data, size_t length, size_t bitLength)>;

class BitStreamWriter {
public:
    BitStreamWriter();
    ~BitStreamWriter();

    void Write(uint8_t value);
    void Write(uint16_t value);
    void Write(uint32_t value);
    void Write(int32_t value);
    void Write(float value);
    void Write(const char* str);
    void WriteBytes(const uint8_t* data, size_t length);
    void WriteBits(const uint8_t* data, size_t bitCount);

    const uint8_t* GetData() const;
    size_t GetByteLength() const;
    size_t GetBitLength() const;
    void Reset();

private:
    struct Impl;
    std::unique_ptr<Impl> pImpl_;
};

class BitStreamReader {
public:
    explicit BitStreamReader(const uint8_t* data, size_t byteLength, size_t bitLength = 0);
    ~BitStreamReader();

    bool Read(uint8_t& value);
    bool Read(uint16_t& value);
    bool Read(uint32_t& value);
    bool Read(int32_t& value);
    bool Read(float& value);
    bool ReadString(std::string& outStr, size_t maxLen = 512);
    bool ReadBytes(uint8_t* buffer, size_t length);
    bool ReadBits(uint8_t* buffer, size_t bitCount);

    size_t GetReadOffsetBits() const;
    size_t GetRemainingBits() const;
    void Reset();

private:
    struct Impl;
    std::unique_ptr<Impl> pImpl_;
};

class RakNetWrapper {
public:
    RakNetWrapper();
    ~RakNetWrapper();

    // Non-copyable, non-movable
    RakNetWrapper(const RakNetWrapper&) = delete;
    RakNetWrapper& operator=(const RakNetWrapper&) = delete;
    RakNetWrapper(RakNetWrapper&&) = delete;
    RakNetWrapper& operator=(RakNetWrapper&&) = delete;

    bool StartNetwork(const char* serverHost, uint16_t serverPort, bool packetTag = false);
    void StopNetwork();

    bool IsReady() const;
    bool IsConnected() const;

    void DoPulse();
    void Shutdown();

    void SetPacketHandler(PacketHandlerCallback handler);

    /// Callback for connection state changes (connected, disconnected, error).
    /// Called from DoPulse when the native MTA transport reports an event.
    using ConnectionStateCallback = std::function<void(bool connected, const char* message)>;
    void SetConnectionStateCallback(ConnectionStateCallback callback);

    bool SendPacket(uint8_t packetId, const BitStreamWriter& stream,
                    PacketPriority priority, PacketReliability reliability,
                    PacketOrdering ordering = PacketOrdering::DEFAULT);

    void SetClientPort(uint16_t port);
    std::string GetConnectedServer(bool includePort = false) const;

    bool GetNetworkStatistics(NetworkStatistics& stats) const;
    int GetPing() const;
    uint32_t GetTime() const;
    std::string GetLocalIP() const;

    void GetSerial(char* buffer, size_t maxLength) const;

    uint8_t GetConnectionError() const;
    void SetConnectionError(uint8_t error);

    void Reset();

    void SetTimeoutTime(uint32_t timeoutMs);

    // MTA handshake helpers
    bool SendOpenPacket();
    bool SendJoinRequest(const std::string& nickname, const std::string& serial);
    bool VerifySerialSignature(const uint8_t* data, size_t length);

    /// Returns true once the full MTA handshake has completed (RakNet connected + join accepted).
    bool IsHandshakeComplete() const;

    /// True once RakNet CONNECTION_REQUEST_ACCEPTED was received. Transport
    /// layer only — the MTA application join is tracked by NeonJoinClient.
    bool IsRakNetConnected() const;

    /// Called only after parsing the server's joined-game response.
    void MarkSessionAccepted();

private:
    struct Impl;
    std::unique_ptr<Impl> pImpl_;
};

// ASCII normalization utility for strings sent to the game client
std::string NormalizeAscii(const std::string& input);

} // namespace WorldRPG::Net
