// Scripted game layer for the headless client. Replaces app/src/main/cpp/gtasa/GameEngine.cpp
// in this build (that file is Android-only in practice; its non-Android section is the stub
// surface copied here). Nothing under app/src/main/cpp is edited.
//
// Driver script (mode driver, after MarkJoined and PLAYER_SPAWN):
//   pick    nearest drivable car (ENTITY_ADD via ServerWorld, or NoteRemoteVehicle) within 80 m
//           of the spawn. The body walks to the door on the car's right at 4 m/s, on-foot
//           PLAYER_PURESYNC (32) every tick, because the server refuses an entry from too far
//           away (VEHICLE_ATTEMPT_FAILED reason 5 = FAIL_DISTANCE).
//   t       body within 1 m of the door -> LocalVehicleEvent{id,true} queued (VEHICLE_INOUT
//           request_in). From here LocalVehicleId and CopyLocalCar report the car, and the
//           sender switches to PLAYER_VEHICLE_PURESYNC (33).
//   t+20s   LocalVehicleEvent{id,false} queued (request_out); the body stands beside the car.
//   t+25s   three weapon-31 shots from the body, 0.25 s apart, 30 m ahead (PLAYER_BULLETSYNC 98).
// Observer mode only records vehicles and logs; it also takes the spawn so the server sends it
// the same relays as the driver.

#include "game_script.h"

#include "gtasa/GameEngine.h"
#include "gtasa/PlayerClothesTable.h"
#include "shared/PedQueries.h"
#include "shared/ServerWorld.h"
#include "stubs/test_ped_queries.h"

#include <unordered_map>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <strings.h>
#include <deque>
#include <map>
#include <memory>
#include <set>

namespace {

using Clock = std::chrono::steady_clock;
const Clock::time_point kProcessStart = Clock::now();

constexpr float kPi = 3.14159265f;
constexpr float kCarSpeed = 2.0f;           // m/s along the car heading once driving
constexpr float kWalkSpeed = 25.0f;         // m/s on foot, from the spawn to the car door
constexpr float kPickRadius = 700.0f;       // m from the spawn
constexpr float kEnterReach = 1.0f;         // m from the door point before the enter request
constexpr double kDriveSeconds = 20.0;      // enter -> exit
constexpr double kShotDelaySeconds = 25.0;  // enter -> first shot
constexpr double kShotSpacingSeconds = 0.25;
// The server needs on-foot sync before it can judge the entry distance (FAIL_DISTANCE, reason 5). A spawn
// next to the door would otherwise request the entry within milliseconds of the spawn.
constexpr double kMinWalkSeconds = 1.5;
constexpr int kShotCount = 3;
constexpr int kShotWeapon = 31;             // M4
constexpr float kShotRange = 30.0f;
constexpr float kSideOffset = 2.0f;         // door point, right side of the car
constexpr float kDriverHealth = 900.f;
constexpr uint32_t kNoVehicle = 0xFFFFFFFFu;

// Road vehicles from the GTA SA car list (no bikes, boats, planes, helicopters or trains).
bool IsDriveableCar(int model) {
    switch (model) {
        case 401: case 402: case 405: case 410: case 412: case 418: case 419: case 421: case 426:
        case 436: case 445: case 466: case 475: case 479: case 492: case 507: case 542: case 560:
        case 580: case 585: case 603:
            return true;
        default:
            return false;
    }
}

bool PlausiblePosition(float x, float y, float z) {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return false;
    if (x == 0.f && y == 0.f) return false;
    return std::fabs(x) < 3000.f && std::fabs(y) < 3000.f && std::fabs(z) < 1000.f;
}

// GTA heading: forward is (-sin h, cos h), right is (cos h, sin h). Radians.
void Forward(float h, float& fx, float& fy) { fx = -std::sin(h); fy = std::cos(h); }
void Right(float h, float& rx, float& ry) { rx = std::cos(h); ry = std::sin(h); }

struct SeenVehicle {
    int model = 0;
    float x = 0, y = 0, z = 0, heading = 0;
};
std::map<uint32_t, SeenVehicle> g_seen;  // NoteRemoteVehicle records
std::set<uint32_t> g_rejected;           // cars the server refused for the driver's entry

enum class Phase : uint8_t { Waiting, Walking, InCar, OnFoot };

struct DriverState {
    Phase phase = Phase::Waiting;
    bool announcedWaiting = false;
    uint32_t carId = kNoVehicle;
    int carModel = 0;
    float carX = 0, carY = 0, carZ = 0, carHeading = 0;
    float doorX = 0, doorY = 0;
    double lastWalk = 0;
    double walkStart = 0;
    double enterAt = 0;
    double exitAt = 0;
    std::deque<double> shotsDue;
};
DriverState g_drv;
std::deque<WorldRPG::Game::LocalVehicleEvent> g_events;

// On-foot body of the local ped. ApplyServerSpawn (PLAYER_SPAWN) puts it at the server spawn, the
// position the server streams elements around and sends relays to. Without it the bot reports
// (0,0,0) and nothing near the spawn reaches it. The driver walks it to the car door.
struct Foot {
    float x = 0, y = 0, z = 0, heading = 0;
};
Foot g_foot;
Foot g_spawn;
bool g_hasSpawn = false;

// Mode and join state are set by the host (main.cpp) on the main thread.
WorldRPG::Headless::Mode g_mode = WorldRPG::Headless::Mode::Observer;
bool g_joined = false;

bool DriverActive() { return g_mode == WorldRPG::Headless::Mode::Driver && g_joined; }

// Car position at time now (frozen at exit). Only meaningful after enterAt.
void CarPos(double now, float& x, float& y, float& z) {
    const double until = g_drv.phase == Phase::OnFoot ? g_drv.exitAt : now;
    const double dt = until > g_drv.enterAt ? until - g_drv.enterAt : 0.0;
    float fx = 0, fy = 0;
    Forward(g_drv.carHeading, fx, fy);
    x = g_drv.carX + fx * kCarSpeed * static_cast<float>(dt);
    y = g_drv.carY + fy * kCarSpeed * static_cast<float>(dt);
    z = g_drv.carZ;
}

// Nearest drivable car within kPickRadius of (ox, oy). Source: ENTITY_ADD vehicles
// (ServerWorld::CopyActors, kind 2) and NoteRemoteVehicle records. knownVehicles counts every
// vehicle seen, drivable or not.
bool PickCar(float ox, float oy, uint32_t& id, int& model, float& x, float& y, float& z, float& heading,
             const char*& source, size_t& knownVehicles, float& distance) {
    struct Candidate { int model; float x, y, z, heading; const char* source; };
    std::map<uint32_t, Candidate> candidates;

    static WorldRPG::ServerWorld::ActorSpawn actors[512];
    const int actorCount = WorldRPG::ServerWorld::CopyActors(actors, 512, 0, 512);
    knownVehicles = 0;
    for (int i = 0; i < actorCount; ++i) {
        const auto& a = actors[i];
        if (a.kind != 2) continue;
        ++knownVehicles;
        if (!IsDriveableCar(a.model) || !PlausiblePosition(a.x, a.y, a.z)) continue;
        candidates.emplace(a.id, Candidate{a.model, a.x, a.y, a.z, a.heading, "entity_add"});
    }
    for (const auto& [vid, s] : g_seen) {
        ++knownVehicles;
        if (!IsDriveableCar(s.model) || !PlausiblePosition(s.x, s.y, s.z)) continue;
        candidates.emplace(vid, Candidate{s.model, s.x, s.y, s.z, s.heading, "note_remote"});
    }
    bool found = false;
    float best = kPickRadius;
    for (const auto& [cid, c] : candidates) {
        if (g_rejected.count(cid)) continue;
        const float d = std::hypot(c.x - ox, c.y - oy);
        if (d > best) continue;
        best = d;
        found = true;
        id = cid;
        model = c.model;
        x = c.x;
        y = c.y;
        z = c.z;
        heading = c.heading;
        source = c.source;
        distance = d;
    }
    return found;
}

void Advance() {
    if (!DriverActive()) return;
    const double now = WorldRPG::Headless::ElapsedSeconds();

    if (g_drv.phase == Phase::Waiting) {
        if (!g_hasSpawn) return;  // the body has no position until PLAYER_SPAWN
        uint32_t id = kNoVehicle;
        int model = 0;
        float x = 0, y = 0, z = 0, heading = 0, distance = 0;
        const char* source = "";
        size_t known = 0;
        // Searched from where the body stands: the spawn at first, the last refused door after a re-pick.
        if (!PickCar(g_foot.x, g_foot.y, id, model, x, y, z, heading, source, known, distance)) {
            if (!g_drv.announcedWaiting) {
                g_drv.announcedWaiting = true;
                std::printf("[%7.3fs] DRV waiting for a drivable car within %.0f m of spawn (vehicles known: %zu)\n",
                            now, kPickRadius, known);
            }
            return;
        }
        float rx = 0, ry = 0;
        Right(heading, rx, ry);
        g_drv.phase = Phase::Walking;
        g_drv.carId = id;
        g_drv.carModel = model;
        g_drv.carX = x;
        g_drv.carY = y;
        g_drv.carZ = z;
        g_drv.carHeading = heading;
        g_drv.doorX = x + rx * kSideOffset;
        g_drv.doorY = y + ry * kSideOffset;
        g_drv.lastWalk = now;
        g_drv.walkStart = now;
        std::printf("[%7.3fs] DRV pick veh=%u model=%d source=%s pos=(%.2f,%.2f,%.2f) heading=%.3f rad "
                    "dist=%.1f m; walking to door (%.2f,%.2f)\n",
                    now, id, model, source, x, y, z, heading, distance, g_drv.doorX, g_drv.doorY);
        return;
    }

    if (g_drv.phase == Phase::Walking) {
        const double dt = now - g_drv.lastWalk;
        g_drv.lastWalk = now;
        const float dx = g_drv.doorX - g_foot.x;
        const float dy = g_drv.doorY - g_foot.y;
        const float dist = std::hypot(dx, dy);
        if (dist <= kEnterReach && now - g_drv.walkStart >= kMinWalkSeconds) {
            g_drv.phase = Phase::InCar;
            g_drv.enterAt = now;
            for (int k = 0; k < kShotCount; ++k) {
                g_drv.shotsDue.push_back(g_drv.enterAt + kShotDelaySeconds + k * kShotSpacingSeconds);
            }
            g_events.push_back(WorldRPG::Game::LocalVehicleEvent{g_drv.carId, true});
            std::printf("[%7.3fs] DRV enter veh=%u (body %.2f m from door); queued VEHICLE_INOUT request_in; "
                        "%d shots scheduled at +%.0fs (weapon %d, %.2fs apart)\n",
                        now, g_drv.carId, dist, kShotCount, kShotDelaySeconds, kShotWeapon, kShotSpacingSeconds);
        } else {
            const float step = std::min(dist, kWalkSpeed * static_cast<float>(dt));
            const float safeDist = std::max(dist, 1e-6f);  // at the door, waiting out kMinWalkSeconds
            g_foot.x += dx / safeDist * step;
            g_foot.y += dy / safeDist * step;
            g_foot.z = g_drv.carZ;
            // Forward (-sin h, cos h) points at the door.
            g_foot.heading = std::atan2(-dx, dy);
        }
        return;
    }

    if (g_drv.phase == Phase::InCar && now >= g_drv.enterAt + kDriveSeconds) {
        g_drv.phase = Phase::OnFoot;
        g_drv.exitAt = g_drv.enterAt + kDriveSeconds;
        float ex = 0, ey = 0, ez = 0;
        CarPos(g_drv.exitAt, ex, ey, ez);
        float rx = 0, ry = 0;
        Right(g_drv.carHeading, rx, ry);
        g_foot.x = ex + rx * kSideOffset;
        g_foot.y = ey + ry * kSideOffset;
        g_foot.z = ez;
        g_foot.heading = g_drv.carHeading;
        g_events.push_back(WorldRPG::Game::LocalVehicleEvent{g_drv.carId, false});
        std::printf("[%7.3fs] DRV exit veh=%u after %.0fs; queued VEHICLE_INOUT request_out; body at (%.2f,%.2f,%.2f)\n",
                    now, g_drv.carId, kDriveSeconds, g_foot.x, g_foot.y, g_foot.z);
    }
}

// Body pose used for on-foot sync and the shot origin. While the driver is in the car it is the car.
void BodyPose(float& x, float& y, float& z, float& heading, float& vx, float& vy, float& vz) {
    vx = vy = vz = 0.f;
    if (DriverActive() && g_drv.phase == Phase::InCar) {
        CarPos(WorldRPG::Headless::ElapsedSeconds(), x, y, z);
        heading = g_drv.carHeading;
        float fx = 0, fy = 0;
        Forward(heading, fx, fy);
        vx = fx * kCarSpeed;
        vy = fy * kCarSpeed;
        return;
    }
    x = g_foot.x;
    y = g_foot.y;
    z = g_foot.z;
    heading = g_foot.heading;
}

// createPed bookkeeping for the host. The phone creates the body in GameEngine.cpp.
// Four slots and ids from 0x300001 match that file. No GTA ped is constructed here.
struct HostScriptPed {
    uint32_t id = 0;
    int model = 0;
    bool used = false;
    char texture[4][40]{};
    char cloth[4][40]{};
    bool on[4]{};
};
HostScriptPed g_hostPeds[4];
uint32_t g_hostNextPed = 0x300001u;

} // namespace

// ---------------------------------------------------------------------------
// Host control
// ---------------------------------------------------------------------------

namespace WorldRPG::Headless {
namespace {
std::unordered_map<uint32_t, PedQueries::Snapshot> queryNative;
PedSyncFields pedSyncFields;
PlayerCombatUpdate combatUpdate;
std::optional<VehicleSync::UnoccupiedUpdate> unoccupiedUpdate;
}
PlayerCombatUpdate TakePlayerCombatUpdate() { return std::exchange(combatUpdate, {}); }
PedSyncFields TakePedSyncFields() { return std::exchange(pedSyncFields, {}); }
std::optional<VehicleSync::UnoccupiedUpdate> TakeUnoccupiedUpdate() { return std::exchange(unoccupiedUpdate, std::nullopt); }
void SetPedQueryNative(uint32_t id, const PedQueries::Snapshot& snapshot) { queryNative[id] = snapshot; }
void ClearPedQueryNative() { queryNative.clear(); }
bool CopyPedQueryNative(uint32_t id, PedQueries::Snapshot& snapshot) {
    auto it = queryNative.find(id);
    if (it == queryNative.end() || snapshot.generation != it->second.generation) return false;
    snapshot.streamed = true;
    snapshot.weapons = it->second.weapons;
    snapshot.currentSlot = it->second.currentSlot;
    snapshot.taskType = it->second.taskType;
    snapshot.health = it->second.health;
    return true;
}

struct TestRemoteActor {
    bool streamed = false;
    bool hasPose = false;
    float x = 0.f, y = 0.f, z = 0.f;
    float heading = 0.f;
    float vx = 0.f, vy = 0.f, vz = 0.f;
};
std::unordered_map<uint32_t, TestRemoteActor> g_testRemoteActors;

ScriptProbe& Probe() {
    static ScriptProbe probe;
    return probe;
}
void ResetProbe() { Probe() = ScriptProbe{}; }
void SetRemoteActorStreamed(uint32_t id, bool streamed) { g_testRemoteActors[id].streamed = streamed; }
void SetRemoteActorPose(uint32_t id, float x, float y, float z, float headingRad, float vx, float vy, float vz) {
    auto& a = g_testRemoteActors[id];
    a.hasPose = true;
    a.x = x; a.y = y; a.z = z;
    a.heading = headingRad;
    a.vx = vx; a.vy = vy; a.vz = vz;
}
void ResetRemoteActorTestState() { g_testRemoteActors.clear(); }

void SetMode(Mode mode) { g_mode = mode; }

void MarkJoined() {
    if (g_joined) return;
    g_joined = true;
    std::printf("[%7.3fs] GAME joined mode=%s\n", ElapsedSeconds(),
                g_mode == Mode::Driver ? "driver" : "observer");
}

double ElapsedSeconds() {
    return std::chrono::duration<double>(Clock::now() - kProcessStart).count();
}

void NoteServerVehicleInOut(uint32_t vehicle, uint8_t action) {
    if (!DriverActive() || g_drv.carId == kNoVehicle || vehicle != g_drv.carId) return;
    const double now = ElapsedSeconds();
    const bool entryRefused = (action == 7 || action == 9) &&
                              (g_drv.phase == Phase::Walking ||
                               (g_drv.phase == Phase::InCar && now - g_drv.enterAt < 5.0));
    if (!entryRefused) {
        if (action == 7 || action == 9) {
            std::printf("[%7.3fs] DRV server reply action=%u on veh=%u while %s; no re-pick\n", now,
                        static_cast<unsigned>(action), vehicle, g_drv.phase == Phase::OnFoot ? "on foot" : "exiting");
        }
        return;
    }
    std::printf("[%7.3fs] DRV entry refused for veh=%u (server action %u = %s); blacklisted, re-picking from (%.2f,%.2f)\n",
                now, vehicle, static_cast<unsigned>(action), action == 7 ? "request_jack_confirmed" : "attempt_failed",
                g_foot.x, g_foot.y);
    g_rejected.insert(vehicle);
    g_drv.shotsDue.clear();
    g_drv.phase = Phase::Waiting;
    g_drv.announcedWaiting = false;
    g_drv.carId = kNoVehicle;
}

} // namespace WorldRPG::Headless

// ---------------------------------------------------------------------------
// Scripted part of the GameEngine.h surface
// ---------------------------------------------------------------------------

namespace WorldRPG::Game {

bool BeginServerSession() { return false; }
bool ServerSessionLive() { return true; }
bool LocalPlacementPending() { return false; }

void ApplyServerSpawn(float x, float y, float z) {
    g_spawn = Foot{x, y, z, 0.f};
    if (g_drv.phase == Phase::Waiting) g_foot = g_spawn;  // observers never leave Waiting
    g_hasSpawn = true;
    std::printf("[%7.3fs] GAME spawn applied at (%.2f,%.2f,%.2f)\n", WorldRPG::Headless::ElapsedSeconds(), x, y, z);
}

void CopySpawn(float& x, float& y, float& z) {
    x = g_spawn.x;
    y = g_spawn.y;
    z = g_spawn.z;
}

void CopyPose(float& x, float& y, float& z, float& heading) {
    float vx = 0, vy = 0, vz = 0;
    BodyPose(x, y, z, heading, vx, vy, vz);
}

float CopyRadarHeading() {
    float x = 0, y = 0, z = 0, heading = 0;
    CopyPose(x, y, z, heading);
    return heading;
}

void CopyBody(float& x, float& y, float& z, float& heading,
              float& vx, float& vy, float& vz, float& health) {
    Advance();
    BodyPose(x, y, z, heading, vx, vy, vz);
    health = 100.f;
}
float CopyLocalArmor() { return 0.f; }
// No libGTASA in this build, so getGroundPosition cannot query the world.
// Optional host-test overrides for sight and camera projection unit tests (default disabled).
static bool g_hostRoofEnabled = false;
static float g_hostRoofZ = 0.f;
static bool g_hostLosEnabled = false;
static bool g_hostLosHit = false;
static LineOfSightResult g_hostLosResult{};
static bool g_hostViewMatrixEnabled = false;
static float g_hostViewMatrix[16]{};

void HostSetRoofTest(bool enabled, float roofZ) { g_hostRoofEnabled = enabled; g_hostRoofZ = roofZ; }
void HostSetLosTest(bool enabled, bool hit, const LineOfSightResult& result) {
    g_hostLosEnabled = enabled; g_hostLosHit = hit; g_hostLosResult = result;
}
void HostSetViewMatrixTest(bool enabled, const float matrix[16]) {
    g_hostViewMatrixEnabled = enabled;
    if (enabled && matrix) std::memcpy(g_hostViewMatrix, matrix, sizeof(g_hostViewMatrix));
}

bool FindGroundZFor3DPosition(float, float, float, float&) { return false; }
bool FindRoofZFor3DPosition(float, float, float, float& out) {
    if (!g_hostRoofEnabled) return false;
    out = g_hostRoofZ;
    return true;
}
bool ProcessLineOfSight(float, float, float, float, float, float, const LineOfSightFlags&, LineOfSightResult& out) {
    if (!g_hostLosEnabled || !g_hostLosHit) return false;
    out = g_hostLosResult;
    return true;
}
bool IsLineOfSightClear(float, float, float, float, float, float, const LineOfSightFlags&, bool& clear) {
    if (!g_hostLosEnabled) return false;
    clear = !g_hostLosHit;
    return true;
}
bool CopyCameraViewMatrix(float out[16]) {
    if (!g_hostViewMatrixEnabled || !out) return false;
    std::memcpy(out, g_hostViewMatrix, sizeof(g_hostViewMatrix));
    return true;
}
bool IsAmbientSphereVisible(float, float, float, float, bool&) { return false; }
uint32_t NoteScriptPed(int model, float x, float y, float z, float) {
    if (model < 0 || model > 312 || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return 0;
    for (auto& slot : g_hostPeds) {
        if (slot.used) continue;
        slot = {};
        slot.used = true;
        slot.model = model;
        slot.id = g_hostNextPed++;
        if (g_hostNextPed < 0x300001u) g_hostNextPed = 0x300001u;
        PedQueries::Create(slot.id, false);
        return slot.id;
    }
    return 0;
}
bool NoteScriptPedModel(uint32_t, int) { return false; }
bool ReleaseScriptPed(uint32_t id) {
    for (auto& slot : g_hostPeds) {
        if (!slot.used || slot.id != id) continue;
        slot.used = false;
        PedQueries::Forget(id);
        return true;
    }
    return false;
}
bool ScriptPedAlive(uint32_t id) {
    for (const auto& slot : g_hostPeds)
        if (slot.used && slot.id == id) return true;
    return false;
}
void NoteScriptClothes(uint32_t id, const char* texture, const char* model, int type, bool remove) {
    if (type < 0 || type >= 4) return;
    if (!remove) {
        if (type >= PlayerClothesTable::kSlots) return;
        const auto& choices = PlayerClothesTable::kTable[type];
        bool listed = false;
        for (int i = 0; texture && model && i < choices.count; ++i)
            if (strcasecmp(texture, choices.items[i].texture) == 0 && strcasecmp(model, choices.items[i].model) == 0)
                listed = true;
        if (!listed) return;
    }
    for (auto& slot : g_hostPeds) {
        if (!slot.used || slot.id != id) continue;
        slot.on[type] = !remove && texture && model && texture[0] && model[0];
        if (slot.on[type]) {
            std::snprintf(slot.texture[type], sizeof slot.texture[type], "%s", texture);
            std::snprintf(slot.cloth[type], sizeof slot.cloth[type], "%s", model);
        }
        return;
    }
}
bool CopyScriptClothes(uint32_t id, int type, char* texture, int textureBytes, char* model, int modelBytes) {
    if (!texture || !model || textureBytes <= 0 || modelBytes <= 0 || type < 0 || type >= 4) return false;
    texture[0] = 0;
    model[0] = 0;
    for (const auto& slot : g_hostPeds) {
        if (!slot.used || slot.id != id || !slot.on[type]) continue;
        std::snprintf(texture, static_cast<size_t>(textureBytes), "%s", slot.texture[type]);
        std::snprintf(model, static_cast<size_t>(modelBytes), "%s", slot.cloth[type]);
        return texture[0] != 0 && model[0] != 0;
    }
    return false;
}
void NoteScriptRotation(uint32_t, float) {}
void NoteScriptFrozen(uint32_t, bool) {}
void NoteScriptCamera(float x, float y, float z, float lx, float ly, float lz) {
    auto& probe = WorldRPG::Headless::Probe();
    ++probe.cameraNotes;
    const float values[6] = {x, y, z, lx, ly, lz};
    for (int i = 0; i < 6; ++i) probe.camera[i] = values[i];
}
void ClearScriptCamera() { ++WorldRPG::Headless::Probe().cameraClears; }
bool NoteScriptTime(int hour, int minute) {
    if (hour < 0 || hour > 23 || minute < 0 || minute > 60) return false;
    auto& probe = WorldRPG::Headless::Probe();
    probe.timeSet = true;
    probe.hour = hour;
    probe.minute = minute;
    return true;
}
bool NoteScriptWeather(int weather) {
    if (weather < 0 || weather > 255) return false;
    auto& probe = WorldRPG::Headless::Probe();
    probe.weatherSet = true;
    probe.weather = weather;
    return true;
}
bool CopyScriptWeather(int& weather) {
    const auto& probe = WorldRPG::Headless::Probe();
    if (!probe.weatherSet) return false;
    weather = probe.weather;
    return true;
}
bool CopyScriptPedFacts(uint32_t id, int& model, float&, bool&) {
    for (const auto& slot : g_hostPeds) {
        if (!slot.used || slot.id != id) continue;
        model = slot.model;
        return true;
    }
    return false;
}
void NoteLocalHealth(float) {}
void SetLocalWantedLevel(uint8_t level) {
    auto& probe = WorldRPG::Headless::Probe();
    ++probe.wantedCalls;
    probe.wantedLevel = level > 6 ? 6 : level;
}

void NoteDamage(uint32_t, float, uint8_t, uint8_t) {}
Combat::DamageReport PeekDamage() { return {}; }
void AcknowledgeDamage(uint64_t) {}
void NoteRemoteAim(uint32_t id, float, float, int, int, uint8_t buttons, bool, const PlayerWeaponSync::RemoteWeapon* weapon) {
    Headless::combatUpdate.fire = (buttons & PlayerWeaponSync::kFire) != 0;
    Headless::combatUpdate.buttons = buttons;
    if (weapon) NoteRemotePlayerWeapon(id, *weapon);
}
PlayerWeaponSync::LocalWeapon CopyLocalWeapon() { return {}; }
static std::unordered_map<uint32_t, bool> drivebyState;
void NoteDriveby(uint32_t id, bool enabled) { drivebyState[id] = enabled; }
bool IsDriveby(uint32_t id) { return drivebyState[id]; }
void NoteVehicleCombat(uint32_t id, const PlayerWeaponSync::RemoteWeapon& weapon, uint8_t buttons, bool driveby) {
    NoteDriveby(id, driveby);
    NoteRemoteAim(id, 0.f, 0.f, 0, 0, driveby ? buttons : 0, false, &weapon);
}
void NoteRemotePlayerWeapon(uint32_t id, const PlayerWeaponSync::RemoteWeapon& weapon) {
    Headless::combatUpdate.player = id;
    Headless::combatUpdate.weapon = weapon;
}
void QueueRemoteBullet(const Combat::SBulletSync& shot) {
    Headless::combatUpdate.bullet = shot;
}
bool CopySpatialScene(std::vector<Spatial::Element>&) { return false; }
bool CopyPedQuery(uint32_t id, PedQueries::Snapshot& out) {
    if (!PedQueries::Copy(id, out)) return false;
    Headless::CopyPedQueryNative(id, out);
    return true;
}
bool RemoteFacingLocal(uint32_t, float, float) { return false; }
void CopyOnFoot(float, float, float, OnFootKeys& keys) { keys = {}; }
void ConsumeLocalJump(uint64_t) {}
void ConsumeLocalAttack(uint64_t) {}
void NoteRemotePlayerSync(uint32_t id, float, float, float, float, float, float, float, bool, int, int, float,
                         uint8_t buttons, bool, const PlayerWeaponSync::RemoteWeapon& weapon, bool, float, bool, float) {
    NoteRemoteAim(id, 0, 0, 0, 0, buttons, false, &weapon);
}
void NoteRemotePed(uint32_t, float, float, float, float, float, float, float, bool) {}
void NoteObservedPedSync(uint32_t id, const float* position, const float* heading, const float* velocity, uint64_t) {
    auto& fields = Headless::pedSyncFields;
    fields = {};
    fields.id = id;
    if (position) fields.position = VehicleSync::Vec3{position[0], position[1], position[2]};
    if (heading) fields.heading = *heading;
    if (velocity) fields.velocity = VehicleSync::Vec3{velocity[0], velocity[1], velocity[2]};
}
void SetPedHost(uint32_t, bool) {}
void ForgetRemote(uint32_t) {}
void RequestRemoteReset() {}

// Records every vehicle the server names, with a model when one is given (model >= 400, as in
// GameEngine.cpp). Prints a line the first time an id is seen and when its model is learned.
void NoteRemoteVehicle(uint32_t id, int model, float x, float y, float z, float heading) {
    if (id == 0 || id == kNoVehicle || !PlausiblePosition(x, y, z)) return;
    auto it = g_seen.find(id);
    if (it == g_seen.end()) {
        SeenVehicle& s = g_seen[id];
        if (model >= 400) s.model = model;
        s.x = x;
        s.y = y;
        s.z = z;
        s.heading = heading;
        std::printf("[%7.3fs] VEH record id=%u model=%d pos=(%.2f,%.2f,%.2f) heading=%.3f\n",
                    WorldRPG::Headless::ElapsedSeconds(), id, s.model, x, y, z, heading);
        return;
    }
    SeenVehicle& s = it->second;
    if (model >= 400 && s.model != model) {
        s.model = model;
        std::printf("[%7.3fs] VEH model id=%u model=%d\n", WorldRPG::Headless::ElapsedSeconds(), id, model);
    }
    s.x = x;
    s.y = y;
    s.z = z;
    s.heading = heading;
}

// VEHICLE_STARTSYNC: the pose is recorded like any other vehicle pose; speeds and health have no body here.
void NoteVehicleStartSync(uint32_t id, const float pos[3], const float rot[3], const float*, const float*, float) {
    if (!pos || !rot) return;
    NoteRemoteVehicle(id, 0, pos[0], pos[1], pos[2], rot[2] * 3.14159265f / 180.f);
}
void NoteRemoteSeat(uint32_t, uint32_t, uint8_t) {}
void NoteRemoteUnseat(uint32_t) {}
void NoteRemoteVehicleMotion(uint32_t, float, float, float, float, float, float) {}
void NoteUnoccupiedVehicleSync(const VehicleSync::UnoccupiedUpdate& update) { Headless::unoccupiedUpdate = update; }
void RpcSetVehiclePosition(uint32_t, float, float, float) {}
void RpcSetVehicleHealth(uint32_t id, float health) {
    auto& probe = WorldRPG::Headless::Probe();
    ++probe.vehicleHealthCalls;
    probe.vehicleHealthId = id;
    probe.vehicleHealth = health;
}
void RpcFixVehicle(uint32_t id) { RpcSetVehicleHealth(id, 1000.f); }
void RpcBlowVehicle(uint32_t id, bool) { RpcSetVehicleHealth(id, 0.f); }
void RpcSetVehicleRotation(uint32_t, float, float, float) {}
void RpcSetVehicleVelocity(uint32_t, float, float, float, bool) {}
bool SetOwnedPedControl(uint32_t, const char*, bool) { return false; }
bool StartPedKillOnFoot(uint32_t, uint32_t) { return false; }
bool PresentPedWeaponShot(uint32_t, unsigned, float, float, float) { return false; }
bool StartPedWander(uint32_t, int, int, bool) { return false; }
bool StartPedDriveWander(uint32_t, uint32_t, float, int) { return false; }
bool ScanTrafficObstacle(uint32_t, bool, float, TrafficObstacleScan&) { return false; }
bool IsTrafficPedTaskActive(uint32_t, TrafficPedTask) { return false; }
bool KillNativePedTask(uint32_t, bool, int, bool) { return false; }
bool IsNativeActorStreamed(uint32_t id) {
    if (id == WorldRPG::ServerWorld::LocalPlayer() && id != 0) return true;
    auto it = WorldRPG::Headless::g_testRemoteActors.find(id);
    return it != WorldRPG::Headless::g_testRemoteActors.end() && it->second.streamed;
}
bool HasNativeTrafficCapability(const char*) { return false; }
bool SetTrafficVehicleVelocity(uint32_t, float, float, float, bool) { return false; }
bool SetTrafficVehicleRotation(uint32_t, float, float, float) { return false; }
bool IsTrafficVehicleOnGround(uint32_t) { return false; }
bool SetTrafficVehicleLoadCollision(uint32_t, bool) { return false; }
bool CopyTrafficVehicleState(uint32_t, HostedCar&) { return false; }
bool SetTrafficVehicleEngine(uint32_t, bool) { return false; }
bool CopyRemotePedMotion(uint32_t id, float& heading, float& vx, float& vy, float& vz) {
    auto it = WorldRPG::Headless::g_testRemoteActors.find(id);
    if (it == WorldRPG::Headless::g_testRemoteActors.end() || !it->second.hasPose) return false;
    heading = it->second.heading;
    vx = it->second.vx;
    vy = it->second.vy;
    vz = it->second.vz;
    return true;
}
bool CopyRemotePose(uint32_t id, float& x, float& y, float& z) {
    auto it = WorldRPG::Headless::g_testRemoteActors.find(id);
    if (it != WorldRPG::Headless::g_testRemoteActors.end() && it->second.hasPose) {
        x = it->second.x;
        y = it->second.y;
        z = it->second.z;
        return true;
    }
    x = y = z = 0.f;
    return false;
}
int CopyHostedPeds(HostedPed*, int) { return 0; }
int CopyHostedCars(HostedCar*, int) { return 0; }

bool ConsumeLocalVehicleEvent(LocalVehicleEvent& out) {
    Advance();
    if (g_events.empty()) return false;
    out = g_events.front();
    g_events.pop_front();
    return true;
}

uint32_t LocalVehicleId() {
    Advance();
    return g_drv.phase == Phase::InCar ? g_drv.carId : kNoVehicle;
}

void AcknowledgeVehicleFire(uint64_t) {}
bool CopyLocalCar(LocalCarState& out) {
    Advance();
    if (!DriverActive() || g_drv.phase != Phase::InCar) return false;
    const double now = WorldRPG::Headless::ElapsedSeconds();
    float x = 0, y = 0, z = 0;
    CarPos(now, x, y, z);
    float fx = 0, fy = 0;
    Forward(g_drv.carHeading, fx, fy);

    // Once a second: the car state the packet 33 sender is handed (the sender logs only 4 lines).
    static double lastLog = -1.0;
    if (now - lastLog >= 1.0) {
        lastLog = now;
        std::printf("[%7.3fs] DRV car veh=%u model=%d pos=(%.2f,%.2f,%.2f) heading=%.3f rad vel=(%.2f,%.2f,0) hp=%.0f\n",
                    now, g_drv.carId, g_drv.carModel, x, y, z, g_drv.carHeading, fx * kCarSpeed, fy * kCarSpeed,
                    kDriverHealth);
    }

    out = LocalCarState{};
    out.vehicleId = g_drv.carId;
    out.model = g_drv.carModel;
    out.x = x;
    out.y = y;
    out.z = z;
    // rotZ is degrees in [0, 360), matching the desktop's camera basis used by the sender.
    float deg = g_drv.carHeading * 180.f / kPi;
    while (deg < 0.f) deg += 360.f;
    while (deg >= 360.f) deg -= 360.f;
    out.rotZ = deg;
    out.vx = fx * kCarSpeed;
    out.vy = fy * kCarSpeed;
    out.vz = 0.f;
    out.health = kDriverHealth;
    out.accelerate = 255;
    return true;
}

// Scripted shots replace the CWeapon::Fire hook; the host never queues shots itself.
void NoteLocalShot(int, const float*, const float*, const Combat::BulletHit&) {}

void FinishLocalShot(uint64_t serial) {
    if (!g_drv.shotsDue.empty() && uint64_t(g_drv.shotsDue.front() * 1000000) == serial)
        g_drv.shotsDue.pop_front();
}
bool PeekLocalShot(LocalShot& out) {
    Advance();
    if (!DriverActive() || g_drv.shotsDue.empty()) return false;
    if (g_drv.phase != Phase::OnFoot) return false;
    const double now = WorldRPG::Headless::ElapsedSeconds();
    if (now < g_drv.shotsDue.front()) return false;
    const double due = g_drv.shotsDue.front();
    out.serial = uint64_t(due * 1000000);

    float fx = 0, fy = 0;
    Forward(g_foot.heading, fx, fy);
    out.weapon = kShotWeapon;
    out.sx = g_foot.x;
    out.sy = g_foot.y;
    out.sz = g_foot.z + 1.f;
    out.ex = out.sx + fx * kShotRange;
    out.ey = out.sy + fy * kShotRange;
    out.ez = out.sz;
    std::printf("[%7.3fs] DRV shot weapon=%d due=%.3f from (%.2f,%.2f,%.2f) to (%.2f,%.2f,%.2f)\n", now,
                out.weapon, due, out.sx, out.sy, out.sz, out.ex, out.ey, out.ez);
    return true;
}

// ---------------------------------------------------------------------------
// Stubs: engine features the headless client does not drive.
// ---------------------------------------------------------------------------

void SetPedNativeTaskLocomotion(uint32_t, unsigned int) {}
void SetPedNativeTaskWeapon(uint32_t, uint8_t) {}
void SetPedNativeTaskAnimation(uint32_t, uint16_t, uint16_t) {}
void SetPedNativeTaskPhysicalTakeover(uint32_t, unsigned int, uint16_t, uint16_t, float) {}
void SetPedCameraRotation(uint32_t, float) {}
void SetPedOnFire(uint32_t, bool) {}
void SetPedInWater(uint32_t, bool) {}
void KillPedAnimation(uint32_t) {}
void LockPedHealth(uint32_t, float) {}
bool CopyPedVitalsLock(uint32_t, bool& healthLocked, float& health, bool& armorLocked, float& armor) {
    healthLocked = armorLocked = false;
    health = armor = 0.f;
    return false; // the host has no native health/armor locks
}
void LockPedArmor(uint32_t, float) {}
void LockPedHealthCurrent(uint32_t) {}
void LockPedArmorCurrent(uint32_t) {}
void ResolvePedVitalsLockCurrent(uint32_t, float, float) {}
void UnlockPedVitals(uint32_t) {}
void ForgetPedNativeTaskState(uint32_t) {}
void SetPedVitals(uint32_t, float, float) {}
void NoteRemoteClothes(uint32_t, const std::vector<WorldRPG::Entity::PedClothes>&) {}
void RpcSetPosition(uint32_t id, float x, float y, float z) {
    if (id == WorldRPG::ServerWorld::LocalPlayer()) g_foot = {x, y, z, g_foot.heading};
}
void RpcSetFrozen(uint32_t, bool) {}
void NoteRemoteWasted(uint32_t, int, int, int) {}
void NoteLocalWasted(int, int, int) {}
bool CopyLocalCombatDeath(uint64_t&, HostedWasted&) { return false; }
bool ConsumeHostedWasted(HostedWasted&) { return false; }
void NoteRemoteSpawn(uint32_t) {}
void RpcSetRotation(uint32_t, float) {}
void RpcSetInterior(uint32_t, uint8_t) {}
void RpcSetHealth(uint32_t, float) {}
void RpcSetArmor(uint32_t, float) {}
void QueuePedRpc(uint32_t id, const PedRpcOp& op) {
    if (op.queryGeneration && PedQueries::Current(id, op.queryGeneration)) PedQueries::WeaponRpc(id, op);
}
void RpcSetAlpha(uint32_t, uint8_t) {}
void RpcSetCollisions(uint32_t, bool) {}

// ---------------------------------------------------------------------------
// GameEngine class: no libGTASA on the host, every call reports "not available".
// ---------------------------------------------------------------------------

EngineOffsets EngineOffsets::forVersion(const char* version) {
    EngineOffsets offsets{};
    std::memset(&offsets, 0, sizeof(offsets));
    ENGINE_LOGE("EngineOffsets: host build, no engine offsets for '%s'", version ? version : "(null)");
    return offsets;
}

struct GameEngine::Impl {
    bool initialized = false;
    std::atomic<bool> paused{false};
    EngineOffsets offsets{};
};

GameEngine::GameEngine() : pImpl_(std::make_unique<Impl>()) {}
GameEngine::~GameEngine() = default;

bool GameEngine::initialize(const EngineOffsets& offsets) {
    (void)offsets;
    ENGINE_LOGE("GameEngine::initialize: host build, stub only");
    return false;
}

bool GameEngine::isReady() const { return false; }
void GameEngine::pause() { pImpl_->paused.store(true); }
void GameEngine::resume() { pImpl_->paused.store(false); }
bool GameEngine::isPaused() const { return pImpl_->paused.load(); }

PedHandle GameEngine::spawnPed(int, float, float, float, float) { return INVALID_PED; }
void GameEngine::deletePed(PedHandle) {}
void GameEngine::setPedPosition(PedHandle, float, float, float) {}
void GameEngine::getPedPosition(PedHandle, float& x, float& y, float& z) { x = y = z = 0.f; }
void GameEngine::setPedRotation(PedHandle, float, float, float) {}
void GameEngine::setPedHealth(PedHandle, float) {}
float GameEngine::getPedHealth(PedHandle) { return 0.f; }

VehicleHandle GameEngine::spawnVehicle(int, float, float, float, float) { return INVALID_VEHICLE; }
void GameEngine::deleteVehicle(VehicleHandle) {}
void GameEngine::setVehiclePosition(VehicleHandle, float, float, float) {}
void GameEngine::getVehiclePosition(VehicleHandle, float& x, float& y, float& z) { x = y = z = 0.f; }
void GameEngine::setVehicleRotation(VehicleHandle, float, float, float) {}
void GameEngine::setVehicleVelocity(VehicleHandle, float, float, float) {}
void GameEngine::setVehicleHealth(VehicleHandle, float) {}

ObjectHandle GameEngine::spawnObject(int, float, float, float) { return INVALID_OBJECT; }
void GameEngine::deleteObject(ObjectHandle) {}
void GameEngine::setObjectPosition(ObjectHandle, float, float, float) {}
void GameEngine::setObjectRotation(ObjectHandle, float, float, float) {}

void GameEngine::requestModel(int) {}
void GameEngine::loadModels() {}
bool GameEngine::hasModelLoaded(int) { return false; }

PedHandle GameEngine::getPlayerPed() { return INVALID_PED; }
void GameEngine::setPlayerControl(bool) {}

} // namespace WorldRPG::Game
