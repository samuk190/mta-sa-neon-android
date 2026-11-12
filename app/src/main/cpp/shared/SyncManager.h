#pragma once

#include "ElementManager.h"
#include "../net/raknet/RakNetWrapper.h"

#include <memory>
#include <functional>

#ifdef __ANDROID__
#include <android/log.h>
#define SYNC_LOG_TAG "WorldRPGNeon"
#define SYNC_LOGI(...) __android_log_print(ANDROID_LOG_INFO, SYNC_LOG_TAG, __VA_ARGS__)
#define SYNC_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, SYNC_LOG_TAG, __VA_ARGS__)
#else
#include <cstdio>
#define SYNC_LOGI(...) do { printf(__VA_ARGS__); printf("\n"); } while(0)
#define SYNC_LOGE(...) do { fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } while(0)
#endif

namespace WorldRPG::Sync {

/// Callback for chat/server messages received from server
using ChatMessageCallback = std::function<void(const char* message, uint8_t color)>;
using ServerMessageCallback = std::function<void(const char* message)>;

/// Central synchronization coordinator.
/// Processes incoming network packets and updates ElementManager.
/// Handles interpolation, dead reckoning, and entity lifecycle events.
class SyncManager {
public:
    SyncManager();
    ~SyncManager();

    SyncManager(const SyncManager&) = delete;
    SyncManager& operator=(const SyncManager&) = delete;

    /// Initialize with references to network and element systems
    bool initialize(ElementManager& elements);

    /// Register packet handlers with the network layer
    void registerPacketHandlers(Net::RakNetWrapper& network);

    /// Per-frame update: apply interpolation, predict positions, detect timeouts
    /// @param deltaTimeMs Time since last frame in milliseconds
    void update(float deltaTimeMs);

    /// Set chat message callback
    void setChatCallback(ChatMessageCallback callback);

    /// Set server message callback
    void setServerMessageCallback(ServerMessageCallback callback);

    /// Get the element manager reference
    ElementManager& getElementManager();

private:
    struct Impl;
    std::unique_ptr<Impl> pImpl_;
};

} // namespace WorldRPG::Sync