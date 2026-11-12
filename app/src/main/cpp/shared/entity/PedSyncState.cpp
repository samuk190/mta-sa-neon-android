#include "PedSyncState.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <mutex>
#include <unordered_map>

#include "../../gtasa/GameEngine.h"

namespace WorldRPG::Game {

// Engine hooks declared in GameEngine.h (stage S4). They forward to the existing
// remote-ped calls in GameEngine.cpp; no GTA natives are called here.
void SetPedWander(uint32_t id, int direction) { RequestWander(id, direction); }
void ClearPedWander(uint32_t id) { SetPedHost(id, false); }
void ApplyPedTarget(uint32_t id, float x, float y, float z, float heading,
    float vx, float vy, float vz) {
    NoteRemotePed(id, x, y, z, heading, vx, vy, vz);
}

} // namespace WorldRPG::Game

namespace WorldRPG::Sync::PedSyncState {
namespace {

// iPedSync default (CTickRateSettings.h:19).
constexpr uint64_t kPedSyncRateMs = 400;
// CPedSync.cpp:418-419 receive-interval clamp (50 ms floor).
constexpr uint64_t kMinReceiveIntervalMs = 50;

struct Entry {
    bool hosted = false;
    bool havePose = false;
    Interp::Vec3 pose{};
    float heading = 0.f;
    Interp::Vec3 velocity{};
    Interp::PositionInterp position{};
    Interp::RotationInterp rotation{};
    uint64_t lastSpatialReceiveMs = 0;
};

std::mutex gMutex;
std::unordered_map<uint32_t, Entry> gPeds;

bool ValidId(uint32_t id) { return id != 0 && id != 0xFFFFFFFFu; }

float PlanarSpeed(const Interp::Vec3& v) { return std::sqrt(v.x * v.x + v.y * v.y); }

} // namespace

uint64_t NowMs() {
    using namespace std::chrono;
    return static_cast<uint64_t>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

void BeginHosted(uint32_t id, const Vec3& pos, float heading, const Vec3& velocity) {
    if (!ValidId(id)) return;
    std::lock_guard<std::mutex> lock(gMutex);
    Entry& e = gPeds[id];
    e.hosted = true;
    e.havePose = true;
    e.pose = pos;
    e.heading = heading;
    e.velocity = velocity;
    // AddPed drops the old interpolation and receive timing (CPedSync.cpp:167-186).
    e.position = {};
    e.rotation = {};
    e.lastSpatialReceiveMs = 0;
}

void EndHosted(uint32_t id) {
    if (!ValidId(id)) return;
    std::lock_guard<std::mutex> lock(gMutex);
    Entry& e = gPeds[id];
    e.hosted = false;
    e.position = {};
    e.rotation = {};
    e.lastSpatialReceiveMs = 0;
    Game::ClearPedWander(id);
}

bool IsHosted(uint32_t id) {
    std::lock_guard<std::mutex> lock(gMutex);
    auto it = gPeds.find(id);
    return it != gPeds.end() && it->second.hosted;
}

void SetObserverPosition(uint32_t id, const Vec3& target, const Vec3& velocity, uint64_t delayMs) {
    if (!ValidId(id)) return;
    const uint64_t now = NowMs();
    std::lock_guard<std::mutex> lock(gMutex);
    Entry& e = gPeds[id];
    e.velocity = velocity;
    if (!e.havePose) {
        // No streamed pose yet: place straight (reference SetPosition branch).
        e.pose = target;
        e.havePose = true;
        e.position = {};
        return;
    }
    Interp::BeginPosition(e.position, e.pose, target, delayMs, now);
}

void SetObserverRotation(uint32_t id, float heading, uint64_t delayMs) {
    if (!ValidId(id)) return;
    const uint64_t now = NowMs();
    std::lock_guard<std::mutex> lock(gMutex);
    Entry& e = gPeds[id];
    if (!e.havePose) {
        e.heading = heading;
        e.rotation = {};
        return;
    }
    Interp::BeginRotation(e.rotation, e.heading, heading, delayMs, now);
}

uint64_t SpatialSyncRateMs(uint32_t id, bool spatialPacket) {
    if (!ValidId(id)) return kPedSyncRateMs;
    const uint64_t now = NowMs();
    std::lock_guard<std::mutex> lock(gMutex);
    Entry& e = gPeds[id];
    uint64_t rate = kPedSyncRateMs;
    if (!spatialPacket) return rate;
    const uint64_t last = e.lastSpatialReceiveMs;
    if (last != 0 && now > last) {
        const uint64_t interval = now - last;
        if (interval < kPedSyncRateMs)
            rate = std::clamp(interval, kMinReceiveIntervalMs, kPedSyncRateMs);
    }
    e.lastSpatialReceiveMs = now;
    return rate;
}

bool SamplePose(uint32_t id) {
    if (!ValidId(id)) return false;
    const uint64_t now = NowMs();
    std::lock_guard<std::mutex> lock(gMutex);
    auto it = gPeds.find(id);
    if (it == gPeds.end() || !it->second.havePose) return false;
    Entry& e = it->second;
    if (!e.position.active() && !e.rotation.active()) return false;

    const Interp::Vec3 before = e.pose;
    const float beforeHeading = e.heading;
    if (e.position.active())
        e.pose = Interp::StepPosition(e.position, e.pose, PlanarSpeed(e.velocity), now);
    if (e.rotation.active())
        Interp::StepRotation(e.rotation, e.heading, now);

    const bool moved = before.x != e.pose.x || before.y != e.pose.y || before.z != e.pose.z ||
                       beforeHeading != e.heading;
    if (!moved) return false;
    Game::ApplyPedTarget(id, e.pose.x, e.pose.y, e.pose.z, e.heading,
        e.velocity.x, e.velocity.y, e.velocity.z);
    return true;
}

void Forget(uint32_t id) {
    std::lock_guard<std::mutex> lock(gMutex);
    gPeds.erase(id);
}

} // namespace WorldRPG::Sync::PedSyncState
