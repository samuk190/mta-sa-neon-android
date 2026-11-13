#include "ServerWorld.h"
#include "ResourceFiles.h"
#include "../gtasa/GameEngine.h"
#include "../net/raknet/RakNetWrapper.h"
#include "entity/EntityAddTypes.h"

#include <cmath>
#include <cstring>
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
uint32_t g_local = 0;
uint32_t g_resourceRoot = 0;
std::unordered_map<uint32_t, Visual> g_visuals;
std::unordered_map<uint32_t, std::unordered_map<std::string, Datum>> g_data;

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
    stored.blipHandle = oldBlip;
    stored.blipLive = live;
    if (visual.model >= 0) stored.model = visual.model;
}

} // namespace

void SetLocalPlayer(uint32_t id) { g_local = id; }
uint32_t LocalPlayer() { return g_local; }
uint32_t LastResourceRoot() { return g_resourceRoot; }

int CopyActors(ActorSpawn* out, int capacity) {
    if (!out || capacity <= 0) return 0;
    std::lock_guard<std::mutex> lock(g_lock);
    int count = 0;
    for (const auto& [id, visual] : g_visuals) {
        if (visual.kind != 14 && visual.kind != 2) continue;
        if (count >= capacity) break;
        ActorSpawn& slot = out[count++];
        slot.id = id;
        slot.kind = visual.kind;
        slot.model = visual.model;
        slot.x = visual.x;
        slot.y = visual.y;
        slot.z = visual.z;
        slot.heading = visual.heading;
        slot.hosted = visual.hosted;
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

const char* ElementKindName(uint32_t id) {
    std::lock_guard<std::mutex> lock(g_lock);
    if (id == g_local) return "player";
    auto found = g_visuals.find(id);
    if (found == g_visuals.end()) return id == g_resourceRoot ? "resource" : "element";
    switch (found->second.kind) {
        case 1: return "player";
        case 2: return "vehicle";
        case 14: return "ped";
        case 4: return "marker";
        case 5: return "blip";
        default: return "element";
    }
}

void NotePlayerSkin(uint32_t id, int model) {
    if (id == 0 || id == 0xFFFFFFFFu || model < 0 || model > 312) return;
    std::lock_guard<std::mutex> lock(g_lock);
    auto& stored = g_visuals[id];
    stored.kind = 1;
    stored.model = model;
}

int ModelOf(uint32_t id) {
    std::lock_guard<std::mutex> lock(g_lock);
    auto found = g_visuals.find(id);
    if (found == g_visuals.end()) return -1;
    return found->second.model;
}

bool GetData(uint32_t id, const char* key, double& number, std::string& text, bool& isNumber) {
    if (!key) return false;
    std::lock_guard<std::mutex> lock(g_lock);
    auto element = g_data.find(id);
    if (element == g_data.end()) return false;
    auto found = element->second.find(key);
    if (found == element->second.end()) return false;
    isNumber = found->second.number;
    number = found->second.value;
    text = found->second.text;
    return true;
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
    std::lock_guard<std::mutex> lock(g_lock);
    int stored = 0;
    for (uint32_t index = 0; index < count; ++index) {
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
        if (!ReadEntityBody(reader, common, visual)) {
            DIAG("ENTITY_ADD fail idx=%u/%u id=%u type=%u bit=%zu", index, static_cast<unsigned>(count), common.id, static_cast<unsigned>(common.type), reader.GetReadOffsetBits());
            break;
        }
        if (common.type != kTypePlayer) Remember(common.id, visual);
        ++stored;
    }
    DIAG("ENTITY_ADD stored %d/%u", stored, static_cast<unsigned>(count));
}

void OnEntityRemove(const uint8_t* body, size_t length, size_t bits) {
    Net::BitStreamReader reader(body, length, bits);
    uint32_t id = 0;
    if (!ReadElementId(reader, id)) return;
    {
        std::lock_guard<std::mutex> lock(g_lock);
        g_visuals.erase(id);
        g_data.erase(id);
    }
    Game::ForgetRemote(id);
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
    std::lock_guard<std::mutex> lock(g_lock);
    g_data[id][std::string(name.begin(), name.end())] = value;
}

void OnPlayerList(const uint8_t* body, size_t length, size_t bits) {
    Net::BitStreamReader reader(body, length, bits);
    bool showInChat = false;
    if (!ReadFlag(reader, showInChat)) return;
    (void)showInChat;
    std::lock_guard<std::mutex> lock(g_lock);
    int players = 0;
    while (reader.GetRemainingBits() > 40 && players < 64) {
        uint32_t id = 0;
        uint8_t context = 0, nickLength = 0;
        if (!ReadElementId(reader, id) || !reader.Read(context) || !reader.Read(nickLength) || nickLength > 32)
            break;
        if (!SkipBytes(reader, nickLength)) break;
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
        if (!reader.Read(moveAnim)) break;
        uint32_t model = 0;
        if (!ReadCompressed(reader, 2, model)) break;
        Visual visual;
        visual.kind = 1;
        visual.model = static_cast<int>(model);
        Remember(id, visual);
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
        if (!ReadCompressed(reader, 2, dimension) || !reader.Read(style)) break;
        if (!ReadCompressed(reader, 1, alpha) || !reader.Read(interior)) break;
        bool animated = false;
        for (int slot = 0; slot < 16; ++slot) {
            bool occupied = false;
            if (!ReadFlag(reader, occupied)) { animated = true; break; }
            if (occupied) {
                uint8_t weapon = 0;
                if (!reader.ReadBits(&weapon, 6)) { animated = true; break; }
            }
        }
        if (animated || !ReadFlag(reader, animated) || animated) break;
    }
    if (players > 0) SW_LOGI("PLAYER_LIST stored %d player model(s)", players);
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

void Tick(float playerX, float playerY, float playerZ) {
#ifdef __ANDROID__
    ResolveBlips();
    if (!g_setBlip) return;
    std::lock_guard<std::mutex> lock(g_lock);
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
