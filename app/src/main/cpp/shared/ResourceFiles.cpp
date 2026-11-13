#include "ResourceFiles.h"

#include "../net/raknet/RakNetWrapper.h"

#include <array>
#include <cmath>
#include <cstring>
#include <mutex>
#include <string_view>
#include <utility>
#include <vector>

namespace WorldRPG::Net {

namespace {

// Desktop CPacketHandler::Packet_ResourceStart strips these before use.
constexpr std::string_view kResourceNameBadChars = "/\\:*?\"<>|";
constexpr std::string_view kFileNameBadChars = ":*?\"<>|";

constexpr uint32_t kElementIdBits = 17;  // NumberOfSignificantBits(131072-1)
constexpr uint32_t kElementIdInvalid = 0xFFFFFFFFu;
constexpr size_t kNeonPackageIdBytes = 16;
constexpr size_t kNeonContentKeyBytes = 32;

std::mutex g_totalsMutex;
ResourceStartTotals g_totals;

std::string RemoveChars(const std::string& in, std::string_view bad) {
    std::string out;
    out.reserve(in.size());
    for (char c : in) {
        if (bad.find(c) == std::string_view::npos) out.push_back(c);
    }
    return out;
}

// Same rules as the desktop file-path guard: relative, no empty, '.' or '..'
// segments, no backslash, no control bytes.
bool IsSafeRelativePath(const std::string& path) {
    if (path.empty() || path.size() > 512 || path.front() == '/') return false;
    for (unsigned char c : path) {
        if (c < 0x20 || c == 0x7F || c == '\\') return false;
    }
    size_t start = 0;
    while (true) {
        const size_t end = path.find('/', start);
        const std::string_view segment = std::string_view(path).substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (segment.empty() || segment == "." || segment == "..") return false;
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return true;
}

// Same width as PacketHandler.cpp ReadElementId: 17 bits, all-ones is invalid.
bool ReadElementIdBits(BitStreamReader& r, uint32_t& out) {
    uint8_t bytes[4] = {};
    if (!r.ReadBits(bytes, kElementIdBits)) return false;
    uint32_t value = 0;
    std::memcpy(&value, bytes, 4);
    out = value == ((1u << kElementIdBits) - 1u) ? kElementIdInvalid : value;
    return true;
}

bool ReadFlagBit(BitStreamReader& r, bool& out) {
    uint8_t bit = 0;
    if (!r.ReadBits(&bit, 1)) return false;
    out = (bit & 1u) != 0;
    return true;
}

// Server writes Write(double): 8 raw bytes, little-endian host order.
bool ReadDouble(BitStreamReader& r, double& out) {
    uint8_t bytes[8] = {};
    if (!r.ReadBytes(bytes, sizeof(bytes))) return false;
    std::memcpy(&out, bytes, sizeof(out));
    return true;
}

void WipeBytes(uint8_t* data, size_t length) {
    volatile uint8_t* p = data;
    for (size_t i = 0; i < length; ++i) p[i] = 0;
}

// Reads one 'F' chunk body after the tag: name, type, CRC, [MD5], size, [bit].
bool ReadFileChunk(BitStreamReader& r, bool md5, ResourceFile& file, const char*& reason) {
    uint8_t nameLength = 0;
    if (!r.Read(nameLength) || nameLength == 0) {
        reason = "file name length";
        return false;
    }
    std::vector<uint8_t> nameBytes(nameLength);
    if (!r.ReadBits(nameBytes.data(), nameLength * 8u)) {
        reason = "file name bytes";
        return false;
    }
    const std::string name = RemoveChars(std::string(nameBytes.begin(), nameBytes.end()), kFileNameBadChars);
    if (!IsSafeRelativePath(name)) {
        reason = "unsafe file path";
        return false;
    }
    uint8_t type = 0;
    uint32_t crc = 0;
    if (!r.Read(type) || !r.Read(crc)) {
        reason = "file type or CRC";
        return false;
    }
    if (type != kResourceFileTypeClientScript && type != kResourceFileTypeClientConfig &&
        type != kResourceFileTypeClientFileMobile && type != kResourceFileTypeClientFile) {
        reason = "unknown file type";
        return false;
    }
    if (md5 && !r.ReadBytes(file.md5, sizeof(file.md5))) {
        reason = "file MD5";
        return false;
    }
    double size = 0.0;
    if (!ReadDouble(r, size)) {
        reason = "file size";
        return false;
    }
    if (!std::isfinite(size) || size < 0.0 || size > 4294967295.0) {
        reason = "file size out of range";
        return false;
    }
    file.name = name;
    file.type = type;
    file.clientFile = type == kResourceFileTypeClientFile || type == kResourceFileTypeClientFileMobile;
    file.crc = crc;
    file.hasMd5 = md5;
    file.size = static_cast<uint64_t>(size);
    if (file.clientFile) {
        if (!ReadFlagBit(r, file.autoDownload)) {
            reason = "auto-download bit";
            return false;
        }
    } else {
        // Desktop AddResourceFile(..., true) for client scripts; configs never download.
        file.autoDownload = type == kResourceFileTypeClientScript;
    }
    return true;
}

// Strict parse of one layout/MD5 combination. Every chunk must parse and the
// stream must end with fewer than 8 bits left (the desktop loop's own stop rule).
bool ParseCandidate(const uint8_t* body, size_t length, size_t bits, ResourceStartLayout layout, bool md5, ResourceStartInfo& info,
                    std::string& why) {
    BitStreamReader r(body, length, bits);
    info = ResourceStartInfo{};
    info.layout = layout;
    info.md5Present = md5;

    const char* reason = nullptr;
    const auto failAt = [&](const char* what) {
        why = std::string(what) + " at bit " + std::to_string(r.GetReadOffsetBits()) + " of " +
              std::to_string(r.GetReadOffsetBits() + r.GetRemainingBits());
        return false;
    };

    uint8_t nameLength = 0;
    if (!r.Read(nameLength) || nameLength == 0) return failAt("resource name length");
    std::vector<uint8_t> nameBytes(nameLength);
    if (!r.ReadBits(nameBytes.data(), nameLength * 8u)) return failAt("resource name bytes");
    info.name = RemoveChars(std::string(nameBytes.begin(), nameBytes.end()), kResourceNameBadChars);
    if (info.name.empty()) return failAt("resource name empty after sanitising");

    if (layout == ResourceStartLayout::Desktop) {
        if (!r.Read(info.startCounter)) return failAt("start counter");
    }
    if (!r.Read(info.resourceId)) return failAt("resource id");
    if (!ReadElementIdBits(r, info.entityId) || !ReadElementIdBits(r, info.dynamicEntityId)) return failAt("element ids");
    if (!r.Read(info.noClientCacheScripts)) return failAt("no-cache script count");
    if (!r.ReadString(info.minServerRequirement, 256) || !r.ReadString(info.minClientRequirement, 256)) return failAt("min version");
    if (layout == ResourceStartLayout::Desktop) {
        if (!ReadFlagBit(r, info.oopEnabled) || !r.Read(info.downloadPriorityGroup)) return failAt("OOP bit or priority group");
    }

    bool sawResourceChunk = false;
    bool sawNeonKey = false;
    bool sawNative = false;
    uint32_t nativeRemaining = 0;
    while (r.GetRemainingBits() >= 8) {
        uint8_t tag = 0;
        r.Read(tag);
        if (nativeRemaining != 0 && tag != 'F') return failAt("native world group interrupted");
        if ((tag == 'K' || tag == 'N' || tag == 'A') && sawResourceChunk) return failAt("misplaced K/N/A chunk");
        if (tag != 'K') sawResourceChunk = true;

        switch (tag) {
            case 'K': {
                // Neon encrypted asset capability. Parsed only to keep the stream aligned:
                // the content key is wiped here and never stored or logged.
                if (sawNeonKey) return failAt("duplicate K chunk");
                uint8_t formatVersion = 0;
                uint8_t packageId[kNeonPackageIdBytes] = {};
                uint8_t contentKey[kNeonContentKeyBytes] = {};
                const bool ok = r.Read(formatVersion) && r.ReadBits(packageId, sizeof(packageId) * 8u) &&
                                r.ReadBits(contentKey, sizeof(contentKey) * 8u);
                WipeBytes(contentKey, sizeof(contentKey));
                if (!ok || formatVersion != 1) return failAt("malformed K chunk");
                sawNeonKey = true;
                info.neonAssetPackage = true;
                info.neonAssetFormat = formatVersion;
                break;
            }
            case 'N':
            case 'A': {
                if (sawNative) return failAt("duplicate native world descriptor");
                uint8_t format = 0;
                uint8_t fileCount = 0;
                uint8_t manifestLength = 0;
                if (!r.Read(format) || !r.Read(fileCount) || !r.Read(manifestLength)) return failAt("native descriptor header");
                const bool startupAuthorization = tag == 'A';
                const bool supportedFormat = format >= 1 && format <= 3;
                const bool supportedCount = format == 3 ? (startupAuthorization ? fileCount == 1 : fileCount >= 4 && fileCount <= 35)
                                                        : fileCount == 3;
                if (!supportedFormat || !supportedCount || manifestLength == 0) return failAt("unsupported native descriptor");
                std::vector<uint8_t> manifest(manifestLength);
                if (!r.ReadBits(manifest.data(), manifest.size() * 8u)) return failAt("native manifest path");
                if (startupAuthorization) {
                    uint8_t wireVersion = 0, startupMode = 0, policy = 0;
                    if (!r.Read(wireVersion) || !r.Read(startupMode) || !r.Read(policy)) return failAt("native startup authorization");
                }
                sawNative = true;
                nativeRemaining = fileCount;
                info.nativeWorldGroup = true;
                info.nativeWorldFormat = format;
                break;
            }
            case 'E': {
                uint8_t nameSize = 0;
                if (!r.Read(nameSize)) return failAt("exported function length");
                std::string name(nameSize, '\0');
                if (nameSize > 0 && !r.ReadBits(reinterpret_cast<uint8_t*>(name.data()), nameSize * 8u)) return failAt("exported function name");
                info.exportedFunctions.push_back(std::move(name));
                break;
            }
            case 'F': {
                ResourceFile file;
                if (!ReadFileChunk(r, md5, file, reason)) return failAt(reason);
                if (nativeRemaining != 0) {
                    if (!file.clientFile || !file.autoDownload) return failAt("native group member is not an auto-download client file");
                    --nativeRemaining;
                    file.nativeWorldGroup = true;
                }
                info.files.push_back(std::move(file));
                break;
            }
            default:
                return failAt("unknown chunk tag");
        }
    }
    if (nativeRemaining != 0) return failAt("incomplete native world group");
    why.clear();
    return true;
}

const char* LayoutName(ResourceStartLayout layout) {
    return layout == ResourceStartLayout::Desktop ? "desktop" : "mobile-shim";
}

} // namespace

size_t ResourceStartInfo::ClientFileCount() const {
    size_t count = 0;
    for (const ResourceFile& file : files) count += file.clientFile ? 1 : 0;
    return count;
}

size_t ResourceStartInfo::AutoDownloadCount() const {
    size_t count = 0;
    for (const ResourceFile& file : files) count += (file.clientFile && file.autoDownload) ? 1 : 0;
    return count;
}

uint64_t ResourceStartInfo::AutoDownloadBytes() const {
    uint64_t total = 0;
    for (const ResourceFile& file : files) {
        if (file.clientFile && file.autoDownload) total += file.size;
    }
    return total;
}

bool ParseResourceStart(const uint8_t* body, size_t length, size_t bits, ResourceStartInfo& out, std::string& error) {
    struct Attempt {
        ResourceStartLayout layout;
        bool md5;
    };
    static constexpr std::array<Attempt, 4> kOrder = {{
        {ResourceStartLayout::Desktop, true},
        {ResourceStartLayout::MobileShim, true},
        {ResourceStartLayout::Desktop, false},
        {ResourceStartLayout::MobileShim, false},
    }};

    {
        std::lock_guard<std::mutex> lock(g_totalsMutex);
        ++g_totals.packets;
    }

    std::string reasons;
    for (const Attempt& attempt : kOrder) {
        ResourceStartInfo candidate;
        std::string why;
        if (ParseCandidate(body, length, bits, attempt.layout, attempt.md5, candidate, why)) {
            std::lock_guard<std::mutex> lock(g_totalsMutex);
            ++g_totals.parsed;
            if (attempt.layout == ResourceStartLayout::Desktop) ++g_totals.desktopLayout;
            else ++g_totals.mobileShimLayout;
            g_totals.filesListed += candidate.files.size();
            g_totals.clientFilesListed += candidate.ClientFileCount();
            g_totals.autoDownloadFiles += candidate.AutoDownloadCount();
            out = std::move(candidate);
            error.clear();
            return true;
        }
        reasons += std::string("[") + LayoutName(attempt.layout) + (attempt.md5 ? " md5: " : " no-md5: ") + why + "] ";
    }
    {
        std::lock_guard<std::mutex> lock(g_totalsMutex);
        ++g_totals.failed;
    }
    error = reasons;
    return false;
}

ResourceStartTotals GetResourceStartTotals() {
    std::lock_guard<std::mutex> lock(g_totalsMutex);
    return g_totals;
}

} // namespace WorldRPG::Net
