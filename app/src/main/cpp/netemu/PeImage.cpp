#include "PeImage.h"

#include "GuestMemory.h"
#include "NetEmuLog.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace NetEmu {

namespace {
template <class T>
bool ReadAt(const std::vector<uint8_t>& f, size_t off, T* out) {
    if (off > f.size() || f.size() - off < sizeof(T)) return false;
    std::memcpy(out, f.data() + off, sizeof(T));
    return true;
}

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

constexpr uint16_t kMachineI386 = 0x014C;
constexpr uint16_t kMagicPe32 = 0x010B;
}  // namespace

bool PeImage::Load(GuestMemory& mem, const std::vector<uint8_t>& f, const ImportResolver& resolve, std::string* error) {
    auto fail = [&](const char* why) {
        if (error) *error = why;
        return false;
    };
    uint16_t mz = 0;
    uint32_t peOff = 0;
    if (!ReadAt(f, 0, &mz) || mz != 0x5A4D || !ReadAt(f, 0x3C, &peOff)) return fail("not an MZ file");
    uint32_t sig = 0;
    uint16_t machine = 0, nsec = 0, optSize = 0, magic = 0;
    if (!ReadAt(f, peOff, &sig) || sig != 0x00004550) return fail("missing PE signature");
    ReadAt(f, peOff + 4, &machine);
    ReadAt(f, peOff + 6, &nsec);
    ReadAt(f, peOff + 20, &optSize);
    const size_t opt = peOff + 24;
    ReadAt(f, opt, &magic);
    if (machine != kMachineI386 || magic != kMagicPe32) return fail("not a PE32 i386 image");

    uint32_t entryRva = 0, imageBase = 0, imageSize = 0, headerSize = 0, numDirs = 0;
    ReadAt(f, opt + 16, &entryRva);
    ReadAt(f, opt + 28, &imageBase);
    ReadAt(f, opt + 56, &imageSize);
    ReadAt(f, opt + 60, &headerSize);
    ReadAt(f, opt + 92, &numDirs);
    if (!imageSize || imageSize > 0x08000000u) return fail("bad image size");
    auto dir = [&](uint32_t i, uint32_t* rva, uint32_t* size) {
        *rva = *size = 0;
        if (i >= numDirs) return;
        ReadAt(f, opt + 96 + i * 8, rva);
        ReadAt(f, opt + 100 + i * 8, size);
    };

    // Preferred base first; relocate when it is taken.
    uint32_t base = mem.AllocPagesAt(imageBase, imageSize);
    if (!base) {
        for (uint32_t va = Map::kImageBegin; va + imageSize <= Map::kImageEnd && !base; va += 0x01000000u)
            base = mem.AllocPagesAt(va, imageSize);
    }
    if (!base) return fail("no room for the image");
    base_ = base;
    size_ = imageSize;
    entry_ = entryRva ? base + entryRva : 0;

    if (!mem.WriteBytes(base, f.data(), std::min<size_t>(headerSize, f.size()))) return fail("header copy failed");
    const size_t secTable = opt + optSize;
    for (uint16_t i = 0; i < nsec; ++i) {
        const size_t s = secTable + i * 40u;
        char name[9] = {};
        uint32_t vsize = 0, va = 0, rawSize = 0, rawOff = 0, ch = 0;
        if (s + 40 > f.size()) return fail("truncated section table");
        std::memcpy(name, f.data() + s, 8);
        ReadAt(f, s + 8, &vsize);
        ReadAt(f, s + 12, &va);
        ReadAt(f, s + 16, &rawSize);
        ReadAt(f, s + 20, &rawOff);
        ReadAt(f, s + 36, &ch);
        const uint32_t copy = std::min(rawSize, vsize ? vsize : rawSize);
        if (static_cast<uint64_t>(va) + std::max(vsize, rawSize) > imageSize) return fail("section outside image");
        if (copy && (static_cast<uint64_t>(rawOff) + copy > f.size())) return fail("section data outside file");
        if (copy) mem.WriteBytes(base + va, f.data() + rawOff, copy);
        sections_.push_back({name, base + va, std::max(vsize, rawSize), ch});
    }
    ApplySectionProtections(mem, base, headerSize);

    // Base relocations (only IMAGE_REL_BASED_HIGHLOW appears in i386 DLLs).
    const int64_t delta = static_cast<int64_t>(base) - imageBase;
    uint32_t relRva = 0, relSize = 0;
    dir(5, &relRva, &relSize);
    if (delta && relRva) {
        uint32_t off = 0;
        while (off + 8 <= relSize) {
            const uint32_t page = mem.Read32(base + relRva + off);
            const uint32_t blockSize = mem.Read32(base + relRva + off + 4);
            if (blockSize < 8) break;
            for (uint32_t e = 8; e + 2 <= blockSize; e += 2) {
                const uint16_t entry = mem.Read16(base + relRva + off + e);
                const uint32_t type = entry >> 12;
                const uint32_t at = base + page + (entry & 0x0FFF);
                if (type == 3) mem.Write32(at, static_cast<uint32_t>(mem.Read32(at) + delta));
                else if (type != 0) NETEMU_LOGW("relocation type %u ignored", type);
            }
            off += blockSize;
        }
    } else if (delta) {
        return fail("image moved but has no relocations");
    }

    // Imports.
    uint32_t impRva = 0, impSize = 0;
    dir(1, &impRva, &impSize);
    for (uint32_t d = impRva; impRva; d += 20) {
        const uint32_t ilt = mem.Read32(base + d);
        const uint32_t nameRva = mem.Read32(base + d + 12);
        const uint32_t iat = mem.Read32(base + d + 16);
        if (!nameRva) break;
        const std::string dll = Lower(mem.ReadA(base + nameRva, 256));
        for (uint32_t k = 0;; ++k) {
            const uint32_t thunk = mem.Read32(base + (ilt ? ilt : iat) + k * 4);
            if (!thunk) break;
            std::string fn;
            uint16_t ord = 0;
            if (thunk & 0x80000000u) ord = static_cast<uint16_t>(thunk & 0xFFFF);
            else fn = mem.ReadA(base + thunk + 2, 512);
            const uint32_t target = resolve(dll, fn, ord);
            if (!target) {
                ++unresolved_;
                NETEMU_LOGW("unresolved import %s!%s", dll.c_str(), fn.empty() ? ("#" + std::to_string(ord)).c_str() : fn.c_str());
            }
            mem.Write32(base + iat + k * 4, target);
        }
    }

    // Exports.
    uint32_t expRva = 0, expSize = 0;
    dir(0, &expRva, &expSize);
    if (expRva) {
        const uint32_t e = base + expRva;
        const uint32_t ordBase = mem.Read32(e + 16);
        const uint32_t nFuncs = mem.Read32(e + 20);
        const uint32_t nNames = mem.Read32(e + 24);
        const uint32_t funcs = base + mem.Read32(e + 28);
        const uint32_t names = base + mem.Read32(e + 32);
        const uint32_t ords = base + mem.Read32(e + 36);
        // Ordinals are 16-bit; larger tables only come from a malformed file.
        if (nFuncs > 0x10000 || nNames > 0x10000) return fail("export table too large");
        for (uint32_t i = 0; i < nFuncs; ++i) {
            const uint32_t rva = mem.Read32(funcs + i * 4);
            if (rva) ordinals_[static_cast<uint16_t>(ordBase + i)] = base + rva;
        }
        for (uint32_t i = 0; i < nNames; ++i) {
            const std::string n = mem.ReadA(base + mem.Read32(names + i * 4), 512);
            const uint16_t idx = mem.Read16(ords + i * 2);
            if (idx >= nFuncs) continue;
            const uint32_t rva = mem.Read32(funcs + idx * 4u);
            if (rva) exports_[n] = base + rva;
        }
    }

    // Static TLS. The directory holds absolute VAs that were relocated above.
    uint32_t tlsRva = 0, tlsSize = 0;
    dir(9, &tlsRva, &tlsSize);
    if (tlsRva) {
        const uint32_t t = base + tlsRva;
        tls_.present = true;
        tls_.rawStart = mem.Read32(t);
        tls_.rawEnd = mem.Read32(t + 4);
        tls_.indexVa = mem.Read32(t + 8);
        const uint32_t cbs = mem.Read32(t + 12);
        tls_.zeroFill = mem.Read32(t + 16);
        for (uint32_t i = 0; cbs && i < 64; ++i) {
            const uint32_t cb = mem.Read32(cbs + i * 4);
            if (!cb) break;
            tls_.callbacks.push_back(cb);
        }
    }
    NETEMU_LOGI("PE mapped at 0x%08X size 0x%X, %zu sections, %zu exports, %zu unresolved imports, TLS %s (%zu callbacks)",
                base_, size_, sections_.size(), exports_.size(), unresolved_, tls_.present ? "yes" : "no", tls_.callbacks.size());
    return true;
}

uint32_t PeImage::MapView(GuestMemory& mem, const std::vector<uint8_t>& f, uint32_t* sizeOut) {
    uint32_t peOff = 0, imageBase = 0, imageSize = 0, headerSize = 0, relRva = 0, relSize = 0;
    uint16_t nsec = 0, optSize = 0;
    if (!ReadAt(f, 0x3C, &peOff) || !ReadAt(f, peOff + 6, &nsec) || !ReadAt(f, peOff + 20, &optSize)) return 0;
    const size_t opt = peOff + 24;
    ReadAt(f, opt + 28, &imageBase);
    ReadAt(f, opt + 56, &imageSize);
    ReadAt(f, opt + 60, &headerSize);
    ReadAt(f, opt + 96 + 5 * 8, &relRva);
    ReadAt(f, opt + 100 + 5 * 8, &relSize);
    if (!imageSize || imageSize > 0x08000000u) return 0;
    const uint32_t base = mem.AllocPages(imageSize);
    if (!base) return 0;
    mem.WriteBytes(base, f.data(), std::min<size_t>(headerSize, f.size()));
    for (uint16_t i = 0; i < nsec; ++i) {
        const size_t sct = opt + optSize + i * 40u;
        uint32_t vsize = 0, va = 0, rawSize = 0, rawOff = 0;
        ReadAt(f, sct + 8, &vsize);
        ReadAt(f, sct + 12, &va);
        ReadAt(f, sct + 16, &rawSize);
        ReadAt(f, sct + 20, &rawOff);
        uint32_t ch = 0;
        ReadAt(f, sct + 36, &ch);
        const uint32_t copy = std::min(rawSize, vsize ? vsize : rawSize);
        if (copy && static_cast<uint64_t>(rawOff) + copy <= f.size() && static_cast<uint64_t>(va) + copy <= imageSize)
            mem.WriteBytes(base + va, f.data() + rawOff, copy);
        const bool x = ch & 0x20000000u, r = ch & 0x40000000u, w = ch & 0x80000000u;
        if (va < imageSize)
            mem.SetProtect(base + va, std::max<uint32_t>(std::max(vsize, rawSize), 1),
                           x ? (w ? 0x80u : r ? 0x20u : 0x10u) : (w ? 0x08u : r ? 0x02u : 0x01u));  // fresh view: WRITECOPY
    }
    mem.SetProtect(base, std::max<uint32_t>(headerSize, 1), 0x02);
    const int64_t delta = static_cast<int64_t>(base) - imageBase;
    for (uint32_t off = 0; delta && relRva && off + 8 <= relSize;) {
        const uint32_t page = mem.Read32(base + relRva + off);
        const uint32_t blockSize = mem.Read32(base + relRva + off + 4);
        if (blockSize < 8) break;
        for (uint32_t e = 8; e + 2 <= blockSize; e += 2) {
            const uint16_t entry = mem.Read16(base + relRva + off + e);
            const uint32_t at = base + page + (entry & 0x0FFF);
            if ((entry >> 12) == 3) mem.Write32(at, static_cast<uint32_t>(mem.Read32(at) + delta));
        }
        off += blockSize;
    }
    if (sizeOut) *sizeOut = imageSize;
    return base;
}

// What VirtualQuery reports for a mapped image on Windows: headers read-only,
// each section by its characteristics (writable data reads back as
// PAGE_READWRITE once the loader and CRT have touched it).
void PeImage::ApplySectionProtections(GuestMemory& mem, uint32_t base, uint32_t headerSize) const {
    mem.SetProtect(base, std::max<uint32_t>(headerSize, 1), 0x02);  // PAGE_READONLY
    for (const PeSection& s : sections_) {
        const bool x = s.characteristics & 0x20000000u, r = s.characteristics & 0x40000000u, w = s.characteristics & 0x80000000u;
        const uint32_t prot = x ? (w ? 0x80u : r ? 0x20u : 0x10u)  // EXECUTE_WRITECOPY / EXECUTE_READ / EXECUTE
                                : (w ? 0x04u : r ? 0x02u : 0x01u);  // READWRITE / READONLY / NOACCESS
        mem.SetProtect(s.va, std::max<uint32_t>(s.size, 1), prot);
    }
}

uint32_t PeImage::Export(const std::string& name) const {
    auto it = exports_.find(name);
    return it == exports_.end() ? 0 : it->second;
}

uint32_t PeImage::ExportOrdinal(uint16_t ordinal) const {
    auto it = ordinals_.find(ordinal);
    return it == ordinals_.end() ? 0 : it->second;
}

}  // namespace NetEmu
