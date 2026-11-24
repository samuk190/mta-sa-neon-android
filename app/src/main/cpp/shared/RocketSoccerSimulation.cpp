// Desktop CRocketSoccerSimulation.cpp (Client/mods/deathmatch/logic), the
// same calls into the RocketWeb kernel in the same order. Desktop uses
// RocketSim's RotMat and Bullet's btQuaternion only to interpolate a body
// between two ticks; that math is written out below (Bullet 3.24
// btMatrix3x3::getRotation, btQuaternion::slerp and btMatrix3x3::setRotation)
// so the Bullet library is not needed.
#include "RocketSoccerSimulation.h"

extern "C" {
#include "Kernel.h"
}

#include <zlib.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace WorldRPG::RocketSoccer {
namespace {

constexpr double Tick = 1.0 / 120.0;

void Check(NeonRocketKernel* kernel, int ok) {
    if (!ok) throw std::runtime_error(kernel && nrk_error(kernel)[0] ? nrk_error(kernel) : "Reference kernel operation failed");
}

void ReadState(NeonRocketKernel* kernel, std::array<float, 1024>& scratch, std::vector<float>& state) {
    const int count = nrk_state(kernel, scratch.data(), scratch.size());
    Check(kernel, count);
    state.assign(scratch.begin(), scratch.begin() + count);
}

struct Quat {
    float x, y, z, w;
};

// m is btMatrix3x3 rows: RotMat(forward, right, up) has those as columns
// (RotMat::operator btMatrix3x3, row-major from column-major).
using Mat3 = std::array<std::array<float, 3>, 3>;

Mat3 FromBasis(const float* forward, const float* right, const float* up) {
    Mat3 m{};
    for (int i = 0; i < 3; ++i) m[i] = {forward[i], right[i], up[i]};
    return m;
}

// btMatrix3x3::getRotation (scalar path).
Quat GetRotation(const Mat3& m) {
    float t[4];
    const float trace = m[0][0] + m[1][1] + m[2][2];
    if (trace > 0.f) {
        float s = std::sqrt(trace + 1.f);
        t[3] = s * 0.5f;
        s = 0.5f / s;
        t[0] = (m[2][1] - m[1][2]) * s;
        t[1] = (m[0][2] - m[2][0]) * s;
        t[2] = (m[1][0] - m[0][1]) * s;
    } else {
        const int i = m[0][0] < m[1][1] ? (m[1][1] < m[2][2] ? 2 : 1) : (m[0][0] < m[2][2] ? 2 : 0);
        const int j = (i + 1) % 3;
        const int k = (i + 2) % 3;
        float s = std::sqrt(m[i][i] - m[j][j] - m[k][k] + 1.f);
        t[i] = s * 0.5f;
        s = 0.5f / s;
        t[3] = (m[k][j] - m[j][k]) * s;
        t[j] = (m[j][i] + m[i][j]) * s;
        t[k] = (m[k][i] + m[i][k]) * s;
    }
    return {t[0], t[1], t[2], t[3]};
}

// btQuaternion::slerp.
Quat Slerp(const Quat& a, const Quat& b, float f) {
    const float magnitude = std::sqrt((a.x * a.x + a.y * a.y + a.z * a.z + a.w * a.w) *
                                      (b.x * b.x + b.y * b.y + b.z * b.z + b.w * b.w));
    const float product = (a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w) / magnitude;
    const float absproduct = std::fabs(product);
    constexpr float kEpsilon = 1.192092896e-07f;  // SIMD_EPSILON (FLT_EPSILON)
    if (!(absproduct < 1.f - kEpsilon)) return a;
    const float theta = std::acos(absproduct);
    const float d = std::sin(theta);
    const float sign = product < 0 ? -1.f : 1.f;
    const float s0 = std::sin((1.f - f) * theta) / d;
    const float s1 = std::sin(sign * f * theta) / d;
    return {a.x * s0 + b.x * s1, a.y * s0 + b.y * s1, a.z * s0 + b.z * s1, a.w * s0 + b.w * s1};
}

Quat Normalized(const Quat& q) {
    const float length = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    return {q.x / length, q.y / length, q.z / length, q.w / length};
}

// btMatrix3x3::setRotation (scalar path).
Mat3 FromQuat(const Quat& q) {
    const float d = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
    const float s = 2.f / d;
    const float xs = q.x * s, ys = q.y * s, zs = q.z * s;
    const float wx = q.w * xs, wy = q.w * ys, wz = q.w * zs;
    const float xx = q.x * xs, xy = q.x * ys, xz = q.x * zs;
    const float yy = q.y * ys, yz = q.y * zs, zz = q.z * zs;
    return {{{1.f - (yy + zz), xy - wz, xz + wy}, {xy + wz, 1.f - (xx + zz), yz - wx}, {xz - wy, yz + wx, 1.f - (xx + yy)}}};
}

// Desktop Interpolate: position lerp, rotation slerp, velocity from the newer tick.
Body Interpolate(const float* previous, const float* current, float fraction) {
    Body body;
    for (int i = 0; i < 3; ++i) body.position[i] = previous[i] + (current[i] - previous[i]) * fraction;
    const Quat qa = GetRotation(FromBasis(previous + 3, previous + 6, previous + 9));
    const Quat qb = GetRotation(FromBasis(current + 3, current + 6, current + 9));
    const Mat3 m = FromQuat(Normalized(Slerp(qa, qb, fraction)));
    for (int i = 0; i < 3; ++i) {
        body.forward[i] = m[i][0];
        body.right[i] = m[i][1];
        body.up[i] = m[i][2];
        body.velocity[i] = current[12 + i];
    }
    return body;
}

}  // namespace

const std::vector<Triangle>& GetArenaMesh() {
    static const auto triangles = [] {
        std::vector<Triangle> result;
        size_t size = 0;
        const unsigned char* data = nrk_collision_data(&size);
        size_t cursor = 0;
        // CMFs store indexed vertices in Bullet metres (desktop GetArenaMesh).
        auto integer = [&](size_t offset) {
            int value;
            std::memcpy(&value, data + offset, 4);
            return value;
        };
        while (cursor + 8 <= size) {
            const int count = integer(cursor), vertices = integer(cursor + 4);
            const size_t vertexStart = cursor + 8 + static_cast<size_t>(count) * 12;
            for (int i = 0; i < count; ++i) {
                Triangle triangle{};
                for (int corner = 0; corner < 3; ++corner) {
                    const int index = integer(cursor + 8 + i * 12 + corner * 4);
                    for (int axis = 0; axis < 3; ++axis) {
                        float value;
                        std::memcpy(&value, data + vertexStart + index * 12 + axis * 4, 4);
                        triangle.vertices[corner * 3 + axis] = value * 50;
                    }
                }
                result.push_back(triangle);
            }
            cursor = vertexStart + static_cast<size_t>(vertices) * 12;
        }
        return result;
    }();
    return triangles;
}

struct Simulation::Impl {
    std::unique_ptr<NeonRocketKernel, decltype(&nrk_destroy)> kernel{nrk_create(), nrk_destroy};
    std::vector<float> previous, current;
    std::array<float, 1024> stateScratch{};
    double accumulator{};
    unsigned long long ticks{};
    bool unlimited{true};
    // Camera calls mutate WASM memory too. Reconciliation uses its own world
    // so authoritative restores cannot reset the player's camera smoothing.
    std::unique_ptr<NeonRocketKernel, decltype(&nrk_destroy)> prediction{nullptr, nrk_destroy};
    std::vector<float> predictedPrevious, predictedCurrent;
};

Simulation::Simulation() : m_Impl(std::make_unique<Impl>()) {
    Check(m_Impl->kernel.get(), m_Impl->kernel != nullptr);
    Configure(false, false);
}

Simulation::~Simulation() = default;

void Simulation::Configure(bool flat, bool opponent) {
    auto& scene = *m_Impl;
    Check(scene.kernel.get(), nrk_command(scene.kernel.get(), 0, flat, opponent));
    Reset();
}

void Simulation::Reset(int kickoff) {
    auto& scene = *m_Impl;
    Check(scene.kernel.get(), nrk_command(scene.kernel.get(), 2, kickoff, 0));
    ReadState(scene.kernel.get(), scene.stateScratch, scene.current);
    scene.previous = scene.current;
    scene.accumulator = 0;
    ResetCamera();
}

void Simulation::SetControls(int car, const Controls& controls) {
    auto axis = [](float v) { return std::isfinite(v) ? std::clamp(v, -1.f, 1.f) : 0.f; };
    const float values[8] = {axis(controls.throttle), axis(controls.steer), axis(controls.pitch), axis(controls.yaw),
                             axis(controls.roll), controls.jump ? 1.f : 0.f, controls.boost ? 1.f : 0.f,
                             controls.handbrake ? 1.f : 0.f};
    Check(m_Impl->kernel.get(), nrk_controls(m_Impl->kernel.get(), car, values));
}

Snapshot Simulation::Step(float seconds, const Controls& controls) {
    auto& scene = *m_Impl;
    if (std::isfinite(seconds)) scene.accumulator += std::clamp(static_cast<double>(seconds), 0.0, 0.25);
    const int due = static_cast<int>(std::floor(scene.accumulator / Tick));
    scene.accumulator = std::max(0.0, scene.accumulator - due * Tick);
    if (due > 0) SetControls(0, controls);
    const int steps = std::min(due, 12);
    if (steps == 1) {
        scene.previous = scene.current;
        Check(scene.kernel.get(), nrk_command(scene.kernel.get(), 1, 1, 0));
        ReadState(scene.kernel.get(), scene.stateScratch, scene.current);
        ++scene.ticks;
    } else if (steps > 1) {
        // Only the final two tick states are needed for render interpolation.
        for (int i = 0; i < steps - 1; ++i) {
            Check(scene.kernel.get(), nrk_command(scene.kernel.get(), 1, 1, 0));
            ++scene.ticks;
        }
        ReadState(scene.kernel.get(), scene.stateScratch, scene.previous);
        Check(scene.kernel.get(), nrk_command(scene.kernel.get(), 1, 1, 0));
        ReadState(scene.kernel.get(), scene.stateScratch, scene.current);
        ++scene.ticks;
    }
    Snapshot result{};
    const float fraction = static_cast<float>(scene.accumulator / Tick);
    result.ball = Interpolate(scene.previous.data() + 4, scene.current.data() + 4, fraction);
    for (int i = 0; i < static_cast<int>(scene.current[2]); ++i) {
        const int offset = 22 + i * 51;
        const float* old = scene.previous.data() + offset;
        const float* now = scene.current.data() + offset;
        // A respawn must snap rather than interpolate across the whole field.
        result.cars.push_back(Interpolate(old[21] == 1 && now[21] == 0 ? now : old, now, fraction));
    }
    result.car = result.cars.at(0);
    result.grounded = scene.current[22 + 19] == 1;
    result.boosting = scene.current[22 + 23] == 1;
    result.goalTeam = static_cast<int>(scene.current[1]);
    result.ticks = scene.ticks;
    result.raw = scene.current;
    result.ballOnGround = nrk_ball_grounded(scene.kernel.get()) == 1;
    return result;
}

Snapshot Simulation::Predict(std::string_view checkpoint, const std::vector<std::array<float, 8>>& frames, int slot,
                             const std::array<float, 8>& opponent, float fraction) {
    auto& scene = *m_Impl;
    if (slot < 0 || slot > 1 || frames.size() > 120 || !std::isfinite(fraction) || fraction < 0 || fraction > 1 ||
        checkpoint.size() > 512 * 1024)
        throw std::runtime_error("Invalid Rocket prediction arguments");
    if (!scene.prediction) {
        // Kept only once fully prepared, so a failed setup is retried next call.
        std::unique_ptr<NeonRocketKernel, decltype(&nrk_destroy)> created{nrk_create(), nrk_destroy};
        auto* kernel = created.get();
        Check(kernel, kernel && nrk_command(kernel, 0, 0, 1) && nrk_command(kernel, 5, 0, 0) &&
                          nrk_command(kernel, 2, 0, 0) && nrk_checkpoint_prepare(kernel));
        ReadState(kernel, scene.stateScratch, scene.predictedCurrent);
        scene.predictedPrevious = scene.predictedCurrent;
        scene.prediction = std::move(created);
    }
    auto* kernel = scene.prediction.get();
    if (!checkpoint.empty()) {
        std::vector<unsigned char> raw(512 * 1024);
        uLongf length = static_cast<uLongf>(raw.size());
        if (uncompress(raw.data(), &length, reinterpret_cast<const Bytef*>(checkpoint.data()),
                       static_cast<uLong>(checkpoint.size())) != Z_OK ||
            !nrk_checkpoint_restore(kernel, raw.data(), length))
            throw std::runtime_error("Incompatible or corrupt Rocket prediction checkpoint");
        ReadState(kernel, scene.stateScratch, scene.predictedCurrent);
        scene.predictedPrevious = scene.predictedCurrent;
    }
    auto stepWith = [&](const std::array<float, 8>& controls) {
        Check(kernel, nrk_controls(kernel, slot, controls.data()) && nrk_controls(kernel, 1 - slot, opponent.data()) &&
                          nrk_command(kernel, 1, 1, 0));
    };
    if (frames.size() == 1) {
        scene.predictedPrevious = scene.predictedCurrent;
        stepWith(frames.front());
        ReadState(kernel, scene.stateScratch, scene.predictedCurrent);
    } else if (!frames.empty()) {
        // Reconciliation only interpolates between the last two predicted ticks.
        for (size_t i = 0; i + 1 < frames.size(); ++i) stepWith(frames[i]);
        ReadState(kernel, scene.stateScratch, scene.predictedPrevious);
        stepWith(frames.back());
        ReadState(kernel, scene.stateScratch, scene.predictedCurrent);
    }
    Snapshot result{};
    const auto& now = scene.predictedCurrent;
    const auto& old = scene.predictedPrevious;
    result.ball = Interpolate(old.data() + 4, now.data() + 4, fraction);
    for (int car = 0; car < static_cast<int>(now[2]); ++car) {
        const int offset = 22 + car * 51;
        result.cars.push_back(Interpolate(old[offset + 21] == 1 && now[offset + 21] == 0 ? now.data() + offset
                                                                                           : old.data() + offset,
                                          now.data() + offset, fraction));
    }
    result.car = result.cars.at(slot);
    result.grounded = now[22 + slot * 51 + 19] == 1;
    result.boosting = now[22 + slot * 51 + 23] == 1;
    result.goalTeam = static_cast<int>(now[1]);
    result.ticks = static_cast<unsigned long long>(now[0]);
    result.raw = now;
    result.ballOnGround = nrk_ball_grounded(kernel) == 1;
    return result;
}

std::string Simulation::Rebase(std::string_view checkpoint) {
    auto* kernel = m_Impl->prediction.get();
    if (!kernel || checkpoint.empty() || checkpoint.size() > 512 * 1024)
        throw std::runtime_error("Invalid Rocket reference checkpoint");
    std::vector<unsigned char> raw(512 * 1024);
    uLongf length = static_cast<uLongf>(raw.size());
    if (uncompress(raw.data(), &length, reinterpret_cast<const Bytef*>(checkpoint.data()),
                   static_cast<uLong>(checkpoint.size())) != Z_OK ||
        !nrk_checkpoint_rebase(kernel, raw.data(), length))
        throw std::runtime_error("Incompatible Rocket reference checkpoint");
    // Rebase leaves the predicted world untouched. The normalized empty
    // checkpoint restores that authoritative state on the next render tick.
    unsigned char compressed[64];
    uLongf size = sizeof(compressed);
    if (compress2(compressed, &size, raw.data(), 24, Z_BEST_SPEED) != Z_OK)
        throw std::runtime_error("Cannot normalize Rocket reference checkpoint");
    return {reinterpret_cast<const char*>(compressed), size};
}

void Simulation::SetUnlimitedBoost(bool value) {
    Check(m_Impl->kernel.get(), nrk_command(m_Impl->kernel.get(), 5, value, 0));
    m_Impl->unlimited = value;
}

bool Simulation::ControlBall(int action) {
    auto& scene = *m_Impl;
    if (action < 0 || action > 3 || !nrk_command(scene.kernel.get(), 4, 0, action)) return false;
    ReadState(scene.kernel.get(), scene.stateScratch, scene.current);
    std::copy_n(scene.current.begin() + 4, 18, scene.previous.begin() + 4);
    return true;
}

int Simulation::PollGoal() {
    const int goal = static_cast<int>(m_Impl->current[1]);
    Check(m_Impl->kernel.get(), nrk_command(m_Impl->kernel.get(), 3, 0, 0));
    m_Impl->current[1] = 0;
    return goal;
}

std::vector<float> Simulation::Pads() {
    std::vector<float> pads(256);
    const int count = nrk_pads(m_Impl->kernel.get(), pads.data(), pads.size());
    Check(m_Impl->kernel.get(), count);
    pads.resize(count);
    return pads;
}

std::array<double, 42> Simulation::Camera(const std::array<double, 28>& input) {
    std::array<double, 42> output{};
    Check(m_Impl->kernel.get(), nrk_camera(m_Impl->kernel.get(), input.data(), output.data()));
    return output;
}

void Simulation::ResetCamera() { Check(m_Impl->kernel.get(), nrk_command(m_Impl->kernel.get(), 6, 0, 0)); }

}  // namespace WorldRPG::RocketSoccer
