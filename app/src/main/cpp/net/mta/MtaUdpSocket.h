#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace WorldRPG::Net::Mta {

// Connected UDP restricts received datagrams to this peer in the kernel.
// The owner polls it on the transport thread, never Android's UI thread.
class UdpSocket {
public:
    UdpSocket() = default;
    ~UdpSocket();
    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;
    bool Connect(const std::string& host, std::uint16_t port);
    bool Send(std::span<const std::uint8_t> bytes);
    // 1 = datagram, 0 = timeout, -1 = socket error. Truncation is an error.
    int Receive(std::vector<std::uint8_t>& bytes, int timeoutMs);
    void Close();
    const std::string& Error() const { return error_; }
private:
    int fd_ = -1;
    std::string error_;
};
} // namespace WorldRPG::Net::Mta
