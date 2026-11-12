// Scripted replacement for the Android game layer in the headless client.
// The host build has no libGTASA, so gtasa/GameEngine.cpp is not compiled here;
// game_script.cpp provides the same linker surface with scripted behaviour.
#pragma once

#include <cstdint>

namespace WorldRPG::Headless {

// observer: passive, only records vehicles and logs.
// driver:   after MarkJoined() and once a drivable car is known, enters it,
//           drives forward for 20 s, exits, then fires 3 M4 shots on foot.
enum class Mode : uint8_t { Observer, Driver };

void SetMode(Mode mode);
// Called by the host once NeonJoinClient reaches JoinState::Complete.
void MarkJoined();
// The server's VEHICLE_INOUT reply for the local ped (action as on the wire, 0..9). A jack or
// attempt_failed reply to the driver's entry sends the driver back to pick another car.
void NoteServerVehicleInOut(uint32_t vehicle, uint8_t action);
// Seconds since process start; the timestamp used by every headless log line.
double ElapsedSeconds();

} // namespace WorldRPG::Headless
