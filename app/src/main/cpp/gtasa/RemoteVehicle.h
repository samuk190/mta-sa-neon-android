#pragma once

// Applies decoded PLAYER_VEHICLE_PURESYNC (33) and UNOCCUPIED_VEHICLE_SYNC (56) state to remote vehicles.
// Pose goes through WorldRPG::Game::NoteRemoteVehicle (the same path as VEHICLE_START, S3/S4).
// Velocity, turn velocity and health are kept per vehicle here; see ApplyEngineMotion.

#include <cstdint>

#include "../shared/VehicleSync.h"

namespace WorldRPG::RemoteVehicle {

// Packet 33 names only the player. Occupancy (vehicle and seat) must come from the entity that
// owns it (the ped body in ENTITY_ADD, or a vehicle enter event). Until a caller sets it, the
// packet is ignored, which matches the reference (CNetAPI.cpp 1306: no occupied vehicle, no read).
void SetOccupant(uint32_t playerId, uint32_t vehicleId, uint8_t seat);
void ClearOccupant(uint32_t playerId);
bool FindOccupant(uint32_t playerId, uint32_t& vehicleId, uint8_t& seat);

// Packet 56, one entry. Skips vehicles this client hosts (ServerWorld::ActorHosted).
void ApplyUnoccupied(const VehicleSync::UnoccupiedUpdate& update);

// Packet 33, one player. The caller passes the seat it resolved with FindOccupant.
void ApplyOccupied(uint32_t playerId, uint32_t vehicleId, uint8_t seat, const VehicleSync::OccupiedSync& sync);

// Velocity, turn velocity and health for one vehicle. Not yet applied to the GTA body:
// GameEngine keeps remote cars keyed by element id with no VehicleHandle, so this is a logging
// no-op for now (see RemoteVehicle.cpp).
void ApplyEngineMotion(uint32_t vehicleId, const VehicleSync::Vec3& velocity, const VehicleSync::Vec3& turnVelocity, float health);

} // namespace WorldRPG::RemoteVehicle
