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

}  // namespace

void InstallRenderQueueCapacity() {
    void* library = dlopen("libGTASA.so", RTLD_NOLOAD | RTLD_NOW);
    if (!library) return;
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

    Hook::MemoryPatcher patcher;
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
void InstallRenderQueueCapacity() {}
bool InstallRenderGuard() { return true; }
#endif

}  // namespace WorldRPG::Game
