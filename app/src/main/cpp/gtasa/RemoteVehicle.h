#pragma once

// Applies decoded PLAYER_VEHICLE_PURESYNC (33) and UNOCCUPIED_VEHICLE_SYNC (56) state to remote vehicles.
// GTA bodies are updated through GameEngine on the game thread.

#include <cstdint>

#include "../shared/VehicleSync.h"

namespace WorldRPG::RemoteVehicle {

// Packet 33 names only the player. Occupancy (vehicle and seat) must come from the entity that
// owns it (the ped body in ENTITY_ADD, or a vehicle enter event). Until a caller sets it, the
// packet is ignored, which matches the reference (CNetAPI.cpp 1306: no occupied vehicle, no read).
void SetOccupant(uint32_t playerId, uint32_t vehicleId, uint8_t seat);
void ClearOccupant(uint32_t playerId);
bool ClearMatchingSeat(uint32_t playerId, uint32_t vehicleId, uint8_t seat);
bool FindOccupant(uint32_t playerId, uint32_t& vehicleId, uint8_t& seat);
// An element left the session (PLAYER_QUIT or ENTITY_REMOVE): drops its own seat, and every seat of
// it when it is a vehicle. A reused ElementID then starts with no occupancy (desktop: the
// CClientPed / CClientVehicle destructors clear both sides).
void ForgetElement(uint32_t id);
// The ped stored in this seat of the vehicle (desktop CClientVehicle::GetOccupant). Read only.
bool FindSeatOccupant(uint32_t vehicleId, uint8_t seat, uint32_t& playerId);

// Reconnect: drops every stored occupant of the old session.
void ResetSession();

// Packet 56, one entry. Validates the vehicle and its sync time context.
void ApplyUnoccupied(const VehicleSync::UnoccupiedUpdate& update);

// Packet 33, one player. The caller passes the seat it resolved with FindOccupant.
void ApplyOccupied(uint32_t playerId, uint32_t vehicleId, uint8_t seat, const VehicleSync::OccupiedSync& sync);

} // namespace WorldRPG::RemoteVehicle
