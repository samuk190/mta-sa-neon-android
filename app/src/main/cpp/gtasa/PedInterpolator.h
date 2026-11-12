#pragma once

// Time-based observer interpolation for remote peds (stage S4).
//
// Pure math, no GTA calls. Mirrors the reference client:
//   position: CClientPed::SetTargetPosition (CClientPed.cpp:8657-8708),
//             CClientPed::UpdateTargetPosition (CClientPed.cpp:8765-8822),
//             warp threshold constants (CClientPed.cpp:57-58).
//   rotation: CClientPed::SetTargetRotation (CClientPed.cpp:5342-5364),
//             CClientPed::Interpolate rotation branch (CClientPed.cpp:5441-5520).
//   angle:    GetOffsetRadians (Shared/mods/deathmatch/logic/Utils.h:237).
// Times are milliseconds on a monotonic clock. A finish/end time of 0 means
// "inactive", as in the reference.

#include <cstdint>

namespace WorldRPG::Game::Interp {

struct Vec3 {
    float x = 0.f;
    float y = 0.f;
    float z = 0.f;
};

// Mirrors CClientPed::m_interp.pos.
struct PositionInterp {
    Vec3 target{};
    Vec3 error{};          // target - position at the moment the target arrived
    uint64_t startMs = 0;
    uint64_t finishMs = 0; // 0 = no interpolation running
    float lastAlpha = 0.f;

    bool active() const { return finishMs != 0; }
};

// Mirrors CClientPed::m_ulBeginRotationTime / m_ulEndRotationTime / m_fBeginRotation / m_fTargetRotationA.
struct RotationInterp {
    float begin = 0.f;
    float target = 0.f;
    uint64_t beginMs = 0;
    uint64_t endMs = 0;  // 0 = no interpolation running

    bool active() const { return beginMs != 0; }
};

// Signed shortest angular offset from a to b, in (-pi, pi]. Utils.h:237.
float OffsetRadians(float a, float b);

// CClientPed::SetTargetPosition for a streamed-in ped: store the target,
// the error against the current position and the [now, now + delayMs] window.
void BeginPosition(PositionInterp& interp, const Vec3& current, const Vec3& target,
    uint64_t delayMs, uint64_t nowMs);

// CClientPed::UpdateTargetPosition: returns the position to show this frame.
// `planarSpeed` is the ped's move speed length, used by the warp threshold.
// A jump larger than the threshold aborts the interpolation and snaps to target.
Vec3 StepPosition(PositionInterp& interp, const Vec3& current, float planarSpeed, uint64_t nowMs);

// CClientPed::SetTargetRotation(delay, rotation, ...): begin from the current heading.
void BeginRotation(RotationInterp& interp, float current, float target,
    uint64_t delayMs, uint64_t nowMs);

// CClientPed::Interpolate rotation branch. Writes the heading for this frame and
// returns true while an interpolation is active (or just finished).
bool StepRotation(RotationInterp& interp, float& heading, uint64_t nowMs);

} // namespace WorldRPG::Game::Interp
