#include "raknet/RakNetWrapper.h"

#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include <mutex>
#include <atomic>

#ifdef __ANDROID__
#include <android/log.h>
#define LOG_TAG "WorldRPGNeon"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#else
#include <cstdio>
#define LOGI(...) do { printf(__VA_ARGS__); printf("\n"); } while(0)
#define LOGE(...) do { fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } while(0)
#endif

// ---------------------------------------------------------------------------
// CNet Adapter for MTA Neon Android
//
// This file implements the CNet interface contract defined in the MTA Neon
// desktop source (sdk/net/CNet.h) using the RakNetWrapper as the underlying
// transport. It replaces raw POSIX UDP sockets with a RakNet-compatible layer
// that handles connection handshake, packet serialization, and statistics.
//
// Design notes:
// - All heap allocations use std::make_unique / std::make_shared (no raw new/delete).
// - Strings sent to the game client are ASCII-normalized via NormalizeAscii().
// - The BitStream classes mirror MTA's NetBitStreamInterface serialization format.
// - Thread safety is handled internally by RakNetWrapper; this adapter is
//   single-threaded from the game loop perspective (DoPulse called per frame).
// ---------------------------------------------------------------------------

namespace WorldRPG::Net {

// Forward-declared types matching MTA's CNet interface
using PPACKETHANDLER = bool (*)(unsigned char, const uint8_t*, size_t);

struct PacketStat {
    int count;
    int totalBytes;
    uint64_t totalTimeUs;
};

class CNetAdapter {
public:
    CNetAdapter();
    ~CNetAdapter();

    // Non-copyable
    CNetAdapter(const CNetAdapter&) = delete;
    CNetAdapter& operator=(const CNetAdapter&) = delete;

    // --- Core lifecycle ---
    bool StartNetwork(const char* serverHost, uint16_t serverPort, bool packetTag = false);
    void StopNetwork();
    void DoPulse();
    void Shutdown();
    void Reset();

    // --- State queries ---
    bool IsReady() const;
    bool IsConnected() const;
    int GetPing() const;
    uint32_t GetTime() const;
    std::string GetLocalIP() const;
    std::string GetConnectedServer(bool includePort = false) const;
    uint8_t GetConnectionError() const;
    void SetConnectionError(uint8_t error);

    // --- Packet handling ---
    void RegisterPacketHandler(PPACKETHANDLER handler);
    bool SendPacket(uint8_t packetId, const BitStreamWriter& stream,
                    PacketPriority priority, PacketReliability reliability,
                    PacketOrdering ordering = PacketOrdering::DEFAULT);

    // --- Configuration ---
    void SetClientPort(uint16_t port);
    void SetTimeoutTime(uint32_t timeoutMs);
    void SetFakeLag(uint16_t packetLoss, uint16_t minExtraPing,
                    uint16_t extraPingVariance, int kbpsLimit);

    // --- Statistics ---
    bool GetNetworkStatistics(NetworkStatistics& stats) const;
    const PacketStat* GetPacketStats() const;

    // --- Serial / Security ---
    void GetSerial(char* buffer, size_t maxLength) const;
    bool VerifySignature(const uint8_t* data, size_t length);

    // --- MTA Handshake Helpers ---
    bool SendOpenPacket();
    bool SendJoinRequest(const std::string& nickname, const std::string& serial);

private:
    std::unique_ptr<RakNetWrapper> raknet_;
    PPACKETHANDLER packetHandler_ = nullptr;
    mutable std::mutex mutex_;
    std::array<PacketStat, 256> packetStats_{};
    std::atomic<bool> initialized_{false};
};

// ---------------------------------------------------------------------------
// Implementation
// ---------------------------------------------------------------------------

CNetAdapter::CNetAdapter()
    : raknet_(std::make_unique<RakNetWrapper>()) {
    memset(packetStats_.data(), 0, sizeof(packetStats_));
}

CNetAdapter::~CNetAdapter() {
    Shutdown();
}

bool CNetAdapter::StartNetwork(const char* serverHost, uint16_t serverPort, bool packetTag) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (initialized_.load()) {
        LOGI("CNetAdapter::StartNetwork called but already initialized");
        return true;
    }

    bool result = raknet_->StartNetwork(serverHost, serverPort, packetTag);
    if (result) {
        initialized_.store(true);
        LOGI("CNetAdapter: Network started for %s:%u", serverHost, serverPort);
    } else {
        LOGE("CNetAdapter: Failed to start network for %s:%u", serverHost, serverPort);
    }
    return result;
}

void CNetAdapter::StopNetwork() {
    std::lock_guard<std::mutex> lock(mutex_);
    raknet_->StopNetwork();
    initialized_.store(false);
    LOGI("CNetAdapter: Network stopped");
}

void CNetAdapter::DoPulse() {
    if (!initialized_.load()) return;

    raknet_->DoPulse();
}

void CNetAdapter::Shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (initialized_.load()) {
        raknet_->Shutdown();
        initialized_.store(false);
    }
}

void CNetAdapter::Reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    raknet_->Reset();
    packetHandler_ = nullptr;
    memset(packetStats_.data(), 0, sizeof(packetStats_));
    initialized_.store(false);
}

bool CNetAdapter::IsReady() const {
    return raknet_->IsReady();
}

bool CNetAdapter::IsConnected() const {
    return raknet_->IsConnected();
}

int CNetAdapter::GetPing() const {
    return raknet_->GetPing();
}

uint32_t CNetAdapter::GetTime() const {
    return raknet_->GetTime();
}

std::string CNetAdapter::GetLocalIP() const {
    return raknet_->GetLocalIP();
}

std::string CNetAdapter::GetConnectedServer(bool includePort) const {
    return raknet_->GetConnectedServer(includePort);
}

uint8_t CNetAdapter::GetConnectionError() const {
    return raknet_->GetConnectionError();
}

void CNetAdapter::SetConnectionError(uint8_t error) {
    raknet_->SetConnectionError(error);
}

void CNetAdapter::RegisterPacketHandler(PPACKETHANDLER handler) {
    std::lock_guard<std::mutex> lock(mutex_);
    packetHandler_ = handler;

    // Bridge RakNetWrapper's callback to the legacy PPACKETHANDLER signature
    raknet_->SetPacketHandler([this](uint8_t packetId, const uint8_t* data, size_t length, size_t /*bitLength*/) -> bool {
        if (packetHandler_) {
            // Update packet stats
            if (packetId < packetStats_.size()) {
                packetStats_[packetId].count++;
                packetStats_[packetId].totalBytes += static_cast<int>(length);
            }
            return packetHandler_(packetId, data, length);
        }
        return false;
    });
}

bool CNetAdapter::SendPacket(uint8_t packetId, const BitStreamWriter& stream,
                              PacketPriority priority, PacketReliability reliability,
                              PacketOrdering ordering) {
    if (!initialized_.load()) return false;
    return raknet_->SendPacket(packetId, stream, priority, reliability, ordering);
}

void CNetAdapter::SetClientPort(uint16_t port) {
    raknet_->SetClientPort(port);
}

void CNetAdapter::SetTimeoutTime(uint32_t timeoutMs) {
    raknet_->SetTimeoutTime(timeoutMs);
}

void CNetAdapter::SetFakeLag(uint16_t /*packetLoss*/, uint16_t /*minExtraPing*/,
                              uint16_t /*extraPingVariance*/, int /*kbpsLimit*/) {
    // Fake lag simulation is a debug feature; not implemented in mobile wrapper.
    // Real RakNet supports this via RakPeerInterface::ApplyNetworkSimulator.
    LOGI("CNetAdapter::SetFakeLag called (no-op on mobile)");
}

bool CNetAdapter::GetNetworkStatistics(NetworkStatistics& stats) const {
    return raknet_->GetNetworkStatistics(stats);
}

const PacketStat* CNetAdapter::GetPacketStats() const {
    return packetStats_.data();
}

void CNetAdapter::GetSerial(char* buffer, size_t maxLength) const {
    raknet_->GetSerial(buffer, maxLength);
}

bool CNetAdapter::VerifySignature(const uint8_t* data, size_t length) {
    return raknet_->VerifySerialSignature(data, length);
}

bool CNetAdapter::SendOpenPacket() {
    if (!initialized_.load()) return false;
    return raknet_->SendOpenPacket();
}

bool CNetAdapter::SendJoinRequest(const std::string& nickname, const std::string& serial) {
    if (!initialized_.load()) return false;
    return raknet_->SendJoinRequest(nickname, serial);
}

// ---------------------------------------------------------------------------
// Global singleton accessor (matches MTA's pattern of a single CNet instance)
// ---------------------------------------------------------------------------
static std::unique_ptr<CNetAdapter> g_netAdapter;
static std::once_flag g_netInitFlag;

CNetAdapter* GetCNetAdapter() {
    std::call_once(g_netInitFlag, []() {
        g_netAdapter = std::make_unique<CNetAdapter>();
    });
    return g_netAdapter.get();
}

void DestroyCNetAdapter() {
    g_netAdapter.reset();
}

} // namespace WorldRPG::Net
