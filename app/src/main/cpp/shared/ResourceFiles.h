#pragma once

// RESOURCE_START (game packet 82) descriptor: resource identity, the file list
// and the exported functions. Parsed from the server's CResourceStartPacket.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace WorldRPG::Net {

// Wire file types, in CResourceFile.h order (MAP=0 SCRIPT=1 CLIENT_SCRIPT=2
// CONFIG=3 CLIENT_CONFIG=4 HTML=5 CLIENT_FILE=6). The mobile shim remaps
// CLIENT_FILE 6 -> 5, so both values are accepted as a client file.
constexpr uint8_t kResourceFileTypeClientScript = 2;
constexpr uint8_t kResourceFileTypeClientConfig = 4;
constexpr uint8_t kResourceFileTypeClientFileMobile = 5;
constexpr uint8_t kResourceFileTypeClientFile = 6;

struct ResourceFile {
    std::string name;             // relative to the resource directory, '/' separators
    uint8_t type = 0;             // raw wire type
    bool clientFile = false;      // CLIENT_FILE (wire 6, or 5 after the mobile remap)
    uint32_t crc = 0;             // CRC-32 (zlib convention)
    bool hasMd5 = false;          // false when the packet carries no MD5 (see layout notes)
    uint8_t md5[16] = {};
    uint64_t size = 0;            // size hint, from the wire double
    bool autoDownload = false;    // client files: the wire bit; scripts and configs: true
    bool nativeWorldGroup = false;
};

enum class ResourceStartLayout : uint8_t {
    Desktop,     // start counter, OOP bit and priority group present
    MobileShim,  // those three fields stripped by the server-side mobile shim
};

struct ResourceStartInfo {
    std::string name;             // sanitised as the desktop client does
    ResourceStartLayout layout = ResourceStartLayout::Desktop;
    bool md5Present = false;      // per-file MD5 field present in this layout
    uint32_t startCounter = 0;    // desktop layout only
    uint16_t resourceId = 0;
    uint32_t entityId = 0xFFFFFFFFu;
    uint32_t dynamicEntityId = 0xFFFFFFFFu;
    uint16_t noClientCacheScripts = 0;
    std::string minServerRequirement;
    std::string minClientRequirement;
    bool oopEnabled = false;      // desktop layout only
    int32_t downloadPriorityGroup = 0;  // desktop layout only
    bool neonAssetPackage = false;      // 'K' chunk present (key is discarded, never stored)
    uint8_t neonAssetFormat = 0;
    bool nativeWorldGroup = false;      // 'N' or 'A' chunk present
    uint8_t nativeWorldFormat = 0;
    std::vector<ResourceFile> files;
    std::vector<std::string> exportedFunctions;

    size_t ClientFileCount() const;
    size_t AutoDownloadCount() const;
    uint64_t AutoDownloadBytes() const;
};

// Parses a RESOURCE_START body (the bytes after the 0x63 user-packet and game-id
// bytes) of `bits` valid bits. Tries, in order: desktop layout with MD5, mobile
// shim layout with MD5, desktop without MD5, shim without MD5. A layout wins only
// when every chunk parses strictly and the stream ends in padding. On failure
// `error` lists the reason for each attempt.
bool ParseResourceStart(const uint8_t* body, size_t length, size_t bits, ResourceStartInfo& out, std::string& error);

// Session totals kept by ParseResourceStart (thread-safe to read).
struct ResourceStartTotals {
    uint64_t packets = 0;
    uint64_t parsed = 0;
    uint64_t failed = 0;
    uint64_t desktopLayout = 0;
    uint64_t mobileShimLayout = 0;
    uint64_t filesListed = 0;
    uint64_t clientFilesListed = 0;
    uint64_t autoDownloadFiles = 0;
};
ResourceStartTotals GetResourceStartTotals();

} // namespace WorldRPG::Net
