#include "RemoteVehicle.h"

#include "GameEngine.h"
#include "../shared/ServerWorld.h"
#include "../shared/entity/EntityAddTypes.h"
#include "../shared/entity/EntityVehicleModels.h"

#include <cstdio>
#include <mutex>
#include <unordered_map>

namespace WorldRPG::RemoteVehicle {
namespace {

using VehicleSync::Vec3;

constexpr float kPi = 3.14159265f;
constexpr size_t kMaxVehicles = 512;
constexpr size_t kMaxOccupants = 128;

struct VehicleState {
    bool havePose = false;
    Vec3 position;
    Vec3 rotation; // degrees
    bool derailed = false;
};

struct Occupant {
    uint32_t vehicleId = 0xFFFFFFFFu;
    uint8_t seat = 0;
};

std::mutex g_mutex;
std::unordered_map<uint32_t, VehicleState> g_vehicles;
std::unordered_map<uint32_t, Occupant> g_occupants;

float RadiansFromDegrees(float degrees) { return degrees * kPi / 180.f; }

bool IsTrainId(uint32_t vehicleId) {
    const int model = WorldRPG::ServerWorld::ModelOf(vehicleId);
    return model >= 0 && WorldRPG::Entity::VehicleModels::IsTrainModel(static_cast<uint32_t>(model));
}

// Caller holds g_mutex. Creates or updates the stored state and returns it, or null when full.
VehicleState* StateFor(uint32_t vehicleId) {
    auto found = g_vehicles.find(vehicleId);
    if (found != g_vehicles.end()) return &found->second;
    if (g_vehicles.size() >= kMaxVehicles) {
        DIAG("RemoteVehicle state table full, vehicle id=%u dropped", vehicleId);
        return nullptr;
    }
    return &g_vehicles[vehicleId];
}

} // namespace

void SetOccupant(uint32_t playerId, uint32_t vehicleId, uint8_t seat) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_occupants.size() >= kMaxOccupants && g_occupants.find(playerId) == g_occupants.end()) {
        DIAG("RemoteVehicle occupant table full, player id=%u not bound", playerId);
        return;
    }
    g_occupants[playerId] = Occupant{vehicleId, seat};
}

void ClearOccupant(uint32_t playerId) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_occupants.erase(playerId);
}

void ResetSession() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_vehicles.clear();
    g_occupants.clear();
}

bool FindOccupant(uint32_t playerId, uint32_t& vehicleId, uint8_t& seat) {
    std::lock_guard<std::mutex> lock(g_mutex);
    auto found = g_occupants.find(playerId);
    if (found == g_occupants.end()) return false;
    vehicleId = found->second.vehicleId;
    seat = found->second.seat;
    return true;
}

// CClientVehicle::SetTargetPosition / SetTargetRotation / SetMoveSpeed / SetTurnSpeed / SetHealth /
// SetEngineOn / SetDerailed. The phone has no remote VehicleHandle yet, so each call records the
// desktop state and logs it. Engine effects are the open gap listed in the roadmap.
void SetTargetPosition(uint32_t vehicleId, const Vec3& pos, uint32_t rateMs, bool useVelocityZ, float velocityZ) {
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        VehicleState* state = StateFor(vehicleId);
        if (!state) return;
        state->position = pos;
        state->havePose = true;
    }
    DIAG("veh %u SetTargetPosition pos=%.1f %.1f %.1f rate=%u velZ=%s%.2f", vehicleId, pos.x, pos.y, pos.z, rateMs,
         useVelocityZ ? "" : "unused:", velocityZ);
}

void SetTargetRotation(uint32_t vehicleId, const Vec3& rot, uint32_t rateMs) {
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        VehicleState* state = StateFor(vehicleId);
        if (!state) return;
        state->rotation = rot;
    }
    DIAG("veh %u SetTargetRotation rot=%.1f %.1f %.1f rate=%u", vehicleId, rot.x, rot.y, rot.z, rateMs);
}

void SetMoveSpeed(uint32_t vehicleId, const Vec3& v) {
    DIAG("veh %u SetMoveSpeed %.2f %.2f %.2f", vehicleId, v.x, v.y, v.z);
}

void SetTurnSpeed(uint32_t vehicleId, const Vec3& v) {
    DIAG("veh %u SetTurnSpeed %.2f %.2f %.2f", vehicleId, v.x, v.y, v.z);
}

void SetHealth(uint32_t vehicleId, float health) {
    DIAG("veh %u SetHealth %.1f", vehicleId, health);
}

void SetEngineOn(uint32_t vehicleId, bool on) {
    DIAG("veh %u SetEngineOn %d", vehicleId, on ? 1 : 0);
}

void SetDerailed(uint32_t vehicleId, bool derailed) {
    std::lock_guard<std::mutex> lock(g_mutex);
    VehicleState* state = StateFor(vehicleId);
    if (state) state->derailed = derailed;
}

// CUnoccupiedVehicleSync::Packet_UnoccupiedVehicleSync (CUnoccupiedVehicleSync.cpp:178-208), in the
// desktop order. Rate is CTickRateSettings iUnoccupiedVehicle (400 ms).
// Gap: CClientVehicle::CanUpdateSync(timeContext) is not ported; the per-vehicle time context is not
// tracked yet, so every update is applied.
void ApplyUnoccupied(const VehicleSync::UnoccupiedUpdate& u) {
    constexpr uint32_t kUnoccupiedRateMs = 400;
    if (u.vehicleId == 0 || u.vehicleId == 0xFFFFFFFFu) {
        DIAG("UNOCCUPIED_VEHICLE_SYNC invalid vehicle id=%u", u.vehicleId);
        return;
    }
    if (u.syncPosition) SetTargetPosition(u.vehicleId, u.position, kUnoccupiedRateMs, u.syncVelocity, u.velocity.z);
    if (u.syncRotation) SetTargetRotation(u.vehicleId, u.rotation, kUnoccupiedRateMs);
    if (u.syncVelocity) SetMoveSpeed(u.vehicleId, u.velocity);
    if (u.syncTurnVelocity) SetTurnSpeed(u.vehicleId, u.turnVelocity);
    if (u.syncHealth) SetHealth(u.vehicleId, u.health);
    SetEngineOn(u.vehicleId, u.engineOn);
    if (IsTrainId(u.vehicleId)) SetDerailed(u.vehicleId, u.derailed);
}

void ApplyOccupied(uint32_t playerId, uint32_t vehicleId, uint8_t seat, const VehicleSync::OccupiedSync& s) {
    if (vehicleId == 0 || vehicleId == 0xFFFFFFFFu) {
        DIAG("PLAYER_VEHICLE_PURESYNC player=%u invalid vehicle", playerId);
        return;
    }
    if (seat != 0 || !s.hasDriverBlock) {
        DIAG("PLAYER_VEHICLE_PURESYNC player=%u veh=%u seat=%u hp=%.1f (passenger, no pose)", playerId, vehicleId,
             static_cast<unsigned>(seat), s.playerHealth);
        return;
    }
    if (WorldRPG::ServerWorld::ActorHosted(vehicleId)) return;

    {
        std::lock_guard<std::mutex> lock(g_mutex);
        VehicleState* state = StateFor(vehicleId);
        if (state) {
            state->position = s.position;
            state->rotation = s.rotation;
            state->havePose = true;
            state->derailed = s.flags.derailed;
        }
    }

    if (s.trainBlock && !s.flags.derailed) {
        DIAG("PLAYER_VEHICLE_PURESYNC veh=%u train on track, pose not applied", vehicleId);
    } else {
        WorldRPG::Game::NoteRemoteVehicle(vehicleId, s.remoteModel, s.position.x, s.position.y, s.position.z,
                                          RadiansFromDegrees(s.rotation.z));
    }

    ApplyEngineMotion(vehicleId, s.velocity, s.turnVelocity, s.vehicleHealth);

    DIAG("occupied veh id=%u by player=%u pos=%.1f %.1f %.1f rotZ=%.1f model=%d trailers=%u", vehicleId, playerId, s.position.x,
         s.position.y, s.position.z, s.rotation.z, s.remoteModel, static_cast<unsigned>(s.trailers.size()));
}

void ApplyEngineMotion(uint32_t vehicleId, const Vec3& velocity, const Vec3& turnVelocity, float health) {
    // Logging no-op. Needs GameEngine::setVehicleVelocity / setVehicleHealth with a VehicleHandle
    // for this element id, which no table here maps yet.
    DIAG("engine motion pending veh id=%u vel=%.2f %.2f %.2f turn=%.2f %.2f %.2f hp=%.1f", vehicleId, velocity.x, velocity.y,
         velocity.z, turnVelocity.x, turnVelocity.y, turnVelocity.z, health);
}

} // namespace WorldRPG::RemoteVehicle
