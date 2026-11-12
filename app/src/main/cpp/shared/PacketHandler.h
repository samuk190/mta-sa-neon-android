#pragma once

#include "SyncManager.h"
#include "../net/raknet/RakNetWrapper.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>

#ifdef __ANDROID__
#include <android/log.h>
#define PKT_LOG_TAG "WorldRPGNeon"
#define PKT_LOGI(...) __android_log_print(ANDROID_LOG_INFO, PKT_LOG_TAG, __VA_ARGS__)
#define PKT_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, PKT_LOG_TAG, __VA_ARGS__)
#else
#include <cstdio>
#define PKT_LOGI(...) do { printf(__VA_ARGS__); printf("\n"); } while(0)
#define PKT_LOGE(...) do { fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } while(0)
#endif

namespace WorldRPG::Sync {

/// Game packet type bytes carried inside the RID_USER_PACKET_ENUM (99)
/// envelope: wire format is [99][gameId][body...].
/// NOTE: this local numbering is provisional. P2 replaces it with the real
/// ePacketTypes from mta_neon_source/Shared/sdk/net/Packets.h.
enum class MTAPacketID : uint8_t {
    ELEMENT_CREATE      = 0,
    ELEMENT_DESTROY     = 1,
    PLAYER_SYNC         = 2,
    VEHICLE_SYNC        = 3,
    OBJECT_SYNC         = 4,
    PED_SYNC            = 5,
    CHAT_MESSAGE        = 6,
    SERVER_MESSAGE      = 7,
    SPAWN_PLAYER        = 8,
    PICKUP_SYNC         = 9,
    BLIP_SYNC           = 10,
    RADAR_AREA_SYNC     = 11,
};

constexpr uint8_t USER_PACKET_ENUM_ID = 99; // RID_USER_PACKET_ENUM

using PacketHandlerFn = std::function<bool(const uint8_t* data, size_t length)>;

/// Maps MTA user packet IDs to handler functions.
/// Extensible registration pattern for adding new packet types.
class PacketHandler {
public:
    PacketHandler();
    ~PacketHandler();

    PacketHandler(const PacketHandler&) = delete;
    PacketHandler& operator=(const PacketHandler&) = delete;

    /// Initialize with references to sync and network systems
    bool initialize(SyncManager& syncManager);

    /// Register all built-in MTA packet handlers with the network layer
    void registerAll(Net::RakNetWrapper& network);

    /// Register a custom handler for a specific packet ID
    void registerHandler(MTAPacketID id, PacketHandlerFn handler);

    /// Process an incoming packet (called from network callback).
    /// bitLength counts every bit of data, including the leading game-id byte
    /// when packetId is USER_PACKET_ENUM_ID.
    bool handlePacket(uint8_t packetId, const uint8_t* data, size_t length, size_t bitLength = 0);

private:
    struct Impl;
    std::unique_ptr<Impl> pImpl_;
};

} // namespace WorldRPG::Sync