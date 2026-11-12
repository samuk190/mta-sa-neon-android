#pragma once

#include "ElementTypes.h"
#include "SyncTypes.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <shared_mutex>

#ifdef __ANDROID__
#include <android/log.h>
#define ELEM_LOG_TAG "WorldRPGNeon"
#define ELEM_LOGI(...) __android_log_print(ANDROID_LOG_INFO, ELEM_LOG_TAG, __VA_ARGS__)
#define ELEM_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, ELEM_LOG_TAG, __VA_ARGS__)
#else
#include <cstdio>
#define ELEM_LOGI(...) do { printf(__VA_ARGS__); printf("\n"); } while(0)
#define ELEM_LOGE(...) do { fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } while(0)
#endif

namespace WorldRPG::Sync {

/// Callback signatures for element lifecycle events
using ElementCreatedCallback = std::function<void(ElementType type, ElementID id)>;
using ElementDestroyedCallback = std::function<void(ElementType type, ElementID id)>;

/// Fixed-size pool allocator for each element type.
/// No dynamic allocation during gameplay — all slots pre-allocated at init.
class ElementManager {
public:
    ElementManager();
    ~ElementManager();

    ElementManager(const ElementManager&) = delete;
    ElementManager& operator=(const ElementManager&) = delete;

    /// Create or retrieve an element slot by type and ID
    bool createElement(ElementType type, ElementID id);

    /// Mark an element as destroyed (slot freed on next cleanup pass)
    void destroyElement(ElementType type, ElementID id);

    /// Check if an element exists and is active
    bool hasElement(ElementType type, ElementID id) const;

    /// Get the current state of an element
    ElementState getElementState(ElementType type, ElementID id) const;

    /// Update player sync data (thread-safe write)
    bool updatePlayerSync(const PlayerSyncData& data);

    /// Update vehicle sync data (thread-safe write)
    bool updateVehicleSync(const VehicleSyncData& data);

    /// Update object sync data (thread-safe write)
    bool updateObjectSync(const ObjectSyncData& data);

    /// Update ped sync data (thread-safe write)
    bool updatePedSync(const PedSyncData& data);

    /// Read current player sync data (thread-safe read)
    bool getPlayerSync(ElementID id, PlayerSyncData& out) const;

    /// Read current vehicle sync data (thread-safe read)
    bool getVehicleSync(ElementID id, VehicleSyncData& out) const;

    /// Iterate all active elements of a given type
    void iterateElements(ElementType type, std::function<void(ElementID id, ElementState state)> callback) const;

    /// Set lifecycle event callbacks
    void setCallbacks(ElementCreatedCallback onCreate, ElementDestroyedCallback onDestroy);

    /// Get count of active elements per type
    size_t activeCount(ElementType type) const;

private:
    struct Impl;
    std::unique_ptr<Impl> pImpl_;
};

} // namespace WorldRPG::Sync