#pragma once
#include <cerrno>
#include <cstdint>
#include <span>
#include <sys/syscall.h>
#include <unistd.h>
namespace WorldRPG::Net::Mta {
// The syscall is available on Android's supported kernels without requiring
// the getrandom libc symbol introduced after this APK's minimum SDK.
inline bool RandomBytes(std::span<std::uint8_t> bytes) {
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const auto count = ::syscall(SYS_getrandom,bytes.data()+offset,bytes.size()-offset,0);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return false;
        offset += count;
    }
    return true;
}
} // namespace WorldRPG::Net::Mta
