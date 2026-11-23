#pragma once
// Maps a PE32 (i386) DLL into guest memory: sections, base relocations,
// imports (bound to shim thunks through a resolver), exports and static TLS.

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace NetEmu {

class GuestMemory;

struct PeTls {
    uint32_t rawStart = 0;   // VA of the TLS template
    uint32_t rawEnd = 0;
    uint32_t indexVa = 0;    // VA of the DWORD that receives the TLS index
    uint32_t zeroFill = 0;
    std::vector<uint32_t> callbacks;
    bool present = false;
};

struct PeSection {
    std::string name;
    uint32_t va = 0;
    uint32_t size = 0;
    uint32_t characteristics = 0;
};

class PeImage {
public:
    // Returns the guest address bound to an import, or 0 when unknown.
    // name is empty for an ordinal import. dll is lower case.
    using ImportResolver = std::function<uint32_t(const std::string& dll, const std::string& name, uint16_t ordinal)>;

    bool Load(GuestMemory& mem, const std::vector<uint8_t>& file, const ImportResolver& resolve, std::string* error);

    // Maps a copy of an image the way NtMapViewOfSection(SEC_IMAGE) does:
    // headers and sections at their RVAs, relocated to the new base, imports
    // left unbound. Returns the base or 0.
    static uint32_t MapView(GuestMemory& mem, const std::vector<uint8_t>& file, uint32_t* size);

    uint32_t Base() const { return base_; }
    uint32_t Size() const { return size_; }
    uint32_t EntryPoint() const { return entry_; }
    const PeTls& Tls() const { return tls_; }
    const std::vector<PeSection>& Sections() const { return sections_; }
    uint32_t Export(const std::string& name) const;
    uint32_t ExportOrdinal(uint16_t ordinal) const;
    size_t UnresolvedImports() const { return unresolved_; }

private:
    uint32_t base_ = 0;
    uint32_t size_ = 0;
    uint32_t entry_ = 0;
    size_t unresolved_ = 0;
    PeTls tls_;
    std::vector<PeSection> sections_;
    void ApplySectionProtections(GuestMemory& mem, uint32_t base, uint32_t headerSize) const;
    std::unordered_map<std::string, uint32_t> exports_;
    std::unordered_map<uint16_t, uint32_t> ordinals_;
};

}  // namespace NetEmu
