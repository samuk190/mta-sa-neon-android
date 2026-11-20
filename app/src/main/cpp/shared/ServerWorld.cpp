#include "ServerWorld.h"
#include "PedQueries.h"
#include "ResourceFiles.h"
#include "ConnectionLoading.h"
#include "../gtasa/GameEngine.h"
#include "../gtasa/RemoteVehicle.h"
#include "../net/raknet/RakNetWrapper.h"
#include "entity/EntityAddTypes.h"
#include "entity/PedSyncState.h"
#include "entity/PedWire.h"

#include <cmath>
#include <algorithm>
#include <cstring>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <vector>

#ifdef __ANDROID__
#include <android/log.h>
#include <dlfcn.h>
#define SW_LOGI(...) __android_log_print(ANDROID_LOG_INFO, "WorldRPGNeon", __VA_ARGS__)
#else
#include <cstdio>
#define SW_LOGI(...) do { std::printf(__VA_ARGS__); std::printf("\n"); } while (0)
#endif

namespace WorldRPG::ServerWorld {
namespace {
using namespace WorldRPG::Entity;

std::mutex g_lock;
bool g_currentWaterFlags = true;
uint32_t g_local = 0;
uint64_t g_generation = 0, g_localGeneration = 0;
// CClientGame::SetAllDimensions: the local player's dimension (PLAYER_SPAWN, PLAYER_LIST, SET_ELEMENT_DIMENSION).
std::atomic<uint16_t> g_localDimension{0};
std::atomic<uint8_t> g_localInterior{0};
uint32_t g_resourceRoot = 0;
std::unordered_map<uint32_t, Visual> g_visuals;
std::vector<uint32_t> g_teamOrder;
std::unordered_map<uint32_t, Attachment> g_attachments;
std::vector<uint32_t> g_attachmentOrder;
std::unordered_map<uint32_t, std::unordered_map<std::string, Datum>> g_data;
// Player nicks. PLAYER_LIST reads the nick and this keeps it. The local nick is
// noted at join. Not part of Visual, so noting the local nick does not invent a pose.
std::unordered_map<uint32_t, std::string> g_nicks;
// Radar blip handles of a session that ended (ResetSession). CRadar is only touched on the game thread, so Tick
// clears them. Guarded by g_lock.
std::vector<int> g_staleBlips;

#ifdef __ANDROID__
using Vec3 = struct { float x, y, z; };
using SetBlipFn = int (*)(int, Vec3, unsigned, int, char*);
using ClearBlipFn = void (*)(int);
SetBlipFn g_setBlip = nullptr;
ClearBlipFn g_clearBlip = nullptr;
bool g_blipTried = false;

void ResolveBlips() {
    if (g_blipTried) return;
    g_blipTried = true;
    void* handle = dlopen("libGTASA.so", RTLD_NOLOAD | RTLD_GLOBAL);
    if (!handle) return;
    g_setBlip = reinterpret_cast<SetBlipFn>(dlsym(handle, "_ZN6CRadar12SetCoordBlipE9eBlipType7CVectorj12eBlipDisplayPc"));
    g_clearBlip = reinterpret_cast<ClearBlipFn>(dlsym(handle, "_ZN6CRadar9ClearBlipEi"));
    SW_LOGI("Radar blip symbols set=%p clear=%p", reinterpret_cast<void*>(g_setBlip), reinterpret_cast<void*>(g_clearBlip));
}
#endif

[[maybe_unused]] bool SkipName(Net::BitStreamReader& reader) {
    uint32_t length = 0;
    if (!ReadCompressed(reader, 2, length) || length > 256) {
        DIAG("skipname fail len=%u bit=%zu", static_cast<unsigned>(length), reader.GetReadOffsetBits());
        return false;
    }
    return SkipBytes(reader, length);
}

[[maybe_unused]] bool ReadAngle16(Net::BitStreamReader& reader, float& out) {
    uint8_t raw[2] = {};
    if (!reader.ReadBits(raw, 16)) return false;
    const unsigned value = raw[0] | (raw[1] << 8);
    constexpr float pi = 3.14159265f;
    out = -pi + (static_cast<float>(value) / 65535.f) * (2.f * pi);
    return true;
}

[[maybe_unused]] bool ReadDoorRatio(Net::BitStreamReader& reader) {
    bool wide = false;
    if (!ReadFlag(reader, wide)) return false;
    if (!wide) {
        bool open = false;
        return ReadFlag(reader, open);
    }
    uint8_t bits[2] = {};
    return reader.ReadBits(bits, 10);
}

[[maybe_unused]] bool VehicleHasDoors(int model) {
    if (model < 400 || model > 611) return false;
    static const int noDamage[] = {
        430, 446, 448, 449, 452, 453, 454, 461, 462, 463, 468, 472, 473, 481, 484,
        493, 509, 510, 521, 522, 523, 537, 538, 569, 570, 581, 586, 590, 595
    };
    static const int doorless[] = {
        424, 441, 457, 465, 485, 486, 501, 504, 530, 531, 564, 568, 571, 572, 594
    };
    for (int id : noDamage) if (id == model) return false;
    for (int id : doorless) if (id == model) return false;
    return true;
}

[[maybe_unused]] bool ReadPedTail(Net::BitStreamReader& reader, Visual& visual) {
    if (!ReadAngle16(reader, visual.heading)) return false;
    uint8_t health = 0, armor = 0;
    if (!reader.Read(health) || !reader.Read(armor)) return false;
    (void)health; (void)armor;
    bool inVehicle = false;
    if (!ReadFlag(reader, inVehicle)) return false;
    if (inVehicle) {
        uint32_t vehicle = 0;
        uint8_t seat = 0;
        if (!ReadElementId(reader, vehicle) || !reader.ReadBits(&seat, 4)) return false;
    }
    for (int i = 0; i < 4; ++i) {
        bool flag = false;
        if (!ReadFlag(reader, flag)) return false;
    }
    uint32_t alpha = 0;
    if (!ReadCompressed(reader, 1, alpha)) return false;
    uint8_t moveAnim = 0;
    if (!reader.Read(moveAnim)) return false;
    uint8_t clothes = 0;
    if (!reader.Read(clothes)) return false;
    for (uint8_t i = 0; i < clothes; ++i) {
        uint8_t texLen = 0, modelLen = 0, slot = 0;
        if (!reader.Read(texLen) || !SkipBytes(reader, texLen)) return false;
        if (!reader.Read(modelLen) || !SkipBytes(reader, modelLen) || !reader.Read(slot)) return false;
    }
    for (;;) {
        uint8_t slot = 0;
        if (!reader.Read(slot)) return false;
        if (slot == 0xFF) break;
        uint8_t type = 0;
        uint16_t ammo = 0;
        if (!reader.Read(type) || !reader.Read(ammo)) return false;
    }
    uint8_t current = 0;
    if (!reader.Read(current)) return false;
    bool anim = false;
    if (!ReadFlag(reader, anim)) return false;
    if (!anim) return true;
    if (!SkipName(reader) || !SkipName(reader)) return false;
    float time = 0, blend = 0, elapsed = 0, speed = 0;
    bool loop = false, update = false, interrupt = false, freeze = false, restore = false;
    return reader.Read(time) && ReadFlag(reader, loop) && ReadFlag(reader, update) &&
           ReadFlag(reader, interrupt) && ReadFlag(reader, freeze) && reader.Read(blend) &&
           ReadFlag(reader, restore) && reader.Read(elapsed) && reader.Read(speed);
}

[[maybe_unused]] bool ReadVehicleTail(Net::BitStreamReader& reader, Visual& visual) {
    uint8_t rot[6] = {};
    if (!reader.ReadBits(rot, 48)) return false;
    const unsigned rz = rot[4] | (rot[5] << 8);
    constexpr float pi = 3.14159265f;
    visual.heading = (static_cast<float>(rz) * (360.f / 65536.f)) * pi / 180.f;
    uint16_t model = 0;
    if (!reader.Read(model)) return false;
    visual.model = model;
    uint8_t health[2] = {};
    if (!reader.ReadBits(health, 12)) return false;
    uint8_t blow = 0;
    if (!reader.ReadBits(&blow, 2)) return false;
    uint8_t colorCount = 0;
    if (!reader.ReadBits(&colorCount, 2)) return false;
    for (int i = 0; i <= (colorCount & 3); ++i) {
        uint8_t rgb[3] = {};
        if (!reader.ReadBits(rgb, 24)) return false;
    }
    uint8_t paint = 0;
    if (!reader.ReadBits(&paint, 2)) return false;
    uint8_t damage[8] = {};
    if (!reader.ReadBits(damage, 48)) return false;
    uint8_t variant = 0, variant2 = 0;
    if (!reader.Read(variant) || !reader.Read(variant2)) return false;
    const bool turret = model == 407 || model == 432 || model == 601;
    if (turret) {
        uint8_t aim[4] = {};
        if (!reader.ReadBits(aim, 32)) return false;
    }
    const bool adjustable = model == 406 || model == 443 || model == 486 || model == 520 ||
                            model == 524 || model == 525 || model == 530 || model == 531 || model == 592;
    if (adjustable) {
        uint32_t property = 0;
        if (!ReadCompressed(reader, 2, property)) return false;
    }
    if (VehicleHasDoors(model)) {
        for (int door = 0; door < 6; ++door) if (!ReadDoorRatio(reader)) return false;
    }
    uint8_t upgrades = 0;
    if (!reader.Read(upgrades)) return false;
    if (!SkipBytes(reader, upgrades)) return false;
    uint8_t plate[8] = {};
    if (!reader.ReadBits(plate, 64)) return false;
    uint8_t lights = 0;
    if (!reader.ReadBits(&lights, 2)) return false;
    for (int i = 0; i < 12; ++i) {
        bool flag = false;
        if (!ReadFlag(reader, flag)) return false;
    }
    uint32_t alpha = 0;
    if (!ReadCompressed(reader, 1, alpha)) return false;
    bool head = false;
    if (!ReadFlag(reader, head)) return false;
    if (head && !SkipBytes(reader, 3)) return false;
    bool handling = false;
    if (!ReadFlag(reader, handling)) return false;
    if (handling) return false;
    bool siren = false;
    if (!ReadFlag(reader, siren)) return false;
    if (!siren) return true;
    uint8_t count = 0, type = 0;
    if (!reader.Read(count) || !reader.Read(type)) return false;
    for (uint8_t i = 0; i < count && i < 8; ++i) {
        // SVehicleSirenSync is not required to place the car. Stop this element
        // rather than invent the rest of a siren block.
        return false;
    }
    return true;
}

void Remember(uint32_t id, const Visual& visual) {
    if (id == 0 || id == 0xFFFFFFFFu) return;
    auto& stored = g_visuals[id];
    const int oldBlip = stored.blipHandle;
    const bool live = stored.blipLive;
    stored = visual;
    stored.generation = ++g_generation;
    if (visual.kind == 14) PedQueries::Create(id, false, visual.ped.get());
    else if (visual.kind == 1) PedQueries::Ensure(id, true);
    stored.blipHandle = oldBlip;
    stored.blipLive = live;
    if (visual.model >= 0) stored.model = visual.model;
    if (visual.kind == 13 && std::find(g_teamOrder.begin(), g_teamOrder.end(), id) == g_teamOrder.end())
        g_teamOrder.push_back(id);
}

// CClientWeapon.cpp:22-23 registers references; CClientEntity.cpp:94-98 detaches on deletion.
// Keep snapshots from pointing at a new element that later reuses the same id.
void ForgetElementReferences(uint32_t id) {
    g_attachments.erase(id);
    g_attachmentOrder.erase(std::remove(g_attachmentOrder.begin(), g_attachmentOrder.end(), id), g_attachmentOrder.end());
    for (auto& [child, attachment] : g_attachments)
        if (attachment.parent == id) attachment.parent = 0xFFFFFFFFu;
    for (auto& [weaponId, visual] : g_visuals) {
        if (!visual.weapon || (visual.weapon->owner != id && visual.weapon->target != id)) continue;
        auto next = std::make_shared<WeaponSpawn>(*visual.weapon);
        if (next->owner == id) next->owner = 0xFFFFFFFFu;
        if (next->target == id) next->target = 0xFFFFFFFFu;
        visual.weapon = std::move(next);
    }
}

} // namespace

void ResetSession() {
    std::vector<uint32_t> peds;
    {
        std::lock_guard<std::mutex> lock(g_lock);
        for (const auto& [id, visual] : g_visuals) {
            if (visual.blipLive) g_staleBlips.push_back(visual.blipHandle);
            if (visual.kind == 14) peds.push_back(id);
        }
        g_visuals.clear();
        g_teamOrder.clear();
        PedQueries::Reset();
        g_localGeneration = ++g_generation;
        g_attachments.clear();
        g_attachmentOrder.clear();
        g_data.clear();
        g_nicks.clear();
        g_resourceRoot = 0;
    }
    g_localDimension = 0;
    g_localInterior = 0;
    // Hosted and observer ped state is keyed by ElementID, which the new session reuses.
    for (uint32_t id : peds) {
        WorldRPG::Sync::PedSyncState::Forget(id);
        Game::ForgetPedNativeTaskState(id);
    }
    SW_LOGI("ServerWorld reset for a new session (%zu ped state(s) forgotten)", peds.size());
}

void SetLocalPlayer(uint32_t id) {
    std::lock_guard lock(g_lock);
    if (g_local != id || !g_localGeneration) g_localGeneration = ++g_generation;
    g_local = id; PedQueries::Ensure(id, true);
}
uint32_t LocalPlayer() { return g_local; }
uint32_t LastResourceRoot() { return g_resourceRoot; }

int CopyActors(ActorSpawn* out, int capacity, int pedLimit, int vehicleLimit) {
    if (!out || capacity <= 0) return 0;
    // Desktop streams peds and vehicles apart (CClientManager.cpp 27-28): a ped uses the player
    // streamer (CClientPed.cpp 591-604) and stops at 110 (CClientPlayerManager::IsPlayerLimitReached,
    // CClientPlayerManager.cpp 172-174; MAX_PEDS_MTA, Common.h 17). A vehicle uses its own streamer
    // (CClientVehicle.cpp 81-82) and stops at 64 (CClientVehicleManager::IsVehicleLimitReached,
    // CClientVehicleManager.cpp 780-784; MAX_VEHICLES_MTA, Common.h 16). Callers pass the phone caps.
    // Both limits negative keeps the old shared buffer for a caller that has not split.
    const bool shared = pedLimit < 0 && vehicleLimit < 0;
    if (pedLimit < 0) pedLimit = 0;
    if (vehicleLimit < 0) vehicleLimit = 0;
    std::lock_guard<std::mutex> lock(g_lock);
    int count = 0;
    auto write = [&](uint32_t id, const Visual& visual) {
        ActorSpawn& slot = out[count++];
        slot.id = id;
        slot.kind = visual.kind;
        slot.model = visual.model;
        slot.x = visual.x;
        slot.y = visual.y;
        slot.z = visual.z;
        slot.heading = visual.heading;
        slot.rotX = visual.rotX;
        slot.rotY = visual.rotY;
        slot.health = visual.health;
        slot.hosted = visual.hosted;
        slot.interior = visual.interior;
    };
    if (shared) {
        for (const auto& [id, visual] : g_visuals) {
            if (visual.kind != 14 && visual.kind != 2) continue;
            if (count >= capacity) break;
            write(id, visual);
        }
        return count;
    }
    // Peds first. A short buffer then still holds peds when the vehicle quota would have filled it.
    for (int pass = 0; pass < 2; ++pass) {
        const int kind = pass == 0 ? 14 : 2;
        const int limit = pass == 0 ? pedLimit : vehicleLimit;
        int used = 0;
        if (limit <= 0) continue;
        for (const auto& [id, visual] : g_visuals) {
            if (count >= capacity || used >= limit) break;
            if (visual.kind != kind) continue;
            ++used;
            write(id, visual);
        }
    }
    return count;
}

void SetActorHost(uint32_t id, bool host) {
    std::lock_guard<std::mutex> lock(g_lock);
    auto found = g_visuals.find(id);
    if (found == g_visuals.end()) return;
    found->second.hosted = host;
}

bool ActorHosted(uint32_t id) {
    std::lock_guard<std::mutex> lock(g_lock);
    auto found = g_visuals.find(id);
    return found != g_visuals.end() && found->second.hosted;
}

bool ElementPose(uint32_t id, float& x, float& y, float& z) {
    std::lock_guard<std::mutex> lock(g_lock);
    auto found = g_visuals.find(id);
    if (found == g_visuals.end()) return false;
    x = found->second.x;
    y = found->second.y;
    z = found->second.z;
    return true;
}

bool CopyVisual(uint32_t id, ActorSpawn& out) {
    std::lock_guard<std::mutex> lock(g_lock);
    auto found = g_visuals.find(id);
    if (found == g_visuals.end()) return false;
    out = {};
    out.id = id;
    out.kind = found->second.kind;
    out.model = found->second.model;
    out.x = found->second.x;
    out.y = found->second.y;
    out.z = found->second.z;
    out.heading = found->second.heading;
    out.rotX = found->second.rotX;
    out.rotY = found->second.rotY;
    out.health = found->second.health;
    out.hosted = found->second.hosted;
    out.interior = found->second.interior;
    return true;
}

uint64_t ElementGeneration(uint32_t id) {
    std::lock_guard lock(g_lock);
    if (id && id == g_local) return g_localGeneration;
    auto it=g_visuals.find(id);
    if(it==g_visuals.end()) return 0;
    if(!it->second.generation) it->second.generation=++g_generation;
    return it->second.generation;
}
void CopySpatialElements(std::vector<Spatial::Element>& out) {
    std::lock_guard lock(g_lock);
    out.clear();
    for(auto& [id,v]:g_visuals) {
        const char* type=nullptr;
        switch(v.kind) {
            case 1:type="player";break; case 2:type="vehicle";break; case 3:type="object";break;
            case 4:type="marker";break; case 5:type="blip";break; case 6:type="pickup";break;
            case 7:type="radararea";break; case 14:type="ped";break; case 15:type="colshape";break;
            case 18:type="weapon";break; default:continue;
        }
        if(!v.generation) v.generation=++g_generation;
        Spatial::Element e;
        e.id=id; e.generation=v.generation; e.shapeRevision=v.shapeRevision; e.type=type; e.position={v.x,v.y,v.z};
        e.model=v.model; e.interior=id==g_local?g_localInterior.load():v.interior; e.dimension=id==g_local?g_localDimension.load():v.dimension;
        e.bounds={e.position,v.kind==4?v.size:0.f};
        if(v.colshape) { e.shape=*v.colshape; e.bounds=Spatial::ShapeBounds(e.position,*e.shape); }
        out.push_back(std::move(e));
    }
    if(g_local && std::none_of(out.begin(),out.end(),[](const auto& e){return e.id==g_local;})) {
        Spatial::Element e; e.id=g_local; e.generation=g_localGeneration; e.type="player"; e.dimension=g_localDimension; e.interior=g_localInterior;
        out.push_back(e);
    }
}
bool CopyColshape(uint32_t id,Spatial::Element& out) {
    std::lock_guard lock(g_lock);
    auto it=g_visuals.find(id);
    if(it==g_visuals.end() || !it->second.colshape) return false;
    auto& v=it->second;
    if(!v.generation) v.generation=++g_generation;
    out={}; out.id=id; out.generation=v.generation; out.shapeRevision=v.shapeRevision; out.type="colshape";
    out.position={v.x,v.y,v.z}; out.interior=v.interior; out.dimension=v.dimension; out.shape=*v.colshape;
    out.bounds=Spatial::ShapeBounds(out.position,*out.shape);
    return true;
}
bool UpdateColshape(uint32_t id,const std::function<bool(Spatial::Geometry&)>& update) {
    std::lock_guard lock(g_lock);
    auto it=g_visuals.find(id);
    if(it==g_visuals.end() || !it->second.colshape) return false;
    auto geometry=std::make_shared<Spatial::Geometry>(*it->second.colshape);
    if(!update(*geometry)) return false;
    it->second.colshape=std::move(geometry);
    it->second.shapeRevision=++g_generation;
    return true;
}

uint8_t SyncTimeContext(uint32_t id) {
    std::lock_guard<std::mutex> lock(g_lock);
    auto found = g_visuals.find(id);
    if (found == g_visuals.end()) return 0;
    return found->second.syncTimeContext;
}

bool CopyPedSpawn(uint32_t id, Entity::PedSpawn& out) {
    std::lock_guard<std::mutex> lock(g_lock);
    auto found = g_visuals.find(id);
    if (found == g_visuals.end() || found->second.kind != 14 || !found->second.ped) return false;
    out = *found->second.ped;
    return true;
}

bool CopyWeaponSpawn(uint32_t id, Entity::WeaponSpawn& out) {
    std::lock_guard<std::mutex> lock(g_lock);
    auto found = g_visuals.find(id);
    if (found == g_visuals.end() || found->second.kind != 18 || !found->second.weapon) return false;
    out = *found->second.weapon;
    return true;
}

bool CopyAttachment(uint32_t id, Attachment& out) {
    std::lock_guard lock(g_lock);
    auto found = g_attachments.find(id);
    if (found == g_attachments.end()) return false;
    out = found->second;
    return true;
}

void NoteAttachedOffsets(uint32_t id, float x, float y, float z, float rx, float ry, float rz) {
    if (id == 0 || id == 0xFFFFFFFFu) return;
    std::lock_guard lock(g_lock);
    Attachment& attachment = g_attachments[id];
    attachment.position[0] = x;
    attachment.position[1] = y;
    attachment.position[2] = z;
    attachment.rotation[0] = rx;
    attachment.rotation[1] = ry;
    attachment.rotation[2] = rz;
}

std::vector<uint32_t> CopyAttachedElements(uint32_t parent) {
    std::lock_guard lock(g_lock);
    std::vector<uint32_t> result;
    for (uint32_t child : g_attachmentOrder) {
        auto found = g_attachments.find(child);
        if (found != g_attachments.end() && found->second.parent == parent) result.push_back(child);
    }
    return result;
}

bool CopyMarkerFields(uint32_t id, MarkerFields& out) {
    std::lock_guard<std::mutex> lock(g_lock);
    auto found = g_visuals.find(id);
    if (found == g_visuals.end() || found->second.kind != 4) return false;
    const auto& visual = found->second;
    out = {};
    out.type = visual.markerType;
    out.x = visual.x;
    out.y = visual.y;
    out.z = visual.z;
    out.size = visual.size;
    out.r = visual.r;
    out.g = visual.g;
    out.b = visual.b;
    out.a = visual.a;
    out.ignoreAlpha = visual.markerIgnoreAlpha;
    out.hasTarget = visual.markerHasTarget;
    out.targetX = visual.markerTargetX;
    out.targetY = visual.markerTargetY;
    out.targetZ = visual.markerTargetZ;
    out.arrowR = visual.markerArrowR;
    out.arrowG = visual.markerArrowG;
    out.arrowB = visual.markerArrowB;
    out.arrowA = visual.markerArrowA;
    out.arrowSize = visual.markerArrowSize;
    return true;
}

void CopyTeams(std::vector<TeamFields>& out) {
    std::lock_guard<std::mutex> lock(g_lock);
    out.clear();
    for (uint32_t id : g_teamOrder) {
        auto found = g_visuals.find(id);
        if (found == g_visuals.end() || found->second.kind != 13 || !found->second.team) continue;
        TeamFields team;
        team.id = id;
        team.name = found->second.team->name;
        team.r = found->second.r;
        team.g = found->second.g;
        team.b = found->second.b;
        team.friendlyFire = found->second.team->friendlyFire;
        team.players = found->second.team->players;
        out.push_back(std::move(team));
    }
}

bool CopyTeam(uint32_t id, TeamFields& out) {
    std::lock_guard<std::mutex> lock(g_lock);
    auto found = g_visuals.find(id);
    if (found == g_visuals.end() || found->second.kind != 13 || !found->second.team) return false;
    out = {};
    out.id = id;
    out.name = found->second.team->name;
    out.r = found->second.r;
    out.g = found->second.g;
    out.b = found->second.b;
    out.friendlyFire = found->second.team->friendlyFire;
    out.players = found->second.team->players;
    return true;
}

bool CopyRadarFields(uint32_t id, RadarFields& out) {
    std::lock_guard<std::mutex> lock(g_lock);
    auto found = g_visuals.find(id);
    if (found == g_visuals.end() || found->second.kind != 7) return false;
    const auto& visual = found->second;
    out = {};
    out.x = visual.x;
    out.y = visual.y;
    out.w = visual.w;
    out.h = visual.h;
    out.r = visual.r;
    out.g = visual.g;
    out.b = visual.b;
    out.a = visual.a;
    out.flashing = visual.radarFlashing;
    return true;
}

int CountKind(int kind) {
    std::lock_guard<std::mutex> lock(g_lock);
    int count = 0;
    for (const auto& [id, visual] : g_visuals) {
        (void)id;
        if (visual.kind == kind) ++count;
    }
    return count;
}

const char* ElementKindName(uint32_t id) {
    std::lock_guard<std::mutex> lock(g_lock);
    if (id == g_local) return "player";
    auto found = g_visuals.find(id);
    if (found == g_visuals.end()) return id == g_resourceRoot ? "resource" : "element";
    switch (found->second.kind) {
        case 1: return "player";
        case 2: return "vehicle";
        case 3: return "object";
        case 5: return "blip";
        case 6: return "pickup";
        case 7: return "radararea";
        case 15: return "colshape";
        case 14: return "ped";
        case 18: return "weapon";
        case 4: return "marker";
        case 13: return "team";
        default: return "element";
    }
}

void NotePlayerSkin(uint32_t id, int model) {
    if (id == 0 || id == 0xFFFFFFFFu || model < 0 || model > 312) return;
    std::lock_guard<std::mutex> lock(g_lock);
    auto& stored = g_visuals[id];
    stored.kind = 1;
    PedQueries::Ensure(id, true);
    stored.model = model;
}

int ModelOf(uint32_t id) {
    std::lock_guard<std::mutex> lock(g_lock);
    auto found = g_visuals.find(id);
    if (found == g_visuals.end()) return -1;
    return found->second.model;
}

bool GetData(uint32_t id, const char* key, double& number, std::string& text, bool& isNumber, bool* isBoolean) {
    if (!key) return false;
    std::lock_guard<std::mutex> lock(g_lock);
    auto element = g_data.find(id);
    if (element == g_data.end()) return false;
    auto found = element->second.find(key);
    if (found == element->second.end()) return false;
    isNumber = found->second.number;
    if (isBoolean) *isBoolean = found->second.boolean;
    number = found->second.value;
    text = found->second.text;
    return true;
}

void CopyElementData(uint32_t id, std::vector<std::pair<std::string, ElementDataValue>>& out) {
    out.clear();
    std::lock_guard<std::mutex> lock(g_lock);
    auto element = g_data.find(id);
    if (element == g_data.end()) return;
    out.reserve(element->second.size());
    for (const auto& [key, datum] : element->second) {
        ElementDataValue value;
        value.number = datum.number;
        value.boolean = datum.boolean;
        value.value = datum.value;
        value.text = datum.text;
        out.emplace_back(key, std::move(value));
    }
}

void NotePlayerNick(uint32_t id, const std::string& nick) {
    if (id == 0 || id == 0xFFFFFFFFu) return;
    std::lock_guard<std::mutex> lock(g_lock);
    g_nicks[id] = nick;
    PedQueries::Ensure(id, true);
}

bool CopyPlayerNick(uint32_t id, std::string& out) {
    std::lock_guard<std::mutex> lock(g_lock);
    auto found = g_nicks.find(id);
    if (found == g_nicks.end()) return false;
    out = found->second;
    return true;
}

static bool SameNick(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        unsigned char ca = static_cast<unsigned char>(a[i]);
        unsigned char cb = static_cast<unsigned char>(b[i]);
        if (ca >= 'A' && ca <= 'Z') ca = static_cast<unsigned char>(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = static_cast<unsigned char>(cb - 'A' + 'a');
        if (ca != cb) return false;
    }
    return true;
}

static bool IsListedPlayer(uint32_t id) {
    if (id != 0 && id == g_local) return true;
    auto found = g_visuals.find(id);
    return found != g_visuals.end() && found->second.kind == 1;
}

bool FindPlayerByNick(const std::string& nick, uint32_t& id) {
    std::lock_guard<std::mutex> lock(g_lock);
    for (const auto& [playerId, name] : g_nicks) {
        if (!IsListedPlayer(playerId) || !SameNick(name, nick)) continue;
        id = playerId;
        return true;
    }
    return false;
}

void SetCurrentWaterFlags(bool enabled) {
    std::lock_guard<std::mutex> lock(g_lock);
    g_currentWaterFlags = enabled;
}

void OnEntityAdd(const uint8_t* body, size_t length, size_t bits) {
    Net::BitStreamReader reader(body, length, bits);
    uint32_t count = 0;
    if (!ReadCompressed(reader, 4, count)) { DIAG("ENTITY_ADD count read failed bits=%zu", reader.GetReadOffsetBits()); return; }
    DIAG("ENTITY_ADD begin count=%u bodyBits=%zu bitsRead=%zu remaining=%zu", static_cast<unsigned>(count), bits, reader.GetReadOffsetBits(), reader.GetRemainingBits());
    {
        // Ground-truth dump: first 80 body bytes, lowest address first, for offline bit decoding.
        char hex[80 * 2 + 1] = {};
        const size_t dumpBytes = length < 80 ? length : 80;
        for (size_t i = 0; i < dumpBytes; ++i) std::snprintf(hex + i * 2, 3, "%02x", static_cast<unsigned>(body[i]));
        DIAG("ENTITY_ADD body hex count=%u bytes=%zu %s", static_cast<unsigned>(count), dumpBytes, hex);
    }
    if (count == 0 || count > 512) { DIAG("ENTITY_ADD bad count=%u", static_cast<unsigned>(count)); return; }
    // Desktop CPacketHandler::Packet_EntityAdd (CPacketHandler.cpp:2989-2995): status, then the entity
    // percentage through CCore::UpdateDummyProgress(index * 100 / count, "%").
    ConnectionLoading::Update(ConnectionLoading::Phase::World, "Preparing world", 0.0f);
    std::lock_guard<std::mutex> lock(g_lock);
    int stored = 0;
    std::vector<std::pair<uint32_t, uint32_t>> attachments;
    for (uint32_t index = 0; index < count; ++index) {
        ConnectionLoading::UpdateProgress(static_cast<float>(index * 100 / count) / 100.0f);
        EntityCommon common;
        if (!ReadEntityCommon(reader, common)) {
            DIAG("ENTITY_ADD stop idx=%u/%u common header failed bit=%zu", index, static_cast<unsigned>(count), reader.GetReadOffsetBits());
            break;
        }
        DIAG("ENTITY_ADD idx=%u/%u id=%u type=%u bit=%zu", index, static_cast<unsigned>(count), common.id, static_cast<unsigned>(common.type), reader.GetReadOffsetBits());
        if (common.id != 0 && common.id != 0xFFFFFFFFu) {
            for (const auto& [key, value] : common.customData) g_data[common.id][key] = value;
        }
        Visual visual;
        visual.kind = common.type;
        const auto resolveKind = +[](uint32_t id) -> int {
            // OnEntityAdd already holds g_lock: do not call ElementKindName here.
            if (id == 0 || id == 0xFFFFFFFFu) return -1;
            if (id == g_local || g_nicks.find(id) != g_nicks.end()) return kTypePlayer;
            auto found = g_visuals.find(id);
            return found == g_visuals.end() ? -1 : found->second.kind;
        };
        if (!ReadEntityBody(reader, common, visual, resolveKind, g_currentWaterFlags)) {
            DIAG("ENTITY_ADD fail idx=%u/%u id=%u type=%u bit=%zu", index, static_cast<unsigned>(count), common.id, static_cast<unsigned>(common.type), reader.GetReadOffsetBits());
            break;
        }
        visual.interior = common.interior;
        visual.syncTimeContext = common.syncTimeContext;
        visual.dimension = static_cast<uint16_t>(common.dimension);
        if (common.type != kTypePlayer) Remember(common.id, visual);
        if (common.id == 0 || common.id == 0xFFFFFFFFu) { ++stored; continue; }
        Attachment attachment;
        if (common.attached) {
            attachment.position[0] = common.attachedX; attachment.position[1] = common.attachedY; attachment.position[2] = common.attachedZ;
            attachment.rotation[0] = common.attachedRotX; attachment.rotation[1] = common.attachedRotY; attachment.rotation[2] = common.attachedRotZ;
            attachments.emplace_back(common.id, common.attachedTo);
        }
        g_attachments[common.id] = attachment;
        g_attachmentOrder.erase(std::remove(g_attachmentOrder.begin(), g_attachmentOrder.end(), common.id), g_attachmentOrder.end());
        ++stored;
    }
    // CPacketHandler.cpp:4443-4467 links attachments after all entities in the
    // batch exist. Forward references must not disappear during parsing.
    for (const auto& [child, parent] : attachments) {
        if (parent == 0 || parent == 0xFFFFFFFFu ||
            (parent != g_local && !g_visuals.count(parent) && !g_nicks.count(parent))) continue;
        g_attachments[child].parent = parent;
        g_attachmentOrder.erase(std::remove(g_attachmentOrder.begin(), g_attachmentOrder.end(), child), g_attachmentOrder.end());
        g_attachmentOrder.push_back(child);
    }
    DIAG("ENTITY_ADD stored %d/%u", stored, static_cast<unsigned>(count));
}

// CPacketHandler::Packet_EntityRemove (CPacketHandler.cpp 4491-4566) reads ids until the stream ends
// (`while (bitStream.Read(ID))`). Reading one id left every later element of a batch on the phone.
// A player id raises protocol error 45 on desktop and stops the packet; it is skipped the same way here.
void OnEntityRemove(const uint8_t* body, size_t length, size_t bits) {
    Net::BitStreamReader reader(body, length, bits);
    uint32_t id = 0;
    int removed = 0;
    while (ReadElementId(reader, id)) {
        {
            std::lock_guard<std::mutex> lock(g_lock);
            if (IsListedPlayer(id)) {
                DIAG("ENTITY_REMOVE id=%u is a player (desktop protocol error 45); packet stopped", id);
                return;
            }
            g_visuals.erase(id);
            g_teamOrder.erase(std::remove(g_teamOrder.begin(), g_teamOrder.end(), id), g_teamOrder.end());
            PedQueries::Forget(id);
            g_data.erase(id);
            g_nicks.erase(id);
            ForgetElementReferences(id);
        }
        // Deleting a CClientPed takes it out of CPedSync (CClientPed::~CClientPed, CClientPed.cpp 926). Without this a reused
        // ElementID inherits the old hosted flag and interpolators.
        WorldRPG::Sync::PedSyncState::Forget(id);
        Game::ForgetPedNativeTaskState(id);
        // ForgetRemote takes g_remoteMutex and only queues; the body is dropped on the game thread.
        Game::ForgetRemote(id);
        WorldRPG::RemoteVehicle::ForgetElement(id);
        ++removed;
    }
    if (removed > 1) DIAG("ENTITY_REMOVE removed %d element(s)", removed);
}

void SetSyncTimeContext(uint32_t id, uint8_t context) {
    std::lock_guard<std::mutex> lock(g_lock);
    auto found = g_visuals.find(id);
    if (found != g_visuals.end()) found->second.syncTimeContext = context;
}

void NoteElementPosition(uint32_t id, float x, float y, float z) {
    std::lock_guard<std::mutex> lock(g_lock);
    auto found = g_visuals.find(id);
    if (found == g_visuals.end()) return;
    auto& v=found->second;
    if(v.colshape && v.colshape->type==Spatial::ShapeType::Polygon) {
        auto geometry=std::make_shared<Spatial::Geometry>(*v.colshape);
        for(auto& point:geometry->points) { point.x+=x-v.x; point.y+=y-v.y; }
        v.colshape=std::move(geometry);
    }
    found->second.x = x;
    found->second.y = y;
    found->second.z = z;
}

void NoteElementInterior(uint32_t id, uint8_t interior) {
    std::lock_guard<std::mutex> lock(g_lock);
    if(id==g_local) g_localInterior=interior;
    auto found = g_visuals.find(id);
    if (found != g_visuals.end()) found->second.interior = interior;
}

void NoteElementDimension(uint32_t id, uint16_t dimension) {
    std::lock_guard<std::mutex> lock(g_lock);
    if(id==g_local) g_localDimension=dimension;
    auto found = g_visuals.find(id);
    if (found != g_visuals.end()) found->second.dimension = dimension;
}

uint16_t DimensionOf(uint32_t id) {
    std::lock_guard<std::mutex> lock(g_lock);
    if(id==g_local) return g_localDimension;
    auto found = g_visuals.find(id);
    return found != g_visuals.end() ? found->second.dimension : g_localDimension.load();
}

uint8_t InteriorOf(uint32_t id) {
    std::lock_guard lock(g_lock);
    if(id==g_local) return g_localInterior;
    auto it=g_visuals.find(id); return it==g_visuals.end()?0:it->second.interior;
}
void SetLocalDimension(uint16_t dimension) { g_localDimension = dimension; }
uint16_t LocalDimension() { return g_localDimension.load(); }

void NoteElementModel(uint32_t id, int model) {
    std::lock_guard<std::mutex> lock(g_lock);
    auto found = g_visuals.find(id);
    if (found != g_visuals.end() && model >= 0) found->second.model = model;
}

void NoteVehicle(uint32_t id, int model) {
    if (id == 0 || id == 0xFFFFFFFFu || model < 0) return;
    std::lock_guard<std::mutex> lock(g_lock);
    auto& stored = g_visuals[id];
    if (stored.kind != 0 && stored.kind != 2) return;
    stored.kind = 2;
    stored.model = model;
}

void NotePlayerSeen(uint32_t id) {
    if (id == 0 || id == 0xFFFFFFFFu || id == g_local) return;
    std::lock_guard<std::mutex> lock(g_lock);
    auto& stored = g_visuals[id];
    if (stored.kind == 0) stored.kind = 1;
    PedQueries::Ensure(id, true);
}

bool IsKnownPlayer(uint32_t id) {
    if (id == 0 || id == 0xFFFFFFFFu || id == g_local) return false;
    std::lock_guard<std::mutex> lock(g_lock);
    if (g_nicks.count(id)) return true;
    auto found = g_visuals.find(id);
    return found != g_visuals.end() && found->second.kind == 1;
}

void OnPlayerQuit(uint32_t id) {
    if (id == 0 || id == 0xFFFFFFFFu || id == g_local) return;
    {
        std::lock_guard<std::mutex> lock(g_lock);
        g_visuals.erase(id);
        g_teamOrder.erase(std::remove(g_teamOrder.begin(), g_teamOrder.end(), id), g_teamOrder.end());
        PedQueries::Forget(id);
        g_data.erase(id);
        g_nicks.erase(id);
        ForgetElementReferences(id);
    }
    WorldRPG::Sync::PedSyncState::Forget(id);
    Game::ForgetPedNativeTaskState(id);
    // The body is destroyed on the game thread (ApplyRemotePeds g_forget -> DropBody -> DestroyRemotePlayerPed).
    Game::ForgetRemote(id);
    WorldRPG::RemoteVehicle::ForgetElement(id);
    DIAG("PLAYER_QUIT id=%u removed", id);
}

void OnCustomData(const uint8_t* body, size_t length, size_t bits) {
    Net::BitStreamReader reader(body, length, bits);
    uint32_t id = 0, nameLength = 0;
    if (!ReadElementId(reader, id) || !ReadCompressed(reader, 2, nameLength) || nameLength == 0 || nameLength > 128)
        return;
    std::vector<uint8_t> name(nameLength);
    if (!reader.ReadBits(name.data(), nameLength * 8)) return;
    Datum value;
    if (!ReadLuaValue(reader, value)) return;
    DIAG("CUSTOM_DATA id=%u local=%u name=%.*s number=%d boolean=%d value=%g", id, g_local,
         static_cast<int>(name.size()), reinterpret_cast<const char*>(name.data()), value.number ? 1 : 0,
         value.boolean ? 1 : 0, value.value);
    std::lock_guard<std::mutex> lock(g_lock);
    g_data[id][std::string(name.begin(), name.end())] = value;
}

bool ReadSetElementDataRpc(uint32_t id, Net::BitStreamReader& reader) {
    uint32_t nameLength = 0;
    // MAX_CUSTOMDATA_NAME_LENGTH is 128 (desktop logs and drops a longer name).
    if (!ReadCompressed(reader, 2, nameLength) || nameLength > 128) return false;
    std::vector<uint8_t> name(nameLength);
    if (nameLength && !reader.ReadBits(name.data(), nameLength * 8)) return false;
    Datum value;
    if (!ReadLuaValue(reader, value)) return false;
    std::lock_guard<std::mutex> lock(g_lock);
    g_data[id][std::string(name.begin(), name.end())] = value;
    return true;
}

bool ReadRemoveElementDataRpc(uint32_t id, Net::BitStreamReader& reader) {
    uint32_t nameLength = 0;
    if (!ReadCompressed(reader, 2, nameLength) || nameLength > 128) return false;
    std::vector<uint8_t> name(nameLength);
    if (nameLength && !reader.ReadBits(name.data(), nameLength * 8)) return false;
    bool recursive = false;
    if (!ReadFlag(reader, recursive)) return false;
    std::lock_guard<std::mutex> lock(g_lock);
    auto element = g_data.find(id);
    if (element != g_data.end()) element->second.erase(std::string(name.begin(), name.end()));
    return true;
}

void OnPlayerList(const uint8_t* body, size_t length, size_t bits) {
    {
        // Ground truth for the mobile-peer layout: the first records decode to ids like 6 and 48384 (device
        // 2026-11-14), so the wire differs from CPlayerListPacket::Write before the first ElementID. First 96 bytes.
        static int dumps = 0;
        if (dumps < 3) {
            ++dumps;
            char hex[96 * 2 + 1] = {};
            const size_t n = length < 96 ? length : 96;
            for (size_t i = 0; i < n; ++i) std::snprintf(hex + i * 2, 3, "%02x", static_cast<unsigned>(body[i]));
            DIAG("PLAYER_LIST body bytes=%zu bits=%zu %s", length, bits, hex);
        }
    }
    // Desktop Packet_PlayerList first reads two strings, AC info and SD info (ReadString: ushort length + bytes,
    // CPacketHandler.cpp 831-834), then the bJustJoined bit. CPlayerListPacket::Write (server) does not write them, so
    // the net module adds them, and their bytes on the mobile peer are not two plain ReadStrings: the captured body
    // (device 2026-11-14) starts 03 00 | 02 00 "-1" | 00 00, and the first record decodes at bit 64
    // (bJustJoined 1, context 1, nick length 17, the real nick). The phone used to start at bit 0, which made every
    // record garbage (ids 6, 48384), so no player model was stored and a CJ player showed as skin 7 until it respawned.
    // Each layout is tried and the one whose first record carries a plausible nick is used.
    auto probe = [&](size_t skip) {
        Net::BitStreamReader r(body, length, bits);
        std::vector<uint8_t> dump((skip + 7) / 8 + 1);
        if (skip && !r.ReadBits(dump.data(), skip)) return false;
        bool flag = false;
        uint32_t pid = 0;
        uint8_t ctx = 0, nl = 0;
        if (!ReadFlag(r, flag) || !ReadElementId(r, pid) || !r.Read(ctx) || !r.Read(nl)) return false;
        if (nl < 1 || nl > 32) return false;
        std::vector<uint8_t> chars(nl);
        if (!r.ReadBits(chars.data(), nl * 8u)) return false;
        for (uint8_t c : chars)
            if (c < 0x20 || c > 0x7e) return false;
        return true;
    };
    auto stringsAt = [&](size_t start, size_t& end) {
        // Two ReadString from `start` (bits). False when a length runs past the body.
        Net::BitStreamReader r(body, length, bits);
        std::vector<uint8_t> dump((start + 7) / 8 + 1);
        if (start && !r.ReadBits(dump.data(), start)) return false;
        for (int i = 0; i < 2; ++i) {
            uint16_t len = 0;
            if (!r.Read(len) || len > 256 || !SkipBytes(r, len)) return false;
        }
        end = r.GetReadOffsetBits();
        return true;
    };
    size_t skip = 0;
    bool found = false;
    size_t end = 0;
    if (stringsAt(0, end) && probe(end)) { skip = end; found = true; }             // desktop: AC, SD
    if (!found && stringsAt(16, end) && probe(end)) { skip = end; found = true; }  // ushort, then AC, SD (seen live)
    if (!found && probe(0)) { skip = 0; found = true; }                            // no prefix
    if (!found) {
        DIAG("PLAYER_LIST: no layout gives a readable first record (bits=%zu); packet ignored", bits);
        return;
    }
    DIAG("PLAYER_LIST layout: records start at bit %zu", skip);
    Net::BitStreamReader reader(body, length, bits);
    if (skip) {
        std::vector<uint8_t> dump((skip + 7) / 8 + 1);
        if (!reader.ReadBits(dump.data(), skip)) return;
    }
    bool showInChat = false;
    if (!ReadFlag(reader, showInChat)) return;
    (void)showInChat;
    std::vector<std::pair<uint32_t, Game::PedRpcOp>> weapons;
    std::unique_lock<std::mutex> lock(g_lock);
    int players = 0;
    while (reader.GetRemainingBits() > 40 && players < 64) {
        uint32_t id = 0;
        uint8_t context = 0, nickLength = 0;
        if (!ReadElementId(reader, id) || !reader.Read(context) || !reader.Read(nickLength) || nickLength > 32)
            break;
        std::string nick(nickLength, '\0');
        if (nickLength && !reader.ReadBits(reinterpret_cast<uint8_t*>(nick.data()), nickLength * 8)) break;
        DIAG("PLAYER_LIST record id=%u context=%u nick=%s bit=%zu", id, static_cast<unsigned>(context), nick.c_str(),
             reader.GetReadOffsetBits());
        uint16_t version = 0;
        uint32_t build = 0;
        if (!reader.Read(version) || !reader.Read(build)) break;
        uint8_t flags = 0;
        if (!reader.Read(flags)) break;
        uint8_t tagLength = 0;
        if (!reader.Read(tagLength) || !SkipBytes(reader, tagLength)) break;
        if (flags & 0x04) {
            uint8_t color[3] = {};
            if (!reader.ReadBits(color, 24)) break;
        }
        uint8_t moveAnim = 0;
        if (!reader.Read(moveAnim)) { DIAG("PLAYER_LIST id=%u stop at moveAnim bit=%zu", id, reader.GetReadOffsetBits()); break; }
        // bIsSpawned (0x40, the second flag bit): the model, team, pose, dimension, style, alpha, interior, weapons and
        // animation are only on the wire for a spawned player (CPacketHandler.cpp 934-995 and 1043-1104).
        PedQueries::Ensure(id, true);
        if (flags & 0x80) PedQueries::Wasted(id);
        if (!(flags & 0x40)) {
            g_nicks[id] = nick;
            DIAG("PLAYER_LIST id=%u not spawned; record ends after moveAnim", id);
            continue;
        }
        uint32_t model = 0;
        if (!ReadCompressed(reader, 2, model)) { DIAG("PLAYER_LIST id=%u stop at model bit=%zu", id, reader.GetReadOffsetBits()); break; }
        Visual visual;
        visual.kind = 1;
        visual.model = static_cast<int>(model);
        Remember(id, visual);
        g_nicks[id] = nick;
        ++players;
        bool hasTeam = false;
        if (!ReadFlag(reader, hasTeam)) break;
        if (hasTeam) {
            uint32_t team = 0;
            if (!ReadElementId(reader, team)) break;
        }
        const bool inVehicle = (flags & 0x20) != 0;
        if (inVehicle) {
            uint32_t vehicle = 0;
            uint8_t seat = 0;
            if (!ReadElementId(reader, vehicle) || !reader.ReadBits(&seat, 4)) break;
        } else {
            float x = 0, y = 0, z = 0;
            uint8_t rotation[2] = {};
            if (!ReadPosition(reader, x, y, z) || !reader.ReadBits(rotation, 16)) break;
            g_visuals[id].x = x;
            g_visuals[id].y = y;
            g_visuals[id].z = z;
        }
        uint32_t dimension = 0, alpha = 0;
        uint8_t style = 0, interior = 0;
        if (!ReadCompressed(reader, 2, dimension) || !reader.Read(style)) { DIAG("PLAYER_LIST id=%u stop at dimension/style bit=%zu", id, reader.GetReadOffsetBits()); break; }
        // CClientPlayer::SetDimension (Packet_PlayerList). The local player's entry sets the streamer dimension.
        g_visuals[id].dimension = static_cast<uint16_t>(dimension);
        if (id == g_local) g_localDimension = static_cast<uint16_t>(dimension);
        if (!ReadCompressed(reader, 1, alpha) || !reader.Read(interior)) { DIAG("PLAYER_LIST id=%u stop at alpha/interior bit=%zu", id, reader.GetReadOffsetBits()); break; }
        g_visuals[id].interior=interior;
        if(id==g_local) g_localInterior=interior;
        bool animated = false;
        for (int slot = 0; slot < 16; ++slot) {
            bool occupied = false;
            if (!ReadFlag(reader, occupied)) { animated = true; break; }
            if (occupied) {
                uint8_t weapon = 0;
                if (!reader.ReadBits(&weapon, 6)) { animated = true; break; }
                Game::PedRpcOp give;
                give.kind = Game::PedRpcOp::Kind::GiveWeapon;
                give.weapon = weapon & 0x3F;
                give.ammo = 1;
                give.queryGeneration = PedQueries::Generation(id);
                weapons.emplace_back(id, give);
            }
        }
        if (animated || !ReadFlag(reader, animated)) { DIAG("PLAYER_LIST id=%u stop at weapons bit=%zu", id, reader.GetReadOffsetBits()); break; }
        // Packet_PlayerList animation block (CPacketHandler.cpp 1084-1104): ReadString block and anim (ushort length +
        // bytes), int time, looped / updatePosition / interruptable / freezeLastFrame bits, int blendTime, taskRestore
        // bit, double startTime, float speed. The reader used to stop the whole packet here, so every player listed
        // after an animated one kept no model (a CJ desktop player then showed as skin 7 after a reconnect).
        // The animation itself is not applied (SetPedAnimation has no hook yet).
        if (animated) {
            std::string block, anim;
            int32_t time = 0, blend = 0;
            double startTime = 0;
            float speed = 0;
            bool looped = false, updatePosition = false, interruptable = false, freezeLast = false, restore = false;
            if (!Entity::PedWire::ReadLengthPrefixedString(reader, block) || !Entity::PedWire::ReadLengthPrefixedString(reader, anim) ||
                !reader.Read(time) || !ReadFlag(reader, looped) || !ReadFlag(reader, updatePosition) ||
                !ReadFlag(reader, interruptable) || !ReadFlag(reader, freezeLast) || !reader.Read(blend) ||
                !ReadFlag(reader, restore) || !reader.ReadBits(reinterpret_cast<uint8_t*>(&startTime), 64) || !reader.Read(speed))
                break;
            DIAG("PLAYER_LIST id=%u animation %s/%s (not applied)", id, block.c_str(), anim.c_str());
        }
    }
    lock.unlock();
    // Never acquire the game registry lock while holding the world registry.
    for (const auto& [id, give] : weapons) Game::QueuePedRpc(id, give);
    if (players > 0) SW_LOGI("PLAYER_LIST stored %d player model(s), %zu bits left", players, reader.GetRemainingBits());
}

// Takes the descriptor ParseResourceStart already built for either layout (desktop or mobile shim),
// so the layout is detected once, in ResourceFiles.cpp, and not read a second time here.
void OnResourceStart(const Net::ResourceStartInfo& info) {
    constexpr uint32_t kInvalidElement = 0xFFFFFFFFu;
    const uint32_t root = info.entityId == kInvalidElement ? 0 : info.entityId;
    SW_LOGI("Resource %s id=%u root=%u", info.name.c_str(), static_cast<unsigned>(info.resourceId), static_cast<unsigned>(root));
    std::lock_guard<std::mutex> lock(g_lock);
    g_resourceRoot = root;
    Visual visual;
    visual.kind = 0;
    Remember(root, visual);
    Remember(info.dynamicEntityId, visual);
}

void ForgetPlaceholder(uint32_t id) {
    if (id == 0 || id == 0xFFFFFFFFu) return;
    std::lock_guard<std::mutex> lock(g_lock);
    auto found = g_visuals.find(id);
    if (found == g_visuals.end() || found->second.kind != 0) return;
    g_visuals.erase(found);
    ForgetElementReferences(id);
    if (g_resourceRoot == id) g_resourceRoot = 0;
}

void Tick(float playerX, float playerY, float playerZ) {
#ifdef __ANDROID__
    ResolveBlips();
    if (!g_setBlip) return;
    std::lock_guard<std::mutex> lock(g_lock);
    if (g_clearBlip) {
        for (int handle : g_staleBlips) g_clearBlip(handle);
    }
    g_staleBlips.clear();
    for (auto& [id, visual] : g_visuals) {
        if (visual.kind != 5 && visual.kind != 4) continue;
        float x = visual.x, y = visual.y, z = visual.z;
        if (visual.attached != 0xFFFFFFFFu) {
            auto parent = g_visuals.find(visual.attached);
            if (parent != g_visuals.end()) {
                x += parent->second.x;
                y += parent->second.y;
                z += parent->second.z;
            }
        }
        const float dx = x - playerX, dy = y - playerY;
        const bool near = dx * dx + dy * dy < 350.f * 350.f;
        if (!near && visual.blipLive && g_clearBlip) {
            g_clearBlip(visual.blipHandle);
            visual.blipLive = false;
        }
        if (!near || visual.blipLive) continue;
        char name[16] = "wrpg";
        const unsigned color = (static_cast<unsigned>(visual.a) << 24) | (static_cast<unsigned>(visual.r) << 16) |
                               (static_cast<unsigned>(visual.g) << 8) | static_cast<unsigned>(visual.b);
        visual.blipHandle = g_setBlip(4, Vec3{x, y, z}, color, 2, name);
        visual.blipLive = visual.blipHandle != 0;
        (void)id;
    }
#else
    (void)playerX; (void)playerY; (void)playerZ;
#endif
}

std::string MapSnapshot(float playerX, float playerY, float playerHeading) {
    std::lock_guard<std::mutex> lock(g_lock);
    char line[128];
    std::string out;
    std::snprintf(line, sizeof(line), "P %.2f %.2f %.3f\n", playerX, playerY, playerHeading);
    out += line;
    int drawn = 0;
    for (const auto& [id, visual] : g_visuals) {
        if (drawn > 200) break;
        if (visual.kind != 4 && visual.kind != 5 && visual.kind != 7) continue;
        float x = visual.x, y = visual.y;
        if (visual.attached != 0xFFFFFFFFu) {
            auto parent = g_visuals.find(visual.attached);
            if (parent != g_visuals.end()) { x += parent->second.x; y += parent->second.y; }
        }
        if (visual.kind == 7) {
            std::snprintf(line, sizeof(line), "A %.1f %.1f %.1f %.1f %d %d %d %d\n",
                x, y, visual.w, visual.h, visual.r, visual.g, visual.b, visual.a);
        } else {
            std::snprintf(line, sizeof(line), "%c %.1f %.1f %d %d %d %d\n",
                visual.kind == 5 ? 'B' : 'M', x, y, visual.r, visual.g, visual.b, visual.icon);
        }
        out += line;
        ++drawn;
        (void)id;
    }
    return out;
}

} // namespace WorldRPG::ServerWorld
