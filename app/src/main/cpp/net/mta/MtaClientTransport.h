#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>

namespace WorldRPG::Net::Mta {
// Frames that pass through send() and receive(). Reliability ACKs, the clock
// datagram, and handshake messages sent inside the transport are not included.
// Packet loss, an unsent queue, congestion, and a bandwidth cap are not tracked.
struct FrameCounters {
    std::uint64_t bytesReceived = 0;
    std::uint64_t bytesSent = 0;
    std::uint32_t packetsReceived = 0;
    std::uint32_t packetsSent = 0;
    std::uint32_t messagesInResendBuffer = 0;
};

// Single-peer native MTA transport. No RakPeer, Windows DLL or emulation.
// Called synchronously by the owning transport/game thread.
class ClientTransport {
public:
    ClientTransport();
    ~ClientTransport();
    ClientTransport(const ClientTransport&) = delete;
    ClientTransport& operator=(const ClientTransport&) = delete;
    bool startup(int maxConnections, int threadCount);
    bool connect(const char* host, unsigned short port);
    bool send(const unsigned char* data, int length, int priority, int reliability,
              int channel, std::size_t bitLength = 0);
    int receive(unsigned char* buffer, int capacity, std::size_t* bitLength = nullptr);
    FrameCounters frameCounters() const;
    int getPing() const;
    bool isConnected() const;
    void shutdown();
    const char* lastError() const;
private:
    struct Impl;
    std::unique_ptr<Impl> pImpl_;
};
} // namespace WorldRPG::Net::Mta
