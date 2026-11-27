#pragma once

#include "SyncManager.h"
#include "../net/raknet/RakNetWrapper.h"

#include <cstdint>
#include <memory>

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

constexpr uint8_t USER_PACKET_ENUM_ID = 99; // RID_USER_PACKET_ENUM

/// Dispatches incoming MTA game packets (real ePacketID values from Shared/sdk/net/Packets.h).
class PacketHandler {
public:
    PacketHandler();
    ~PacketHandler();

    PacketHandler(const PacketHandler&) = delete;
    PacketHandler& operator=(const PacketHandler&) = delete;

    /// Initialize with references to sync and network systems
    bool initialize(SyncManager& syncManager);

    /// Attach this handler to the network layer
    void registerAll(Net::RakNetWrapper& network);

    /// Process an incoming packet (called from network callback).
    /// bitLength counts every bit of data, including the leading game-id byte
    /// when packetId is USER_PACKET_ENUM_ID.
    bool handlePacket(uint8_t packetId, const uint8_t* data, size_t length, size_t bitLength = 0);

private:
    bool handleGamePacket(uint8_t gameId, const uint8_t* body, size_t bodyLength, size_t bodyBits);

    struct Impl;
    std::unique_ptr<Impl> pImpl_;
};

} // namespace WorldRPG::Sync