#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace WorldRPG::Net::Mta {

/// Stock RakNet Connect() transport. Excluded from the APK build.
/// Official MTA net.so ignores OPEN_CONNECTION_REQUEST_1 and only answers
/// the 51-byte MTA open packet implemented by MtaClientTransport.
/// mtasa-net uses this same stock handshake and is not compatible with
/// the original MTA network module.
class RakNetDirectTransport {
public:
    RakNetDirectTransport();
    ~RakNetDirectTransport();
    RakNetDirectTransport(const RakNetDirectTransport&) = delete;
    RakNetDirectTransport& operator=(const RakNetDirectTransport&) = delete;

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