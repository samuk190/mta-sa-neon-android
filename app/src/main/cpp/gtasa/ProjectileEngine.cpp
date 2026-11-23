#include "ProjectileEngine.h"
#include "GameTasks.h"
#include "GameEngine.h"
#include <array>
#include <cstddef>
#include <cstring>
#include <mutex>
#include <unordered_map>
#ifdef __ANDROID__
#include <dlfcn.h>
#endif
namespace WorldRPG::Projectiles {
namespace {
// libGTASA 2.10 arm64: gaProjectileInfo is 1792 bytes, 32 slots of 56;
// ms_apProjectile is 256 bytes. AddProjectile 0x6feaf8-0x6feb18 reads active
// at +28 with stride 56; 0x6fec98/0x6fecac store creator/type at +8/+0;
// 0x6fecf0 stores deadline at +24 (x24 is slot+8), Update 0x6ff3fc loads +24.
struct Info {
    uint32_t type, pad;
    void* creator;
    void* target;
    uint32_t deadline;
    uint8_t active, unused[3];
    Vec3 last;
    uint32_t pad2;
    void* fx;
};
static_assert(sizeof(Info) == 56 && offsetof(Info, deadline) == 24 && offsetof(Info, active) == 28);
struct Api {
    Info* info = nullptr;
    void** objects = nullptr;
    uint32_t* clock = nullptr;
    void** ignore = nullptr;
    bool (*add)(void*, int, Vec3, float, Vec3*, void*) = nullptr;
    bool (*remove)(void*) = nullptr;
    void* (*weaponInfo)(int, signed char) = nullptr;
    void (*requestModel)(int, int) = nullptr;
    void (*loadModels)(bool) = nullptr;
    void (*update)() = nullptr;
    bool layoutVerified = false;
};
Api api;
ResolveEntity resolver = nullptr;
struct Record { Request request; Snapshot snapshot; int slot; void* object; uint32_t deadline; bool remove = false; };
std::mutex mutex;
std::unordered_map<uint32_t, Record> records;
uint32_t nextId = 0x62000001u;
bool Ready() {
#ifdef __ANDROID__
    if (!api.add) {
        void* lib = dlopen("libGTASA.so", RTLD_NOLOAD | RTLD_GLOBAL);
        if (!lib) return false;
        api.info = static_cast<Info*>(dlsym(lib, "gaProjectileInfo"));
        api.objects = static_cast<void**>(dlsym(lib, "_ZN15CProjectileInfo15ms_apProjectileE"));
        api.clock = static_cast<uint32_t*>(dlsym(lib, "_ZN6CTimer22m_snTimeInMillisecondsE"));
        api.ignore = static_cast<void**>(dlsym(lib, "_ZN6CWorld13pIgnoreEntityE"));
        api.add = reinterpret_cast<decltype(api.add)>(dlsym(lib, "_ZN15CProjectileInfo13AddProjectileEP7CEntity11eWeaponType7CVectorfPS3_S1_"));
        api.remove = reinterpret_cast<decltype(api.remove)>(dlsym(lib, "_ZN15CProjectileInfo25RemoveIfThisIsAProjectileEP7CObject"));
        api.weaponInfo = reinterpret_cast<decltype(api.weaponInfo)>(dlsym(lib, "_ZN11CWeaponInfo13GetWeaponInfoE11eWeaponTypea"));
        api.requestModel = reinterpret_cast<decltype(api.requestModel)>(dlsym(lib, "_ZN10CStreaming12RequestModelEii"));
        api.loadModels = reinterpret_cast<decltype(api.loadModels)>(dlsym(lib, "_ZN10CStreaming22LoadAllRequestedModelsEb"));
        api.update = reinterpret_cast<decltype(api.update)>(dlsym(lib, "_ZN15CProjectileInfo6UpdateEv"));
        // Fail closed on a different libGTASA build before touching any native
        // fields. These are the actual load/store instructions cited above.
        auto word = [](const void* fn, size_t offset) {
            uint32_t value = 0;
            if (fn) std::memcpy(&value, static_cast<const unsigned char*>(fn) + offset, 4);
            return value;
        };
        const void* addCode = reinterpret_cast<const void*>(api.add);
        api.layoutVerified = word(addCode, 0x7b0) == 0x2d0d2d0au &&
            word(addCode, 0x7b4) == 0xbd00710du && word(addCode, 0x7d4) == 0xb900131au &&
            word(reinterpret_cast<const void*>(api.update), 0x1e4) == 0xb9401908u;
        if (!api.layoutVerified) ENGINE_LOGE("DIAG projectile ABI differs from verified libGTASA 2.10 arm64; disabled");
        dlclose(lib);
    }
#endif
    return api.info && api.objects && api.clock && api.ignore && api.add && api.remove && resolver &&
        api.weaponInfo && api.requestModel && api.loadModels && api.layoutVerified;
}
bool Live(const Record& r) {
    return api.info[r.slot].active && api.objects[r.slot] == r.object &&
        api.info[r.slot].deadline == r.deadline && api.info[r.slot].type == static_cast<uint32_t>(r.request.weapon);
}
Vec3 Position(void* body) {
    // CPlaceable matrix +0x18 / position +0x30; the same read is performed
    // by libGTASA RemoveProjectile at 0x6ff004-0x6ff024 (fallback body+8).
    auto* bytes = static_cast<unsigned char*>(body);
    void* matrix = nullptr;
    std::memcpy(&matrix, bytes + 0x18, sizeof(matrix));
    Vec3 value;
    std::memcpy(&value, matrix ? static_cast<unsigned char*>(matrix) + 0x30 : bytes + 8, sizeof(value));
    return value;
}
void Refresh(Record& r) {
    auto& info = api.info[r.slot];
    r.snapshot.position = Position(r.object);
    r.snapshot.counter = info.deadline - *api.clock; // CProjectileInfoSA.cpp:177-184, unsigned DWORD.
    if (!info.creator) r.snapshot.creator = 0;
    if (!info.target) r.snapshot.target = 0;
}
}
void Pump(ResolveEntity resolve) {
    resolver = resolve;
    if (!Ready()) return;
    std::lock_guard lock(mutex);
    for (auto it = records.begin(); it != records.end();) {
        auto& r = it->second;
        if (!Live(r)) { it = records.erase(it); continue; }
        if (r.remove) {
            // CProjectileInfoSA::RemoveProjectile(..., false): native cleanup,
            // without adding an explosion or destroying another pool slot.
            api.remove(r.object);
            it = records.erase(it);
        } else { Refresh(r); ++it; }
    }
}
uint32_t Create(const Request& request) {
    uint32_t result = 0;
    if (!GameTasks::Run([&] {
        if (!Ready()) { ENGINE_LOGI("DIAG createProjectile: native projectile API unavailable"); return; }
        void* creator = resolver(request.creator);
        void* target = request.target ? resolver(request.target) : nullptr;
        if (!creator || (request.target && !target)) return;
        // Native AddProjectile loads model1 from CWeaponInfo+12 at 0x6feb5c.
        // Stream the same model before construction, as other mobile native bodies do.
        void* weaponInfo = api.weaponInfo(request.weapon, 1);
        if (!weaponInfo) return;
        int model = -1;
        std::memcpy(&model, static_cast<unsigned char*>(weaponInfo) + 12, sizeof(model));
        if (model <= 0) return;
        api.requestModel(model, 0x14);
        api.loadModels(false);
        Vec3 origin = Position(creator);
        if (request.originMask & 1) origin.x = request.origin.x;
        if (request.originMask & 2) origin.y = request.origin.y;
        if (request.originMask & 4) origin.z = request.origin.z;
        std::lock_guard lock(mutex);
        std::array<bool, 32> before{};
        for (int i = 0; i < 32; ++i) before[i] = api.info[i].active;
        void* previousIgnore = *api.ignore;
        *api.ignore = creator; // CProjectileInfoSA::AddProjectile: ignore creator during collision query.
        const bool added = api.add(creator, request.weapon, origin, request.force, nullptr, target);
        *api.ignore = previousIgnore;
        if (!added) return;
        for (int i = 0; i < 32; ++i) {
            if (before[i] || !api.info[i].active || !api.objects[i] || api.info[i].creator != creator ||
                api.info[i].type != static_cast<uint32_t>(request.weapon)) continue;
            void* body = api.objects[i];
            if (request.velocity.x != 0 || request.velocity.y != 0 || request.velocity.z != 0) {
                // CPhysical move speed +0x68: AddProjectile loads at 0x6fe78c/790,
                // stores the projectile's XYZ at 0x6feccc/d0. Native physics integrates it.
                std::memcpy(static_cast<unsigned char*>(body) + 0x68, &request.velocity, sizeof(Vec3));
            }
            const uint32_t id = nextId++;
            Record record{request, {id, request.creator, request.target, 0, request.weapon, request.force, {}},
                          i, body, api.info[i].deadline};
            Refresh(record);
            records.emplace(id, std::move(record));
            result = id;
            return;
        }
        ENGINE_LOGI("DIAG createProjectile: native creation did not yield an identifiable slot");
    })) return 0;
    return result;
}
bool Copy(uint32_t id, Snapshot& out) {
    std::lock_guard lock(mutex);
    auto it = records.find(id);
    if (it == records.end() || it->second.remove) return false;
    out = it->second.snapshot;
    return true;
}
bool Release(uint32_t id) {
    std::lock_guard lock(mutex);
    auto it = records.find(id);
    if (it == records.end() || it->second.remove) return false;
    it->second.remove = true;
    return true;
}
bool SetCounter(uint32_t id, uint32_t milliseconds) {
    bool result = false;
    GameTasks::Run([&] {
        if (!Ready()) return;
        std::lock_guard lock(mutex);
        auto it = records.find(id);
        if (it == records.end() || it->second.remove || !Live(it->second)) return;
        // Deadline +24 read/store evidence above. Match desktop SetCounter = now + delta.
        api.info[it->second.slot].deadline = *api.clock + milliseconds;
        it->second.deadline = api.info[it->second.slot].deadline;
        Refresh(it->second);
        result = true;
    });
    return result;
}
void DropOwner(const std::string& owner, uint16_t netId, uint32_t generation) {
    std::lock_guard lock(mutex);
    for (auto& [id, r] : records)
        if (r.request.owner == owner && r.request.netId == netId && r.request.generation == generation) r.remove = true;
}
void Reset() { std::lock_guard lock(mutex); for (auto& [id, r] : records) r.remove = true; }
}
