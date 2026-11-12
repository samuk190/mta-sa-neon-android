#pragma once

#include <cstdint>
#include <string>

namespace WorldRPG::Net::Vendor {

/// Thin adapter wrapping RakNet::RakPeerInterface for use by RakNetWrapper.
/// Provides a simplified synchronous API matching the SocketTransport interface
/// that was previously used, enabling drop-in replacement without changing callers.
class RakNetAdapter {
public:
    RakNetAdapter();
    ~RakNetAdapter();

    RakNetAdapter(const RakNetAdapter&) = delete;
    RakNetAdapter& operator=(const RakNetAdapter&) = delete;

    /// Initialize RakPeerInterface with maxConnections and thread count.
    /// Replaces: socket() + bind() in SocketTransport.
    bool startup(int maxConnections, int threadCount);

    /// Connect to remote server (non-blocking; returns true if connect initiated).
    /// Replaces: manual UDP handshake in SocketTransport::Connect().
    bool connect(const char* host, unsigned short port);

    /// Send data packet with priority/reliability/ordering.
    /// Replaces: sendto() in SocketTransport::SendPacket().
    bool send(const unsigned char* data, int length,
              int priority, int reliability, int orderingChannel);

    /// Receive incoming packets (non-blocking). Returns bytes received or 0.
    /// Replaces: recvfrom() in SocketTransport::DoPulse().
    int receive(unsigned char* outBuffer, int maxLength);

    /// Get average ping to connected server in milliseconds.
    /// Replaces: GetPing() stub returning 0.
    int getPing() const;

    /// Check if currently connected to a server.
    /// Replaces: IsConnected() stub.
    bool isConnected() const;

    /// Shutdown and release all RakNet resources.
    /// Replaces: close() in SocketTransport::~SocketTransport().
    void shutdown();

private:
    struct Impl;
    Impl* pImpl_;
};

} // namespace WorldRPG::Net::Vendor