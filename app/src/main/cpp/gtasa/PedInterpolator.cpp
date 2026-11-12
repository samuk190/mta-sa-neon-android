#include "PedInterpolator.h"

#include <algorithm>
#include <cmath>

namespace WorldRPG::Game::Interp {
namespace {

constexpr float kPi = 3.14159265f;
// CClientPed.cpp:57-58 (PED_INTERPOLATION_WARP_THRESHOLD*). TICK_RATE/100 is 1 at iPureSync=100.
constexpr float kWarpThreshold = 5.f;
constexpr float kWarpThresholdPerSpeed = 5.f;

// SharedUtil::Unlerp: (pos - from) / (to - from), 1 when from == to.
float Unlerp(uint64_t from, uint64_t pos, uint64_t to) {
    if (from == to) return 1.f;
    return static_cast<float>(static_cast<double>(static_cast<int64_t>(pos) - static_cast<int64_t>(from)) /
                              static_cast<double>(static_cast<int64_t>(to) - static_cast<int64_t>(from)));
}

Vec3 Sub(const Vec3& a, const Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 Add(const Vec3& a, const Vec3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 Scale(const Vec3& a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float Length(const Vec3& a) { return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z); }

} // namespace

float OffsetRadians(float a, float b) {
    float c = (b > a) ? b - a : 0.0f - (a - b);
    if (c > kPi)
        c = 0.0f - (2 * kPi - c);
    else if (c <= -kPi)
        c = (2 * kPi + c);
    return c;
}

void BeginPosition(PositionInterp& interp, const Vec3& current, const Vec3& target,
    uint64_t delayMs, uint64_t nowMs) {
    interp.target = target;
    interp.error = Sub(target, current);
    interp.startMs = nowMs;
    interp.finishMs = nowMs + delayMs;
    interp.lastAlpha = 0.f;
}

Vec3 StepPosition(PositionInterp& interp, const Vec3& current, float planarSpeed, uint64_t nowMs) {
    if (!interp.active()) return current;

    float alpha = Unlerp(interp.startMs, nowMs, interp.finishMs);
    // Don't let it overcompensate the error.
    alpha = std::clamp(alpha, 0.f, 1.f);

    // Only the increment since the last frame is applied, so the error is spread over the window.
    const float currentAlpha = alpha - interp.lastAlpha;
    interp.lastAlpha = alpha;
    const Vec3 compensation = Scale(interp.error, currentAlpha);

    if (alpha == 1.f) interp.finishMs = 0;

    Vec3 next = Add(current, compensation);

    // Warp check. Written as !(<=) so NaN values also abort the interpolation.
    const float threshold = kWarpThreshold + kWarpThresholdPerSpeed * planarSpeed;
    if (!(Length(Sub(current, interp.target)) <= threshold)) {
        interp.finishMs = 0;
        next = interp.target;
    }
    return next;
}

void BeginRotation(RotationInterp& interp, float current, float target,
    uint64_t delayMs, uint64_t nowMs) {
    interp.beginMs = nowMs;
    interp.endMs = nowMs + delayMs;
    interp.begin = current;
    interp.target = target;
}

bool StepRotation(RotationInterp& interp, float& heading, uint64_t nowMs) {
    if (!interp.active()) return false;

    if (nowMs < interp.endMs) {
        const float delta = OffsetRadians(interp.begin, interp.target);
        // Wrap-around edge: jump straight to the target, as the reference does.
        if (delta < -kPi || delta > kPi) {
            heading = interp.target;
            return true;
        }
        const float deltaTime = static_cast<float>(interp.endMs - interp.beginMs);
        const float progress = static_cast<float>(nowMs - interp.beginMs);
        heading = interp.begin + delta * (progress / deltaTime);
        return true;
    }

    heading = interp.target;
    interp.beginMs = 0;
    return true;
}

} // namespace WorldRPG::Game::Interp
