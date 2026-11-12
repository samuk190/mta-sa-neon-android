#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <memory>

#ifdef __ANDROID__
#include <android/log.h>
#define SYM_LOG_TAG "WorldRPGNeon"
#define SYM_LOGI(...) __android_log_print(ANDROID_LOG_INFO, SYM_LOG_TAG, __VA_ARGS__)
#define SYM_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, SYM_LOG_TAG, __VA_ARGS__)
#else
#include <cstdio>
#define SYM_LOGI(...) do { printf(__VA_ARGS__); printf("\n"); } while(0)
#define SYM_LOGE(...) do { fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } while(0)
#endif

namespace WorldRPG::Hook {

/// Resolves symbols from loaded shared libraries at runtime.
/// Parses ELF .dynsym and .dynstr sections to find function/data addresses.
class SymbolResolver {
public:
    SymbolResolver();
    ~SymbolResolver();

    SymbolResolver(const SymbolResolver&) = delete;
    SymbolResolver& operator=(const SymbolResolver&) = delete;

    /// Resolve a symbol by name from a specific library.
    /// Returns 0 if not found. Results are cached.
    uintptr_t resolve(const char* libraryName, const char* symbolName);

    /// Resolve using a pre-computed base address (avoids re-parsing maps)
    uintptr_t resolveFromBase(uintptr_t baseAddress, const char* symbolName);

    /// Clear the symbol cache for a specific library or all libraries
    void clearCache(const char* libraryName = nullptr);

    /// Get the number of cached symbols
    size_t cacheSize() const;

private:
    struct Impl;
    std::unique_ptr<Impl> pImpl_;
};

} // namespace WorldRPG::Hook