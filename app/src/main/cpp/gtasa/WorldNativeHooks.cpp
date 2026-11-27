#include "WorldNativeHooks.h"

#include "../shared/WorldNativeBridge.h"
#include "../shared/EngineWorld.h"

#include <android/log.h>
#include <dlfcn.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <vector>

#define WORLD_HOOK_LOGI(...) __android_log_print(ANDROID_LOG_INFO, "WorldRPG_WorldHooks", __VA_ARGS__)
#define WORLD_HOOK_LOGW(...) __android_log_print(ANDROID_LOG_WARN, "WorldRPG_WorldHooks", __VA_ARGS__)

namespace WorldRPG::Gta {
namespace {

struct CVector {
    float x, y, z;
};

struct RwRGBA {
    uint8_t r, g, b, a;
};

// Verified ARM64 CPool header layout (matches RenderGuard.cpp:265-270):
// +0x00: void* m_pObjects
// +0x08: uint8_t* m_byteMap (bit 7 = 0x80 => free slot)
// +0x10: int32_t m_nSize
// +0x14: int32_t m_nFirstFree
struct NativePoolHeader {
    void* objects;
    uint8_t* byteMap;
    int32_t size;
    int32_t firstFree;
};

// Pool symbol table aligned 1:1 with EngineWorld.cpp kPools[0..19].
const char* kPoolSymbols[Models::kPoolCount] = {
    "_ZN6CPools16ms_pBuildingPoolE",
    "_ZN6CPools11ms_pPedPoolE",
    "_ZN6CPools14ms_pObjectPoolE",
    "_ZN6CPools13ms_pDummyPoolE",
    "_ZN6CPools15ms_pVehiclePoolE",
    "_ZN6CPools16ms_pColModelPoolE",
    "_ZN6CPools12ms_pTaskPoolE",
    "_ZN6CPools13ms_pEventPoolE",
    "_ZN6CPools21ms_pTaskAllocatorPoolE",
    "_ZN6CPools23ms_pPedIntelligencePoolE",
    "_ZN6CPools20ms_pPedAttractorPoolE",
    "_ZN6CPools21ms_pEntryInfoNodePoolE",
    "_ZN6CPools17ms_pNodeRoutePoolE",
    "_ZN6CPools19ms_pPatrolRoutePoolE",
    "_ZN6CPools18ms_pPointRoutePoolE",
    "_ZN6CPools25ms_pPtrNodeDoubleLinkPoolE",
    "_ZN6CPools25ms_pPtrNodeSingleLinkPoolE",
    "_ZN24CCustomCarEnvMapPipeline24m_gEnvMapPipeMatDataPoolE",
    "_ZN24CCustomCarEnvMapPipeline27m_gEnvMapPipeAtmDataPoolE",
    "_ZN24CCustomCarEnvMapPipeline25m_gSpecMapPipeMatDataPoolE",
};

void* g_lib = nullptr;
NativePoolHeader** g_pools[Models::kPoolCount]{};

using AddExplosionFn = bool (*)(void* victim, void* creator, int type, CVector pos, uint32_t time, uint8_t makeSound, float camShake, uint8_t isVisible);
using StartFireFn = void* (*)(void* mgr, CVector pos, float size, uint8_t unknown, void* creator, uint32_t time, int8_t numGenerations, uint8_t arg);
using ExtinguishPointFn = void (*)(void* mgr, CVector pos, float radius);
using AddPointLightFn = void (*)(uint8_t type, CVector pos, CVector dir, float radius, float r, float g, float b, uint8_t fogType, bool extraShadows, void* entity);
using LoadSceneFn = void (*)(const CVector& pos);
using FxBloodFn = void (*)(void* fx, CVector* pos, CVector* dir, int count, float brightness);
using FxBulletImpactFn = void (*)(void* fx, CVector* pos, CVector* dir, int smokeSize, int sparkCount, float smokeIntensity);
using FxSparksFn = void (*)(void* fx, CVector* pos, CVector* dir, float force, int count, CVector* across, uint8_t blur, float spread, float life);
using FxSimpleVecFn = void (*)(void* fx, CVector* pos, CVector* dir);
using FxDebrisFn = void (*)(void* fx, CVector* pos, RwRGBA* color, float scale, int count);

AddExplosionFn g_addExplosion = nullptr;
void* g_fireManager = nullptr;
StartFireFn g_startFire = nullptr;
ExtinguishPointFn g_extinguishPoint = nullptr;
AddPointLightFn g_addPointLight = nullptr;
LoadSceneFn g_loadScene = nullptr;
LoadSceneFn g_loadSceneCollision = nullptr;

void* g_fx = nullptr;
FxBloodFn g_fxBlood = nullptr;
FxBloodFn g_fxWood = nullptr;
FxBulletImpactFn g_fxBulletImpact = nullptr;
FxSparksFn g_fxSparks = nullptr;
FxSimpleVecFn g_fxTyreBurst = nullptr;
FxSimpleVecFn g_fxPunchImpact = nullptr;
FxDebrisFn g_fxDebris = nullptr;
FxDebrisFn g_fxGlass = nullptr;

template <typename T>
T ResolveAny(void* handle, std::initializer_list<const char*> names) {
    if (!handle) return nullptr;
    for (const char* name : names) {
        if (void* sym = dlsym(handle, name)) return reinterpret_cast<T>(sym);
    }
    return nullptr;
}

int NativeGetPoolCapacity(int pool) {
    if (pool < 0 || pool >= Models::kPoolCount || !g_pools[pool]) return -1;
    NativePoolHeader* header = *g_pools[pool];
    if (!header || header->size < 0 || header->size > 2000000) return -1;
    return header->size;
}

int NativeGetPoolUsedCapacity(int pool) {
    if (pool < 0 || pool >= Models::kPoolCount || !g_pools[pool]) return -1;
    NativePoolHeader* header = *g_pools[pool];
    if (!header || !header->byteMap || header->size <= 0 || header->size > 2000000) return -1;
    int used = 0;
    for (int32_t i = 0; i < header->size; ++i) {
        if ((header->byteMap[i] & 0x80u) == 0) ++used;
    }
    return used;
}

bool NativeCreateExplosion(float x, float y, float z, int type, bool makeSound, float camShake, bool damaging) {
    (void)damaging;
    if (!g_addExplosion) return false;
    g_addExplosion(nullptr, nullptr, type, CVector{x, y, z}, 0, makeSound ? 1 : 0, camShake, 1);
    return true;
}

bool NativeCreateFire(float x, float y, float z, float size) {
    if (!g_fireManager || !g_startFire) return false;
    g_startFire(g_fireManager, CVector{x, y, z}, size, 1, nullptr, 15000, 1, 1);
    return true;
}

bool NativeExtinguishFire(float x, float y, float z, float radius) {
    if (!g_fireManager || !g_extinguishPoint) return false;
    g_extinguishPoint(g_fireManager, CVector{x, y, z}, radius);
    return true;
}

bool NativeAddPointLight(int type, float x, float y, float z, float dx, float dy, float dz,
                         float radius, float r, float g, float b, bool fog, bool shadow) {
    if (!g_addPointLight) return false;
    g_addPointLight(static_cast<uint8_t>(type), CVector{x, y, z}, CVector{dx, dy, dz},
                    radius, r, g, b, fog ? 1 : 0, shadow, nullptr);
    return true;
}

bool NativePreloadWorldArea(float x, float y, float z, int mode) {
    const CVector pos{x, y, z};
    bool ran = false;
    if ((mode == 0 || mode == 2) && g_loadSceneCollision) {
        g_loadSceneCollision(pos);
        ran = true;
    }
    if ((mode == 0 || mode == 1) && g_loadScene) {
        g_loadScene(pos);
        ran = true;
    }
    return ran;
}

bool NativeFxAddBlood(float x, float y, float z, float dx, float dy, float dz, int count, float brightness) {
    if (!g_fx || !g_fxBlood) return false;
    CVector p{x, y, z}, d{dx, dy, dz};
    g_fxBlood(g_fx, &p, &d, count, brightness);
    return true;
}

bool NativeFxAddBulletImpact(float x, float y, float z, float dx, float dy, float dz, int smokeSize, int sparkCount, float smokeIntensity) {
    if (!g_fx || !g_fxBulletImpact) return false;
    CVector p{x, y, z}, d{dx, dy, dz};
    g_fxBulletImpact(g_fx, &p, &d, smokeSize, sparkCount, smokeIntensity);
    return true;
}

bool NativeFxAddSparks(float x, float y, float z, float dx, float dy, float dz, float force, int count,
                       float acrossX, float acrossY, float acrossZ, bool blur, float spread, float life) {
    if (!g_fx || !g_fxSparks) return false;
    CVector p{x, y, z}, d{dx, dy, dz}, across{acrossX, acrossY, acrossZ};
    g_fxSparks(g_fx, &p, &d, force, count, &across, blur ? 1 : 0, spread, life);
    return true;
}

bool NativeFxAddTyreBurst(float x, float y, float z, float dx, float dy, float dz) {
    if (!g_fx || !g_fxTyreBurst) return false;
    CVector p{x, y, z}, d{dx, dy, dz};
    g_fxTyreBurst(g_fx, &p, &d);
    return true;
}

bool NativeFxAddWood(float x, float y, float z, float dx, float dy, float dz, int count, float brightness) {
    if (!g_fx || !g_fxWood) return false;
    CVector p{x, y, z}, d{dx, dy, dz};
    g_fxWood(g_fx, &p, &d, count, brightness);
    return true;
}

bool NativeFxAddPunchImpact(float x, float y, float z, float dx, float dy, float dz) {
    if (!g_fx || !g_fxPunchImpact) return false;
    CVector p{x, y, z}, d{dx, dy, dz};
    g_fxPunchImpact(g_fx, &p, &d);
    return true;
}

bool NativeFxAddDebris(float x, float y, float z, uint8_t r, uint8_t g, uint8_t b, uint8_t a, float scale, int count) {
    if (!g_fx || !g_fxDebris) return false;
    CVector p{x, y, z};
    RwRGBA col{r, g, b, a};
    g_fxDebris(g_fx, &p, &col, scale, count);
    return true;
}

bool NativeFxAddGlass(float x, float y, float z, uint8_t r, uint8_t g, uint8_t b, uint8_t a, float scale, int count) {
    if (!g_fx || !g_fxGlass) return false;
    CVector p{x, y, z};
    RwRGBA col{r, g, b, a};
    g_fxGlass(g_fx, &p, &col, scale, count);
    return true;
}

}  // namespace

void InstallWorldNativeHooks(void* gtasaHandle) {
    g_lib = gtasaHandle;
    if (!g_lib) return;

    int resolvedPools = 0;
    for (int i = 0; i < Models::kPoolCount; ++i) {
        g_pools[i] = reinterpret_cast<NativePoolHeader**>(dlsym(g_lib, kPoolSymbols[i]));
        if (g_pools[i]) ++resolvedPools;
    }

    g_addExplosion = ResolveAny<AddExplosionFn>(g_lib, {
        "_ZN10CExplosion12AddExplosionEP7CEntityS1_14eExplosionType7CVectorjhfh",
        "_ZN10CExplosion12AddExplosionEP7CEntityS1_14eExplosionType7CVectorjbff",
    });
    g_fireManager = dlsym(g_lib, "gFireManager");
    g_startFire = ResolveAny<StartFireFn>(g_lib, {
        "_ZN12CFireManager9StartFireE7CVectorfhP7CEntityjah",
        "_ZN12CFireManager9StartFireE7CVectorfhP7CEntityjha",
        "_ZN12CFireManager9StartFireE7CVectorffP7CEntityjha",
    });
    g_extinguishPoint = ResolveAny<ExtinguishPointFn>(g_lib, {
        "_ZN12CFireManager15ExtinguishPointE7CVectorf",
    });
    g_addPointLight = ResolveAny<AddPointLightFn>(g_lib, {
        "_ZN12CPointLights8AddLightEh7CVectorS0_ffffhbP7CEntity",
        "_ZN12CPointLights8AddLightEh7CVectorS0_ffffhP7CEntity",
    });
    g_loadScene = ResolveAny<LoadSceneFn>(g_lib, {
        "_ZN10CStreaming9LoadSceneERK7CVector",
    });
    g_loadSceneCollision = ResolveAny<LoadSceneFn>(g_lib, {
        "_ZN10CStreaming18LoadSceneCollisionERK7CVector",
    });

    g_fx = dlsym(g_lib, "g_fx");
    g_fxBlood = ResolveAny<FxBloodFn>(g_lib, {"_ZN4Fx_c8AddBloodEP7CVectorS1_if"});
    g_fxWood = ResolveAny<FxBloodFn>(g_lib, {"_ZN4Fx_c7AddWoodEP7CVectorS1_if"});
    g_fxBulletImpact = ResolveAny<FxBulletImpactFn>(g_lib, {"_ZN4Fx_c15AddBulletImpactEP7CVectorS1_iif"});
    g_fxSparks = ResolveAny<FxSparksFn>(g_lib, {"_ZN4Fx_c9AddSparksEP7CVectorS1_fiS1_hff"});
    g_fxTyreBurst = ResolveAny<FxSimpleVecFn>(g_lib, {"_ZN4Fx_c12AddTyreBurstEP7CVectorS1_"});
    g_fxPunchImpact = ResolveAny<FxSimpleVecFn>(g_lib, {"_ZN4Fx_c14AddPunchImpactEP7CVectorS1_"});
    g_fxDebris = ResolveAny<FxDebrisFn>(g_lib, {"_ZN4Fx_c9AddDebrisEP7CVectorP6RwRGBAfi"});
    g_fxGlass = ResolveAny<FxDebrisFn>(g_lib, {"_ZN4Fx_c8AddGlassEP7CVectorP6RwRGBAfi"});

    WorldNative::Hooks hooks;
    hooks.getPoolCapacity = &NativeGetPoolCapacity;
    hooks.getPoolUsedCapacity = &NativeGetPoolUsedCapacity;
    hooks.createExplosion = &NativeCreateExplosion;
    hooks.createFire = &NativeCreateFire;
    hooks.extinguishFire = &NativeExtinguishFire;
    hooks.addPointLight = &NativeAddPointLight;
    hooks.preloadWorldArea = &NativePreloadWorldArea;
    hooks.fxAddBlood = &NativeFxAddBlood;
    hooks.fxAddBulletImpact = &NativeFxAddBulletImpact;
    hooks.fxAddSparks = &NativeFxAddSparks;
    hooks.fxAddTyreBurst = &NativeFxAddTyreBurst;
    hooks.fxAddWood = &NativeFxAddWood;
    hooks.fxAddPunchImpact = &NativeFxAddPunchImpact;
    hooks.fxAddDebris = &NativeFxAddDebris;
    hooks.fxAddGlass = &NativeFxAddGlass;
    WorldNative::RegisterHooks(hooks);

    WORLD_HOOK_LOGI("World native hooks installed: pools=%d/%d explosion=%d fire=%d light=%d loadScene=%d fx=%d",
                    resolvedPools, Models::kPoolCount,
                    g_addExplosion ? 1 : 0, (g_fireManager && g_startFire) ? 1 : 0,
                    g_addPointLight ? 1 : 0, (g_loadScene || g_loadSceneCollision) ? 1 : 0,
                    g_fx ? 1 : 0);
}

}  // namespace WorldRPG::Gta
