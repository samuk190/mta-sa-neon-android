#include "ConnectionLoading.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <dlfcn.h>
#include <mutex>
#include <string>

namespace WorldRPG::JniBridge {
void PostGameLine(int kind, const char* text);
}

namespace WorldRPG::ConnectionLoading {
namespace {

using Clock = std::chrono::steady_clock;

// CClientGame::PulseConnectionLoading CONNECTION_LOADING_QUIET_MS.
constexpr auto kQuiet = std::chrono::milliseconds(1500);

// Bar parts: engine, then the server phases in Phase order.
constexpr float kEngineWeight = 0.30f;
constexpr std::array<float, 4> kPhaseWeight{0.05f, 0.10f, 0.45f, 0.10f};

struct State {
    std::mutex mutex;
    bool active = false;
    std::string status;
    Phase phase = Phase::Join;
    float phaseProgress = -1.0f;            // counter of the current phase, < 0 when none
    std::array<float, 4> done{};            // completed fraction of each server phase
    float engine = 0.0f;
    float shownFill = 0.0f;                 // the bar never moves back
    Clock::time_point lastActivity{};
    unsigned generation = 0;
    // Last line sent.
    bool sentActive = false;
    unsigned sentGeneration = 0;
    std::string sentLine;
};

State& S() {
    static State state;
    return state;
}

int Index(Phase p) { return static_cast<int>(p); }

void Reset(State& s) {
    s.phase = Phase::Join;
    s.phaseProgress = -1.0f;
    s.done.fill(0.0f);
    s.engine = 0.0f;
    s.shownFill = 0.0f;
}

float Fill(State& s) {
    float fill = kEngineWeight * s.engine;
    for (int i = 0; i < 4; ++i) fill += kPhaseWeight[i] * s.done[i];
    s.shownFill = std::max(s.shownFill, std::clamp(fill, 0.0f, 1.0f));
    return s.shownFill;
}

} // namespace

void Begin(const char* status) {
    State& s = S();
    std::lock_guard<std::mutex> lock(s.mutex);
    s.lastActivity = Clock::now();
    s.status = status ? status : "";
    if (s.active) return;
    s.active = true;
    Reset(s);
    ++s.generation;
}

void Update(Phase phase, const char* status, float progress) {
    State& s = S();
    std::lock_guard<std::mutex> lock(s.mutex);
    // Resource starts also happen during play; only the connection window shows (CClientGame.cpp:3639-3641).
    if (!s.active) return;
    s.lastActivity = Clock::now();
    s.status = status ? status : "";
    if (Index(phase) > Index(s.phase)) {
        for (int i = 0; i < Index(phase); ++i) s.done[i] = 1.0f;
        s.phase = phase;
    }
    if (phase == s.phase) {
        s.phaseProgress = progress < 0.0f ? -1.0f : std::clamp(progress, 0.0f, 1.0f);
        if (s.phaseProgress >= 0.0f) s.done[Index(phase)] = std::max(s.done[Index(phase)], s.phaseProgress);
    }
}

void UpdateProgress(float progress) {
    State& s = S();
    std::lock_guard<std::mutex> lock(s.mutex);
    if (!s.active) return;
    s.lastActivity = Clock::now();
    s.phaseProgress = std::clamp(progress, 0.0f, 1.0f);
    s.done[Index(s.phase)] = std::max(s.done[Index(s.phase)], s.phaseProgress);
}

void SetEngine(float fraction) {
    State& s = S();
    std::lock_guard<std::mutex> lock(s.mutex);
    if (!s.active || fraction < 0.0f) return;
    s.engine = std::max(s.engine, std::min(fraction, 1.0f));
}

void End() {
    State& s = S();
    std::lock_guard<std::mutex> lock(s.mutex);
    s.active = false;
    s.status.clear();
}

bool Active() {
    State& s = S();
    std::lock_guard<std::mutex> lock(s.mutex);
    return s.active;
}

float ReadEngineLoad() {
    static const float* percent = [] {
        void* gta = dlopen("libGTASA.so", RTLD_NOLOAD | RTLD_LAZY);
        return gta ? static_cast<const float*>(dlsym(gta, "_ZN14CLoadingScreen15m_PercentLoadedE")) : nullptr;
    }();
    if (!percent) return -1.0f;
    const float value = *percent;
    if (!(value >= 0.0f)) return -1.0f;   // NaN guard
    return std::min(value / 100.0f, 1.0f);
}

void Pulse(bool joined, bool gameLoaded, bool transferringInitialFiles) {
    State& s = S();
    std::lock_guard<std::mutex> lock(s.mutex);
    if (!s.active || !joined || !gameLoaded) return;
    if (transferringInitialFiles) return;
    if (Clock::now() - s.lastActivity >= kQuiet) {
        s.active = false;
        s.status.clear();
    }
}

void Flush() {
    std::string line;
    {
        State& s = S();
        std::lock_guard<std::mutex> lock(s.mutex);
        if (!s.active) {
            if (!s.sentActive) return;
            s.sentActive = false;
            s.sentLine.clear();
            line = "E";
        } else {
            const float fill = Fill(s);
            // Moving segment: the share of the bar the current phase still owns. While GTA is still
            // loading and the server phase has no counter, the engine share counts too.
            float segStart = -1.0f, segEnd = -1.0f;
            if (s.phaseProgress < 0.0f) {
                const float own = kPhaseWeight[Index(s.phase)] * (1.0f - s.done[Index(s.phase)]) +
                                  kEngineWeight * (1.0f - s.engine);
                segStart = fill;
                segEnd = std::min(1.0f, fill + std::max(own, 0.02f));
            }
            const bool reopened = s.generation != s.sentGeneration || !s.sentActive;
            char head[64];
            std::snprintf(head, sizeof(head), "%s|%.4f|%.4f|%.4f|", reopened ? "B" : "U", static_cast<double>(fill),
                          static_cast<double>(segStart), static_cast<double>(segEnd));
            line = head + s.status;
            const std::string body = line.substr(1);
            if (!reopened && body == s.sentLine) return;
            s.sentActive = true;
            s.sentGeneration = s.generation;
            s.sentLine = body;
        }
    }
    WorldRPG::JniBridge::PostGameLine(kLineKind, line.c_str());
}

} // namespace WorldRPG::ConnectionLoading
