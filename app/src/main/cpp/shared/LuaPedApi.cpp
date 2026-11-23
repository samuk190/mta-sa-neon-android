// Ped/player queries. Native ped snapshots are collected on the game thread.

#include "LuaApiExtra.h"
#include "LuaVMStub.h"
#include "ServerWorld.h"
#include "PedQueries.h"
#include "../gtasa/GameEngine.h"
#include "../gtasa/RemoteVehicle.h"
#include "../net/raknet/RakNetWrapper.h"

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

#include <algorithm>
#include <cmath>
#include <cfloat>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace WorldRPG::Script {
namespace {

// Nametag fields from CClientPlayer. Showing starts true. Color is white until a
// script sets an override. A team color is not applied: OnPlayerList reads a team
// id and drops it, so no player has a team. Cleared when InstallPedLua runs, which
// is each new Lua VM (a new session).
struct Nametag {
    bool showing = true;
    bool colorOverride = false;
    unsigned char red = 255;
    unsigned char green = 255;
    unsigned char blue = 255;
    // Empty until setPlayerNametagText. The displayed text is the nick until then
    // (CClientPlayer::SetNametagText(m_strNick) in the constructor).
    bool textOverride = false;
    std::string text;
};

std::unordered_map<uint32_t, Nametag> g_nametags;
std::unordered_map<uint32_t, std::unordered_map<unsigned, float>> g_stats;

bool IsPlayer(lua_State* L, int index, uint32_t& id) {
    if (!lua_istable(L, index)) return false;
    id = ElementIdOf(L, index);
    if (id == 0 || id == 0xFFFFFFFFu) return false;
    const char* kind = ServerWorld::ElementKindName(id);
    return kind && std::strcmp(kind, "player") == 0;
}

Nametag& TagFor(uint32_t id) { return g_nametags[id]; }

int Fail(lua_State* L, const char* name, const char* message) {
    LUA_LOGE("%s: %s", name, message);
    lua_pushboolean(L, 0);
    return 1;
}

int PushFalse(lua_State* L) {
    lua_pushboolean(L, 0);
    return 1;
}

bool ReadPed(lua_State* L, uint32_t& id) {
    if (!lua_istable(L, 1)) return false;
    id = ElementIdOf(L, 1);
    if (!id || id == UINT32_MAX) return false;
    const char* kind = ServerWorld::ElementKindName(id);
    return (kind && (!std::strcmp(kind, "ped") || !std::strcmp(kind, "player"))) ||
        Game::ScriptPedAlive(id) || ServerWorld::IsKnownPlayer(id);
}
// CScriptArgReader::ReadNumber<unsigned char>: nil/omitted use 255, numeric
// strings are accepted and negative/NaN values fail. Narrow without float-cast UB.
bool ReadWeaponSlot(lua_State* L, unsigned& slot) {
    slot = 255;
    if (lua_isnoneornil(L, 2)) return true;
    if (!lua_isnumber(L, 2)) return false;
    const double value = lua_tonumber(L, 2);
    if (!std::isfinite(value) || value < -FLT_EPSILON) return false;
    slot = static_cast<unsigned>(std::fmod(std::trunc(std::max(0.0, value)), 256.0));
    return true;
}

// CLuaPedDefs.cpp:685-709; CClientPed::GetWeaponType:4174-4189.
int GetPedWeapon(lua_State* L) {
    uint32_t id = 0;
    unsigned slot = 255;
    PedQueries::Snapshot state;
    if (!ReadPed(L, id) || !ReadWeaponSlot(L, slot) || !Game::CopyPedQuery(id, state)) return PushFalse(L);
    if (slot == 255) slot = state.currentSlot;
    lua_pushnumber(L, slot < PedQueries::Slots ? state.weapons[slot].type : 0);
    return 1;
}
// CLuaPedDefs.cpp:711-731.
int GetPedWeaponSlot(lua_State* L) {
    uint32_t id = 0;
    PedQueries::Snapshot state;
    if (!ReadPed(L, id) || !Game::CopyPedQuery(id, state)) return PushFalse(L);
    lua_pushnumber(L, state.currentSlot);
    return 1;
}
int PedAmmo(lua_State* L, bool clip) {
    uint32_t id = 0;
    unsigned slot = 255;
    PedQueries::Snapshot state;
    if (!ReadPed(L, id) || !ReadWeaponSlot(L, slot) || !Game::CopyPedQuery(id, state)) return PushFalse(L);
    if (slot == 255) slot = state.currentSlot;
    uint16_t ammo = 0;
    if (!PedQueries::Ammo(state, slot, clip, ammo)) return PushFalse(L);
    lua_pushnumber(L, ammo);
    return 1;
}
// CLuaPedDefs.cpp:733-761 and 763-807 respectively.
int GetPedAmmoInClip(lua_State* L) { return PedAmmo(L, true); }
int GetPedTotalAmmo(lua_State* L) { return PedAmmo(L, false); }
// CLuaPedDefs.cpp:2168-2198. Invalid arguments return nil here, unlike the weapon queries.
int IsPedDead(lua_State* L) {
    uint32_t id = 0;
    PedQueries::Snapshot state;
    if (!ReadPed(L, id) || !Game::CopyPedQuery(id, state)) lua_pushnil(L);
    else lua_pushboolean(L, PedQueries::IsDead(state));
    return 1;
}

void DiagOnce(bool& once, const char* text) {
    if (once) return;
    once = true;
    LUA_LOGI("%s", text);
}

// CLuaPedDefs.cpp:1240-1259. ENTITY_ADD stores SetHasJetPack. The local jetpack task is not read.
// isPedWearingJetpack is the same function (CLuaPedDefs.cpp:432).
int JetPackQuery(lua_State* L, const char* name) {
    uint32_t id = 0;
    if (!ReadPed(L, id)) {
        LUA_LOGE("%s: expected a ped", name);
        lua_pushboolean(L, 0);
        return 1;
    }
    Entity::PedSpawn spawn;
    const bool has = ServerWorld::CopyPedSpawn(id, spawn) && spawn.hasJetpack;
    if (id == ServerWorld::LocalPlayer()) {
        static bool once = false;
        DiagOnce(once, "DIAG doesPedHaveJetPack: the local CPed jetpack task is not read");
    }
    lua_pushboolean(L, has ? 1 : 0);
    return 1;
}
int DoesPedHaveJetPack(lua_State* L) { return JetPackQuery(L, "doesPedHaveJetPack"); }
int IsPedWearingJetpack(lua_State* L) { return JetPackQuery(L, "isPedWearingJetpack"); }

// CLuaPedDefs.cpp:1975-1995 and CStaticFunctionDefinitions.cpp:1960-1963.
int IsPedOnFire(lua_State* L) {
    uint32_t id = 0;
    if (!ReadPed(L, id)) {
        LUA_LOGE("isPedOnFire: expected a ped");
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushboolean(L, PedQueries::ReadPresentation(id).onFire ? 1 : 0);
    return 1;
}

// CLuaPedDefs.cpp:1998-2019 and CStaticFunctionDefinitions.cpp:2719-2730.
// Only a client-created ped or the local player. CPed::SetOnFire is not called.
int SetPedOnFire(lua_State* L) {
    uint32_t id = 0;
    if (!ReadPed(L, id) || lua_type(L, 2) != LUA_TBOOLEAN) {
        LUA_LOGE("setPedOnFire: expected a ped and a boolean");
        lua_pushboolean(L, 0);
        return 1;
    }
    if (id != ServerWorld::LocalPlayer() && !Game::ScriptPedAlive(id)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const bool onFire = lua_toboolean(L, 2) != 0;
    PedQueries::OnFire(id, onFire);
    Game::SetPedOnFire(id, onFire);
    static bool once = false;
    DiagOnce(once, "DIAG setPedOnFire: CPed::SetOnFire is not hooked; the flag is stored");
    lua_pushboolean(L, 1);
    return 1;
}

// CLuaPedDefs.cpp:1171-1189. A bad ped is nil. The keysync choking bit is SKeysyncFlags bit 1.
int IsPedChoking(lua_State* L) {
    uint32_t id = 0;
    if (!ReadPed(L, id)) {
        LUA_LOGE("isPedChoking: expected a ped");
        lua_pushnil(L);
        return 1;
    }
    lua_pushboolean(L, PedQueries::ReadPresentation(id).choking ? 1 : 0);
    return 1;
}

// CLuaPedDefs.cpp:1192-1211. Same nil-on-error rule as isPedChoking.
int IsPedDucked(lua_State* L) {
    uint32_t id = 0;
    if (!ReadPed(L, id)) {
        LUA_LOGE("isPedDucked: expected a ped");
        lua_pushnil(L);
        return 1;
    }
    lua_pushboolean(L, PedQueries::ReadPresentation(id).ducked ? 1 : 0);
    return 1;
}

// CLuaPedDefs.cpp:1286-1306. GetContactEntity is not stored, so a valid ped has no contact.
int GetPedContactElement(lua_State* L) {
    uint32_t id = 0;
    if (!ReadPed(L, id)) {
        LUA_LOGE("getPedContactElement: expected a ped");
        lua_pushboolean(L, 0);
        return 1;
    }
    static bool once = false;
    DiagOnce(once, "DIAG getPedContactElement: CClientPed::GetContactEntity is not stored");
    lua_pushboolean(L, 0);
    return 1;
}

// CLuaPedDefs.cpp:809-831. GetShotData returns false without a weapon or a wrist bone.
int GetPedWeaponMuzzlePosition(lua_State* L) {
    uint32_t id = 0;
    if (!ReadPed(L, id)) {
        LUA_LOGE("getPedWeaponMuzzlePosition: expected a ped");
        lua_pushboolean(L, 0);
        return 1;
    }
    PedQueries::Snapshot state;
    const bool known = Game::CopyPedQuery(id, state);
    const unsigned slot = known && state.currentSlot < PedQueries::Slots ? state.currentSlot : 0;
    if (!known || state.weapons[slot].type == 0) {
        lua_pushboolean(L, 0);
        return 1;
    }
    static bool once = false;
    DiagOnce(once, "DIAG getPedWeaponMuzzlePosition: GetTransformedBonePosition is not hooked");
    lua_pushboolean(L, 0);
    return 1;
}

// CLuaPedDefs.cpp:998-1021. A valid ped returns three numbers. The keysync aim origin is used
// when a shot was synced. Otherwise the vector stays zero: the camera and muzzle paths are not run.
int GetPedTargetStart(lua_State* L) {
    uint32_t id = 0;
    if (!ReadPed(L, id)) {
        LUA_LOGE("getPedTargetStart: expected a ped");
        lua_pushboolean(L, 0);
        return 1;
    }
    const PedQueries::Presentation aim = PedQueries::ReadPresentation(id);
    if (!aim.aim) {
        static bool once = false;
        DiagOnce(once, "DIAG getPedTargetStart: camera and muzzle shot origin are not computed");
    }
    lua_pushnumber(L, aim.aim ? aim.aimX : 0);
    lua_pushnumber(L, aim.aim ? aim.aimY : 0);
    lua_pushnumber(L, aim.aim ? aim.aimZ : 0);
    return 3;
}

// CLuaPedDefs.cpp:1045-1068. End = origin + front * weapon range. The range is not available.
int GetPedTargetEnd(lua_State* L) {
    uint32_t id = 0;
    if (!ReadPed(L, id)) {
        LUA_LOGE("getPedTargetEnd: expected a ped");
        lua_pushboolean(L, 0);
        return 1;
    }
    static bool once = false;
    DiagOnce(once, "DIAG getPedTargetEnd: weapon range is not applied to the aim vector");
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    return 3;
}

// CLuaPedDefs.cpp:1092-1116 and CStaticFunctionDefinitions.cpp:1760-1776.
int GetPedTargetCollision(lua_State* L) {
    uint32_t id = 0;
    if (!ReadPed(L, id)) {
        LUA_LOGE("getPedTargetCollision: expected a ped");
        lua_pushboolean(L, 0);
        return 1;
    }
    static bool once = false;
    DiagOnce(once, "DIAG getPedTargetCollision: CWorld::ProcessLineOfSight is not hooked");
    lua_pushboolean(L, 0);
    return 1;
}

// CLuaFunctionDefs.Util.cpp:13-54. Only stock models are available: the port's
// engineRequestModel is still a stub, so both custom-model walks are empty.
// The stock validity set follows CPlayerManager::IsValidPlayerModel
// (Server/mods/deathmatch/logic/CPlayerManager.cpp:338-352). Do not infer model
// availability from a streamed ped: the desktop also lists unloaded models.
int GetValidPedModels(lua_State* L) {
    // ReadBool(includeCustom, true) does not gate the desktop body on HasErrors:
    // even a non-boolean argument returns the stock list, with custom=false.
    lua_newtable(L);
    int index = 0;
    for (int model = 0; model <= 312; ++model) {
        if (model == 74 || model == 149 || model == 208) continue;
        lua_pushnumber(L, model);
        lua_rawseti(L, -2, ++index);
    }
    return 1;
}

// Nick stored at join or from PLAYER_LIST. "Player" is the previous fixed text,
// used only until a nick is stored for this id.
std::string PlayerNameOf(uint32_t id) {
    std::string nick;
    if (ServerWorld::CopyPlayerNick(id, nick)) return nick;
    return "Player";
}

bool ReadStringArg(lua_State* L, int index, std::string& out) {
    const int type = lua_type(L, index);
    if (type != LUA_TSTRING && type != LUA_TNUMBER) return false;
    size_t length = 0;
    const char* text = lua_tolstring(L, index, &length);
    if (!text) return false;
    out.assign(text, length);
    return true;
}

// UTF-16 code units of the bytes up to the first 0, matching MbUTF8ToUTF16 on a C string.
size_t Utf16Units(const std::string& text) {
    size_t units = 0;
    const unsigned char* s = reinterpret_cast<const unsigned char*>(text.data());
    const unsigned char* end = s + text.size();
    while (s < end && *s != 0) {
        const unsigned char lead = *s;
        size_t need = 1;
        uint32_t code = lead;
        if (lead < 0x80) {
            need = 1;
            code = lead;
        } else if ((lead & 0xE0) == 0xC0) {
            need = 2;
            code = lead & 0x1Fu;
        } else if ((lead & 0xF0) == 0xE0) {
            need = 3;
            code = lead & 0x0Fu;
        } else if ((lead & 0xF8) == 0xF0) {
            need = 4;
            code = lead & 0x07u;
        } else {
            ++units;
            ++s;
            continue;
        }
        if (s + need > end) {
            ++units;
            break;
        }
        bool ok = true;
        for (size_t i = 1; i < need; ++i) {
            if ((s[i] & 0xC0) != 0x80) {
                ok = false;
                break;
            }
            code = (code << 6) | (s[i] & 0x3Fu);
        }
        if (!ok) {
            ++units;
            ++s;
            continue;
        }
        units += code >= 0x10000u ? 2 : 1;
        s += need;
    }
    return units;
}

// CClientGame::IsNametagValid (CClientGame.cpp 1673-1694).
// MIN_PLAYER_NAMETAG_LENGTH 1, MAX_PLAYER_NAMETAG_LENGTH 64 (CClientCommon.h).
// The character walk indexes the C string by the UTF-16 length, as the desktop does.
bool NametagValid(const std::string& text) {
    const size_t units = Utf16Units(text);
    if (units < 1 || units > 64) return false;
    for (size_t i = 0; i < units; ++i) {
        const unsigned char byte = i < text.size() ? static_cast<unsigned char>(text[i]) : 0;
        if (byte < 32) return false;
    }
    return true;
}

// CScriptArgReader::ReadNumber into a signed int, then the unsigned char cast in
// SetPlayerNametagColor. Numeric strings are accepted. NaN and anything else fail.
bool ReadChannel(lua_State* L, int index, unsigned char& channel, const char*& error) {
    const int type = lua_type(L, index);
    if (type != LUA_TNUMBER && type != LUA_TSTRING) {
        error = "expected a number";
        return false;
    }
    if (!lua_isnumber(L, index)) {
        error = "expected a number, got a non-convertible string";
        return false;
    }
    const double number = lua_tonumber(L, index);
    if (std::isnan(number)) {
        error = "expected a number, got NaN";
        return false;
    }
    channel = static_cast<unsigned char>(static_cast<int>(number));
    return true;
}

// Desktop CLuaPlayerDefs::GetPlayerName (CLuaPlayerDefs.cpp 325-349).
// CClientPlayer::GetNick. The nick is the join name, then the PLAYER_LIST name.
int GetPlayerName(lua_State* L) {
    uint32_t id = 0;
    if (!IsPlayer(L, 1, id)) return Fail(L, "getPlayerName", "expected a player");
    const std::string name = PlayerNameOf(id);
    lua_pushlstring(L, name.data(), name.size());
    return 1;
}

bool IsPlayerId(uint32_t id) {
    if (id == 0 || id == 0xFFFFFFFFu) return false;
    const char* kind = ServerWorld::ElementKindName(id);
    return kind && std::strcmp(kind, "player") == 0;
}

// Desktop CLuaPlayerDefs::GetPlayerFromName (CLuaPlayerDefs.cpp 351-376).
// getPlayerFromNick is the same function. Match is case-insensitive
// (CClientPlayerManager::Get default, CClientPlayerManager.cpp 117-136).
int GetPlayerFromName(lua_State* L) {
    std::string name;
    if (!ReadStringArg(L, 1, name)) return Fail(L, "getPlayerFromName", "expected a string");
    uint32_t id = 0;
    if (!ServerWorld::FindPlayerByNick(name, id) || !IsPlayerId(id)) return PushFalse(L);
    PushElement(L, id);
    return 1;
}

// Desktop CLuaPlayerDefs::GetPlayerNametagText (CLuaPlayerDefs.cpp 378-402).
int GetPlayerNametagText(lua_State* L) {
    uint32_t id = 0;
    if (!IsPlayer(L, 1, id)) return Fail(L, "getPlayerNametagText", "expected a player");
    const auto found = g_nametags.find(id);
    const std::string text = (found != g_nametags.end() && found->second.textOverride)
                                 ? found->second.text
                                 : PlayerNameOf(id);
    lua_pushlstring(L, text.data(), text.size());
    return 1;
}

// Desktop CLuaPlayerDefs::GetPlayerPing (CLuaPlayerDefs.cpp 452-473).
// CClientPlayer::GetPing (CClientPlayer.h 48): the local player uses g_pNet->GetPing,
// which is RakNetWrapper::GetPing. A remote player's m_uiPing starts at 0 and the
// latency write in CNetAPI is not stored, so a remote player stays at 0.
int GetPlayerPing(lua_State* L) {
    uint32_t id = 0;
    if (!IsPlayer(L, 1, id)) return Fail(L, "getPlayerPing", "expected a player");
    int ping = 0;
    if (id == ServerWorld::LocalPlayer()) {
        if (WorldRPG::Net::RakNetWrapper* net = WorldRPG::Net::RakNetWrapper::Live()) ping = net->GetPing();
    }
    lua_pushnumber(L, ping);
    return 1;
}

// Desktop CLuaPlayerDefs::GetPlayerTeam (CLuaPlayerDefs.cpp 475-499).
// OnPlayerList reads a team id and drops it, so no player has a team element.
int GetPlayerTeam(lua_State* L) {
    uint32_t id = 0;
    if (!IsPlayer(L, 1, id)) return Fail(L, "getPlayerTeam", "expected a player");
    (void)id;
    lua_pushboolean(L, 0);
    return 1;
}

// Desktop CLuaPlayerDefs::GetPlayerNametagColor (CLuaPlayerDefs.cpp:404-429).
// CClientPlayer::GetNametagColor (CClientPlayer.cpp:142-166): the override, otherwise
// the team color, otherwise white. No team is stored, so the middle branch is white.
int GetPlayerNametagColor(lua_State* L) {
    uint32_t id = 0;
    if (!IsPlayer(L, 1, id)) return Fail(L, "getPlayerNametagColor", "expected a player");
    const auto found = g_nametags.find(id);
    const bool overridden = found != g_nametags.end() && found->second.colorOverride;
    const unsigned char red = overridden ? found->second.red : 255;
    const unsigned char green = overridden ? found->second.green : 255;
    const unsigned char blue = overridden ? found->second.blue : 255;
    lua_pushnumber(L, red);
    lua_pushnumber(L, green);
    lua_pushnumber(L, blue);
    return 3;
}

// Desktop CLuaPlayerDefs::GetPlayerSerial (CLuaPlayerDefs.cpp:770-777).
// g_pCore->GetNetwork()->GetSerial is RakNetWrapper::GetSerial.
int GetPlayerSerial(lua_State* L) {
    char serial[64] = {};
    if (WorldRPG::Net::RakNetWrapper* net = WorldRPG::Net::RakNetWrapper::Live())
        net->GetSerial(serial, sizeof(serial));
    lua_pushstring(L, serial);
    return 1;
}

// Desktop CLuaPlayerDefs::GetPlayerUserName (CLuaPlayerDefs.cpp:764-768).
// The desktop function returns false. There is no username to read.
int GetPlayerUserName(lua_State* L) {
    static bool once = false;
    DiagOnce(once, "DIAG getPlayerUserName: the desktop client returns false");
    lua_pushboolean(L, 0);
    return 1;
}

// Desktop CLuaPlayerDefs::IsPlayerNametagShowing (CLuaPlayerDefs.cpp:431-450).
// CClientPlayer starts with the nametag showing (CClientPlayer.cpp:76 and 240).
int IsPlayerNametagShowing(lua_State* L) {
    uint32_t id = 0;
    if (!IsPlayer(L, 1, id)) return Fail(L, "isPlayerNametagShowing", "expected a player");
    const auto found = g_nametags.find(id);
    const bool showing = found == g_nametags.end() || found->second.showing;
    lua_pushboolean(L, showing ? 1 : 0);
    return 1;
}

// Desktop CLuaPlayerDefs::SetPlayerNametagText (CLuaPlayerDefs.cpp 653-676) and
// CStaticFunctionDefinitions::SetPlayerNametagText (CStaticFunctionDefinitions.cpp 2170-2182).
// RUN_CHILDREN does not run: players are not given children on the phone, so
// CountChildren() is 0 and the walk is skipped on desktop too.
int SetPlayerNametagText(lua_State* L) {
    uint32_t id = 0;
    std::string text;
    if (!IsPlayer(L, 1, id)) return Fail(L, "setPlayerNametagText", "expected a player");
    if (!ReadStringArg(L, 2, text)) return Fail(L, "setPlayerNametagText", "expected a string");
    if (!NametagValid(text)) return PushFalse(L);
    Nametag& tag = TagFor(id);
    tag.textOverride = true;
    tag.text = std::move(text);
    lua_pushboolean(L, 1);
    return 1;
}

// Desktop CLuaPlayerDefs::SetPlayerNametagColor (CLuaPlayerDefs.cpp:678-733) and
// CStaticFunctionDefinitions::SetPlayerNametagColor (CStaticFunctionDefinitions.cpp:2185-2199).
// Two calls: (player, r, g, b) sets an override; (player, false) clears it.
// (player, true) is rejected. RUN_CHILDREN does not run: no element has children,
// so CountChildren() is 0 and the walk is skipped on desktop too.
int SetPlayerNametagColor(lua_State* L) {
    uint32_t id = 0;
    if (!IsPlayer(L, 1, id)) return Fail(L, "setPlayerNametagColor", "expected a player");
    if (lua_type(L, 2) == LUA_TBOOLEAN) {
        if (lua_toboolean(L, 2)) return PushFalse(L);
        Nametag& tag = TagFor(id);
        tag.colorOverride = false;
        tag.red = 255;
        tag.green = 255;
        tag.blue = 255;
        lua_pushboolean(L, 1);
        return 1;
    }
    const char* error = nullptr;
    unsigned char red = 0, green = 0, blue = 0;
    if (!ReadChannel(L, 2, red, error) || !ReadChannel(L, 3, green, error) || !ReadChannel(L, 4, blue, error))
        return Fail(L, "setPlayerNametagColor", error ? error : "expected a number");
    Nametag& tag = TagFor(id);
    tag.colorOverride = true;
    tag.red = red;
    tag.green = green;
    tag.blue = blue;
    lua_pushboolean(L, 1);
    return 1;
}

// Desktop CLuaPlayerDefs::SetPlayerNametagShowing (CLuaPlayerDefs.cpp:735-760) and
// CStaticFunctionDefinitions::SetPlayerNametagShowing (CStaticFunctionDefinitions.cpp:2202-2214).
// The showing flag is a boolean, not a number. Same empty child walk as the color setter.
int SetPlayerNametagShowing(lua_State* L) {
    uint32_t id = 0;
    if (!IsPlayer(L, 1, id)) return Fail(L, "setPlayerNametagShowing", "expected a player");
    if (lua_type(L, 2) != LUA_TBOOLEAN) return Fail(L, "setPlayerNametagShowing", "expected a boolean");
    TagFor(id).showing = lua_toboolean(L, 2) != 0;
    lua_pushboolean(L, 1);
    return 1;
}

// CScriptArgReader::ReadUserData<CClientPed>: a ped or a player.
bool IsPed(lua_State* L, int index, uint32_t& id) {
    if (!lua_istable(L, index)) return false;
    id = ElementIdOf(L, index);
    if (id == 0 || id == 0xFFFFFFFFu) return false;
    const char* kind = ServerWorld::ElementKindName(id);
    return kind && (std::strcmp(kind, "player") == 0 || std::strcmp(kind, "ped") == 0);
}

// Desktop CClientPed::GetOccupiedVehicle / GetOccupiedVehicleSeat. The local player is in the
// car the game reports (WatchLocalVehicle); its seat is 0 because the phone only requests the
// driver seat (NeonJoinProtocol.cpp VEHICLE_REQUEST_IN). Other peds are seated by
// VEHICLE_INOUT (PacketHandler.cpp ApplyRemoteVehicleInOut). A ped that ENTITY_ADD placed in a
// vehicle is not seated yet (the warp logs DIAG), so it reports no vehicle.
bool PedOccupiedVehicle(uint32_t pedId, uint32_t& vehicleId, uint8_t& seat) {
    if (pedId == ServerWorld::LocalPlayer()) {
        vehicleId = WorldRPG::Game::LocalVehicleId();
        seat = 0;
    } else if (!WorldRPG::RemoteVehicle::FindOccupant(pedId, vehicleId, seat)) {
        return false;
    }
    if (vehicleId == 0 || vehicleId == 0xFFFFFFFFu) return false;
    const char* kind = ServerWorld::ElementKindName(vehicleId);
    return kind && std::strcmp(kind, "vehicle") == 0;
}

// CLuaPedDefs.cpp:834-857.
int GetPedOccupiedVehicle(lua_State* L) {
    uint32_t id = 0, vehicle = 0;
    uint8_t seat = 0;
    if (!IsPed(L, 1, id)) return Fail(L, "getPedOccupiedVehicle", "expected a ped");
    if (!PedOccupiedVehicle(id, vehicle, seat)) return PushFalse(L);
    PushElement(L, vehicle);
    return 1;
}

// CLuaPedDefs.cpp:859-880 and CStaticFunctionDefinitions.cpp:1972-1980.
int GetPedOccupiedVehicleSeat(lua_State* L) {
    uint32_t id = 0, vehicle = 0;
    uint8_t seat = 0;
    if (!IsPed(L, 1, id)) return Fail(L, "getPedOccupiedVehicleSeat", "expected a ped");
    if (!PedOccupiedVehicle(id, vehicle, seat)) return PushFalse(L);
    lua_pushinteger(L, seat);
    return 1;
}

// CLuaPedDefs.cpp:1214-1238 and CStaticFunctionDefinitions.cpp:1966-1970. A bad argument is nil.
int IsPedInVehicle(lua_State* L) {
    uint32_t id = 0, vehicle = 0;
    uint8_t seat = 0;
    if (!IsPed(L, 1, id)) {
        LUA_LOGE("isPedInVehicle: expected a ped");
        lua_pushnil(L);
        return 1;
    }
    lua_pushboolean(L, PedOccupiedVehicle(id, vehicle, seat) ? 1 : 0);
    return 1;
}

bool SameControl(const char* a, const char* b) {
    if (!a || !b) return false;
    while (*a && *b) {
        const char ca = (*a >= 'A' && *a <= 'Z') ? static_cast<char>(*a - 'A' + 'a') : *a;
        const char cb = (*b >= 'A' && *b <= 'Z') ? static_cast<char>(*b - 'A' + 'a') : *b;
        if (ca != cb) return false;
        ++a;
        ++b;
    }
    return *a == *b;
}

const char* kControls[] = {
    "fire", "next_weapon", "previous_weapon", "forwards", "backwards", "left", "right", "zoom_in", "zoom_out",
    "enter_exit", "change_camera", "jump", "sprint", "look_behind", "crouch", "action", "walk", "vehicle_fire",
    "vehicle_secondary_fire", "vehicle_left", "vehicle_right", "steer_forward", "steer_back", "accelerate",
    "brake_reverse", "radio_next", "radio_previous", "radio_user_track_skip", "horn", "sub_mission", "handbrake",
    "vehicle_look_left", "vehicle_look_right", "vehicle_look_behind", "vehicle_mouse_look", "special_control_left",
    "special_control_right", "special_control_down", "special_control_up", "aim_weapon", "conversation_yes",
    "conversation_no", "group_control_forwards", "group_control_back",
};

bool KnownControl(const char* name) {
    for (const char* control : kControls)
        if (SameControl(control, name)) return true;
    return false;
}

std::mutex g_controlMu;
std::unordered_map<uint32_t, std::unordered_map<std::string, bool>> g_controls;
std::unordered_map<uint32_t, bool> g_knockable;

bool SampledControl(const char* name, bool& state) {
    Game::OnFootKeys keys;
    Game::CopyOnFoot(0.f, 0.f, 0.f, keys);
    if (SameControl(name, "fire")) state = keys.fire;
    else if (SameControl(name, "jump")) state = keys.jump;
    else if (SameControl(name, "sprint")) state = keys.sprint;
    else if (SameControl(name, "crouch")) state = keys.duck;
    else if (SameControl(name, "forwards")) state = keys.stickY == -128;
    else if (SameControl(name, "backwards")) state = keys.stickY == 128;
    else if (SameControl(name, "left")) state = keys.stickX == -128;
    else if (SameControl(name, "right")) state = keys.stickX == 128;
    else return false;
    return true;
}

// CLuaPedDefs.cpp:1641-1669. A control name alone means the local player.
bool ReadControlQuery(lua_State* L, uint32_t& id, std::string& control) {
    if (lua_type(L, 1) == LUA_TSTRING) {
        control = lua_tostring(L, 1);
        id = ServerWorld::LocalPlayer();
        return true;
    }
    if (!IsPed(L, 1, id) || lua_type(L, 2) != LUA_TSTRING) return false;
    control = lua_tostring(L, 2);
    return true;
}

int GetPedControlState(lua_State* L) {
    uint32_t id = 0;
    std::string control;
    if (!ReadControlQuery(L, id, control) || !KnownControl(control.c_str())) {
        LUA_LOGE("getPedControlState: expected a ped and a control name");
        lua_pushboolean(L, 0);
        return 1;
    }
    {
        std::lock_guard lock(g_controlMu);
        auto ped = g_controls.find(id);
        if (ped != g_controls.end()) {
            auto found = ped->second.find(control);
            if (found != ped->second.end()) {
                lua_pushboolean(L, found->second ? 1 : 0);
                return 1;
            }
        }
    }
    bool state = false;
    if (id == ServerWorld::LocalPlayer() && SampledControl(control.c_str(), state)) {
        lua_pushboolean(L, state ? 1 : 0);
        return 1;
    }
    lua_pushboolean(L, 0);
    return 1;
}

// CLuaPedDefs.cpp:2251-2283. The GTA pad is not written; the value is what getPedControlState reads back.
int SetPedControlState(lua_State* L) {
    uint32_t id = 0;
    std::string control;
    int stateIndex = 3;
    if (lua_type(L, 1) == LUA_TSTRING) {
        control = lua_tostring(L, 1);
        id = ServerWorld::LocalPlayer();
        stateIndex = 2;
    } else if (!IsPed(L, 1, id) || lua_type(L, 2) != LUA_TSTRING) {
        LUA_LOGE("setPedControlState: expected a ped and a control name");
        lua_pushboolean(L, 0);
        return 1;
    } else {
        control = lua_tostring(L, 2);
    }
    if (!KnownControl(control.c_str()) || lua_type(L, stateIndex) != LUA_TBOOLEAN) {
        LUA_LOGE("setPedControlState: expected a known control and a boolean");
        lua_pushboolean(L, 0);
        return 1;
    }
    {
        std::lock_guard lock(g_controlMu);
        g_controls[id][control] = lua_toboolean(L, stateIndex) != 0;
    }
    static bool once = false;
    DiagOnce(once, "DIAG setPedControlState: CPad is not written; the control is stored for getPedControlState");
    lua_pushboolean(L, 1);
    return 1;
}

// CLuaPedDefs.cpp:1262-1283 and CClientPed.cpp:7912-7925.
int IsPedOnGround(lua_State* L) {
    uint32_t id = 0;
    if (!IsPed(L, 1, id)) {
        LUA_LOGE("isPedOnGround: expected a ped");
        lua_pushboolean(L, 0);
        return 1;
    }
    if (!lua_isnoneornil(L, 2) && lua_type(L, 2) != LUA_TBOOLEAN) {
        LUA_LOGE("isPedOnGround: expected a boolean");
        lua_pushboolean(L, 0);
        return 1;
    }
    const bool checkVehicles = lua_toboolean(L, 2) != 0;
    float x = 0, y = 0, z = 0, heading = 0;
    const bool posed = id == ServerWorld::LocalPlayer() ? (Game::CopyPose(x, y, z, heading), true)
                                                         : ServerWorld::ElementPose(id, x, y, z);
    float ground = 0;
    if (!posed || !Game::FindGroundZFor3DPosition(x, y, z, ground)) {
        static bool once = false;
        DiagOnce(once, "DIAG isPedOnGround: CWorld::FindGroundZFor3DPosition did not return a ground height");
        lua_pushboolean(L, 0);
        return 1;
    }
    if (z < ground - 1e-4f) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const float gap = z - ground;
    const bool onGround = gap < 1.f || std::fabs(gap - 1.f) < 1e-4f;
    if (!onGround && checkVehicles) {
        static bool once = false;
        DiagOnce(once, "DIAG isPedOnGround: CPed::IsStandingOnEntity is not hooked");
    }
    lua_pushboolean(L, onGround ? 1 : 0);
    return 1;
}

// CLuaPedDefs.cpp:1144-1167. Stats are not read from the ped. An id outside 0..342 is false.
int GetPedStat(lua_State* L) {
    uint32_t id = 0;
    const bool ped = IsPed(L, 1, id) || (lua_istable(L, 1) && Game::ScriptPedAlive(id = ElementIdOf(L, 1)));
    if (!ped || !lua_isnumber(L, 2)) {
        LUA_LOGE("getPedStat: expected a ped and a stat id");
        lua_pushboolean(L, 0);
        return 1;
    }
    const double stat = lua_tonumber(L, 2);
    if (!std::isfinite(stat) || stat < 0 || stat >= 343) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const auto pedStats = g_stats.find(id);
    if (pedStats != g_stats.end()) {
        const auto value = pedStats->second.find(static_cast<unsigned>(stat));
        if (value != pedStats->second.end()) {
            lua_pushnumber(L, value->second);
            return 1;
        }
    }
    static bool once = false;
    DiagOnce(once, "DIAG getPedStat: CPed::GetStat is not read; an unset stat returns 0");
    lua_pushnumber(L, 0);
    return 1;
}

// CLuaPedDefs.cpp:882-918. No CTaskManager, so a well-formed query returns false.
int GetPedTask(lua_State* L) {
    uint32_t id = 0;
    if (!IsPed(L, 1, id) || lua_type(L, 2) != LUA_TSTRING || !lua_isnumber(L, 3)) {
        LUA_LOGE("getPedTask: expected a ped, a priority and a task index");
        lua_pushboolean(L, 0);
        return 1;
    }
    const char* priority = lua_tostring(L, 2);
    if (!SameControl(priority, "primary") && !SameControl(priority, "secondary")) {
        lua_pushboolean(L, 0);
        return 1;
    }
    static bool once = false;
    DiagOnce(once, "DIAG getPedTask: CTaskManager is not hooked");
    lua_pushboolean(L, 0);
    return 1;
}

int GetPedSimplestTask(lua_State* L) {
    uint32_t id = 0;
    if (!IsPed(L, 1, id)) {
        LUA_LOGE("getPedSimplestTask: expected a ped");
        lua_pushboolean(L, 0);
        return 1;
    }
    static bool once = false;
    DiagOnce(once, "DIAG getPedSimplestTask: CTaskManager is not hooked");
    lua_pushboolean(L, 0);
    return 1;
}

int IsPedDoingTask(lua_State* L) {
    uint32_t id = 0;
    if (!IsPed(L, 1, id) || lua_type(L, 2) != LUA_TSTRING) {
        LUA_LOGE("isPedDoingTask: expected a ped and a task name");
        lua_pushboolean(L, 0);
        return 1;
    }
    static bool once = false;
    DiagOnce(once, "DIAG isPedDoingTask: CTaskManager is not hooked");
    lua_pushboolean(L, 0);
    return 1;
}

// CLuaPedDefs.cpp:973-995. ProcessLineOfSight is not hooked, so there is no target element.
int GetPedTarget(lua_State* L) {
    uint32_t id = 0;
    if (!IsPed(L, 1, id)) {
        LUA_LOGE("getPedTarget: expected a ped");
        lua_pushboolean(L, 0);
        return 1;
    }
    static bool once = false;
    DiagOnce(once, "DIAG getPedTarget: CWorld::ProcessLineOfSight is not hooked");
    lua_pushboolean(L, 0);
    return 1;
}

// CLuaPedDefs.cpp:1552-1579. Slot 0..12 is stored. CPed::SetCurrentWeapon is not called.
int SetPedWeaponSlot(lua_State* L) {
    uint32_t id = 0;
    if (!IsPed(L, 1, id) || !lua_isnumber(L, 2)) {
        LUA_LOGE("setPedWeaponSlot: expected a ped and a slot");
        lua_pushboolean(L, 0);
        return 1;
    }
    const double slot = lua_tonumber(L, 2);
    if (!std::isfinite(slot) || slot < 0 || slot >= PedQueries::Slots) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const char* kind = ServerWorld::ElementKindName(id);
    const bool player = kind && std::strcmp(kind, "player") == 0;
    if (!PedQueries::SetCurrentSlot(id, static_cast<unsigned>(slot), player)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    static bool once = false;
    DiagOnce(once, "DIAG setPedWeaponSlot: CPed::SetCurrentWeapon is not called; the client slot is stored");
    lua_pushboolean(L, 1);
    return 1;
}

// CLuaPedDefs.cpp:2548-2573. Script peds store the heading. Other peds have no rotation hook.
int SetPedRotation(lua_State* L) {
    uint32_t id = 0;
    if (!IsPed(L, 1, id) || !lua_isnumber(L, 2)) {
        LUA_LOGE("setPedRotation: expected a ped and a rotation");
        lua_pushboolean(L, 0);
        return 1;
    }
    if (!lua_isnoneornil(L, 3) && lua_type(L, 3) != LUA_TBOOLEAN) {
        LUA_LOGE("setPedRotation: expected a boolean");
        lua_pushboolean(L, 0);
        return 1;
    }
    if (!Game::ScriptPedAlive(id)) {
        static bool once = false;
        DiagOnce(once, "DIAG setPedRotation: CClientPed::SetCurrentRotation is not hooked");
        lua_pushboolean(L, 0);
        return 1;
    }
    Game::NoteScriptRotation(id, static_cast<float>(lua_tonumber(L, 2)));
    lua_pushboolean(L, 1);
    return 1;
}

// CLuaPedDefs.cpp:1329-1346. Unset means the ped can be knocked off, which is the GTA default.
int CanPedBeKnockedOffBike(lua_State* L) {
    uint32_t id = 0;
    if (!IsPed(L, 1, id)) {
        LUA_LOGE("canPedBeKnockedOffBike: expected a ped");
        lua_pushboolean(L, 0);
        return 1;
    }
    std::lock_guard lock(g_controlMu);
    auto found = g_knockable.find(id);
    lua_pushboolean(L, (found == g_knockable.end() || found->second) ? 1 : 0);
    return 1;
}

// CLuaPedDefs.cpp:2576-2588. Remote players are rejected. The bike flag is stored, not applied to CPed.
int SetPedCanBeKnockedOffBike(lua_State* L) {
    uint32_t id = 0;
    if (!IsPed(L, 1, id) || lua_type(L, 2) != LUA_TBOOLEAN) {
        LUA_LOGE("setPedCanBeKnockedOffBike: expected a ped and a boolean");
        lua_pushboolean(L, 0);
        return 1;
    }
    const char* kind = ServerWorld::ElementKindName(id);
    if (kind && std::strcmp(kind, "player") == 0 && id != ServerWorld::LocalPlayer()) {
        lua_pushboolean(L, 0);
        return 1;
    }
    {
        std::lock_guard lock(g_controlMu);
        g_knockable[id] = lua_toboolean(L, 2) != 0;
    }
    static bool once = false;
    DiagOnce(once, "DIAG setPedCanBeKnockedOffBike: CPed::SetCanBeKnockedOffBike is not hooked");
    lua_pushboolean(L, 1);
    return 1;
}

// Client element flags from CClientPed. A GTA write that is not hooked is a DIAG line.
// Ids at or above MAX_SERVER_ELEMENTS (Common.h) are local entities (CClientEntity.h:159).
constexpr uint32_t kMaxServerElements = 131072u;
constexpr double kDegToRad = 0.017453292519943295;
constexpr double kRadToDeg = 57.29577951308232;

struct PedRef {
    uint32_t id = 0;
    bool player = false;
};

struct BodyState {
    uint64_t generation = 0;
    bool haveHead = false, headless = false;
    bool haveFrozen = false, frozen = false;
    bool bleeding = false;
    bool haveFight = false;
    unsigned fight = 15;
    bool haveWalk = false;
    unsigned walk = 0;
    bool haveNative = false, nativeWalk = false;
    bool haveArmor = false;
    float armor = 0;
    bool haveGang = false, gang = false;
    bool haveCam = false;
    float camRad = 0;
    bool haveMission = false, mission = false;
    bool haveStory = false, story = false;
    bool haveCrit = false, crit = true;
    bool haveDrag = false, drag = true;
    bool haveOnly = false, onlyPlayer = false;
    bool haveStay = false, stay = false;
    bool haveNever = false, never = false;
    bool haveProof = false;
    bool proofBullet = false, proofFire = false, proofExplosion = false, proofCollision = false, proofMelee = false;
    bool haveAccuracy = false;
    int accuracy = 0;
    bool haveRate = false;
    int rate = 0;
};

std::mutex g_bodyMu;
std::unordered_map<uint32_t, BodyState> g_body;
long long g_money = 0;
bool g_mapForced = false;
int g_mapAlpha = 155;
std::string g_missionOwner;
std::string g_missionBlock;

// CHud.h eHudComponent order. Lua names are CLuaFunctionParseHelpers.cpp:97-114.
enum HudComp {
    kAmmo, kWeapon, kHealth, kBreath, kArmour, kMoney, kVehicleName, kAreaName, kRadar, kClock, kRadio, kWanted,
    kCrosshair, kAll, kVital, kHelp, kRadarMap, kRadarBlips, kRadarAltimeter, kHudCount
};
enum HudPropId {
    kFill, kFill2, kBorder, kPercent, kBlink, kPos, kSize, kDrop, kOutline, kShadow, kStyle, kAlign, kProportional,
    kCustomAlpha, kTextSize, kAllProps
};
struct HudSlot {
    bool pos = false, size = false, textSize = false, fill = false, fill2 = false, drop = false;
    bool border = false, percent = false, blink = false, outline = false, shadow = false;
    bool style = false, align = false, proportional = false, alpha = false;
    float x = 0, y = 0, w = 0, h = 0, tw = 0, th = 0, blinkValue = 10.f, outlineValue = 0, shadowValue = 0;
    unsigned char fr = 255, fg = 255, fb = 255, fa = 255;
    unsigned char f2r = 255, f2g = 255, f2b = 255, f2a = 255;
    unsigned char dr = 0, dg = 0, db = 0, da = 255;
    bool borderValue = true, percentValue = false, proportionalValue = false, alphaValue = false;
    std::string styleValue = "gothic";
    std::string alignValue = "left";
};
HudSlot g_hud[kHudCount];

void ResetPedBody() {
    std::lock_guard lock(g_bodyMu);
    g_body.clear();
    g_money = 0;
    g_mapForced = false;
    g_mapAlpha = 155;
    g_missionOwner.clear();
    g_missionBlock.clear();
    for (HudSlot& slot : g_hud) slot = {};
}

BodyState LoadBody(uint32_t id) {
    const uint64_t generation = PedQueries::Generation(id);
    std::lock_guard lock(g_bodyMu);
    auto found = g_body.find(id);
    if (found == g_body.end() || found->second.generation != generation) return {};
    return found->second;
}

void StoreBody(uint32_t id, BodyState state) {
    state.generation = PedQueries::Generation(id);
    std::lock_guard lock(g_bodyMu);
    g_body[id] = std::move(state);
}

std::string ArgPhrase(lua_State* L, int index) {
    char buf[160];
    switch (lua_type(L, index)) {
        case LUA_TNUMBER:
            std::snprintf(buf, sizeof buf, "number (%s)", lua_tostring(L, index));
            return buf;
        case LUA_TSTRING: {
            size_t length = 0;
            const char* text = lua_tolstring(L, index, &length);
            std::string value(text ? text : "", text ? length : 0);
            for (unsigned char c : value) {
                if (c < 32 || c > 126) return "string";
            }
            if (value.size() > 10) {
                value.resize(10);
                value[7] = value[8] = value[9] = '.';
            }
            return "string (\"" + value + "\")";
        }
        case LUA_TBOOLEAN:
            return lua_toboolean(L, index) ? "boolean (true)" : "boolean (false)";
        case LUA_TNIL: return "nil";
        case LUA_TNONE: return "none";
        case LUA_TFUNCTION: return "function";
        case LUA_TTHREAD: return "coroutine";
        case LUA_TUSERDATA: return "userdata";
        case LUA_TLIGHTUSERDATA: return "light userdata";
        case LUA_TTABLE: {
            const uint32_t id = ElementIdOf(L, index);
            if (id && Game::ScriptPedAlive(id)) return "ped";
            if (id) {
                const char* kind = ServerWorld::ElementKindName(id);
                if (kind) return kind;
            }
            return "table";
        }
        default: return "none";
    }
}

int FailArg(lua_State* L, const char* function, const char* expected, int index) {
    const std::string got = ArgPhrase(L, index);
    LUA_LOGE("Bad argument @ '%s' [Expected %s at argument %d, got %s]", function, expected, index, got.c_str());
    lua_pushboolean(L, 0);
    return 1;
}

int FailText(lua_State* L, const char* function, const char* message) {
    LUA_LOGE("Bad argument @ '%s' [%s]", function, message);
    lua_pushboolean(L, 0);
    return 1;
}

int RaiseArg(lua_State* L, const char* function, const char* expected, int index) {
    const std::string got = ArgPhrase(L, index);
    return luaL_error(L, "Bad argument @ '%s' [Expected %s at argument %d, got %s]", function, expected, index, got.c_str());
}

bool AsPed(uint32_t id, PedRef& ped) {
    ped = {};
    if (!id || id == UINT32_MAX) return false;
    if (Game::ScriptPedAlive(id)) {
        ped.id = id;
        return true;
    }
    const char* kind = ServerWorld::ElementKindName(id);
    if (!kind) return false;
    if (std::strcmp(kind, "player") == 0) {
        ped.id = id;
        ped.player = true;
        return true;
    }
    if (std::strcmp(kind, "ped") == 0) {
        ped.id = id;
        return true;
    }
    return false;
}

bool TakePed(lua_State* L, int index, PedRef& ped) {
    return lua_istable(L, index) && AsPed(ElementIdOf(L, index), ped);
}

bool ClientPed(const PedRef& ped) { return !ped.player; }

bool LocalEntity(const PedRef& ped) { return ped.id >= kMaxServerElements; }

bool CanAuthorPed(const PedRef& ped) {
    return ped.id == ServerWorld::LocalPlayer() || LocalEntity(ped) || ServerWorld::ActorHosted(ped.id);
}

bool ValidWalk(unsigned anim) {
    // CPed.h IsValidMoveAnim: 0, MOVE_PLAYER..MOVE_JETPACK (54..70), MOVE_MAN..MOVE_SKATE (118..138).
    return anim == 0 || (anim >= 54 && anim <= 70) || (anim >= 118 && anim <= 138);
}

template <typename Fn>
void VisitTree(uint32_t id, Fn&& fn, int depth = 0) {
    if (depth > 64) return;
    if (ClientCallPropagationEnabled(id)) {
        std::vector<uint32_t> kids;
        CopyClientChildren(id, kids);
        for (uint32_t child : kids) VisitTree(child, fn, depth + 1);
    }
    fn(id);
}

bool ReadScriptNumber(lua_State* L, const char* function, int index, double& out, bool positive) {
    const int type = lua_type(L, index);
    if (type != LUA_TNUMBER && type != LUA_TSTRING) {
        FailArg(L, function, "number", index);
        return false;
    }
    if (!lua_isnumber(L, index)) {
        FailText(L, function, "Expected number, got non-convertible string");
        return false;
    }
    out = lua_tonumber(L, index);
    if (std::isnan(out)) {
        FailText(L, function, "Expected number, got NaN");
        return false;
    }
    // CScriptArgReader::ReadNumber rejects NaN. Unsigned values also reject negatives.
    // A float may be infinite; an unsigned read cannot.
    if (positive && out < -static_cast<double>(FLT_EPSILON)) {
        FailText(L, function, "Expected positive value, got negative");
        return false;
    }
    if (positive && !std::isfinite(out)) {
        FailText(L, function, "Expected number, got inf");
        return false;
    }
    return true;
}

bool StreamedAlive(uint32_t id) {
    PedQueries::Snapshot state;
    if (!Game::CopyPedQuery(id, state) || !state.streamed) return false;
    return !PedQueries::IsDead(state);
}

bool ApplyHeadless(uint32_t id, bool headless) {
    PedRef ped;
    if (!AsPed(id, ped)) return false;
    BodyState state = LoadBody(id);
    state.haveHead = true;
    state.headless = headless;
    StoreBody(id, state);
    static bool once = false;
    DiagOnce(once, "DIAG setPedHeadless: CPed::RemoveBodyPart is not called");
    return true;
}

// CLuaPedDefs.cpp:1878-1898 and CClientPed::IsHeadless. ENTITY_ADD headless is the unset value.
int IsPedHeadless(lua_State* L) {
    PedRef ped;
    if (!TakePed(L, 1, ped)) return FailArg(L, "isPedHeadless", "ped", 1);
    const BodyState state = LoadBody(ped.id);
    Entity::PedSpawn spawn;
    const bool headless = state.haveHead ? state.headless
                                         : (ServerWorld::CopyPedSpawn(ped.id, spawn) && spawn.headless);
    lua_pushboolean(L, headless ? 1 : 0);
    return 1;
}

// CLuaPedDefs.cpp:2356-2377 and CStaticFunctionDefinitions.cpp:2558-2567.
int SetPedHeadless(lua_State* L) {
    PedRef ped;
    if (!TakePed(L, 1, ped)) return FailArg(L, "setPedHeadless", "ped", 1);
    if (lua_type(L, 2) != LUA_TBOOLEAN) return FailArg(L, "setPedHeadless", "bool", 2);
    const bool headless = lua_toboolean(L, 2) != 0;
    bool ok = false;
    VisitTree(ped.id, [&](uint32_t id) {
        const bool applied = ApplyHeadless(id, headless);
        if (id == ped.id) ok = applied;
    });
    lua_pushboolean(L, ok ? 1 : 0);
    return 1;
}

bool ApplyFrozen(uint32_t id, bool frozen) {
    PedRef ped;
    if (!AsPed(id, ped)) return false;
    BodyState state = LoadBody(id);
    state.haveFrozen = true;
    state.frozen = frozen;
    StoreBody(id, state);
    if (Game::ScriptPedAlive(id)) Game::NoteScriptFrozen(id, frozen);
    static bool once = false;
    DiagOnce(once, "DIAG setPedFrozen: CClientPed::SetFrozen task clear is not called");
    return true;
}

// CLuaPedDefs.cpp:1901-1921. A script ped's wardrobe hold is not m_bFrozen (that starts false).
int IsPedFrozen(lua_State* L) {
    PedRef ped;
    if (!TakePed(L, 1, ped)) return FailArg(L, "isPedFrozen", "ped", 1);
    const BodyState state = LoadBody(ped.id);
    Entity::PedSpawn spawn;
    const bool frozen = state.haveFrozen ? state.frozen
                                         : (ServerWorld::CopyPedSpawn(ped.id, spawn) && spawn.frozen);
    lua_pushboolean(L, frozen ? 1 : 0);
    return 1;
}

// CLuaPedDefs.cpp:2380-2401 and CStaticFunctionDefinitions.cpp:2570-2579.
int SetPedFrozen(lua_State* L) {
    PedRef ped;
    if (!TakePed(L, 1, ped)) return FailArg(L, "setPedFrozen", "ped", 1);
    if (lua_type(L, 2) != LUA_TBOOLEAN) return FailArg(L, "setPedFrozen", "bool", 2);
    const bool frozen = lua_toboolean(L, 2) != 0;
    bool ok = false;
    VisitTree(ped.id, [&](uint32_t id) {
        const bool applied = ApplyFrozen(id, frozen);
        if (id == ped.id) ok = applied;
    });
    lua_pushboolean(L, ok ? 1 : 0);
    return 1;
}

// CLuaPedDefs.cpp:1924-1944. Without a streamed ped, GetFootBlood is not read and the result is false.
int IsPedFootBloodEnabled(lua_State* L) {
    PedRef ped;
    if (!TakePed(L, 1, ped)) return FailArg(L, "isPedFootBloodEnabled", "ped", 1);
    static bool once = false;
    DiagOnce(once, "DIAG isPedFootBloodEnabled: CPed::GetFootBlood is not read");
    lua_pushboolean(L, 0);
    return 1;
}

bool ApplyFootBlood(uint32_t id) {
    PedRef ped;
    if (!AsPed(id, ped)) return false;
    static bool once = false;
    DiagOnce(once, "DIAG setPedFootBloodEnabled: CPed::SetFootBlood is not called");
    return true;
}

// CLuaPedDefs.cpp:2404-2425. The call returns true for a ped even when no body stores the bit.
int SetPedFootBloodEnabled(lua_State* L) {
    PedRef ped;
    if (!TakePed(L, 1, ped)) return FailArg(L, "setPedFootBloodEnabled", "ped", 1);
    if (lua_type(L, 2) != LUA_TBOOLEAN) return FailArg(L, "setPedFootBloodEnabled", "bool", 2);
    bool ok = false;
    VisitTree(ped.id, [&](uint32_t id) {
        const bool applied = ApplyFootBlood(id);
        if (id == ped.id) ok = applied;
    });
    lua_pushboolean(L, ok ? 1 : 0);
    return 1;
}

// CLuaPedDefs.cpp:1947-1949. ArgumentParser. Players are peds. Default is not bleeding.
int IsPedBleeding(lua_State* L) {
    PedRef ped;
    if (!TakePed(L, 1, ped)) return RaiseArg(L, "isPedBleeding", "ped", 1);
    lua_pushboolean(L, LoadBody(ped.id).bleeding ? 1 : 0);
    return 1;
}

// CLuaPedDefs.cpp:2428-2431.
int SetPedBleeding(lua_State* L) {
    PedRef ped;
    if (!TakePed(L, 1, ped)) return RaiseArg(L, "setPedBleeding", "ped", 1);
    if (lua_type(L, 2) != LUA_TBOOLEAN) return RaiseArg(L, "setPedBleeding", "boolean", 2);
    BodyState state = LoadBody(ped.id);
    state.bleeding = lua_toboolean(L, 2) != 0;
    StoreBody(ped.id, state);
    static bool once = false;
    DiagOnce(once, "DIAG setPedBleeding: CPed::SetBleeding is not called");
    lua_pushboolean(L, 1);
    return 1;
}

// CLuaPedDefs.cpp:2145-2165. No streamed ped means GetOxygenLevel is -1 and the call returns false.
int GetPedOxygenLevel(lua_State* L) {
    PedRef ped;
    if (!TakePed(L, 1, ped)) return FailArg(L, "getPedOxygenLevel", "ped", 1);
    static bool once = false;
    DiagOnce(once, "DIAG getPedOxygenLevel: CPed::GetOxygenLevel is not read");
    lua_pushboolean(L, 0);
    return 1;
}

// CLuaPedDefs.cpp:2846-2867. SetOxygenLevel still returns true when the ped has no body.
int SetPedOxygenLevel(lua_State* L) {
    PedRef ped;
    if (!TakePed(L, 1, ped)) return FailArg(L, "setPedOxygenLevel", "ped", 1);
    double oxygen = 0;
    if (!ReadScriptNumber(L, "setPedOxygenLevel", 2, oxygen, false)) return 1;
    static bool once = false;
    DiagOnce(once, "DIAG setPedOxygenLevel: CPed::SetOxygenLevel is not called");
    lua_pushboolean(L, 1);
    return 1;
}

// CLuaPedDefs.cpp:1139-1141. A script write wins. Otherwise the spawn value, then the local sample.
int GetPedArmorNamed(lua_State* L, const char* name) {
    PedRef ped;
    if (!TakePed(L, 1, ped)) return FailArg(L, name, "ped", 1);
    const BodyState state = LoadBody(ped.id);
    if (state.haveArmor) {
        lua_pushnumber(L, state.armor);
        return 1;
    }
    Entity::PedSpawn spawn;
    if (ServerWorld::CopyPedSpawn(ped.id, spawn)) {
        lua_pushnumber(L, spawn.armor);
        return 1;
    }
    lua_pushnumber(L, ped.id == ServerWorld::LocalPlayer() ? Game::CopyLocalArmor() : 0.0);
    return 1;
}
int GetPedArmor(lua_State* L) { return GetPedArmorNamed(L, "getPedArmor"); }
int GetPlayerArmor(lua_State* L) { return GetPedArmorNamed(L, "getPlayerArmor"); }

// CLuaPedDefs.cpp:2834-2843. A synced armor lock ignores the write and still returns true.
int SetPedArmor(lua_State* L) {
    PedRef ped;
    if (!TakePed(L, 1, ped)) return RaiseArg(L, "setPedArmor", "ped", 1);
    if (!lua_isnumber(L, 2)) return RaiseArg(L, "setPedArmor", "number", 2);
    const double number = lua_tonumber(L, 2);
    if (std::isnan(number))
        return luaL_error(L, "Bad argument @ 'setPedArmor' [Expected number at argument 2, got NaN]");
    if (std::isinf(number))
        return luaL_error(L, "Bad argument @ 'setPedArmor' [Expected number at argument 2, got inf]");
    const float armor = static_cast<float>(number);
    if (armor < 0.f) return luaL_error(L, "Armor must be greater than or equal to 0");
    if (armor > 100.f) return luaL_error(L, "Armor must be less than or equal to 100");
    Entity::PedSpawn spawn;
    const bool locked = ServerWorld::CopyPedSpawn(ped.id, spawn) && spawn.synced;
    if (!locked) {
        BodyState state = LoadBody(ped.id);
        state.haveArmor = true;
        state.armor = armor;
        StoreBody(ped.id, state);
    }
    static bool once = false;
    DiagOnce(once, "DIAG setPedArmor: CPed::SetArmor is not called");
    lua_pushboolean(L, 1);
    return 1;
}

unsigned FightingStyle(uint32_t id) {
    const BodyState state = LoadBody(id);
    return state.haveFight ? state.fight : 15u;
}

// CLuaPedDefs.cpp:1722-1741. Default STYLE_GRAB_KICK is 15.
int GetPedFightingStyle(lua_State* L) {
    PedRef ped;
    if (!TakePed(L, 1, ped)) return FailArg(L, "getPedFightingStyle", "ped", 1);
    lua_pushnumber(L, FightingStyle(ped.id));
    return 1;
}

bool ApplyFight(uint32_t id, unsigned style) {
    PedRef ped;
    if (!AsPed(id, ped) || !ClientPed(ped) || !LocalEntity(ped)) return false;
    if (FightingStyle(id) == style) return false;
    BodyState state = LoadBody(id);
    state.haveFight = true;
    state.fight = style;
    StoreBody(id, state);
    static bool once = false;
    DiagOnce(once, "DIAG setPedFightingStyle: CPed::SetFightingStyle is not called");
    return true;
}

// CLuaPedDefs.cpp:2310-2316 and CStaticFunctionDefinitions.cpp:2527-2543.
int SetPedFightingStyle(lua_State* L) {
    PedRef ped;
    if (!TakePed(L, 1, ped)) return RaiseArg(L, "setPedFightingStyle", "ped", 1);
    if (!lua_isnumber(L, 2)) return RaiseArg(L, "setPedFightingStyle", "number", 2);
    const double number = lua_tonumber(L, 2);
    if (std::isnan(number))
        return luaL_error(L, "Bad argument @ 'setPedFightingStyle' [Expected number at argument 2, got NaN]");
    if (std::isinf(number))
        return luaL_error(L, "Bad argument @ 'setPedFightingStyle' [Expected number at argument 2, got inf]");
    if (number < 0.0 || number >= 4294967296.0)
        return luaL_error(L, "Bad argument @ 'setPedFightingStyle' [Expected number within the destination type range at argument 2, got out-of-range number]");
    const unsigned style = static_cast<unsigned>(number);
    if (style < 4 || style > 16) return luaL_error(L, "Style can only be between 4 and 16");
    bool ok = false;
    VisitTree(ped.id, [&](uint32_t id) {
        const bool applied = ApplyFight(id, style);
        if (id == ped.id) ok = applied;
    });
    lua_pushboolean(L, ok ? 1 : 0);
    return 1;
}

unsigned WalkingStyle(uint32_t id) {
    const BodyState state = LoadBody(id);
    if (state.haveNative && state.nativeWalk) return state.haveWalk ? state.walk : 0u;
    if (state.haveWalk) return state.walk;
    Entity::PedSpawn spawn;
    if (ServerWorld::CopyPedSpawn(id, spawn)) return spawn.moveAnim;
    return 0;
}

// CLuaPedDefs.cpp:1832-1852.
int GetPedWalkingStyle(lua_State* L) {
    PedRef ped;
    if (!TakePed(L, 1, ped)) return FailArg(L, "getPedWalkingStyle", "ped", 1);
    lua_pushnumber(L, WalkingStyle(ped.id));
    return 1;
}

bool ApplyWalk(uint32_t id, unsigned anim) {
    PedRef ped;
    if (!AsPed(id, ped) || !ValidWalk(anim)) return false;
    BodyState state = LoadBody(id);
    state.haveNative = true;
    state.nativeWalk = false;
    state.haveWalk = true;
    state.walk = anim;
    StoreBody(id, state);
    static bool once = false;
    DiagOnce(once, "DIAG setPedWalkingStyle: CPed::SetMoveAnim is not called");
    return true;
}

// CLuaPedDefs.cpp:2785-2807 and CStaticFunctionDefinitions.cpp:2434-2447.
int SetPedWalkingStyle(lua_State* L) {
    PedRef ped;
    if (!TakePed(L, 1, ped)) return FailArg(L, "setPedWalkingStyle", "ped", 1);
    double number = 0;
    if (!ReadScriptNumber(L, "setPedWalkingStyle", 2, number, true)) return 1;
    if (number >= 9223372036854775808.0) return FailText(L, "setPedWalkingStyle", "Expected number, got inf");
    const unsigned anim = static_cast<unsigned>(static_cast<int64_t>(number));
    bool ok = false;
    VisitTree(ped.id, [&](uint32_t id) {
        const bool applied = ApplyWalk(id, anim);
        if (id == ped.id) ok = applied;
    });
    lua_pushboolean(L, ok ? 1 : 0);
    return 1;
}

// CLuaPedDefs.cpp:1855-1875.
int IsPedUsingNativeWalkingStyle(lua_State* L) {
    PedRef ped;
    if (!TakePed(L, 1, ped)) return FailArg(L, "isPedUsingNativeWalkingStyle", "ped", 1);
    const BodyState state = LoadBody(ped.id);
    lua_pushboolean(L, state.haveNative && state.nativeWalk ? 1 : 0);
    return 1;
}

bool ApplyNativeWalk(uint32_t id, bool enabled) {
    PedRef ped;
    if (!AsPed(id, ped)) return false;
    BodyState state = LoadBody(id);
    const bool current = state.haveNative && state.nativeWalk;
    if (current != enabled) {
        state.haveNative = true;
        state.nativeWalk = enabled;
        state.haveWalk = true;
        state.walk = 0;
        StoreBody(id, state);
        static bool once = false;
        DiagOnce(once, "DIAG setPedUseNativeWalkingStyle: CPed::SetMoveAnim is not called");
    }
    return true;
}

// CLuaPedDefs.cpp:2810-2831. An unchanged flag still returns true and does not reset the style.
int SetPedUseNativeWalkingStyle(lua_State* L) {
    PedRef ped;
    if (!TakePed(L, 1, ped)) return FailArg(L, "setPedUseNativeWalkingStyle", "ped", 1);
    if (lua_type(L, 2) != LUA_TBOOLEAN) return FailArg(L, "setPedUseNativeWalkingStyle", "bool", 2);
    const bool enabled = lua_toboolean(L, 2) != 0;
    bool ok = false;
    VisitTree(ped.id, [&](uint32_t id) {
        const bool applied = ApplyNativeWalk(id, enabled);
        if (id == ped.id) ok = applied;
    });
    lua_pushboolean(L, ok ? 1 : 0);
    return 1;
}

// CLuaPedDefs.cpp:1615-1638.
int GetPedClothes(lua_State* L) {
    PedRef ped;
    if (!TakePed(L, 1, ped)) return FailArg(L, "getPedClothes", "ped", 1);
    double number = 0;
    if (!ReadScriptNumber(L, "getPedClothes", 2, number, true)) return 1;
    if (number >= 9223372036854775808.0) return FailText(L, "getPedClothes", "Expected number, got inf");
    const unsigned type = static_cast<unsigned char>(static_cast<int64_t>(number));
    if (type >= 18) {
        lua_pushboolean(L, 0);
        return 1;
    }
    char texture[40];
    char model[40];
    if (Game::CopyScriptClothes(ped.id, static_cast<int>(type), texture, 40, model, 40)) {
        lua_pushstring(L, texture);
        lua_pushstring(L, model);
        return 2;
    }
    Entity::PedSpawn spawn;
    if (ServerWorld::CopyPedSpawn(ped.id, spawn)) {
        for (const auto& piece : spawn.clothes) {
            if (piece.type == type && !piece.texture.empty()) {
                lua_pushstring(L, piece.texture.c_str());
                lua_pushstring(L, piece.model.c_str());
                return 2;
            }
        }
    }
    static bool once = false;
    if (Game::ScriptPedAlive(ped.id) && type >= 4)
        DiagOnce(once, "DIAG getPedClothes: script clothes are stored for slots 0-3");
    lua_pushboolean(L, 0);
    return 1;
}

bool ApplyGang(uint32_t id, bool driveby) {
    PedRef ped;
    if (!AsPed(id, ped)) return false;
    BodyState state = LoadBody(id);
    state.haveGang = true;
    state.gang = driveby;
    StoreBody(id, state);
    static bool once = false;
    DiagOnce(once, "DIAG setPedDoingGangDriveby: the gang driveby task is not started");
    return true;
}

// CLuaPedDefs.cpp:1699-1719.
int IsPedDoingGangDriveby(lua_State* L) {
    PedRef ped;
    if (!TakePed(L, 1, ped)) return FailArg(L, "isPedDoingGangDriveby", "ped", 1);
    lua_pushboolean(L, LoadBody(ped.id).gang ? 1 : 0);
    return 1;
}

// CLuaPedDefs.cpp:2286-2307.
int SetPedDoingGangDriveby(lua_State* L) {
    PedRef ped;
    if (!TakePed(L, 1, ped)) return FailArg(L, "setPedDoingGangDriveby", "ped", 1);
    if (lua_type(L, 2) != LUA_TBOOLEAN) return FailArg(L, "setPedDoingGangDriveby", "bool", 2);
    const bool driveby = lua_toboolean(L, 2) != 0;
    bool ok = false;
    VisitTree(ped.id, [&](uint32_t id) {
        const bool applied = ApplyGang(id, driveby);
        if (id == ped.id) ok = applied;
    });
    lua_pushboolean(L, ok ? 1 : 0);
    return 1;
}

// CLuaPedDefs.cpp:1952-1972. Degrees. The local camera is TheCamera until a script sets one.
int GetPedCameraRotation(lua_State* L) {
    PedRef ped;
    if (!TakePed(L, 1, ped)) return FailArg(L, "getPedCameraRotation", "ped", 1);
    const BodyState state = LoadBody(ped.id);
    double radians = state.haveCam ? state.camRad : 0.0;
    if (!state.haveCam && ped.id == ServerWorld::LocalPlayer()) {
        Game::OnFootKeys keys;
        Game::CopyOnFoot(0.f, 0.f, 0.f, keys);
        radians = keys.camera;
    }
    lua_pushnumber(L, radians * kRadToDeg);
    return 1;
}

bool ApplyCamera(uint32_t id, float radians) {
    PedRef ped;
    if (!AsPed(id, ped)) return false;
    BodyState state = LoadBody(id);
    state.haveCam = true;
    state.camRad = radians;
    StoreBody(id, state);
    Game::SetPedCameraRotation(id, radians);
    static bool once = false;
    DiagOnce(once, "DIAG setPedCameraRotation: CCam::SetDirection is not called");
    return true;
}

// CLuaPedDefs.cpp:2434-2456.
int SetPedCameraRotation(lua_State* L) {
    PedRef ped;
    if (!TakePed(L, 1, ped)) return FailArg(L, "setPedCameraRotation", "ped", 1);
    double degrees = 0;
    if (!ReadScriptNumber(L, "setPedCameraRotation", 2, degrees, false)) return 1;
    const float radians = static_cast<float>(degrees * kDegToRad);
    bool ok = false;
    VisitTree(ped.id, [&](uint32_t id) {
        const bool applied = ApplyCamera(id, radians);
        if (id == ped.id) ok = applied;
    });
    lua_pushboolean(L, ok ? 1 : 0);
    return 1;
}

void DiagLiteral(const char* text) {
    static std::mutex mu;
    static std::unordered_set<const char*> seen;
    std::lock_guard lock(mu);
    if (!seen.insert(text).second) return;
    LUA_LOGI("%s", text);
}

bool ApplyFlag(uint32_t id, bool enabled, bool BodyState::* have, bool BodyState::* value, const char* diag) {
    PedRef ped;
    if (!AsPed(id, ped) || !ClientPed(ped)) return false;
    BodyState state = LoadBody(id);
    state.*have = true;
    state.*value = enabled;
    StoreBody(id, state);
    DiagLiteral(diag);
    return true;
}

// CLuaPedDefs.cpp:3690-3724 and 3733-3740, 4542-4574. These policies are for script peds.
int IsPedMissionActor(lua_State* L) {
    PedRef ped;
    if (!TakePed(L, 1, ped)) return RaiseArg(L, "isPedMissionActor", "ped", 1);
    const BodyState state = LoadBody(ped.id);
    lua_pushboolean(L, ClientPed(ped) && state.haveMission && state.mission ? 1 : 0);
    return 1;
}
int SetPedMissionActor(lua_State* L) {
    PedRef ped;
    if (!TakePed(L, 1, ped)) return RaiseArg(L, "setPedMissionActor", "ped", 1);
    if (lua_type(L, 2) != LUA_TBOOLEAN) return RaiseArg(L, "setPedMissionActor", "boolean", 2);
    const bool enabled = lua_toboolean(L, 2) != 0;
    const bool ok = ApplyFlag(ped.id, enabled, &BodyState::haveMission, &BodyState::mission,
                              "DIAG setPedMissionActor: CPed created-by state is not written");
    lua_pushboolean(L, ok ? 1 : 0);
    return 1;
}
int IsPedStoryProtected(lua_State* L) {
    PedRef ped;
    if (!TakePed(L, 1, ped)) return RaiseArg(L, "isPedStoryProtected", "ped", 1);
    const BodyState state = LoadBody(ped.id);
    lua_pushboolean(L, ClientPed(ped) && state.haveStory && state.story ? 1 : 0);
    return 1;
}
int SetPedStoryProtected(lua_State* L) {
    PedRef ped;
    if (!TakePed(L, 1, ped)) return RaiseArg(L, "setPedStoryProtected", "ped", 1);
    if (lua_type(L, 2) != LUA_TBOOLEAN) return RaiseArg(L, "setPedStoryProtected", "boolean", 2);
    const bool ok = ApplyFlag(ped.id, lua_toboolean(L, 2) != 0, &BodyState::haveStory, &BodyState::story,
                              "DIAG setPedStoryProtected: CPed story bits are not written");
    lua_pushboolean(L, ok ? 1 : 0);
    return 1;
}
int GetPedSuffersCriticalHits(lua_State* L) {
    PedRef ped;
    if (!TakePed(L, 1, ped)) return RaiseArg(L, "getPedSuffersCriticalHits", "ped", 1);
    const BodyState state = LoadBody(ped.id);
    const bool value = ClientPed(ped) && (state.haveCrit ? state.crit : true);
    lua_pushboolean(L, value ? 1 : 0);
    return 1;
}
int SetPedSuffersCriticalHits(lua_State* L) {
    PedRef ped;
    if (!TakePed(L, 1, ped)) return RaiseArg(L, "setPedSuffersCriticalHits", "ped", 1);
    if (lua_type(L, 2) != LUA_TBOOLEAN) return RaiseArg(L, "setPedSuffersCriticalHits", "boolean", 2);
    const bool ok = ApplyFlag(ped.id, lua_toboolean(L, 2) != 0, &BodyState::haveCrit, &BodyState::crit,
                              "DIAG setPedSuffersCriticalHits: CPed no-critical-hits is not written");
    lua_pushboolean(L, ok ? 1 : 0);
    return 1;
}
int CanPedBeDraggedOut(lua_State* L) {
    PedRef ped;
    if (!TakePed(L, 1, ped)) return RaiseArg(L, "canPedBeDraggedOut", "ped", 1);
    const BodyState state = LoadBody(ped.id);
    const bool value = ClientPed(ped) && (state.haveDrag ? state.drag : true);
    lua_pushboolean(L, value ? 1 : 0);
    return 1;
}
int SetPedCanBeDraggedOut(lua_State* L) {
    PedRef ped;
    if (!TakePed(L, 1, ped)) return RaiseArg(L, "setPedCanBeDraggedOut", "ped", 1);
    if (lua_type(L, 2) != LUA_TBOOLEAN) return RaiseArg(L, "setPedCanBeDraggedOut", "boolean", 2);
    const bool ok = ApplyFlag(ped.id, lua_toboolean(L, 2) != 0, &BodyState::haveDrag, &BodyState::drag,
                              "DIAG setPedCanBeDraggedOut: CPed cannot-be-dragged-out is not written");
    lua_pushboolean(L, ok ? 1 : 0);
    return 1;
}
int IsPedOnlyDamagedByPlayer(lua_State* L) {
    PedRef ped;
    if (!TakePed(L, 1, ped)) return RaiseArg(L, "isPedOnlyDamagedByPlayer", "ped", 1);
    const BodyState state = LoadBody(ped.id);
    lua_pushboolean(L, ClientPed(ped) && state.haveOnly && state.onlyPlayer ? 1 : 0);
    return 1;
}
int SetPedOnlyDamagedByPlayer(lua_State* L) {
    PedRef ped;
    if (!TakePed(L, 1, ped)) return RaiseArg(L, "setPedOnlyDamagedByPlayer", "ped", 1);
    if (lua_type(L, 2) != LUA_TBOOLEAN) return RaiseArg(L, "setPedOnlyDamagedByPlayer", "boolean", 2);
    const bool ok = ApplyFlag(ped.id, lua_toboolean(L, 2) != 0, &BodyState::haveOnly, &BodyState::onlyPlayer,
                              "DIAG setPedOnlyDamagedByPlayer: CPed only-damaged-by-player is not written");
    lua_pushboolean(L, ok ? 1 : 0);
    return 1;
}
int GetPedStayInSamePlace(lua_State* L) {
    PedRef ped;
    if (!TakePed(L, 1, ped)) return RaiseArg(L, "getPedStayInSamePlace", "ped", 1);
    const BodyState state = LoadBody(ped.id);
    lua_pushboolean(L, ClientPed(ped) && state.haveStay && state.stay ? 1 : 0);
    return 1;
}
int SetPedStayInSamePlace(lua_State* L) {
    PedRef ped;
    if (!TakePed(L, 1, ped)) return RaiseArg(L, "setPedStayInSamePlace", "ped", 1);
    if (lua_type(L, 2) != LUA_TBOOLEAN) return RaiseArg(L, "setPedStayInSamePlace", "boolean", 2);
    const bool ok = ApplyFlag(ped.id, lua_toboolean(L, 2) != 0, &BodyState::haveStay, &BodyState::stay,
                              "DIAG setPedStayInSamePlace: CPed::SetScriptStayInSamePlace is not called");
    lua_pushboolean(L, ok ? 1 : 0);
    return 1;
}
int IsPedNeverTargeted(lua_State* L) {
    PedRef ped;
    if (!TakePed(L, 1, ped)) return RaiseArg(L, "isPedNeverTargeted", "ped", 1);
    const BodyState state = LoadBody(ped.id);
    lua_pushboolean(L, ClientPed(ped) && state.haveNever && state.never ? 1 : 0);
    return 1;
}
int SetPedNeverTargeted(lua_State* L) {
    PedRef ped;
    if (!TakePed(L, 1, ped)) return RaiseArg(L, "setPedNeverTargeted", "ped", 1);
    if (lua_type(L, 2) != LUA_TBOOLEAN) return RaiseArg(L, "setPedNeverTargeted", "boolean", 2);
    const bool ok = ApplyFlag(ped.id, lua_toboolean(L, 2) != 0, &BodyState::haveNever, &BodyState::never,
                              "DIAG setPedNeverTargeted: CPed never-targeted is not written");
    lua_pushboolean(L, ok ? 1 : 0);
    return 1;
}

// CLuaPedDefs.cpp:3727-3730. Five booleans. A player with no body returns five falses.
int GetPedPhysicalProofs(lua_State* L) {
    PedRef ped;
    if (!TakePed(L, 1, ped)) return RaiseArg(L, "getPedPhysicalProofs", "ped", 1);
    const BodyState state = LoadBody(ped.id);
    lua_pushboolean(L, state.haveProof && state.proofBullet ? 1 : 0);
    lua_pushboolean(L, state.haveProof && state.proofFire ? 1 : 0);
    lua_pushboolean(L, state.haveProof && state.proofExplosion ? 1 : 0);
    lua_pushboolean(L, state.haveProof && state.proofCollision ? 1 : 0);
    lua_pushboolean(L, state.haveProof && state.proofMelee ? 1 : 0);
    return 5;
}

// CLuaPedDefs.cpp:4577-4583. Only a local or syncing script ped may write proofs.
int SetPedPhysicalProofs(lua_State* L) {
    PedRef ped;
    if (!TakePed(L, 1, ped)) return RaiseArg(L, "setPedPhysicalProofs", "ped", 1);
    bool values[5];
    for (int i = 0; i < 5; ++i) {
        if (lua_type(L, 2 + i) != LUA_TBOOLEAN) return RaiseArg(L, "setPedPhysicalProofs", "boolean", 2 + i);
        values[i] = lua_toboolean(L, 2 + i) != 0;
    }
    if (!ClientPed(ped) || !(LocalEntity(ped) || ServerWorld::ActorHosted(ped.id))) {
        lua_pushboolean(L, 0);
        return 1;
    }
    BodyState state = LoadBody(ped.id);
    state.haveProof = true;
    state.proofBullet = values[0];
    state.proofFire = values[1];
    state.proofExplosion = values[2];
    state.proofCollision = values[3];
    state.proofMelee = values[4];
    StoreBody(ped.id, state);
    static bool once = false;
    DiagOnce(once, "DIAG setPedPhysicalProofs: CPed::SetPhysicalProofs is not called");
    lua_pushboolean(L, 1);
    return 1;
}

int ParserInt(lua_State* L, const char* function, int index, int& out) {
    if (!lua_isnumber(L, index)) return RaiseArg(L, function, "number", index);
    const double number = lua_tonumber(L, index);
    if (std::isnan(number))
        return luaL_error(L, "Bad argument @ '%s' [Expected number at argument %d, got NaN]", function, index);
    if (std::isinf(number))
        return luaL_error(L, "Bad argument @ '%s' [Expected number at argument %d, got inf]", function, index);
    if (number < -2147483648.0 || number >= 2147483648.0)
        return luaL_error(L, "Bad argument @ '%s' [Expected number within the destination type range at argument %d, got out-of-range number]",
                          function, index);
    out = static_cast<int>(number);
    return 0;
}

// CLuaPedDefs.cpp:2965-2971. An unstreamed ped returns -1.
int GetPedWeaponAccuracy(lua_State* L) {
    PedRef ped;
    if (!TakePed(L, 1, ped)) return RaiseArg(L, "getPedWeaponAccuracy", "ped", 1);
    PedQueries::Snapshot snap;
    if (!Game::CopyPedQuery(ped.id, snap) || !snap.streamed) {
        lua_pushinteger(L, -1);
        return 1;
    }
    const BodyState state = LoadBody(ped.id);
    if (!state.haveAccuracy) {
        static bool once = false;
        DiagOnce(once, "DIAG getPedWeaponAccuracy: CPed::GetWeaponAccuracy is not read");
        lua_pushinteger(L, -1);
        return 1;
    }
    lua_pushinteger(L, state.accuracy);
    return 1;
}

// CLuaPedDefs.cpp:2986-2994.
int SetPedWeaponAccuracy(lua_State* L) {
    PedRef ped;
    if (!TakePed(L, 1, ped)) return RaiseArg(L, "setPedWeaponAccuracy", "ped", 1);
    int accuracy = 0;
    if (const int error = ParserInt(L, "setPedWeaponAccuracy", 2, accuracy)) return error;
    if (!StreamedAlive(ped.id) || !CanAuthorPed(ped) || accuracy < 0 || accuracy > 255) {
        lua_pushboolean(L, 0);
        return 1;
    }
    BodyState state = LoadBody(ped.id);
    state.haveAccuracy = true;
    state.accuracy = accuracy;
    StoreBody(ped.id, state);
    static bool once = false;
    DiagOnce(once, "DIAG setPedWeaponAccuracy: CPed::SetWeaponAccuracy is not called");
    lua_pushboolean(L, 1);
    return 1;
}

// CLuaPedDefs.cpp:2950-2962.
int GetPedWeaponShootingRate(lua_State* L) {
    PedRef ped;
    if (!TakePed(L, 1, ped)) return RaiseArg(L, "getPedWeaponShootingRate", "ped", 1);
    PedQueries::Snapshot snap;
    if (!Game::CopyPedQuery(ped.id, snap) || !snap.streamed) {
        lua_pushinteger(L, -1);
        return 1;
    }
    const BodyState state = LoadBody(ped.id);
    if (!state.haveRate) {
        static bool once = false;
        DiagOnce(once, "DIAG getPedWeaponShootingRate: CPed::GetWeaponShootingRate is not read");
        lua_pushinteger(L, -1);
        return 1;
    }
    lua_pushinteger(L, state.rate);
    return 1;
}

// CLuaPedDefs.cpp:2974-2983.
bool ReadSignedInt(lua_State* L, const char* function, int index, int& out) {
    double number = 0;
    if (!ReadScriptNumber(L, function, index, number, false)) return false;
    if (!std::isfinite(number) || number < -2147483648.0 || number >= 2147483648.0) {
        FailText(L, function, "Expected number within the destination type range");
        return false;
    }
    out = static_cast<int>(number);
    return true;
}

// CLuaPlayerDefs.cpp:501-511 and CStaticFunctionDefinitions.cpp:2078-2081.
int GetPlayerMoney(lua_State* L) {
    lua_pushnumber(L, static_cast<lua_Number>(g_money));
    return 1;
}

// CLuaPlayerDefs.cpp:582-604. The instant flag does not animate the HUD.
int SetPlayerMoney(lua_State* L) {
    int amount = 0;
    if (!ReadSignedInt(L, "setPlayerMoney", 1, amount)) return 1;
    if (!lua_isnoneornil(L, 2) && lua_type(L, 2) != LUA_TBOOLEAN) return FailArg(L, "setPlayerMoney", "bool", 2);
    g_money = amount;
    static bool once = false;
    DiagOnce(once, "DIAG setPlayerMoney: CClientGame::SetMoney does not update the HUD");
    lua_pushboolean(L, 1);
    return 1;
}

// CLuaPlayerDefs.cpp:607-628.
int GivePlayerMoney(lua_State* L) {
    int amount = 0;
    if (!ReadSignedInt(L, "givePlayerMoney", 1, amount)) return 1;
    g_money += amount;
    static bool once = false;
    DiagOnce(once, "DIAG givePlayerMoney: CClientGame::SetMoney does not update the HUD");
    lua_pushboolean(L, 1);
    return 1;
}

// CLuaPlayerDefs.cpp:630-648.
int TakePlayerMoney(lua_State* L) {
    int amount = 0;
    if (!ReadSignedInt(L, "takePlayerMoney", 1, amount)) return 1;
    g_money -= amount;
    static bool once = false;
    DiagOnce(once, "DIAG takePlayerMoney: CClientGame::SetMoney does not update the HUD");
    lua_pushboolean(L, 1);
    return 1;
}

// CLuaPlayerDefs.cpp:514-523. CWanted::GetWantedLevel is not read, so the stars stay 0.
int GetPlayerWantedLevel(lua_State* L) {
    static bool once = false;
    DiagOnce(once, "DIAG getPlayerWantedLevel: CWanted::GetWantedLevel is not read");
    lua_pushnumber(L, 0);
    return 1;
}

int SetPedWeaponShootingRate(lua_State* L) {
    PedRef ped;
    if (!TakePed(L, 1, ped)) return RaiseArg(L, "setPedWeaponShootingRate", "ped", 1);
    int rate = 0;
    if (const int error = ParserInt(L, "setPedWeaponShootingRate", 2, rate)) return error;
    if (!StreamedAlive(ped.id) || !CanAuthorPed(ped) || rate < 0 || rate > 255) {
        lua_pushboolean(L, 0);
        return 1;
    }
    BodyState state = LoadBody(ped.id);
    state.haveRate = true;
    state.rate = rate;
    StoreBody(ped.id, state);
    static bool once = false;
    DiagOnce(once, "DIAG setPedWeaponShootingRate: CPed::SetWeaponShootingRate is not called");
    lua_pushboolean(L, 1);
    return 1;
}

bool ReadOptNumber(lua_State* L, const char* function, int index, double& out, double fallback) {
    if (lua_isnoneornil(L, index)) {
        out = fallback;
        return true;
    }
    return ReadScriptNumber(L, function, index, out, false);
}

bool ReadOptBool(lua_State* L, const char* function, int index, bool& out, bool fallback) {
    if (lua_isnoneornil(L, index)) {
        out = fallback;
        return true;
    }
    if (lua_type(L, index) != LUA_TBOOLEAN) {
        FailArg(L, function, "bool", index);
        return false;
    }
    out = lua_toboolean(L, index) != 0;
    return true;
}

bool ReadOptString(lua_State* L, const char* function, int index, std::string& out) {
    if (lua_isnoneornil(L, index)) {
        out.clear();
        return true;
    }
    if (lua_type(L, index) != LUA_TSTRING) {
        FailArg(L, function, "string", index);
        return false;
    }
    size_t length = 0;
    const char* text = lua_tolstring(L, index, &length);
    out.assign(text ? text : "", text ? length : 0);
    return true;
}

// CLuaPedDefs.cpp:2602-2664 and CStaticFunctionDefinitions.cpp:2330-2379.
// An empty block or name kills the animation. A named block is not loaded, so that call returns false.
int SetPedAnimation(lua_State* L) {
    PedRef ped;
    if (!TakePed(L, 1, ped)) return FailArg(L, "setPedAnimation", "ped", 1);
    int index = 2;
    std::string block;
    if (lua_type(L, 2) == LUA_TBOOLEAN || lua_isnoneornil(L, 2)) {
        index = 3;
    } else if (lua_type(L, 2) == LUA_TSTRING) {
        if (!ReadOptString(L, "setPedAnimation", 2, block)) return 1;
        index = 3;
    } else {
        return FailArg(L, "setPedAnimation", "string", 2);
    }
    std::string name;
    if (!ReadOptString(L, "setPedAnimation", index, name)) return 1;
    double time = -1, blend = 250;
    bool loop = true, update = true, interruptible = true, freeze = true, restore = false;
    if (!ReadOptNumber(L, "setPedAnimation", index + 1, time, -1)) return 1;
    if (!ReadOptBool(L, "setPedAnimation", index + 2, loop, true)) return 1;
    if (!ReadOptBool(L, "setPedAnimation", index + 3, update, true)) return 1;
    if (!ReadOptBool(L, "setPedAnimation", index + 4, interruptible, true)) return 1;
    if (!ReadOptBool(L, "setPedAnimation", index + 5, freeze, true)) return 1;
    if (!ReadOptNumber(L, "setPedAnimation", index + 6, blend, 250)) return 1;
    if (!ReadOptBool(L, "setPedAnimation", index + 7, restore, false)) return 1;
    (void)time;
    (void)blend;
    (void)loop;
    (void)update;
    (void)interruptible;
    (void)freeze;
    (void)restore;
    bool ok = false;
    VisitTree(ped.id, [&](uint32_t id) {
        PedRef child;
        const bool applied = AsPed(id, child) && (block.empty() || name.empty());
        if (!applied && AsPed(id, child)) {
            static bool once = false;
            DiagOnce(once, "DIAG setPedAnimation: CAnimManager::GetAnimationBlock is not loaded");
        }
        if (id == ped.id) ok = applied;
    });
    lua_pushboolean(L, ok ? 1 : 0);
    return 1;
}

// CLuaPedDefs.cpp:1769-1806. No named-anim task is running, so there is no block or name.
int GetPedAnimation(lua_State* L) {
    PedRef ped;
    if (!TakePed(L, 1, ped)) return FailArg(L, "getPedAnimation", "ped", 1);
    static bool once = false;
    DiagOnce(once, "DIAG getPedAnimation: CTaskSimpleRunNamedAnim is not read");
    lua_pushboolean(L, 0);
    return 1;
}

int AnimScalar(lua_State* L, const char* name, const char* diag) {
    PedRef ped;
    if (!TakePed(L, 1, ped)) return RaiseArg(L, name, "ped", 1);
    DiagLiteral(diag);
    lua_pushnumber(L, -1);
    return 1;
}

// CLuaPedDefs.cpp:2695-2752. Task type 401 is not running.
int GetPedAnimationProgress(lua_State* L) {
    return AnimScalar(L, "getPedAnimationProgress", "DIAG getPedAnimationProgress: the named-anim task is not read");
}
int GetPedAnimationSpeed(lua_State* L) {
    return AnimScalar(L, "getPedAnimationSpeed", "DIAG getPedAnimationSpeed: the named-anim task is not read");
}
int GetPedAnimationLength(lua_State* L) {
    return AnimScalar(L, "getPedAnimationLength", "DIAG getPedAnimationLength: the named-anim task is not read");
}

// CLuaPedDefs.cpp:2667-2692 and CStaticFunctionDefinitions.cpp:2382-2408.
int SetPedAnimationProgress(lua_State* L) {
    PedRef ped;
    if (!TakePed(L, 1, ped)) return FailArg(L, "setPedAnimationProgress", "ped", 1);
    std::string name;
    double progress = 0;
    if (!ReadOptString(L, "setPedAnimationProgress", 2, name)) return 1;
    if (!ReadOptNumber(L, "setPedAnimationProgress", 3, progress, 0)) return 1;
    (void)progress;
    bool ok = false;
    VisitTree(ped.id, [&](uint32_t id) {
        PedRef child;
        const bool applied = AsPed(id, child) && name.empty();
        if (!applied && AsPed(id, child)) {
            static bool once = false;
            DiagOnce(once, "DIAG setPedAnimationProgress: RpAnimBlendClumpGetAssociation is not called");
        }
        if (id == ped.id) ok = applied;
    });
    lua_pushboolean(L, ok ? 1 : 0);
    return 1;
}

// CLuaPedDefs.cpp:2755-2782. Speed must be 0..10 and the name must be set. The association is still missing.
int SetPedAnimationSpeed(lua_State* L) {
    PedRef ped;
    if (!TakePed(L, 1, ped)) return FailArg(L, "setPedAnimationSpeed", "ped", 1);
    std::string name;
    double speed = 1;
    if (!ReadOptString(L, "setPedAnimationSpeed", 2, name)) return 1;
    if (!ReadOptNumber(L, "setPedAnimationSpeed", 3, speed, 1)) return 1;
    if (name.empty() || speed < 0.0 || speed > 10.0) {
        lua_pushboolean(L, 0);
        return 1;
    }
    static bool once = false;
    DiagOnce(once, "DIAG setPedAnimationSpeed: RpAnimBlendClumpGetAssociation is not called");
    lua_pushboolean(L, 0);
    return 1;
}

// CLuaPedDefs.cpp:1608-1612. No weapon is in WEAPONSTATE_RELOADING.
int IsPedReloadingWeapon(lua_State* L) {
    PedRef ped;
    if (!TakePed(L, 1, ped)) return FailArg(L, "isPedReloadingWeapon", "ped", 1);
    static bool once = false;
    DiagOnce(once, "DIAG isPedReloadingWeapon: CWeapon::GetState is not read");
    lua_pushboolean(L, 0);
    return 1;
}

// CLuaPlayerDefs.cpp:781-800 and CPlayerMap::SetForcedState (CPlayerMap.cpp:751-759).
int ForcePlayerMap(lua_State* L) {
    if (lua_type(L, 1) != LUA_TBOOLEAN) return FailArg(L, "forcePlayerMap", "bool", 1);
    g_mapForced = lua_toboolean(L, 1) != 0;
    static bool once = false;
    DiagOnce(once, "DIAG forcePlayerMap: the player map texture is not shown");
    lua_pushboolean(L, 1);
    return 1;
}

// CLuaPlayerDefs.cpp:803-813.
int IsPlayerMapForced(lua_State* L) {
    lua_pushboolean(L, g_mapForced ? 1 : 0);
    return 1;
}

// CLuaPlayerDefs.cpp:816-826. The map image is not created, so it is not showing.
int IsPlayerMapVisible(lua_State* L) {
    static bool once = false;
    DiagOnce(once, "DIAG isPlayerMapVisible: CPlayerMap has no map image");
    lua_pushboolean(L, 0);
    return 1;
}

// CLuaPlayerDefs.cpp:829-842. The box is calculated only while the map is showing.
int GetPlayerMapBoundingBox(lua_State* L) {
    static bool once = false;
    DiagOnce(once, "DIAG getPlayerMapBoundingBox: the map is not showing");
    lua_pushboolean(L, 0);
    return 1;
}

// CLuaPlayerDefs.cpp:846-849. CClientVariables default mapalpha is 155.
int GetPlayerMapOpacity(lua_State* L) {
    const int opacity = std::max(0, std::min(255, g_mapAlpha));
    lua_pushnumber(L, opacity);
    return 1;
}

// CLuaPlayerDefs.cpp:526-528. The local player starts at debug level 0.
int GetPlayerScriptDebugLevel(lua_State* L) {
    static bool once = false;
    DiagOnce(once, "DIAG getPlayerScriptDebugLevel: the local player starts at level 0");
    lua_pushinteger(L, 0);
    return 1;
}

// CLuaPlayerDefs.cpp:852-854. Camera mode and the current weapon are not read.
int IsPlayerCrosshairVisible(lua_State* L) {
    static bool once = false;
    DiagOnce(once, "DIAG isPlayerCrosshairVisible: CCamera mode and CWeapon are not read");
    lua_pushboolean(L, 0);
    return 1;
}

bool EnumName(const char* registered, const char* given) {
    if (!registered || !given) return false;
    if (std::strcmp(registered, given) == 0) return true;
    while (*registered && *given) {
        const char a = *registered >= 'A' && *registered <= 'Z' ? static_cast<char>(*registered - 'A' + 'a') : *registered;
        const char b = *given >= 'A' && *given <= 'Z' ? static_cast<char>(*given - 'A' + 'a') : *given;
        if (a != b) return false;
        // Desktop looks up the lowercased input. A mixed-case key only matches exactly.
        if (*registered >= 'A' && *registered <= 'Z') return false;
        ++registered;
        ++given;
    }
    return *registered == 0 && *given == 0;
}

bool HudComponent(const char* name, int& id) {
    const char* names[] = {"ammo", "weapon", "health", "breath", "armour", "money", "vehicle_name", "area_name", "radar",
                           "clock", "radio", "wanted", "crosshair", "all", "radar_map", "radar_blips", "radar_altimeter"};
    const int ids[] = {kAmmo, kWeapon, kHealth, kBreath, kArmour, kMoney, kVehicleName, kAreaName, kRadar,
                       kClock, kRadio, kWanted, kCrosshair, kAll, kRadarMap, kRadarBlips, kRadarAltimeter};
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        if (EnumName(names[i], name)) {
            id = ids[i];
            return true;
        }
    }
    return false;
}

bool HudProperty(const char* name, int& id) {
    const char* names[] = {"fillColor", "fillColorSecondary", "drawBlackBorder", "drawPercentage", "blinkingValue",
                           "position", "size", "dropColor", "fontOutline", "fontShadow", "fontStyle", "fontAlignment",
                           "proportional", "useCustomAlpha", "textSize", "all"};
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        if (EnumName(names[i], name)) {
            id = static_cast<int>(i);
            return true;
        }
    }
    return false;
}

bool HudBar(int id) { return id == kHealth || id == kArmour || id == kBreath; }
bool HudText(int id) {
    return id == kClock || id == kMoney || id == kAmmo || id == kAreaName || id == kVehicleName || id == kRadio || id == kWanted;
}
bool HudSecondary(int id) { return id == kRadio || id == kMoney || id == kWanted || id == kWeapon; }

bool ReadHudEnum(lua_State* L, const char* function, int index, const char* typeName, bool component, int& id) {
    if (lua_type(L, index) != LUA_TSTRING)
        return RaiseArg(L, function, "enum", index), false;
    const char* name = lua_tostring(L, index);
    if (component ? HudComponent(name, id) : HudProperty(name, id)) return true;
    luaL_error(L, "Bad argument @ '%s' [Expected %s at argument %d, got %s]", function, typeName, index, ArgPhrase(L, index).c_str());
    return false;
}

void ColorFromNumber(double number, unsigned char& r, unsigned char& g, unsigned char& b, unsigned char& a) {
    // CLuaFunctionParser pops the color as a float, then TOCOLOR2SCOLOR reads ARGB bytes.
    const auto bits = static_cast<std::uint32_t>(static_cast<float>(number));
    r = static_cast<unsigned char>((bits >> 16) & 0xFF);
    g = static_cast<unsigned char>((bits >> 8) & 0xFF);
    b = static_cast<unsigned char>(bits & 0xFF);
    a = static_cast<unsigned char>((bits >> 24) & 0xFF);
}

void PushColor(lua_State* L, unsigned char r, unsigned char g, unsigned char b, unsigned char a) {
    lua_pushnumber(L, r);
    lua_pushnumber(L, g);
    lua_pushnumber(L, b);
    lua_pushnumber(L, a);
}

bool ReadHudVector(lua_State* L, float& x, float& y) {
    if (!lua_isnumber(L, 3) || !lua_isnumber(L, 4)) return false;
    const double a = lua_tonumber(L, 3);
    const double b = lua_tonumber(L, 4);
    if (!std::isfinite(a) || !std::isfinite(b)) return false;
    x = static_cast<float>(a);
    y = static_cast<float>(b);
    return true;
}

void ClearHudSlot(HudSlot& slot) { slot = {}; }

void ResetHudOne(int component, int property);

void ResetHudOne(int component, int property) {
    if (component == kCrosshair || component == kVital || component == kHelp || component == kRadar) return;
    if (component == kAll) {
        for (int i = 0; i < kHelp; ++i) {
            if (i == kAll) continue;
            ResetHudOne(i, property);
        }
        return;
    }
    if (component < 0 || component >= kHudCount) return;
    HudSlot& slot = g_hud[component];
    if (property == kAllProps) {
        for (int i = 0; i < kAllProps; ++i) ResetHudOne(component, i);
        return;
    }
    switch (property) {
        case kPos: slot.pos = false; break;
        case kSize: slot.size = false; break;
        case kTextSize: break;
        case kFill:
            if (HudBar(component) || HudText(component) || component == kWeapon) slot.fill = false;
            break;
        case kFill2:
            if (HudSecondary(component)) slot.fill2 = false;
            break;
        case kDrop:
            if (HudText(component)) {
                slot.drop = true;
                slot.dr = slot.dg = slot.db = 0;
                slot.da = 255;
            }
            break;
        case kBorder:
            if (HudBar(component)) {
                slot.border = true;
                slot.borderValue = true;
            }
            break;
        case kPercent:
            if (HudBar(component)) {
                slot.percent = true;
                slot.percentValue = false;
            }
            break;
        case kBlink:
            if (component == kHealth) {
                slot.blink = true;
                slot.blinkValue = 10.f;
            }
            break;
        case kOutline:
            if (HudText(component)) {
                slot.outline = true;
                slot.outlineValue = 0;
            }
            break;
        case kShadow:
            if (HudText(component)) {
                slot.shadow = true;
                slot.shadowValue = 0;
            }
            break;
        case kStyle:
            if (HudText(component)) {
                slot.style = true;
                slot.styleValue = "gothic";
            }
            break;
        case kAlign:
            if (HudText(component)) {
                slot.align = true;
                slot.alignValue = "left";
            }
            break;
        case kProportional:
            if (HudText(component)) {
                slot.proportional = true;
                slot.proportionalValue = false;
            }
            break;
        case kCustomAlpha:
            slot.alpha = true;
            slot.alphaValue = false;
            break;
        default: break;
    }
}

// CLuaPlayerDefs.cpp:1147-1272. Radar layout is not available. Other unset values are the reset defaults.
int GetPlayerHudComponentProperty(lua_State* L) {
    int component = 0, property = 0;
    if (!ReadHudEnum(L, "getPlayerHudComponentProperty", 1, "hud-component", true, component)) return 0;
    if (!ReadHudEnum(L, "getPlayerHudComponentProperty", 2, "hud-component-property", false, property)) return 0;
    if (component == kRadar) {
        static bool once = false;
        DiagOnce(once, "DIAG getPlayerHudComponentProperty: CRadar::GetLayout is not read");
        lua_pushboolean(L, 0);
        return 1;
    }
    if (component == kAll || component == kCrosshair || component == kVital || component == kHelp) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const HudSlot& slot = g_hud[component];
    static bool once = false;
    DiagOnce(once, "DIAG getPlayerHudComponentProperty: CHud native placement and colors are not read");
    switch (property) {
        case kPos:
            lua_pushnumber(L, slot.pos ? slot.x : 0);
            lua_pushnumber(L, slot.pos ? slot.y : 0);
            return 2;
        case kSize:
            lua_pushnumber(L, slot.size ? slot.w : 0);
            lua_pushnumber(L, slot.size ? slot.h : 0);
            return 2;
        case kTextSize:
            lua_pushnumber(L, slot.textSize ? slot.tw : 0);
            lua_pushnumber(L, slot.textSize ? slot.th : 0);
            return 2;
        case kFill:
            if (!HudBar(component) && !HudText(component) && component != kWeapon) break;
            PushColor(L, slot.fill ? slot.fr : 255, slot.fill ? slot.fg : 255, slot.fill ? slot.fb : 255, slot.fill ? slot.fa : 255);
            return 4;
        case kFill2:
            if (!HudSecondary(component)) break;
            PushColor(L, slot.fill2 ? slot.f2r : 255, slot.fill2 ? slot.f2g : 255, slot.fill2 ? slot.f2b : 255, slot.fill2 ? slot.f2a : 255);
            return 4;
        case kDrop:
            if (!HudText(component)) break;
            PushColor(L, slot.drop ? slot.dr : 0, slot.drop ? slot.dg : 0, slot.drop ? slot.db : 0, slot.drop ? slot.da : 255);
            return 4;
        case kBorder:
            if (!HudBar(component)) break;
            lua_pushboolean(L, (slot.border ? slot.borderValue : true) ? 1 : 0);
            return 1;
        case kPercent:
            if (!HudBar(component)) break;
            lua_pushboolean(L, (slot.percent ? slot.percentValue : false) ? 1 : 0);
            return 1;
        case kBlink:
            if (component != kHealth) break;
            lua_pushnumber(L, slot.blink ? slot.blinkValue : 10.0);
            return 1;
        case kOutline:
            if (!HudText(component)) break;
            lua_pushnumber(L, slot.outline ? slot.outlineValue : 0);
            return 1;
        case kShadow:
            if (!HudText(component)) break;
            lua_pushnumber(L, slot.shadow ? slot.shadowValue : 0);
            return 1;
        case kStyle:
            if (!HudText(component)) break;
            lua_pushstring(L, slot.style ? slot.styleValue.c_str() : "gothic");
            return 1;
        case kAlign:
            if (!HudText(component)) break;
            lua_pushstring(L, slot.align ? slot.alignValue.c_str() : "left");
            return 1;
        case kProportional:
            if (!HudText(component)) break;
            lua_pushboolean(L, (slot.proportional ? slot.proportionalValue : false) ? 1 : 0);
            return 1;
        case kCustomAlpha:
            lua_pushboolean(L, (slot.alpha ? slot.alphaValue : false) ? 1 : 0);
            return 1;
        default: break;
    }
    lua_pushboolean(L, 0);
    return 1;
}

// CLuaPlayerDefs.cpp:857-1015. The value is stored. CHud is not written.
int SetPlayerHudComponentProperty(lua_State* L) {
    int component = 0, property = 0;
    if (!ReadHudEnum(L, "setPlayerHudComponentProperty", 1, "hud-component", true, component)) return 0;
    if (!ReadHudEnum(L, "setPlayerHudComponentProperty", 2, "hud-component-property", false, property)) return 0;
    if (lua_isnone(L, 3)) return RaiseArg(L, "setPlayerHudComponentProperty", "value", 3);
    if (component == kAll || component == kCrosshair || component == kVital || component == kHelp || component == kRadar ||
        component == kRadarMap || component == kRadarBlips || component == kRadarAltimeter || property == kTextSize ||
        property == kAllProps) {
        lua_pushboolean(L, 0);
        return 1;
    }
    HudSlot& slot = g_hud[component];
    bool ok = false;
    switch (property) {
        case kPos:
        case kSize:
        case kTextSize: {
            float x = 0, y = 0;
            if (!ReadHudVector(L, x, y)) break;
            if (property == kPos) {
                slot.pos = true;
                slot.x = x;
                slot.y = y;
            } else if (property == kSize) {
                slot.size = true;
                slot.w = x;
                slot.h = y;
            } else {
                slot.textSize = true;
                slot.tw = x;
                slot.th = y;
            }
            ok = true;
            break;
        }
        case kFill:
        case kFill2:
        case kDrop:
        case kOutline:
        case kShadow:
        case kBlink: {
            if (!lua_isnumber(L, 3) || !std::isfinite(lua_tonumber(L, 3))) break;
            const double number = lua_tonumber(L, 3);
            if (property == kFill || property == kFill2) {
                if (property == kFill && !HudBar(component) && !HudText(component) && component != kWeapon) break;
                if (property == kFill2 && !HudSecondary(component)) break;
                unsigned char r, g, b, a;
                ColorFromNumber(number, r, g, b, a);
                if (property == kFill) {
                    slot.fill = true;
                    slot.fr = r;
                    slot.fg = g;
                    slot.fb = b;
                    slot.fa = a;
                } else {
                    slot.fill2 = true;
                    slot.f2r = r;
                    slot.f2g = g;
                    slot.f2b = b;
                    slot.f2a = a;
                }
            } else if (property == kDrop) {
                if (!HudText(component)) break;
                ColorFromNumber(number, slot.dr, slot.dg, slot.db, slot.da);
                slot.drop = true;
            } else if (property == kBlink) {
                if (component != kHealth) break;
                slot.blink = true;
                slot.blinkValue = static_cast<float>(number);
            } else if (property == kOutline) {
                if (!HudText(component)) break;
                slot.outline = true;
                slot.outlineValue = static_cast<float>(number);
            } else {
                if (!HudText(component)) break;
                slot.shadow = true;
                slot.shadowValue = static_cast<float>(number);
            }
            ok = true;
            break;
        }
        case kBorder:
        case kPercent:
        case kProportional:
        case kCustomAlpha: {
            if (lua_type(L, 3) != LUA_TBOOLEAN) break;
            const bool value = lua_toboolean(L, 3) != 0;
            if (property == kBorder) {
                if (!HudBar(component)) break;
                slot.border = true;
                slot.borderValue = value;
            } else if (property == kPercent) {
                if (!HudBar(component)) break;
                slot.percent = true;
                slot.percentValue = value;
            } else if (property == kProportional) {
                if (!HudText(component)) break;
                slot.proportional = true;
                slot.proportionalValue = value;
            } else {
                slot.alpha = true;
                slot.alphaValue = value;
            }
            ok = true;
            break;
        }
        case kStyle:
        case kAlign: {
            if (lua_type(L, 3) != LUA_TSTRING || !HudText(component)) break;
            const char* text = lua_tostring(L, 3);
            if (property == kStyle) {
                const char* styles[] = {"gothic", "subtitles", "menu", "pricedown"};
                const char* found = nullptr;
                for (const char* style : styles)
                    if (EnumName(style, text)) found = style;
                if (!found) break;
                slot.style = true;
                slot.styleValue = found;
            } else {
                const char* aligns[] = {"center", "left", "right"};
                const char* found = nullptr;
                for (const char* align : aligns)
                    if (EnumName(align, text)) found = align;
                if (!found) break;
                slot.align = true;
                slot.alignValue = found;
            }
            ok = true;
            break;
        }
        default: break;
    }
    if (ok) {
        static bool once = false;
        DiagOnce(once, "DIAG setPlayerHudComponentProperty: CHud is not written");
    }
    lua_pushboolean(L, ok ? 1 : 0);
    return 1;
}

// CLuaPlayerDefs.cpp:1018-1144.
int ResetPlayerHudComponentProperty(lua_State* L) {
    int component = 0, property = 0;
    if (!ReadHudEnum(L, "resetPlayerHudComponentProperty", 1, "hud-component", true, component)) return 0;
    if (!ReadHudEnum(L, "resetPlayerHudComponentProperty", 2, "hud-component-property", false, property)) return 0;
    if (component == kCrosshair || component == kVital || component == kHelp || component == kRadar) {
        lua_pushboolean(L, 0);
        return 1;
    }
    if (property == kFill2 && component != kAll && !HudSecondary(component)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    if ((property == kFill || property == kDrop || property == kOutline || property == kShadow || property == kStyle ||
         property == kAlign || property == kProportional) &&
        component != kAll && !HudText(component) && !(property == kFill && (HudBar(component) || component == kWeapon))) {
        lua_pushboolean(L, 0);
        return 1;
    }
    if ((property == kBorder || property == kPercent) && component != kAll && !HudBar(component)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    if (property == kBlink && component != kAll && component != kHealth) {
        lua_pushboolean(L, 0);
        return 1;
    }
    if (property == kTextSize) {
        lua_pushboolean(L, 0);
        return 1;
    }
    ResetHudOne(component, property);
    static bool once = false;
    DiagOnce(once, "DIAG resetPlayerHudComponentProperty: CHud is not written");
    lua_pushboolean(L, 1);
    return 1;
}

bool IEq(const char* a, const char* b) {
    if (!a || !b) return false;
    while (*a && *b) {
        const char ca = *a >= 'A' && *a <= 'Z' ? static_cast<char>(*a - 'A' + 'a') : *a;
        const char cb = *b >= 'A' && *b <= 'Z' ? static_cast<char>(*b - 'A' + 'a') : *b;
        if (ca != cb) return false;
        ++a;
        ++b;
    }
    return *a == 0 && *b == 0;
}

bool TaskOwner(const PedRef& ped) { return StreamedAlive(ped.id) && CanAuthorPed(ped); }

int TaskFalse(lua_State* L) {
    lua_pushboolean(L, 0);
    return 1;
}

bool TakeVector(lua_State* L, const char* function, int index, float& x, float& y, float& z) {
    if (!lua_isnumber(L, index) || !lua_isnumber(L, index + 1) || !lua_isnumber(L, index + 2))
        return RaiseArg(L, function, "vector3", index), false;
    const double a = lua_tonumber(L, index);
    const double b = lua_tonumber(L, index + 1);
    const double c = lua_tonumber(L, index + 2);
    if (std::isnan(a) || std::isnan(b) || std::isnan(c))
        return luaL_error(L, "Bad argument @ '%s' [Expected number at argument %d, got NaN]", function, index), false;
    if (!std::isfinite(a) || !std::isfinite(b) || !std::isfinite(c))
        return luaL_error(L, "Bad argument @ '%s' [Expected number at argument %d, got inf]", function, index), false;
    x = static_cast<float>(a);
    y = static_cast<float>(b);
    z = static_cast<float>(c);
    return true;
}

bool TakeOptString(lua_State* L, const char* function, int index, std::string& out, const char* fallback) {
    if (lua_isnoneornil(L, index)) {
        out = fallback;
        return true;
    }
    if (!lua_isstring(L, index)) return RaiseArg(L, function, "string", index), false;
    out = lua_tostring(L, index);
    return true;
}

bool TakeOptDouble(lua_State* L, const char* function, int index, double& out, double fallback) {
    if (lua_isnoneornil(L, index)) {
        out = fallback;
        return true;
    }
    if (!lua_isnumber(L, index)) return RaiseArg(L, function, "number", index), false;
    out = lua_tonumber(L, index);
    if (std::isnan(out)) return luaL_error(L, "Bad argument @ '%s' [Expected number at argument %d, got NaN]", function, index), false;
    if (!std::isfinite(out)) return luaL_error(L, "Bad argument @ '%s' [Expected number at argument %d, got inf]", function, index), false;
    return true;
}

bool MoveOk(const std::string& name, bool sprint) {
    return IEq(name.c_str(), "walk") || IEq(name.c_str(), "run") || (sprint && IEq(name.c_str(), "sprint"));
}

bool IsVehicleArg(lua_State* L, int index, uint32_t& id) {
    if (!lua_istable(L, index)) return false;
    id = ElementIdOf(L, index);
    const char* kind = id ? ServerWorld::ElementKindName(id) : nullptr;
    return kind && std::strcmp(kind, "vehicle") == 0;
}

// CLuaPedDefs.cpp:2997-3002. The script-command query is not hooked.
int IsPedNativeTaskReady(lua_State* L) {
    PedRef ped;
    if (!TakePed(L, 1, ped)) return RaiseArg(L, "isPedNativeTaskReady", "ped", 1);
    if (TaskOwner(ped)) {
        static bool once = false;
        DiagOnce(once, "DIAG isPedNativeTaskReady: CTaskManager is not hooked");
    }
    return TaskFalse(L);
}

// CLuaPedDefs.cpp:3005-3032. The go-to task is not created.
int SetPedGoTo(lua_State* L) {
    PedRef ped;
    if (!TakePed(L, 1, ped)) return RaiseArg(L, "setPedGoTo", "ped", 1);
    float x, y, z;
    if (!TakeVector(L, "setPedGoTo", 2, x, y, z)) return 0;
    std::string movement;
    double radius = 0.5, slowdown = 2, timeout = -2;
    if (!TakeOptString(L, "setPedGoTo", 5, movement, "walk")) return 0;
    if (!TakeOptDouble(L, "setPedGoTo", 6, radius, 0.5)) return 0;
    if (!TakeOptDouble(L, "setPedGoTo", 7, slowdown, 2)) return 0;
    if (!TakeOptDouble(L, "setPedGoTo", 8, timeout, -2)) return 0;
    if (!TaskOwner(ped) || !MoveOk(movement, true) || radius <= 0 || slowdown < radius || timeout < -2) return TaskFalse(L);
    static bool once = false;
    DiagOnce(once, "DIAG setPedGoTo: CTaskComplexGoToPointAndStandStill is not created");
    return TaskFalse(L);
}

// CLuaPedDefs.cpp:3035-3068.
int SetPedNavigateTo(lua_State* L) {
    PedRef ped;
    if (!TakePed(L, 1, ped)) return RaiseArg(L, "setPedNavigateTo", "ped", 1);
    float x, y, z;
    if (!TakeVector(L, "setPedNavigateTo", 2, x, y, z)) return 0;
    std::string movement;
    double radius = 0.5, slowdown = 3, height = 2, timeout = -1;
    bool keep = false, blend = true;
    if (!TakeOptString(L, "setPedNavigateTo", 5, movement, "walk")) return 0;
    if (!TakeOptDouble(L, "setPedNavigateTo", 6, radius, 0.5)) return 0;
    if (!TakeOptDouble(L, "setPedNavigateTo", 7, slowdown, 3)) return 0;
    if (!TakeOptDouble(L, "setPedNavigateTo", 8, height, 2)) return 0;
    if (!ReadOptBool(L, "setPedNavigateTo", 9, keep, false)) return 1;
    if (!TakeOptDouble(L, "setPedNavigateTo", 10, timeout, -1)) return 0;
    if (!ReadOptBool(L, "setPedNavigateTo", 11, blend, true)) return 1;
    (void)keep;
    (void)blend;
    if (!TaskOwner(ped) || !MoveOk(movement, true) || radius <= 0 || slowdown < radius || height <= 0 || timeout < -1)
        return TaskFalse(L);
    static bool once = false;
    DiagOnce(once, "DIAG setPedNavigateTo: CTaskComplexFollowNodeRoute is not created");
    return TaskFalse(L);
}

// CLuaPedDefs.cpp:3071-3082.
int SetPedChatWith(lua_State* L) {
    PedRef ped, partner;
    if (!TakePed(L, 1, ped)) return RaiseArg(L, "setPedChatWith", "ped", 1);
    if (!TakePed(L, 2, partner)) return RaiseArg(L, "setPedChatWith", "ped", 2);
    if (lua_type(L, 3) != LUA_TBOOLEAN) return RaiseArg(L, "setPedChatWith", "boolean", 3);
    bool update = true, conversation = true;
    if (!ReadOptBool(L, "setPedChatWith", 4, update, true)) return 1;
    if (!ReadOptBool(L, "setPedChatWith", 5, conversation, true)) return 1;
    (void)update;
    (void)conversation;
    if (ped.id == partner.id || !TaskOwner(ped) || !StreamedAlive(partner.id)) return TaskFalse(L);
    static bool once = false;
    DiagOnce(once, "DIAG setPedChatWith: CTaskComplexPartnerChatEx is not created");
    return TaskFalse(L);
}

// CLuaPedDefs.cpp:3085-3095.
int SetPedStandStill(lua_State* L) {
    PedRef ped;
    if (!TakePed(L, 1, ped)) return RaiseArg(L, "setPedStandStill", "ped", 1);
    double duration = 0;
    if (!TakeOptDouble(L, "setPedStandStill", 2, duration, 0)) return 0;
    if (!TaskOwner(ped) || duration < 0) return TaskFalse(L);
    static bool once = false;
    DiagOnce(once, "DIAG setPedStandStill: CTaskSimpleStandStill is not created");
    return TaskFalse(L);
}

// CLuaPedDefs.cpp:3103-3112.
int SetPedHandsUp(lua_State* L) {
    PedRef ped;
    if (!TakePed(L, 1, ped)) return RaiseArg(L, "setPedHandsUp", "ped", 1);
    if (!lua_isnumber(L, 2)) return RaiseArg(L, "setPedHandsUp", "number", 2);
    const double duration = lua_tonumber(L, 2);
    if (!std::isfinite(duration)) return luaL_error(L, "Bad argument @ 'setPedHandsUp' [Expected number at argument 2, got NaN]");
    uint32_t vehicle = 0;
    uint8_t seat = 0;
    const bool inVehicle = PedOccupiedVehicle(ped.id, vehicle, seat);
    if (!TaskOwner(ped) || duration <= 0 || duration > 600000 || inVehicle) return TaskFalse(L);
    static bool once = false;
    DiagOnce(once, "DIAG setPedHandsUp: CTaskSimpleHandsUp is not created");
    return TaskFalse(L);
}

// CLuaPedDefs.cpp:3115-3124.
int SetPedTurnToFace(lua_State* L) {
    PedRef ped, target;
    if (!TakePed(L, 1, ped)) return RaiseArg(L, "setPedTurnToFace", "ped", 1);
    if (!TakePed(L, 2, target)) return RaiseArg(L, "setPedTurnToFace", "ped", 2);
    if (ped.id == target.id || !TaskOwner(ped) || !StreamedAlive(target.id)) return TaskFalse(L);
    static bool once = false;
    DiagOnce(once, "DIAG setPedTurnToFace: CTaskComplexTurnToFaceEntity is not created");
    return TaskFalse(L);
}

// CLuaPedDefs.cpp:3127-3142.
int SetPedGoToOffset(lua_State* L) {
    PedRef ped, target;
    if (!TakePed(L, 1, ped)) return RaiseArg(L, "setPedGoToOffset", "ped", 1);
    if (!TakePed(L, 2, target)) return RaiseArg(L, "setPedGoToOffset", "ped", 2);
    double timeout = -1, radius = 0.5, angle = 0;
    bool repeat = false;
    if (!TakeOptDouble(L, "setPedGoToOffset", 3, timeout, -1)) return 0;
    if (!TakeOptDouble(L, "setPedGoToOffset", 4, radius, 0.5)) return 0;
    if (!TakeOptDouble(L, "setPedGoToOffset", 5, angle, 0)) return 0;
    if (!ReadOptBool(L, "setPedGoToOffset", 6, repeat, false)) return 1;
    (void)repeat;
    if (ped.id == target.id || !TaskOwner(ped) || !StreamedAlive(target.id) || timeout < -1 || radius <= 0) return TaskFalse(L);
    static bool once = false;
    DiagOnce(once, "DIAG setPedGoToOffset: CTaskComplexGoToEntityOffset is not created");
    return TaskFalse(L);
}

// CLuaPedDefs.cpp:3145-3154.
int SetPedKillOnFoot(lua_State* L) {
    PedRef ped, target;
    if (!TakePed(L, 1, ped)) return RaiseArg(L, "setPedKillOnFoot", "ped", 1);
    if (!TakePed(L, 2, target)) return RaiseArg(L, "setPedKillOnFoot", "ped", 2);
    if (ped.id == target.id || !TaskOwner(ped) || !StreamedAlive(target.id)) return TaskFalse(L);
    static bool once = false;
    DiagOnce(once, "DIAG setPedKillOnFoot: CTaskComplexKillPedOnFoot is not created");
    return TaskFalse(L);
}

// CLuaPedDefs.cpp:4624-4629.
int SetPedAchieveHeading(lua_State* L) {
    PedRef ped;
    if (!TakePed(L, 1, ped)) return RaiseArg(L, "setPedAchieveHeading", "ped", 1);
    if (!lua_isnumber(L, 2)) return RaiseArg(L, "setPedAchieveHeading", "number", 2);
    const double heading = lua_tonumber(L, 2);
    if (!std::isfinite(heading) || !TaskOwner(ped)) return TaskFalse(L);
    static bool once = false;
    DiagOnce(once, "DIAG setPedAchieveHeading: CTaskSimpleAchieveHeading is not created");
    return TaskFalse(L);
}

// CLuaPedDefs.cpp:4632-4641. A zero XY pair is not a coordinate.
int SetPedAimAt(lua_State* L) {
    PedRef ped;
    if (!TakePed(L, 1, ped)) return RaiseArg(L, "setPedAimAt", "ped", 1);
    float x, y, z;
    if (!TakeVector(L, "setPedAimAt", 2, x, y, z)) return 0;
    double duration = 1000;
    if (!TakeOptDouble(L, "setPedAimAt", 5, duration, 1000)) return 0;
    if (!TaskOwner(ped) || (x == 0.f && y == 0.f) || duration < -1) return TaskFalse(L);
    static bool once = false;
    DiagOnce(once, "DIAG setPedAimAt: CTaskSimpleGunControl is not created");
    return TaskFalse(L);
}

// CLuaPedDefs.cpp:3266-3283.
int SetPedShootAt(lua_State* L) {
    PedRef ped;
    if (!TakePed(L, 1, ped)) return RaiseArg(L, "setPedShootAt", "ped", 1);
    float x, y, z;
    if (!TakeVector(L, "setPedShootAt", 2, x, y, z)) return 0;
    double duration = 1000, burst = 5;
    if (!TakeOptDouble(L, "setPedShootAt", 5, duration, 1000)) return 0;
    if (!TakeOptDouble(L, "setPedShootAt", 6, burst, 5)) return 0;
    (void)duration;
    if (!TaskOwner(ped) || (x == 0.f && y == 0.f) || burst < 1 || burst > 32767) return TaskFalse(L);
    static bool once = false;
    DiagOnce(once, "DIAG setPedShootAt: CTaskSimpleGunControl is not created");
    return TaskFalse(L);
}

// CLuaPedDefs.cpp:3213-3224.
int SetPedJump(lua_State* L) {
    PedRef ped;
    if (!TakePed(L, 1, ped)) return RaiseArg(L, "setPedJump", "ped", 1);
    bool climb = true;
    if (!ReadOptBool(L, "setPedJump", 2, climb, true)) return 1;
    (void)climb;
    if (!TaskOwner(ped)) return TaskFalse(L);
    static bool once = false;
    DiagOnce(once, "DIAG setPedJump: CTaskComplexJump is not created");
    return TaskFalse(L);
}

// CLuaPedDefs.cpp:3227-3238. Speech is not changed, so a streamed owner still returns false.
int SetPedScriptedSpeechMuted(lua_State* L) {
    PedRef ped;
    if (!TakePed(L, 1, ped)) return RaiseArg(L, "setPedScriptedSpeechMuted", "ped", 1);
    if (lua_type(L, 2) != LUA_TBOOLEAN) return RaiseArg(L, "setPedScriptedSpeechMuted", "boolean", 2);
    if (!TaskOwner(ped)) return TaskFalse(L);
    static bool once = false;
    DiagOnce(once, "DIAG setPedScriptedSpeechMuted: CPed speech control is not called");
    return TaskFalse(L);
}

// CLuaPedDefs.cpp:3241-3253. There is no facial task.
int SetPedFacialTalk(lua_State* L) {
    PedRef ped;
    if (!TakePed(L, 1, ped)) return RaiseArg(L, "setPedFacialTalk", "ped", 1);
    if (!lua_isnumber(L, 2)) return RaiseArg(L, "setPedFacialTalk", "number", 2);
    const double duration = lua_tonumber(L, 2);
    if (!std::isfinite(duration) || duration < 0) return TaskFalse(L);
    static bool once = false;
    DiagOnce(once, "DIAG setPedFacialTalk: CTaskComplexFacial is not present");
    return TaskFalse(L);
}

// CLuaPedDefs.cpp:3256-3263.
int StopPedFacialTalk(lua_State* L) {
    PedRef ped;
    if (!TakePed(L, 1, ped)) return RaiseArg(L, "stopPedFacialTalk", "ped", 1);
    static bool once = false;
    DiagOnce(once, "DIAG stopPedFacialTalk: CTaskComplexFacial is not present");
    return TaskFalse(L);
}

// CLuaPedDefs.cpp:3194-3210.
int IsPedNativeAmbientCopWanderTask(lua_State* L) {
    PedRef ped;
    if (!TakePed(L, 1, ped)) return RaiseArg(L, "isPedNativeAmbientCopWanderTask", "ped", 1);
    if (StreamedAlive(ped.id)) {
        static bool once = false;
        DiagOnce(once, "DIAG isPedNativeAmbientCopWanderTask: CTaskManager is not hooked");
    }
    return TaskFalse(L);
}

// CLuaPedDefs.cpp:3527-3531. Unstreamed, or no sequence task, is -1.
int GetPedTaskSequenceProgress(lua_State* L) {
    PedRef ped;
    if (!TakePed(L, 1, ped)) return RaiseArg(L, "getPedTaskSequenceProgress", "ped", 1);
    if (TaskOwner(ped)) {
        static bool once = false;
        DiagOnce(once, "DIAG getPedTaskSequenceProgress: the sequence task is not read");
    }
    lua_pushinteger(L, -1);
    return 1;
}

// CLuaPedDefs.cpp:3534-3554. The ped is not the driver of a streamed vehicle.
int SetPedDriveWander(lua_State* L) {
    PedRef ped;
    uint32_t vehicle = 0;
    if (!TakePed(L, 1, ped)) return RaiseArg(L, "setPedDriveWander", "ped", 1);
    if (!IsVehicleArg(L, 2, vehicle)) return RaiseArg(L, "setPedDriveWander", "vehicle", 2);
    if (!lua_isnumber(L, 3)) return RaiseArg(L, "setPedDriveWander", "number", 3);
    const double speed = lua_tonumber(L, 3);
    uint32_t occupied = 0;
    uint8_t seat = 0;
    const bool driving = PedOccupiedVehicle(ped.id, occupied, seat) && occupied == vehicle && seat == 0;
    if (!TaskOwner(ped) || !std::isfinite(speed) || speed < 0 || speed > 255 || !driving) return TaskFalse(L);
    static bool once = false;
    DiagOnce(once, "DIAG setPedDriveWander: CTaskComplexCarDriveWander is not created");
    return TaskFalse(L);
}

// CLuaPedDefs.cpp:2920-2942. Enter and exit need a streamed body.
int SetPedEnterVehicle(lua_State* L) {
    PedRef ped;
    uint32_t vehicle = 0;
    if (!TakePed(L, 1, ped)) return RaiseArg(L, "setPedEnterVehicle", "ped", 1);
    if (!lua_isnoneornil(L, 2) && !IsVehicleArg(L, 2, vehicle)) return RaiseArg(L, "setPedEnterVehicle", "vehicle", 2);
    if (!lua_isnoneornil(L, 3) && lua_type(L, 3) != LUA_TBOOLEAN && !lua_isnumber(L, 3))
        return RaiseArg(L, "setPedEnterVehicle", "number", 3);
    static bool once = false;
    DiagOnce(once, "DIAG setPedEnterVehicle: CPed::EnterVehicle is not called");
    return TaskFalse(L);
}

int SetPedExitVehicle(lua_State* L) {
    PedRef ped;
    if (!TakePed(L, 1, ped)) return RaiseArg(L, "setPedExitVehicle", "ped", 1);
    static bool once = false;
    DiagOnce(once, "DIAG setPedExitVehicle: CPed::ExitVehicle is not called");
    return TaskFalse(L);
}

bool GxtName(const std::string& value) {
    if (value.empty() || value.size() > 7) return false;
    for (unsigned char character : value) {
        const bool digit = character >= '0' && character <= '9';
        const bool letter = (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z');
        if (!digit && !letter && character != '_') return false;
    }
    return true;
}

bool MissionOwned() {
    const std::string& owner = ActiveVmOwner();
    return !owner.empty() && owner == g_missionOwner;
}

// CLuaPlayerDefs.cpp:159-185. The GXT block is not loaded, so a new lease fails.
int AcquireMissionText(lua_State* L) {
    if (lua_type(L, 1) != LUA_TSTRING) return RaiseArg(L, "acquireMissionText", "string", 1);
    const std::string block = lua_tostring(L, 1);
    const std::string& owner = ActiveVmOwner();
    if (owner.empty() || !GxtName(block)) return TaskFalse(L);
    if (g_missionOwner == owner && g_missionBlock == block) {
        lua_pushboolean(L, 1);
        return 1;
    }
    g_missionOwner.clear();
    g_missionBlock.clear();
    static bool once = false;
    DiagOnce(once, "DIAG acquireMissionText: LoadMissionTextBlock is not called");
    return TaskFalse(L);
}

int ShowMissionText(lua_State* L) {
    if (lua_type(L, 1) != LUA_TSTRING) return RaiseArg(L, "showMissionText", "string", 1);
    if (!lua_isnumber(L, 2)) return RaiseArg(L, "showMissionText", "number", 2);
    double flags = 1;
    if (!TakeOptDouble(L, "showMissionText", 3, flags, 1)) return 0;
    if (!MissionOwned() || !GxtName(lua_tostring(L, 1)) || flags > 65535) return TaskFalse(L);
    static bool once = false;
    DiagOnce(once, "DIAG showMissionText: the mission text queue is not drawn");
    return TaskFalse(L);
}

int ShowMissionHelp(lua_State* L) {
    if (lua_type(L, 1) != LUA_TSTRING) return RaiseArg(L, "showMissionHelp", "string", 1);
    bool permanent = false;
    if (!ReadOptBool(L, "showMissionHelp", 2, permanent, false)) return 1;
    (void)permanent;
    if (!MissionOwned() || !GxtName(lua_tostring(L, 1))) return TaskFalse(L);
    static bool once = false;
    DiagOnce(once, "DIAG showMissionHelp: the help queue is not drawn");
    return TaskFalse(L);
}

int ShowMissionBigText(lua_State* L) {
    if (lua_type(L, 1) != LUA_TSTRING) return RaiseArg(L, "showMissionBigText", "string", 1);
    if (!lua_isnumber(L, 2)) return RaiseArg(L, "showMissionBigText", "number", 2);
    if (!MissionOwned() || !GxtName(lua_tostring(L, 1))) return TaskFalse(L);
    static bool once = false;
    DiagOnce(once, "DIAG showMissionBigText: the big text queue is not drawn");
    return TaskFalse(L);
}

int ClearMissionTexts(lua_State* L) {
    if (!MissionOwned()) return TaskFalse(L);
    lua_pushboolean(L, 1);
    return 1;
}

int ClearMissionHelp(lua_State* L) {
    if (!MissionOwned()) return TaskFalse(L);
    lua_pushboolean(L, 1);
    return 1;
}

int ReleaseMissionText(lua_State* L) {
    const std::string& owner = ActiveVmOwner();
    if (owner.empty() || owner != g_missionOwner) return TaskFalse(L);
    g_missionOwner.clear();
    g_missionBlock.clear();
    lua_pushboolean(L, 1);
    return 1;
}

bool BoneInRange(unsigned bone) { return bone <= 302; }

int BoneRejected(lua_State* L, const char* function, unsigned bone) {
    LUA_LOGE("Invalid bone: %u", bone);
    (void)function;
    lua_pushboolean(L, 0);
    return 1;
}

// CLuaPedDefs.cpp:1349-1468. There is no streamed CEntity, so every bone read or write is false.
int BoneCall(lua_State* L, const char* function, int extraNumbers) {
    PedRef ped;
    if (!TakePed(L, 1, ped)) return FailArg(L, function, "ped", 1);
    if (!lua_isnumber(L, 2)) return FailArg(L, function, "number", 2);
    const double bone = lua_tonumber(L, 2);
    if (!std::isfinite(bone) || bone < 0 || bone > 65535) return FailArg(L, function, "number", 2);
    if (!BoneInRange(static_cast<unsigned>(bone))) return BoneRejected(L, function, static_cast<unsigned>(bone));
    for (int i = 0; i < extraNumbers; ++i) {
        if (!lua_isnumber(L, 3 + i)) return FailArg(L, function, "number", 3 + i);
    }
    static bool once = false;
    DiagOnce(once, "DIAG element bone: CEntity bone transforms are not read or written");
    lua_pushboolean(L, 0);
    return 1;
}

int GetElementBonePosition(lua_State* L) { return BoneCall(L, "getElementBonePosition", 0); }
int GetElementBoneRotation(lua_State* L) { return BoneCall(L, "getElementBoneRotation", 0); }
int GetElementBoneQuaternion(lua_State* L) { return BoneCall(L, "getElementBoneQuaternion", 0); }
int GetElementBoneMatrix(lua_State* L) { return BoneCall(L, "getElementBoneMatrix", 0); }
int SetElementBonePosition(lua_State* L) { return BoneCall(L, "setElementBonePosition", 3); }
int SetElementBoneRotation(lua_State* L) { return BoneCall(L, "setElementBoneRotation", 3); }
int SetElementBoneQuaternion(lua_State* L) { return BoneCall(L, "setElementBoneQuaternion", 4); }
int SetElementBoneMatrix(lua_State* L) { return BoneCall(L, "setElementBoneMatrix", 16); }
int UpdateElementRpHAnim(lua_State* L) {
    PedRef ped;
    if (!TakePed(L, 1, ped)) return FailArg(L, "updateElementRpHAnim", "ped", 1);
    static bool once = false;
    DiagOnce(once, "DIAG updateElementRpHAnim: CEntity::UpdateRpHAnim is not called");
    lua_pushboolean(L, 0);
    return 1;
}

// CLuaPedDefs.cpp:1492-1520. Bone ids above the right foot are false. No streamed bone is readable.
int GetPedBonePosition(lua_State* L) {
    PedRef ped;
    if (!TakePed(L, 1, ped)) return FailArg(L, "getPedBonePosition", "ped", 1);
    if (!lua_isnumber(L, 2)) return FailArg(L, "getPedBonePosition", "number", 2);
    const double bone = lua_tonumber(L, 2);
    if (!std::isfinite(bone) || bone < 0) return FailArg(L, "getPedBonePosition", "number", 2);
    if (static_cast<unsigned>(bone) > 54) {
        lua_pushboolean(L, 0);
        return 1;
    }
    static bool once = false;
    DiagOnce(once, "DIAG getPedBonePosition: CPed bone position is not read");
    lua_pushboolean(L, 0);
    return 1;
}

int PedModel(const PedRef& ped) {
    int model = 0;
    float heading = 0;
    bool frozen = false;
    if (Game::CopyScriptPedFacts(ped.id, model, heading, frozen)) return model;
    const int stored = ServerWorld::ModelOf(ped.id);
    if (stored >= 0) return stored;
    return ped.id == ServerWorld::LocalPlayer() ? 0 : -1;
}

// CLuaPedDefs.cpp:2483-2511 and CStaticFunctionDefinitions.cpp:2698-2715.
int SetPedStat(lua_State* L) {
    PedRef ped;
    if (!TakePed(L, 1, ped) || !lua_isnumber(L, 2) || !lua_isnumber(L, 3)) {
        LUA_LOGE("Bad argument @ 'setPedStat' [Expected ped, stat and value]");
        lua_pushboolean(L, 0);
        return 1;
    }
    const double stat = lua_tonumber(L, 2);
    const double value = lua_tonumber(L, 3);
    if (!std::isfinite(stat) || !std::isfinite(value) || stat < 0 || stat > 342 || value < 0 || value > 1000) {
        LUA_LOGE("Bad argument @ 'setPedStat' [Stat must be 0 to 342 and value must be 0 to 1000.]");
        lua_pushboolean(L, 0);
        return 1;
    }
    const unsigned id = static_cast<unsigned>(stat);
    if ((id == 21 || id == 23) && PedModel(ped) != 0) return TaskFalse(L);
    g_stats[ped.id][id] = static_cast<float>(value);
    static bool once = false;
    DiagOnce(once, "DIAG setPedStat: CPed::SetStat is not called");
    lua_pushboolean(L, 1);
    return 1;
}

// CLuaPedDefs.cpp:2514-2545. Only a client-created ped can be killed here.
int KillPed(lua_State* L) {
    PedRef ped;
    if (!TakePed(L, 1, ped)) return FailArg(L, "killPed", "ped", 1);
    if (!lua_isnoneornil(L, 3) && !lua_isnumber(L, 3)) return FailArg(L, "killPed", "number", 3);
    if (!lua_isnoneornil(L, 4) && !lua_isnumber(L, 4)) return FailArg(L, "killPed", "number", 4);
    if (!lua_isnoneornil(L, 5) && lua_type(L, 5) != LUA_TBOOLEAN) return FailArg(L, "killPed", "bool", 5);
    if (!LocalEntity(ped) || ped.player) {
        LUA_LOGE("Bad argument @ 'killPed' [This client side function will only work with client created peds]");
        lua_pushboolean(L, 0);
        return 1;
    }
    PedQueries::Snapshot state;
    if (Game::CopyPedQuery(ped.id, state) && PedQueries::IsDead(state)) return TaskFalse(L);
    PedQueries::Wasted(ped.id);
    static bool once = false;
    DiagOnce(once, "DIAG killPed: CClientPed::Kill is not called; the ped is marked dead");
    lua_pushboolean(L, 1);
    return 1;
}

// CLuaPedDefs.cpp:2459-2480. The aim target is accepted. CPed::SetAimTarget is not called.
int SetPedAimTarget(lua_State* L) {
    PedRef ped;
    if (!TakePed(L, 1, ped)) return FailArg(L, "setPedAimTarget", "ped", 1);
    float x, y, z;
    if (!TakeVector(L, "setPedAimTarget", 2, x, y, z)) return 0;
    (void)x;
    (void)y;
    (void)z;
    static bool once = false;
    DiagOnce(once, "DIAG setPedAimTarget: CPed::SetAimTarget is not called");
    lua_pushboolean(L, 1);
    return 1;
}

// CStaticFunctionDefinitions.cpp:4708-4722. No satchel projectiles are tracked.
int DetonateSatchels(lua_State* L) {
    static bool once = false;
    DiagOnce(once, "DIAG detonateSatchels: no satchel projectiles are counted");
    lua_pushboolean(L, 0);
    return 1;
}

struct Voice {
    bool set = false;
    bool disabled = false;
    std::string type;
    std::string bank;
};
std::unordered_map<uint32_t, Voice> g_voice;
std::unordered_map<uint32_t, std::unordered_map<std::string, float>> g_analog;

int WeaponIdFromName(const char* name) {
    // CLuaFunctionParseHelpers.cpp weapon-type enum. A repeated name keeps the last registration.
    struct Alias {
        const char* name;
        int id;
    };
    const Alias aliases[] = {
        {"fist", 0}, {"brass_knuckle", 1}, {"brassknuckle", 1}, {"golfclub", 2}, {"nightstick", 3}, {"knife", 4},
        {"baseball_bat", 5}, {"bat", 5}, {"shovel", 6}, {"pool_cue", 7}, {"poolstick", 7}, {"katana", 8}, {"chainsaw", 9},
        {"dildo2", 11}, {"dildo1", 10}, {"dildo", 10}, {"vibe2", 13}, {"vibe1", 12}, {"vibrator", 12},
        {"flowers", 14}, {"flower", 14}, {"cane", 15}, {"grenade", 16}, {"teargas", 17}, {"molotov", 18},
        {"pistol", 22}, {"colt 45", 22}, {"silenced_pistol", 23}, {"silenced", 23}, {"desert_eagle", 24}, {"deagle", 24},
        {"shotgun", 25}, {"sawnoff_shotgun", 26}, {"sawed-off", 26}, {"spas12_shotgun", 27}, {"combat shotgun", 27},
        {"uzi", 28}, {"mp5", 29}, {"ak47", 30}, {"ak-47", 30}, {"m4", 31}, {"tec9", 32}, {"tec-9", 32}, {"rifle", 33},
        {"sniper_rifle", 34}, {"sniper", 34}, {"rocket_launcher", 35}, {"rocket launcher", 35},
        {"rocket_launcher_hs", 36}, {"rocket launcher hs", 36}, {"flamethrower", 37}, {"minigun", 38}, {"satchel", 39},
        {"detonator", 40}, {"bomb", 40}, {"spraycan", 41}, {"extinguisher", 42}, {"fire extinguisher", 42},
        {"camera", 43}, {"nightvision", 44}, {"infrared", 45}, {"parachute", 46},
    };
    for (const Alias& alias : aliases)
        if (IEq(alias.name, name)) return alias.id;
    return -1;
}

bool WeaponEnumId(int weapon) {
    return (weapon >= 0 && weapon <= 18) || (weapon >= 22 && weapon <= 46);
}

int ReadWeaponId(lua_State* L, const char* function, int index, int& weapon) {
    if (lua_isnumber(L, index)) {
        const double value = lua_tonumber(L, index);
        if (!std::isfinite(value) || value != std::floor(value) || !WeaponEnumId(static_cast<int>(value)))
            return FailArg(L, function, "weapon", index);
        weapon = static_cast<int>(value);
        return 0;
    }
    if (lua_type(L, index) == LUA_TSTRING) {
        weapon = WeaponIdFromName(lua_tostring(L, index));
        if (weapon < 0) return FailArg(L, function, "weapon", index);
        return 0;
    }
    return FailArg(L, function, "weapon", index);
}

// CLuaPedDefs.cpp:610-642. A voice that was not set cannot be read.
int GetPedVoice(lua_State* L) {
    PedRef ped;
    if (!TakePed(L, 1, ped)) return FailArg(L, "getPedVoice", "ped", 1);
    const auto found = g_voice.find(ped.id);
    if (found != g_voice.end() && found->second.disabled) {
        lua_pushstring(L, "PED_TYPE_DISABLED");
        return 1;
    }
    // CClientPed::GetVoice leaves the name pointers untouched when there is no game ped.
    static bool once = false;
    DiagOnce(once, "DIAG getPedVoice: CPed::GetVoice is not read");
    lua_pushboolean(L, 0);
    return 1;
}

// CLuaPedDefs.cpp:645-682.
int SetPedVoice(lua_State* L) {
    PedRef ped;
    if (!TakePed(L, 1, ped) || lua_type(L, 2) != LUA_TSTRING) return FailArg(L, "setPedVoice", "string", 2);
    if (!lua_isnoneornil(L, 3) && lua_type(L, 3) != LUA_TSTRING) return FailArg(L, "setPedVoice", "string", 3);
    Voice voice;
    voice.set = true;
    voice.type = lua_tostring(L, 2);
    if (IEq(voice.type.c_str(), "PED_TYPE_DISABLED")) voice.disabled = true;
    else if (lua_type(L, 3) == LUA_TSTRING) voice.bank = lua_tostring(L, 3);
    g_voice[ped.id] = voice;
    static bool once = false;
    DiagOnce(once, "DIAG setPedVoice: CPed::SetVoice is not called");
    lua_pushboolean(L, 1);
    return 1;
}

// CLuaPedDefs.cpp:601-607. ResetVoice is not called, so the voice shorts do not change.
int ResetPedVoice(lua_State* L) {
    PedRef ped;
    if (!TakePed(L, 1, ped)) return RaiseArg(L, "resetPedVoice", "ped", 1);
    static bool once = false;
    DiagOnce(once, "DIAG resetPedVoice: CPed::ResetVoice is not called");
    return TaskFalse(L);
}

// CLuaPedDefs.cpp:4612-4621. Say is not called. A bad speech id is an error.
int PlayPedVoiceLine(lua_State* L) {
    PedRef ped;
    if (!TakePed(L, 1, ped)) return RaiseArg(L, "playPedVoiceLine", "ped", 1);
    if (!lua_isnumber(L, 2)) return RaiseArg(L, "playPedVoiceLine", "number", 2);
    const double speech = lua_tonumber(L, 2);
    if (!std::isfinite(speech) || speech < 0 || speech > 359)
        return luaL_error(L, "The argument speechId is invalid. The valid range is 0-359.");
    if (!lua_isnoneornil(L, 3)) {
        if (!lua_isnumber(L, 3)) return RaiseArg(L, "playPedVoiceLine", "number", 3);
        if (lua_tonumber(L, 3) < 0) return luaL_error(L, "The argument probability cannot have a negative value.");
    }
    static bool once = false;
    DiagOnce(once, "DIAG playPedVoiceLine: CPed::Say is not called");
    return 0;
}

const char* CanonicalControl(const char* name) {
    for (const char* control : kControls)
        if (SameControl(control, name)) return control;
    return nullptr;
}

bool AnalogControl(const char* name) {
    const char* analog[] = {"left", "right", "forwards", "backwards", "vehicle_left", "vehicle_right", "steer_forward",
                            "steer_back", "accelerate", "brake_reverse", "special_control_left", "special_control_right",
                            "special_control_up", "special_control_down"};
    for (const char* control : analog)
        if (SameControl(control, name)) return true;
    return false;
}

const char* AnalogOpposite(const char* canonical) {
    if (SameControl(canonical, "forwards")) return "backwards";
    if (SameControl(canonical, "backwards")) return "forwards";
    if (SameControl(canonical, "left")) return "right";
    if (SameControl(canonical, "right")) return "left";
    if (SameControl(canonical, "vehicle_left")) return "vehicle_right";
    if (SameControl(canonical, "vehicle_right")) return "vehicle_left";
    if (SameControl(canonical, "steer_forward")) return "steer_back";
    if (SameControl(canonical, "steer_back")) return "steer_forward";
    if (SameControl(canonical, "special_control_left")) return "special_control_right";
    if (SameControl(canonical, "special_control_right")) return "special_control_left";
    if (SameControl(canonical, "special_control_down")) return "special_control_up";
    if (SameControl(canonical, "special_control_up")) return "special_control_down";
    return nullptr;
}

float StoredAnalog(uint32_t id, const char* canonical) {
    const auto pedControls = g_analog.find(id);
    if (pedControls == g_analog.end()) return 0.f;
    const auto found = pedControls->second.find(canonical);
    return found == pedControls->second.end() ? 0.f : found->second;
}

// CLuaPedDefs.cpp:1672-1696. A player reads the controller. A script ped reads the pad, which starts at 0.
int GetPedAnalogControlState(lua_State* L) {
    PedRef ped;
    if (!TakePed(L, 1, ped) || !lua_isstring(L, 2)) return FailArg(L, "getPedAnalogControlState", "string", 2);
    if (!lua_isnoneornil(L, 3) && lua_type(L, 3) != LUA_TBOOLEAN) return FailArg(L, "getPedAnalogControlState", "bool", 3);
    const char* canonical = CanonicalControl(lua_tostring(L, 2));
    if (ped.player) {
        static bool once = false;
        DiagOnce(once, "DIAG getPedAnalogControlState: the controller state is not read");
        lua_pushnumber(L, 0);
        return 1;
    }
    if (!canonical) return TaskFalse(L);
    lua_pushnumber(L, StoredAnalog(ped.id, canonical));
    return 1;
}

// CLuaPedDefs.cpp:1744-1766 and CClientPad::SetControlState. Only the 14 analog names are stored.
int SetPedAnalogControlState(lua_State* L) {
    PedRef ped;
    if (!TakePed(L, 1, ped) || !lua_isstring(L, 2) || !lua_isnumber(L, 3)) return FailArg(L, "setPedAnalogControlState", "number", 3);
    const char* canonical = CanonicalControl(lua_tostring(L, 2));
    const double state = lua_tonumber(L, 3);
    if (!canonical || !AnalogControl(canonical) || !std::isfinite(state)) return TaskFalse(L);
    const float clamped = static_cast<float>(std::max(0.0, std::min(1.0, state)));
    g_analog[ped.id][canonical] = clamped;
    if (const char* opposite = AnalogOpposite(canonical)) g_analog[ped.id][opposite] = 0.f;
    static bool once = false;
    DiagOnce(once, "DIAG setPedAnalogControlState: CPad is not written");
    lua_pushboolean(L, 1);
    return 1;
}

// CLuaPedDefs.cpp:1582-1607. Only a client ped, not a player, receives a weapon.
int GivePedWeapon(lua_State* L) {
    PedRef ped;
    if (!TakePed(L, 1, ped)) return FailArg(L, "givePedWeapon", "ped", 1);
    int weapon = 0;
    if (const int error = ReadWeaponId(L, "givePedWeapon", 2, weapon)) return error;
    double ammo = 30;
    bool current = false;
    if (!lua_isnoneornil(L, 3) && !ReadScriptNumber(L, "givePedWeapon", 3, ammo, true)) return 1;
    if (!ReadOptBool(L, "givePedWeapon", 4, current, false)) return 1;
    if (ped.player || ammo > 65535) return TaskFalse(L);
    const int slot = PedQueries::WeaponSlot(static_cast<unsigned>(weapon));
    if (slot >= 0) {
        Game::PedRpcOp op;
        op.kind = Game::PedRpcOp::Kind::GiveWeapon;
        op.weapon = weapon;
        op.ammo = static_cast<unsigned>(ammo);
        op.setCurrent = current;
        if (!PedQueries::WeaponRpc(ped.id, op)) return TaskFalse(L);
    }
    static bool once = false;
    DiagOnce(once, "DIAG givePedWeapon: CPed::GiveWeapon is not called; the client cache is updated");
    lua_pushboolean(L, 1);
    return 1;
}

// CLuaPedDefs.cpp:2022-2054. Both elements must be client-created, and the warp is not performed.
int WarpPedIntoVehicle(lua_State* L) {
    PedRef ped;
    uint32_t vehicle = 0;
    if (!TakePed(L, 1, ped)) return FailArg(L, "warpPedIntoVehicle", "ped", 1);
    if (!IsVehicleArg(L, 2, vehicle)) return FailArg(L, "warpPedIntoVehicle", "vehicle", 2);
    double seat = 0;
    if (!lua_isnoneornil(L, 3) && !ReadScriptNumber(L, "warpPedIntoVehicle", 3, seat, true)) return 1;
    (void)seat;
    if (!LocalEntity(ped) || ped.player || vehicle < kMaxServerElements) {
        LUA_LOGE("Bad argument @ 'warpPedIntoVehicle' [This client side function will only work with client created peds and vehicles]");
        lua_pushboolean(L, 0);
        return 1;
    }
    static bool once = false;
    DiagOnce(once, "DIAG warpPedIntoVehicle: CPed::WarpIntoVehicle is not called");
    return TaskFalse(L);
}

// CLuaPedDefs.cpp:2319-2353. The look-at task is accepted and not started.
int SetPedLookAt(lua_State* L) {
    PedRef ped;
    if (!TakePed(L, 1, ped)) return FailArg(L, "setPedLookAt", "ped", 1);
    if (!lua_isnumber(L, 2) || !lua_isnumber(L, 3) || !lua_isnumber(L, 4)) return FailArg(L, "setPedLookAt", "vector3", 2);
    if (!std::isfinite(lua_tonumber(L, 2)) || !std::isfinite(lua_tonumber(L, 3)) || !std::isfinite(lua_tonumber(L, 4)))
        return FailText(L, "setPedLookAt", "Expected number, got inf");
    // Argument 5 is the time. The next value is a target element, or a blend followed by an optional target.
    if (!lua_isnoneornil(L, 5)) {
        if (!lua_isnumber(L, 5) || !std::isfinite(lua_tonumber(L, 5))) return FailArg(L, "setPedLookAt", "number", 5);
    }
    if (lua_istable(L, 6)) {
        if (!ElementIdOf(L, 6)) return FailArg(L, "setPedLookAt", "element", 6);
    } else if (!lua_isnoneornil(L, 6)) {
        if (!lua_isnumber(L, 6) || !std::isfinite(lua_tonumber(L, 6))) return FailArg(L, "setPedLookAt", "number", 6);
        if (lua_istable(L, 7)) {
            if (!ElementIdOf(L, 7)) return FailArg(L, "setPedLookAt", "element", 7);
        } else if (!lua_isnoneornil(L, 7)) {
            return FailArg(L, "setPedLookAt", "element", 7);
        }
    }
    static bool once = false;
    DiagOnce(once, "DIAG setPedLookAt: CPed::LookAt is not called");
    lua_pushboolean(L, 1);
    return 1;
}

// CLuaPedDefs.cpp:4586-4609. Slot 4 is the permanent default task.
int KillPedTask(lua_State* L) {
    PedRef ped;
    if (!TakePed(L, 1, ped)) return RaiseArg(L, "killPedTask", "ped", 1);
    if (!lua_isstring(L, 2)) return RaiseArg(L, "killPedTask", "enum", 2);
    if (!lua_isnumber(L, 3)) return RaiseArg(L, "killPedTask", "number", 3);
    const char* kind = lua_tostring(L, 2);
    const double slot = lua_tonumber(L, 3);
    bool graceful = true;
    if (!ReadOptBool(L, "killPedTask", 4, graceful, true)) return 1;
    (void)graceful;
    if (!std::isfinite(slot) || slot < 0 || slot > 255 || slot != std::floor(slot)) return RaiseArg(L, "killPedTask", "number", 3);
    if (IEq(kind, "primary")) {
        if (static_cast<int>(slot) == 4) return luaL_error(L, "Killing TASK_PRIORITY_DEFAULT is not allowed");
        if (slot >= 5) return luaL_error(L, "Invalid task slot number");
    } else if (IEq(kind, "secondary")) {
        if (slot >= 6) return luaL_error(L, "Invalid task slot number");
    } else {
        return RaiseArg(L, "killPedTask", "enum", 2);
    }
    static bool once = false;
    DiagOnce(once, "DIAG killPedTask: CTaskManager::RemoveTask is not called");
    return TaskFalse(L);
}

// CLuaPedDefs.cpp:4644-4712. No cargo entry exists, so the state is "released" and every change fails.
int GetPedCarriedObject(lua_State* L) {
    (void)L;
    lua_pushboolean(L, 0);
    lua_pushstring(L, "released");
    return 2;
}
int CarryRejected(lua_State* L, const char* which) {
    static bool once = false;
    DiagOnce(once, which);
    return TaskFalse(L);
}
int SetPedCarryObject(lua_State* L) {
    PedRef ped;
    const char* preset = lua_isnoneornil(L, 3) ? "box" : (lua_isstring(L, 3) ? lua_tostring(L, 3) : nullptr);
    if (!TakePed(L, 1, ped) || !lua_istable(L, 2) || !preset || std::strcmp(preset, "box") != 0) return TaskFalse(L);
    return CarryRejected(L, "DIAG setPedCarryObject: CClientCargoManager::Start is not called");
}
int PickUpPedObject(lua_State* L) {
    PedRef ped;
    if (!TakePed(L, 1, ped) || !lua_istable(L, 2)) return TaskFalse(L);
    return CarryRejected(L, "DIAG pickUpPedObject: CClientCargoManager::Start is not called");
}
int PutDownPedObject(lua_State* L) {
    PedRef ped;
    if (!TakePed(L, 1, ped)) return TaskFalse(L);
    return CarryRejected(L, "DIAG putDownPedObject: CClientCargoManager::PutDown is not called");
}
int CancelPedCarryObject(lua_State* L) {
    PedRef ped;
    if (!TakePed(L, 1, ped)) return TaskFalse(L);
    return CarryRejected(L, "DIAG cancelPedCarryObject: CClientCargoManager::Cancel is not called");
}

// CLuaPedDefs.cpp:4669-4686. There is no game ped, so the sound level is not read.
int GetPedNoiseLevel(lua_State* L) {
    PedRef ped;
    if (!TakePed(L, 1, ped)) return TaskFalse(L);
    if (lua_gettop(L) > 1) {
        if (!lua_isnumber(L, 2) || !lua_isnumber(L, 3) || !lua_isnumber(L, 4)) return TaskFalse(L);
        if (!std::isfinite(lua_tonumber(L, 2)) || !std::isfinite(lua_tonumber(L, 3)) || !std::isfinite(lua_tonumber(L, 4)))
            return TaskFalse(L);
    }
    if (!TaskOwner(ped)) return TaskFalse(L);
    static bool once = false;
    DiagOnce(once, "DIAG getPedNoiseLevel: CEventList::GetSoundLevel is not read");
    return TaskFalse(L);
}

bool TextOrNumber(lua_State* L, int index) { return lua_isstring(L, index) || lua_isnumber(L, index); }

// CLuaPedDefs.cpp:3557-3676. The ped is not the driver of a streamed vehicle.
int SetPedDriveTo(lua_State* L) {
    PedRef ped;
    uint32_t vehicle = 0;
    if (!TakePed(L, 1, ped)) return RaiseArg(L, "setPedDriveTo", "ped", 1);
    if (!IsVehicleArg(L, 2, vehicle)) return RaiseArg(L, "setPedDriveTo", "vehicle", 2);
    float x, y, z;
    if (!TakeVector(L, "setPedDriveTo", 3, x, y, z)) return 0;
    if (!lua_isnumber(L, 6) || !std::isfinite(lua_tonumber(L, 6))) return RaiseArg(L, "setPedDriveTo", "number", 6);
    if (!lua_isnoneornil(L, 7) && !TextOrNumber(L, 7)) return RaiseArg(L, "setPedDriveTo", "string", 7);
    if (!lua_isnoneornil(L, 8) && !TextOrNumber(L, 8)) return RaiseArg(L, "setPedDriveTo", "string", 8);
    (void)vehicle;
    (void)x;
    (void)y;
    (void)z;
    static bool once = false;
    DiagOnce(once, "DIAG setPedDriveTo: CTaskComplexCarDrive is not created");
    return TaskFalse(L);
}
int SetPedDriveMission(lua_State* L) {
    PedRef ped;
    uint32_t vehicle = 0, target = 0;
    if (!TakePed(L, 1, ped)) return RaiseArg(L, "setPedDriveMission", "ped", 1);
    if (!IsVehicleArg(L, 2, vehicle)) return RaiseArg(L, "setPedDriveMission", "vehicle", 2);
    if (!IsVehicleArg(L, 3, target)) return RaiseArg(L, "setPedDriveMission", "vehicle", 3);
    if (!TextOrNumber(L, 4)) return RaiseArg(L, "setPedDriveMission", "string", 4);
    if (!lua_isnumber(L, 5) || !std::isfinite(lua_tonumber(L, 5))) return RaiseArg(L, "setPedDriveMission", "number", 5);
    if (!lua_isnoneornil(L, 6) && !TextOrNumber(L, 6)) return RaiseArg(L, "setPedDriveMission", "string", 6);
    (void)vehicle;
    (void)target;
    static bool once = false;
    DiagOnce(once, "DIAG setPedDriveMission: CTaskComplexCarDriveMission is not created");
    return TaskFalse(L);
}
int SetPedDriveBy(lua_State* L) {
    PedRef ped;
    if (!TakePed(L, 1, ped)) return RaiseArg(L, "setPedDriveBy", "ped", 1);
    int range = 0;
    if (lua_isnumber(L, 2) && lua_isnumber(L, 3) && lua_isnumber(L, 4)) {
        float x, y, z;
        if (!TakeVector(L, "setPedDriveBy", 2, x, y, z)) return 0;
        range = 5;
        (void)x;
        (void)y;
        (void)z;
    } else {
        PedRef other;
        uint32_t vehicle = 0;
        if (!TakePed(L, 2, other) && !IsVehicleArg(L, 2, vehicle)) return RaiseArg(L, "setPedDriveBy", "vector3", 2);
        range = 3;
    }
    if (!lua_isnumber(L, range) || !std::isfinite(lua_tonumber(L, range))) return RaiseArg(L, "setPedDriveBy", "number", range);
    if (!lua_isnoneornil(L, range + 1) && !TextOrNumber(L, range + 1)) return RaiseArg(L, "setPedDriveBy", "string", range + 1);
    if (!lua_isnoneornil(L, range + 2) && lua_type(L, range + 2) != LUA_TBOOLEAN)
        return RaiseArg(L, "setPedDriveBy", "bool", range + 2);
    if (!lua_isnoneornil(L, range + 3) && !lua_isnumber(L, range + 3)) return RaiseArg(L, "setPedDriveBy", "number", range + 3);
    static bool once = false;
    DiagOnce(once, "DIAG setPedDriveBy: CTaskComplexGangDriveBy is not created");
    return TaskFalse(L);
}

int FailSequence(lua_State* L, const char* reason) {
    LUA_LOGE("setPedTaskSequence: %s", reason);
    return TaskFalse(L);
}

bool SequenceField(lua_State* L, int table, const char* field, bool required, double fallback, double& out, std::string& error) {
    lua_getfield(L, table, field);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        if (required) {
            error = std::string("missing numeric field '") + field + "'";
            return false;
        }
        out = fallback;
        return true;
    }
    if (!lua_isnumber(L, -1) || !std::isfinite(lua_tonumber(L, -1))) {
        lua_pop(L, 1);
        error = std::string("field '") + field + "' must be a number";
        return false;
    }
    out = lua_tonumber(L, -1);
    lua_pop(L, 1);
    return true;
}

// CLuaPedDefs.cpp:3286-3524. A well-formed sequence still fails because the task factory is absent.
int SetPedTaskSequence(lua_State* L) {
    PedRef ped;
    if (!TakePed(L, 1, ped)) return FailSequence(L, "argument 1 must be a ped");
    if (!TaskOwner(ped)) return FailSequence(L, "ped is not a live local or synchronized streamed ped");
    if (!lua_istable(L, 2)) return FailSequence(L, "argument 2 must be a sequence task table");
    const int count = static_cast<int>(lua_rawlen(L, 2));
    if (count < 1 || count > 8) return FailSequence(L, "sequence must contain between 1 and 8 tasks");
    if (!lua_isnoneornil(L, 3) && lua_type(L, 3) != LUA_TBOOLEAN) return FailSequence(L, "argument 3 must be a boolean");
    bool driveTo = false;
    for (int i = 1; i <= count; ++i) {
        lua_rawgeti(L, 2, i);
        if (!lua_istable(L, -1)) {
            lua_pop(L, 1);
            char text[64];
            std::snprintf(text, sizeof(text), "task %d must be a table", i);
            return FailSequence(L, text);
        }
        const int task = lua_gettop(L);
        lua_getfield(L, task, "task");
        const char* name = lua_isstring(L, -1) ? lua_tostring(L, -1) : nullptr;
        std::string taskName = name ? name : "";
        lua_pop(L, 1);
        std::string error;
        if (taskName.empty()) error = "missing string field 'task'";
        else if (IEq(taskName.c_str(), "leave_car") || IEq(taskName.c_str(), "leave_car_immediately")) {
            error = "field 'vehicle' must be a streamed vehicle";
        } else if (IEq(taskName.c_str(), "go_to") || IEq(taskName.c_str(), "shoot_at") || IEq(taskName.c_str(), "drive_to")) {
            double x = 0, y = 0, z = 0;
            if (!SequenceField(L, task, "x", true, 0, x, error) || !SequenceField(L, task, "y", true, 0, y, error) ||
                !SequenceField(L, task, "z", true, 0, z, error)) {
            } else if (IEq(taskName.c_str(), "go_to")) {
                lua_getfield(L, task, "movement");
                if (!lua_isnil(L, -1)) {
                    const char* movement = lua_isstring(L, -1) ? lua_tostring(L, -1) : nullptr;
                    if (!movement || (!IEq(movement, "walk") && !IEq(movement, "run") && !IEq(movement, "sprint")))
                        error = "field 'movement' must be walk, run, or sprint";
                }
                lua_pop(L, 1);
            } else if (IEq(taskName.c_str(), "shoot_at") && x == 0 && y == 0) {
                error = "shoot_at target or burstLength is outside the native range";
            } else if (IEq(taskName.c_str(), "drive_to")) {
                driveTo = true;
            }
            (void)x;
            (void)y;
            (void)z;
        } else if (IEq(taskName.c_str(), "smart_flee")) {
            lua_getfield(L, task, "target");
            PedRef target;
            const bool pedOk = TakePed(L, -1, target) && TaskOwner(target);
            lua_pop(L, 1);
            if (!pedOk) error = "field 'target' must be a streamed ped";
        } else if (!IEq(taskName.c_str(), "die")) {
            error = "unsupported task type '" + taskName + "'";
        }
        lua_pop(L, 1);
        if (!error.empty()) {
            char text[160];
            std::snprintf(text, sizeof(text), "task %d: %s", i, error.c_str());
            return FailSequence(L, text);
        }
    }
    if (driveTo) return FailSequence(L, "drive_to requires the synchronized ped to own and drive its streamed vehicle");
    return FailSequence(L, "native task factory refused task 1");
}

int NoNativeLease(lua_State* L) {
    static bool once = false;
    DiagOnce(once, "DIAG native ped profile: the resource lease is not stored");
    return TaskFalse(L);
}

bool FiniteNumber(lua_State* L, int index) { return lua_isnumber(L, index) && std::isfinite(lua_tonumber(L, index)); }

// CLuaPedDefs.cpp:3743-4510. Every lease call fails because this VM has no resource lease table.
int AcquirePedNativeEventProfile(lua_State* L) {
    PedRef ped;
    if (!TakePed(L, 1, ped)) return FailArg(L, "acquirePedNativeEventProfile", "ped", 1);
    if (!lua_isstring(L, 2)) return FailArg(L, "acquirePedNativeEventProfile", "string", 2);
    const char* profile = lua_tostring(L, 2);
    if (!ClientPed(ped) || (std::strcmp(profile, "mission") != 0 && std::strcmp(profile, "ambient-wander") != 0 &&
                            std::strcmp(profile, "ambient-cop-safe") != 0))
        return TaskFalse(L);
    return NoNativeLease(L);
}
int ReleasePedNativeEventProfile(lua_State* L) {
    if (!FiniteNumber(L, 1)) return FailArg(L, "releasePedNativeEventProfile", "number", 1);
    return NoNativeLease(L);
}
int IsPedNativeEventProfileActive(lua_State* L) {
    PedRef ped;
    if (!TakePed(L, 1, ped) || !FiniteNumber(L, 2)) return FailArg(L, "isPedNativeEventProfileActive", "number", 2);
    return NoNativeLease(L);
}
int AcquirePedNativeGroup(lua_State* L) {
    if (!lua_istable(L, 1) || lua_type(L, 2) != LUA_TSTRING || std::strcmp(lua_tostring(L, 2), "ambient-random") != 0) {
        LUA_LOGE("Expected a ped table and ambient-random group profile");
        return TaskFalse(L);
    }
    const int count = static_cast<int>(lua_rawlen(L, 1));
    if (count < 2 || count > 5) {
        LUA_LOGE("Native ambient groups require 2 to 5 peds");
        return TaskFalse(L);
    }
    for (int index = 1; index <= count; ++index) {
        lua_rawgeti(L, 1, index);
        PedRef member;
        const bool ped = TakePed(L, -1, member) && ClientPed(member);
        lua_pop(L, 1);
        if (!ped) {
            LUA_LOGE("Native ambient group members must all be peds");
            return TaskFalse(L);
        }
    }
    return NoNativeLease(L);
}
int ReleasePedNativeGroup(lua_State* L) {
    if (!FiniteNumber(L, 1)) return TaskFalse(L);
    return NoNativeLease(L);
}
int IsPedNativeGroupActive(lua_State* L) {
    if (!FiniteNumber(L, 1)) return TaskFalse(L);
    return NoNativeLease(L);
}
int GetPedNativeGroupDiagnostic(lua_State* L) {
    if (!FiniteNumber(L, 1)) return TaskFalse(L);
    return NoNativeLease(L);
}
int ValidatePedNativeCouple(lua_State* L) {
    PedRef a, b;
    if (!TakePed(L, 1, a) || !TakePed(L, 2, b)) return TaskFalse(L);
    return NoNativeLease(L);
}
int AcquirePedNativeCouple(lua_State* L) {
    PedRef a, b;
    if (!TakePed(L, 1, a) || !TakePed(L, 2, b) || !FiniteNumber(L, 3)) return TaskFalse(L);
    const double leader = lua_tonumber(L, 3);
    if (leader != 1 && leader != 2) return TaskFalse(L);
    return NoNativeLease(L);
}
int ReleasePedNativeCouple(lua_State* L) {
    if (!FiniteNumber(L, 1)) return TaskFalse(L);
    return NoNativeLease(L);
}
int IsPedNativeCoupleActive(lua_State* L) {
    if (!FiniteNumber(L, 1)) return TaskFalse(L);
    return NoNativeLease(L);
}
int GetPedNativeCoupleDiagnostic(lua_State* L) {
    if (!FiniteNumber(L, 1)) return TaskFalse(L);
    return NoNativeLease(L);
}
int AcquirePedNativeCouplePresentation(lua_State* L) {
    PedRef a, b;
    if (!TakePed(L, 1, a) || !TakePed(L, 2, b)) return TaskFalse(L);
    return NoNativeLease(L);
}
int UpdatePedNativeCouplePresentation(lua_State* L) {
    if (!FiniteNumber(L, 1)) return TaskFalse(L);
    const double sideA = lua_isnoneornil(L, 2) ? 0 : (FiniteNumber(L, 2) ? lua_tonumber(L, 2) : -1);
    const double sideB = lua_isnoneornil(L, 3) ? 0 : (FiniteNumber(L, 3) ? lua_tonumber(L, 3) : -1);
    if (sideA < 0 || sideA > 2 || sideB < 0 || sideB > 2) return TaskFalse(L);
    return NoNativeLease(L);
}
int ReleasePedNativeCouplePresentation(lua_State* L) {
    if (!FiniteNumber(L, 1)) return TaskFalse(L);
    return NoNativeLease(L);
}
int IsPedNativeCouplePresentationActive(lua_State* L) {
    if (!FiniteNumber(L, 1)) return TaskFalse(L);
    return NoNativeLease(L);
}
int AcquirePedNativePointArm(lua_State* L) {
    PedRef ped;
    if (!TakePed(L, 1, ped)) return TaskFalse(L);
    return NoNativeLease(L);
}
int UpdatePedNativePointArm(lua_State* L) {
    if (!FiniteNumber(L, 1) || !lua_isnumber(L, 2) || !lua_isnumber(L, 3) || !lua_isnumber(L, 4)) return TaskFalse(L);
    return NoNativeLease(L);
}
int ReleasePedNativePointArm(lua_State* L) {
    if (!FiniteNumber(L, 1)) return TaskFalse(L);
    return NoNativeLease(L);
}
int IsPedNativePointArmActive(lua_State* L) {
    if (!FiniteNumber(L, 1)) return TaskFalse(L);
    return NoNativeLease(L);
}
int AddPedNativeGunAimedAtEvent(lua_State* L) {
    PedRef ped, aiming;
    if (!TakePed(L, 1, ped) || !TakePed(L, 2, aiming) || !FiniteNumber(L, 3))
        return FailArg(L, "addPedNativeGunAimedAtEvent", "ped", 1);
    return NoNativeLease(L);
}
int AddPedNativeDamageResponseEvent(lua_State* L) {
    PedRef ped, attacker;
    if (!TakePed(L, 1, ped) || !TakePed(L, 2, attacker) || !FiniteNumber(L, 3) || !FiniteNumber(L, 4) || !FiniteNumber(L, 5))
        return FailArg(L, "addPedNativeDamageResponseEvent", "number", 3);
    const int weapon = static_cast<int>(lua_tonumber(L, 3));
    const int zone = static_cast<int>(lua_tonumber(L, 4));
    if (weapon < 0 || weapon > 54 || !(zone == 0 || (zone >= 3 && zone <= 9))) return TaskFalse(L);
    return NoNativeLease(L);
}
int GetPedNativeVehicleResponse(lua_State* L) {
    PedRef ped;
    if (!TakePed(L, 1, ped)) return FailArg(L, "getPedNativeVehicleResponse", "ped", 1);
    if (!FiniteNumber(L, 2)) return FailArg(L, "getPedNativeVehicleResponse", "number", 2);
    if (!ClientPed(ped)) return TaskFalse(L);
    return NoNativeLease(L);
}
int RestorePedNativeVehicleResponse(lua_State* L) {
    PedRef ped;
    if (!TakePed(L, 1, ped) || !ClientPed(ped) || !lua_istable(L, 2)) return TaskFalse(L);
    return NoNativeLease(L);
}
int AddPedNativeDamageEvent(lua_State* L) {
    PedRef damaged, attacker;
    if (!TakePed(L, 1, damaged) || !TakePed(L, 2, attacker) || !FiniteNumber(L, 3) || !FiniteNumber(L, 4) || !FiniteNumber(L, 5) ||
        !FiniteNumber(L, 6) || !FiniteNumber(L, 7))
        return FailArg(L, "addPedNativeDamageEvent", "number", 3);
    const int weapon = static_cast<int>(lua_tonumber(L, 3));
    const int zone = static_cast<int>(lua_tonumber(L, 4));
    const double factor = lua_tonumber(L, 5);
    const double direction = lua_tonumber(L, 6);
    if (weapon < 0 || weapon > 54 || !(zone == 0 || (zone >= 3 && zone <= 9)) || factor <= 0 || factor > 10000 || direction < 0 ||
        direction > 3)
        return TaskFalse(L);
    return NoNativeLease(L);
}
int AddPedNativeBikeJackTask(lua_State* L) {
    PedRef victim, jacker;
    uint32_t vehicle = 0;
    if (!TakePed(L, 1, victim) || !TakePed(L, 2, jacker) || !IsVehicleArg(L, 3, vehicle) || !FiniteNumber(L, 4) || !FiniteNumber(L, 5) ||
        lua_type(L, 6) != LUA_TBOOLEAN || !FiniteNumber(L, 7))
        return FailArg(L, "addPedNativeBikeJackTask", "vehicle", 3);
    (void)vehicle;
    return NoNativeLease(L);
}

// CLuaPedDefs.cpp:2114-2142. A ped that is not in a vehicle returns false.
int RemovePedFromVehicle(lua_State* L) {
    PedRef ped;
    if (!TakePed(L, 1, ped)) return FailArg(L, "removePedFromVehicle", "ped", 1);
    if (!LocalEntity(ped) || ped.player) {
        LUA_LOGE("Bad argument @ 'removePedFromVehicle' [This client side function will only work with client created peds]");
        lua_pushboolean(L, 0);
        return 1;
    }
    uint32_t vehicle = 0;
    uint8_t seat = 0;
    if (!PedOccupiedVehicle(ped.id, vehicle, seat)) return TaskFalse(L);
    static bool once = false;
    DiagOnce(once, "DIAG removePedFromVehicle: CPed::RemoveFromVehicle is not called");
    return TaskFalse(L);
}

}  // namespace

void BindPedLua(lua_State* L) {
    g_nametags.clear();
    g_stats.clear();
    g_voice.clear();
    g_analog.clear();
    ResetPedBody();
    const struct {
        const char* name;
        lua_CFunction fn;
    } binds[] = {
        {"isPedDead", IsPedDead},
        // CLuaCompatibilityDefs.cpp:67. Same function as isPedDead.
        {"isPlayerDead", IsPedDead},
        {"getPedWeapon", GetPedWeapon},
        // CLuaCompatibilityDefs.cpp:35.
        {"getPlayerWeapon", GetPedWeapon},
        {"getPedWeaponSlot", GetPedWeaponSlot},
        // CLuaCompatibilityDefs.cpp:34.
        {"getPlayerWeaponSlot", GetPedWeaponSlot},
        {"getPedTotalAmmo", GetPedTotalAmmo},
        // CLuaCompatibilityDefs.cpp:37.
        {"getPlayerTotalAmmo", GetPedTotalAmmo},
        {"getPedAmmoInClip", GetPedAmmoInClip},
        // CLuaCompatibilityDefs.cpp:36.
        {"getPlayerAmmoInClip", GetPedAmmoInClip},
        {"getValidPedModels", GetValidPedModels},
        {"getPedOccupiedVehicle", GetPedOccupiedVehicle},
        // CLuaCompatibilityDefs.cpp:39.
        {"getPlayerOccupiedVehicle", GetPedOccupiedVehicle},
        {"getPedOccupiedVehicleSeat", GetPedOccupiedVehicleSeat},
        {"isPedInVehicle", IsPedInVehicle},
        // CLuaCompatibilityDefs.cpp:20.
        {"isPlayerInVehicle", IsPedInVehicle},
        {"doesPedHaveJetPack", DoesPedHaveJetPack},
        // CLuaCompatibilityDefs.cpp:21.
        {"doesPlayerHaveJetPack", DoesPedHaveJetPack},
        {"isPedOnFire", IsPedOnFire},
        {"setPedOnFire", SetPedOnFire},
        {"isPedChoking", IsPedChoking},
        // CLuaCompatibilityDefs.cpp:42.
        {"isPlayerChoking", IsPedChoking},
        {"isPedDucked", IsPedDucked},
        // CLuaCompatibilityDefs.cpp:43.
        {"isPlayerDucked", IsPedDucked},
        {"getPedContactElement", GetPedContactElement},
        // CLuaCompatibilityDefs.cpp:19.
        {"getPlayerContactElement", GetPedContactElement},
        {"getPedWeaponMuzzlePosition", GetPedWeaponMuzzlePosition},
        {"getPedTargetStart", GetPedTargetStart},
        // CLuaCompatibilityDefs.cpp:31.
        {"getPlayerTargetStart", GetPedTargetStart},
        {"getPedTargetEnd", GetPedTargetEnd},
        // CLuaCompatibilityDefs.cpp:32.
        {"getPlayerTargetEnd", GetPedTargetEnd},
        {"getPedTargetCollision", GetPedTargetCollision},
        // CLuaCompatibilityDefs.cpp:33.
        {"getPlayerTargetCollision", GetPedTargetCollision},
        {"isPedOnGround", IsPedOnGround},
        // CLuaCompatibilityDefs.cpp:26.
        {"isPlayerOnGround", IsPedOnGround},
        {"getPedStat", GetPedStat},
        // CLuaCompatibilityDefs.cpp:44.
        {"getPlayerStat", GetPedStat},
        {"getPedTask", GetPedTask},
        // CLuaCompatibilityDefs.cpp:27.
        {"getPlayerTask", GetPedTask},
        {"getPedSimplestTask", GetPedSimplestTask},
        // CLuaCompatibilityDefs.cpp:28.
        {"getPlayerSimplestTask", GetPedSimplestTask},
        {"isPedDoingTask", IsPedDoingTask},
        // CLuaCompatibilityDefs.cpp:29.
        {"isPlayerDoingTask", IsPedDoingTask},
        {"getPedTarget", GetPedTarget},
        // CLuaCompatibilityDefs.cpp:30.
        {"getPlayerTarget", GetPedTarget},
        {"setPedWeaponSlot", SetPedWeaponSlot},
        // CLuaCompatibilityDefs.cpp:45.
        {"setPlayerWeaponSlot", SetPedWeaponSlot},
        {"setPedRotation", SetPedRotation},
        // CLuaCompatibilityDefs.cpp:47.
        {"setPlayerRotation", SetPedRotation},
        {"canPedBeKnockedOffBike", CanPedBeKnockedOffBike},
        // CLuaCompatibilityDefs.cpp:18.
        {"canPlayerBeKnockedOffBike", CanPedBeKnockedOffBike},
        {"setPedCanBeKnockedOffBike", SetPedCanBeKnockedOffBike},
        // CLuaCompatibilityDefs.cpp:48.
        {"setPlayerCanBeKnockedOffBike", SetPedCanBeKnockedOffBike},
        {"getPedControlState", GetPedControlState},
        // CLuaCompatibilityDefs.cpp:71.
        {"getControlState", GetPedControlState},
        {"setPedControlState", SetPedControlState},
        // CLuaCompatibilityDefs.cpp:70.
        {"setControlState", SetPedControlState},
        {"getPlayerFromName", GetPlayerFromName},
        {"getPlayerFromNick", GetPlayerFromName},
        {"getPlayerName", GetPlayerName},
        {"getPlayerNametagColor", GetPlayerNametagColor},
        {"getPlayerNametagText", GetPlayerNametagText},
        {"getPlayerPing", GetPlayerPing},
        {"getPlayerSerial", GetPlayerSerial},
        {"getPlayerTeam", GetPlayerTeam},
        {"getPlayerUserName", GetPlayerUserName},
        {"isPlayerNametagShowing", IsPlayerNametagShowing},
        {"setPlayerNametagColor", SetPlayerNametagColor},
        {"setPlayerNametagShowing", SetPlayerNametagShowing},
        {"setPlayerNametagText", SetPlayerNametagText},
        {"isPedWearingJetpack", IsPedWearingJetpack},
        {"isPedHeadless", IsPedHeadless},
        {"setPedHeadless", SetPedHeadless},
        {"isPedFrozen", IsPedFrozen},
        {"setPedFrozen", SetPedFrozen},
        {"isPedFootBloodEnabled", IsPedFootBloodEnabled},
        {"setPedFootBloodEnabled", SetPedFootBloodEnabled},
        {"isPedBleeding", IsPedBleeding},
        {"setPedBleeding", SetPedBleeding},
        {"getPedOxygenLevel", GetPedOxygenLevel},
        {"setPedOxygenLevel", SetPedOxygenLevel},
        {"getPedArmor", GetPedArmor},
        {"getPlayerArmor", GetPlayerArmor},
        {"setPedArmor", SetPedArmor},
        {"getPedFightingStyle", GetPedFightingStyle},
        {"setPedFightingStyle", SetPedFightingStyle},
        {"getPedWalkingStyle", GetPedWalkingStyle},
        {"setPedWalkingStyle", SetPedWalkingStyle},
        {"isPedUsingNativeWalkingStyle", IsPedUsingNativeWalkingStyle},
        {"setPedUseNativeWalkingStyle", SetPedUseNativeWalkingStyle},
        {"getPedClothes", GetPedClothes},
        {"isPedDoingGangDriveby", IsPedDoingGangDriveby},
        {"setPedDoingGangDriveby", SetPedDoingGangDriveby},
        {"getPedCameraRotation", GetPedCameraRotation},
        {"setPedCameraRotation", SetPedCameraRotation},
        {"isPedMissionActor", IsPedMissionActor},
        {"setPedMissionActor", SetPedMissionActor},
        {"isPedStoryProtected", IsPedStoryProtected},
        {"setPedStoryProtected", SetPedStoryProtected},
        {"getPedSuffersCriticalHits", GetPedSuffersCriticalHits},
        {"setPedSuffersCriticalHits", SetPedSuffersCriticalHits},
        {"canPedBeDraggedOut", CanPedBeDraggedOut},
        {"setPedCanBeDraggedOut", SetPedCanBeDraggedOut},
        {"isPedOnlyDamagedByPlayer", IsPedOnlyDamagedByPlayer},
        {"setPedOnlyDamagedByPlayer", SetPedOnlyDamagedByPlayer},
        {"getPedStayInSamePlace", GetPedStayInSamePlace},
        {"setPedStayInSamePlace", SetPedStayInSamePlace},
        {"isPedNeverTargeted", IsPedNeverTargeted},
        {"setPedNeverTargeted", SetPedNeverTargeted},
        {"getPedPhysicalProofs", GetPedPhysicalProofs},
        {"setPedPhysicalProofs", SetPedPhysicalProofs},
        {"getPedWeaponAccuracy", GetPedWeaponAccuracy},
        {"setPedWeaponAccuracy", SetPedWeaponAccuracy},
        {"getPedWeaponShootingRate", GetPedWeaponShootingRate},
        {"setPedWeaponShootingRate", SetPedWeaponShootingRate},
        {"getPlayerMoney", GetPlayerMoney},
        {"setPlayerMoney", SetPlayerMoney},
        {"givePlayerMoney", GivePlayerMoney},
        {"takePlayerMoney", TakePlayerMoney},
        {"getPlayerWantedLevel", GetPlayerWantedLevel},
        {"setPedAnimation", SetPedAnimation},
        {"getPedAnimation", GetPedAnimation},
        {"getPedAnimationProgress", GetPedAnimationProgress},
        {"getPedAnimationSpeed", GetPedAnimationSpeed},
        {"getPedAnimationLength", GetPedAnimationLength},
        {"setPedAnimationProgress", SetPedAnimationProgress},
        {"setPedAnimationSpeed", SetPedAnimationSpeed},
        {"isPedReloadingWeapon", IsPedReloadingWeapon},
        {"forcePlayerMap", ForcePlayerMap},
        {"isPlayerMapForced", IsPlayerMapForced},
        {"isPlayerMapVisible", IsPlayerMapVisible},
        {"getPlayerMapBoundingBox", GetPlayerMapBoundingBox},
        {"getPlayerMapOpacity", GetPlayerMapOpacity},
        {"getPlayerScriptDebugLevel", GetPlayerScriptDebugLevel},
        {"isPlayerCrosshairVisible", IsPlayerCrosshairVisible},
        {"getPlayerHudComponentProperty", GetPlayerHudComponentProperty},
        {"setPlayerHudComponentProperty", SetPlayerHudComponentProperty},
        {"resetPlayerHudComponentProperty", ResetPlayerHudComponentProperty},
        {"isPedNativeTaskReady", IsPedNativeTaskReady},
        {"setPedGoTo", SetPedGoTo},
        {"setPedNavigateTo", SetPedNavigateTo},
        {"setPedChatWith", SetPedChatWith},
        {"setPedStandStill", SetPedStandStill},
        {"setPedHandsUp", SetPedHandsUp},
        {"setPedTurnToFace", SetPedTurnToFace},
        {"setPedGoToOffset", SetPedGoToOffset},
        {"setPedKillOnFoot", SetPedKillOnFoot},
        {"setPedAchieveHeading", SetPedAchieveHeading},
        {"setPedAimAt", SetPedAimAt},
        {"setPedShootAt", SetPedShootAt},
        {"setPedJump", SetPedJump},
        {"setPedScriptedSpeechMuted", SetPedScriptedSpeechMuted},
        {"setPedFacialTalk", SetPedFacialTalk},
        {"stopPedFacialTalk", StopPedFacialTalk},
        {"isPedNativeAmbientCopWanderTask", IsPedNativeAmbientCopWanderTask},
        {"getPedTaskSequenceProgress", GetPedTaskSequenceProgress},
        {"setPedDriveWander", SetPedDriveWander},
        {"setPedEnterVehicle", SetPedEnterVehicle},
        {"setPedExitVehicle", SetPedExitVehicle},
        {"acquireMissionText", AcquireMissionText},
        {"showMissionText", ShowMissionText},
        {"showMissionHelp", ShowMissionHelp},
        {"showMissionBigText", ShowMissionBigText},
        {"clearMissionTexts", ClearMissionTexts},
        {"clearMissionHelp", ClearMissionHelp},
        {"releaseMissionText", ReleaseMissionText},
        {"getElementBonePosition", GetElementBonePosition},
        {"getElementBoneRotation", GetElementBoneRotation},
        {"getElementBoneQuaternion", GetElementBoneQuaternion},
        {"getElementBoneMatrix", GetElementBoneMatrix},
        {"setElementBonePosition", SetElementBonePosition},
        {"setElementBoneRotation", SetElementBoneRotation},
        {"setElementBoneQuaternion", SetElementBoneQuaternion},
        {"setElementBoneMatrix", SetElementBoneMatrix},
        {"updateElementRpHAnim", UpdateElementRpHAnim},
        {"getPedBonePosition", GetPedBonePosition},
        {"setPedStat", SetPedStat},
        {"killPed", KillPed},
        {"setPedAimTarget", SetPedAimTarget},
        {"detonateSatchels", DetonateSatchels},
        {"removePedFromVehicle", RemovePedFromVehicle},
        {"getPedVoice", GetPedVoice},
        {"setPedVoice", SetPedVoice},
        {"resetPedVoice", ResetPedVoice},
        {"playPedVoiceLine", PlayPedVoiceLine},
        {"getPedAnalogControlState", GetPedAnalogControlState},
        {"setPedAnalogControlState", SetPedAnalogControlState},
        {"givePedWeapon", GivePedWeapon},
        {"warpPedIntoVehicle", WarpPedIntoVehicle},
        {"setPedLookAt", SetPedLookAt},
        {"killPedTask", KillPedTask},
        {"getPedCarriedObject", GetPedCarriedObject},
        {"setPedCarryObject", SetPedCarryObject},
        {"pickUpPedObject", PickUpPedObject},
        {"putDownPedObject", PutDownPedObject},
        {"cancelPedCarryObject", CancelPedCarryObject},
        {"getPedNoiseLevel", GetPedNoiseLevel},
        {"setPedDriveTo", SetPedDriveTo},
        {"setPedDriveMission", SetPedDriveMission},
        {"setPedDriveBy", SetPedDriveBy},
        {"setPedTaskSequence", SetPedTaskSequence},
        {"acquirePedNativeEventProfile", AcquirePedNativeEventProfile},
        {"releasePedNativeEventProfile", ReleasePedNativeEventProfile},
        {"isPedNativeEventProfileActive", IsPedNativeEventProfileActive},
        {"acquirePedNativeGroup", AcquirePedNativeGroup},
        {"releasePedNativeGroup", ReleasePedNativeGroup},
        {"isPedNativeGroupActive", IsPedNativeGroupActive},
        {"getPedNativeGroupDiagnostic", GetPedNativeGroupDiagnostic},
        {"validatePedNativeCouple", ValidatePedNativeCouple},
        {"acquirePedNativeCouple", AcquirePedNativeCouple},
        {"releasePedNativeCouple", ReleasePedNativeCouple},
        {"isPedNativeCoupleActive", IsPedNativeCoupleActive},
        {"getPedNativeCoupleDiagnostic", GetPedNativeCoupleDiagnostic},
        {"acquirePedNativeCouplePresentation", AcquirePedNativeCouplePresentation},
        {"updatePedNativeCouplePresentation", UpdatePedNativeCouplePresentation},
        {"releasePedNativeCouplePresentation", ReleasePedNativeCouplePresentation},
        {"isPedNativeCouplePresentationActive", IsPedNativeCouplePresentationActive},
        {"acquirePedNativePointArm", AcquirePedNativePointArm},
        {"updatePedNativePointArm", UpdatePedNativePointArm},
        {"releasePedNativePointArm", ReleasePedNativePointArm},
        {"isPedNativePointArmActive", IsPedNativePointArmActive},
        {"addPedNativeGunAimedAtEvent", AddPedNativeGunAimedAtEvent},
        {"addPedNativeDamageResponseEvent", AddPedNativeDamageResponseEvent},
        {"getPedNativeVehicleResponse", GetPedNativeVehicleResponse},
        {"restorePedNativeVehicleResponse", RestorePedNativeVehicleResponse},
        {"addPedNativeDamageEvent", AddPedNativeDamageEvent},
        {"addPedNativeBikeJackTask", AddPedNativeBikeJackTask},
    };
    for (const auto& bind : binds) {
        lua_pushcfunction(L, bind.fn);
        lua_setglobal(L, bind.name);
    }
}

}  // namespace WorldRPG::Script

extern "C" void InstallPedLua(lua_State* L) {
    WorldRPG::Script::BindPedLua(L);
}
