#include "GameHeap.h"
#include "GameEngine.h"
#include "../shared/DebugFlags.h"

#include <cstdint>
#include <cstring>

#ifdef __ANDROID__
#include <elf.h>
#include <link.h>
#include <malloc.h>
#include <sys/mman.h>
#include <unistd.h>
#include <cstdlib>
#include <map>
#include <mutex>
#include <new>
#endif

namespace WorldRPG::Game {

#ifdef __ANDROID__
namespace {

// 1.75 GB: every pointer difference inside the arena fits in a signed 32-bit value.
constexpr std::size_t kArenaBytes = 0x70000000u;
constexpr std::size_t kSmallArenaBytes = 0x40000000u;  // fallback when the larger reserve fails
constexpr std::uint32_t kMagic = 0x4e454f48u;          // "NEOH"
constexpr std::uint32_t kLargeClass = 0xFFFFFFFFu;
constexpr std::size_t kHeaderBytes = 16;
constexpr std::size_t kMaxSmall = 256u * 1024u;  // larger requests take whole runs
constexpr std::size_t kRunAlign = 64u * 1024u;
constexpr std::size_t kSlabBytes = 64u * 1024u;
constexpr int kMaxClasses = 64;

struct Header {
    std::uint32_t magic;
    std::uint32_t cls;       // small class index, or kLargeClass
    std::uint64_t capacity;  // user bytes for a small block; whole run bytes (header included) for a large one
};
static_assert(sizeof(Header) == kHeaderBytes, "header must keep 16-byte alignment");

std::mutex g_mutex;
unsigned char* g_base = nullptr;
std::size_t g_size = 0;
std::size_t g_bump = 0;  // offset of the first never-used byte
std::size_t g_classSize[kMaxClasses] = {};
void* g_classFree[kMaxClasses] = {};
int g_classCount = 0;
std::map<std::uintptr_t, std::size_t> g_freeRuns;  // address -> bytes, neighbours merged
std::size_t g_liveBytes = 0;
std::size_t g_fallbacks = 0;
std::size_t g_badFrees = 0;

bool InArena(const void* p) {
    const auto a = reinterpret_cast<std::uintptr_t>(p);
    const auto b = reinterpret_cast<std::uintptr_t>(g_base);
    return g_base && a >= b && a < b + g_size;
}

Header* HeaderOf(void* user) {
    return reinterpret_cast<Header*>(static_cast<unsigned char*>(user) - kHeaderBytes);
}

void BuildClasses() {
    int n = 0;
    for (std::size_t s = 16; s <= 256; s += 16) g_classSize[n++] = s;
    std::size_t s = 256;
    while (s < kMaxSmall && n < kMaxClasses - 1) {
        s = (s + s / 4 + 15) & ~static_cast<std::size_t>(15);
        if (s > kMaxSmall) s = kMaxSmall;
        g_classSize[n++] = s;
    }
    g_classCount = n;
}

int ClassFor(std::size_t bytes) {
    if (bytes <= 256) return bytes == 0 ? 0 : static_cast<int>((bytes + 15) / 16) - 1;
    int lo = 16, hi = g_classCount - 1;
    while (lo < hi) {
        const int mid = (lo + hi) / 2;
        if (g_classSize[mid] >= bytes) hi = mid;
        else lo = mid + 1;
    }
    return lo;
}

// Caller holds g_mutex. Returns nullptr when the arena is used up.
unsigned char* Bump(std::size_t bytes, std::size_t align) {
    const std::size_t start = (g_bump + align - 1) & ~(align - 1);
    if (start > g_size || bytes > g_size - start) return nullptr;
    g_bump = start + bytes;
    return g_base + start;
}

void* AllocSmallLocked(int cls) {
    if (void* head = g_classFree[cls]) {
        std::memcpy(&g_classFree[cls], head, sizeof(void*));
        return head;
    }
    const std::size_t block = kHeaderBytes + g_classSize[cls];
    const std::size_t count = block >= kSlabBytes / 4 ? 1 : kSlabBytes / block;
    unsigned char* slab = Bump(block * count, 16);
    if (!slab) return nullptr;
    // The first block is returned; the others go on the free list.
    for (std::size_t i = 1; i < count; ++i) {
        unsigned char* raw = slab + i * block;
        auto* h = reinterpret_cast<Header*>(raw);
        h->magic = kMagic;
        h->cls = static_cast<std::uint32_t>(cls);
        h->capacity = g_classSize[cls];
        void* user = raw + kHeaderBytes;
        std::memcpy(user, &g_classFree[cls], sizeof(void*));
        g_classFree[cls] = user;
    }
    auto* h = reinterpret_cast<Header*>(slab);
    h->magic = kMagic;
    h->cls = static_cast<std::uint32_t>(cls);
    h->capacity = g_classSize[cls];
    return slab + kHeaderBytes;
}

void* AllocLargeLocked(std::size_t bytes) {
    const std::size_t run = (bytes + kHeaderBytes + kRunAlign - 1) & ~(kRunAlign - 1);
    unsigned char* raw = nullptr;
    // Best fit among the free runs.
    auto best = g_freeRuns.end();
    for (auto it = g_freeRuns.begin(); it != g_freeRuns.end(); ++it) {
        if (it->second >= run && (best == g_freeRuns.end() || it->second < best->second)) best = it;
    }
    if (best != g_freeRuns.end()) {
        raw = reinterpret_cast<unsigned char*>(best->first);
        const std::size_t rest = best->second - run;
        g_freeRuns.erase(best);
        if (rest) g_freeRuns[reinterpret_cast<std::uintptr_t>(raw) + run] = rest;
    } else {
        raw = Bump(run, kRunAlign);
        if (!raw) return nullptr;
    }
    auto* h = reinterpret_cast<Header*>(raw);
    h->magic = kMagic;
    h->cls = kLargeClass;
    h->capacity = run;
    return raw + kHeaderBytes;
}

void FreeLargeLocked(unsigned char* raw, std::size_t run) {
    // Give the pages back; the address range stays reserved for the next large request.
    madvise(raw, run, MADV_DONTNEED);
    auto addr = reinterpret_cast<std::uintptr_t>(raw);
    auto next = g_freeRuns.lower_bound(addr);
    if (next != g_freeRuns.begin()) {
        auto prev = std::prev(next);
        if (prev->first + prev->second == addr) {
            addr = prev->first;
            run += prev->second;
            g_freeRuns.erase(prev);
        }
    }
    if (next != g_freeRuns.end() && addr + run == next->first) {
        run += next->second;
        g_freeRuns.erase(next);
    }
    g_freeRuns[addr] = run;
}

std::size_t UserCapacity(const Header* h) {
    return h->cls == kLargeClass ? static_cast<std::size_t>(h->capacity) - kHeaderBytes
                                 : static_cast<std::size_t>(h->capacity);
}

void* HeapMalloc(std::size_t bytes) {
    if (bytes == 0) bytes = 1;
    void* out = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        out = bytes > kMaxSmall ? AllocLargeLocked(bytes) : AllocSmallLocked(ClassFor(bytes));
        if (out) g_liveBytes += UserCapacity(HeaderOf(out));
        else ++g_fallbacks;
    }
    // Arena used up: the system heap keeps the game running (the 2 GB guarantee is lost for this block).
    return out ? out : std::malloc(bytes);
}

void HeapFree(void* p) {
    if (!p) return;
    if (!InArena(p)) {
        std::free(p);  // allocated before the heap was installed, or by a system library
        return;
    }
    Header* h = HeaderOf(p);
    std::lock_guard<std::mutex> lock(g_mutex);
    if (h->magic != kMagic) {
        ++g_badFrees;  // not a block start; leaking it is safer than corrupting a list
        return;
    }
    g_liveBytes -= UserCapacity(h);
    if (h->cls == kLargeClass) {
        const std::size_t run = static_cast<std::size_t>(h->capacity);
        h->magic = 0;
        FreeLargeLocked(reinterpret_cast<unsigned char*>(h), run);
        return;
    }
    if (h->cls >= static_cast<std::uint32_t>(g_classCount)) {
        ++g_badFrees;
        return;
    }
    std::memcpy(p, &g_classFree[h->cls], sizeof(void*));
    g_classFree[h->cls] = p;
}

void* HeapCalloc(std::size_t count, std::size_t each) {
    if (each != 0 && count > static_cast<std::size_t>(-1) / each) return nullptr;
    const std::size_t bytes = count * each;
    void* p = HeapMalloc(bytes);
    if (p) std::memset(p, 0, bytes);
    return p;
}

void* HeapRealloc(void* p, std::size_t bytes) {
    if (!p) return HeapMalloc(bytes);
    if (bytes == 0) {
        HeapFree(p);
        return nullptr;
    }
    std::size_t old = 0;
    if (InArena(p)) {
        const Header* h = HeaderOf(p);
        if (h->magic != kMagic) return nullptr;
        old = UserCapacity(h);
        // Keep the block when it still fits and is not mostly wasted.
        if (bytes <= old && (h->cls != kLargeClass || bytes > old / 2)) return p;
    } else {
        old = malloc_usable_size(p);
    }
    void* q = HeapMalloc(bytes);
    if (!q) return nullptr;
    std::memcpy(q, p, old < bytes ? old : bytes);
    HeapFree(p);
    return q;
}

char* HeapStrdup(const char* text) {
    if (!text) return nullptr;
    const std::size_t n = std::strlen(text) + 1;
    auto* out = static_cast<char*>(HeapMalloc(n));
    if (out) std::memcpy(out, text, n);
    return out;
}

void* HeapNew(std::size_t bytes) {
    void* p = HeapMalloc(bytes);
    if (!p) std::abort();  // operator new must not return null
    return p;
}

struct Replacement {
    const char* name;
    void* target;
};

const Replacement kReplacements[] = {
    {"malloc", reinterpret_cast<void*>(&HeapMalloc)},   {"calloc", reinterpret_cast<void*>(&HeapCalloc)},
    {"realloc", reinterpret_cast<void*>(&HeapRealloc)}, {"free", reinterpret_cast<void*>(&HeapFree)},
    {"strdup", reinterpret_cast<void*>(&HeapStrdup)},   {"_Znwm", reinterpret_cast<void*>(&HeapNew)},
    {"_Znam", reinterpret_cast<void*>(&HeapNew)},       {"_ZdlPv", reinterpret_cast<void*>(&HeapFree)},
    {"_ZdaPv", reinterpret_cast<void*>(&HeapFree)},
};

struct LibraryInfo {
    std::uintptr_t base = 0;
    const ElfW(Dyn)* dynamic = nullptr;
};

int FindGta(struct dl_phdr_info* info, std::size_t, void* data) {
    if (!info->dlpi_name || !std::strstr(info->dlpi_name, "libGTASA.so")) return 0;
    auto* out = static_cast<LibraryInfo*>(data);
    out->base = info->dlpi_addr;
    for (int i = 0; i < info->dlpi_phnum; ++i) {
        if (info->dlpi_phdr[i].p_type == PT_DYNAMIC)
            out->dynamic = reinterpret_cast<const ElfW(Dyn)*>(info->dlpi_addr + info->dlpi_phdr[i].p_vaddr);
    }
    return 1;
}

// Rewrites the GOT slots of one relocation table. Returns the number of slots changed.
int PatchTable(std::uintptr_t base, const ElfW(Rela)* rela, std::size_t bytes, const ElfW(Sym)* symbols,
               const char* strings) {
    const long pageLong = sysconf(_SC_PAGESIZE);
    const std::uintptr_t page = pageLong > 0 ? static_cast<std::uintptr_t>(pageLong) : 4096u;
    int changed = 0;
    for (std::size_t i = 0; i < bytes / sizeof(ElfW(Rela)); ++i) {
        const auto type = ELF64_R_TYPE(rela[i].r_info);
        if (type != R_AARCH64_JUMP_SLOT && type != R_AARCH64_GLOB_DAT) continue;
        const char* name = strings + symbols[ELF64_R_SYM(rela[i].r_info)].st_name;
        for (const Replacement& r : kReplacements) {
            if (std::strcmp(name, r.name) != 0) continue;
            auto* slot = reinterpret_cast<void**>(base + rela[i].r_offset);
            void* pageStart = reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(slot) & ~(page - 1));
            // The GOT is read-only after relocation (RELRO).
            if (mprotect(pageStart, page, PROT_READ | PROT_WRITE) != 0) continue;
            *slot = r.target;
            mprotect(pageStart, page, PROT_READ);
            ++changed;
            break;
        }
    }
    return changed;
}

}  // namespace

bool InstallGameHeap() {
    static bool done = false;
    static bool ok = false;
    if (done) return ok;
    if (WorldRPG::Debug::Flag("noheap")) {
        done = true;
        ENGINE_LOGI("GameHeap: disabled by the noheap switch");
        return false;
    }
    LibraryInfo lib;
    dl_iterate_phdr(FindGta, &lib);
    if (!lib.base || !lib.dynamic) return false;  // not the engine process, or libGTASA not loaded yet
    done = true;

    std::uintptr_t jmprel = 0, rela = 0, symtab = 0, strtab = 0;
    std::size_t jmprelBytes = 0, relaBytes = 0;
    for (const ElfW(Dyn)* d = lib.dynamic; d->d_tag != DT_NULL; ++d) {
        switch (d->d_tag) {
            case DT_JMPREL: jmprel = d->d_un.d_ptr; break;
            case DT_PLTRELSZ: jmprelBytes = d->d_un.d_val; break;
            case DT_RELA: rela = d->d_un.d_ptr; break;
            case DT_RELASZ: relaBytes = d->d_un.d_val; break;
            case DT_SYMTAB: symtab = d->d_un.d_ptr; break;
            case DT_STRTAB: strtab = d->d_un.d_ptr; break;
            default: break;
        }
    }
    if (!symtab || !strtab || (!jmprel && !rela)) {
        ENGINE_LOGE("GameHeap: libGTASA dynamic section is incomplete");
        return false;
    }
    // Bionic leaves these addresses unrelocated; a value below the load base is an offset.
    auto at = [&lib](std::uintptr_t value) { return value < lib.base ? lib.base + value : value; };

    BuildClasses();
    for (std::size_t want : {kArenaBytes, kSmallArenaBytes}) {
        void* mem = mmap(nullptr, want, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
        if (mem != MAP_FAILED) {
            g_base = static_cast<unsigned char*>(mem);
            g_size = want;
            break;
        }
    }
    if (!g_base) {
        ENGINE_LOGE("GameHeap: could not reserve the arena; libGTASA keeps the system heap");
        return false;
    }

    const auto* symbols = reinterpret_cast<const ElfW(Sym)*>(at(symtab));
    const auto* strings = reinterpret_cast<const char*>(at(strtab));
    int changed = 0;
    if (jmprel) changed += PatchTable(lib.base, reinterpret_cast<const ElfW(Rela)*>(at(jmprel)), jmprelBytes, symbols, strings);
    if (rela) changed += PatchTable(lib.base, reinterpret_cast<const ElfW(Rela)*>(at(rela)), relaBytes, symbols, strings);
    ok = changed > 0;
    ENGINE_LOGI("GameHeap: arena %p..%p (%zu MB), %d GOT slots redirected", static_cast<void*>(g_base),
                static_cast<void*>(g_base + g_size), g_size >> 20, changed);
    return ok;
}

void LogGameHeap(const char* reason) {
    std::size_t bump = 0, live = 0, runs = 0, fallbacks = 0, bad = 0;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        bump = g_bump;
        live = g_liveBytes;
        runs = g_freeRuns.size();
        fallbacks = g_fallbacks;
        bad = g_badFrees;
    }
    if (!g_base) return;
    ENGINE_LOGI("GameHeap %s: reached %zu MB of %zu MB, live %zu MB, free runs %zu, system fallbacks %zu, bad frees %zu",
                reason ? reason : "", bump >> 20, g_size >> 20, live >> 20, runs, fallbacks, bad);
}

#else
bool InstallGameHeap() { return false; }
void LogGameHeap(const char*) {}
#endif

}  // namespace WorldRPG::Game
