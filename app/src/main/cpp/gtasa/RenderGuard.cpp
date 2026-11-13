#include "RenderGuard.h"
#include "GameEngine.h"

#include <cstdint>
#include <cstring>

#ifdef __ANDROID__
#include <dlfcn.h>
#include "../hook/MemoryPatcher.h"
#endif

// Guard for the SIGSEGV seen on the S25 in sortLODs (emu_ArraysGetID, fault 0x30) under
// CRenderer::RenderEverythingBarRoads. sortLODs (libGTASA 2.10 arm64, 0x4f5104) reads, for each
// entry of CRenderer::ms_aVisibleLodPtrs:
//   entity+0x20 (RwObject) -> first atomic (GetFirstAtomic when the object is a clump, type byte != 1)
//   atomic+0x30 (geometry) -> +0x98 (instance data) -> +0x5c (vertex array handle, 8 bytes)
// and passes the handle to emu_ArraysGetID, which dereferences handle+0x30. A geometry with instance
// data but a zero handle crashes. Before the render, entries in that state are logged (model index at
// entity+50, the field AddEntityToRenderList reads) and removed from the list, so the frame renders
// without them and the log says which model it was.
namespace WorldRPG::Game {

#ifdef __ANDROID__
namespace {

using RenderFn = void (*)();
RenderFn g_originalRender = nullptr;
int* g_lodCount = nullptr;
void** g_lods = nullptr;
void* (*g_firstAtomic)(void*) = nullptr;

bool HandleIsZero(void* entity) {
    auto* raw = static_cast<unsigned char*>(entity);
    void* object = *reinterpret_cast<void**>(raw + 0x20);
    if (!object) return false;
    void* atomic = object;
    if (*static_cast<unsigned char*>(object) != 1) {
        if (!g_firstAtomic) return false;
        atomic = g_firstAtomic(object);
        if (!atomic) return false;
    }
    void* geometry = *reinterpret_cast<void**>(static_cast<unsigned char*>(atomic) + 0x30);
    if (!geometry) return false;
    void* instance = *reinterpret_cast<void**>(static_cast<unsigned char*>(geometry) + 0x98);
    if (!instance) return false;
    std::uint64_t handle = 0;
    std::memcpy(&handle, static_cast<unsigned char*>(instance) + 0x5c, sizeof(handle));
    return handle == 0;
}

void GuardedRender() {
    if (g_lodCount && g_lods) {
        int count = *g_lodCount;
        int kept = 0;
        for (int i = 0; i < count; ++i) {
            void* entity = g_lods[i];
            if (entity && HandleIsZero(entity)) {
                static int reported = 0;
                if (reported < 20) {
                    ++reported;
                    auto* raw = static_cast<unsigned char*>(entity);
                    const int model = *reinterpret_cast<std::int16_t*>(raw + 50);
                    ENGINE_LOGE("RenderGuard: LOD entity %p model %d has no vertex array; skipped this frame",
                                entity, model);
                }
                continue;
            }
            g_lods[kept++] = entity;
        }
        *g_lodCount = kept;
    }
    if (g_originalRender) g_originalRender();
}

}  // namespace

bool InstallRenderGuard() {
    static bool done = false;
    static bool ok = false;
    if (done) return ok;
    done = true;
    void* handle = dlopen("libGTASA.so", RTLD_NOLOAD | RTLD_GLOBAL);
    if (!handle) return false;
    g_lodCount = static_cast<int*>(dlsym(handle, "_ZN9CRenderer19ms_nNoOfVisibleLodsE"));
    g_lods = static_cast<void**>(dlsym(handle, "_ZN9CRenderer18ms_aVisibleLodPtrsE"));
    g_firstAtomic = reinterpret_cast<void* (*)(void*)>(dlsym(handle, "_Z14GetFirstAtomicP7RpClump"));
    void* target = dlsym(handle, "_ZN9CRenderer24RenderEverythingBarRoadsEv");
    if (!g_lodCount || !g_lods || !g_firstAtomic || !target) {
        ENGINE_LOGE("RenderGuard: symbols missing count=%p list=%p atomic=%p render=%p",
                    static_cast<void*>(g_lodCount), static_cast<void*>(g_lods),
                    reinterpret_cast<void*>(g_firstAtomic), target);
        return false;
    }
    static Hook::MemoryPatcher patcher;
    const auto hooked = patcher.installHook(reinterpret_cast<uintptr_t>(target),
                                            reinterpret_cast<uintptr_t>(&GuardedRender));
    if (!hooked.success) {
        ENGINE_LOGE("RenderGuard: hook failed: %s", hooked.errorMessage.c_str());
        return false;
    }
    g_originalRender = reinterpret_cast<RenderFn>(hooked.trampolineAddress);
    ok = true;
    ENGINE_LOGI("RenderGuard: installed on CRenderer::RenderEverythingBarRoads");
    return true;
}
#else
bool InstallRenderGuard() { return true; }
#endif

}  // namespace WorldRPG::Game
