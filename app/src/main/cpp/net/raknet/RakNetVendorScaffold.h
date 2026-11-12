#pragma once

/// RakNet Vendor Integration Scaffold (Phase 16)
///
/// This header documents the exact integration points for replacing the current
/// self-contained POSIX UDP SocketTransport in RakNetWrapper.cpp with actual
/// facebookarchive/RakNet RakPeerInterface calls.
///
/// VENDOR SOURCE: https://github.com/facebookarchive/RakNet
/// LICENSE: BSD-2-Clause
/// COMPATIBILITY: ARM64 Android, CMake-native build
///
/// INTEGRATION STEPS:
/// 1. Clone facebookarchive/RakNet into app/src/main/cpp/raknet/
/// 2. Uncomment add_subdirectory(raknet) in CMakeLists.txt
/// 3. Replace SocketTransport methods below with RakPeerInterface equivalents
/// 4. Rebuild and run ConnectionTest to validate handshake compatibility
///
/// MAPPING TABLE:
/// +---------------------------+----------------------------------------------+
/// | Current SocketTransport   | RakNet Replacement                           |
/// +---------------------------+----------------------------------------------+
/// | socket() + bind()         | RakPeerInterface::Startup()                  |
/// | sendto()                  | RakPeerInterface::Send()                     |
/// | recvfrom()                | RakPeerInterface::Receive()                  |
/// | close()                   | RakPeerInterface::Shutdown()                 |
/// | connect() (UDP handshake) | RakPeerInterface::Connect()                  |
/// | Manual packet framing     | RakNet BitStream (already compatible)        |
/// | GetPing() stub            | RakPeerInterface::GetAveragePing()           |
/// | IsConnected() stub        | RakPeerInterface::GetConnectionState()       |
/// +---------------------------+----------------------------------------------+
///
/// CRITICAL NOTES:
/// - RakNet version MUST match server's RakNet version (MTA uses custom fork)
/// - Packet ID enum values must remain identical (eRakNetPackets 0-271)
/// - BitStream serialization format is already compatible (LSB-first bit packing)
/// - Thread safety: RakPeerInterface is internally threaded; remove std::mutex
///   from RakNetWrapper when switching to avoid double-locking
/// - Memory: RakNet uses its own allocator; ensure RAKNET_MALLOC/FREE macros
///   are defined before including RakNet headers on Android NDK

namespace WorldRPG::Net::Vendor {

/// Placeholder struct representing the future RakPeerInterface wrapper.
/// When RakNet sources are vendored, this becomes a thin adapter class
/// that implements the same interface as the current SocketTransport.
struct RakNetAdapter {
    /// Initialize RakPeerInterface with maxConnections and thread count
    /// Replaces: socket() + bind() in SocketTransport
    bool startup(int maxConnections, int threadCount);

    /// Connect to remote server
    /// Replaces: manual UDP handshake in SocketTransport::Connect()
    bool connect(const char* host, unsigned short port);

    /// Send data packet with priority/reliability/ordering
    /// Replaces: sendto() in SocketTransport::SendPacket()
    bool send(const unsigned char* data, int length,
              int priority, int reliability, int orderingChannel);

    /// Receive incoming packets (non-blocking)
    /// Replaces: recvfrom() in SocketTransport::DoPulse()
    int receive(unsigned char* outBuffer, int maxLength);

    /// Get average ping to connected server
    /// Replaces: GetPing() stub returning 0
    int getPing();

    /// Check connection state
    /// Replaces: IsConnected() stub
    bool isConnected();

    /// Shutdown and release all resources
    /// Replaces: close() in SocketTransport::~SocketTransport()
    void shutdown();
};

} // namespace WorldRPG::Net::Vendor