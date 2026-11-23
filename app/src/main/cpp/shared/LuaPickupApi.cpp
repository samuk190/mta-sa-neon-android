// Pickups. Desktop CLuaPickupDefs.cpp and CStaticFunctionDefinitions.cpp 4570-4697.
// CPickup is not created. Health and armor models are 1240 and 1242.

#include "LuaPickupApi.h"

#include "LuaApiExtra.h"
#include "LuaVMStub.h"
#include "ModelInfoRead.h"

#include "lua.hpp"

#include <cmath>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace WorldRPG::Script {
namespace {

enum Type { Health = 0, Armor = 1, Weapon = 2, Custom = 3 };

struct Pickup {
    uint32_t id = 0;
    float x = 0, y = 0, z = 0;
    int type = Health;
    float amount = 0;
    int weapon = 0;
    int ammo = 0;
    int model = 1240;
};

std::unordered_map<uint32_t, Pickup> g_pickups;
bool g_diag = false;

const unsigned short kWeaponModels[47] = {
    0,   331, 333, 334, 335, 336, 337, 338, 339, 341, 321, 322, 323, 324, 325, 326, 342, 343, 344, 0, 0, 0, 346, 347,
    348, 349, 350, 351, 352, 353, 355, 356, 372, 357, 358, 359, 360, 361, 362, 363, 364, 365, 366, 367, 368, 369, 371};

int Fail(lua_State* L) {
    lua_pushboolean(L, 0);
    return 1;
}

void Warn(const std::string& message) { LUA_LOGI("WARNING: %s", message.c_str()); }

std::string Got(lua_State* L, int index) {
    const int type = lua_type(L, index);
    if (type == LUA_TNUMBER) {
        const char* text = lua_tostring(L, index);
        return std::string("number '") + (text ? text : "0") + "'";
    }
    if (type == LUA_TBOOLEAN) return "boolean";
    if (type == LUA_TSTRING) {
        const char* text = lua_tostring(L, index);
        return std::string("string '") + (text ? text : "") + "'";
    }
    if (type == LUA_TNIL) return "nil";
    if (type == LUA_TNONE) return "none";
    if (type == LUA_TTABLE) {
        lua_getfield(L, index, "element");
        const bool element = lua_toboolean(L, -1) != 0;
        lua_pop(L, 1);
        if (!element) return "table";
        const uint32_t id = ElementIdOf(L, index);
        if (!KnownClientElement(id)) return "destroyed element";
        if (g_pickups.count(id)) return "pickup";
        const int top = lua_gettop(L);
        lua_getglobal(L, "getElementType");
        lua_pushvalue(L, index);
        std::string name = "element";
        if (lua_isfunction(L, -2) && lua_pcall(L, 1, 1, 0) == 0) {
            if (const char* text = lua_tostring(L, -1)) name = text;
        }
        lua_settop(L, top);
        return name;
    }
    return "userdata";
}

int Bad(lua_State* L, const char* function, const std::string& detail) {
    Warn(std::string("Bad argument @ '") + function + "' [" + detail + "]");
    return Fail(L);
}

bool Finite(lua_State* L, int index, double& out) {
    if (!lua_isnumber(L, index)) return false;
    const lua_Number number = lua_tonumber(L, index);
    if (!std::isfinite(number)) return false;
    out = number;
    return true;
}

bool Byte(lua_State* L, const char* function, int index, int& out) {
    double value = 0;
    if (!Finite(L, index, value)) return Bad(L, function, "Expected number at argument " + std::to_string(index) + ", got " + Got(L, index)), false;
    if (value < 0) return Bad(L, function, "Expected positive value, got negative"), false;
    if (value > 255) return Bad(L, function, "Expected number within the destination type range at argument " + std::to_string(index) + ", got out-of-range number"), false;
    out = static_cast<int>(value);
    return true;
}

bool ValidWeapon(int id) { return id > 0 && id != 19 && id != 20 && id != 21 && id <= 46; }

bool StockObject(unsigned model) {
    if (model >= Models::kStockModelCount) return false;
    if (model <= 312) return false;
    if (model >= 313 && model <= 320) return false;
    if (model >= 384 && model <= 397) return false;
    if (model >= 400 && model <= 611) return false;
    Models::StockModelView view;
    const Models::LiveModelStatus status = Models::ReadLiveStockModel(static_cast<uint16_t>(model), view);
    if (status == Models::LiveModelStatus::Missing) return false;
    (void)view;
    return true;
}

Pickup* Find(uint32_t id) {
    auto found = g_pickups.find(id);
    if (found == g_pickups.end()) return nullptr;
    if (!KnownClientElement(id)) {
        g_pickups.erase(found);
        return nullptr;
    }
    return &g_pickups.find(id)->second;
}

bool Apply(Pickup& pickup, int type, double third, double ammo) {
    if (type == Health || type == Armor) {
        if (third < 0 || third > 100) return false;
        pickup.type = type;
        pickup.amount = static_cast<float>(third);
        pickup.model = type == Armor ? 1242 : 1240;
        return true;
    }
    if (type == Weapon) {
        const int weapon = static_cast<unsigned char>(third);
        if (!ValidWeapon(weapon)) return false;
        int rounds = static_cast<unsigned short>(ammo);
        if (ammo > 9999) rounds = 9999;
        pickup.type = Weapon;
        pickup.weapon = weapon;
        pickup.ammo = rounds;
        pickup.model = kWeaponModels[weapon];
        return true;
    }
    if (type == Custom) {
        const unsigned model = static_cast<unsigned short>(third);
        if (!StockObject(model)) return false;
        pickup.type = Custom;
        pickup.model = static_cast<int>(model);
        return true;
    }
    return false;
}

void Each(uint32_t id, const std::function<void(uint32_t)>& visit) {
    if (ClientCallPropagationEnabled(id)) {
        std::vector<uint32_t> children;
        CopyClientChildren(id, children);
        for (uint32_t child : children) Each(child, visit);
    }
    visit(id);
}

int CreatePickup(lua_State* L) {
    // CLuaPickupDefs::CreatePickup (48-92). Respawn is checked and not stored, matching the desktop body.
    double x = 0, y = 0, z = 0, third = 0, respawn = 30000, ammo = 50;
    if (!Finite(L, 1, x) || !Finite(L, 2, y) || !Finite(L, 3, z))
        return Bad(L, "createPickup", "Expected vector3 at argument 1, got " + Got(L, 1));
    int type = 0;
    if (!Byte(L, "createPickup", 4, type)) return 1;
    if (!Finite(L, 5, third)) return Bad(L, "createPickup", "Expected number at argument 5, got " + Got(L, 5));
    if (!lua_isnoneornil(L, 6)) {
        if (!Finite(L, 6, respawn) || respawn < 0)
            return Bad(L, "createPickup", "Expected number at argument 6, got " + Got(L, 6));
    }
    if (!lua_isnoneornil(L, 7)) {
        if (!Finite(L, 7, ammo)) return Bad(L, "createPickup", "Expected number at argument 7, got " + Got(L, 7));
    }
    (void)respawn;
    Pickup pickup;
    pickup.x = static_cast<float>(x);
    pickup.y = static_cast<float>(y);
    pickup.z = static_cast<float>(z);
    if (!Apply(pickup, type, third, ammo)) return Fail(L);
    const uint32_t id = CreateResourceElement("pickup");
    if (!id) return Fail(L);
    pickup.id = id;
    g_pickups[id] = pickup;
    if (!g_diag) {
        g_diag = true;
        LUA_LOGE("DIAG createPickup: CPickup is not created");
    }
    PushElement(L, id);
    return 1;
}

int GetNumber(lua_State* L, const char* function, double (*read)(const Pickup&)) {
    Pickup* pickup = Find(ElementIdOf(L, 1));
    if (!pickup) return Bad(L, function, "Expected pickup at argument 1, got " + Got(L, 1));
    lua_pushnumber(L, read(*pickup));
    return 1;
}

int GetPickupType(lua_State* L) {
    return GetNumber(L, "getPickupType", [](const Pickup& pickup) { return static_cast<double>(pickup.type); });
}
int GetPickupWeapon(lua_State* L) {
    return GetNumber(L, "getPickupWeapon", [](const Pickup& pickup) { return static_cast<double>(pickup.weapon); });
}
int GetPickupAmount(lua_State* L) {
    return GetNumber(L, "getPickupAmount", [](const Pickup& pickup) { return static_cast<double>(pickup.amount); });
}
int GetPickupAmmo(lua_State* L) {
    return GetNumber(L, "getPickupAmmo", [](const Pickup& pickup) { return static_cast<double>(pickup.ammo); });
}

int SetPickupType(lua_State* L) {
    // CStaticFunctionDefinitions::SetPickupType accepts any element and updates pickup children first.
    const uint32_t id = ElementIdOf(L, 1);
    if (!lua_istable(L, 1) || !KnownClientElement(id))
        return Bad(L, "setPickupType", "Expected element at argument 1, got " + Got(L, 1));
    int type = 0;
    if (!Byte(L, "setPickupType", 2, type)) return 1;
    double third = 0, ammo = 50;
    if (!Finite(L, 3, third)) return Bad(L, "setPickupType", "Expected number at argument 3, got " + Got(L, 3));
    if (!lua_isnoneornil(L, 4) && !Finite(L, 4, ammo))
        return Bad(L, "setPickupType", "Expected number at argument 4, got " + Got(L, 4));
    bool ok = false;
    Each(id, [&](uint32_t child) {
        Pickup* pickup = Find(child);
        if (!pickup) return;
        if (Apply(*pickup, type, third, ammo) && child == id) ok = true;
    });
    lua_pushboolean(L, ok ? 1 : 0);
    return 1;
}

int GetElementPositionPickup(lua_State* L) {
    Pickup* pickup = Find(ElementIdOf(L, 1));
    if (!pickup) {
        if (!lua_isfunction(L, lua_upvalueindex(1))) return Fail(L);
        const int count = lua_gettop(L);
        lua_pushvalue(L, lua_upvalueindex(1));
        lua_insert(L, 1);
        lua_call(L, count, LUA_MULTRET);
        return lua_gettop(L);
    }
    lua_pushnumber(L, pickup->x);
    lua_pushnumber(L, pickup->y);
    lua_pushnumber(L, pickup->z);
    return 3;
}

int SetElementPositionPickup(lua_State* L) {
    Pickup* pickup = Find(ElementIdOf(L, 1));
    if (!pickup) {
        if (!lua_isfunction(L, lua_upvalueindex(1))) return Fail(L);
        const int count = lua_gettop(L);
        lua_pushvalue(L, lua_upvalueindex(1));
        lua_insert(L, 1);
        lua_call(L, count, LUA_MULTRET);
        return lua_gettop(L);
    }
    double x = 0, y = 0, z = 0;
    if (!Finite(L, 2, x) || !Finite(L, 3, y) || !Finite(L, 4, z)) return Fail(L);
    pickup->x = static_cast<float>(x);
    pickup->y = static_cast<float>(y);
    pickup->z = static_cast<float>(z);
    lua_pushboolean(L, 1);
    return 1;
}

void Register(lua_State* L) {
    g_pickups.clear();
    const struct { const char* name; lua_CFunction fn; } binds[] = {
        {"createPickup", CreatePickup},
        {"getPickupType", GetPickupType},
        {"getPickupWeapon", GetPickupWeapon},
        {"getPickupAmount", GetPickupAmount},
        {"getPickupAmmo", GetPickupAmmo},
        {"setPickupType", SetPickupType},
    };
    for (const auto& bind : binds) {
        lua_pushcfunction(L, bind.fn);
        lua_setglobal(L, bind.name);
    }
    lua_getglobal(L, "getElementPosition");
    lua_pushcclosure(L, GetElementPositionPickup, 1);
    lua_setglobal(L, "getElementPosition");
    lua_getglobal(L, "setElementPosition");
    lua_pushcclosure(L, SetElementPositionPickup, 1);
    lua_setglobal(L, "setElementPosition");
}

}  // namespace

void PickupRelease(uint32_t id) { g_pickups.erase(id); }
void InstallPickupLua(lua_State* L) { Register(L); }

}  // namespace WorldRPG::Script
