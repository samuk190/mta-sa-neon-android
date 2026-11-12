#include "RakNetDirectTransport.h"

#include <RakPeerInterface.h>
#include <MessageIdentifiers.h>
#include <BitStream.h>
#include <RakNetTypes.h>

#include <cstring>
#include <vector>

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

namespace WorldRPG::Net::Mta {

struct RakNetDirectTransport::Impl {
    RakNet::RakPeerInterface* peer = nullptr;
    bool started = false;
    bool connected = false;
    std::string error;
    RakNet::SystemAddress serverAddress;

    // Queue of received game packets (ID_USER_PACKET_ENUM stripped)
    struct QueuedPacket {
        std::vector<uint8_t> data;
        size_t bitLength;
    };
    std::vector<QueuedPacket> queue;
};

RakNetDirectTransport::RakNetDirectTransport() : pImpl_(std::make_unique<Impl>()) {}

RakNetDirectTransport::~RakNetDirectTransport() {
    shutdown();
}

bool RakNetDirectTransport::startup(int maxConnections, int /*threadCount*/) {
    if (pImpl_->started) return true;

    pImpl_->peer = RakNet::RakPeerInterface::GetInstance();
    if (!pImpl_->peer) {
        pImpl_->error = "Failed to create RakPeerInterface instance";
        LOGE("RakNetDirectTransport: %s", pImpl_->error.c_str());
        return false;
    }

    RakNet::SocketDescriptor socketDesc(0, 0);
    socketDesc.socketFamily = AF_INET;

    auto result = pImpl_->peer->Startup(maxConnections, &socketDesc, 1);
    if (result != RakNet::RAKNET_STARTED && result != RakNet::RAKNET_ALREADY_STARTED) {
        pImpl_->error = "RakPeer Startup failed with code " + std::to_string(result);
        LOGE("RakNetDirectTransport: %s", pImpl_->error.c_str());
        return false;
    }

    pImpl_->peer->SetOccasionalPing(true);
    pImpl_->started = true;
    LOGI("RakNetDirectTransport: startup OK (maxConn=%d)", maxConnections);
    return true;
}

bool RakNetDirectTransport::connect(const char* host, unsigned short port) {
    if (!pImpl_->started || !host || !*host || !port) return false;

    auto result = pImpl_->peer->Connect(host, port, nullptr, 0);
    if (result != RakNet::CONNECTION_ATTEMPT_STARTED) {
        pImpl_->error = "RakPeer Connect failed with code " + std::to_string(result);
        LOGE("RakNetDirectTransport: %s to %s:%u", pImpl_->error.c_str(), host, port);
        return false;
    }

    pImpl_->connected = false;
    pImpl_->queue.clear();
    LOGI("RakNetDirectTransport: connect initiated to %s:%u", host, port);
    return true;
}

bool RakNetDirectTransport::send(const unsigned char* data, int length,
                                  int priority, int reliability,
                                  int channel, std::size_t /*bitLength*/) {
    if (!pImpl_->started || !pImpl_->connected || !data || length <= 0) return false;

    // Wrap with ID_USER_PACKET_ENUM prefix as mtasa-net's CNetClient does.
    // The caller passes raw MTA game packet bytes; we prepend the RakNet
    // user packet marker so the server recognizes it as an application packet.
    RakNet::BitStream bs;
    bs.Write(static_cast<unsigned char>(ID_USER_PACKET_ENUM));
    bs.Write(reinterpret_cast<const char*>(data), static_cast<unsigned int>(length));

    uint32_t receipt = pImpl_->peer->Send(
        &bs,
        static_cast<PacketPriority>(priority),
        static_cast<PacketReliability>(reliability),
        static_cast<char>(channel),
        pImpl_->serverAddress,
        false);

    return receipt != 0;
}

int RakNetDirectTransport::receive(unsigned char* buffer, int capacity,
                                    std::size_t* bitLength) {
    if (!buffer || capacity <= 0 || !pImpl_->started) return 0;

    // Drain RakNet packets and queue game packets
    while (auto* packet = pImpl_->peer->Receive()) {
        unsigned char packetId = packet->data[0];

        switch (packetId) {
            case ID_CONNECTION_REQUEST_ACCEPTED: {
                pImpl_->connected = true;
                pImpl_->serverAddress = packet->systemAddress;
                LOGI("RakNetDirectTransport: CONNECTION_REQUEST_ACCEPTED from %s:%u",
                     packet->systemAddress.ToString(false), packet->systemAddress.GetPort());
                // Queue a synthetic connection accepted event for the wrapper
                pImpl_->queue.push_back({{14}, 8});
                break;
            }
            case ID_CONNECTION_ATTEMPT_FAILED: {
                pImpl_->error = "Connection attempt failed";
                LOGE("RakNetDirectTransport: CONNECTION_ATTEMPT_FAILED");
                pImpl_->queue.push_back({{15}, 8});
                break;
            }
            case ID_DISCONNECTION_NOTIFICATION: {
                pImpl_->connected = false;
                LOGI("RakNetDirectTransport: DISCONNECTION_NOTIFICATION");
                pImpl_->queue.push_back({{19}, 8});
                break;
            }
            case ID_CONNECTION_LOST: {
                pImpl_->connected = false;
                LOGE("RakNetDirectTransport: CONNECTION_LOST");
                pImpl_->queue.push_back({{20}, 8});
                break;
            }
            case ID_INCOMPATIBLE_PROTOCOL_VERSION: {
                pImpl_->error = "Incompatible protocol version";
                LOGE("RakNetDirectTransport: ID_INCOMPATIBLE_PROTOCOL_VERSION");
                pImpl_->queue.push_back({{95}, 8});
                break;
            }
            case ID_USER_PACKET_ENUM: {
                // Strip the ID_USER_PACKET_ENUM prefix and queue the MTA game packet body.
                // The wrapper expects raw MTA packet bytes starting with the ePacketID byte.
                if (packet->length > 1) {
                    const unsigned char* gameData = packet->data + 1;
                    size_t gameLen = packet->length - 1;
                    size_t gameBits = packet->bitSize > 8 ? packet->bitSize - 8 : 0;
                    if (static_cast<int>(gameLen) <= capacity) {
                        pImpl_->queue.push_back({
                            std::vector<uint8_t>(gameData, gameData + gameLen),
                            gameBits
                        });
                    } else {
                        LOGE("RakNetDirectTransport: game packet %zu bytes exceeds buffer %d",
                             gameLen, capacity);
                    }
                }
                break;
            }
            default:
                // Ignore other RakNet internal packets
                break;
        }

        pImpl_->peer->DeallocatePacket(packet);
    }

    // Return next queued packet
    if (pImpl_->queue.empty()) return 0;

    auto& front = pImpl_->queue.front();
    int copyLen = std::min(static_cast<int>(front.data.size()), capacity);
    std::memcpy(buffer, front.data.data(), copyLen);
    if (bitLength) *bitLength = front.bitLength ? front.bitLength : copyLen * 8;
    pImpl_->queue.erase(pImpl_->queue.begin());
    return copyLen;
}

int RakNetDirectTransport::getPing() const {
    if (!pImpl_->started || !pImpl_->connected) return 0;
    return pImpl_->peer->GetLastPing(pImpl_->serverAddress);
}

bool RakNetDirectTransport::isConnected() const {
    return pImpl_->connected;
}

void RakNetDirectTransport::shutdown() {
    if (pImpl_->peer) {
        pImpl_->peer->Shutdown(3000);
        RakNet::RakPeerInterface::DestroyInstance(pImpl_->peer);
        pImpl_->peer = nullptr;
    }
    pImpl_->started = false;
    pImpl_->connected = false;
    pImpl_->queue.clear();
}

const char* RakNetDirectTransport::lastError() const {
    return pImpl_->error.c_str();
}

} // namespace WorldRPG::Net::Mta