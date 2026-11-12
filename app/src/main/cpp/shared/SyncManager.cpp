#include "SyncManager.h"
#include "PacketHandler.h"
#include "PerformanceProfiler.h"

#include <cmath>
#include <chrono>

namespace WorldRPG::Sync {

struct SyncManager::Impl {
    ElementManager* elements = nullptr;
    ChatMessageCallback chatCallback;
    ServerMessageCallback serverMsgCallback;

    // Interpolation state per entity (simplified: global lerp factor)
    static constexpr float kInterpolationTimeMs = 100.0f;
    float interpolationAlpha = 1.0f;

    // Timeout detection
    static constexpr uint32_t kEntityTimeoutMs = 5000;

    void applyInterpolation(float deltaTimeMs) {
        if (kInterpolationTimeMs <= 0.0f) {
            interpolationAlpha = 1.0f;
            return;
        }
        interpolationAlpha = std::min(1.0f, interpolationAlpha + deltaTimeMs / kInterpolationTimeMs);
    }

    void detectTimeouts(uint32_t currentTimeMs) {
        auto checkPool = [&](ElementType type) {
            elements->iterateElements(type, [&](ElementID id, ElementState state) {
                if (state != ElementState::SYNCING) return;
                // TODO: Store last-update timestamp per element and compare
                // For now, timeout detection is a stub
                (void)id;
                (void)currentTimeMs;
            });
        };
        checkPool(ElementType::PLAYER);
        checkPool(ElementType::VEHICLE);
        checkPool(ElementType::OBJECT);
        checkPool(ElementType::PED);
    }
};

SyncManager::SyncManager() : pImpl_(std::make_unique<Impl>()) {}
SyncManager::~SyncManager() = default;

bool SyncManager::initialize(ElementManager& elements) {
    pImpl_->elements = &elements;
    SYNC_LOGI("SyncManager: initialized");
    return true;
}

void SyncManager::registerPacketHandlers(Net::RakNetWrapper& network) {
    // Register handlers for MTA user packets via PacketHandler module
    // The actual registration happens when PacketHandler::registerAll() is called
    // with references to this SyncManager and the network layer.
    SYNC_LOGI("SyncManager: packet handlers registered");
    (void)network;
}

void SyncManager::update(float deltaTimeMs) {
    PROFILE_SCOPE("SyncManager::update");
    if (!pImpl_->elements) return;

    pImpl_->applyInterpolation(deltaTimeMs);

    auto now = static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    pImpl_->detectTimeouts(now);
}

void SyncManager::setChatCallback(ChatMessageCallback callback) {
    pImpl_->chatCallback = std::move(callback);
}

void SyncManager::setServerMessageCallback(ServerMessageCallback callback) {
    pImpl_->serverMsgCallback = std::move(callback);
}

ElementManager& SyncManager::getElementManager() {
    return *pImpl_->elements;
}

} // namespace WorldRPG::Sync