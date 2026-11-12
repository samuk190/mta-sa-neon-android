#include "RakNetAdapter.h"

#ifdef __ANDROID__
#include <android/log.h>
#define ADAPTER_LOG_TAG "WorldRPGNeon"
#define ADAPTER_LOGI(...) __android_log_print(ANDROID_LOG_INFO, ADAPTER_LOG_TAG, __VA_ARGS__)
#define ADAPTER_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, ADAPTER_LOG_TAG, __VA_ARGS__)
#else
#include <cstdio>
#define ADAPTER_LOGI(...) do { printf(__VA_ARGS__); printf("\n"); } while(0)
#define ADAPTER_LOGE(...) do { fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } while(0)
#endif

#include "RakPeerInterface.h"
#include "MessageIdentifiers.h"
#include "BitStream.h"
#include "RakNetTypes.h"

namespace WorldRPG::Net::Vendor {

struct RakNetAdapter::Impl {
    RakNet::RakPeerInterface* peer = nullptr;
    bool started = false;
    bool connected = false;
};

RakNetAdapter::RakNetAdapter() : pImpl_(new Impl()) {}

RakNetAdapter::~RakNetAdapter() {
    shutdown();
    delete pImpl_;
}

bool RakNetAdapter::startup(int maxConnections, int /*threadCount*/) {
    if (pImpl_->started) return true;

    pImpl_->peer = RakNet::RakPeerInterface::GetInstance();
    if (!pImpl_->peer) {
        ADAPTER_LOGE("RakNetAdapter: failed to get RakPeerInterface instance");
        return false;
    }

    RakNet::SocketDescriptor sd(0, 0); // Bind to any available port
    RakNet::StartupResult result = pImpl_->peer->Startup(
        static_cast<unsigned int>(maxConnections), &sd, 1);

    if (result != RakNet::RAKNET_STARTED) {
        ADAPTER_LOGE("RakNetAdapter: Startup failed with code %d", static_cast<int>(result));
        RakNet::RakPeerInterface::DestroyInstance(pImpl_->peer);
        pImpl_->peer = nullptr;
        return false;
    }

    pImpl_->started = true;
    ADAPTER_LOGI("RakNetAdapter: started successfully (maxConn=%d)", maxConnections);
    return true;
}

bool RakNetAdapter::connect(const char* host, unsigned short port) {
    if (!pImpl_->peer || !host) return false;

    RakNet::ConnectionAttemptResult result = pImpl_->peer->Connect(
        host, port, nullptr, 0);

    if (result != RakNet::CONNECTION_ATTEMPT_STARTED) {
        ADAPTER_LOGE("RakNetAdapter: Connect to %s:%u failed with code %d",
                     host, port, static_cast<int>(result));
        return false;
    }

    pImpl_->connected = true;
    ADAPTER_LOGI("RakNetAdapter: connect initiated to %s:%u", host, port);
    return true;
}

bool RakNetAdapter::send(const unsigned char* data, int length,
                         int priority, int reliability, int orderingChannel) {
    if (!pImpl_->peer || !data || length <= 0) return false;

    RakNet::BitStream bs(const_cast<unsigned char*>(data), length, false);
    uint32_t receipt = pImpl_->peer->Send(
        &bs,
        static_cast<PacketPriority>(priority),
        static_cast<PacketReliability>(reliability),
        static_cast<char>(orderingChannel),
        RakNet::UNASSIGNED_SYSTEM_ADDRESS,
        true);

    return receipt != 0;
}

int RakNetAdapter::receive(unsigned char* outBuffer, int maxLength) {
    if (!pImpl_->peer || !outBuffer || maxLength <= 0) return 0;

    RakNet::Packet* packet = pImpl_->peer->Receive();
    if (!packet) return 0;

    int copyLen = static_cast<int>(packet->length);
    if (copyLen > maxLength) {
        ADAPTER_LOGE("RakNetAdapter: packet truncated %u -> %d bytes (bitSize=%u lost)",
                     packet->length, maxLength, packet->bitSize);
        copyLen = maxLength;
    }

    memcpy(outBuffer, packet->data, copyLen);
    pImpl_->peer->DeallocatePacket(packet);

    return copyLen;
}

int RakNetAdapter::getPing() const {
    if (!pImpl_->peer) return 0;

    RakNet::SystemAddress addr = pImpl_->peer->GetSystemAddressFromGuid(
        pImpl_->peer->GetMyGUID());

    // GetAveragePing returns -1 if not connected
    int ping = pImpl_->peer->GetAveragePing(addr);
    return ping >= 0 ? ping : 0;
}

bool RakNetAdapter::isConnected() const {
    if (!pImpl_->peer) return false;
    return pImpl_->peer->NumberOfConnections() > 0;
}

void RakNetAdapter::shutdown() {
    if (pImpl_->peer) {
        pImpl_->peer->Shutdown(300);
        RakNet::RakPeerInterface::DestroyInstance(pImpl_->peer);
        pImpl_->peer = nullptr;
    }
    pImpl_->started = false;
    pImpl_->connected = false;
}

} // namespace WorldRPG::Net::Vendor