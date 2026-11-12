#include "SymbolResolver.h"
#include "MemoryPatcher.h"

#ifdef __ANDROID__
#include <elf.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstring>
#include <cstdio>

// Android NDK <elf.h> does not define ElfW() macro; provide it for aarch64
#ifndef ElfW
#if defined(__LP64__)
#define ElfW(type) Elf64_##type
#else
#define ElfW(type) Elf32_##type
#endif
#endif
#endif

namespace WorldRPG::Hook {

struct SymbolResolver::Impl {
    // Cache: libraryName -> (symbolName -> address)
    std::unordered_map<std::string, std::unordered_map<std::string, uintptr_t>> cache;

#ifdef __ANDROID__
    /// Parse ELF dynamic symbol table from a loaded library's base address
    uintptr_t parseDynsym(uintptr_t baseAddr, const char* symbolName) {
        if (baseAddr == 0 || !symbolName) return 0;

        auto* ehdr = reinterpret_cast<const ElfW(Ehdr)*>(baseAddr);
        if (ehdr->e_ident[EI_MAG0] != ELFMAG0) return 0;

        // Find .dynsym and .dynstr via program headers
        const ElfW(Phdr)* phdr = reinterpret_cast<const ElfW(Phdr)*>(baseAddr + ehdr->e_phoff);
        uintptr_t dynAddr = 0;
        size_t dynSize = 0;

        for (int i = 0; i < ehdr->e_phnum; ++i) {
            if (phdr[i].p_type == PT_DYNAMIC) {
                dynAddr = baseAddr + phdr[i].p_vaddr;
                dynSize = phdr[i].p_memsz;
                break;
            }
        }
        if (dynAddr == 0) return 0;

        // Parse dynamic entries to find SYMTAB and STRTAB
        uintptr_t symtabAddr = 0;
        uintptr_t strtabAddr = 0;
        size_t strtabSize = 0;

        auto* dyn = reinterpret_cast<const ElfW(Dyn)*>(dynAddr);
        for (size_t i = 0; i < dynSize / sizeof(ElfW(Dyn)); ++i) {
            switch (dyn[i].d_tag) {
                case DT_SYMTAB: symtabAddr = baseAddr + dyn[i].d_un.d_ptr; break;
                case DT_STRTAB: strtabAddr = baseAddr + dyn[i].d_un.d_ptr; break;
                case DT_STRSZ:  strtabSize = dyn[i].d_un.d_val; break;
                case DT_NULL:   goto done;
            }
        }
        done:

        if (symtabAddr == 0 || strtabAddr == 0) return 0;

        // Walk symbol table until we hit STN_UNDEF or exhaust strtab
        auto* sym = reinterpret_cast<const ElfW(Sym)*>(symtabAddr);
        for (size_t idx = 0; ; ++idx) {
            if (sym[idx].st_name >= strtabSize) break;
            const char* name = reinterpret_cast<const char*>(strtabAddr + sym[idx].st_name);
            if (std::strcmp(name, symbolName) == 0 && sym[idx].st_value != 0) {
                return baseAddr + sym[idx].st_value;
            }
            if (sym[idx].st_name == 0 && idx > 0) break; // End marker
        }

        return 0;
    }
#endif
};

SymbolResolver::SymbolResolver() : pImpl_(std::make_unique<Impl>()) {}
SymbolResolver::~SymbolResolver() = default;

uintptr_t SymbolResolver::resolve(const char* libraryName, const char* symbolName) {
    if (!libraryName || !symbolName) return 0;

    std::string lib(libraryName);
    std::string sym(symbolName);

    // Check cache first
    auto libIt = pImpl_->cache.find(lib);
    if (libIt != pImpl_->cache.end()) {
        auto symIt = libIt->second.find(sym);
        if (symIt != libIt->second.end()) {
            return symIt->second;
        }
    }

#ifdef __ANDROID__
    // Try dlsym first (works for exported symbols)
    void* handle = dlopen(libraryName, RTLD_NOLOAD);
    uintptr_t addr = 0;
    if (handle) {
        void* symAddr = dlsym(handle, symbolName);
        if (symAddr) {
            addr = reinterpret_cast<uintptr_t>(symAddr);
        }
        dlclose(handle);
    }

    // Fall back to ELF parsing if dlsym failed
    if (addr == 0) {
        uintptr_t base = MemoryPatcher::getLibraryBaseAddress(libraryName);
        if (base != 0) {
            addr = pImpl_->parseDynsym(base, symbolName);
        }
    }
#else
    (void)libraryName;
    (void)symbolName;
    uintptr_t addr = 0;
#endif

    if (addr != 0) {
        pImpl_->cache[lib][sym] = addr;
        SYM_LOGI("resolve: %s::%s -> %p", libraryName, symbolName, reinterpret_cast<void*>(addr));
    } else {
        SYM_LOGE("resolve: %s::%s NOT FOUND", libraryName, symbolName);
    }

    return addr;
}

uintptr_t SymbolResolver::resolveFromBase(uintptr_t baseAddress, const char* symbolName) {
    if (baseAddress == 0 || !symbolName) return 0;

#ifdef __ANDROID__
    uintptr_t addr = pImpl_->parseDynsym(baseAddress, symbolName);
    if (addr != 0) {
        SYM_LOGI("resolveFromBase: %s -> %p (base=%p)", symbolName,
                 reinterpret_cast<void*>(addr), reinterpret_cast<void*>(baseAddress));
    }
    return addr;
#else
    (void)baseAddress;
    (void)symbolName;
    return 0;
#endif
}

void SymbolResolver::clearCache(const char* libraryName) {
    if (libraryName) {
        pImpl_->cache.erase(std::string(libraryName));
    } else {
        pImpl_->cache.clear();
    }
}

size_t SymbolResolver::cacheSize() const {
    size_t total = 0;
    for (const auto& [lib, syms] : pImpl_->cache) {
        total += syms.size();
    }
    return total;
}

} // namespace WorldRPG::Hook