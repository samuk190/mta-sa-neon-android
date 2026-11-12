#include "PerformanceProfiler.h"

#include <algorithm>
#include <cstdio>

namespace WorldRPG::Debug {

PerformanceProfiler& PerformanceProfiler::instance() {
 static PerformanceProfiler profiler;
 return profiler;
}

void PerformanceProfiler::registerSection(const char* name) {
 if (!name) return;
 std::string key(name);
 if (sections_.find(key) == sections_.end()) {
 ProfileSection section;
 section.name = key;
 sections_[key] = section;
 PROF_LOGI("PerformanceProfiler: registered section '%s'", name);
 }
}

PerformanceProfiler::ScopeTimer::ScopeTimer(const char* sectionName)
 : name_(sectionName), start_(std::chrono::high_resolution_clock::now()) {}

PerformanceProfiler::ScopeTimer::~ScopeTimer() {
 auto end = std::chrono::high_resolution_clock::now();
 double ms = std::chrono::duration<double, std::milli>(end - start_).count();
 PerformanceProfiler::instance().recordSample(name_, ms);
}

void PerformanceProfiler::recordSample(const char* name, double milliseconds) {
 if (!name) return;
 std::string key(name);
 auto it = sections_.find(key);
 if (it == sections_.end()) {
 // Auto-register on first sample for convenience
 registerSection(name);
 it = sections_.find(key);
 if (it == sections_.end()) return;
 }

 auto& s = it->second;
 s.callCount++;
 s.totalMs += milliseconds;
 s.lastMs = milliseconds;
 s.minMs = std::min(s.minMs, milliseconds);
 s.maxMs = std::max(s.maxMs, milliseconds);
}

ProfileSection PerformanceProfiler::getSection(const char* name) const {
 if (!name) return {};
 auto it = sections_.find(std::string(name));
 return (it != sections_.end()) ? it->second : ProfileSection{};
}

void PerformanceProfiler::dumpAll() const {
 PROF_LOGI("=== PerformanceProfiler Dump ===");
 PROF_LOGI("%-30s %8s %10s %10s %10s %10s %s",
 "Section", "Calls", "Avg(ms)", "Min(ms)", "Max(ms)", "Last(ms)", "Status");

 for (const auto& [name, s] : sections_) {
 const char* status = "OK";
 if (s.averageMs() > warningThresholdMs_) status = "WARN";
 else if (s.maxMs > warningThresholdMs_ * 2.0) status = "SPIKE";

 PROF_LOGI("%-30s %8llu %10.3f %10.3f %10.3f %10.3f %s",
 name.c_str(),
 (unsigned long long)s.callCount,
 s.averageMs(),
 s.minMs,
 s.maxMs,
 s.lastMs,
 status);
 }
 PROF_LOGI("=== End Dump (threshold=%.2fms) ===", warningThresholdMs_);
}

void PerformanceProfiler::reset() {
 for (auto& [name, s] : sections_) {
 s.callCount = 0;
 s.totalMs = 0.0;
 s.minMs = 1e9;
 s.maxMs = 0.0;
 s.lastMs = 0.0;
 }
 PROF_LOGI("PerformanceProfiler: all sections reset");
}

void PerformanceProfiler::setWarningThreshold(double ms) {
 warningThresholdMs_ = ms;
 PROF_LOGI("PerformanceProfiler: warning threshold set to %.2fms", ms);
}

} // namespace WorldRPG::Debug