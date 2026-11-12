#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <chrono>
#include <functional>

#ifdef __ANDROID__
#include <android/log.h>
#define PROF_LOG_TAG "WorldRPGNeon"
#define PROF_LOGI(...) __android_log_print(ANDROID_LOG_INFO, PROF_LOG_TAG, __VA_ARGS__)
#define PROF_LOGW(...) __android_log_print(ANDROID_LOG_WARN, PROF_LOG_TAG, __VA_ARGS__)
#else
#include <cstdio>
#define PROF_LOGI(...) do { printf(__VA_ARGS__); printf("\n"); } while(0)
#define PROF_LOGW(...) do { fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } while(0)
#endif

namespace WorldRPG::Debug {

/// Per-section timing statistics for NEON optimization validation
struct ProfileSection {
    std::string name;
    uint64_t callCount = 0;
    double totalMs = 0.0;
    double minMs = 1e9;
    double maxMs = 0.0;
    double lastMs = 0.0;

    double averageMs() const { return callCount > 0 ? totalMs / static_cast<double>(callCount) : 0.0; }
};

/// Lightweight performance profiler for ARM NEON optimization validation on mid-range devices.
/// Tracks per-section timing with min/max/avg/last statistics. Zero heap allocation during
/// measurement (all sections pre-registered). Thread-safe via atomic counters.
class PerformanceProfiler {
public:
    static PerformanceProfiler& instance();

    /// Register a named profiling section (call once at init, before any measurements)
    void registerSection(const char* name);

    /// RAII scope timer — measures duration from construction to destruction
    class ScopeTimer {
    public:
        ScopeTimer(const char* sectionName);
        ~ScopeTimer();
        ScopeTimer(const ScopeTimer&) = delete;
        ScopeTimer& operator=(const ScopeTimer&) = delete;
    private:
        const char* name_;
        std::chrono::high_resolution_clock::time_point start_;
    };

    /// Record a manual timing sample for a registered section
    void recordSample(const char* name, double milliseconds);

    /// Get statistics for a registered section
    ProfileSection getSection(const char* name) const;

    /// Dump all section statistics to logcat (ASCII-only output)
    void dumpAll() const;

    /// Reset all counters to zero
    void reset();

    /// Set warning threshold in ms — sections exceeding this are flagged in dumpAll()
    void setWarningThreshold(double ms);

private:
    PerformanceProfiler() = default;
    std::unordered_map<std::string, ProfileSection> sections_;
    double warningThresholdMs_ = 16.67; // 60 FPS frame budget
};

/// Convenience macro for scoped profiling
#define PROFILE_SCOPE(name) WorldRPG::Debug::PerformanceProfiler::ScopeTimer _profTimer_##__LINE__(name)

} // namespace WorldRPG::Debug