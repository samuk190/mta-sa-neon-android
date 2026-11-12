#include "MemoryPatcher.h"

#ifdef __ANDROID__
#include <sys/mman.h>
#include <unistd.h>
#include <dlfcn.h>
#include <link.h>
#include <cstring>
#include <cerrno>
#endif

namespace WorldRPG::Hook {

// ---------------------------------------------------------------------------
// PageGuard implementation
// ---------------------------------------------------------------------------

#ifdef __ANDROID__

PageGuard::PageGuard(uintptr_t address, size_t size, int newProtection)
    : address_(address), size_(size), originalProtection_(-1), valid_(false)
{
    if (size == 0) return;

    long pageSize = sysconf(_SC_PAGESIZE);
    if (pageSize <= 0) pageSize = 4096;

    uintptr_t pageStart = address & ~(static_cast<uintptr_t>(pageSize) - 1);
    uintptr_t pageEnd = (address + size + pageSize - 1) & ~(static_cast<uintptr_t>(pageSize) - 1);
    size_t alignedSize = pageEnd - pageStart;

    // Read current protection via /proc/self/maps would be ideal, but for simplicity
    // we assume RWX was not set and restore to RX after patching
    originalProtection_ = PROT_READ | PROT_EXEC;

    if (mprotect(reinterpret_cast<void*>(pageStart), alignedSize, newProtection) == 0) {
        valid_ = true;
    } else {
        HOOK_LOGE("PageGuard: mprotect failed at %p size %zu: %s",
                  reinterpret_cast<void*>(pageStart), alignedSize, strerror(errno));
    }
}

PageGuard::~PageGuard() {
    if (valid_ && originalProtection_ >= 0) {
        long pageSize = sysconf(_SC_PAGESIZE);
        if (pageSize <= 0) pageSize = 4096;
        uintptr_t pageStart = address_ & ~(static_cast<uintptr_t>(pageSize) - 1);
        uintptr_t pageEnd = (address_ + size_ + pageSize - 1) & ~(static_cast<uintptr_t>(pageSize) - 1);
        size_t alignedSize = pageEnd - pageStart;
        mprotect(reinterpret_cast<void*>(pageStart), alignedSize, originalProtection_);
    }
}

PageGuard::PageGuard(PageGuard&& other) noexcept
    : address_(other.address_), size_(other.size_),
      originalProtection_(other.originalProtection_), valid_(other.valid_)
{
    other.valid_ = false;
}

PageGuard& PageGuard::operator=(PageGuard&& other) noexcept {
    if (this != &other) {
        if (valid_ && originalProtection_ >= 0) {
            long pageSize = sysconf(_SC_PAGESIZE);
            if (pageSize <= 0) pageSize = 4096;
            uintptr_t pageStart = address_ & ~(static_cast<uintptr_t>(pageSize) - 1);
            uintptr_t pageEnd = (address_ + size_ + pageSize - 1) & ~(static_cast<uintptr_t>(pageSize) - 1);
            mprotect(reinterpret_cast<void*>(pageStart), pageEnd - pageStart, originalProtection_);
        }
        address_ = other.address_;
        size_ = other.size_;
        originalProtection_ = other.originalProtection_;
        valid_ = other.valid_;
        other.valid_ = false;
    }
    return *this;
}

#else // Non-Android stub

PageGuard::PageGuard(uintptr_t, size_t, int) : address_(0), size_(0), originalProtection_(0), valid_(true) {}
PageGuard::~PageGuard() = default;
PageGuard::PageGuard(PageGuard&& o) noexcept : address_(o.address_), size_(o.size_), originalProtection_(o.originalProtection_), valid_(o.valid_) { o.valid_ = false; }
PageGuard& PageGuard::operator=(PageGuard&& o) noexcept { address_ = o.address_; size_ = o.size_; originalProtection_ = o.originalProtection_; valid_ = o.valid_; o.valid_ = false; return *this; }

#endif

// ---------------------------------------------------------------------------
// MemoryPatcher implementation
// ---------------------------------------------------------------------------

struct MemoryPatcher::Impl {
    struct HookEntry {
        uintptr_t targetAddress;
        uintptr_t trampolineAddress;
        uint8_t originalBytes[16];
        size_t patchSize;
    };
    std::vector<HookEntry> hooks;
};

MemoryPatcher::MemoryPatcher() : pImpl_(std::make_unique<Impl>()) {}
MemoryPatcher::~MemoryPatcher() = default;

bool MemoryPatcher::patchAddress(uintptr_t baseAddr, uintptr_t offset,
                                  const uint8_t* newData, size_t size)
{
    if (!newData || size == 0) return false;

    uintptr_t target = baseAddr + offset;

#ifdef __ANDROID__
    PageGuard guard(target, size, PROT_READ | PROT_WRITE | PROT_EXEC);
    if (!guard.isValid()) {
        HOOK_LOGE("patchAddress: failed to make memory writable at %p", reinterpret_cast<void*>(target));
        return false;
    }
    std::memcpy(reinterpret_cast<void*>(target), newData, size);
    __builtin___clear_cache(reinterpret_cast<char*>(target),
                            reinterpret_cast<char*>(target + size));
    HOOK_LOGI("patchAddress: patched %zu bytes at %p", size, reinterpret_cast<void*>(target));
    return true;
#else
    (void)target;
    HOOK_LOGI("patchAddress: stub (non-Android)");
    return false;
#endif
}

HookResult MemoryPatcher::installHook(uintptr_t targetAddr, uintptr_t hookFunc)
{
    HookResult result{};

#ifdef __ANDROID__
#if defined(__aarch64__)
    // ARM64 inline hook: replace first 16 bytes with LDR X16, #8; BR X16; <addr>
    constexpr size_t kPatchSize = 16;
    uint8_t trampolineCode[kPatchSize];

    // Save original bytes
    Impl::HookEntry entry{};
    entry.targetAddress = targetAddr;
    entry.patchSize = kPatchSize;
    std::memcpy(entry.originalBytes, reinterpret_cast<const void*>(targetAddr), kPatchSize);

    // Allocate executable trampoline (original bytes + jump back)
    // For simplicity, use mmap for trampoline
    void* trampoline = mmap(nullptr, 64, PROT_READ | PROT_WRITE | PROT_EXEC,
                            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (trampoline == MAP_FAILED) {
        result.success = false;
        result.errorMessage = "mmap failed for trampoline";
        HOOK_LOGE("installHook: %s", result.errorMessage.c_str());
        return result;
    }

    // Copy original instructions to trampoline
    std::memcpy(trampoline, entry.originalBytes, kPatchSize);

    // Add jump back to target + patchSize
    uint8_t* trampJump = static_cast<uint8_t*>(trampoline) + kPatchSize;
    uintptr_t returnAddr = targetAddr + kPatchSize;

    // LDR X16, #8; BR X16; <returnAddr>
    uint32_t ldrX16 = 0x58000050; // LDR X16, [PC, #8]
    uint32_t brX16  = 0xD61F0200; // BR X16
    std::memcpy(trampJump, &ldrX16, 4);
    std::memcpy(trampJump + 4, &brX16, 4);
    std::memcpy(trampJump + 8, &returnAddr, 8);

    entry.trampolineAddress = reinterpret_cast<uintptr_t>(trampoline);

    // Patch target: LDR X16, #8; BR X16; <hookFunc>
    PageGuard guard(targetAddr, kPatchSize, PROT_READ | PROT_WRITE | PROT_EXEC);
    if (!guard.isValid()) {
        munmap(trampoline, 64);
        result.success = false;
        result.errorMessage = "mprotect failed for hook target";
        return result;
    }

    uint32_t ldrHook = 0x58000050;
    uint32_t brHook  = 0xD61F0200;
    std::memcpy(reinterpret_cast<void*>(targetAddr), &ldrHook, 4);
    std::memcpy(reinterpret_cast<void*>(targetAddr + 4), &brHook, 4);
    std::memcpy(reinterpret_cast<void*>(targetAddr + 8), &hookFunc, 8);

    __builtin___clear_cache(reinterpret_cast<char*>(targetAddr),
                            reinterpret_cast<char*>(targetAddr + kPatchSize));
    __builtin___clear_cache(static_cast<char*>(trampoline),
                            static_cast<char*>(trampoline) + kPatchSize + 16);

    pImpl_->hooks.push_back(entry);

    result.success = true;
    result.trampolineAddress = entry.trampolineAddress;
    HOOK_LOGI("installHook: hooked %p -> %p, trampoline at %p",
              reinterpret_cast<void*>(targetAddr),
              reinterpret_cast<void*>(hookFunc),
              trampoline);

#elif defined(__arm__)
    // ARMv7 thumb/ARM hook stub — requires different trampoline logic
    result.success = false;
    result.errorMessage = "ARMv7 hooking not yet implemented";
    HOOK_LOGE("installHook: %s", result.errorMessage.c_str());
#else
    result.success = false;
    result.errorMessage = "Unsupported architecture";
#endif

#else
    (void)targetAddr; (void)hookFunc;
    result.success = false;
    result.errorMessage = "Non-Android platform";
#endif

    return result;
}

bool MemoryPatcher::uninstallHook(uintptr_t targetAddr)
{
#ifdef __ANDROID__
    for (auto it = pImpl_->hooks.begin(); it != pImpl_->hooks.end(); ++it) {
        if (it->targetAddress == targetAddr) {
            PageGuard guard(targetAddr, it->patchSize, PROT_READ | PROT_WRITE | PROT_EXEC);
            if (guard.isValid()) {
                std::memcpy(reinterpret_cast<void*>(targetAddr), it->originalBytes, it->patchSize);
                __builtin___clear_cache(reinterpret_cast<char*>(targetAddr),
                                        reinterpret_cast<char*>(targetAddr + it->patchSize));
            }
            if (it->trampolineAddress) {
                munmap(reinterpret_cast<void*>(it->trampolineAddress), 64);
            }
            pImpl_->hooks.erase(it);
            HOOK_LOGI("uninstallHook: restored %p", reinterpret_cast<void*>(targetAddr));
            return true;
        }
    }
    HOOK_LOGE("uninstallHook: no hook found at %p", reinterpret_cast<void*>(targetAddr));
    return false;
#else
    (void)targetAddr;
    return false;
#endif
}

uintptr_t MemoryPatcher::getLibraryBaseAddress(const char* libraryName)
{
#ifdef __ANDROID__
    if (!libraryName) return 0;

    FILE* maps = fopen("/proc/self/maps", "r");
    if (!maps) {
        HOOK_LOGE("getLibraryBaseAddress: cannot open /proc/self/maps");
        return 0;
    }

    char line[512];
    uintptr_t baseAddr = 0;
    while (fgets(line, sizeof(line), maps)) {
        if (std::strstr(line, libraryName) && std::strstr(line, "r-xp")) {
            // Parse start address
            char* end = nullptr;
            baseAddr = static_cast<uintptr_t>(std::strtoull(line, &end, 16));
            break;
        }
    }
    fclose(maps);

    if (baseAddr) {
        HOOK_LOGI("getLibraryBaseAddress: %s -> %p", libraryName, reinterpret_cast<void*>(baseAddr));
    }
    return baseAddr;
#else
    (void)libraryName;
    return 0;
#endif
}

bool MemoryPatcher::isExecutableAddress(uintptr_t addr)
{
#ifdef __ANDROID__
    FILE* maps = fopen("/proc/self/maps", "r");
    if (!maps) return false;

    char line[512];
    bool found = false;
    while (fgets(line, sizeof(line), maps)) {
        char* dash = std::strchr(line, '-');
        if (!dash) continue;

        char* end = nullptr;
        uintptr_t start = static_cast<uintptr_t>(std::strtoull(line, &end, 16));
        uintptr_t stop = static_cast<uintptr_t>(std::strtoull(dash + 1, &end, 16));

        if (addr >= start && addr < stop) {
            // Check permissions field (after spaces): rwxp or r-xp
            char* perms = std::strchr(line, ' ');
            while (perms && *perms == ' ') ++perms;
            if (perms && perms[2] == 'x') {
                found = true;
            }
            break;
        }
    }
    fclose(maps);
    return found;
#else
    (void)addr;
    return false;
#endif
}

} // namespace WorldRPG::Hook