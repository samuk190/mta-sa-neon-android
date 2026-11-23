#pragma once
// 32-bit guest address space for the emulated netc.dll.
//
// One host reservation backs guest addresses [0, 2 GiB). Unicorn maps it with
// uc_mem_map_ptr, so guest address A is host byte arena_[A]. The first 64 KiB
// stay unmapped to catch null pointers.
//
// Guest map:
//   0x00010000 .. 0x10000000  page allocator (thread stacks, VirtualAlloc low)
//   0x10000000 .. 0x20000000  PE images (netc prefers 0x10000000)
//   0x20000000 .. 0x70000000  page allocator (heap chunks, VirtualAlloc)
//   0x70000000 .. 0x70800000  static data: GDT, PEB, TEBs, fake objects
//   0x7FF00000 .. 0x7FF40000  shim thunks (code hook range)
//   0x7FFE0000                KUSER_SHARED_DATA

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace NetEmu {

namespace Map {
constexpr uint32_t kNullGuard = 0x00010000u;
constexpr uint32_t kLowPagesBegin = 0x00100000u;
constexpr uint32_t kLowPagesEnd = 0x10000000u;
constexpr uint32_t kImageBegin = 0x10000000u;
constexpr uint32_t kImageEnd = 0x20000000u;
constexpr uint32_t kPagesBegin = 0x20000000u;
constexpr uint32_t kPagesEnd = 0x70000000u;
constexpr uint32_t kStaticBegin = 0x70000000u;
constexpr uint32_t kStaticEnd = 0x70800000u;
constexpr uint32_t kGdt = kStaticBegin;           // 64 KiB, 8192 descriptors
constexpr uint32_t kStaticBump = kStaticBegin + 0x10000u;
constexpr uint32_t kThunkBegin = 0x7FF00000u;
constexpr uint32_t kThunkEnd = 0x7FF40000u;
constexpr uint32_t kThunkStride = 16;
constexpr uint32_t kSharedUserData = 0x7FFE0000u;
constexpr uint32_t kArenaSize = 0x80000000u;
constexpr uint32_t kPage = 0x1000u;
}  // namespace Map

class GuestMemory {
public:
    GuestMemory() = default;
    ~GuestMemory();
    GuestMemory(const GuestMemory&) = delete;
    GuestMemory& operator=(const GuestMemory&) = delete;

    bool Init(std::string* error);
    uint8_t* Arena() const { return arena_; }

    // Host pointer for [va, va+len) or nullptr when the range leaves the arena
    // or touches the null guard.
    uint8_t* Host(uint32_t va, size_t len = 1) const;
    bool Valid(uint32_t va, size_t len = 1) const { return Host(va, len) != nullptr; }

    uint32_t Read32(uint32_t va) const;
    uint16_t Read16(uint32_t va) const;
    uint8_t Read8(uint32_t va) const;
    uint64_t Read64(uint32_t va) const;
    bool Write32(uint32_t va, uint32_t v);
    bool Write16(uint32_t va, uint16_t v);
    bool Write8(uint32_t va, uint8_t v);
    bool Write64(uint32_t va, uint64_t v);
    bool ReadBytes(uint32_t va, void* out, size_t len) const;
    bool WriteBytes(uint32_t va, const void* in, size_t len);
    bool Fill(uint32_t va, uint8_t value, size_t len);

    // NUL-terminated strings. ReadW converts UTF-16LE to UTF-8.
    std::string ReadA(uint32_t va, size_t maxLen = 32768) const;
    std::string ReadA(uint32_t va, size_t len, bool exact) const;
    std::string ReadW(uint32_t va, size_t maxChars = 32768) const;
    std::string ReadWCount(uint32_t va, size_t chars) const;
    size_t StrLenW(uint32_t va, size_t maxChars = 32768) const;
    // Write with terminator, truncated to capacity (bytes for A, chars for W).
    // Return the characters written without the terminator.
    size_t WriteA(uint32_t va, const std::string& s, size_t capacity);
    size_t WriteW(uint32_t va, const std::string& utf8, size_t capacityChars);

    // Page allocator, 4 KiB granular. Returns 0 when exhausted.
    uint32_t AllocPages(uint32_t size, uint32_t align = 0x10000u, bool low = false);
    uint32_t AllocPagesAt(uint32_t va, uint32_t size);
    bool FreePages(uint32_t va);
    // Region containing va: allocation base and size. False when free.
    bool QueryPages(uint32_t va, uint32_t* base, uint32_t* size) const;
    // Size of the unallocated span starting at va (page aligned), up to the
    // next allocation or fixed region. 0 when va is allocated or reserved.
    uint32_t FreeSpan(uint32_t va) const;

    // General heap used by HeapAlloc, malloc-style shims and the host side.
    // 16-byte aligned. Returns 0 when exhausted.
    uint32_t HeapAlloc(uint32_t size, bool zero);
    bool HeapFree(uint32_t va);
    uint32_t HeapRealloc(uint32_t va, uint32_t size, bool zero);
    uint32_t HeapSize(uint32_t va) const;  // 0xFFFFFFFF when va is not a heap block
    bool IsHeapBlock(uint32_t va) const;

    // Permanent static allocations (never freed).
    uint32_t StaticAlloc(uint32_t size, uint32_t align = 16);

    // Allocate and copy a string into the guest heap.
    uint32_t NewA(const std::string& s);
    uint32_t NewW(const std::string& utf8);

private:
    struct HeapHeader {
        uint32_t size;
        uint32_t cls;
        uint32_t magic;
        uint32_t pad;
    };
    static constexpr uint32_t kHeapMagic = 0x48454150u;  // "HEAP"
    static constexpr uint32_t kFreeMagic = 0x46524545u;  // "FREE"
    static constexpr uint32_t kLargeClass = 0xFFFFu;
    static constexpr uint32_t kClassCount = 80;
    static uint32_t ClassFor(uint32_t size);
    static uint32_t ClassSize(uint32_t cls);
    bool RefillClass(uint32_t cls);

    uint8_t* arena_ = nullptr;
    std::map<uint32_t, uint32_t> freeLow_;   // start -> size
    std::map<uint32_t, uint32_t> freeHigh_;  // start -> size
    std::map<uint32_t, uint32_t> used_;      // start -> size
    std::vector<uint32_t> freeLists_[kClassCount];
    uint32_t staticCur_ = Map::kStaticBump;
};

}  // namespace NetEmu
