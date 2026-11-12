#include "ServerWorld.h"
#include "../gtasa/GameEngine.h"
#include "../net/raknet/RakNetWrapper.h"

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

struct Visual {
    int kind = 0; // 4 marker, 5 blip, 7 area, 14 ped, 1 player
    int model = -1;
    float x = 0, y = 0, z = 0, w = 0, h = 0, size = 1;
    int icon = 0;
    int r = 255, g = 255, b = 255, a = 255;
    int markerType = 2;
    float heading = 0;
    bool hosted = false;
    uint32_t attached = 0xFFFFFFFFu;
    int blipHandle = -1;
    bool blipLive = false;
};

struct Datum {
    bool number = false;
    double value = 0;
    std::string text;
};

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

bool ReadFlag(Net::BitStreamReader& reader, bool& out) {
    uint8_t bit = 0;
    if (!reader.ReadBits(&bit, 1)) return false;
    out = (bit & 1u) != 0;
    return true;
}

bool ReadElementId(Net::BitStreamReader& reader, uint32_t& out) {
    uint8_t bytes[4] = {};
    if (!reader.ReadBits(bytes, 17)) return false;
    uint32_t value = 0;
    std::memcpy(&value, bytes, 4);
    out = value == ((1u << 17) - 1) ? 0xFFFFFFFFu : value;
    return true;
}

bool ReadCompressed(Net::BitStreamReader& reader, int bytes, uint32_t& out) {
    out = 0;
    if (bytes < 1 || bytes > 4) return false;
    for (int i = bytes - 1; i > 0; --i) {
        bool empty = false;
        if (!ReadFlag(reader, empty)) return false;
        if (!empty) {
            uint8_t buf[4] = {};
            if (!reader.ReadBits(buf, static_cast<size_t>((i + 1) * 8))) return false;
            for (int b = 0; b <= i; ++b) out |= static_cast<uint32_t>(buf[b]) << (8 * b);
            return true;
        }
    }
    bool highNibbleEmpty = false;
    if (!ReadFlag(reader, highNibbleEmpty)) return false;
    if (!highNibbleEmpty) {
        uint8_t low = 0;
        if (!reader.Read(low)) return false;
        out = low;
        return true;
    }
    uint8_t nibble = 0;
    if (!reader.ReadBits(&nibble, 4)) return false;
    out = nibble & 0x0Fu;
    return true;
}

bool ReadFixed24(Net::BitStreamReader& reader, float& out) {
    uint8_t bytes[4] = {};
    if (!reader.ReadBits(bytes, 24)) return false;
    int32_t raw = bytes[0] | (bytes[1] << 8) | (bytes[2] << 16);
    if (raw & 0x800000) raw |= ~0xFFFFFF;
    out = static_cast<float>(raw) / 1024.f;
    return true;
}

bool ReadPosition(Net::BitStreamReader& reader, float& x, float& y, float& z) {
    return ReadFixed24(reader, x) && ReadFixed24(reader, y) && reader.Read(z);
}

bool ReadPosition2(Net::BitStreamReader& reader, float& x, float& y) {
    return ReadFixed24(reader, x) && ReadFixed24(reader, y);
}

bool SkipBytes(Net::BitStreamReader& reader, uint32_t count) {
    if (count > 1024 * 1024) return false;
    std::vector<uint8_t> dump(count);
    return count == 0 || reader.ReadBits(dump.data(), count * 8);
}

bool SkipLuaBody(Net::BitStreamReader& reader, uint8_t kind, int depth);

bool SkipLua(Net::BitStreamReader& reader, int depth) {
    uint8_t kind = 0;
    if (!reader.ReadBits(&kind, 4)) return false;
    return SkipLuaBody(reader, kind & 0x0Fu, depth);
}

bool SkipLuaBody(Net::BitStreamReader& reader, uint8_t kind, int depth) {
    if (depth > 12) return false;
    if (kind == 0) return true;
    if (kind == 1) {
        bool value = false;
        return ReadFlag(reader, value);
    }
    if (kind == 3) {
        bool wide = false;
        if (!ReadFlag(reader, wide)) return false;
        if (wide) {
            bool asDouble = false;
            if (!ReadFlag(reader, asDouble)) return false;
            if (asDouble) {
                uint8_t bytes[8] = {};
                return reader.ReadBits(bytes, 64);
            }
            float number = 0;
            return reader.Read(number);
        }
        uint32_t integer = 0;
        return ReadCompressed(reader, 4, integer);
    }
    if (kind == 4 || kind == 9) {
        uint32_t length = 0;
        const int width = kind == 4 ? 2 : 4;
        if (!ReadCompressed(reader, width, length) || length > 1024 * 1024) return false;
        return SkipBytes(reader, length);
    }
    if (kind == 5) {
        uint32_t count = 0;
        if (!ReadCompressed(reader, 4, count) || count > 4096) return false;
        for (uint32_t i = 0; i < count; ++i) if (!SkipLua(reader, depth + 1)) return false;
        return true;
    }
    if (kind == 2 || kind == 7) {
        uint32_t id = 0;
        return ReadElementId(reader, id);
    }
    if (kind == 10) {
        uint32_t ref = 0;
        return ReadCompressed(reader, 4, ref);
    }
    return false;
}

// Effective peer bitstream for entity/ped data. NeonJoinProtocol pins the peer to 0x0030,
// so the server writes the 0x0030 layout even though MOD_NAME reports 0x3D.
constexpr uint16_t kServerEntityBitStream = 0x0030;
#define DIAG(...) SW_LOGI("DIAG " __VA_ARGS__)
#define LUA_FAIL() do { DIAG("lua fail line=%d kind=%u bit=%zu", __LINE__, static_cast<unsigned>(kind), reader.GetReadOffsetBits()); return false; } while (0)
#define CB_FAIL() do { DIAG("customblock fail line=%d count=%u bit=%zu", __LINE__, static_cast<unsigned>(count), reader.GetReadOffsetBits()); return false; } while (0)
#define SN_FAIL() do { DIAG("skipname fail line=%d len=%u bit=%zu", __LINE__, static_cast<unsigned>(length), reader.GetReadOffsetBits()); return false; } while (0)
#define ENTITY_FAIL() do { DIAG("ENTITY_ADD fail line=%d idx=%u/%u id=%u type=%u bit=%zu", __LINE__, index, count, static_cast<unsigned>(id), static_cast<unsigned>(type), reader.GetReadOffsetBits()); return; } while (0)

bool ReadLuaValue(Net::BitStreamReader& reader, Datum& out) {
    uint8_t kind = 0;
    if (!reader.ReadBits(&kind, 4)) LUA_FAIL();
    kind &= 0x0Fu;
    DIAG("lua value kind=%u bit=%zu", static_cast<unsigned>(kind), reader.GetReadOffsetBits());
    if (kind == 0) {
        out.number = false;
        out.text.clear();
        return true;
    }
    if (kind == 1) {
        bool value = false;
        if (!ReadFlag(reader, value)) LUA_FAIL();
        out.number = true;
        out.value = value ? 1 : 0;
        return true;
    }
    if (kind == 3) {
        bool wide = false;
        if (!ReadFlag(reader, wide)) LUA_FAIL();
        if (wide) {
            bool asDouble = false;
            if (!ReadFlag(reader, asDouble)) LUA_FAIL();
            if (asDouble) {
                uint8_t bytes[8] = {};
                if (!reader.ReadBits(bytes, 64)) LUA_FAIL();
                double number = 0;
                std::memcpy(&number, bytes, 8);
                out.number = true;
                out.value = number;
                return true;
            }
            float number = 0;
            if (!reader.Read(number)) LUA_FAIL();
            out.number = true;
            out.value = number;
            return true;
        }
        uint32_t integer = 0;
        if (!ReadCompressed(reader, 4, integer)) LUA_FAIL();
        out.number = true;
        out.value = static_cast<int32_t>(integer);
        return true;
    }
    if (kind == 4) {
        uint32_t length = 0;
        if (!ReadCompressed(reader, 2, length) || length > 65535) LUA_FAIL();
        if (length > 512) {
            out.number = false;
            out.text.clear();
            return SkipBytes(reader, length);
        }
        std::vector<uint8_t> bytes(length);
        if (length && !reader.ReadBits(bytes.data(), length * 8)) LUA_FAIL();
        out.number = false;
        out.text.assign(bytes.begin(), bytes.end());
        return true;
    }
    // The type nibble is already consumed. Skipping must not read it again.
    out.number = false;
    out.text.clear();
    DIAG("lua skip-body kind=%u bit=%zu", static_cast<unsigned>(kind), reader.GetReadOffsetBits());
    return SkipLuaBody(reader, kind, 0);
}

bool ReadCustomBlock(Net::BitStreamReader& reader, uint32_t id) {
    uint32_t count = 0;
    if (!ReadCompressed(reader, 2, count) || count > 64) CB_FAIL();
    DIAG("customblock id=%u count=%u bit=%zu", id, static_cast<unsigned>(count), reader.GetReadOffsetBits());
    for (uint32_t i = 0; i < count; ++i) {
        uint8_t nameLength = 0;
        if (!reader.Read(nameLength)) CB_FAIL();
        std::vector<uint8_t> name(nameLength);
        if (nameLength && !reader.ReadBits(name.data(), nameLength * 8)) CB_FAIL();
        DIAG("customblock item %u/%u len=%u name=%.*s bit=%zu", i, static_cast<unsigned>(count), nameLength, static_cast<int>(name.size()), reinterpret_cast<const char*>(name.data()), reader.GetReadOffsetBits());
        Datum value;
        if (!ReadLuaValue(reader, value)) CB_FAIL();
        if (id != 0 && id != 0xFFFFFFFFu) {
            std::string key(name.begin(), name.end());
            g_data[id][key] = value;
        }
    }
    return true;
}

bool SkipName(Net::BitStreamReader& reader) {
    uint32_t length = 0;
    if (!ReadCompressed(reader, 2, length) || length > 256) SN_FAIL();
    return SkipBytes(reader, length);
}

bool ReadAngle16(Net::BitStreamReader& reader, float& out) {
    uint8_t raw[2] = {};
    if (!reader.ReadBits(raw, 16)) return false;
    const unsigned value = raw[0] | (raw[1] << 8);
    constexpr float pi = 3.14159265f;
    out = -pi + (static_cast<float>(value) / 65535.f) * (2.f * pi);
    return true;
}

bool ReadDoorRatio(Net::BitStreamReader& reader) {
    bool wide = false;
    if (!ReadFlag(reader, wide)) return false;
    if (!wide) {
        bool open = false;
        return ReadFlag(reader, open);
    }
    uint8_t bits[2] = {};
    return reader.ReadBits(bits, 10);
}

bool VehicleHasDoors(int model) {
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

bool ReadPedTail(Net::BitStreamReader& reader, Visual& visual) {
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

bool ReadVehicleTail(Net::BitStreamReader& reader, Visual& visual) {
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
    if (count == 0 || count > 512) { DIAG("ENTITY_ADD bad count=%u", static_cast<unsigned>(count)); return; }
    std::lock_guard<std::mutex> lock(g_lock);
    int stored = 0;
    for (uint32_t index = 0; index < count; ++index) {
        uint32_t id = 0, parent = 0;
        uint8_t type = 0, interior = 0;
        uint32_t dimension = 0;
        bool attached = false, collisions = false, callProp = false;
        if (!ReadElementId(reader, id) || !reader.Read(type) || !ReadElementId(reader, parent) ||
            !reader.Read(interior) || !ReadCompressed(reader, 2, dimension) || !ReadFlag(reader, attached))
            ENTITY_FAIL();
        DIAG("ENTITY_ADD idx=%u/%u id=%u type=%u parent=%u interior=%u dim=%u attached=%d bit=%zu", index, static_cast<unsigned>(count), id, static_cast<unsigned>(type), parent, static_cast<unsigned>(interior), dimension, attached ? 1 : 0, reader.GetReadOffsetBits());
        if (attached) {
            uint32_t target = 0;
            float ax = 0, ay = 0, az = 0;
            if (!ReadElementId(reader, target) || !ReadPosition(reader, ax, ay, az)) ENTITY_FAIL();
            uint8_t angles[6] = {};
            if (!reader.ReadBits(angles, 48)) ENTITY_FAIL();
            (void)ax; (void)ay; (void)az;
        }
        if (!ReadFlag(reader, collisions)) ENTITY_FAIL();
        // CPacketHandler::Packet_EntityAdd reads the call-propagation bit only when the peer has
        // ExtendedWorldPositions (0x31+). Below that the server omits it.
        if (kServerEntityBitStream >= 0x0031 && !ReadFlag(reader, callProp)) ENTITY_FAIL();
        (void)collisions; (void)callProp; (void)interior; (void)dimension; (void)parent;
        if (!ReadCustomBlock(reader, id) || !SkipName(reader)) ENTITY_FAIL();
        DIAG("ENTITY_ADD custom+name ok id=%u type=%u bit=%zu", id, static_cast<unsigned>(type), reader.GetReadOffsetBits());
        Visual visual;
        visual.kind = type;
        if (type == 0) {
            // DUMMY (type 0): CPacketHandler::Packet_EntityAdd has no DUMMY case, so nothing is read
            // after the common block. The name was already consumed by the common block.
            Remember(id, visual);
        } else if (type == 1) {
            // Players are described by PLAYER_LIST.
        } else if (type == 4) {
            uint8_t markerType = 0;
            if (!ReadPosition(reader, visual.x, visual.y, visual.z) || !reader.ReadBits(&markerType, 3)) ENTITY_FAIL();
            visual.markerType = markerType & 7;
            if (!reader.Read(visual.size)) ENTITY_FAIL();
            uint8_t color[4] = {};
            if (!reader.ReadBits(color, 32)) ENTITY_FAIL();
            visual.r = color[0]; visual.g = color[1]; visual.b = color[2]; visual.a = color[3];
            if (visual.markerType == 0 || visual.markerType == 1) {
                bool hasTarget = false;
                if (!ReadFlag(reader, hasTarget)) ENTITY_FAIL();
                if (hasTarget) {
                    float tx = 0, ty = 0, tz = 0;
                    if (!ReadPosition(reader, tx, ty, tz)) ENTITY_FAIL();
                    if (visual.markerType == 0) {
                        uint8_t arrow[4] = {};
                        float arrowSize = 0;
                        if (!reader.ReadBits(arrow, 32) || !reader.Read(arrowSize)) ENTITY_FAIL();
                    }
                }
            }
            bool ignore = false;
            if (!ReadFlag(reader, ignore)) ENTITY_FAIL();
            Remember(id, visual);
            ++stored;
        } else if (type == 5) {
            uint32_t ordering = 0, distance = 0;
            uint8_t icon = 0, iconSize = 0;
            if (!ReadPosition(reader, visual.x, visual.y, visual.z) || !ReadCompressed(reader, 2, ordering)) ENTITY_FAIL();
            uint8_t rawDistance[2] = {};
            if (!reader.ReadBits(rawDistance, 14)) ENTITY_FAIL();
            distance = rawDistance[0] | ((rawDistance[1] & 0x3Fu) << 8);
            if (!reader.ReadBits(&icon, 6) || !reader.ReadBits(&iconSize, 5)) ENTITY_FAIL();
            visual.icon = icon & 0x3Fu;
            visual.size = static_cast<float>(iconSize & 0x1Fu);
            uint8_t color[4] = {};
            if (!reader.ReadBits(color, 32)) ENTITY_FAIL();
            visual.r = color[0]; visual.g = color[1]; visual.b = color[2]; visual.a = color[3];
            (void)ordering; (void)distance;
            if (attached) visual.attached = parent;
            Remember(id, visual);
            ++stored;
        } else if (type == 7) {
            if (!ReadPosition2(reader, visual.x, visual.y) || !ReadPosition2(reader, visual.w, visual.h)) ENTITY_FAIL();
            uint8_t color[4] = {};
            bool flashing = false;
            if (!reader.ReadBits(color, 32) || !ReadFlag(reader, flashing)) ENTITY_FAIL();
            visual.r = color[0]; visual.g = color[1]; visual.b = color[2]; visual.a = color[3];
            Remember(id, visual);
            ++stored;
        } else if (type == 14) {
            uint32_t model = 0;
            if (!ReadPosition(reader, visual.x, visual.y, visual.z) || !ReadCompressed(reader, 2, model)) ENTITY_FAIL();
            visual.model = static_cast<int>(model);
            if (!ReadPedTail(reader, visual)) ENTITY_FAIL();
            Remember(id, visual);
            ++stored;
        } else if (type == 2) {
            if (!ReadPosition(reader, visual.x, visual.y, visual.z)) ENTITY_FAIL();
            if (!ReadVehicleTail(reader, visual)) {
                if (visual.model > 0) {
                    Remember(id, visual);
                    ++stored;
                }
                SW_LOGI("ENTITY_ADD vehicle %u stopped after the common block", id);
                break;
            }
            Remember(id, visual);
            ++stored;
        } else {
            SW_LOGI("ENTITY_ADD stopped at type %u id %u", type, id);
            break;
        }
    }
    if (stored > 0) SW_LOGI("ENTITY_ADD stored %d visual element(s)", stored);
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

void OnResourceStart(const uint8_t* body, size_t length, size_t bits) {
    Net::BitStreamReader reader(body, length, bits);
    uint8_t nameLength = 0;
    if (!reader.Read(nameLength) || nameLength == 0 || nameLength > 64) return;
    std::vector<uint8_t> name(nameLength);
    if (!reader.ReadBits(name.data(), nameLength * 8)) return;
    uint32_t counter = 0, root = 0, dynamicRoot = 0;
    uint16_t netId = 0, noCache = 0;
    if (!reader.Read(counter) || !reader.Read(netId) || !ReadElementId(reader, root) || !ReadElementId(reader, dynamicRoot))
        return;
    if (!reader.Read(noCache)) return;
    SW_LOGI("Resource %.*s id=%u root=%u", static_cast<int>(name.size()), name.data(), netId, root);
    std::lock_guard<std::mutex> lock(g_lock);
    g_resourceRoot = root;
    Visual visual;
    visual.kind = 0;
    Remember(root, visual);
    Remember(dynamicRoot, visual);
    (void)counter; (void)noCache;
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
