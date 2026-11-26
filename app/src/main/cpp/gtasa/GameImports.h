#pragma once

#ifdef __ANDROID__
#include "GameEngine.h"
#include <algorithm>
#include <array>
#include <dlfcn.h>
#include <sys/mman.h>
#include <unistd.h>

namespace WorldRPG::Game {

struct Import {
    const char* symbol;
    uintptr_t offset;
    void* replacement;
    void* original = nullptr;
    void** slot = nullptr;
};

// libGTASA 2.10 ARM64 R_AARCH64_JUMP_SLOT entries. Compare every entry with
// dlsym before changing the RELRO pages, before the GTA engine starts its threads.
template <size_t N>
bool BindImports(void* library, std::array<Import, N>& imports) {
    const long pageSize = sysconf(_SC_PAGESIZE);
    if (pageSize <= 0) return false;
    std::array<void*, N> pages{};
    size_t numPages = 0;
    for (auto& import : imports) {
        import.original = dlsym(library, import.symbol);
        Dl_info info{};
        if (!import.original || !dladdr(import.original, &info)) return false;
        const auto addr = reinterpret_cast<uintptr_t>(info.dli_fbase) + import.offset;
        import.slot = reinterpret_cast<void**>(addr);
        if (*import.slot != import.original) return false;
        auto* page = reinterpret_cast<void*>(addr & ~(static_cast<uintptr_t>(pageSize) - 1));
        if (std::find(pages.begin(), pages.begin() + numPages, page) == pages.begin() + numPages)
            pages[numPages++] = page;
    }

    size_t writable = 0;
    for (; writable < numPages; ++writable) {
        if (mprotect(pages[writable], pageSize, PROT_READ | PROT_WRITE) != 0) break;
    }
    const bool ok = writable == numPages;
    if (ok) {
        for (const auto& import : imports) __atomic_store_n(import.slot, import.replacement, __ATOMIC_RELEASE);
    }
    for (size_t i = 0; i < writable; ++i) {
        if (mprotect(pages[i], pageSize, PROT_READ) != 0)
            ENGINE_LOGE("Could not restore libGTASA GOT protection");
    }
    return ok;
}

}  // namespace WorldRPG::Game
#endif
