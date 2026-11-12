#pragma once

#include <cstdint>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#ifdef __ANDROID__
#include <android/log.h>
#define HOOK_LOG_TAG "WorldRPGNeon"
#define HOOK_LOGI(...) __android_log_print(ANDROID_LOG_INFO, HOOK_LOG_TAG, __VA_ARGS__)
#define HOOK_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, HOOK_LOG_TAG, __VA_ARGS__)
#else
#include <cstdio>
#define HOOK_LOGI(...) do { printf(__VA_ARGS__); printf("\n"); } while(0)
#define HOOK_LOGE(...) do { fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } while(0)
#endif

namespace WorldRPG::Hook {

/// Result of a hook installation attempt
struct HookResult {
    bool success;
    uintptr_t trampolineAddress; ///< Address of original function (for calling through)
    std::string errorMessage;
};

/// RAII wrapper for memory page protection changes
class PageGuard {
public:
    PageGuard(uintptr_t address, size_t size, int newProtection);
    ~PageGuard();

    PageGuard(const PageGuard&) = delete;
    PageGuard& operator=(const PageGuard&) = delete;
    PageGuard(PageGuard&& other) noexcept;
    PageGuard& operator=(PageGuard&& other) noexcept;

    bool isValid() const { return valid_; }

private:
    uintptr_t address_;
    size_t size_;
    int originalProtection_;
    bool valid_;
};

/// Runtime memory patching utilities for ARM64/ARMv7
class MemoryPatcher {
public:
    MemoryPatcher();
    ~MemoryPatcher();

    MemoryPatcher(const MemoryPatcher&) = delete;
    MemoryPatcher& operator=(const MemoryPatcher&) = delete;

    /// Patch bytes at a specific offset from a base address
    bool patchAddress(uintptr_t baseAddr, uintptr_t offset,
                      const uint8_t* newData, size_t size);

    /// Install an inline hook at targetAddr, redirecting to hookFunc.
    /// On success, trampolineOut receives the address to call the original function.
    HookResult installHook(uintptr_t targetAddr, uintptr_t hookFunc);

    /// Uninstall a previously installed hook
    bool uninstallHook(uintptr_t targetAddr);

    /// Get the base address of a loaded shared library by name
    static uintptr_t getLibraryBaseAddress(const char* libraryName);

    /// Check if an address is in executable memory
    static bool isExecutableAddress(uintptr_t addr);

private:
    struct Impl;
    std::unique_ptr<Impl> pImpl_;
};

} // namespace WorldRPG::Hook