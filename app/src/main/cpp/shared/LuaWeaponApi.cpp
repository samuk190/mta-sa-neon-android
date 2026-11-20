#include "LuaWeaponApi.h"
#include "LuaProjectileApi.h"

#include "LuaVMStub.h"
#include "LuaApiExtra.h"
#include "ServerWorld.h"
#include "ClientWeapon.h"
#include "../gtasa/GameEngine.h"

#include "lua.hpp"

#include <chrono>
#include <cmath>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace WorldRPG::Script {
namespace {

#include "LuaWeaponData.inc"

const char* kWeaponNames[] = {
    "Fist", "Brassknuckle", "Golfclub", "Nightstick", "Knife", "Bat", "Shovel", "Poolstick", "Katana", "Chainsaw",
    "Dildo", "Dildo", "Vibrator", "Vibrator", "Flower", "Cane", "Grenade", "Teargas", "Molotov", "", "", "",
    "Colt 45", "Silenced", "Deagle", "Shotgun", "Sawed-off", "Combat Shotgun", "Uzi", "MP5", "AK-47", "M4", "Tec-9",
    "Rifle", "Sniper", "Rocket Launcher", "Rocket Launcher HS", "Flamethrower", "Minigun", "Satchel", "Bomb",
    "Spraycan", "Fire Extinguisher", "Camera", "Nightvision", "Infrared", "Parachute",
};

struct Override { int id; int skill; std::string property; double a, b, c; bool triple; };
std::vector<Override> g_overrides;
bool g_diagWeapon = false;

int SkillOf(lua_State* L, int index) {
    if (lua_isnumber(L, index)) {
        const int skill = static_cast<int>(lua_tointeger(L, index));
        return skill >= 0 && skill <= 2 ? skill : -1;
    }
    const char* name = lua_tostring(L, index);
    if (!name) return -1;
    if (std::strcmp(name, "poor") == 0) return 0;
    if (std::strcmp(name, "std") == 0) return 1;
    if (std::strcmp(name, "pro") == 0) return 2;
    return -1;
}

int WeaponOf(lua_State* L, int index) {
    if (lua_isnumber(L, index)) {
        const int id = static_cast<int>(lua_tointeger(L, index));
        return id >= 0 && id <= 46 ? id : -1;
    }
    const char* name = lua_tostring(L, index);
    if (!name) return -1;
    for (int id = 0; id < static_cast<int>(sizeof(kWeaponNames) / sizeof(kWeaponNames[0])); ++id) {
        if (kWeaponNames[id][0] != '\0' && std::strcmp(kWeaponNames[id], name) == 0) return id;
    }
    return -1;
}

const WStat* FindStat(int id, int skill) {
    for (const WStat& stat : kWeaponStats)
        if (stat.id == id && stat.skill == skill) return &stat;
    return nullptr;
}

Override* FindOverride(int id, int skill, const char* property) {
    for (Override& item : g_overrides)
        if (item.id == id && item.skill == skill && item.property == property) return &item;
    return nullptr;
}

int FlagBit(const char* property) {
    if (std::strcmp(property, "flag_aim_no_auto") == 0) return 0x1;
    if (std::strcmp(property, "flag_aim_arm") == 0) return 0x2;
    if (std::strcmp(property, "flag_aim_1st_person") == 0) return 0x4;
    if (std::strcmp(property, "flag_aim_free") == 0) return 0x8;
    if (std::strcmp(property, "flag_move_and_aim") == 0) return 0x10;
    if (std::strcmp(property, "flag_move_and_shoot") == 0) return 0x20;
    if (std::strcmp(property, "flag_type_throw") == 0) return 0x100;
    if (std::strcmp(property, "flag_type_heavy") == 0) return 0x200;
    if (std::strcmp(property, "flag_type_constant") == 0) return 0x400;
    if (std::strcmp(property, "flag_type_dual") == 0) return 0x800;
    if (std::strcmp(property, "flag_anim_reload") == 0) return 0x1000;
    if (std::strcmp(property, "flag_anim_crouch") == 0) return 0x2000;
    if (std::strcmp(property, "flag_anim_reload_loop") == 0) return 0x4000;
    if (std::strcmp(property, "flag_anim_reload_long") == 0) return 0x8000;
    if (std::strcmp(property, "flag_shot_slows") == 0) return 0x10000;
    if (std::strcmp(property, "flag_shot_rand_speed") == 0) return 0x20000;
    if (std::strcmp(property, "flag_shot_anim_abrupt") == 0) return 0x40000;
    if (std::strcmp(property, "flag_shot_expands") == 0) return 0x80000;
    return 0;
}

bool PushProperty(lua_State* L, const WStat& stat, const char* property) {
    const int flag = FlagBit(property);
    if (flag != 0) {
        lua_pushboolean(L, (stat.flags & flag) != 0);
        return true;
    }
    if (std::strcmp(property, "fire_offset") == 0) {
        lua_pushnumber(L, stat.ox);
        lua_pushnumber(L, stat.oy);
        lua_pushnumber(L, stat.oz);
        return true;
    }
    auto pushInt = [&](int value) { lua_pushinteger(L, value); };
    auto pushFloat = [&](float value) { lua_pushnumber(L, value); };
    if (std::strcmp(property, "weapon_range") == 0) pushFloat(stat.weapon_range);
    else if (std::strcmp(property, "target_range") == 0) pushFloat(stat.target_range);
    else if (std::strcmp(property, "accuracy") == 0) pushFloat(stat.accuracy);
    else if (std::strcmp(property, "life_span") == 0) pushFloat(stat.life_span);
    else if (std::strcmp(property, "firing_speed") == 0) pushFloat(stat.firing_speed);
    else if (std::strcmp(property, "spread") == 0) pushFloat(stat.spread);
    else if (std::strcmp(property, "move_speed") == 0) pushFloat(stat.move_speed);
    else if (std::strcmp(property, "anim_loop_start") == 0) pushFloat(stat.anim_loop_start);
    else if (std::strcmp(property, "anim_loop_stop") == 0) pushFloat(stat.anim_loop_stop);
    else if (std::strcmp(property, "anim_loop_bullet_fire") == 0) pushFloat(stat.anim_loop_bullet_fire);
    else if (std::strcmp(property, "anim2_loop_start") == 0) pushFloat(stat.anim2_loop_start);
    else if (std::strcmp(property, "anim2_loop_stop") == 0) pushFloat(stat.anim2_loop_stop);
    else if (std::strcmp(property, "anim2_loop_bullet_fire") == 0) pushFloat(stat.anim2_loop_bullet_fire);
    else if (std::strcmp(property, "anim_breakout_time") == 0) pushFloat(stat.anim_breakout_time);
    else if (std::strcmp(property, "radius") == 0) pushFloat(stat.radius);
    else if (std::strcmp(property, "damage") == 0) pushInt(stat.damage);
    else if (std::strcmp(property, "maximum_clip_ammo") == 0) pushInt(stat.maximum_clip_ammo);
    else if (std::strcmp(property, "flags") == 0) pushInt(stat.flags);
    else if (std::strcmp(property, "anim_group") == 0) pushInt(stat.anim_group);
    else if (std::strcmp(property, "fire_type") == 0) pushInt(stat.fire_type);
    else if (std::strcmp(property, "model") == 0) pushInt(stat.model);
    else if (std::strcmp(property, "model2") == 0) pushInt(stat.model2);
    else if (std::strcmp(property, "weapon_slot") == 0) pushInt(stat.weapon_slot);
    else if (std::strcmp(property, "skill_level") == 0) pushInt(stat.skill_level);
    else if (std::strcmp(property, "required_skill_level") == 0) pushInt(stat.required_skill_level);
    else if (std::strcmp(property, "aim_offset") == 0) pushInt(stat.aim_offset);
    else if (std::strcmp(property, "default_combo") == 0) pushInt(stat.default_combo);
    else if (std::strcmp(property, "combos_available") == 0) pushInt(stat.combos_available);
    else return false;
    return true;
}

int ReadProperty(lua_State* L, bool original) {
    const int id = WeaponOf(L, 1);
    const int skill = SkillOf(L, 2);
    const char* property = lua_tostring(L, 3);
    if (id < 0 || skill < 0 || !property) {
        lua_pushboolean(L, 0);
        return 1;
    }
    if (!original) {
        // A flag read uses the flags word, including a setWeaponProperty override of that word.
        // Desktop IsFlagSet (CStaticFunctionDefinitions::SetWeaponPropertyFlag 2657-2676).
        const int bit = FlagBit(property);
        if (bit != 0) {
            const WStat* stat = FindStat(id, skill);
            if (!stat) {
                lua_pushboolean(L, 0);
                return 1;
            }
            int flags = stat->flags;
            if (Override* word = FindOverride(id, skill, "flags")) flags = static_cast<int>(word->a);
            lua_pushboolean(L, (flags & bit) != 0);
            return 1;
        }
        if (Override* item = FindOverride(id, skill, property)) {
            if (item->triple) {
                lua_pushnumber(L, item->a);
                lua_pushnumber(L, item->b);
                lua_pushnumber(L, item->c);
                return 3;
            }
            if (FlagBit(property) != 0) lua_pushboolean(L, item->a != 0);
            else if (std::strcmp(property, "damage") == 0 || std::strcmp(property, "flags") == 0 ||
                     std::strcmp(property, "maximum_clip_ammo") == 0 || std::strcmp(property, "model") == 0)
                lua_pushinteger(L, static_cast<lua_Integer>(item->a));
            else lua_pushnumber(L, item->a);
            return 1;
        }
    }
    const WStat* stat = FindStat(id, skill);
    if (!stat) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const int before = lua_gettop(L);
    if (!PushProperty(L, *stat, property)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    return lua_gettop(L) - before;
}

int l_getWeaponProperty(lua_State* L) { return ReadProperty(L, false); }
int l_getOriginalWeaponProperty(lua_State* L) { return ReadProperty(L, true); }

int l_setWeaponProperty(lua_State* L) {
    // CLuaWeaponDefs::SetWeaponProperty writes CWeaponStat. The phone stores the value.
    const int id = WeaponOf(L, 1);
    const int skill = SkillOf(L, 2);
    const char* property = lua_tostring(L, 3);
    const WStat* stat = FindStat(id, skill);
    if (id < 22 || id > 42 || skill < 0 || !property || !stat) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const bool fire = std::strcmp(property, "fire_offset") == 0;
    const int flag = FlagBit(property);
    if (flag != 0) {
        // SetWeaponPropertyFlag (CStaticFunctionDefinitions.cpp 2657-2676): set or clear the bit.
        // The same value already stored returns false.
        if (!lua_isboolean(L, 4)) {
            lua_pushboolean(L, 0);
            return 1;
        }
        int flags = stat->flags;
        if (Override* word = FindOverride(id, skill, "flags")) flags = static_cast<int>(word->a);
        const bool enable = lua_toboolean(L, 4) != 0;
        if (enable == ((flags & flag) != 0)) {
            lua_pushboolean(L, 0);
            return 1;
        }
        if (enable) flags |= flag;
        else flags &= ~flag;
        Override next{id, skill, "flags", static_cast<double>(flags), 0, 0, false};
        if (Override* word = FindOverride(id, skill, "flags")) *word = next;
        else g_overrides.push_back(next);
        if (!g_diagWeapon) {
            g_diagWeapon = true;
            LUA_LOGE("DIAG setWeaponProperty: CWeaponStat is not written; the value is stored");
        }
        lua_pushboolean(L, 1);
        return 1;
    }
    if (!fire) {
        if (!PushProperty(L, *stat, property)) {
            lua_pushboolean(L, 0);
            return 1;
        }
        lua_pop(L, 1);
    }
    Override next{id, skill, property, 0, 0, 0, false};
    if (std::strcmp(property, "fire_offset") == 0) {
        if (!lua_isnumber(L, 4) || !lua_isnumber(L, 5) || !lua_isnumber(L, 6)) {
            lua_pushboolean(L, 0);
            return 1;
        }
        next.triple = true;
        next.a = lua_tonumber(L, 4);
        next.b = lua_tonumber(L, 5);
        next.c = lua_tonumber(L, 6);
    } else if (lua_type(L, 4) == LUA_TBOOLEAN) {
        next.a = lua_toboolean(L, 4) ? 1 : 0;
    } else if (lua_isnumber(L, 4)) {
        next.a = lua_tonumber(L, 4);
    } else {
        lua_pushboolean(L, 0);
        return 1;
    }
    if (Override* item = FindOverride(id, skill, property)) *item = next;
    else g_overrides.push_back(next);
    if (!g_diagWeapon) {
        g_diagWeapon = true;
        LUA_LOGE("DIAG setWeaponProperty: CWeaponStat is not written; the value is stored");
    }
    lua_pushboolean(L, 1);
    return 1;
}

// Custom weapon elements: CClientWeapon (CClientWeapon.cpp) and the CLuaWeaponDefs
// element functions (CLuaWeaponDefs.cpp:150-695). The state machine is in ClientWeapon.h.
// Two kinds of weapon element exist, as on desktop:
//  - local: createWeapon, owned by the calling resource, ids 0x36000001+.
//  - server: ENTITY_ADD type WEAPON. The desktop builds a CClientWeapon of type AK-47
//    (CPacketHandler.cpp:3181) and applies the packet to it (3258-3345). The phone copies the
//    ENTITY_ADD snapshot into the same state the first time a script touches the element.
// No native CWeapon or CObject is created yet: the model, FireInstantHit, gun shells and
// the getElementPosition / setElementPosition branch for local weapons log DIAG once.

constexpr uint32_t kFirstLocalWeapon = 0x36000001u;
constexpr size_t kMaxLocalWeapons = 64;

struct LocalWeapon {
    Weapons::Weapon state;
    std::string owner;
    uint16_t netId = 0;
    uint32_t generation = 0;
};

std::vector<LocalWeapon> g_localWeapons;
// Server weapon state, with the owner and target the snapshot had when last compared.
// ServerWorld clears those snapshot fields when the element is destroyed; the desktop
// CClientEntityRefManager nulls m_pOwner / m_pTarget at the same moment.
struct ServerWeapon {
    Weapons::Weapon state;
    Entity::WeaponSpawn spawn;  // the snapshot the state was built from
    uint32_t spawnOwner = 0xFFFFFFFFu;
    uint32_t spawnTarget = 0xFFFFFFFFu;
};

// Weapon RPCs are not ported, so a snapshot only changes when ServerWorld clears its owner or
// target. Any other difference is a new ENTITY_ADD that reused the id: a new CClientWeapon.
bool SameSpawn(const Entity::WeaponSpawn& a, const Entity::WeaponSpawn& b) {
    if (a.targetType != b.targetType || a.subTarget != b.subTarget || a.targetX != b.targetX ||
        a.targetY != b.targetY || a.targetZ != b.targetZ || a.statsChanged != b.statsChanged ||
        a.damage != b.damage || a.accuracy != b.accuracy || a.targetRange != b.targetRange ||
        a.weaponRange != b.weaponRange || a.state != b.state || a.ammo != b.ammo || a.clipAmmo != b.clipAmmo)
        return false;
    for (int i = 0; i < 13; ++i)
        if (a.flags[i] != b.flags[i]) return false;
    return true;
}
std::unordered_map<uint32_t, ServerWeapon> g_serverWeapons;
uint32_t g_nextLocalWeapon = kFirstLocalWeapon;
bool g_diagWeaponObject = false;
bool g_diagWeaponHit = false;
bool g_diagWeaponBone = false;
bool g_diagWeaponPose = false;

uint32_t NowMs() {
    using namespace std::chrono;
    return static_cast<uint32_t>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

// CreateWeaponStatUnlisted(type, WEAPONSKILL_STD): the STD row of weapon.dat.
Weapons::Stat StdStat(int type) {
    Weapons::Stat out;
    const WStat* stat = FindStat(type, 1);
    if (!stat) return out;
    out.known = true;
    out.damage = stat->damage;
    out.accuracy = stat->accuracy;
    out.targetRange = stat->target_range;
    out.weaponRange = stat->weapon_range;
    out.maximumClipAmmo = stat->maximum_clip_ammo;
    out.animLoopStart = stat->anim_loop_start;
    out.animLoopStop = stat->anim_loop_stop;
    out.flags = stat->flags;
    out.fireOffset = Weapons::Vec3{stat->ox, stat->oy, stat->oz};
    return out;
}

LocalWeapon* FindLocalWeapon(uint32_t id) {
    for (LocalWeapon& weapon : g_localWeapons)
        if (weapon.state.id == id) return &weapon;
    return nullptr;
}

constexpr int kServerWeaponType = 30;  // WEAPONTYPE_AK47, CPacketHandler.cpp:3181

// The CPacketHandler WEAPON tail (3258-3345) applied to a new CClientWeapon.
void ApplySpawn(Weapons::Weapon& weapon, uint32_t id, const Entity::WeaponSpawn& spawn) {
    Weapons::Vec3 position;
    ServerWorld::ElementPose(id, position.x, position.y, position.z);
    Weapons::Initialize(weapon, id, kServerWeaponType, StdStat(kServerWeaponType), position, NowMs());
    if (spawn.targetType == 1 && spawn.target != 0xFFFFFFFFu) {
        const char* kind = ServerWorld::ElementKindName(spawn.target);
        // SetWeaponTarget checks GetType() == CCLIENTPED / CCLIENTVEHICLE (CClientWeapon.cpp:541-557).
        Weapons::SetWeaponTarget(weapon, spawn.target, spawn.subTarget, std::strcmp(kind, "ped") == 0,
                                 std::strcmp(kind, "vehicle") == 0);
    } else if (spawn.targetType == 0) {
        Weapons::SetWeaponTarget(weapon, Weapons::Vec3{spawn.targetX, spawn.targetY, spawn.targetZ});
    }
    if (spawn.statsChanged) {
        weapon.stat.damage = spawn.damage;
        weapon.stat.accuracy = spawn.accuracy;
        weapon.stat.targetRange = spawn.targetRange;
        weapon.stat.weaponRange = spawn.weaponRange;
    }
    Weapons::Config& config = weapon.config;
    config.disableWeaponModel = spawn.flags[0];
    config.instantReload = spawn.flags[1];
    config.shootIfTargetBlocked = spawn.flags[2];
    config.shootIfTargetOutOfRange = spawn.flags[3];
    config.flags.checkBuildings = spawn.flags[4];
    config.flags.checkCarTires = spawn.flags[5];
    config.flags.checkDummies = spawn.flags[6];
    config.flags.checkObjects = spawn.flags[7];
    config.flags.checkPeds = spawn.flags[8];
    config.flags.checkVehicles = spawn.flags[9];
    config.flags.ignoreSomeObjectsForCamera = spawn.flags[10];
    config.flags.seeThroughStuff = spawn.flags[11];
    config.flags.shootThroughStuff = spawn.flags[12];
    weapon.visible = !config.disableWeaponModel;
    weapon.ammoInClip = spawn.clipAmmo;
    weapon.ammoTotal = spawn.ammo;
    weapon.state = spawn.state < 3 ? static_cast<Weapons::State>(spawn.state) : Weapons::State::Ready;
    weapon.owner = spawn.owner == 0xFFFFFFFFu ? 0 : spawn.owner;
}

// ReadUserData(CClientWeapon*). isLocal is IsLocalEntity().
Weapons::Weapon* ResolveWeapon(uint32_t id, bool& isLocal) {
    isLocal = false;
    if (LocalWeapon* local = FindLocalWeapon(id)) {
        isLocal = true;
        return &local->state;
    }
    Entity::WeaponSpawn spawn;
    if (!id || !ServerWorld::CopyWeaponSpawn(id, spawn)) {
        g_serverWeapons.erase(id);  // the element is gone
        return nullptr;
    }
    auto found = g_serverWeapons.find(id);
    if (found != g_serverWeapons.end() && !SameSpawn(found->second.spawn, spawn)) {
        g_serverWeapons.erase(found);
        found = g_serverWeapons.end();
    }
    if (found == g_serverWeapons.end()) {
        found = g_serverWeapons.emplace(id, ServerWeapon{}).first;
        ApplySpawn(found->second.state, id, spawn);
        found->second.spawn = spawn;
        found->second.spawnOwner = spawn.owner;
        found->second.spawnTarget = spawn.target;
    }
    ServerWeapon& server = found->second;
    if (spawn.owner != server.spawnOwner) {
        if (spawn.owner == 0xFFFFFFFFu && server.state.owner == server.spawnOwner) server.state.owner = 0;
        server.spawnOwner = spawn.owner;
    }
    if (spawn.target != server.spawnTarget) {
        if (spawn.target == 0xFFFFFFFFu && server.state.target == server.spawnTarget) server.state.target = 0;
        server.spawnTarget = spawn.target;
    }
    return &server.state;
}

Weapons::Weapon* ReadWeapon(lua_State* L, int index, bool& isLocal) {
    if (!lua_istable(L, index)) return nullptr;
    return ResolveWeapon(ElementIdOf(L, index), isLocal);
}

int BadWeapon(lua_State* L, const char* function) {
    LUA_LOGE("%s: expected weapon at argument 1", function);
    lua_pushboolean(L, 0);
    return 1;
}

bool IsPedElement(uint32_t id) {
    // Fire and DoPulse use the bone for CCLIENTPED and CCLIENTPLAYER.
    if (id == ServerWorld::LocalPlayer() || Game::ScriptPedAlive(id)) return true;
    const char* kind = ServerWorld::ElementKindName(id);
    return std::strcmp(kind, "ped") == 0 || std::strcmp(kind, "player") == 0;
}

// The element still exists (the desktop entity pointer was not nulled).
bool ElementAlive(uint32_t id) {
    if (!id || id == 0xFFFFFFFFu) return false;
    if (id == ServerWorld::LocalPlayer() || ServerWorld::IsKnownPlayer(id) || Game::ScriptPedAlive(id)) return true;
    if (FindLocalWeapon(id)) return true;
    float x = 0, y = 0, z = 0;
    return std::strcmp(ServerWorld::ElementKindName(id), "element") != 0 || ServerWorld::ElementPose(id, x, y, z);
}

bool TargetPosition(uint32_t id, int bone, Weapons::Vec3& out) {
    if (!ElementAlive(id)) return false;
    if (bone >= 0 && !g_diagWeaponBone) {
        g_diagWeaponBone = true;
        LUA_LOGE("DIAG weapon target: CClientPed::GetBonePosition is not ported; the element position is used");
    }
    out = Weapons::Vec3{};
    if (LocalWeapon* local = FindLocalWeapon(id)) {
        out = local->state.position;
        return true;
    }
    if (id == ServerWorld::LocalPlayer()) {
        float heading = 0;
        Game::CopyPose(out.x, out.y, out.z, heading);
        return true;
    }
    if (Game::CopyRemotePose(id, out.x, out.y, out.z) || ServerWorld::ElementPose(id, out.x, out.y, out.z)) return true;
    if (!g_diagWeaponPose) {
        g_diagWeaponPose = true;
        LUA_LOGE("DIAG weapon target: element %u has no position on the phone; the zero vector is used", id);
    }
    return true;
}

void FireInstantHit(const Weapons::Weapon& weapon, const Weapons::Vec3& origin, const Weapons::Vec3& target, bool fixed) {
    if (g_diagWeaponHit) return;
    g_diagWeaponHit = true;
    LUA_LOGE("DIAG fireWeapon: CWeapon::FireInstantHit is not ported; weapon %u type %d from %.1f,%.1f,%.1f to %.1f,%.1f,%.1f%s",
             weapon.id, weapon.type, origin.x, origin.y, origin.z, target.x, target.y, target.z,
             fixed ? " (fixed target: heading only)" : "");
}

Weapons::Hooks MakeHooks() {
    Weapons::Hooks hooks;
    hooks.targetPosition = TargetPosition;
    hooks.isPed = IsPedElement;
    hooks.fireInstantHit = FireInstantHit;
    return hooks;
}

bool ReadFinite(lua_State* L, int index, float& out) {
    if (!lua_isnumber(L, index)) return false;
    const double value = lua_tonumber(L, index);
    out = static_cast<float>(value);
    return std::isfinite(value);
}

bool ReadVector(lua_State* L, int index, Weapons::Vec3& out) {
    return ReadFinite(L, index, out.x) && ReadFinite(L, index + 1, out.y) && ReadFinite(L, index + 2, out.z);
}

bool ReadInt(lua_State* L, int index, int& out) {
    if (!lua_isnumber(L, index)) return false;
    const double value = lua_tonumber(L, index);
    // ReadNumber<int> rejects values it cannot hold; a cast out of range is undefined.
    if (!std::isfinite(value) || value < -2147483648.0 || value > 2147483647.0) return false;
    out = static_cast<int>(value);
    return true;
}

int PushTrue(lua_State* L) {
    lua_pushboolean(L, 1);
    return 1;
}

int PushFalse(lua_State* L) {
    lua_pushboolean(L, 0);
    return 1;
}

// CLuaWeaponDefs::CreateWeapon (150-185) + CStaticFunctionDefinitions::CreateWeapon (7827-7833).
int CreateWeapon(lua_State* L) {
    const int type = WeaponOf(L, 1);
    Weapons::Vec3 position;
    if (type < 0 || !ReadVector(L, 2, position)) {
        LUA_LOGE("createWeapon: expected weapon type and position");
        return PushFalse(L);
    }
    if (ActiveVmOwner().empty() || g_localWeapons.size() >= kMaxLocalWeapons) return PushFalse(L);
    LocalWeapon created;
    created.owner = ActiveVmOwner();
    created.netId = ActiveVmOwnerNet();
    created.generation = ActiveVmOwnerGeneration();
    const Weapons::Stat stat = StdStat(type);
    if (!stat.known) LUA_LOGE("DIAG createWeapon: no weapon.dat STD row for type %d; stats are zero", type);
    Weapons::Initialize(created.state, g_nextLocalWeapon++, type, stat, position, NowMs());
    if (!g_diagWeaponObject) {
        g_diagWeaponObject = true;
        LUA_LOGE("DIAG createWeapon: CClientObject / CWeapon are not created; the weapon is state only");
    }
    g_localWeapons.push_back(std::move(created));
    PushElement(L, g_localWeapons.back().state.id);
    return 1;
}

// CLuaWeaponDefs::FireWeapon (187-206) + CStaticFunctionDefinitions::FireWeapon (7835-7843).
int FireWeapon(lua_State* L) {
    bool isLocal = false;
    Weapons::Weapon* weapon = ReadWeapon(L, 1, isLocal);
    if (!weapon) return BadWeapon(L, "fireWeapon");
    Weapons::Fire(*weapon, isLocal, ServerWorld::LocalPlayer(), false, NowMs(), MakeHooks());
    return PushTrue(L);
}

// CLuaWeaponDefs::SetWeaponState (265-286).
int SetWeaponState(lua_State* L) {
    bool isLocal = false;
    Weapons::Weapon* weapon = ReadWeapon(L, 1, isLocal);
    Weapons::State state;
    if (!weapon) return BadWeapon(L, "setWeaponState");
    if (lua_type(L, 2) != LUA_TSTRING || !Weapons::StateFromName(lua_tostring(L, 2), state)) {
        LUA_LOGE("setWeaponState: expected weapon-state at argument 2");
        return PushFalse(L);
    }
    weapon->state = state;
    return PushTrue(L);
}

// CLuaWeaponDefs::GetWeaponState (288-307).
int GetWeaponState(lua_State* L) {
    bool isLocal = false;
    Weapons::Weapon* weapon = ReadWeapon(L, 1, isLocal);
    if (!weapon) return BadWeapon(L, "getWeaponState");
    lua_pushstring(L, Weapons::StateName(weapon->state));
    return 1;
}

// CLuaWeaponDefs::SetWeaponTarget (309-361).
int SetWeaponTarget(lua_State* L) {
    bool isLocal = false;
    Weapons::Weapon* weapon = ReadWeapon(L, 1, isLocal);
    if (!weapon) return BadWeapon(L, "setWeaponTarget");
    if (lua_istable(L, 2)) {
        const uint32_t target = ElementIdOf(L, 2);
        int bone = 255;
        if (!lua_isnoneornil(L, 3) && !ReadInt(L, 3, bone)) return PushFalse(L);
        if (!ElementAlive(target)) {
            LUA_LOGE("setWeaponTarget: expected element at argument 2");
            return PushFalse(L);
        }
        const char* kind = ServerWorld::ElementKindName(target);
        const bool ped = Game::ScriptPedAlive(target) || std::strcmp(kind, "ped") == 0;
        Weapons::SetWeaponTarget(*weapon, target, bone, ped, std::strcmp(kind, "vehicle") == 0);
        return PushTrue(L);
    }
    if (lua_type(L, 2) == LUA_TNUMBER) {
        Weapons::Vec3 target;
        if (!ReadVector(L, 2, target)) {
            LUA_LOGE("setWeaponTarget: expected vector at argument 2");
            return PushFalse(L);
        }
        Weapons::SetWeaponTarget(*weapon, target);
        return PushTrue(L);
    }
    if (lua_type(L, 2) == LUA_TNIL) {
        Weapons::ResetWeaponTarget(*weapon);
        return PushTrue(L);
    }
    LUA_LOGE("setWeaponTarget: Expected element, number or nil at argument 2");
    return PushFalse(L);
}

// CLuaWeaponDefs::GetWeaponTarget (363-394).
int GetWeaponTarget(lua_State* L) {
    bool isLocal = false;
    Weapons::Weapon* weapon = ReadWeapon(L, 1, isLocal);
    if (!weapon) return BadWeapon(L, "getWeaponTarget");
    // A destroyed target reads as the nulled desktop pointer: nil, type stays entity.
    if (weapon->targetType == Weapons::TargetType::Entity && weapon->target && !ElementAlive(weapon->target))
        weapon->target = 0;
    switch (weapon->targetType) {
        case Weapons::TargetType::Vector:
            lua_pushnumber(L, weapon->vecTarget.x);
            lua_pushnumber(L, weapon->vecTarget.y);
            lua_pushnumber(L, weapon->vecTarget.z);
            return 3;
        case Weapons::TargetType::Entity:
            if (weapon->target) PushElement(L, weapon->target);
            else lua_pushnil(L);
            return 1;
        case Weapons::TargetType::Fixed:
            lua_pushnil(L);
            return 1;
    }
    return PushFalse(L);
}

// CLuaWeaponDefs::GetWeaponOwner (396-415): false without an owner.
int GetWeaponOwner(lua_State* L) {
    bool isLocal = false;
    Weapons::Weapon* weapon = ReadWeapon(L, 1, isLocal);
    if (!weapon) return BadWeapon(L, "getWeaponOwner");
    const uint32_t owner = weapon->owner;
    if (!owner || (owner != ServerWorld::LocalPlayer() && !ServerWorld::IsKnownPlayer(owner))) return PushFalse(L);
    PushElement(L, owner);
    return 1;
}

// CLuaWeaponDefs::SetWeaponFlags (451-496).
int SetWeaponFlags(lua_State* L) {
    bool isLocal = false;
    Weapons::Weapon* weapon = ReadWeapon(L, 1, isLocal);
    if (!weapon) return BadWeapon(L, "setWeaponFlags");
    const Weapons::Flag flag = lua_type(L, 2) == LUA_TSTRING ? Weapons::FlagFromName(lua_tostring(L, 2)) : Weapons::Flag::Invalid;
    if (flag == Weapons::Flag::Invalid) {
        LUA_LOGE("setWeaponFlags: expected weapon-flags at argument 2");
        return PushFalse(L);
    }
    if (flag != Weapons::Flag::Flags) {
        if (!lua_isboolean(L, 3)) return PushFalse(L);
        return Weapons::SetFlag(*weapon, flag, lua_toboolean(L, 3) != 0) ? PushTrue(L) : PushFalse(L);
    }
    // A fresh SLineOfSightFlags (CWorld.h:25-35), then eight booleans in this order.
    Weapons::LineOfSightFlags flags;  // all eight are read below; ignoreSomeObjectsForCamera stays false
    bool* order[] = {&flags.checkBuildings, &flags.checkCarTires, &flags.checkDummies, &flags.checkObjects,
                     &flags.checkPeds, &flags.checkVehicles, &flags.seeThroughStuff, &flags.shootThroughStuff};
    for (int i = 0; i < 8; ++i) {
        if (!lua_isboolean(L, 3 + i)) {
            LUA_LOGE("setWeaponFlags: expected boolean at argument %d", 3 + i);
            return PushFalse(L);
        }
        *order[i] = lua_toboolean(L, 3 + i) != 0;
    }
    weapon->config.flags = flags;
    return PushTrue(L);
}

// CLuaWeaponDefs::GetWeaponFlags (498-538).
int GetWeaponFlags(lua_State* L) {
    bool isLocal = false;
    Weapons::Weapon* weapon = ReadWeapon(L, 1, isLocal);
    if (!weapon) return BadWeapon(L, "getWeaponFlags");
    const Weapons::Flag flag = lua_type(L, 2) == LUA_TSTRING ? Weapons::FlagFromName(lua_tostring(L, 2)) : Weapons::Flag::Invalid;
    if (flag == Weapons::Flag::Invalid) {
        LUA_LOGE("getWeaponFlags: expected weapon-flags at argument 2");
        return PushFalse(L);
    }
    if (flag != Weapons::Flag::Flags) {
        bool value = false;
        if (!Weapons::GetFlag(*weapon, flag, value)) return PushFalse(L);
        lua_pushboolean(L, value);
        return 1;
    }
    const Weapons::LineOfSightFlags& flags = weapon->config.flags;
    for (bool value : {flags.checkBuildings, flags.checkCarTires, flags.checkDummies, flags.checkObjects,
                       flags.checkPeds, flags.checkVehicles, flags.seeThroughStuff, flags.shootThroughStuff})
        lua_pushboolean(L, value);
    return 8;
}

// CLuaWeaponDefs::SetWeaponFiringRate / GetWeaponFiringRate / ResetWeaponFiringRate (540-604).
int SetWeaponFiringRate(lua_State* L) {
    bool isLocal = false;
    Weapons::Weapon* weapon = ReadWeapon(L, 1, isLocal);
    int rate = 0;
    if (!weapon) return BadWeapon(L, "setWeaponFiringRate");
    if (!ReadInt(L, 2, rate)) return PushFalse(L);
    weapon->fireRate = rate;
    return PushTrue(L);
}

int GetWeaponFiringRate(lua_State* L) {
    bool isLocal = false;
    Weapons::Weapon* weapon = ReadWeapon(L, 1, isLocal);
    if (!weapon) return BadWeapon(L, "getWeaponFiringRate");
    lua_pushnumber(L, weapon->fireRate);
    return 1;
}

int ResetWeaponFiringRate(lua_State* L) {
    bool isLocal = false;
    Weapons::Weapon* weapon = ReadWeapon(L, 1, isLocal);
    if (!weapon) return BadWeapon(L, "resetWeaponFiringRate");
    weapon->fireRate = Weapons::WeaponFireTime(weapon->stat);
    return PushTrue(L);
}

// CLuaWeaponDefs::GetWeaponClipAmmo / GetWeaponAmmo / SetWeaponAmmo / SetWeaponClipAmmo (606-694).
int GetWeaponAmmo(lua_State* L) {
    bool isLocal = false;
    Weapons::Weapon* weapon = ReadWeapon(L, 1, isLocal);
    if (!weapon) return BadWeapon(L, "getWeaponAmmo");
    lua_pushnumber(L, weapon->ammoTotal);
    return 1;
}

int GetWeaponClipAmmo(lua_State* L) {
    bool isLocal = false;
    Weapons::Weapon* weapon = ReadWeapon(L, 1, isLocal);
    if (!weapon) return BadWeapon(L, "getWeaponClipAmmo");
    lua_pushnumber(L, weapon->ammoInClip);
    return 1;
}

int SetWeaponAmmo(lua_State* L) {
    bool isLocal = false;
    Weapons::Weapon* weapon = ReadWeapon(L, 1, isLocal);
    int ammo = 0;
    if (!weapon) return BadWeapon(L, "setWeaponAmmo");
    if (!ReadInt(L, 2, ammo)) return PushFalse(L);
    weapon->ammoTotal = ammo;
    return PushTrue(L);
}

int SetWeaponClipAmmo(lua_State* L) {
    bool isLocal = false;
    Weapons::Weapon* weapon = ReadWeapon(L, 1, isLocal);
    int ammo = 0;
    if (!weapon) return BadWeapon(L, "setWeaponClipAmmo");
    if (!ReadInt(L, 2, ammo)) return PushFalse(L);
    weapon->ammoInClip = ammo;
    return PushTrue(L);
}

// WeaponProperty names valid for ReadEnumString (CLuaFunctionParseHelpers.cpp:250-311).
bool IsWeaponPropertyName(const char* property) {
    if (!property) return false;
    if (std::strcmp(property, "fire_rotation") == 0) return true;
    if (FlagBit(property) != 0) return true;
    static const char* names[] = {"weapon_range", "target_range", "accuracy", "damage", "life_span", "firing_speed",
                                  "spread", "maximum_clip_ammo", "move_speed", "flags", "anim_group", "fire_type",
                                  "model", "model2", "weapon_slot", "fire_offset", "skill_level", "required_skill_level",
                                  "anim_loop_start", "anim_loop_stop", "anim_loop_bullet_fire", "anim2_loop_start",
                                  "anim2_loop_stop", "anim2_loop_bullet_fire", "anim_breakout_time", "radius",
                                  "aim_offset", "default_combo", "combos_available"};
    for (const char* name : names)
        if (std::strcmp(name, property) == 0) return true;
    return false;
}

// Custom weapon branch of CLuaWeaponDefs::GetWeaponProperty (696-735).
int GetElementWeaponProperty(lua_State* L, Weapons::Weapon& weapon) {
    if (!lua_isnumber(L, 2) && lua_type(L, 2) != LUA_TSTRING) return PushFalse(L);
    if (lua_isnumber(L, 2)) {
        const int skill = static_cast<int>(lua_tointeger(L, 2));
        if (skill < 0 || skill > 3) {
            LUA_LOGE("getWeaponProperty: Invalid weapon skill value.");
            return PushFalse(L);
        }
    }
    const char* property = lua_tostring(L, 3);
    if (!IsWeaponPropertyName(property)) return PushFalse(L);
    if (std::strcmp(property, "damage") == 0) {
        lua_pushinteger(L, weapon.stat.damage);
        return 1;
    }
    if (std::strcmp(property, "fire_rotation") == 0) {
        constexpr float toDegrees = 180.0f / 3.14159265358979323846f;
        lua_pushnumber(L, weapon.fireRotationNoTarget.x * toDegrees);
        lua_pushnumber(L, weapon.fireRotationNoTarget.y * toDegrees);
        lua_pushnumber(L, weapon.fireRotationNoTarget.z * toDegrees);
        return 3;
    }
    if (std::strcmp(property, "accuracy") == 0) lua_pushnumber(L, weapon.stat.accuracy);
    else if (std::strcmp(property, "target_range") == 0) lua_pushnumber(L, weapon.stat.targetRange);
    else if (std::strcmp(property, "weapon_range") == 0) lua_pushnumber(L, weapon.stat.weaponRange);
    else return PushFalse(L);
    return 1;
}

// CLuaWeaponDefs::SetWeaponProperty (208-263) + CStaticFunctionDefinitions::SetWeaponProperty (7894-7944).
int SetElementWeaponProperty(lua_State* L, Weapons::Weapon& weapon) {
    const char* property = lua_type(L, 2) == LUA_TSTRING ? lua_tostring(L, 2) : nullptr;
    if (!IsWeaponPropertyName(property)) {
        LUA_LOGE("setWeaponProperty: expected weapon-property at argument 2");
        return PushFalse(L);
    }
    if (std::strcmp(property, "damage") == 0) {
        int damage = 0;
        if (!ReadInt(L, 3, damage)) return PushFalse(L);
        weapon.stat.damage = static_cast<short>(damage);
        return PushTrue(L);
    }
    if (std::strcmp(property, "fire_rotation") == 0) {
        Weapons::Vec3 rotation;
        if (!ReadVector(L, 3, rotation)) return PushFalse(L);
        constexpr float toRadians = 3.14159265358979323846f / 180.0f;
        weapon.fireRotationNoTarget = Weapons::Vec3{rotation.x * toRadians, rotation.y * toRadians, rotation.z * toRadians};
        return PushTrue(L);
    }
    float value = 0;
    if (!ReadFinite(L, 3, value)) return PushFalse(L);
    if (std::strcmp(property, "accuracy") == 0) weapon.stat.accuracy = value;
    else if (std::strcmp(property, "target_range") == 0) weapon.stat.targetRange = value;
    else if (std::strcmp(property, "weapon_range") == 0) weapon.stat.weaponRange = value;
    else return PushFalse(L);
    return PushTrue(L);
}

int GetWeaponPropertyAny(lua_State* L) {
    bool isLocal = false;
    if (lua_istable(L, 1)) {
        Weapons::Weapon* weapon = ReadWeapon(L, 1, isLocal);
        return weapon ? GetElementWeaponProperty(L, *weapon) : PushFalse(L);
    }
    return l_getWeaponProperty(L);
}

int SetWeaponPropertyAny(lua_State* L) {
    bool isLocal = false;
    if (lua_istable(L, 1)) {
        Weapons::Weapon* weapon = ReadWeapon(L, 1, isLocal);
        return weapon ? SetElementWeaponProperty(L, *weapon) : BadWeapon(L, "setWeaponProperty");
    }
    return l_setWeaponProperty(L);
}

}  // namespace

bool LocalWeaponAlive(uint32_t id) { return FindLocalWeapon(id) != nullptr; }

// CClientWeapon::~CClientWeapon: removed from the manager; no native object to destroy yet.
bool ReleaseLocalWeapon(uint32_t id) {
    for (auto it = g_localWeapons.begin(); it != g_localWeapons.end(); ++it) {
        if (it->state.id != id) continue;
        g_localWeapons.erase(it);
        return true;
    }
    return false;
}

// The resource dynamic element is destroyed with the resource, and its weapons with it.
void WeaponsDropOwner(const std::string& owner, uint16_t netId, uint32_t generation) {
    for (auto it = g_localWeapons.begin(); it != g_localWeapons.end();) {
        if (it->owner == owner && it->netId == netId && it->generation == generation) it = g_localWeapons.erase(it);
        else ++it;
    }
}

// CClientWeaponManager::DoPulse calls CClientWeapon::DoPulse for every weapon. Server weapons
// pulse once a script has touched them (their state is built on first use, see ResolveWeapon).
void WeaponsPulse() {
    if (g_localWeapons.empty() && g_serverWeapons.empty()) return;
    const uint32_t now = NowMs();
    const uint32_t localPlayer = ServerWorld::LocalPlayer();
    const Weapons::Hooks hooks = MakeHooks();
    for (LocalWeapon& weapon : g_localWeapons) Weapons::DoPulse(weapon.state, true, localPlayer, now, hooks);
    for (auto it = g_serverWeapons.begin(); it != g_serverWeapons.end();) {
        Entity::WeaponSpawn spawn;
        if (!ServerWorld::CopyWeaponSpawn(it->first, spawn)) {
            it = g_serverWeapons.erase(it);
            continue;
        }
        bool isLocal = false;
        if (Weapons::Weapon* weapon = ResolveWeapon(it->first, isLocal)) Weapons::DoPulse(*weapon, false, localPlayer, now, hooks);
        ++it;
    }
}

void WeaponsReset() {
    g_localWeapons.clear();
    g_serverWeapons.clear();
}

void InstallWeaponLua(lua_State* L) {
    InstallProjectileLua(L);
    const struct { const char* name; lua_CFunction fn; } binds[] = {
        {"createWeapon", CreateWeapon},
        {"fireWeapon", FireWeapon},
        {"setWeaponState", SetWeaponState},
        {"getWeaponState", GetWeaponState},
        {"setWeaponTarget", SetWeaponTarget},
        {"getWeaponTarget", GetWeaponTarget},
        {"getWeaponOwner", GetWeaponOwner},
        {"setWeaponFlags", SetWeaponFlags},
        {"getWeaponFlags", GetWeaponFlags},
        {"setWeaponFiringRate", SetWeaponFiringRate},
        {"getWeaponFiringRate", GetWeaponFiringRate},
        {"resetWeaponFiringRate", ResetWeaponFiringRate},
        {"getWeaponAmmo", GetWeaponAmmo},
        {"getWeaponClipAmmo", GetWeaponClipAmmo},
        {"setWeaponAmmo", SetWeaponAmmo},
        {"setWeaponClipAmmo", SetWeaponClipAmmo},
        {"getWeaponProperty", GetWeaponPropertyAny},
        {"getOriginalWeaponProperty", l_getOriginalWeaponProperty},
        {"setWeaponProperty", SetWeaponPropertyAny},
    };
    for (const auto& bind : binds) {
        lua_pushcfunction(L, bind.fn);
        lua_setglobal(L, bind.name);
    }
}

}  // namespace WorldRPG::Script
