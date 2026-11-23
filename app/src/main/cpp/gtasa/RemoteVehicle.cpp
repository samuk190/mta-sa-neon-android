#include "RemoteVehicle.h"

#include "GameEngine.h"
#include "../shared/ServerWorld.h"

#include <cstring>
#include <mutex>
#include <unordered_map>

namespace WorldRPG::RemoteVehicle {
namespace {

constexpr float kPi = 3.14159265f;

struct Occupant {
    uint32_t vehicleId = 0xFFFFFFFFu;
    uint8_t seat = 0;
};

std::mutex g_mutex;
std::unordered_map<uint32_t, Occupant> g_occupants;

float RadiansFromDegrees(float degrees) { return degrees * kPi / 180.f; }

} // namespace

void SetOccupant(uint32_t playerId, uint32_t vehicleId, uint8_t seat) {
    uint32_t displaced = 0;
    {
        std::lock_guard lock(g_mutex);
        const auto current = g_occupants.find(playerId);
        if (current != g_occupants.end() && current->second.vehicleId == vehicleId && current->second.seat == seat) return;
        for (auto it = g_occupants.begin(); it != g_occupants.end(); ++it) {
            if (it->second.vehicleId == vehicleId && it->second.seat == seat) {
                displaced = it->first;
                g_occupants.erase(it);
                break;
            }
        }
        g_occupants[playerId] = Occupant{vehicleId, seat};
    }
    if (displaced) Game::NoteRemoteUnseat(displaced);
}

void ClearOccupant(uint32_t playerId) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_occupants.erase(playerId);
}

bool ClearMatchingSeat(uint32_t playerId, uint32_t vehicleId, uint8_t seat) {
    std::lock_guard lock(g_mutex);
    auto it = g_occupants.find(playerId);
    if (it == g_occupants.end() || it->second.vehicleId != vehicleId || it->second.seat != seat) return false;
    g_occupants.erase(it);
    return true;
}

void ResetSession() {
    std::lock_guard<std::mutex> lock(g_mutex);
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

void ForgetElement(uint32_t id) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_occupants.erase(id);
    for (auto it = g_occupants.begin(); it != g_occupants.end();) {
        it = it->second.vehicleId == id ? g_occupants.erase(it) : std::next(it);
    }
}

bool FindSeatOccupant(uint32_t vehicleId, uint8_t seat, uint32_t& playerId) {
    std::lock_guard<std::mutex> lock(g_mutex);
    for (const auto& [id, occupant] : g_occupants) {
        if (occupant.vehicleId != vehicleId || occupant.seat != seat) continue;
        playerId = id;
        return true;
    }
    return false;
}

// CUnoccupiedVehicleSync.cpp:178-208. Read the whole packet entry before checking
// whether this vehicle or its sync time context is still current.
void ApplyUnoccupied(const VehicleSync::UnoccupiedUpdate& update) {
    if (std::strcmp(ServerWorld::ElementKindName(update.vehicleId), "vehicle") != 0) return;
    const uint8_t local = ServerWorld::SyncTimeContext(update.vehicleId);
    if (local && update.timeContext && local != update.timeContext) return;
    Game::NoteUnoccupiedVehicleSync(update);
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

    if (s.trainBlock && !s.flags.derailed) {
        DIAG("PLAYER_VEHICLE_PURESYNC veh=%u train on track, pose not applied", vehicleId);
    } else {
        WorldRPG::Game::NoteRemoteVehicle(vehicleId, s.remoteModel, s.position.x, s.position.y, s.position.z,
                                          RadiansFromDegrees(s.rotation.z));
    }

    DIAG("occupied veh id=%u by player=%u pos=%.1f %.1f %.1f rotZ=%.1f model=%d trailers=%u", vehicleId, playerId, s.position.x,
         s.position.y, s.position.z, s.rotation.z, s.remoteModel, static_cast<unsigned>(s.trailers.size()));
}

} // namespace WorldRPG::RemoteVehicle
