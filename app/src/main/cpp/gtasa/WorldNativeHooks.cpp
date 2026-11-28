#include "WorldNativeHooks.h"

#include "GameTasks.h"
#include "../shared/EngineWorld.h"
#include "../shared/WorldNativeBridge.h"

#include <android/log.h>
#include <dlfcn.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <vector>

#define WORLD_HOOK_LOGI(...) __android_log_print(ANDROID_LOG_INFO, "WorldRPG_WorldHooks", __VA_ARGS__)
#define WORLD_HOOK_LOGW(...) __android_log_print(ANDROID_LOG_WARN, "WorldRPG_WorldHooks", __VA_ARGS__)

namespace WorldRPG::Gta {
namespace {

constexpr float kRadToDeg = 57.29577951308232f;

struct CVector {
    float x, y, z;
};

struct RwRGBA {
    uint8_t r, g, b, a;
};

// Verified ARM64 CPool header layout (matches RenderGuard.cpp:265-270 and
// _ZN9CBuildingnwEm / _ZN6CDummynwEm in libGTASA.so 2.10 arm64):
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

// Verified libGTASA.so 2.10 arm64 CEntity / CPlaceable offsets:
//   +0x08: CSimpleTransform { float x, y, z, heading } (verified _ZNK7CEntity14GetBoundCentreER7CVector)
//   +0x18: CMatrix* m_matrix (vRight +0x00, vUp +0x10, vAt +0x20, vPos +0x30)
//   +0x32: int16_t m_nModelIndex (verified _ZN7CEntity14DeleteRwObjectEv)
//   +0x4B: uint8_t m_nAreaCode
//   +0x5A: uint8_t m_nType (low 3 bits: 1=building, 2=vehicle, 3=ped, 4=object, 5=dummy)
//   CBuilding & CDummy pool element stride: 0x60 (verified _ZN9CBuildingnwEm & _ZN6CDummynwEm)
constexpr size_t kBuildingOrDummyStride = 0x60;
constexpr size_t kEntityTransformOff = 0x08;
constexpr size_t kEntityMatrixPtrOff = 0x18;
constexpr size_t kEntityModelIdOff = 0x32;
constexpr size_t kEntityAreaCodeOff = 0x4B;
constexpr size_t kEntityTypeOff = 0x5A;

// Verified libGTASA.so 2.10 arm64 CGarages::aGarages layout:
//   stride = 0xF4 (verified _ZN8CGarages12IsGarageOpenEs)
//   +0x00: float x, y, z; +0x1C: Height, Width, Depth; +0x28: Left, Right, Front, Back
//   (verified _ZN7CGarage19IsPointInsideGarageE7CVector & desktop CGarageSA.h)
constexpr size_t kGarageStride = 0xF4;
constexpr int kMaxGarages = 50;

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

// Verified ARM64 function pointer signatures (cross-checked with nm -D + c++filt on libGTASA.so 2.10).
using AddExplosionFn = bool (*)(void* victim, void* creator, int type, CVector pos, uint32_t time, uint8_t makeSound, float camShake, uint8_t isVisible);
using StartFireFn = void* (*)(void* mgr, CVector pos, float size, uint8_t unknown, void* creator, uint32_t time, int8_t numGenerations, uint8_t arg);
using ExtinguishPointFn = void (*)(void* mgr, CVector pos, float radius);
using AddPointLightFn = void (*)(uint8_t type, CVector pos, CVector dir, float radius, float r, float g, float b, uint8_t fogType, bool extraShadows, void* entity);
using LoadSceneFn = void (*)(const CVector& pos);
using CreateSwatRopeFn = void (*)(const CVector& pos);
using TestSphereFn = void* (*)(CVector centre, float radius, void* ignoredEntity, bool checkBuildings, bool checkVehicles, bool checkPeds, bool checkObjects, bool checkDummies, bool cameraIgnore);
using WorldEntityFn = void (*)(void* entity);

using FxBloodFn = void (*)(void* fx, CVector& pos, CVector& dir, int count, float brightness);
using FxBulletImpactFn = void (*)(void* fx, CVector& pos, CVector& dir, int smokeSize, int sparkCount, float smokeIntensity);
using FxSparksFn = void (*)(void* fx, CVector& pos, CVector& dir, float force, int count, CVector across, uint8_t blur, float spread, float life);
using FxTyreBurstFn = void (*)(void* fx, CVector& pos, CVector& dir);
using FxPunchImpactFn = void (*)(void* fx, CVector& pos, CVector& dir, int material);
using FxDebrisFn = void (*)(void* fx, CVector& pos, RwRGBA& color, float scale, int count);

AddExplosionFn g_addExplosion = nullptr;
void* g_fireManager = nullptr;
StartFireFn g_startFire = nullptr;
ExtinguishPointFn g_extinguishPoint = nullptr;
AddPointLightFn g_addPointLight = nullptr;
LoadSceneFn g_loadScene = nullptr;
LoadSceneFn g_loadSceneCollision = nullptr;
CreateSwatRopeFn g_createSwatRope = nullptr;
TestSphereFn g_testSphere = nullptr;
WorldEntityFn g_worldRemove = nullptr;
WorldEntityFn g_worldAdd = nullptr;
uint8_t* g_garages = nullptr;
int32_t* g_numGarages = nullptr;

void* g_fx = nullptr;
FxBloodFn g_fxBlood = nullptr;
FxBloodFn g_fxWood = nullptr;
FxBulletImpactFn g_fxBulletImpact = nullptr;
FxSparksFn g_fxSparks = nullptr;
FxTyreBurstFn g_fxTyreBurst = nullptr;
FxPunchImpactFn g_fxPunchImpact = nullptr;
FxDebrisFn g_fxDebris = nullptr;
FxDebrisFn g_fxGlass = nullptr;

struct RemovedNativeEntity {
    void* entity = nullptr;
    uint16_t model = 0;
    float x = 0.f;
    float y = 0.f;
    float z = 0.f;
    uint8_t areaCode = 0;
};

std::mutex g_removedMutex;
std::vector<RemovedNativeEntity> g_removedEntities;

template <typename T>
T ResolveAny(void* handle, std::initializer_list<const char*> names) {
    if (!handle) return nullptr;
    for (const char* name : names) {
        if (void* sym = dlsym(handle, name)) return reinterpret_cast<T>(sym);
    }
    return nullptr;
}

bool ReadEntityPosition(const uint8_t* ent, float& x, float& y, float& z) {
    if (!ent) return false;
    const uint8_t* mat = nullptr;
    std::memcpy(&mat, ent + kEntityMatrixPtrOff, sizeof(mat));
    if (mat) {
        std::memcpy(&x, mat + 0x30, sizeof(float));
        std::memcpy(&y, mat + 0x34, sizeof(float));
        std::memcpy(&z, mat + 0x38, sizeof(float));
    } else {
        std::memcpy(&x, ent + kEntityTransformOff + 0x0, sizeof(float));
        std::memcpy(&y, ent + kEntityTransformOff + 0x4, sizeof(float));
        std::memcpy(&z, ent + kEntityTransformOff + 0x8, sizeof(float));
    }
    return std::isfinite(x) && std::isfinite(y) && std::isfinite(z);
}

void ReadEntityRotationDeg(const uint8_t* ent, float& rxDeg, float& ryDeg, float& rzDeg) {
    rxDeg = ryDeg = rzDeg = 0.f;
    if (!ent) return;
    const uint8_t* mat = nullptr;
    std::memcpy(&mat, ent + kEntityMatrixPtrOff, sizeof(mat));
    if (mat) {
        // Matches desktop CWorldSA::TestSphereAgainstWorld + ConvertMatrixToEulerAngles (negated).
        // CMatrix layout: right (+0x00), up (+0x10), at (+0x20).
        float right[3], up[3], at[3];
        std::memcpy(right, mat + 0x00, sizeof(right));
        std::memcpy(up, mat + 0x10, sizeof(up));
        std::memcpy(at, mat + 0x20, sizeof(at));
        const float rx = std::asin(std::clamp(up[2], -1.f, 1.f));
        float ry = 0.f, rz = 0.f;
        if (std::fabs(up[2]) < 0.99999f) {
            ry = std::atan2(-right[2], at[2]);
            rz = std::atan2(-up[0], up[1]);
        } else {
            rz = std::atan2(right[1], right[0]);
        }
        rxDeg = -rx * kRadToDeg;
        ryDeg = -ry * kRadToDeg;
        rzDeg = -rz * kRadToDeg;
    } else {
        float heading = 0.f;
        std::memcpy(&heading, ent + kEntityTransformOff + 0xC, sizeof(float));
        if (std::isfinite(heading)) rzDeg = -heading * kRadToDeg;
    }
}

bool MatchesRemovalSphere(float ex, float ey, float ez, float x, float y, float z, float radius) {
    const float dx = ex - x;
    const float dy = ey - y;
    const float dz = ez - z;
    return (dx * dx + dy * dy + dz * dz) <= (radius * radius);
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
    GameTasks::Defer([x, y, z, type, makeSound, camShake]() {
        if (g_addExplosion) {
            g_addExplosion(nullptr, nullptr, type, CVector{x, y, z}, 0, makeSound ? 1 : 0, camShake, 1);
        }
        return true;
    });
    return true;
}

bool NativeCreateFire(float x, float y, float z, float size) {
    if (!g_fireManager || !g_startFire) return false;
    GameTasks::Defer([x, y, z, size]() {
        if (g_fireManager && g_startFire) {
            g_startFire(g_fireManager, CVector{x, y, z}, size, 1, nullptr, 15000, 1, 1);
        }
        return true;
    });
    return true;
}

bool NativeExtinguishFire(float x, float y, float z, float radius) {
    if (!g_fireManager || !g_extinguishPoint) return false;
    GameTasks::Defer([x, y, z, radius]() {
        if (g_fireManager && g_extinguishPoint) {
            g_extinguishPoint(g_fireManager, CVector{x, y, z}, radius);
        }
        return true;
    });
    return true;
}

bool NativeAddPointLight(int type, float x, float y, float z, float dx, float dy, float dz,
                         float radius, float r, float g, float b, bool fog, bool shadow) {
    if (!g_addPointLight) return false;
    GameTasks::Defer([type, x, y, z, dx, dy, dz, radius, r, g, b, fog, shadow]() {
        if (g_addPointLight) {
            g_addPointLight(static_cast<uint8_t>(type), CVector{x, y, z}, CVector{dx, dy, dz},
                            radius, r, g, b, fog ? 1 : 0, shadow, nullptr);
        }
        return true;
    });
    return true;
}

bool NativePreloadWorldArea(float x, float y, float z, int mode) {
    const bool wantCol = (mode == 0 || mode == 2) && g_loadSceneCollision != nullptr;
    const bool wantMod = (mode == 0 || mode == 1) && g_loadScene != nullptr;
    if (!wantCol && !wantMod) return false;
    GameTasks::Defer([x, y, z, wantCol, wantMod]() {
        const CVector pos{x, y, z};
        if (wantCol && g_loadSceneCollision) g_loadSceneCollision(pos);
        if (wantMod && g_loadScene) g_loadScene(pos);
        return true;
    });
    return true;
}

bool NativeCreateSwatRope(float x, float y, float z, uint32_t durationMs) {
    (void)durationMs;
    if (!g_createSwatRope) return false;
    GameTasks::Defer([x, y, z]() {
        if (g_createSwatRope) {
            const CVector pos{x, y, z};
            g_createSwatRope(pos);
        }
        return true;
    });
    return true;
}

bool NativeTestSphereAgainstWorld(float x, float y, float z, float radius,
                                  bool checkBuildings, bool checkVehicles, bool checkPeds,
                                  bool checkObjects, bool checkDummies, bool cameraIgnore,
                                  WorldNative::SphereTestResult& out) {
    out = {};
    if (!g_testSphere) return false;
    bool ok = GameTasks::RunQuery([&]() {
        void* hit = g_testSphere(CVector{x, y, z}, radius, nullptr,
                                 checkBuildings, checkVehicles, checkPeds,
                                 checkObjects, checkDummies, cameraIgnore);
        if (!hit) return;
        const auto* ent = reinterpret_cast<const uint8_t*>(hit);
        out.hit = true;
        int16_t modelId = 0;
        std::memcpy(&modelId, ent + kEntityModelIdOff, sizeof(modelId));
        out.model = modelId >= 0 ? static_cast<uint16_t>(modelId) : 0;
        ReadEntityPosition(ent, out.x, out.y, out.z);
        ReadEntityRotationDeg(ent, out.rxDeg, out.ryDeg, out.rzDeg);
        out.entityType = static_cast<uint8_t>(ent[kEntityTypeOff] & 0x7u);
    });
    return ok;
}

bool NativeGetGarageGeometry(int garageId, float pos[3], float sizeWDH[3], float bbox[4]) {
    if (!g_garages || garageId < 0 || garageId >= kMaxGarages) return false;
    if (g_numGarages && *g_numGarages > 0 && garageId >= *g_numGarages) return false;
    const uint8_t* base = g_garages + static_cast<size_t>(garageId) * kGarageStride;
    float height = 0.f, width = 0.f, depth = 0.f;
    std::memcpy(pos, base + 0x00, sizeof(float) * 3);
    std::memcpy(&height, base + 0x1C, sizeof(float));
    std::memcpy(&width, base + 0x20, sizeof(float));
    std::memcpy(&depth, base + 0x24, sizeof(float));
    sizeWDH[0] = width;
    sizeWDH[1] = depth;
    sizeWDH[2] = height;
    std::memcpy(bbox, base + 0x28, sizeof(float) * 4);
    return std::isfinite(pos[0]) && std::isfinite(pos[1]) && std::isfinite(pos[2]);
}

int NativeRemoveWorldModel(uint16_t model, float radius, float x, float y, float z, int8_t interior) {
    if (!g_worldRemove || (!g_pools[0] && !g_pools[3])) return -1;
    if (radius < 0.f || !std::isfinite(radius)) return 0;
    int removed = 0;
    const bool ran = GameTasks::Run([&]() {
        const int poolIndices[2] = {0, 3};  // buildings, dummies
        std::lock_guard<std::mutex> lock(g_removedMutex);
        for (int poolIdx : poolIndices) {
            if (!g_pools[poolIdx]) continue;
            NativePoolHeader* pool = *g_pools[poolIdx];
            if (!pool || !pool->objects || !pool->byteMap || pool->size <= 0 || pool->size > 200000) continue;
            auto* base = reinterpret_cast<uint8_t*>(pool->objects);
            for (int32_t i = 0; i < pool->size; ++i) {
                if ((pool->byteMap[i] & 0x80u) != 0) continue;
                uint8_t* ent = base + static_cast<size_t>(i) * kBuildingOrDummyStride;
                int16_t entModel = 0;
                std::memcpy(&entModel, ent + kEntityModelIdOff, sizeof(entModel));
                if (entModel < 0 || static_cast<uint16_t>(entModel) != model) continue;
                const uint8_t areaCode = ent[kEntityAreaCodeOff];
                if (interior >= 0 && static_cast<int>(areaCode) != static_cast<int>(interior)) continue;
                float ex = 0.f, ey = 0.f, ez = 0.f;
                if (!ReadEntityPosition(ent, ex, ey, ez)) continue;
                if (!MatchesRemovalSphere(ex, ey, ez, x, y, z, radius)) continue;
                const bool alreadyRemoved = std::any_of(
                    g_removedEntities.begin(), g_removedEntities.end(),
                    [ent](const RemovedNativeEntity& e) { return e.entity == ent; });
                if (alreadyRemoved) continue;
                g_worldRemove(ent);
                g_removedEntities.push_back(RemovedNativeEntity{ent, model, ex, ey, ez, areaCode});
                ++removed;
            }
        }
    });
    return ran ? removed : 0;
}

int NativeRestoreWorldModel(uint16_t model, float radius, float x, float y, float z, int8_t interior) {
    if (!g_worldAdd) return -1;
    if (radius < 0.f || !std::isfinite(radius)) return 0;
    int restored = 0;
    const bool ran = GameTasks::Run([&]() {
        std::lock_guard<std::mutex> lock(g_removedMutex);
        for (auto it = g_removedEntities.begin(); it != g_removedEntities.end();) {
            const bool modelMatch = (it->model == model);
            const bool intMatch = (interior < 0 || static_cast<int>(it->areaCode) == static_cast<int>(interior));
            if (modelMatch && intMatch && MatchesRemovalSphere(it->x, it->y, it->z, x, y, z, radius)) {
                if (it->entity) g_worldAdd(it->entity);
                it = g_removedEntities.erase(it);
                ++restored;
            } else {
                ++it;
            }
        }
    });
    return ran ? restored : 0;
}

int NativeRestoreAllWorldModels() {
    if (!g_worldAdd) return -1;
    int restored = 0;
    const bool ran = GameTasks::Run([&]() {
        std::lock_guard<std::mutex> lock(g_removedMutex);
        for (const auto& entry : g_removedEntities) {
            if (entry.entity) {
                g_worldAdd(entry.entity);
                ++restored;
            }
        }
        g_removedEntities.clear();
    });
    return ran ? restored : 0;
}

bool NativeFxAddBlood(float x, float y, float z, float dx, float dy, float dz, int count, float brightness) {
    if (!g_fx || !g_fxBlood) return false;
    GameTasks::Defer([x, y, z, dx, dy, dz, count, brightness]() {
        if (g_fx && g_fxBlood) {
            CVector p{x, y, z}, d{dx, dy, dz};
            g_fxBlood(g_fx, p, d, count, brightness);
        }
        return true;
    });
    return true;
}

bool NativeFxAddBulletImpact(float x, float y, float z, float dx, float dy, float dz, int smokeSize, int sparkCount, float smokeIntensity) {
    if (!g_fx || !g_fxBulletImpact) return false;
    GameTasks::Defer([x, y, z, dx, dy, dz, smokeSize, sparkCount, smokeIntensity]() {
        if (g_fx && g_fxBulletImpact) {
            CVector p{x, y, z}, d{dx, dy, dz};
            g_fxBulletImpact(g_fx, p, d, smokeSize, sparkCount, smokeIntensity);
        }
        return true;
    });
    return true;
}

bool NativeFxAddSparks(float x, float y, float z, float dx, float dy, float dz, float force, int count,
                       float acrossX, float acrossY, float acrossZ, bool blur, float spread, float life) {
    if (!g_fx || !g_fxSparks) return false;
    GameTasks::Defer([x, y, z, dx, dy, dz, force, count, acrossX, acrossY, acrossZ, blur, spread, life]() {
        if (g_fx && g_fxSparks) {
            CVector p{x, y, z}, d{dx, dy, dz}, across{acrossX, acrossY, acrossZ};
            g_fxSparks(g_fx, p, d, force, count, across, blur ? 1 : 0, spread, life);
        }
        return true;
    });
    return true;
}

bool NativeFxAddTyreBurst(float x, float y, float z, float dx, float dy, float dz) {
    if (!g_fx || !g_fxTyreBurst) return false;
    GameTasks::Defer([x, y, z, dx, dy, dz]() {
        if (g_fx && g_fxTyreBurst) {
            CVector p{x, y, z}, d{dx, dy, dz};
            g_fxTyreBurst(g_fx, p, d);
        }
        return true;
    });
    return true;
}

bool NativeFxAddWood(float x, float y, float z, float dx, float dy, float dz, int count, float brightness) {
    if (!g_fx || !g_fxWood) return false;
    GameTasks::Defer([x, y, z, dx, dy, dz, count, brightness]() {
        if (g_fx && g_fxWood) {
            CVector p{x, y, z}, d{dx, dy, dz};
            g_fxWood(g_fx, p, d, count, brightness);
        }
        return true;
    });
    return true;
}

bool NativeFxAddPunchImpact(float x, float y, float z, float dx, float dy, float dz) {
    if (!g_fx || !g_fxPunchImpact) return false;
    GameTasks::Defer([x, y, z, dx, dy, dz]() {
        if (g_fx && g_fxPunchImpact) {
            CVector p{x, y, z}, d{dx, dy, dz};
            g_fxPunchImpact(g_fx, p, d, 0);
        }
        return true;
    });
    return true;
}

bool NativeFxAddDebris(float x, float y, float z, uint8_t r, uint8_t g, uint8_t b, uint8_t a, float scale, int count) {
    if (!g_fx || !g_fxDebris) return false;
    GameTasks::Defer([x, y, z, r, g, b, a, scale, count]() {
        if (g_fx && g_fxDebris) {
            CVector p{x, y, z};
            RwRGBA col{r, g, b, a};
            g_fxDebris(g_fx, p, col, scale, count);
        }
        return true;
    });
    return true;
}

bool NativeFxAddGlass(float x, float y, float z, uint8_t r, uint8_t g, uint8_t b, uint8_t a, float scale, int count) {
    if (!g_fx || !g_fxGlass) return false;
    GameTasks::Defer([x, y, z, r, g, b, a, scale, count]() {
        if (g_fx && g_fxGlass) {
            CVector p{x, y, z};
            RwRGBA col{r, g, b, a};
            g_fxGlass(g_fx, p, col, scale, count);
        }
        return true;
    });
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
    });
    g_fireManager = dlsym(g_lib, "gFireManager");
    g_startFire = ResolveAny<StartFireFn>(g_lib, {
        "_ZN12CFireManager9StartFireE7CVectorfhP7CEntityjah",
    });
    g_extinguishPoint = ResolveAny<ExtinguishPointFn>(g_lib, {
        "_ZN12CFireManager15ExtinguishPointE7CVectorf",
    });
    g_addPointLight = ResolveAny<AddPointLightFn>(g_lib, {
        "_ZN12CPointLights8AddLightEh7CVectorS0_ffffhbP7CEntity",
    });
    g_loadScene = ResolveAny<LoadSceneFn>(g_lib, {
        "_ZN10CStreaming9LoadSceneERK7CVector",
    });
    g_loadSceneCollision = ResolveAny<LoadSceneFn>(g_lib, {
        "_ZN10CStreaming18LoadSceneCollisionERK7CVector",
    });
    g_createSwatRope = ResolveAny<CreateSwatRopeFn>(g_lib, {
        "_ZN6CRopes20CreateRopeForSwatPedERK7CVector",
    });
    g_testSphere = ResolveAny<TestSphereFn>(g_lib, {
        "_ZN6CWorld22TestSphereAgainstWorldE7CVectorfP7CEntitybbbbbb",
    });
    g_worldRemove = ResolveAny<WorldEntityFn>(g_lib, {"_ZN6CWorld6RemoveEP7CEntity"});
    g_worldAdd = ResolveAny<WorldEntityFn>(g_lib, {"_ZN6CWorld3AddEP7CEntity"});
    g_garages = reinterpret_cast<uint8_t*>(dlsym(g_lib, "_ZN8CGarages8aGaragesE"));
    g_numGarages = reinterpret_cast<int32_t*>(dlsym(g_lib, "_ZN8CGarages10NumGaragesE"));

    g_fx = dlsym(g_lib, "g_fx");
    g_fxBlood = ResolveAny<FxBloodFn>(g_lib, {"_ZN4Fx_c8AddBloodER7CVectorS1_if"});
    g_fxWood = ResolveAny<FxBloodFn>(g_lib, {"_ZN4Fx_c7AddWoodER7CVectorS1_if"});
    g_fxBulletImpact = ResolveAny<FxBulletImpactFn>(g_lib, {"_ZN4Fx_c15AddBulletImpactER7CVectorS1_iif"});
    g_fxSparks = ResolveAny<FxSparksFn>(g_lib, {"_ZN4Fx_c9AddSparksER7CVectorS1_fiS0_hff"});
    g_fxTyreBurst = ResolveAny<FxTyreBurstFn>(g_lib, {"_ZN4Fx_c12AddTyreBurstER7CVectorS1_"});
    g_fxPunchImpact = ResolveAny<FxPunchImpactFn>(g_lib, {"_ZN4Fx_c14AddPunchImpactER7CVectorS1_i"});
    g_fxDebris = ResolveAny<FxDebrisFn>(g_lib, {"_ZN4Fx_c9AddDebrisER7CVectorR6RwRGBAfi"});
    g_fxGlass = ResolveAny<FxDebrisFn>(g_lib, {"_ZN4Fx_c8AddGlassER7CVectorR6RwRGBAfi"});

    WorldNative::Hooks hooks;
    hooks.getPoolCapacity = &NativeGetPoolCapacity;
    hooks.getPoolUsedCapacity = &NativeGetPoolUsedCapacity;
    hooks.createExplosion = &NativeCreateExplosion;
    hooks.createFire = &NativeCreateFire;
    hooks.extinguishFire = &NativeExtinguishFire;
    hooks.addPointLight = &NativeAddPointLight;
    hooks.preloadWorldArea = &NativePreloadWorldArea;
    hooks.createSwatRope = &NativeCreateSwatRope;
    hooks.testSphereAgainstWorld = &NativeTestSphereAgainstWorld;
    hooks.getGarageGeometry = &NativeGetGarageGeometry;
    hooks.removeWorldModel = &NativeRemoveWorldModel;
    hooks.restoreWorldModel = &NativeRestoreWorldModel;
    hooks.restoreAllWorldModels = &NativeRestoreAllWorldModels;
    hooks.fxAddBlood = &NativeFxAddBlood;
    hooks.fxAddBulletImpact = &NativeFxAddBulletImpact;
    hooks.fxAddSparks = &NativeFxAddSparks;
    hooks.fxAddTyreBurst = &NativeFxAddTyreBurst;
    hooks.fxAddWood = &NativeFxAddWood;
    hooks.fxAddPunchImpact = &NativeFxAddPunchImpact;
    hooks.fxAddDebris = &NativeFxAddDebris;
    hooks.fxAddGlass = &NativeFxAddGlass;
    WorldNative::RegisterHooks(hooks);

    const int fxCount = (g_fxBlood ? 1 : 0) + (g_fxWood ? 1 : 0) + (g_fxBulletImpact ? 1 : 0) +
                        (g_fxSparks ? 1 : 0) + (g_fxTyreBurst ? 1 : 0) + (g_fxPunchImpact ? 1 : 0) +
                        (g_fxDebris ? 1 : 0) + (g_fxGlass ? 1 : 0);
    WORLD_HOOK_LOGI(
        "World native hooks installed: pools=%d/%d explosion=%d fire=%d light=%d loadScene=%d "
        "rope=%d sphere=%d worldRemove=%d garages=%d fx=%d/8",
        resolvedPools, Models::kPoolCount,
        g_addExplosion ? 1 : 0, (g_fireManager && g_startFire) ? 1 : 0,
        g_addPointLight ? 1 : 0, (g_loadScene || g_loadSceneCollision) ? 1 : 0,
        g_createSwatRope ? 1 : 0, g_testSphere ? 1 : 0,
        (g_worldRemove && g_worldAdd) ? 1 : 0, g_garages ? 1 : 0,
        g_fx ? fxCount : 0);
}

}  // namespace WorldRPG::Gta
