#include "EngineModelLoader.h"

#include <fstream>
#include <span>

namespace WorldRPG::Script {
namespace {

constexpr std::size_t kChunkHeader = 12;
constexpr std::size_t kMaxDffBytes = 64 * 1024 * 1024;
constexpr std::uint32_t kChunkClump = 0x10;

std::uint32_t ReadLE32(std::span<const std::uint8_t> bytes) {
    return std::uint32_t(bytes[0]) | (std::uint32_t(bytes[1]) << 8) |
           (std::uint32_t(bytes[2]) << 16) | (std::uint32_t(bytes[3]) << 24);
}

bool IsChunkContainer(std::uint32_t type) {
    switch (type) {
        case 0x03: // Extension (plugin headers and opaque plugin data)
        case 0x06: // Texture
        case 0x07: // Material
        case 0x08: // Material list
        case 0x0E: // Frame list
        case 0x0F: // Geometry
        case kChunkClump:
        case 0x12: // Light
        case 0x14: // Atomic
        case 0x1A: // Geometry list
            return true;
        default:
            return false;
    }
}

bool ValidChunks(std::span<const std::uint8_t> bytes, unsigned depth, bool plugins = false) {
    if (depth > 12) return false;
    while (!bytes.empty()) {
        if (bytes.size() < kChunkHeader) return false;
        const auto type = ReadLE32(bytes);
        const auto length = ReadLE32(bytes.subspan(4));
        if (length > bytes.size() - kChunkHeader) return false;
        if (!plugins && IsChunkContainer(type) &&
            !ValidChunks(bytes.subspan(kChunkHeader, length), depth + 1, type == 0x03))
            return false;
        bytes = bytes.subspan(kChunkHeader + length);
    }
    return true;
}

} // namespace

std::vector<std::uint8_t> ReadDffClump(const char* filepath) {
    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    const auto size = file.tellg();
    if (size < static_cast<std::streamoff>(kChunkHeader) || size > static_cast<std::streamoff>(kMaxDffBytes)) return {};
    file.seekg(0);
    std::uint8_t header[kChunkHeader];
    auto remaining = static_cast<std::size_t>(size);
    while (remaining >= kChunkHeader && file.read(reinterpret_cast<char*>(header), kChunkHeader)) {
        const auto type = ReadLE32(header);
        const auto length = ReadLE32(std::span(header).subspan(4));
        if (length > remaining - kChunkHeader) return {};
        if (type == kChunkClump) {
            if (length < kChunkHeader + 4) return {};
            std::vector<std::uint8_t> clump(header, header + kChunkHeader);
            clump.resize(kChunkHeader + length);
            if (!file.read(reinterpret_cast<char*>(clump.data() + kChunkHeader), length)) return {};
            const auto payload = std::span(clump).subspan(kChunkHeader);
            // RenderWare expects the clump struct first, followed by its child chunks.
            if (ReadLE32(payload) != 1 || ReadLE32(payload.subspan(4)) < 4 || !ValidChunks(payload, 0)) return {};
            return clump;
        }
        file.seekg(length, std::ios::cur);
        remaining -= kChunkHeader + length;
    }
    return {};
}

} // namespace WorldRPG::Script

#if defined(__ANDROID__)
#include "../gtasa/GameTasks.h"

#include <cstddef>
#include <dlfcn.h>

namespace WorldRPG::Script {
namespace {

struct RwStream;
struct RpClump;
struct RwTexDictionary;

constexpr int kStreamFilename = 2;
constexpr int kStreamMemory = 3;
constexpr int kStreamRead = 1;
constexpr std::uint32_t kChunkTextureDictionary = 0x16;

// libGTASA's _rwStreamInitialize reads the buffer pointer and 32-bit size
// at offsets 0 and 8 for rwSTREAMMEMORY on arm64.
struct RwMemory {
    const void* bytes;
    std::uint32_t length;
};
static_assert(offsetof(RwMemory, length) == 8);

struct RenderWare {
    void** engineInstance;
    RwStream* (*open)(int, int, const void*);
    int (*close)(RwStream*, void*);
    int (*findChunk)(RwStream*, std::uint32_t, std::uint32_t*, std::uint32_t*);
    RpClump* (*readClump)(RwStream*);
    int (*destroyClump)(RpClump*);
    RwTexDictionary* (*readDictionary)(RwStream*);
    int (*destroyDictionary)(RwTexDictionary*);
};

const RenderWare& Symbols() {
    // All callers resolve and use these symbols from the game's task queue.
    static const RenderWare symbols = [] {
        void* lib = dlopen("libGTASA.so", RTLD_NOW | RTLD_NOLOAD);
        if (!lib) return RenderWare{};
        RenderWare rw{
            reinterpret_cast<void**>(dlsym(lib, "RwEngineInstance")),
            reinterpret_cast<decltype(RenderWare::open)>(
                dlsym(lib, "_Z12RwStreamOpen12RwStreamType18RwStreamAccessTypePKv")),
            reinterpret_cast<decltype(RenderWare::close)>(dlsym(lib, "_Z13RwStreamCloseP8RwStreamPv")),
            reinterpret_cast<decltype(RenderWare::findChunk)>(dlsym(lib, "_Z17RwStreamFindChunkP8RwStreamjPjS1_")),
            reinterpret_cast<decltype(RenderWare::readClump)>(dlsym(lib, "_Z17RpClumpStreamReadP8RwStream")),
            reinterpret_cast<decltype(RenderWare::destroyClump)>(dlsym(lib, "_Z14RpClumpDestroyP7RpClump")),
            reinterpret_cast<decltype(RenderWare::readDictionary)>(
                dlsym(lib, "_Z28RwTexDictionaryGtaStreamReadP8RwStream")),
            reinterpret_cast<decltype(RenderWare::destroyDictionary)>(
                dlsym(lib, "_Z22RwTexDictionaryDestroyP15RwTexDictionary")),
        };
        dlclose(lib);
        return rw;
    }();
    return symbols;
}

template <typename Asset>
Asset* ReadAsset(const RenderWare& rw, int type, const void* input, std::uint32_t chunk, Asset* (*read)(RwStream*)) {
    if (!rw.engineInstance || !*rw.engineInstance || !rw.open || !rw.close || !rw.findChunk || !read) return nullptr;
    RwStream* stream = rw.open(type, kStreamRead, input);
    if (!stream) return nullptr;
    Asset* asset = rw.findChunk(stream, chunk, nullptr, nullptr) ? read(stream) : nullptr;
    rw.close(stream, nullptr);
    return asset;
}

}  // namespace

void* NativeLoadDFF(const char* path) {
    const auto clump = ReadDffClump(path);
    if (clump.empty()) return nullptr;
    RpClump* result = nullptr;
    GameTasks::Run([&] {
        const auto& rw = Symbols();
        if (!rw.destroyClump) return;
        const RwMemory memory{clump.data(), static_cast<std::uint32_t>(clump.size())};
        result = ReadAsset(rw, kStreamMemory, &memory, kChunkClump, rw.readClump);
    });
    return result;
}

void* NativeLoadTXD(const char* path) {
    RwTexDictionary* result = nullptr;
    GameTasks::Run([&] {
        const auto& rw = Symbols();
        if (!rw.destroyDictionary) return;
        // CFileLoader::LoadTexDictionary creates an empty dictionary on error.
        result = ReadAsset(rw, kStreamFilename, path, kChunkTextureDictionary, rw.readDictionary);
    });
    return result;
}

void NativeDestroyClump(void* clump) {
    if (clump) GameTasks::Defer([clump] {
        Symbols().destroyClump(static_cast<RpClump*>(clump));
        return true;
    });
}

void NativeDestroyTXD(void* txd) {
    if (txd) GameTasks::Defer([txd] {
        Symbols().destroyDictionary(static_cast<RwTexDictionary*>(txd));
        return true;
    });
}

}  // namespace WorldRPG::Script

#else

namespace WorldRPG::Script {

void* NativeLoadDFF(const char*) { return nullptr; }
void* NativeLoadTXD(const char*) { return nullptr; }
void NativeDestroyClump(void*) {}
void NativeDestroyTXD(void*) {}

}  // namespace WorldRPG::Script

#endif
