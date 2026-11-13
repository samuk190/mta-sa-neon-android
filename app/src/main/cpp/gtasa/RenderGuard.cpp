#include "RenderGuard.h"
#include "GameEngine.h"

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
bool InstallRenderGuard() { return true; }
#endif

}  // namespace WorldRPG::Game
