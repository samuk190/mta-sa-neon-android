// Server and client objects as CObjects in GTA's world, streamed by distance
// from WorldObjectFeed. Desktop: CClientObject::Create / Destroy
// (CClientObject.cpp 656-718) with CObjectSA::CObjectSA / ~CObjectSA
// (game_sa/CObjectSA.cpp 31-167), streamed by CClientStreamer.
//
// libGTASA 2.10 arm64 (every entry from dlsym; a missing one is a DIAG and no objects):
//   CObject::Create(int, bool)          _ZN7CObject6CreateEib (0x53c19c)
//   CWorld::Add / Remove                _ZN6CWorld3AddEP7CEntity / _ZN6CWorld6RemoveEP7CEntity
//   CPlaceable::SetMatrix               _ZN10CPlaceable9SetMatrixERK7CMatrix
//   CMatrix::SetRotate(x, y, z)         _ZN7CMatrix9SetRotateEfff
//   CEntity::CreateRwObject             _ZN7CEntity14CreateRwObjectEv
// Fixed offsets (each read or written by libGTASA 2.10):
//   CEntity +0x18 CMatrix*, +0x20 RwObject*, +0x32 m_nModelIndex,
//   +0x28 entity flags (CObject::SetIsStatic writes bit 2; bit 0 bUsesCollision,
//   bit 7 bIsVisible, bit 10 bStreamingDontDelete as on PC), +0x4B area (interior);
//   CObject +0x1A0 m_nObjectType (CObject::CanBeDeleted reads it; desktop writes 6),
//   object pool stride 0x220 (CObject::Create).
#include "WorldObjects.h"

#include "../shared/WorldObjectFeed.h"
#include "../shared/ServerWorld.h"

#include <algorithm>
#include <cstring>
#include <unordered_map>
#include <vector>

#ifdef __ANDROID__
#include <android/log.h>
#include <dlfcn.h>
#define WO_LOGI(...) __android_log_print(ANDROID_LOG_INFO, "WorldRPGNeon", __VA_ARGS__)
#else
#include <cstdio>
#define WO_LOGI(...) (std::printf(__VA_ARGS__), std::printf("\n"))
#endif

namespace WorldRPG::Game::WorldObjects {
namespace {

using WorldRPG::WorldObjects::State;

constexpr float kStreamIn = 250.f;   // CClientStreamer default for objects
constexpr float kStreamOut = 275.f;  // hysteresis so a border object does not flicker
constexpr size_t kMinMaxLive = 250;  // stock CObject pool fallback (RenderGuard expands to 1200)
constexpr size_t kMaxMaxLive = 800;  // leave headroom in the expanded 1200-slot CObject pool
constexpr size_t kPoolHeadroom = 200;
constexpr int kCreatesPerFrame = 4;
constexpr int kMaxModel = 19999;

struct Api {
    bool resolved = false;
    bool ok = false;
    void* (*create)(int, bool) = nullptr;
    void (*add)(void*) = nullptr;
    void (*remove)(void*) = nullptr;
    void (*setMatrix)(void*, const void*) = nullptr;
    void (*setRotate)(void*, float, float, float) = nullptr;
    void (*createRwObject)(void*) = nullptr;
    void (*requestModel)(int, int) = nullptr;
    unsigned char* streamingInfo = nullptr;
    void* placeableVtable = nullptr;
    void** objectPool = nullptr;        // CPools::ms_pObjectPool (CPool<CObject>*)
    unsigned char* camera = nullptr;    // TheCamera (a CPlaceable)
    void (*setAlpha)(void*, int) = nullptr;
};

Api& Engine() {
    static Api api;
    if (api.resolved) return api;
    api.resolved = true;
#ifdef __ANDROID__
    void* lib = dlopen("libGTASA.so", RTLD_NOLOAD | RTLD_GLOBAL);
    if (!lib) return api;
    auto sym = [&](const char* name) { return dlsym(lib, name); };
    api.create = reinterpret_cast<void* (*)(int, bool)>(sym("_ZN7CObject6CreateEib"));
    api.add = reinterpret_cast<void (*)(void*)>(sym("_ZN6CWorld3AddEP7CEntity"));
    api.remove = reinterpret_cast<void (*)(void*)>(sym("_ZN6CWorld6RemoveEP7CEntity"));
    api.setMatrix = reinterpret_cast<void (*)(void*, const void*)>(sym("_ZN10CPlaceable9SetMatrixERK7CMatrix"));
    api.setRotate = reinterpret_cast<void (*)(void*, float, float, float)>(sym("_ZN7CMatrix9SetRotateEfff"));
    api.createRwObject = reinterpret_cast<void (*)(void*)>(sym("_ZN7CEntity14CreateRwObjectEv"));
    api.requestModel = reinterpret_cast<void (*)(int, int)>(sym("_ZN10CStreaming12RequestModelEii"));
    api.streamingInfo = static_cast<unsigned char*>(sym("_ZN10CStreaming16ms_aInfoForModelE"));
    if (auto* vtable = static_cast<unsigned char*>(sym("_ZTV10CPlaceable"))) api.placeableVtable = vtable + 16;
    api.objectPool = static_cast<void**>(sym("_ZN6CPools14ms_pObjectPoolE"));
    api.camera = static_cast<unsigned char*>(sym("TheCamera"));
    api.setAlpha = reinterpret_cast<void (*)(void*, int)>(sym("_ZN7CEntity16SetRwObjectAlphaEi"));
    api.ok = api.create && api.add && api.remove && api.setMatrix && api.setRotate && api.createRwObject &&
             api.requestModel && api.streamingInfo && api.objectPool;
    if (!api.ok) WO_LOGI("DIAG WorldObjects: a libGTASA entry is missing; objects are not created");
#endif
    return api;
}

// CStreaming::ms_aInfoForModel stride 0x14, load state at +0x10 (1 = loaded), as GameEngine reads it.
bool ModelLoaded(const Api& api, int model) {
    return model >= 0 && model <= kMaxModel && api.streamingInfo[static_cast<size_t>(model) * 0x14u + 0x10u] == 1;
}

// CMatrix laid out as RwMatrix (right, flags, up, pad, at, pad, pos, pad), then the
// attach pointer and owner flag; zeroed space beyond what SetRotate writes.
struct MatrixBuffer {
    alignas(16) unsigned char bytes[0x80];
};

void BuildMatrix(const Api& api, const State& s, MatrixBuffer& out) {
    std::memset(out.bytes, 0, sizeof(out.bytes));
    api.setRotate(out.bytes, s.rx, s.ry, s.rz);
    std::memcpy(out.bytes + 0x30, &s.x, 4);
    std::memcpy(out.bytes + 0x34, &s.y, 4);
    std::memcpy(out.bytes + 0x38, &s.z, 4);
}

template <typename T>
T& Field(void* object, size_t offset) {
    return *reinterpret_cast<T*>(static_cast<unsigned char*>(object) + offset);
}

struct Live {
    void* entity = nullptr;
    State state;
};

constexpr size_t kObjectStride = 0x220;  // CObject::Create's pool stride
constexpr uint8_t kStreamedType = 6;     // m_nObjectType this streamer writes (CObjectSA.cpp:126)

// The pointer is still the object this streamer created: its CPool<CObject> slot
// (objects, byte map: bit 7 = free) is in use and it carries our type and model.
// A slot GTA freed or reused for another object fails, and is only forgotten.
bool StillOurs(const Api& api, void* entity, int model) {
    auto* pool = static_cast<unsigned char*>(*api.objectPool);
    if (!pool || !entity) return false;
    auto* objects = *reinterpret_cast<unsigned char**>(pool);
    auto* byteMap = *reinterpret_cast<unsigned char**>(pool + 8);
    const int size = *reinterpret_cast<int*>(pool + 16);
    const auto at = static_cast<unsigned char*>(entity);
    if (!objects || !byteMap || at < objects) return false;
    const size_t offset = static_cast<size_t>(at - objects);
    if (offset % kObjectStride || offset / kObjectStride >= static_cast<size_t>(size)) return false;
    if (byteMap[offset / kObjectStride] & 0x80) return false;
    if (api.placeableVtable && *reinterpret_cast<void**>(entity) == api.placeableVtable) return false;
    // CEntity +0x32 m_nModelIndex in libGTASA 2.10 arm64 (CEntity::SetModelIndexNoCreate
    // 0x4ca7e0 `strh w1, [x19, #0x32]`, CEntity::IsVisible 0x4cd2e0, CObject::Init 0x53b8c8).
    return Field<uint8_t>(entity, 0x1A0) == kStreamedType && Field<int16_t>(entity, 0x32) == model;
}

// CPhysicalSA::SetFrozen: CPhysical +0x64 flag word, bit 13 bDontApplySpeed, bit 2
// bDisableFriction (as GameEngine's SetPhysicalFrozen for peds).
void SetFrozen(void* entity, bool frozen) {
    auto& flags = Field<uint32_t>(entity, 0x64);
    constexpr uint32_t kDontApplySpeed = 1u << 13, kDisableFriction = 1u << 2;
    if (frozen) flags |= kDontApplySpeed | kDisableFriction;
    else flags &= ~(kDontApplySpeed | kDisableFriction);
}

// The streaming position: the camera (CClientStreamer follows it), else the given point.
void StreamCenter(const Api& api, float& x, float& y, float& z) {
    if (!api.camera) return;
    const auto* matrix = Field<unsigned char*>(api.camera, 0x18);
    const auto* pos = matrix ? matrix + 0x30 : api.camera + 0x08;  // CPlaceable matrix or placement
    std::memcpy(&x, pos, 4);
    std::memcpy(&y, pos + 4, 4);
    std::memcpy(&z, pos + 8, 4);
}

std::unordered_map<uint32_t, Live> g_live;
std::unordered_map<void*, uint32_t> g_byEntity;
std::vector<State> g_states;
uint64_t g_revision = 0;
int g_frame = 0;

bool SameTransform(const State& a, const State& b) {
    return a.x == b.x && a.y == b.y && a.z == b.z && a.rx == b.rx && a.ry == b.ry && a.rz == b.rz;
}

void ApplyFlags(const Api& api, void* entity, const State& s, bool updateAlpha) {
    auto& flags = Field<uint64_t>(entity, 0x28);
    flags = s.collisions ? (flags | 1u) : (flags & ~uint64_t{1});
    flags |= uint64_t{1} << 10;  // bStreamingDontDelete (CObjectSA.cpp:139)
    // Alpha 0 hides the object and keeps its collision (bIsVisible, bit 7).
    flags = s.alpha == 0 ? (flags & ~(uint64_t{1} << 7)) : (flags | (uint64_t{1} << 7));
    Field<uint8_t>(entity, 0x4B) = s.interior;
    SetFrozen(entity, s.frozen);
    if (updateAlpha && api.setAlpha && Field<void*>(entity, 0x20) && s.alpha > 0) api.setAlpha(entity, s.alpha);
}

void Destroy(const Api& api, Live& live) {
    void* entity = live.entity;
    live.entity = nullptr;
    if (!entity) return;
    g_byEntity.erase(entity);
    // ~CObjectSA: a body already torn down to CPlaceable is neither removed nor deleted
    // (CWorld::Remove calls a vtable slot CPlaceable does not have).
    if (api.placeableVtable && *reinterpret_cast<void**>(entity) == api.placeableVtable) return;
    api.remove(entity);
    auto** vtable = *reinterpret_cast<void (***)(void*)>(entity);
    vtable[1](entity);  // deleting destructor (CObject::Create's own cleanup uses slot +0x8)
}

void* Create(const Api& api, const State& s) {
    if (!ModelLoaded(api, s.model)) {
        api.requestModel(s.model, 0x12);
        return nullptr;
    }
    void* entity = api.create(s.model, true);
    if (!entity) return nullptr;
    MatrixBuffer matrix;
    BuildMatrix(api, s, matrix);
    api.setMatrix(entity, matrix.bytes);
    Field<uint8_t>(entity, 0x1A0) = kStreamedType;  // CObjectSA.cpp:126
    // Create the RW clump before CWorld::Add so CWorld::Add updates RwMatrix + RwFrame immediately.
    if (!Field<void*>(entity, 0x20)) api.createRwObject(entity);
    api.add(entity);
    ApplyFlags(api, entity, s, true);
    return entity;
}

void Move(const Api& api, Live& live, const State& s) {
    const bool transformChanged = !SameTransform(live.state, s);
    const bool alphaChanged = live.state.alpha != s.alpha;
    if (transformChanged) {
        MatrixBuffer matrix;
        BuildMatrix(api, s, matrix);
        // CEntitySA::SetMatrix / SetPosition (CEntitySA.cpp 380-405): out of the world,
        // new matrix, back in (CWorld::Add runs CMatrix::UpdateRwMatrix + CEntity::UpdateRwFrame).
        api.remove(live.entity);
        api.setMatrix(live.entity, matrix.bytes);
        api.add(live.entity);
    }
    ApplyFlags(api, live.entity, s, alphaChanged);
}

}  // namespace

void Pulse(float cameraX, float cameraY, float cameraZ) {
    Api& api = Engine();
    if (!api.ok) return;
    ++g_frame;
    StreamCenter(api, cameraX, cameraY, cameraZ);
    // Objects GTA deleted or reused under us are forgotten, never touched again.
    for (auto it = g_live.begin(); it != g_live.end();) {
        if (StillOurs(api, it->second.entity, it->second.state.model)) {
            ++it;
            continue;
        }
        g_byEntity.erase(it->second.entity);
        it = g_live.erase(it);
    }
    const uint64_t revision = WorldRPG::WorldObjects::Revision();
    const bool changed = revision != g_revision;
    if (!changed && g_frame % 10) return;
    if (changed) {
        WorldRPG::WorldObjects::Copy(g_states);
        g_revision = revision;
    }
    const uint16_t dimension = WorldRPG::ServerWorld::LocalDimension();
    // Wanted: in the local dimension and near the camera, nearest first, at most kMaxLive.
    std::vector<std::pair<float, const State*>> wanted;
    // One state per element; Copy lists the client source last, so a script's own
    // transform of a server object wins over the server's.
    std::unordered_map<uint32_t, const State*> latest;
    for (const State& s : g_states) latest[s.id] = &s;
    for (const auto& entry : latest) {
        const State& s = *entry.second;
        if (!s.visible || (!s.allDimensions && s.dimension != dimension) || s.model < 0 || s.model > kMaxModel) continue;
        const float dx = s.x - cameraX, dy = s.y - cameraY, dz = s.z - cameraZ;
        const float d2 = dx * dx + dy * dy + dz * dz;
        const auto current = g_live.find(s.id);
        const bool live = current != g_live.end() && current->second.entity;
        const float limit = live ? kStreamOut : kStreamIn;
        if (d2 <= limit * limit) wanted.emplace_back(d2, &s);
    }
    std::sort(wanted.begin(), wanted.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    size_t maxLive = kMinMaxLive;
    if (auto* pool = static_cast<unsigned char*>(*api.objectPool)) {
        const int poolSize = *reinterpret_cast<int*>(pool + 16);
        if (poolSize > static_cast<int>(kPoolHeadroom)) {
            maxLive = std::clamp(static_cast<size_t>(poolSize) - kPoolHeadroom, kMinMaxLive, kMaxMaxLive);
        }
    }
    if (wanted.size() > maxLive) wanted.resize(maxLive);
    std::unordered_map<uint32_t, const State*> keep;
    for (const auto& [d2, s] : wanted) keep[s->id] = s;
    // Stream out what is gone, far, or changed model.
    for (auto it = g_live.begin(); it != g_live.end();) {
        const auto found = keep.find(it->first);
        if (found == keep.end() || found->second->model != it->second.state.model) {
            Destroy(api, it->second);
            it = g_live.erase(it);
        } else {
            ++it;
        }
    }
    // Move what changed; create what is new, a few per frame.
    int creates = 0;
    for (const auto& [d2, s] : wanted) {
        auto found = g_live.find(s->id);
        if (found != g_live.end() && found->second.entity) {
            const State& was = found->second.state;
            if (!SameTransform(was, *s) || was.collisions != s->collisions || was.interior != s->interior ||
                was.frozen != s->frozen || was.alpha != s->alpha) {
                Move(api, found->second, *s);
                found->second.state = *s;
            }
            continue;
        }
        if (creates >= kCreatesPerFrame) continue;
        ++creates;
        if (void* entity = Create(api, *s)) {
            g_live[s->id] = {entity, *s};
            g_byEntity[entity] = s->id;
        }
    }
}

void* EntityOf(uint32_t id) {
    const auto found = g_live.find(id);
    return found == g_live.end() ? nullptr : found->second.entity;
}

uint32_t ElementOf(void* entity) {
    const auto found = g_byEntity.find(entity);
    return found == g_byEntity.end() ? 0xFFFFFFFFu : found->second;
}

void Reset() {
    Api& api = Engine();
    for (auto& [id, live] : g_live)
        if (api.ok) Destroy(api, live);
    g_live.clear();
    g_byEntity.clear();
    g_states.clear();
    g_revision = 0;
}

}  // namespace WorldRPG::Game::WorldObjects
