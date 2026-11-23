#include "RenderGuard.h"
#include "GameEngine.h"
#include "GameHeap.h"

#include <array>
#include <cstdint>
#include <cstring>

#ifdef __ANDROID__
#include <dlfcn.h>
#include "../hook/MemoryPatcher.h"
#endif

// Guard for the SIGSEGV seen on the S25 in sortLODs (emu_ArraysGetID, fault 0x30) under
// CRenderer::RenderEverythingBarRoads. sortLODs (libGTASA 2.10 arm64, 0x4f5104) is the qsort comparator
// for CRenderer::ms_aVisibleLodPtrs. For each entity it reads:
//   entity+0x20 (RwObject) -> first atomic (GetFirstAtomic when the object is a clump, type byte != 1)
//   atomic+0x30 (geometry) -> +0x98 (instance data) -> +0x5c (vertex array handle, 8 bytes)
// and passes the handle to emu_ArraysGetID, which dereferences handle+0x30. Models 1261 and 1266 reach it
// with instance data but a zero handle. This replaces the comparator with the same logic, except that a
// zero handle sorts as id 0 instead of being passed to emu_ArraysGetID. The render function itself is not
// hooked (an earlier version hooked RenderEverythingBarRoads to drop those entities).
namespace WorldRPG::Game {

#ifdef __ANDROID__
namespace {

// Verified against the GTA 2.10 arm64 RenderQueue constructor, Flush and GraphicsThread.
struct RenderQueueStorage {
    std::byte dispatch[0x3e8];
    bool threaded;
    bool useMutex;
    std::byte padding[6];
    void* mutex;
    std::byte* begin;
    std::byte* end;
    std::byte* read;
    std::byte flags[8];
    std::byte* committed;
    std::byte* write;
};
static_assert(offsetof(RenderQueueStorage, begin) == 0x3f8);
static_assert(offsetof(RenderQueueStorage, committed) == 0x418);
static_assert(offsetof(RenderQueueStorage, write) == 0x420);

RenderQueueStorage** g_renderQueue = nullptr;
void (*g_flushQueue)(RenderQueueStorage*) = nullptr;
void (*g_lockQueue)(void*) = nullptr;
void (*g_unlockQueue)(void*) = nullptr;
void (*g_safeVertices)() = nullptr;
void (*g_safeIndices)() = nullptr;

using CopyVertices = void (*)(const void*, unsigned, void (*)());
using CopyIndices = void (*)(const void*, unsigned);
using UpdateBuffer = void (*)(void*, const void*, unsigned);
CopyVertices g_copyVertices = nullptr;
CopyIndices g_copyIndices = nullptr;
UpdateBuffer g_updateVertices = nullptr;
UpdateBuffer g_updateIndices = nullptr;

bool EnsureUploadSpace(unsigned size, std::size_t overhead) {
    auto* queue = *g_renderQueue;
    if (!queue || !queue->begin) return false;
    const std::size_t need = static_cast<std::size_t>(size) + overhead;
    if (need <= static_cast<std::size_t>(queue->end - queue->begin)) return true;

    // The native checks use 32-bit payload arithmetic. Bound the growth before
    // calling those checks, including the header and alignment in the bound.
    constexpr std::size_t kMaxQueueBytes = 64 * 1024 * 1024;
    if (need <= kMaxQueueBytes && (!queue->threaded || (queue->useMutex && queue->mutex))) {
        std::size_t capacity = 8 * 1024 * 1024;
        while (capacity < need) capacity *= 2;
        auto* replacement = static_cast<std::byte*>(AllocateGameMemory(capacity));
        if (replacement) {
            // The threaded flush drains the queue and moves live CPU array data out of it.
            // Synchronous Process has consumed all commands, but Flush only resets the cursors.
            if (!queue->threaded) {
                g_safeIndices();
                g_safeVertices();
            }
            g_flushQueue(queue);
            void* mutex = queue->useMutex ? queue->mutex : nullptr;
            if (mutex) g_lockQueue(mutex);
            auto* previous = queue->begin;
            const bool drained = queue->read == previous && queue->committed == previous && queue->write == previous;
            if (drained) {
                queue->begin = replacement;
                queue->end = replacement + capacity;
                queue->read = replacement;
                __atomic_store_n(&queue->committed, replacement, __ATOMIC_RELEASE);
                __atomic_store_n(&queue->write, replacement, __ATOMIC_RELEASE);
            }
            if (mutex) g_unlockQueue(mutex);
            FreeGameMemory(drained ? previous : replacement);
            if (drained) {
                ENGINE_LOGI("RenderQueue: grew to %zu MiB for a %u-byte upload", capacity >> 20, size);
                return true;
            }
        }
    }
    static thread_local unsigned reports = 0;
    if (reports++ < 5) ENGINE_LOGE("RenderQueue: skipped %u-byte upload that cannot fit", size);
    return false;
}

void CopyVerticesChecked(const void* data, unsigned size, void (*copy)()) {
    if (EnsureUploadSpace(size, 0x18 + 7)) g_copyVertices(data, size, copy);
}

void CopyIndicesChecked(const void* data, unsigned size) {
    if (EnsureUploadSpace(size, 0x0c + 3 + 7)) g_copyIndices(data, size);
}

void UpdateVerticesChecked(void* buffer, const void* data, unsigned size) {
    if (EnsureUploadSpace(size, 0x40 + 7)) g_updateVertices(buffer, data, size);
}

void UpdateIndicesChecked(void* buffer, const void* data, unsigned size) {
    if (EnsureUploadSpace(size, 0x40 + 3 + 7)) g_updateIndices(buffer, data, size);
}

template<class Fn>
bool InstallUploadCheck(void* library, Hook::MemoryPatcher& patcher, const char* name,
                        const std::array<std::uint32_t, 4>& prologue, Fn checked, Fn& original) {
    void* target = dlsym(library, name);
    if (!target || std::memcmp(target, prologue.data(), sizeof(prologue)) != 0) {
        ENGINE_LOGE("RenderQueue: unsupported upload function %s", name);
        return false;
    }
    const auto hook = patcher.installHook(reinterpret_cast<std::uintptr_t>(target),
                                          reinterpret_cast<std::uintptr_t>(checked));
    if (!hook.success) {
        ENGINE_LOGE("RenderQueue: could not hook %s: %s", name, hook.errorMessage.c_str());
        return false;
    }
    original = reinterpret_cast<Fn>(hook.trampolineAddress);
    return true;
}

void InstallUploadChecks(void* library, Hook::MemoryPatcher& patcher) {
    g_renderQueue = reinterpret_cast<RenderQueueStorage**>(dlsym(library, "renderQueue"));
    g_flushQueue = reinterpret_cast<decltype(g_flushQueue)>(dlsym(library, "_ZN11RenderQueue5FlushEv"));
    g_lockQueue = reinterpret_cast<decltype(g_lockQueue)>(dlsym(library, "_Z14OS_MutexObtainPv"));
    g_unlockQueue = reinterpret_cast<decltype(g_unlockQueue)>(dlsym(library, "_Z15OS_MutexReleasePv"));
    g_safeVertices = reinterpret_cast<decltype(g_safeVertices)>(dlsym(library, "_ZN14RQVertexBuffer7SetSafeEv"));
    g_safeIndices = reinterpret_cast<decltype(g_safeIndices)>(dlsym(library, "_ZN13RQIndexBuffer7SetSafeEv"));
    if (!g_renderQueue || !g_flushQueue || !g_lockQueue || !g_unlockQueue || !g_safeVertices || !g_safeIndices) {
        ENGINE_LOGE("RenderQueue: upload checks require the GTA 2.10 arm64 queue symbols");
        return;
    }
    constexpr std::array<std::uint32_t, 4> kPrologue{0xa9bd57f6, 0xa9014ff4, 0xa9027bfd, 0x910083fd};
    constexpr std::array<std::uint32_t, 4> kIndexUpdate{0xf81c0ff7, 0xa90157f6, 0xa9024ff4, 0xa9037bfd};
    int installed = 0;
    installed += InstallUploadCheck(library, patcher, "_ZN14RQVertexBuffer3SetEPKvjPFvvE", kPrologue,
                                    &CopyVerticesChecked, g_copyVertices);
    installed += InstallUploadCheck(library, patcher, "_ZN13RQIndexBuffer3SetEPKvj", kPrologue,
                                    &CopyIndicesChecked, g_copyIndices);
    installed += InstallUploadCheck(library, patcher, "_ZN14RQVertexBuffer6UpdateEPKvj", kPrologue,
                                    &UpdateVerticesChecked, g_updateVertices);
    installed += InstallUploadCheck(library, patcher, "_ZN13RQIndexBuffer6UpdateEPKvj", kIndexUpdate,
                                    &UpdateIndicesChecked, g_updateIndices);
    ENGINE_LOGI("RenderQueue: %d/4 inline mesh upload checks installed", installed);
}

void SkipStorySaveOnAndroidPause(void* library, Hook::MemoryPatcher& patcher) {
    const auto event = reinterpret_cast<std::uintptr_t>(dlsym(library, "_Z19OS_ApplicationEvent11OSEventTypePv"));
    // GTA 2.10 arm64 ProcessEvents sends event 8 from NVEvent on pause. Its
    // SaveGameForPause(3, null) calls RemovePickupObjects while the game thread
    // is still rendering. A multiplayer session has no story game to save.
    // Keep the original timer, audio and graphics lifecycle.
    constexpr auto kSaveCallOffset = 0x54u;
    constexpr std::array<std::uint32_t, 3> kSaveCall{0x320007e0, 0xaa1f03e1, 0x97ec544c};
    constexpr std::uint32_t kNop = 0xd503201f;
    if (!event || std::memcmp(reinterpret_cast<const void*>(event + kSaveCallOffset - 8),
                             kSaveCall.data(), sizeof(kSaveCall)) != 0 ||
        !patcher.patchAddress(event, kSaveCallOffset, reinterpret_cast<const std::uint8_t*>(&kNop), sizeof(kNop))) {
        ENGINE_LOGE("Android pause: could not skip the story autosave");
        return;
    }
    ENGINE_LOGI("Android pause: multiplayer skips the story autosave");
}

std::uint64_t (*g_arraysGetId)(std::uint64_t) = nullptr;
void* (*g_firstAtomic)(void*) = nullptr;

std::uint64_t SortKey(const void* slot) {
    void* entity = *static_cast<void* const*>(slot);
    if (!entity) return 0;
    void* object = *reinterpret_cast<void**>(static_cast<unsigned char*>(entity) + 0x20);
    if (!object) return 0;
    void* atomic = object;
    if (*static_cast<unsigned char*>(object) != 1) {
        atomic = g_firstAtomic(object);
        if (!atomic) return 0;
    }
    void* geometry = *reinterpret_cast<void**>(static_cast<unsigned char*>(atomic) + 0x30);
    if (!geometry) return 0;
    void* instance = *reinterpret_cast<void**>(static_cast<unsigned char*>(geometry) + 0x98);
    if (!instance) return 0;
    std::uint64_t handle = 0;
    std::memcpy(&handle, static_cast<unsigned char*>(instance) + 0x5c, sizeof(handle));
    if (handle == 0) {
        static int reported = 0;
        if (reported < 10) {
            ++reported;
            const int model = *reinterpret_cast<std::int16_t*>(static_cast<unsigned char*>(entity) + 50);
            ENGINE_LOGE("RenderGuard: LOD entity %p model %d has no vertex array; sorted as id 0", entity, model);
        }
        return 0;
    }
    return g_arraysGetId(handle);
}

// Same result convention as the original (cset lo / csinv ls): -1 when a < b, 1 when a > b, 0 when equal.
int SafeSortLods(const void* a, const void* b) {
    const std::uint64_t ka = SortKey(a);
    const std::uint64_t kb = SortKey(b);
    if (ka < kb) return -1;
    return ka > kb ? 1 : 0;
}

// Desktop CMultiplayerSA.cpp:1276-1282 provides 255 CVehicleStructure slots.
// Android 2.10 folds the count into four instructions in SetupCommonData.
// Keep the allocator, flag initialization, cursor and native teardown together.
// ARM64 CPools::Initialise and the OpenGL vehicle pipeline inline the native
// allocator. Keep its constructor, allocator, slot flags, cursor and teardown.
struct NativePoolLayout {
    std::size_t storage, flags, header, loop;
    std::uint32_t stride, count;
};

void SetAllocationSize(std::uint32_t* code, std::uint32_t bytes) {
    // A MOVZ/MOVK pair holds an exact byte count. For a lone instruction the
    // allocator can round up to a 64 KiB boundary; the pool retains its exact count.
    if ((code[1] & 0xffe0001fu) == 0x72a00000u) {
        code[0] = 0x52800000u | ((bytes & 0xffff) << 5);
        code[1] = 0x72a00000u | ((bytes >> 16) << 5);
    } else if (bytes <= 0xffff) {
        code[0] = 0x52800000u | (bytes << 5);
    } else {
        code[0] = 0x52a00000u | (((bytes + 0xffff) >> 16) << 5);
    }
}

template <std::size_t Bytes, std::size_t N>
void ResizeNativePools(void* library, Hook::MemoryPatcher& patcher, const char* initializer,
                       const char* firstPool, std::uint32_t signature, const NativePoolLayout (&layouts)[N]) {
    auto* code = static_cast<const std::uint8_t*>(dlsym(library, initializer));
    auto** pool = static_cast<void**>(dlsym(library, firstPool));
    if (!code || !pool || *pool) return;
    std::uint32_t hash = 2166136261u;
    for (std::size_t i = 0; i < Bytes; ++i) hash = (hash ^ code[i]) * 16777619u;
    if (hash != signature) {
        ENGINE_LOGE("Unsupported GTA pool initializer %s", initializer);
        return;
    }
    std::array<std::uint32_t, Bytes / 4> words;
    std::memcpy(words.data(), code, Bytes);
    for (const auto& layout : layouts) {
        const auto size = layout.count;
        SetAllocationSize(&words[layout.storage / 4], size * layout.stride);
        SetAllocationSize(&words[layout.flags / 4], size);
        // x8 packs the 32-bit capacity below a signed -1 allocation cursor.
        words[layout.header / 4] = 0x92800008u | ((~size & 0xffffu) << 5);
        words[layout.header / 4 + 1] = 0xf2a00008u | ((size >> 16) << 5);
        // The original loop reloads x9 (the bitmap) twice without changing it.
        // Reuse that second load for the count; x11 is dead here in these initializers.
        words[layout.loop / 4 - 5] = 0xb940126bu; // ldr w11, [x19, #16]
        words[layout.loop / 4] = 0xeb0b011fu;     // cmp x8, x11
    }
    if (patcher.patchAddress(reinterpret_cast<std::uintptr_t>(code), 0,
                             reinterpret_cast<const std::uint8_t*>(words.data()), Bytes))
        ENGINE_LOGI("GTA startup pools: expanded %zu pools in %s", N, initializer);
}

void ExpandWorldPools(void* library, Hook::MemoryPatcher& patcher) {
    // CGameSA.cpp:1165-1182 and Client/sdk/game/Common.h. Strides are from the
    // mobile allocations; mobile already provides 140 peds, 110 vehicles and 3500 dummies.
    static constexpr NativePoolLayout world[] = {
        {0x018, 0x028, 0x040, 0x09c, 16, 90000},  // single-sector links
        {0x0bc, 0x0cc, 0x0dc, 0x138, 24, 74800},  // double-sector links
        {0x158, 0x164, 0x170, 0x1cc, 40, 72600},  // sector entry nodes
        {0x320, 0x330, 0x340, 0x39c, 96, 32000},  // buildings
        {0x3bc, 0x3cc, 0x3d8, 0x434, 544, 1200},  // objects
        {0x4ec, 0x4fc, 0x50c, 0x568, 56, 30000},  // collision models
        {0x588, 0x598, 0x5a4, 0x600, 168, 5000},  // tasks
        {0x620, 0x62c, 0x638, 0x694, 88, 5000},  // events
    };
    ResizeNativePools<0x9e0>(library, patcher, "_ZN6CPools10InitialiseEv",
        "_ZN6CPools25ms_pPtrNodeSingleLinkPoolE", 0x5cacac08u, world);
    static constexpr NativePoolLayout materials[] = {
        {0x02c, 0x038, 0x044, 0x0a0, 16, 16000},  // vehicle environment materials
        {0x0c0, 0x0cc, 0x0d8, 0x134, 12, 4000},  // vehicle environment atomics
        {0x154, 0x160, 0x16c, 0x1c8, 16, 16000},  // vehicle specular materials
    };
    ResizeNativePools<0x1ec>(library, patcher, "_ZN24CCustomCarEnvMapPipeline10CreatePipeEv",
        "_ZN24CCustomCarEnvMapPipeline24m_gEnvMapPipeMatDataPoolE", 0x17c41fa5u, materials);
}

void ExpandInstanceLinks(void* library, Hook::MemoryPatcher& patcher) {
    // CGameSA.cpp:1184-1188. GTA ARM64 stores 24 bytes per CLink<CEntity*>.
    auto init = reinterpret_cast<std::uintptr_t>(dlsym(library, "_ZN10CStreaming5Init2Ev"));
    auto* list = static_cast<std::uint8_t*>(dlsym(library, "_ZN10CStreaming20ms_rwObjectInstancesE"));
    if (!init || !list || *reinterpret_cast<void**>(list + 0x60)) return;
    constexpr std::size_t begin = 0x450; // Init2 + 0x450 = 0x557748
    constexpr std::size_t end = 0x50c;   // first instruction after list construction
    std::array<std::uint32_t, (end - begin) / 4> code;
    std::memcpy(code.data(), reinterpret_cast<const void*>(init + begin), sizeof(code));
    const auto original = reinterpret_cast<const std::uint8_t*>(code.data());
    std::uint32_t hash = 2166136261u;
    for (std::size_t i = 0; i < sizeof(code); ++i) hash = (hash ^ original[i]) * 16777619u;
    if (hash != 0xc109f0b4u) return;

    constexpr unsigned count = 30000, bytes = count * 24;
    constexpr unsigned allocation = (bytes + 0xffffu) & ~0xffffu;
    auto word = [&](unsigned pc) -> std::uint32_t& { return code[(pc - 0x557748) / 4]; };
    // Leave the native allocation call and list wiring in place. Its alignment
    // NOP gives one slot to load the large offset base in caller-saved w1.
    std::memmove(&word(0x557764), &word(0x557760), 0x5577e0 - 0x557760);
    word(0x557748) = 0x52a00000u | ((allocation >> 16) << 5); // movz w0, ..., lsl #16
    word(0x557760) = word(0x557748) | 1u;                     // movz w1, ..., lsl #16
    struct TailOffset { unsigned pc, reg, fromEnd; };
    constexpr TailOffset offsets[] = {
        {0x557764, 9, 8}, {0x557768, 10, 24}, {0x55776c, 11, 16}, {0x55777c, 12, 48},
        {0x557788, 13, 32}, {0x557798, 15, 40}, {0x5577bc, 12, 56},
    };
    for (const auto& tail : offsets)
        word(tail.pc) = 0x51000020u | ((allocation - bytes + tail.fromEnd) << 10) | tail.reg; // sub wN, w1, #delta
    word(0x5577a0) = 0x1280000au | ((count - 3) << 5); // mov w10, #-(count-2)
    word(0x5577b8) = 0x5280000bu | ((count - 2) << 5); // mov w11, #(count-2)
    word(0x557800) += 1u << 5; // cbnz w10: loop body moved forward one instruction
    if (patcher.patchAddress(init, begin, reinterpret_cast<const std::uint8_t*>(code.data()), sizeof(code)))
        ENGINE_LOGI("GTA streaming instance capacity: %u", count);
}

void ExpandMatrixList(void* library, Hook::MemoryPatcher& patcher) {
    // CGameSA.cpp:1190-1191. CPlaceable::InitMatrixArray() inlines
    // gMatrixList.Init(900); the same engine exports the variable-size member.
    auto init = reinterpret_cast<std::uintptr_t>(dlsym(library, "_ZN10CPlaceable15InitMatrixArrayEv"));
    auto member = reinterpret_cast<std::uintptr_t>(dlsym(library, "_ZN15CMatrixLinkList4InitEi"));
    auto* list = static_cast<std::uint8_t*>(dlsym(library, "gMatrixList"));
    if (!init || !member || !list || *reinterpret_cast<void**>(list + 0x270)) return;
    if (member + 0xe34 != init) return;
    const auto* native = reinterpret_cast<const std::uint32_t*>(init);
    const std::uint32_t signature[] = {0xf81e0ff3, 0xa9017bfd, 0x910043fd, 0x528d9913};
    if (std::memcmp(native, signature, sizeof(signature)) ||
        native[0x40 / 4] != 0x90001b08 || native[0x44 / 4] != 0xf946cd08) return;
    // Resolve the identical GOT slot from the same 4 KiB PC page. Tail calling
    // the member keeps the allocator, constructor, array cookie and destructor paired.
    constexpr std::uint32_t code[] = {
        0x90001b00, 0xf946cc00, 0x5281c201, 0x17fffc70, // gMatrixList, 3600, b Init(int)
    };
    if (patcher.patchAddress(init, 0, reinterpret_cast<const std::uint8_t*>(code), sizeof(code)))
        ENGINE_LOGI("GTA matrix capacity: 3600");
}

void ExpandVehicleModelPool(void* library, Hook::MemoryPatcher& patcher) {
    const auto setup = reinterpret_cast<std::uintptr_t>(dlsym(library, "_ZN17CVehicleModelInfo15SetupCommonDataEv"));
    auto** pool = static_cast<void**>(dlsym(library, "_ZN17CVehicleModelInfo17CVehicleStructure11m_pInfoPoolE"));
    if (!setup || !pool || *pool) {
        ENGINE_LOGE("Vehicle model pool: setup missing or pool already initialized");
        return;
    }

    constexpr std::size_t first = 0xd0;  // 0x553ce8
    constexpr std::size_t last = 0x144;  // 0x553d5c
    std::array<std::uint32_t, (last - first) / 4 + 1> words;
    std::memcpy(words.data(), reinterpret_cast<const void*>(setup + first), sizeof(words));
    struct Change { std::size_t offset; std::uint32_t before, after; };
    constexpr Change changes[] = {
        // 255 * 0x360 = 220320 bytes. 0x38000 is the next size encodable in a single MOV.
        {0xd0, 0x52951800, 0x32110be0},  // backing array: 43200 -> 229376 bytes
        {0xdc, 0x52800640, 0x52801fe0},  // flags allocation: 50 -> 255 bytes
        {0xe8, 0x929ff9a8, 0x929fe008},  // pool count in W8; upper word keeps cursor -1
        {0x144, 0xf100c91f, 0xf103fd1f}, // initialize every flag through slot 254
    };
    for (const auto& change : changes) {
        auto& word = words[(change.offset - first) / 4];
        if (word != change.before) {
            ENGINE_LOGE("Vehicle model pool: unsupported SetupCommonData at +%zx", change.offset);
            return;
        }
        word = change.after;
    }
    // No game thread exists at JNI_OnLoad. Apply the four changes in one write
    // so a failed page-protection change leaves the original initializer intact.
    if (patcher.patchAddress(setup, first, reinterpret_cast<const std::uint8_t*>(words.data()), sizeof(words)))
        ENGINE_LOGI("Vehicle model pool: capacity set to 255 before streaming");
}

}  // namespace

void InstallStreamingCapacities() {
    void* library = dlopen("libGTASA.so", RTLD_NOLOAD | RTLD_NOW);
    if (!library) return;
    Hook::MemoryPatcher patcher;
    ExpandWorldPools(library, patcher);
    ExpandInstanceLinks(library, patcher);
    ExpandMatrixList(library, patcher);
    ExpandVehicleModelPool(library, patcher);
    const auto constructor = reinterpret_cast<std::uintptr_t>(dlsym(library, "_ZN11RenderQueueC1Ev"));

    // GTA 2.10 arm64 queues each vertex/index upload as one inline command.
    // After Flush(), Update() copies the full payload even if it is larger than
    // the whole queue. A material mesh in the server's Blade replacement can
    // exceed 1 MiB by itself and overwrite RenderWare's allocator state.
    // Both malloc and the queue end pointer use these same two size constants.
    constexpr std::uintptr_t kSizeOffset = 0x34;
    constexpr std::array<std::uint32_t, 3> kOriginal{
        0x320c03f5, // mov w21, #0x100000
        0x320d03f6, // mov w22, #0x80000
        0x9a9512c0, // csel x0, x22, x21, ne
    };
    constexpr std::array<std::uint32_t, 2> kExpanded{
        0x320903f5, // mov w21, #0x800000
        0x320903f6, // mov w22, #0x800000
    };
    if (!constructor || std::memcmp(reinterpret_cast<const void*>(constructor + kSizeOffset),
                                    kOriginal.data(), sizeof(kOriginal)) != 0) {
        ENGINE_LOGE("RenderQueue: unsupported constructor for expanded mesh upload capacity");
        dlclose(library);
        return;
    }

    if (patcher.patchAddress(constructor, kSizeOffset,
                             reinterpret_cast<const std::uint8_t*>(kExpanded.data()), sizeof(kExpanded))) {
        ENGINE_LOGI("RenderQueue: expanded mesh upload capacity to 8 MiB");
    }
    InstallUploadChecks(library, patcher);
    SkipStorySaveOnAndroidPause(library, patcher);
    dlclose(library);
}

bool InstallRenderGuard() {
    static bool done = false;
    static bool ok = false;
    if (done) return ok;
    done = true;
    void* handle = dlopen("libGTASA.so", RTLD_NOLOAD | RTLD_GLOBAL);
    if (!handle) return false;
    g_arraysGetId = reinterpret_cast<std::uint64_t (*)(std::uint64_t)>(dlsym(handle, "_Z15emu_ArraysGetIDy"));
    g_firstAtomic = reinterpret_cast<void* (*)(void*)>(dlsym(handle, "_Z14GetFirstAtomicP7RpClump"));
    void* target = dlsym(handle, "_Z8sortLODsPKvS0_");
    if (!g_arraysGetId || !g_firstAtomic || !target) {
        ENGINE_LOGE("RenderGuard: symbols missing ids=%p atomic=%p sort=%p",
                    reinterpret_cast<void*>(g_arraysGetId), reinterpret_cast<void*>(g_firstAtomic), target);
        return false;
    }
    static Hook::MemoryPatcher patcher;
    const auto hooked = patcher.installHook(reinterpret_cast<uintptr_t>(target),
                                            reinterpret_cast<uintptr_t>(&SafeSortLods));
    if (!hooked.success) {
        ENGINE_LOGE("RenderGuard: hook failed: %s", hooked.errorMessage.c_str());
        return false;
    }
    ok = true;
    ENGINE_LOGI("RenderGuard: sortLODs replaced (zero vertex-array handle sorts as 0)");
    return true;
}
#else
void InstallStreamingCapacities() {}
bool InstallRenderGuard() { return true; }
#endif

}  // namespace WorldRPG::Game
