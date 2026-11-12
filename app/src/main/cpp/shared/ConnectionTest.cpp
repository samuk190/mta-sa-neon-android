#include "ConnectionTest.h"

#include <thread>
#include <atomic>
#include <chrono>

namespace WorldRPG::Sync {

struct ConnectionTest::Impl {
    std::atomic<bool> running{false};
    std::atomic<bool> cancelled{false};
};

ConnectionTest::ConnectionTest() : pImpl_(std::make_unique<Impl>()) {}
ConnectionTest::~ConnectionTest() { cancel(); }

ConnectionTestResult ConnectionTest::runTest(const char* host, uint16_t port, uint32_t timeoutMs) {
    ConnectionTestResult result;
    if (!host || port == 0) {
        result.errorMessage = "Invalid host or port";
        return result;
    }

    auto startTime = std::chrono::steady_clock::now();

    Net::RakNetWrapper network;
    if (!network.StartNetwork(host, port)) {
        result.errorMessage = "StartNetwork failed";
        return result;
    }

    // Wait for connection or timeout
    auto deadline = startTime + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        network.DoPulse();
        if (network.IsConnected()) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - startTime).count();
            result.success = true;
            result.pingMs = network.GetPing();
            result.handshakeDurationMs = static_cast<uint32_t>(elapsed);
            TEST_LOGI("ConnectionTest: SUCCESS host=%s:%u ping=%dms handshake=%ums",
                      host, port, result.pingMs, result.handshakeDurationMs);
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (!result.success) {
        result.errorMessage = "Connection timed out";
        uint8_t err = network.GetConnectionError();
        if (err != 0) {
            result.errorMessage += " (error code: " + std::to_string(err) + ")";
        }
        TEST_LOGE("ConnectionTest: FAILED host=%s:%u error=%s", host, port, result.errorMessage.c_str());
    }

    network.StopNetwork();
    return result;
}

void ConnectionTest::runTestAsync(const char* host, uint16_t port, uint32_t timeoutMs,
                                   ConnectionTestCallback callback) {
    if (pImpl_->running.load()) {
        if (callback) {
            ConnectionTestResult r;
            r.errorMessage = "Another test is already running";
            callback(r);
        }
        return;
    }

    pImpl_->running.store(true);
    pImpl_->cancelled.store(false);

    std::string hostStr(host ? host : "");
    std::thread([this, hostStr, port, timeoutMs, callback]() {
        ConnectionTestResult result;
        if (!pImpl_->cancelled.load()) {
            result = runTest(hostStr.c_str(), port, timeoutMs);
        } else {
            result.errorMessage = "Test cancelled";
        }
        pImpl_->running.store(false);
        if (callback && !pImpl_->cancelled.load()) {
            callback(result);
        }
    }).detach();
}

void ConnectionTest::cancel() {
    pImpl_->cancelled.store(true);
    // Note: detached thread will check cancelled flag and exit early
}

bool ConnectionTest::isRunning() const {
    return pImpl_->running.load();
}

} // namespace WorldRPG::Sync