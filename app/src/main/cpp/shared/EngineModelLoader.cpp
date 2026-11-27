#include "EngineModelLoader.h"

#include <algorithm>
#include <fstream>

namespace WorldRPG::Script {
namespace {

constexpr std::size_t kChunkHeader = 12;
constexpr std::size_t kMaxAssetBytes = 64 * 1024 * 1024;
constexpr std::uint32_t kChunkClump = 0x10;
constexpr std::uint32_t kChunkStruct = 0x01;
constexpr std::uint32_t kChunkNativeTexture = 0x15;
constexpr std::uint32_t kChunkTextureDictionary = 0x16;

std::uint16_t ReadLE16(std::span<const std::uint8_t> bytes) {
    return std::uint16_t(bytes[0]) | (std::uint16_t(bytes[1]) << 8);
}

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

std::span<const std::uint8_t> FindChunk(std::span<const std::uint8_t> bytes, std::uint32_t wanted) {
    if (bytes.size() > kMaxAssetBytes) return {};
    while (bytes.size() >= kChunkHeader) {
        const auto length = ReadLE32(bytes.subspan(4));
        if (length > bytes.size() - kChunkHeader) return {};
        const auto chunk = bytes.first(kChunkHeader + length);
        if (ReadLE32(chunk) == wanted) return chunk;
        bytes = bytes.subspan(chunk.size());
    }
    return {};
}

using FindAsset = std::span<const std::uint8_t> (*)(std::span<const std::uint8_t>);

std::vector<std::uint8_t> ReadFileChunk(const char* path, FindAsset find) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    const auto size = file.tellg();
    if (size < static_cast<std::streamoff>(kChunkHeader) || size > static_cast<std::streamoff>(kMaxAssetBytes)) return {};
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (!file.seekg(0) || !file.read(reinterpret_cast<char*>(bytes.data()), size)) return {};
    const auto chunk = find(bytes);
    if (chunk.empty()) return {};
    bytes.erase(bytes.begin(), bytes.begin() + (chunk.data() - bytes.data()));
    bytes.resize(chunk.size());
    return bytes;
}

// Desktop DXT textures use the D3D9 native raster header and length-prefixed mip levels.
// GTA's GLES reader allocates its upload buffer from the dimensions, not from those lengths.
bool ValidD3D9Texture(std::span<const std::uint8_t> data) {
    if (data.size() < 88) return false;
    const auto format = ReadLE32(data.subspan(76));
    unsigned width = ReadLE16(data.subspan(80));
    unsigned height = ReadLE16(data.subspan(82));
    const unsigned mipCount = data[85];
    if (!width || !height || width > 4096 || height > 4096 ||
        !mipCount || mipCount > 13 || (std::max(width, height) >> (mipCount - 1)) == 0) return false;
    const unsigned blockBytes = format == 0x31545844 ? 8 :
                                (format == 0x33545844 || format == 0x35545844) ? 16 : 0;
    if (!blockBytes && format != 21 && format != 22) return false;
    data = data.subspan(88);
    for (unsigned mip = 0; mip < mipCount; ++mip) {
        if (data.size() < 4) return false;
        const auto size = ReadLE32(data);
        const auto expected = blockBytes ? ((width + 3) / 4) * ((height + 3) / 4) * blockBytes
                                         : width * height * 4;
        if (size != expected || size > data.size() - 4) return false;
        data = data.subspan(4 + size);
        width = std::max(1u, width / 2);
        height = std::max(1u, height / 2);
    }
    return data.empty();
}

} // namespace

std::span<const std::uint8_t> FindDffClump(std::span<const std::uint8_t> data) {
    const auto clump = FindChunk(data, kChunkClump);
    if (clump.size() < 2 * kChunkHeader + 4) return {};
    const auto payload = clump.subspan(kChunkHeader);
    // RenderWare expects the clump struct first, followed by its child chunks.
    if (ReadLE32(payload) != kChunkStruct || ReadLE32(payload.subspan(4)) < 4 || !ValidChunks(payload, 0)) return {};
    return clump;
}

std::vector<std::uint8_t> ReadDffClump(const char* filepath) {
    return ReadFileChunk(filepath, FindDffClump);
}

std::span<const std::uint8_t> FindTxdDictionary(std::span<const std::uint8_t> data) {
    const auto dictionary = FindChunk(data, kChunkTextureDictionary);
    constexpr std::size_t kDictHeader = 2 * kChunkHeader + 4;
    if (dictionary.size() < kDictHeader) return {};
    auto payload = dictionary.subspan(kChunkHeader);
    // The GTA dictionary reader reads the struct into a four-byte local.
    if (ReadLE32(payload) != kChunkStruct || ReadLE32(payload.subspan(4)) != 4 || !ValidChunks(payload, 0)) return {};
    unsigned textures = ReadLE16(payload.subspan(kChunkHeader));
    if (!textures) return {};
    payload = dictionary.subspan(kDictHeader);
    for (; textures > 0; --textures) {
        if (payload.size() < 2 * kChunkHeader + 4 || ReadLE32(payload) != kChunkNativeTexture) return {};
        const auto length = ReadLE32(payload.subspan(4));
        if (length < kChunkHeader + 4 || length > payload.size() - kChunkHeader) return {};
        const auto native = payload.subspan(kChunkHeader, length);
        if (ReadLE32(native) != kChunkStruct || !ValidChunks(native, 0)) return {};
        const auto structLength = ReadLE32(native.subspan(4));
        if (structLength < 4) return {};
        const auto raster = native.subspan(kChunkHeader, structLength);
        if (ReadLE32(raster) == 9 && !ValidD3D9Texture(raster)) return {};
        payload = payload.subspan(kChunkHeader + length);
    }
    if (!payload.empty() && ReadLE32(payload) != 0x03) return {};
    return dictionary;
}

std::vector<std::uint8_t> ReadTxdDictionary(const char* filepath) {
    return ReadFileChunk(filepath, FindTxdDictionary);
}

} // namespace WorldRPG::Script

#if defined(__ANDROID__)
#include "../gtasa/GameTasks.h"
#include "../hook/MemoryPatcher.h"
#include "ModelInfoRead.h"

#include <array>
#include <cstddef>
#include <cstring>
#include <dlfcn.h>
#include <optional>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace WorldRPG::Script {
namespace {

struct RwStream;
struct RpClump;
struct RpAtomic;
struct RwTexture;
struct RwTexDictionary;

constexpr int kStreamMemory = 3;
constexpr int kStreamRead = 1;
constexpr unsigned kPedModel = 7;

// libGTASA's _rwStreamInitialize reads the pointer and 32-bit size at 0 and 8.
struct RwMemory {
    const void* bytes;
    std::uint32_t length;
};
static_assert(offsetof(RwMemory, length) == 8);

template <typename T>
T& Field(void* object, std::size_t offset) {
    return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(object) + offset);
}

using LoadModel = bool (*)(RwStream*, std::uint32_t);
using ReadTexture = RwTexture* (*)(const char*, const char*);
using ReadClump = RpClump* (*)(RwStream*);
using ReadDictionary = RwTexDictionary* (*)(RwStream*);

struct RenderWare {
    void** engineInstance = nullptr;
    void** models = nullptr;
    const std::uint8_t* streamingInfo = nullptr;
    void (*removeModel)(int) = nullptr;
    void (*requestModel)(int, int) = nullptr;
    RwStream* (*open)(int, int, const void*) = nullptr;
    int (*close)(RwStream*, void*) = nullptr;
    int (*findChunk)(RwStream*, std::uint32_t, std::uint32_t*, std::uint32_t*) = nullptr;
    RwTexture* (*findTexture)(RwTexDictionary*, const char*) = nullptr;
    RwTexDictionary* (*forEachTexture)(const RwTexDictionary*, RwTexture* (*)(RwTexture*, void*), void*) = nullptr;
    RwTexture* (*detachTexture)(RwTexture*) = nullptr;
    int (*destroyTexture)(RwTexture*) = nullptr;
    ReadDictionary readDictionary = nullptr;
    ReadDictionary readFilteredDictionary = nullptr;
    int (*destroyDictionary)(RwTexDictionary*) = nullptr;
    int (*destroyClump)(RpClump*) = nullptr;
    int (*atomicCount)(RpClump*) = nullptr;
    RpAtomic* (*firstAtomic)(RpClump*) = nullptr;
    void* (*geometrySkin)(void*) = nullptr;
    void* (*skinHierarchy)(RpClump*) = nullptr;

    LoadModel loadClump = nullptr;
    LoadModel loadAtomic = nullptr;
    ReadClump readClump = nullptr;
    ReadTexture readTexture = nullptr;
};

RenderWare rw;
Hook::MemoryPatcher hooks;

struct ImportedTxd {
    std::uint64_t asset;
    RwTexDictionary* dictionary;
};
struct Replacement {
    std::uint64_t asset;
    DffSource source;
    bool alpha;
    std::optional<bool> originalAlpha;
    bool logged = false;
};
struct ModelAssets {
    std::optional<Replacement> replacement;
    std::vector<ImportedTxd> textures;
};

// CStreaming::ConvertBufferToObject calls the loaders synchronously on the game thread.
thread_local std::unordered_map<std::uint16_t, ModelAssets> modelAssets;
std::unordered_map<int, void*> customModels;  // engineRequestModel slots this client owns
thread_local std::unordered_set<std::uint16_t> pendingRefreshes;
thread_local bool refreshQueued = false;
thread_local const std::vector<ImportedTxd>* currentTextures = nullptr;
thread_local RwStream* replacementStream = nullptr;
thread_local bool replacementIsPed = false;

void* GetModel(std::uint32_t id) {
    return rw.models && id < Models::kStockModelCount ? rw.models[id] : nullptr;
}

unsigned ModelType(void* model) {
    // CFileLoader::LoadClumpFile, 0x4d057c, uses GetModelType at vtable+0x28.
    return Field<std::uint8_t (*)(void*)>(*static_cast<void**>(model), 0x28)(model);
}

void SetAlpha(void* model, bool enabled) {
    // Draw-last bit in CBaseModelInfo::m_flags, see ModelInfoRead.
    auto& flags = Field<std::uint16_t>(model, 0x2c);
    flags = enabled ? flags | 2u : flags & ~2u;
}

void RestoreAlpha(std::uint16_t model, const Replacement& replacement) {
    if (void* info = GetModel(model); info && replacement.originalAlpha.has_value())
        SetAlpha(info, *replacement.originalAlpha);
}

RwStream* OpenMemory(std::span<const std::uint8_t> bytes) {
    const RwMemory memory{bytes.data(), static_cast<std::uint32_t>(bytes.size())};
    return rw.open(kStreamMemory, kStreamRead, &memory);
}

RwTexture* ReadModelTexture(const char* name, const char* mask) {
    if (name && currentTextures) {
        for (auto it = currentTextures->rbegin(); it != currentTextures->rend(); ++it) {
            if (auto* texture = rw.findTexture(it->dictionary, name)) {
                // RwTextureRead (0x274310) increments refCount for a dictionary hit.
                ++Field<int>(texture, 0x64);
                return texture;
            }
        }
    }
    return rw.readTexture(name, mask);
}

RpClump* ReadReplacementClump(RwStream* stream) {
    auto* clump = rw.readClump(stream);
    if (!clump || stream != replacementStream) return clump;
    bool valid = rw.atomicCount(clump) > 0;
    if (valid && replacementIsPed) {
        // CClumpModelInfo::SetClump (0x45b8dc-0x45b93c) binds the frame
        // hierarchy to the first atomic's skin after the clump is decoded.
        auto* atomic = rw.firstAtomic(clump);
        auto* geometry = atomic ? Field<void*>(atomic, 0x30) : nullptr;
        valid = geometry && rw.geometrySkin(geometry) && rw.skinHierarchy(clump);
    }
    if (!valid) {
        rw.destroyClump(clump);
        return nullptr;
    }
    return clump;
}

bool LoadWithOverrides(RwStream* stock, std::uint32_t id, LoadModel loader) {
    if (id >= Models::kStockModelCount) return loader(stock, id);
    auto it = modelAssets.find(static_cast<std::uint16_t>(id));
    void* model = GetModel(id);
    if (it == modelAssets.end() || !model) return loader(stock, id);

    auto& assets = it->second;
    const auto* previousTextures = std::exchange(currentTextures, &assets.textures);
    bool loaded = false;
    if (assets.replacement) {
        auto& replacement = *assets.replacement;
        const auto& source = replacement.source;
        const auto disk = source.path.empty() ? std::vector<std::uint8_t>{} : ReadDffClump(source.path.c_str());
        const auto& data = source.path.empty() ? source.bytes : disk;
        if (!data.empty()) {
            if (auto* stream = OpenMemory(data)) {
                auto* before = std::exchange(replacementStream, stream);
                const bool wasPed = std::exchange(replacementIsPed, ModelType(model) == kPedModel);
                loaded = loader(stream, id);
                replacementStream = before;
                replacementIsPed = wasPed;
                rw.close(stream, nullptr);
            }
        }
        if (loaded) {
            if (!replacement.originalAlpha.has_value())
                replacement.originalAlpha = (Field<std::uint16_t>(model, 0x2c) & 2u) != 0;
            SetAlpha(model, replacement.alpha);
        }
        if (!replacement.logged) {
            if (loaded) HOOK_LOGI("RenderWare: applied resource DFF to model %u", id);
            else HOOK_LOGE("RenderWare: could not apply resource DFF to model %u (%s)", id,
                           source.path.empty() ? "raw data" : source.path.c_str());
            replacement.logged = true;
        }
    }
    if (!loaded) loaded = loader(stock, id);
    currentTextures = previousTextures;
    if (loaded) pendingRefreshes.erase(static_cast<std::uint16_t>(id));
    return loaded;
}

bool LoadClumpModel(RwStream* stream, std::uint32_t id) {
    return LoadWithOverrides(stream, id, rw.loadClump);
}

bool LoadAtomicModel(RwStream* stream, std::uint32_t id) {
    return LoadWithOverrides(stream, id, rw.loadAtomic);
}

bool ResolveRenderWare() {
    if (rw.open) return rw.engineInstance && *rw.engineInstance;
    void* lib = dlopen("libGTASA.so", RTLD_NOW | RTLD_NOLOAD);
    if (!lib) return false;
    RenderWare symbols;
    const auto resolve = [lib](auto& entry, const char* symbol) {
        entry = reinterpret_cast<std::remove_reference_t<decltype(entry)>>(dlsym(lib, symbol));
        return entry != nullptr;
    };
    const bool ready =
        resolve(symbols.engineInstance, "RwEngineInstance") &&
        resolve(symbols.models, "_ZN10CModelInfo16ms_modelInfoPtrsE") &&
        resolve(symbols.streamingInfo, "_ZN10CStreaming16ms_aInfoForModelE") &&
        resolve(symbols.removeModel, "_ZN10CStreaming11RemoveModelEi") &&
        resolve(symbols.requestModel, "_ZN10CStreaming12RequestModelEii") &&
        resolve(symbols.open, "_Z12RwStreamOpen12RwStreamType18RwStreamAccessTypePKv") &&
        resolve(symbols.close, "_Z13RwStreamCloseP8RwStreamPv") &&
        resolve(symbols.findChunk, "_Z17RwStreamFindChunkP8RwStreamjPjS1_") &&
        resolve(symbols.findTexture, "_Z31RwTexDictionaryFindNamedTextureP15RwTexDictionaryPKc") &&
        resolve(symbols.forEachTexture, "_Z29RwTexDictionaryForAllTexturesPK15RwTexDictionaryPFP9RwTextureS3_PvES4_") &&
        resolve(symbols.detachTexture, "_Z28RwTexDictionaryRemoveTextureP9RwTexture") &&
        resolve(symbols.destroyTexture, "_Z16RwTextureDestroyP9RwTexture") &&
        resolve(symbols.readDictionary, "_Z25RwTexDictionaryStreamReadP8RwStream") &&
        resolve(symbols.readFilteredDictionary, "_Z28RwTexDictionaryGtaStreamReadP8RwStream") &&
        resolve(symbols.destroyDictionary, "_Z22RwTexDictionaryDestroyP15RwTexDictionary") &&
        resolve(symbols.destroyClump, "_Z14RpClumpDestroyP7RpClump") &&
        resolve(symbols.atomicCount, "_Z20RpClumpGetNumAtomicsP7RpClump") &&
        resolve(symbols.firstAtomic, "_Z14GetFirstAtomicP7RpClump") &&
        resolve(symbols.geometrySkin, "RpSkinGeometryGetSkin") &&
        resolve(symbols.skinHierarchy, "_Z25GetAnimHierarchyFromClumpP7RpClump");
    dlclose(lib);
    if (!ready) return false;
    rw = symbols;
    return *rw.engineInstance != nullptr;
}

template <typename Fn>
bool InstallHook(void* lib, const char* symbol, Fn replacement, Fn& original,
                 const std::array<std::uint32_t, 4>& prologue) {
    if (original) return true;
    void* target = dlsym(lib, symbol);
    // The trampoline copies these instructions verbatim. Reject unknown code
    // before patching; these arm64 GTA 2.10 prologues have no PC-relative instructions.
    if (!target || std::memcmp(target, prologue.data(), sizeof(prologue)) != 0) {
        HOOK_LOGE("RenderWare: unsupported prologue for %s", symbol);
        return false;
    }
    const auto hook = hooks.installHook(reinterpret_cast<std::uintptr_t>(target),
                                        reinterpret_cast<std::uintptr_t>(replacement));
    if (!hook.success) return false;
    original = reinterpret_cast<Fn>(hook.trampolineAddress);
    return true;
}

bool EnableOverrides() {
    if (!ResolveRenderWare()) return false;
    if (rw.loadAtomic && rw.loadClump) return true;
    void* lib = dlopen("libGTASA.so", RTLD_NOW | RTLD_NOLOAD);
    if (!lib) return false;
    const bool ready =
        InstallHook(lib, "_Z13RwTextureReadPKcS0_", ReadModelTexture, rw.readTexture,
                    {0xa9bd57f6, 0xa9014ff4, 0xa9027bfd, 0x910083fd}) &&
        InstallHook(lib, "_Z17RpClumpStreamReadP8RwStream", ReadReplacementClump, rw.readClump,
                    {0xd102c3ff, 0xa90667fa, 0xa9075ff8, 0xa90857f6}) &&
        InstallHook(lib, "_ZN11CFileLoader13LoadClumpFileEP8RwStreamj", LoadClumpModel, rw.loadClump,
                    {0xa9bc5ff8, 0xa90157f6, 0xa9024ff4, 0xa9037bfd}) &&
        InstallHook(lib, "_ZN11CFileLoader14LoadAtomicFileEP8RwStreamj", LoadAtomicModel, rw.loadAtomic,
                    {0xa9bd57f6, 0xa9014ff4, 0xa9027bfd, 0x910083fd});
    dlclose(lib);
    return ready;
}

bool RefreshWhenUnused(std::uint16_t id) {
    void* model = GetModel(id);
    if (!model) return true;
    // CStreaming::RemoveModel (0x392014) reads the state at stride 0x14 + 0x10.
    const auto* stream = rw.streamingInfo + 0x14 * id;
    const auto state = stream[0x10];
    if (state == 4) return false; // Two-phase load still owns the model.
    if (state != 1) return true;  // Normal streaming will use the new override.
    // CBaseModelInfo::AddRef (0x45b528) uses the halfword at +0x22.
    if (Field<std::uint16_t>(model, 0x22) != 0) return false;
    const auto flags = stream[6];
    rw.removeModel(id);
    if (flags & 0x0e) rw.requestModel(id, flags); // Game/mission required or kept in memory.
    return true;
}

void RefreshModel(std::uint16_t id) {
    if (RefreshWhenUnused(id)) {
        pendingRefreshes.erase(id);
        return;
    }
    pendingRefreshes.insert(id);
    if (refreshQueued) return;
    refreshQueued = true;
    GameTasks::Defer([] {
        std::erase_if(pendingRefreshes, RefreshWhenUnused);
        refreshQueued = !pendingRefreshes.empty();
        return !refreshQueued;
    });
}

} // namespace

void* NativeLoadTXD(std::span<const std::uint8_t> bytes, bool filtering) {
    RwTexDictionary* result = nullptr;
    GameTasks::Run([&] {
        if (!ResolveRenderWare()) return;
        auto* stream = OpenMemory(bytes);
        if (!stream) return;
        if (rw.findChunk(stream, kChunkTextureDictionary, nullptr, nullptr))
            result = (filtering ? rw.readFilteredDictionary : rw.readDictionary)(stream);
        rw.close(stream, nullptr);
    });
    return result;
}

bool NativeImportTXD(std::uint16_t model, std::uint64_t asset, void* dictionary) {
    bool imported = false;
    GameTasks::Run([&] {
        if (!dictionary || !EnableOverrides() || !GetModel(model)) return;
        auto& textures = modelAssets[model].textures;
        std::erase_if(textures, [asset](const ImportedTxd& entry) { return entry.asset == asset; });
        textures.push_back({asset, static_cast<RwTexDictionary*>(dictionary)});
        RefreshModel(model);
        imported = true;
    });
    return imported;
}

bool NativeReplaceDFF(std::uint16_t model, std::uint64_t asset, const DffSource& source, bool alpha) {
    bool replaced = false;
    GameTasks::Run([&] {
        if (!EnableOverrides() || !GetModel(model)) return;
        auto& replacement = modelAssets[model].replacement;
        if (replacement) RestoreAlpha(model, *replacement);
        replacement = Replacement{asset, source, alpha, std::nullopt};
        RefreshModel(model);
        replaced = true;
    });
    return replaced;
}

bool NativeRestoreDFF(std::uint16_t model) {
    bool restored = false;
    GameTasks::Run([&] {
        const auto it = modelAssets.find(model);
        if (it == modelAssets.end() || !it->second.replacement) return;
        RestoreAlpha(model, *it->second.replacement);
        it->second.replacement.reset();
        if (it->second.textures.empty()) modelAssets.erase(it);
        RefreshModel(model);
        restored = true;
    });
    return restored;
}

void NativeReleaseModelAsset(std::uint64_t asset, void* dictionary) {
    GameTasks::Defer([=] {
        for (auto it = modelAssets.begin(); it != modelAssets.end();) {
            const auto model = it->first;
            auto& assets = it->second;
            const auto previousCount = assets.textures.size();
            std::erase_if(assets.textures, [asset](const ImportedTxd& txd) { return txd.asset == asset; });
            bool changed = assets.textures.size() != previousCount;
            if (assets.replacement && assets.replacement->asset == asset) {
                RestoreAlpha(model, *assets.replacement);
                assets.replacement.reset();
                changed = true;
            }
            if (changed) RefreshModel(model);
            if (!assets.replacement && assets.textures.empty()) it = modelAssets.erase(it);
            else ++it;
        }
        if (auto* txd = static_cast<RwTexDictionary*>(dictionary)) {
            // GTA's destroy-dictionary leaves surviving textures linked to
            // the old list. Detach textures still owned by streamed materials.
            rw.forEachTexture(txd, [](RwTexture* texture, void*) {
                if (Field<int>(texture, 0x64) > 1) {
                    rw.detachTexture(texture);
                    rw.destroyTexture(texture);
                }
                return texture;
            }, nullptr);
            rw.destroyDictionary(txd);
        }
        return true;
    });
}

// CModelInfoSA::MakeObjectModel / CopyStreamingInfoFromModel (CModelInfoSA.cpp
// 2039-2060, 1925-1935): a new object model is a copy of the parent's model info
// with no references, no RwObject and no 2D effects, in a free ms_modelInfoPtrs
// slot, streaming from the parent's archive entry. A model replacement for the new
// id then loads instead of the parent's DFF (the streaming override above).
// CClientModelManager::GetFirstFreeModelID: the lowest id with neither model info
// nor a streaming entry.
int NativeAllocateObjectModel(int parent) {
    int allocated = -1;
    GameTasks::Run([&] {
        if (!ResolveRenderWare() || parent < 0 || parent >= static_cast<int>(Models::kStockModelCount)) return;
        void* base = rw.models[parent];
        // Object parents only: their model info is a CAtomicModelInfo (0x48 bytes) or a
        // CClumpModelInfo (0x50, the animation index at +0x48).
        if (!base || (ModelType(base) != 1 && ModelType(base) != 5)) return;
        auto* streaming = const_cast<std::uint8_t*>(rw.streamingInfo);
        for (int id = 1; id < static_cast<int>(Models::kStockModelCount); ++id) {
            std::uint8_t* entry = streaming + 0x14 * id;
            if (rw.models[id] || entry[0x10] != 0 || Field<std::uint32_t>(entry, 0x0C) != 0) continue;
            auto* info = static_cast<std::uint8_t*>(::operator new(0x50));
            std::memset(info, 0, 0x50);
            std::memcpy(info, base, 0x48);
            Field<std::uint16_t>(info, 0x22) = 0;     // CBaseModelInfo::AddRef count
            Field<void*>(info, 0x40) = nullptr;        // RwObject (CAtomicModelInfo::SetAtomic)
            Field<std::uint8_t>(info, 0x27) = 0;       // 2D effect count (SetAtomic recomputes it)
            Field<std::int32_t>(info, 0x48) = -1;      // CClumpModelInfo animation index
            const std::uint8_t* from = streaming + 0x14 * parent;
            std::memset(entry, 0, 0x14);
            Field<std::uint16_t>(entry, 0x0) = 0xFFFF;  // prev, next, next in IMG: unlinked
            Field<std::uint16_t>(entry, 0x2) = 0xFFFF;
            Field<std::uint16_t>(entry, 0x4) = 0xFFFF;
            entry[0x7] = from[0x7];                                                   // archive
            std::memcpy(entry + 0x8, from + 0x8, 8);  // offset and size in blocks
            rw.models[id] = info;
            customModels[id] = info;
            allocated = id;
            return;
        }
    });
    return allocated;
}

// CModelInfoSA::DeallocateModel: the slot is cleared once nothing uses the model.
bool NativeFreeModel(int id) {
    bool freed = false;
    GameTasks::Run([&] {
        const auto found = customModels.find(id);
        if (found == customModels.end() || !ResolveRenderWare()) return;
        auto* info = static_cast<std::uint8_t*>(found->second);
        if (Field<std::uint16_t>(info, 0x22) != 0) return;  // still referenced by an entity
        auto* entry = const_cast<std::uint8_t*>(rw.streamingInfo) + 0x14 * id;
        if (entry[0x10] == 1) rw.removeModel(id);
        if (entry[0x10] != 0) return;
        rw.models[id] = nullptr;
        std::memset(entry, 0, 0x14);
        Field<std::uint16_t>(entry, 0x0) = 0xFFFF;
        Field<std::uint16_t>(entry, 0x2) = 0xFFFF;
        Field<std::uint16_t>(entry, 0x4) = 0xFFFF;
        ::operator delete(info);
        customModels.erase(found);
        modelAssets.erase(static_cast<std::uint16_t>(id));
        freed = true;
    });
    return freed;
}

} // namespace WorldRPG::Script

#else

namespace WorldRPG::Script {

void* NativeLoadTXD(std::span<const std::uint8_t> bytes, bool) {
    // On host builds without RenderWare, return a non-null sentinel for validated TXD payloads
    // so engineLoadTXD / engineSetRadarMapTile / TXD lifecycle tests run end-to-end.
    return bytes.empty() ? nullptr : reinterpret_cast<void*>(static_cast<uintptr_t>(1));
}
bool NativeImportTXD(std::uint16_t, std::uint64_t, void*) { return false; }
bool NativeReplaceDFF(std::uint16_t, std::uint64_t, const DffSource&, bool) { return false; }
bool NativeRestoreDFF(std::uint16_t) { return false; }
void NativeReleaseModelAsset(std::uint64_t, void*) {}
int NativeAllocateObjectModel(int) { return -1; }
bool NativeFreeModel(int) { return false; }

} // namespace WorldRPG::Script

#endif
