#include "GuestMemory.h"

#include "NetEmuLog.h"

#include <sys/mman.h>

#include <algorithm>

namespace NetEmu {

namespace {
uint32_t AlignUp(uint32_t v, uint32_t a) { return (v + a - 1) & ~(a - 1); }

void AppendUtf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

std::u16string Utf8ToUtf16(const std::string& s) {
    std::u16string out;
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        uint32_t cp = 0xFFFD;
        size_t len = 1;
        if (c < 0x80) {
            cp = c;
        } else if ((c & 0xE0) == 0xC0 && i + 1 < s.size()) {
            cp = ((c & 0x1Fu) << 6) | (s[i + 1] & 0x3F);
            len = 2;
        } else if ((c & 0xF0) == 0xE0 && i + 2 < s.size()) {
            cp = ((c & 0x0Fu) << 12) | ((s[i + 1] & 0x3F) << 6) | (s[i + 2] & 0x3F);
            len = 3;
        } else if ((c & 0xF8) == 0xF0 && i + 3 < s.size()) {
            cp = ((c & 0x07u) << 18) | ((s[i + 1] & 0x3F) << 12) | ((s[i + 2] & 0x3F) << 6) | (s[i + 3] & 0x3F);
            len = 4;
        }
        i += len;
        if (cp >= 0x10000) {
            cp -= 0x10000;
            out.push_back(static_cast<char16_t>(0xD800 + (cp >> 10)));
            out.push_back(static_cast<char16_t>(0xDC00 + (cp & 0x3FF)));
        } else {
            out.push_back(static_cast<char16_t>(cp));
        }
    }
    return out;
}

// Allocate from a free-range map, first fit with alignment.
uint32_t TakeRange(std::map<uint32_t, uint32_t>& free, uint32_t size, uint32_t align) {
    for (auto it = free.begin(); it != free.end(); ++it) {
        const uint32_t start = it->first;
        const uint32_t len = it->second;
        const uint32_t aligned = AlignUp(start, align);
        if (aligned < start || aligned - start > len || len - (aligned - start) < size) continue;
        free.erase(it);
        if (aligned > start) free[start] = aligned - start;
        const uint32_t tail = start + len - (aligned + size);
        if (tail) free[aligned + size] = tail;
        return aligned;
    }
    return 0;
}

void GiveRange(std::map<uint32_t, uint32_t>& free, uint32_t start, uint32_t size) {
    auto next = free.lower_bound(start);
    if (next != free.end() && start + size == next->first) {
        size += next->second;
        next = free.erase(next);
    }
    if (next != free.begin()) {
        auto prev = std::prev(next);
        if (prev->first + prev->second == start) {
            prev->second += size;
            return;
        }
    }
    free[start] = size;
}

bool CarveRange(std::map<uint32_t, uint32_t>& free, uint32_t va, uint32_t size) {
    auto it = free.upper_bound(va);
    if (it == free.begin()) return false;
    --it;
    const uint32_t start = it->first;
    const uint32_t len = it->second;
    if (va < start || va - start > len || len - (va - start) < size) return false;
    free.erase(it);
    if (va > start) free[start] = va - start;
    const uint32_t tail = start + len - (va + size);
    if (tail) free[va + size] = tail;
    return true;
}
}  // namespace

GuestMemory::~GuestMemory() {
    if (arena_) munmap(arena_, Map::kArenaSize);
}

bool GuestMemory::Init(std::string* error) {
    void* p = mmap(nullptr, Map::kArenaSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (p == MAP_FAILED) {
        if (error) *error = "cannot reserve 2 GiB guest arena";
        return false;
    }
    arena_ = static_cast<uint8_t*>(p);
    // Images are placed before any thread stack, so the image window shares the low map.
    freeLow_[Map::kLowPagesBegin] = Map::kImageEnd - Map::kLowPagesBegin;
    freeHigh_[Map::kPagesBegin] = Map::kPagesEnd - Map::kPagesBegin;
    return true;
}

uint8_t* GuestMemory::Host(uint32_t va, size_t len) const {
    if (!arena_ || va < Map::kNullGuard) return nullptr;
    const uint64_t end = static_cast<uint64_t>(va) + len;
    if (end > Map::kArenaSize) return nullptr;
    return arena_ + va;
}

uint32_t GuestMemory::Read32(uint32_t va) const {
    uint32_t v = 0;
    ReadBytes(va, &v, 4);
    return v;
}
uint16_t GuestMemory::Read16(uint32_t va) const {
    uint16_t v = 0;
    ReadBytes(va, &v, 2);
    return v;
}
uint8_t GuestMemory::Read8(uint32_t va) const {
    uint8_t v = 0;
    ReadBytes(va, &v, 1);
    return v;
}
uint64_t GuestMemory::Read64(uint32_t va) const {
    uint64_t v = 0;
    ReadBytes(va, &v, 8);
    return v;
}
bool GuestMemory::Write32(uint32_t va, uint32_t v) { return WriteBytes(va, &v, 4); }
bool GuestMemory::Write16(uint32_t va, uint16_t v) { return WriteBytes(va, &v, 2); }
bool GuestMemory::Write8(uint32_t va, uint8_t v) { return WriteBytes(va, &v, 1); }
bool GuestMemory::Write64(uint32_t va, uint64_t v) { return WriteBytes(va, &v, 8); }

bool GuestMemory::ReadBytes(uint32_t va, void* out, size_t len) const {
    if (!len) return true;
    const uint8_t* p = Host(va, len);
    if (!p) return false;
    std::memcpy(out, p, len);
    return true;
}

bool GuestMemory::WriteBytes(uint32_t va, const void* in, size_t len) {
    if (!len) return true;
    uint8_t* p = Host(va, len);
    if (!p) return false;
    std::memcpy(p, in, len);
    return true;
}

bool GuestMemory::Fill(uint32_t va, uint8_t value, size_t len) {
    if (!len) return true;
    uint8_t* p = Host(va, len);
    if (!p) return false;
    std::memset(p, value, len);
    return true;
}

std::string GuestMemory::ReadA(uint32_t va, size_t maxLen) const {
    std::string out;
    for (size_t i = 0; i < maxLen; ++i) {
        const uint8_t* p = Host(va + static_cast<uint32_t>(i));
        if (!p || !*p) break;
        out.push_back(static_cast<char>(*p));
    }
    return out;
}

std::string GuestMemory::ReadA(uint32_t va, size_t len, bool) const {
    std::string out(len, '\0');
    if (!ReadBytes(va, out.data(), len)) out.clear();
    return out;
}

size_t GuestMemory::StrLenW(uint32_t va, size_t maxChars) const {
    size_t n = 0;
    while (n < maxChars && Valid(va + static_cast<uint32_t>(n * 2), 2) && Read16(va + static_cast<uint32_t>(n * 2))) ++n;
    return n;
}

std::string GuestMemory::ReadWCount(uint32_t va, size_t chars) const {
    std::string out;
    for (size_t i = 0; i < chars; ++i) {
        const uint32_t at = va + static_cast<uint32_t>(i * 2);
        if (!Valid(at, 2)) break;
        uint32_t cp = Read16(at);
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < chars) {
            const uint32_t lo = Read16(at + 2);
            if (lo >= 0xDC00 && lo <= 0xDFFF) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                ++i;
            }
        }
        AppendUtf8(out, cp);
    }
    return out;
}

std::string GuestMemory::ReadW(uint32_t va, size_t maxChars) const {
    return ReadWCount(va, StrLenW(va, maxChars));
}

size_t GuestMemory::WriteA(uint32_t va, const std::string& s, size_t capacity) {
    if (!capacity) return 0;
    const size_t n = std::min(s.size(), capacity - 1);
    if (!WriteBytes(va, s.data(), n)) return 0;
    Write8(va + static_cast<uint32_t>(n), 0);
    return n;
}

size_t GuestMemory::WriteW(uint32_t va, const std::string& utf8, size_t capacityChars) {
    if (!capacityChars) return 0;
    const std::u16string w = Utf8ToUtf16(utf8);
    const size_t n = std::min(w.size(), capacityChars - 1);
    if (!WriteBytes(va, w.data(), n * 2)) return 0;
    Write16(va + static_cast<uint32_t>(n * 2), 0);
    return n;
}

uint32_t GuestMemory::AllocPages(uint32_t size, uint32_t align, bool low) {
    if (!size) return 0;
    size = AlignUp(size, Map::kPage);
    align = std::max<uint32_t>(align, Map::kPage);
    uint32_t va = TakeRange(low ? freeLow_ : freeHigh_, size, align);
    if (!va && low) va = TakeRange(freeHigh_, size, align);
    if (!va && !low) va = TakeRange(freeLow_, size, align);
    if (!va) {
        NETEMU_LOGE("guest page allocator exhausted (%u bytes)", size);
        return 0;
    }
    used_[va] = size;
    std::memset(arena_ + va, 0, size);
    return va;
}

uint32_t GuestMemory::AllocPagesAt(uint32_t va, uint32_t size) {
    size = AlignUp(size, Map::kPage);
    if (va & (Map::kPage - 1)) return 0;
    if (!CarveRange(freeLow_, va, size) && !CarveRange(freeHigh_, va, size)) return 0;
    used_[va] = size;
    std::memset(arena_ + va, 0, size);
    return va;
}

bool GuestMemory::FreePages(uint32_t va) {
    auto it = used_.find(va);
    if (it == used_.end()) return false;
    const uint32_t size = it->second;
    used_.erase(it);
    SetProtect(va, size, kDefaultProtect);
    // Drop the physical pages; the reservation stays.
    madvise(arena_ + va, size, MADV_DONTNEED);
    GiveRange(va < Map::kImageEnd ? freeLow_ : freeHigh_, va, size);
    return true;
}

void GuestMemory::SetProtect(uint32_t va, uint32_t len, uint32_t prot) {
    if (!len) return;
    const uint32_t first = va / Map::kPage;
    const uint32_t last = static_cast<uint32_t>((static_cast<uint64_t>(va) + len - 1) / Map::kPage);
    for (uint32_t pg = first; pg <= last; ++pg) {
        if (prot == kDefaultProtect) protect_.erase(pg);
        else protect_[pg] = prot;
    }
}

uint32_t GuestMemory::Protect(uint32_t va) const {
    auto it = protect_.find(va / Map::kPage);
    return it == protect_.end() ? kDefaultProtect : it->second;
}

uint32_t GuestMemory::ProtectRunEnd(uint32_t va, uint32_t limit) const {
    const uint32_t prot = Protect(va);
    uint32_t pg = (va & ~(Map::kPage - 1)) + Map::kPage;
    while (pg < limit && pg != 0 && Protect(pg) == prot) pg += Map::kPage;
    return pg < limit && pg != 0 ? pg : limit;
}

bool GuestMemory::QueryPages(uint32_t va, uint32_t* base, uint32_t* size) const {
    auto it = used_.upper_bound(va);
    if (it == used_.begin()) return false;
    --it;
    if (va - it->first >= it->second) return false;
    if (base) *base = it->first;
    if (size) *size = it->second;
    return true;
}

uint32_t GuestMemory::FreeSpan(uint32_t va) const {
    va &= ~(Map::kPage - 1);
    auto spanIn = [va](const std::map<uint32_t, uint32_t>& free) -> uint32_t {
        auto it = free.upper_bound(va);
        if (it == free.begin()) return 0;
        --it;
        if (va - it->first >= it->second) return 0;
        return it->first + it->second - va;
    };
    if (const uint32_t n = spanIn(freeLow_)) return n;
    return spanIn(freeHigh_);
}

uint32_t GuestMemory::ClassFor(uint32_t size) {
    if (size <= 1024) return size ? (size - 1) / 16 : 0;  // 0..63: 16..1024
    uint32_t cls = 64;
    uint32_t cap = 2048;
    while (cap < size) {
        cap <<= 1;
        ++cls;
    }
    return cls;  // 64: 2K ... 70: 128K
}

uint32_t GuestMemory::ClassSize(uint32_t cls) {
    if (cls < 64) return (cls + 1) * 16;
    return 2048u << (cls - 64);
}

bool GuestMemory::RefillClass(uint32_t cls) {
    const uint32_t block = ClassSize(cls) + sizeof(HeapHeader);
    const uint32_t chunk = std::max<uint32_t>(0x40000u, AlignUp(block * 8, Map::kPage));
    const uint32_t base = AllocPages(chunk, 0x10000u);
    if (!base) return false;
    for (uint32_t off = 0; off + block <= chunk; off += block) {
        HeapHeader h{0, cls, kFreeMagic, 0};
        std::memcpy(arena_ + base + off, &h, sizeof(h));
        freeLists_[cls].push_back(base + off + sizeof(HeapHeader));
    }
    return true;
}

uint32_t GuestMemory::HeapAlloc(uint32_t size, bool zero) {
    if (size > 0x40000000u) return 0;
    const uint32_t want = size ? size : 1;
    if (want > 128 * 1024) {
        const uint32_t base = AllocPages(want + sizeof(HeapHeader), Map::kPage);
        if (!base) return 0;
        HeapHeader h{size, kLargeClass, kHeapMagic, 0};
        std::memcpy(arena_ + base, &h, sizeof(h));
        return base + sizeof(HeapHeader);
    }
    const uint32_t cls = ClassFor(want);
    if (freeLists_[cls].empty() && !RefillClass(cls)) return 0;
    const uint32_t va = freeLists_[cls].back();
    freeLists_[cls].pop_back();
    HeapHeader h{size, cls, kHeapMagic, 0};
    std::memcpy(arena_ + va - sizeof(HeapHeader), &h, sizeof(h));
    if (zero) std::memset(arena_ + va, 0, ClassSize(cls));
    return va;
}

bool GuestMemory::IsHeapBlock(uint32_t va) const {
    if (va < Map::kNullGuard + sizeof(HeapHeader) || (va & 15) || !Valid(va - sizeof(HeapHeader), sizeof(HeapHeader))) return false;
    HeapHeader h;
    std::memcpy(&h, arena_ + va - sizeof(HeapHeader), sizeof(h));
    return h.magic == kHeapMagic && (h.cls < kClassCount || h.cls == kLargeClass);
}

bool GuestMemory::HeapFree(uint32_t va) {
    if (!va) return true;
    if (!IsHeapBlock(va)) {
        NETEMU_LOGW("HeapFree of non-heap pointer 0x%08X", va);
        return false;
    }
    HeapHeader h;
    std::memcpy(&h, arena_ + va - sizeof(HeapHeader), sizeof(h));
    if (h.cls == kLargeClass) {
        h.magic = kFreeMagic;
        std::memcpy(arena_ + va - sizeof(HeapHeader), &h, sizeof(h));
        return FreePages(va - sizeof(HeapHeader));
    }
    h.magic = kFreeMagic;
    std::memcpy(arena_ + va - sizeof(HeapHeader), &h, sizeof(h));
    freeLists_[h.cls].push_back(va);
    return true;
}

uint32_t GuestMemory::HeapSize(uint32_t va) const {
    if (!IsHeapBlock(va)) return 0xFFFFFFFFu;
    HeapHeader h;
    std::memcpy(&h, arena_ + va - sizeof(HeapHeader), sizeof(h));
    return h.size;
}

uint32_t GuestMemory::HeapRealloc(uint32_t va, uint32_t size, bool zero) {
    if (!va) return HeapAlloc(size, zero);
    const uint32_t old = HeapSize(va);
    if (old == 0xFFFFFFFFu) return 0;
    HeapHeader h;
    std::memcpy(&h, arena_ + va - sizeof(HeapHeader), sizeof(h));
    if (h.cls != kLargeClass && size && size <= ClassSize(h.cls)) {
        if (zero && size > old) std::memset(arena_ + va + old, 0, size - old);
        h.size = size;
        std::memcpy(arena_ + va - sizeof(HeapHeader), &h, sizeof(h));
        return va;
    }
    const uint32_t nva = HeapAlloc(size, zero);
    if (!nva) return 0;
    std::memcpy(arena_ + nva, arena_ + va, std::min(old, size));
    HeapFree(va);
    return nva;
}

uint32_t GuestMemory::StaticAlloc(uint32_t size, uint32_t align) {
    const uint32_t va = AlignUp(staticCur_, align);
    if (va + size > Map::kStaticEnd) {
        NETEMU_LOGE("guest static region exhausted");
        return 0;
    }
    staticCur_ = va + size;
    std::memset(arena_ + va, 0, size);
    return va;
}

uint32_t GuestMemory::NewA(const std::string& s) {
    const uint32_t va = HeapAlloc(static_cast<uint32_t>(s.size() + 1), false);
    if (va) WriteA(va, s, s.size() + 1);
    return va;
}

uint32_t GuestMemory::NewW(const std::string& utf8) {
    const std::u16string w = Utf8ToUtf16(utf8);
    const uint32_t va = HeapAlloc(static_cast<uint32_t>((w.size() + 1) * 2), false);
    if (va) {
        WriteBytes(va, w.data(), w.size() * 2);
        Write16(va + static_cast<uint32_t>(w.size() * 2), 0);
    }
    return va;
}

}  // namespace NetEmu
