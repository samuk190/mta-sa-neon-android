#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#ifdef __ANDROID__
#include <android/log.h>
#define AC_LOG_TAG "WorldRPGNeon"
#define AC_LOGI(...) __android_log_print(ANDROID_LOG_INFO, AC_LOG_TAG, __VA_ARGS__)
#define AC_LOGW(...) __android_log_print(ANDROID_LOG_WARN, AC_LOG_TAG, __VA_ARGS__)
#define AC_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, AC_LOG_TAG, __VA_ARGS__)
#else
#include <cstdio>
#define AC_LOGI(...) do { printf(__VA_ARGS__); printf("\n"); } while(0)
#define AC_LOGW(...) do { fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } while(0)
#define AC_LOGE(...) do { fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } while(0)
#endif

namespace WorldRPG::Security {

/// Anti-cheat validation result codes
enum class ACResult : uint8_t {
    PASS = 0,       ///< All checks passed
    WARN,           ///< Suspicious but not conclusive
    FAIL,           ///< Definite cheat detected
    ERROR           ///< Validation system error
};

/// Individual check result with metadata
struct ACCheckResult {
    std::string checkName;
    ACResult result = ACResult::PASS;
    std::string detail;     ///< ASCII-only description
    uint32_t timestamp = 0; ///< Unix epoch seconds
};

/// Callback for real-time violation reporting to server
using ACViolationCallback = std::function<void(const ACCheckResult& violation)>;

/// Server-side-first anti-cheat validation interface.
/// Performs lightweight client-side integrity checks and reports anomalies
/// to the server for authoritative decision-making. Client never bans locally;
/// it only collects evidence and forwards it via the sync layer.
///
/// Design principles:
/// - Server is authoritative for all ban/kick decisions
/// - Client checks are hints, not verdicts
/// - No obfuscation or anti-debug (easily bypassed on rooted devices)
/// - Minimal performance impact (<1ms per frame budget)
class AntiCheatValidator {
public:
    AntiCheatValidator();
    ~AntiCheatValidator();

    AntiCheatValidator(const AntiCheatValidator&) = delete;
    AntiCheatValidator& operator=(const AntiCheatValidator&) = delete;

    /// Initialize validation subsystem
    bool initialize();

    /// Set callback for violation reporting
    void setViolationCallback(ACViolationCallback callback);

    /// Run all registered checks (call once per frame or on timer)
    /// Returns aggregated result; individual results available via getLastResults()
    ACResult runAllChecks();

    /// Run a specific named check
    ACCheckResult runCheck(const char* checkName);

    /// Get results from the last runAllChecks() call
    std::vector<ACCheckResult> getLastResults() const;

    /// Register a custom validation check
    using CheckFn = std::function<ACCheckResult()>;
    void registerCheck(const char* name, CheckFn fn);

    /// Get list of registered check names
    std::vector<std::string> getRegisteredChecks() const;

    /// Report a server-received violation acknowledgment
    /// (server confirms it processed our report)
    void acknowledgeServerReport(uint32_t reportId);

    /// Shutdown and release resources
    void shutdown();

private:
    struct Impl;
    std::unique_ptr<Impl> pImpl_;
};

} // namespace WorldRPG::Security