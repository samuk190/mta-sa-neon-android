#pragma once

#include "../net/raknet/RakNetWrapper.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <chrono>

#ifdef __ANDROID__
#include <android/log.h>
#define TEST_LOG_TAG "WorldRPGNeon"
#define TEST_LOGI(...) __android_log_print(ANDROID_LOG_INFO, TEST_LOG_TAG, __VA_ARGS__)
#define TEST_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TEST_LOG_TAG, __VA_ARGS__)
#else
#include <cstdio>
#define TEST_LOGI(...) do { printf(__VA_ARGS__); printf("\n"); } while(0)
#define TEST_LOGE(...) do { fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } while(0)
#endif

namespace WorldRPG::Sync {

/// Result of a server connectivity test
struct ConnectionTestResult {
    bool success = false;
    int pingMs = -1;
    std::string errorMessage;
    uint32_t handshakeDurationMs = 0;
    bool protocolCompatible = true;
};

/// Callback for async test completion
using ConnectionTestCallback = std::function<void(const ConnectionTestResult&)>;

/// Server compatibility and connectivity test harness.
/// Validates RakNet handshake, protocol version, and basic packet exchange
/// against the running WorldRPG MTA Neon server before full game session.
class ConnectionTest {
public:
    ConnectionTest();
    ~ConnectionTest();

    ConnectionTest(const ConnectionTest&) = delete;
    ConnectionTest& operator=(const ConnectionTest&) = delete;

    /// Run a synchronous connectivity test (blocks until timeout or result)
    /// @param host Server hostname or IP
    /// @param port Server UDP port (typically 22003)
    /// @param timeoutMs Maximum wait time in milliseconds
    ConnectionTestResult runTest(const char* host, uint16_t port, uint32_t timeoutMs = 5000);

    /// Run an asynchronous test with callback (non-blocking)
    void runTestAsync(const char* host, uint16_t port, uint32_t timeoutMs,
                      ConnectionTestCallback callback);

    /// Cancel any running async test
    void cancel();

    /// Check if an async test is currently running
    bool isRunning() const;

private:
    struct Impl;
    std::unique_ptr<Impl> pImpl_;
};

} // namespace WorldRPG::Sync