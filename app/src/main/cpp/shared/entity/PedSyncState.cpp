#include "PedSyncState.h"

#include <algorithm>
#include <chrono>
#include <mutex>
#include <unordered_map>

namespace WorldRPG::Sync::PedSyncState {
namespace {

// iPedSync default (CTickRateSettings.h:19).
constexpr uint64_t kPedSyncRateMs = 400;
// CPedSync.cpp:418-419 receive-interval clamp (50 ms floor).
constexpr uint64_t kMinReceiveIntervalMs = 50;

struct Entry {
    bool hosted = false;
    uint64_t lastSpatialReceiveMs = 0;
};

std::mutex gMutex;
std::unordered_map<uint32_t, Entry> gPeds;

bool ValidId(uint32_t id) { return id != 0 && id != 0xFFFFFFFFu; }

} // namespace

uint64_t NowMs() {
    using namespace std::chrono;
    return static_cast<uint64_t>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

void BeginHosted(uint32_t id) {
    if (!ValidId(id)) return;
    std::lock_guard<std::mutex> lock(gMutex);
    gPeds[id] = Entry{true};
}

void EndHosted(uint32_t id) {
    Forget(id);
}

bool IsHosted(uint32_t id) {
    std::lock_guard<std::mutex> lock(gMutex);
    auto it = gPeds.find(id);
    return it != gPeds.end() && it->second.hosted;
}

uint64_t SpatialSyncRateMs(uint32_t id) {
    if (!ValidId(id)) return kPedSyncRateMs;
    const uint64_t now = NowMs();
    std::lock_guard<std::mutex> lock(gMutex);
    Entry& e = gPeds[id];
    uint64_t rate = kPedSyncRateMs;
    const uint64_t last = e.lastSpatialReceiveMs;
    if (last != 0 && now > last) {
        const uint64_t interval = now - last;
        if (interval < kPedSyncRateMs)
            rate = std::clamp(interval, kMinReceiveIntervalMs, kPedSyncRateMs);
    }
    e.lastSpatialReceiveMs = now;
    return rate;
}

void Forget(uint32_t id) {
    std::lock_guard<std::mutex> lock(gMutex);
    gPeds.erase(id);
}

} // namespace WorldRPG::Sync::PedSyncState
