#include "MtaUdpSocket.h"

#include <array>
#include <cerrno>
#include <cstring>
#include <memory>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace WorldRPG::Net::Mta {
UdpSocket::~UdpSocket() { Close(); }

void UdpSocket::Close() {
    if (fd_ >= 0) ::close(fd_);
    fd_ = -1;
}

bool UdpSocket::Connect(const std::string& host, std::uint16_t port) {
    Close();
    error_.clear();
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo* raw = nullptr;
    const int result = ::getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &raw);
    if (result != 0) { error_ = ::gai_strerror(result); return false; }
    const std::unique_ptr<addrinfo, decltype(&::freeaddrinfo)> addresses(raw, ::freeaddrinfo);
    for (auto* address = addresses.get(); address; address = address->ai_next) {
        fd_ = ::socket(address->ai_family, address->ai_socktype | SOCK_CLOEXEC, address->ai_protocol);
        if (fd_ < 0) continue;
        if (::connect(fd_, address->ai_addr, address->ai_addrlen) == 0) return true;
        Close();
    }
    error_ = std::strerror(errno);
    return false;
}

bool UdpSocket::Send(std::span<const std::uint8_t> bytes) {
    if (fd_ < 0 || bytes.empty() || bytes.size() > 65507) {
        error_ = "Invalid UDP send";
        return false;
    }
    ssize_t sent;
    do { sent = ::send(fd_, bytes.data(), bytes.size(), 0); } while (sent < 0 && errno == EINTR);
    if (sent != static_cast<ssize_t>(bytes.size())) {
        error_ = sent < 0 ? std::strerror(errno) : "Incomplete UDP send";
        return false;
    }
    return true;
}

int UdpSocket::Receive(std::vector<std::uint8_t>& bytes, int timeoutMs) {
    bytes.clear();
    if (fd_ < 0 || timeoutMs < 0 || timeoutMs > 10000) { error_ = "Invalid UDP poll"; return -1; }
    pollfd descriptor{fd_, POLLIN, 0};
    int result;
    do { result = ::poll(&descriptor, 1, timeoutMs); } while (result < 0 && errno == EINTR);
    if (result == 0) return 0;
    if (result < 0) { error_ = std::strerror(errno); return -1; }
    std::array<std::uint8_t, 65536> buffer{};
    ssize_t count;
    do { count = ::recv(fd_, buffer.data(), buffer.size(), MSG_DONTWAIT | MSG_TRUNC); }
    while (count < 0 && errno == EINTR);
    if (count < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
        error_ = std::strerror(errno);
        return -1;
    }
    if (static_cast<std::size_t>(count) > buffer.size()) { error_ = "Truncated UDP datagram"; return -1; }
    bytes.assign(buffer.begin(), buffer.begin() + count);
    return 1;
}
} // namespace WorldRPG::Net::Mta
