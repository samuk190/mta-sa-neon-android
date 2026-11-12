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

bool FindOccupant(uint32_t playerId, uint32_t& vehicleId, uint8_t& seat) {
    std::lock_guard<std::mutex> lock(g_mutex);
    auto found = g_occupants.find(playerId);
    if (found == g_occupants.end()) return false;
    vehicleId = found->second.vehicleId;
    seat = found->second.seat;
    return true;
}

void ApplyUnoccupied(const VehicleSync::UnoccupiedUpdate& u) {
    if (u.vehicleId == 0 || u.vehicleId == 0xFFFFFFFFu) {
        DIAG("UNOCCUPIED_VEHICLE_SYNC invalid vehicle id=%u", u.vehicleId);
        return;
    }
    if (WorldRPG::ServerWorld::ActorHosted(u.vehicleId)) return;

    const bool train = IsTrainId(u.vehicleId);
    bool derailed = false;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        VehicleState* state = StateFor(u.vehicleId);
        if (!state) return;
        if (u.syncPosition) {
            state->position = u.position;
            state->havePose = true;
        }
        if (u.syncRotation) state->rotation = u.rotation;
        if (u.syncPosition || u.syncRotation) state->derailed = u.derailed;
        derailed = state->derailed;
    }

    // Trains move on tracks; the reference only uses the target pose for trains when derailed.
    const bool poseAllowed = !train || derailed;
    if (poseAllowed && (u.syncPosition || u.syncRotation)) {
        VehicleState snapshot;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            snapshot = g_vehicles[u.vehicleId];
        }
        if (snapshot.havePose) {
            WorldRPG::Game::NoteRemoteVehicle(u.vehicleId, 0, snapshot.position.x, snapshot.position.y, snapshot.position.z,
                                              RadiansFromDegrees(snapshot.rotation.z));
        }
    }

    if (u.syncVelocity || u.syncTurnVelocity || u.syncHealth) {
        ApplyEngineMotion(u.vehicleId, u.velocity, u.turnVelocity, u.health);
    }

    DIAG("unoccupied veh id=%u pos=%.1f %.1f %.1f rotZ=%.1f%s%s", u.vehicleId, u.position.x, u.position.y, u.position.z, u.rotation.z,
         u.syncPosition ? "" : " (no pos)", train ? " (train)" : "");
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
