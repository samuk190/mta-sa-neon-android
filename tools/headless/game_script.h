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
// What the Game:: calls of the server RPCs reached on the host, for the server RPC tests. Reset before each check.
struct ScriptProbe {
    int cameraNotes = 0;
    float camera[6] = {};
    int cameraClears = 0;
    bool timeSet = false;
    int hour = -1, minute = -1;
    bool weatherSet = false;
    int weather = -1;
    int vehicleHealthCalls = 0;
    uint32_t vehicleHealthId = 0;
    float vehicleHealth = -1.f;
    int wantedCalls = 0;
    uint8_t wantedLevel = 0;
};
ScriptProbe& Probe();
void ResetProbe();
void SetRemoteActorStreamed(uint32_t id, bool streamed);
void SetRemoteActorPose(uint32_t id, float x, float y, float z, float headingRad, float vx = 0.f, float vy = 0.f, float vz = 0.f);
void ResetRemoteActorTestState();
// Seconds since process start; the timestamp used by every headless log line.
double ElapsedSeconds();

} // namespace WorldRPG::Headless
