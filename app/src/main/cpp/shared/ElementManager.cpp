#include "ElementManager.h"

#include <algorithm>
#include <cstring>

namespace WorldRPG::Sync {

// ---------------------------------------------------------------------------
// Per-type fixed-size pool entry
// ---------------------------------------------------------------------------

template<typename SyncDataT, size_t MaxCount>
struct ElementPool {
    struct Entry {
        ElementID id = INVALID_ELEMENT_ID;
        ElementState state = ElementState::FREE;
        SyncDataT data{};
    };

    std::array<Entry, MaxCount> entries{};
    mutable std::shared_mutex mutex;

    Entry* findById(ElementID id) {
        for (auto& e : entries) {
            if (e.id == id && e.state != ElementState::FREE) return &e;
        }
        return nullptr;
    }

    const Entry* findById(ElementID id) const {
        for (const auto& e : entries) {
            if (e.id == id && e.state != ElementState::FREE) return &e;
        }
        return nullptr;
    }

    Entry* findFree() {
        for (auto& e : entries) {
            if (e.state == ElementState::FREE) return &e;
        }
        return nullptr;
    }

    size_t countActive() const {
        size_t n = 0;
        for (const auto& e : entries) {
            if (e.state != ElementState::FREE) ++n;
        }
        return n;
    }
};

// ---------------------------------------------------------------------------
// ElementManager::Impl
// ---------------------------------------------------------------------------

struct ElementManager::Impl {
    ElementPool<PlayerSyncData, MAX_PLAYERS> players;
    ElementPool<VehicleSyncData, MAX_VEHICLES> vehicles;
    ElementPool<ObjectSyncData, MAX_OBJECTS> objects;
    ElementPool<PedSyncData, MAX_PEDS> peds;

    ElementCreatedCallback onCreate;
    ElementDestroyedCallback onDestroy;
};

ElementManager::ElementManager() : pImpl_(std::make_unique<Impl>()) {}
ElementManager::~ElementManager() = default;

bool ElementManager::createElement(ElementType type, ElementID id) {
    if (id == INVALID_ELEMENT_ID) return false;

    auto createInPool = [&](auto& pool) -> bool {
        std::unique_lock lock(pool.mutex);
        if (pool.findById(id)) return true; // Already exists
        auto* slot = pool.findFree();
        if (!slot) {
            ELEM_LOGE("createElement: pool full for type %d", static_cast<int>(type));
            return false;
        }
        slot->id = id;
        slot->state = ElementState::CREATED;
        slot->data = {};
        slot->data.elementId = id;
        lock.unlock();
        if (pImpl_->onCreate) pImpl_->onCreate(type, id);
        ELEM_LOGI("createElement: type=%d id=%u", static_cast<int>(type), id);
        return true;
    };

    switch (type) {
        case ElementType::PLAYER: return createInPool(pImpl_->players);
        case ElementType::VEHICLE: return createInPool(pImpl_->vehicles);
        case ElementType::OBJECT: return createInPool(pImpl_->objects);
        case ElementType::PED: return createInPool(pImpl_->peds);
        default:
            ELEM_LOGE("createElement: unsupported type %d", static_cast<int>(type));
            return false;
    }
}

void ElementManager::destroyElement(ElementType type, ElementID id) {
    auto destroyInPool = [&](auto& pool) {
        std::unique_lock lock(pool.mutex);
        auto* entry = pool.findById(id);
        if (entry) {
            entry->state = ElementState::DESTROYED;
            entry->id = INVALID_ELEMENT_ID;
            entry->data = {};
            lock.unlock();
            if (pImpl_->onDestroy) pImpl_->onDestroy(type, id);
            ELEM_LOGI("destroyElement: type=%d id=%u", static_cast<int>(type), id);
        }
    };

    switch (type) {
        case ElementType::PLAYER: destroyInPool(pImpl_->players); break;
        case ElementType::VEHICLE: destroyInPool(pImpl_->vehicles); break;
        case ElementType::OBJECT: destroyInPool(pImpl_->objects); break;
        case ElementType::PED: destroyInPool(pImpl_->peds); break;
        default: break;
    }
}

bool ElementManager::hasElement(ElementType type, ElementID id) const {
    return getElementState(type, id) != ElementState::FREE;
}

ElementState ElementManager::getElementState(ElementType type, ElementID id) const {
    auto checkPool = [&](const auto& pool) -> ElementState {
        std::shared_lock lock(pool.mutex);
        auto* e = pool.findById(id);
        return e ? e->state : ElementState::FREE;
    };

    switch (type) {
        case ElementType::PLAYER: return checkPool(pImpl_->players);
        case ElementType::VEHICLE: return checkPool(pImpl_->vehicles);
        case ElementType::OBJECT: return checkPool(pImpl_->objects);
        case ElementType::PED: return checkPool(pImpl_->peds);
        default: return ElementState::FREE;
    }
}

bool ElementManager::updatePlayerSync(const PlayerSyncData& data) {
    std::unique_lock lock(pImpl_->players.mutex);
    auto* e = pImpl_->players.findById(data.elementId);
    if (!e) return false;
    e->data = data;
    e->state = ElementState::SYNCING;
    return true;
}

bool ElementManager::updateVehicleSync(const VehicleSyncData& data) {
    std::unique_lock lock(pImpl_->vehicles.mutex);
    auto* e = pImpl_->vehicles.findById(data.elementId);
    if (!e) return false;
    e->data = data;
    e->state = ElementState::SYNCING;
    return true;
}

bool ElementManager::updateObjectSync(const ObjectSyncData& data) {
    std::unique_lock lock(pImpl_->objects.mutex);
    auto* e = pImpl_->objects.findById(data.elementId);
    if (!e) return false;
    e->data = data;
    e->state = ElementState::SYNCING;
    return true;
}

bool ElementManager::updatePedSync(const PedSyncData& data) {
    std::unique_lock lock(pImpl_->peds.mutex);
    auto* e = pImpl_->peds.findById(data.elementId);
    if (!e) return false;
    e->data = data;
    e->state = ElementState::SYNCING;
    return true;
}

bool ElementManager::getPlayerSync(ElementID id, PlayerSyncData& out) const {
    std::shared_lock lock(pImpl_->players.mutex);
    auto* e = pImpl_->players.findById(id);
    if (!e) return false;
    out = e->data;
    return true;
}

bool ElementManager::getVehicleSync(ElementID id, VehicleSyncData& out) const {
    std::shared_lock lock(pImpl_->vehicles.mutex);
    auto* e = pImpl_->vehicles.findById(id);
    if (!e) return false;
    out = e->data;
    return true;
}

void ElementManager::iterateElements(ElementType type,
    std::function<void(ElementID, ElementState)> callback) const
{
    auto iterPool = [&](const auto& pool) {
        std::shared_lock lock(pool.mutex);
        for (const auto& e : pool.entries) {
            if (e.state != ElementState::FREE) {
                callback(e.id, e.state);
            }
        }
    };

    switch (type) {
        case ElementType::PLAYER: iterPool(pImpl_->players); break;
        case ElementType::VEHICLE: iterPool(pImpl_->vehicles); break;
        case ElementType::OBJECT: iterPool(pImpl_->objects); break;
        case ElementType::PED: iterPool(pImpl_->peds); break;
        default: break;
    }
}

void ElementManager::setCallbacks(ElementCreatedCallback onCreate,
                                   ElementDestroyedCallback onDestroy)
{
    pImpl_->onCreate = std::move(onCreate);
    pImpl_->onDestroy = std::move(onDestroy);
}

size_t ElementManager::activeCount(ElementType type) const {
    switch (type) {
        case ElementType::PLAYER: return pImpl_->players.countActive();
        case ElementType::VEHICLE: return pImpl_->vehicles.countActive();
        case ElementType::OBJECT: return pImpl_->objects.countActive();
        case ElementType::PED: return pImpl_->peds.countActive();
        default: return 0;
    }
}

} // namespace WorldRPG::Sync