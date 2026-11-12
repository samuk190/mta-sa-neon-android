#pragma once
#include <cstddef>
#include <memory>

namespace WorldRPG::Net::Mta {
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
    int getPing() const;
    bool isConnected() const;
    void shutdown();
    const char* lastError() const;
private:
    struct Impl;
    std::unique_ptr<Impl> pImpl_;
};
} // namespace WorldRPG::Net::Mta
