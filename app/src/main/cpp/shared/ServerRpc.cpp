#include "ServerRpc.h"

#include "LuaApiExtra.h"
#include "LuaAudioApi.h"
#include "LuaElementTree.h"
#include "LuaWeaponApi.h"
#include "LuaWorldObjectApi.h"
#include "LuaWorldQueryApi.h"
#include "LuaWaterApi.h"
#include "LuaCameraApi.h"
#include "LuaFieldApi.h"
#include "LuaGuiApi.h"
#include "LuaKeyBindApi.h"
#include "LuaVMStub.h"
#include "MtaIds.h"
#include "ResourceLoader.h"
#include "ServerWorld.h"
#include "entity/EntityAddTypes.h"
#include "../gtasa/GameEngine.h"
#include "../gtasa/HudComponents.h"
#include "../net/raknet/RakNetWrapper.h"

#include "lua.hpp"

#include <atomic>
#include <cstring>
#include <cmath>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace WorldRPG::ServerRpc {
namespace {

using Net::BitStreamReader;
namespace Rpc = MtaIds::Rpc;

// A stalled VM must not grow the queue without bound. 512 RPCs is far more than a server sends in the seconds
// before the VM exists (a join sends well under a hundred).
constexpr size_t kMaxQueued = 512;

std::mutex g_mutex;
std::deque<std::function<void()>> g_queue;
std::atomic<uint8_t> g_cameraContext{0};
std::atomic<unsigned> g_dropped{0};

bool Finite(float value) { return std::isfinite(value); }

bool ReadFloat(BitStreamReader& reader, float& out) { return reader.Read(out); }

// Desktop reads these with no range check. A NaN or infinity would reach the engine setters and the Lua getters, so
// the phone rejects them: the packet is dropped as a whole, which is also what a failed Read does on desktop.
bool ReadFiniteFloat(BitStreamReader& reader, float& out) { return reader.Read(out) && Finite(out); }

// NetBitStream::ReadBit.
bool ReadBit(BitStreamReader& reader, bool& out) { return Entity::ReadFlag(reader, out); }

// CWorldRPCs.cpp single float setter: one float, then the Lua-side state.
template <typename Apply>
Result FloatRpc(BitStreamReader& reader, Apply apply) {
    float value = 0.f;
    if (!ReadFiniteFloat(reader, value)) return Result::Malformed;
    Enqueue([apply, value] { apply(value); });
    return Result::Handled;
}

template <typename Apply>
Result ResetRpc(Apply apply) {
    Enqueue([apply] { apply(); });
    return Result::Handled;
}

bool ReadU16String(BitStreamReader& reader, std::string& out, size_t maxBytes = 1024) {
    uint16_t length = 0;
    out.clear();
    if (!reader.Read(length)) return false;
    if (length > maxBytes || static_cast<size_t>(length) * 8 > reader.GetRemainingBits()) return false;
    if (length == 0) return true;
    out.resize(length);
    return reader.ReadBytes(reinterpret_cast<uint8_t*>(out.data()), length);
}

template <int Bits, typename T>
bool ReadRangeBits(BitStreamReader& reader, T& out, T low, T hi) {
    uint32_t raw = 0;
    uint8_t bytes[4] = {};
    if (!reader.ReadBits(bytes, Bits)) return false;
    for (int i = 0; i < (Bits + 7) / 8; ++i) raw |= static_cast<uint32_t>(bytes[i]) << (i * 8);
    if constexpr (Bits < 32) raw &= ((1u << Bits) - 1u);
    const int64_t val = static_cast<int64_t>(raw) + static_cast<int64_t>(low);
    if (val < low) out = low;
    else if (val > hi) out = hi;
    else out = static_cast<T>(val);
    return true;
}

// CWorldRPCs.cpp:72-345 and 564-621.
Result HandleWorld(uint8_t func, BitStreamReader& reader) {
    switch (func) {
        case Rpc::SetTime: {
            // CWorldRPCs::SetTime (CWorldRPCs.cpp:95-107): hour < 24 and minute < 60, else ignored.
            uint8_t hour = 0, minute = 0;
            if (!reader.Read(hour) || !reader.Read(minute)) return Result::Malformed;
            if (hour < 24 && minute < 60) Enqueue([hour, minute] { Game::NoteScriptTime(hour, minute); });
            return Result::Handled;
        }
        case Rpc::SetWeather: {
            // CWorldRPCs::SetWeather (109-120): uchar, <= MAX_VALID_WEATHER (255, Config.h:93).
            uint8_t weather = 0;
            if (!reader.Read(weather)) return Result::Malformed;
            Enqueue([weather] { Script::RpcSetWeather(weather); });
            return Result::Handled;
        }
        case Rpc::SetWeatherBlended: {
            // CWorldRPCs::SetWeatherBlended (122-134): uchar weather, uchar hour.
            uint8_t weather = 0, hour = 0;
            if (!reader.Read(weather) || !reader.Read(hour)) return Result::Malformed;
            Enqueue([weather, hour] { Script::RpcSetWeatherBlended(weather, hour); });
            return Result::Handled;
        }
        case Rpc::SetMinuteDuration: {
            // CWorldRPCs::SetMinuteDuration (136-143): unsigned long, four bytes on the wire.
            uint32_t duration = 0;
            if (!reader.Read(duration)) return Result::Malformed;
            Enqueue([duration] { Script::RpcSetMinuteDuration(duration); });
            return Result::Handled;
        }
        case Rpc::SetGravity:
            return FloatRpc(reader, [](float v) { Script::RpcSetGravity(v); });
        case Rpc::SetGameSpeed:
            return FloatRpc(reader, [](float v) { Script::RpcSetGameSpeed(v); });
        case Rpc::SetWaveHeight:
            return FloatRpc(reader, [](float v) { Script::RpcSetWaveHeight(v); });
        case Rpc::SetBlurLevel: {
            uint8_t level = 0;
            if (!reader.Read(level)) return Result::Malformed;
            Enqueue([level] { Script::RpcSetBlurLevel(level); });
            return Result::Handled;
        }
        case Rpc::SetCloudsEnabled: {
            // CWorldRPCs::SetCloudsEnabled (151-160): a uchar, enabled when it is exactly 1.
            uint8_t enabled = 0;
            if (!reader.Read(enabled)) return Result::Malformed;
            Enqueue([enabled] { Script::RpcSetCloudsEnabled(enabled == 1); });
            return Result::Handled;
        }
        case Rpc::SetJetpackMaxHeight:
            return FloatRpc(reader, [](float v) { Script::RpcSetJetpackMaxHeight(v); });
        case Rpc::SetInteriorSoundsEnabled: {
            bool enabled = false;
            if (!ReadBit(reader, enabled)) return Result::Malformed;
            Enqueue([enabled] { Script::RpcSetInteriorSoundsEnabled(enabled); });
            return Result::Handled;
        }
        case Rpc::SetRainLevel:
            return FloatRpc(reader, [](float v) { Script::RpcSetRainLevel(v); });
        case Rpc::SetSunSize:
            return FloatRpc(reader, [](float v) { Script::RpcSetSunSize(v); });
        case Rpc::SetSunColor: {
            uint8_t c[6] = {};
            for (uint8_t& value : c)
                if (!reader.Read(value)) return Result::Malformed;
            Enqueue([c] { Script::RpcSetSunColor(c[0], c[1], c[2], c[3], c[4], c[5]); });
            return Result::Handled;
        }
        case Rpc::SetWindVelocity: {
            float x = 0, y = 0, z = 0;
            if (!ReadFiniteFloat(reader, x) || !ReadFiniteFloat(reader, y) || !ReadFiniteFloat(reader, z))
                return Result::Malformed;
            Enqueue([x, y, z] { Script::RpcSetWindVelocity(x, y, z); });
            return Result::Handled;
        }
        case Rpc::SetFarClipDistance:
            return FloatRpc(reader, [](float v) { Script::RpcSetFarClipDistance(v); });
        case Rpc::SetFogDistance:
            return FloatRpc(reader, [](float v) { Script::RpcSetFogDistance(v); });
        case Rpc::SetAircraftMaxHeight:
            return FloatRpc(reader, [](float v) { Script::RpcSetAircraftMaxHeight(v); });
        case Rpc::SetAircraftMaxVelocity:
            return FloatRpc(reader, [](float v) { Script::RpcSetAircraftMaxVelocity(v); });
        case Rpc::SetOcclusionsEnabled: {
            bool enabled = false;
            if (!ReadBit(reader, enabled)) return Result::Malformed;
            Enqueue([enabled] { Script::RpcSetOcclusionsEnabled(enabled); });
            return Result::Handled;
        }
        case Rpc::SetMoonSize: {
            // CWorldRPCs::SetMoonSize (608-617): a four byte int.
            int32_t size = 0;
            if (!reader.Read(size)) return Result::Malformed;
            Enqueue([size] { Script::RpcSetMoonSize(size); });
            return Result::Handled;
        }
        case Rpc::ResetRainLevel: return ResetRpc([] { Script::RpcResetRainLevel(); });
        case Rpc::ResetSunSize: return ResetRpc([] { Script::RpcResetSunSize(); });
        case Rpc::ResetSunColor: return ResetRpc([] { Script::RpcResetSunColor(); });
        case Rpc::ResetWindVelocity: return ResetRpc([] { Script::RpcResetWindVelocity(); });
        case Rpc::ResetFarClipDistance: return ResetRpc([] { Script::RpcResetFarClipDistance(); });
        case Rpc::ResetFogDistance: return ResetRpc([] { Script::RpcResetFogDistance(); });
        case Rpc::ResetMoonSize: return ResetRpc([] { Script::RpcResetMoonSize(); });
        case Rpc::SetWantedLevel: {
            uint8_t level = 0;
            if (!reader.Read(level)) return Result::Malformed;
            Enqueue([level] { Script::RpcSetWantedLevel(level); });
            return Result::Handled;
        }
        case Rpc::SetSkyGradient: {
            uint8_t tr = 0, tg = 0, tb = 0, br = 0, bg = 0, bb = 0;
            if (!reader.Read(tr) || !reader.Read(tg) || !reader.Read(tb) ||
                !reader.Read(br) || !reader.Read(bg) || !reader.Read(bb))
                return Result::Malformed;
            Enqueue([tr, tg, tb, br, bg, bb] { Script::RpcSetSkyGradient(tr, tg, tb, br, bg, bb); });
            return Result::Handled;
        }
        case Rpc::ResetSkyGradient: return ResetRpc([] { Script::RpcResetSkyGradient(); });
        case Rpc::SetHeatHaze: {
            uint8_t intensity = 0, randomShift = 0;
            uint16_t speedMin = 0, speedMax = 0, renderX = 0, renderY = 0;
            int16_t scanX = 0, scanY = 0;
            bool inside = false;
            if (!reader.Read(intensity) || !reader.Read(randomShift) ||
                !ReadRangeBits<10, uint16_t>(reader, speedMin, 0, 1000) ||
                !ReadRangeBits<10, uint16_t>(reader, speedMax, 0, 1000) ||
                !ReadRangeBits<11, int16_t>(reader, scanX, -1000, 1000) ||
                !ReadRangeBits<11, int16_t>(reader, scanY, -1000, 1000) ||
                !ReadRangeBits<10, uint16_t>(reader, renderX, 0, 1000) ||
                !ReadRangeBits<10, uint16_t>(reader, renderY, 0, 1000) ||
                !ReadBit(reader, inside))
                return Result::Malformed;
            Enqueue([=] {
                Script::RpcSetHeatHaze(intensity, randomShift, speedMin, speedMax, scanX, scanY, renderX, renderY,
                                       inside);
            });
            return Result::Handled;
        }
        case Rpc::ResetHeatHaze: return ResetRpc([] { Script::RpcResetHeatHaze(); });
        case Rpc::SetFpsLimit: {
            uint16_t fps = 0;
            if (!reader.Read(fps)) return Result::Malformed;
            Enqueue([fps] { Script::RpcSetFpsLimit(fps); });
            return Result::Handled;
        }
        case Rpc::SetGarageOpen: {
            uint8_t garageId = 0, open = 0;
            if (!reader.Read(garageId) || !reader.Read(open)) return Result::Malformed;
            Enqueue([garageId, open] { Script::RpcSetGarageOpen(garageId, open == 1); });
            return Result::Handled;
        }
        case Rpc::SetGlitchEnabled: {
            uint8_t glitch = 0, enabled = 0;
            if (!reader.Read(glitch) || !reader.Read(enabled)) return Result::Malformed;
            Enqueue([glitch, enabled] { Script::RpcSetGlitchEnabled(glitch, enabled == 1); });
            return Result::Handled;
        }
        case Rpc::SetTrafficLightState: {
            uint8_t state = 0;
            if (!reader.ReadBits(&state, 4)) return Result::Malformed;
            bool forced = false;
            if (reader.GetRemainingBits() >= 1) ReadBit(reader, forced);
            const uint8_t st = state & 0x0Fu;
            Enqueue([st, forced] { Script::RpcSetTrafficLightState(st, forced); });
            return Result::Handled;
        }
        case Rpc::SetWaterColor: {
            uint8_t r = 0, g = 0, b = 0, a = 0;
            if (!reader.Read(r) || !reader.Read(g) || !reader.Read(b) || !reader.Read(a)) return Result::Malformed;
            Enqueue([r, g, b, a] { Script::RpcSetWaterColor(r, g, b, a); });
            return Result::Handled;
        }
        case Rpc::ResetWaterColor: return ResetRpc([] { Script::RpcResetWaterColor(); });
        case Rpc::SetWorldWaterLevel: {
            float level = 0.f;
            bool nonSea = true, sea = true, outside = false;
            if (!ReadFiniteFloat(reader, level) || !ReadBit(reader, nonSea)) return Result::Malformed;
            if (reader.GetRemainingBits() >= 1) ReadBit(reader, sea);
            if (reader.GetRemainingBits() >= 1) ReadBit(reader, outside);
            Enqueue([level, nonSea, sea, outside] {
                Script::RpcSetWorldWaterLevel(level, nonSea, true, sea, outside);
            });
            return Result::Handled;
        }
        case Rpc::ResetWorldWaterLevel: return ResetRpc([] { Script::RpcResetWorldWaterLevel(); });
        case Rpc::RemoveWorldModel: {
            uint16_t model = 0;
            float radius = 0.f, x = 0.f, y = 0.f, z = 0.f;
            uint8_t interiorByte = 0xFF;
            if (!reader.Read(model) || !ReadFiniteFloat(reader, radius) || !ReadFiniteFloat(reader, x) ||
                !ReadFiniteFloat(reader, y) || !ReadFiniteFloat(reader, z) || !reader.Read(interiorByte))
                return Result::Malformed;
            const int8_t interior = static_cast<int8_t>(interiorByte);
            Enqueue([model, radius, x, y, z, interior] {
                Script::RpcRemoveWorldModel(model, radius, x, y, z, interior);
            });
            return Result::Handled;
        }
        case Rpc::RestoreWorldModel: {
            uint16_t model = 0;
            float radius = 0.f, x = 0.f, y = 0.f, z = 0.f;
            uint8_t interiorByte = 0xFF;
            if (!reader.Read(model) || !ReadFiniteFloat(reader, radius) || !ReadFiniteFloat(reader, x) ||
                !ReadFiniteFloat(reader, y) || !ReadFiniteFloat(reader, z) || !reader.Read(interiorByte))
                return Result::Malformed;
            const int8_t interior = static_cast<int8_t>(interiorByte);
            Enqueue([model, radius, x, y, z, interior] {
                Script::RpcRestoreWorldModel(model, radius, x, y, z, interior);
            });
            return Result::Handled;
        }
        case Rpc::RestoreAllWorldModels: return ResetRpc([] { Script::RpcRestoreAllWorldModels(); });
        case Rpc::DestroyAllVehicles: ServerWorld::DestroyAllOfKind(2); return Result::Handled;
        case Rpc::DestroyAllObjects: ServerWorld::DestroyAllOfKind(3); return Result::Handled;
        case Rpc::DestroyAllMarkers: ServerWorld::DestroyAllOfKind(4); return Result::Handled;
        case Rpc::DestroyAllBlips: ServerWorld::DestroyAllOfKind(5); return Result::Handled;
        case Rpc::DestroyAllPickups: ServerWorld::DestroyAllOfKind(6); return Result::Handled;
        case Rpc::DestroyAllRadarAreas: ServerWorld::DestroyAllOfKind(7); return Result::Handled;
        default:
            return Result::NotHandled;
    }
}

// CCameraRPCs.cpp:21-196.
Result HandleCamera(uint8_t func, BitStreamReader& reader) {
    switch (func) {
        case Rpc::SetCameraMatrix: {
            uint8_t context = 0;
            if (!reader.Read(context)) return Result::Malformed;
            g_cameraContext = context;
            float v[6] = {};
            for (float& value : v)
                if (!ReadFloat(reader, value)) return Result::Malformed;
            float roll = 0.f, fov = 70.f;
            // Optional tail: roll and FOV (at least 64 unread bits), or roll alone (at least 32).
            const size_t unread = reader.GetRemainingBits();
            if (unread >= 64) {
                if (!ReadFloat(reader, roll) || !ReadFloat(reader, fov)) return Result::Malformed;
            } else if (unread >= 32) {
                if (!ReadFloat(reader, roll)) return Result::Malformed;
            }
            for (float value : v)
                if (!Finite(value)) return Result::Handled;  // desktop returns, nothing is applied
            if (!Finite(roll) || !Finite(fov)) return Result::Handled;
            if (fov <= 0.f) fov = 70.f;
            else if (fov >= 180.f) fov = 179.f;
            roll = std::remainder(roll, 360.f);
            if (roll < 0.f) roll += 360.f;
            Enqueue([v, roll, fov] { Script::RpcSetCameraMatrix(v[0], v[1], v[2], v[3], v[4], v[5], roll, fov); });
            return Result::Handled;
        }
        case Rpc::SetCameraTarget: {
            uint8_t context = 0;
            uint32_t id = 0;
            if (!reader.Read(context)) return Result::Malformed;
            g_cameraContext = context;
            if (!Entity::ReadElementId(reader, id)) return Result::Malformed;
            Enqueue([id] { Script::RpcSetCameraTarget(id); });
            return Result::Handled;
        }
        case Rpc::SetCameraInterior: {
            uint8_t interior = 0;
            if (!reader.Read(interior)) return Result::Malformed;
            Enqueue([interior] { Script::RpcSetCameraInterior(interior); });
            return Result::Handled;
        }
        case Rpc::FadeCamera: {
            uint8_t fadeIn = 0;
            float seconds = 1.f;
            if (!reader.Read(fadeIn) || !reader.Read(seconds)) return Result::Malformed;
            uint8_t color[3] = {};
            if (!fadeIn) {
                for (uint8_t& value : color)
                    if (!reader.Read(value)) return Result::Malformed;
            }
            // Desktop passes any float to FadeIn/FadeOut. A NaN would stall the fade timer forever.
            if (!Finite(seconds)) return Result::Handled;
            Enqueue([fadeIn, seconds, color] {
                Script::RpcFadeCamera(fadeIn != 0, seconds, color[0], color[1], color[2]);
            });
            return Result::Handled;
        }
        default:
            return Result::NotHandled;
    }
}

// CHud.h eHudComponent, in wire order, with the names CLuaFunctionParseHelpers.cpp:97-114 gives them. A null name
// has no Lua spelling and no Android hook.
constexpr const char* kHudNames[] = {
    "ammo", "weapon", "health", "breath", "armour", "money", "vehicle_name", "area_name", "radar", "clock", "radio",
    "wanted", "crosshair", "all", nullptr /* vital stats */, nullptr /* help text */, "radar_map", "radar_blips",
    "radar_altimeter",
};

Result HandlePlayer(uint8_t func, BitStreamReader& reader) {
    switch (func) {
        case Rpc::SetPlayerMoney: {
            // CPlayerRPCs::SetPlayerMoney (29-42): a long (four bytes), then one bit "instant" when bits remain.
            int32_t money = 0;
            if (!reader.Read(money)) return Result::Malformed;
            bool instant = false;
            if (reader.GetRemainingBits() > 0 && !ReadBit(reader, instant)) return Result::Malformed;
            Enqueue([money, instant] { Script::RpcSetPlayerMoney(money, instant); });
            return Result::Handled;
        }
        case Rpc::ShowPlayerHudComponent: {
            // CPlayerRPCs::ShowPlayerHudComponent (44-55): uchar component, uchar show (!= 0).
            uint8_t component = 0, show = 0;
            if (!reader.Read(component) || !reader.Read(show)) return Result::Malformed;
            constexpr size_t kCount = sizeof(kHudNames) / sizeof(kHudNames[0]);
            if (component >= kCount) return Result::Handled;  // desktop casts the byte to eHudComponent unchecked
            Enqueue([component, show] {
                const char* name = kHudNames[component];
                const bool visible = show != 0;
                if (!name || !Game::SetHudComponentVisible(name, visible))
                    LUA_LOGI("DIAG SHOW_PLAYER_HUD_COMPONENT %u (%s): no HUD hook for this component", component,
                             name ? name : "unnamed");
                // CClientGame::SetHudAreaNameDisabled has no Android state or HUD hook.
                if (component == 7 || component == 13)
                    LUA_LOGI("DIAG SHOW_PLAYER_HUD_COMPONENT: SetHudAreaNameDisabled(%d) is not tracked", visible ? 0 : 1);
            });
            return Result::Handled;
        }
        default:
            return Result::NotHandled;
    }
}

Result HandleAudio(uint8_t func, BitStreamReader& reader) {
    if (func != Rpc::PlaySound) return Result::NotHandled;
    // CAudioRPCs::PlaySound (CAudioRPCs.cpp:20-27): SIntegerSync<uchar, 7>, then PlayFrontEndSound.
    uint8_t sound = 0;
    if (!reader.ReadBits(&sound, 7)) return Result::Malformed;
    Script::AudioPlayFrontEndSound(sound);
    return Result::Handled;
}

Result HandleChat(uint8_t func, BitStreamReader& reader) {
    if (func != Rpc::ShowChat) return Result::NotHandled;
    // CInputRPCs::ShowChat (CInputRPCs.cpp:293-304): uchar show, optional uchar inputBlocked. Without the second
    // byte inputBlocked is show != 1.
    uint8_t show = 0, inputBlocked = 0;
    if (!reader.Read(show)) return Result::Malformed;
    const bool hasInputBlocked = reader.Read(inputBlocked);
    const bool visible = show == 1;
    const bool blocked = hasInputBlocked ? inputBlocked == 1 : show != 1;
    Enqueue([visible, blocked] { Script::RpcSetChatVisible(visible, blocked); });
    return Result::Handled;
}

// A u8 length followed by that many bytes (CInputRPCs.cpp reads these into a new char[len + 1]).
bool ReadLengthString(BitStreamReader& reader, std::string& out) {
    uint8_t length = 0;
    out.clear();
    if (!reader.Read(length)) return false;
    if (static_cast<size_t>(length) * 8 > reader.GetRemainingBits()) return false;
    std::vector<uint8_t> bytes(length);
    if (length && !reader.ReadBits(bytes.data(), static_cast<size_t>(length) * 8)) return false;
    out.assign(bytes.begin(), bytes.end());
    return true;
}

// CInputRPCs.cpp:127-130 and 225-228: 0 down, 1 up, 3 both, anything else down.
const char* HitStateName(int8_t hitState) { return hitState == 0 ? "down" : (hitState == 1 ? "up" : (hitState == 3 ? "both" : "down")); }

// CInputRPCs.cpp:30-295.
Result HandleInput(uint8_t func, BitStreamReader& reader) {
    switch (func) {
        case Rpc::BindKey:
        case Rpc::UnbindKey: {
            // BindKey (CInputRPCs.cpp:30-67) and UnbindKey (CInputRPCs.cpp:114-151): key length, key, state byte
            // (down when == 1). The same bit state is used to find the server function.
            std::string key;
            uint8_t state = 0;
            if (!ReadLengthString(reader, key) || !reader.Read(state)) return Result::Malformed;
            const bool down = state == 1;
            const bool bind = func == Rpc::BindKey;
            Enqueue([key, down, bind] {
                const bool ok = bind ? Script::KeyBindsServerBindFunction(key, down) : Script::KeyBindsServerUnbindFunction(key, down);
                if (bind && ok) {
                    static bool seen = false;
                    if (!seen) {
                        seen = true;
                        LUA_LOGI("DIAG BIND_KEY: hardware keyboards and the on-screen key list fire server key binds; "
                                 "GTA control names (no keyboard mapping) never do");
                    }
                }
            });
            return Result::Handled;
        }
        case Rpc::BindCommand: {
            // CInputRPCs.cpp:69-112: key, hit state char, command, arguments, resource (u8 length strings).
            std::string key, command, arguments, resource;
            uint8_t hitState = 0;
            if (!ReadLengthString(reader, key) || !reader.Read(hitState) || !ReadLengthString(reader, command) ||
                !ReadLengthString(reader, arguments) || !ReadLengthString(reader, resource))
                return Result::Malformed;
            const std::string state = HitStateName(static_cast<int8_t>(hitState));
            Enqueue([=] { Script::KeyBindsServerBindCommand(key, state, command, arguments, resource); });
            return Result::Handled;
        }
        case Rpc::UnbindCommand: {
            // CInputRPCs.cpp:153-198: key, hit state char, command, resource.
            std::string key, command, resource;
            uint8_t hitState = 0;
            if (!ReadLengthString(reader, key) || !reader.Read(hitState) || !ReadLengthString(reader, command) ||
                !ReadLengthString(reader, resource))
                return Result::Malformed;
            const std::string state = HitStateName(static_cast<int8_t>(hitState));
            Enqueue([=] { Script::KeyBindsServerUnbindCommand(key, state, command, resource); });
            return Result::Handled;
        }
        case Rpc::SetControlState: {
            // CInputRPCs.cpp:220-239: control name, state byte (true when == 1); an empty name is ignored.
            std::string control;
            uint8_t state = 0;
            if (!ReadLengthString(reader, control) || !reader.Read(state)) return Result::Malformed;
            if (control.empty()) return Result::Handled;
            // DIAG: CStaticFunctionDefinitions::SetControlState (CStaticFunctionDefinitions.cpp:7470-7520) writes
            // CClientPad analog state or SBindableGTAControl::bState; the phone has no CPad injection (see
            // setPedAnalogControlState in LuaPedApi.cpp).
            LUA_LOGI("DIAG SET_CONTROL_STATE %s=%u: CClientPad::SetAnalogControlState is not hooked", control.c_str(),
                     static_cast<unsigned>(state == 1));
            return Result::Handled;
        }
        case Rpc::ShowCursor: {
            // CInputRPCs.cpp:268-281: show, resource net id (u16), toggle controls. An unknown resource is ignored.
            uint8_t show = 0, toggle = 0;
            uint16_t netId = 0;
            if (!reader.Read(show) || !reader.Read(netId) || !reader.Read(toggle)) return Result::Malformed;
            Enqueue([show, netId, toggle] {
                Script::ResourceLoader::ResourceView resource;
                if (!Script::ResourceLoader::CopyResourceByNet(netId, resource)) return;
                Script::GuiServerShowCursor(resource.name, resource.generation, show == 1, toggle == 1);
            });
            return Result::Handled;
        }
        default:
            return Result::NotHandled;
    }
}

constexpr float kPositionLimit = 100000.f;  // SYNC_POSITION_LIMIT (SyncStructures.h:28)
constexpr float kRadiansToDegrees = 57.295779513082320876798154814105f;

bool WithinLimit(float v) { return v > -kPositionLimit && v < kPositionLimit; }

// SPositionSync(true) (SyncStructures.h:262-275): three floats, each inside +-SYNC_POSITION_LIMIT.
bool ReadFloatPosition(BitStreamReader& reader, float out[3]) {
    return reader.Read(out[0]) && reader.Read(out[1]) && reader.Read(out[2]) && WithinLimit(out[0]) && WithinLimit(out[1]) &&
           WithinLimit(out[2]);
}

// SRotationRadiansSync(true) (SyncStructures.h:496-505): three floats. Rejects NaN/Inf.
bool ReadFloatRotation(BitStreamReader& reader, float out[3]) {
    return ReadFiniteFloat(reader, out[0]) && ReadFiniteFloat(reader, out[1]) && ReadFiniteFloat(reader, out[2]);
}

// SPositionSync with the default 24-bit components (SyncStructures.h:276-291).
bool ReadPackedPosition(BitStreamReader& reader, float out[3]) {
    return Entity::ReadPosition(reader, out[0], out[1], out[2]) && WithinLimit(out[0]) && WithinLimit(out[1]) && WithinLimit(out[2]);
}

bool ReadDouble(BitStreamReader& reader, double& out) {
    uint8_t bytes[8] = {};
    if (!reader.ReadBits(bytes, 64)) return false;
    std::memcpy(&out, bytes, sizeof(out));
    return std::isfinite(out);
}

// CElementIDs::GetElement for the elements this port tracks.
bool ElementExists(uint32_t id) {
    return id != 0 && id != 0xFFFFFFFFu &&
           (id == ServerWorld::LocalPlayer() || ServerWorld::IsKnownPlayer(id) || ServerWorld::ModelOf(id) >= 0 ||
            Script::KnownClientElement(id));
}

bool IsKind(uint32_t id, const char* kind) {
    const char* found = ServerWorld::ElementKindName(id);
    return found && std::strcmp(found, kind) == 0;
}

// CPositionRotationAnimation::FromBitStream (CPositionRotationAnimation.cpp:111-178).
bool ReadMoveAnimation(BitStreamReader& reader, Script::ServerObjectMove& move) {
    bool resume = false;
    if (!ReadBit(reader, resume)) return false;
    uint32_t elapsed = 0, left = 0, duration = 0;
    if (resume) {
        if (!Entity::ReadCompressed(reader, 4, elapsed) || !Entity::ReadCompressed(reader, 4, left)) return false;
        move.elapsedMs = elapsed;
        move.totalMs = elapsed + left < elapsed ? 0xFFFFFFFFu : elapsed + left;
    } else {
        if (!Entity::ReadCompressed(reader, 4, duration)) return false;
        move.elapsedMs = 0;
        move.totalMs = duration;
    }
    if (!ReadPackedPosition(reader, move.sourcePos) || !ReadFloatRotation(reader, move.sourceRot)) return false;
    if (!ReadPackedPosition(reader, move.targetPos) || !ReadBit(reader, move.deltaRotation) || !ReadFloatRotation(reader, move.targetRot))
        return false;
    // NetBitStream::ReadString: a u16 length, then the characters.
    uint16_t length = 0;
    if (!reader.Read(length)) return false;
    if (static_cast<size_t>(length) * 8 > reader.GetRemainingBits()) return false;
    std::vector<uint8_t> name(length);
    if (length && !reader.ReadBits(name.data(), static_cast<size_t>(length) * 8)) return false;
    double period = 0, amplitude = 0, overshoot = 0;
    if (!ReadDouble(reader, period) || !ReadDouble(reader, amplitude) || !ReadDouble(reader, overshoot)) return false;
    // An unknown easing name becomes Linear (CPositionRotationAnimation.cpp:166-171). The curve types are not
    // sampled on Android yet (the stored move stays linear), so only "Linear" is kept linear here.
    const std::string easing(name.begin(), name.end());
    move.linear = easing.empty() || easing == "Linear" || !Script::IsKnownEasingName(easing.c_str());
    return true;
}

void PushElementOrNil(lua_State* L, uint32_t id) {
    if (ElementExists(id)) Script::PushElement(L, id);
    else lua_pushnil(L);
}

// CElementRPCs::AttachElements (CElementRPCs.cpp:319-360).
Result HandleAttach(uint32_t id, BitStreamReader& reader) {
    uint32_t parent = 0;
    float position[3] = {}, rotation[3] = {};
    if (!Entity::ReadElementId(reader, parent) || !ReadFloatRotation(reader, position) ||
        !ReadFloatRotation(reader, rotation))
        return Result::Malformed;
    Enqueue([id, parent, position, rotation]() mutable {
        if (!ElementExists(parent)) return;
        for (float& value : rotation) value *= kRadiansToDegrees;
        lua_State* L = Script::ActiveVmState();
        if (L) {
            Script::PushElement(L, parent);
            for (float value : position) lua_pushnumber(L, value);
            for (float value : rotation) lua_pushnumber(L, value);
            if (!Script::ActiveVmFireElementEvent(L, "onClientElementAttach", id, 7)) return;
        }
        ServerWorld::NoteAttachedOffsets(id, position[0], position[1], position[2], rotation[0], rotation[1], rotation[2]);
        ServerWorld::NoteAttachTo(id, parent);
        // DIAG: CClientEntity::InternalAttachTo (CClientEntity.cpp:667-727), the CPhysical::AttachEntityToEntity
        // call, is not hooked. The link is visible to Lua only (getElementAttachedTo, getAttachedElements).
        LUA_LOGI("DIAG ATTACH_ELEMENTS %u -> %u: CPhysical::AttachEntityToEntity is not hooked", id, parent);
    });
    return Result::Handled;
}

// CElementRPCs::DetachElements (CElementRPCs.cpp:362-422). Desktop never reads the attached-to id: it passes an
// uninitialised ElementID to GetElement, so the event's first argument is whatever the stack held. Android passes nil.
Result HandleDetach(uint32_t id, BitStreamReader& reader) {
    uint8_t context = 0;
    if (!reader.Read(context)) return Result::Malformed;
    float position[3] = {}, rotation[3] = {};
    for (float& value : position) reader.Read(value);
    for (float& value : rotation) reader.Read(value);
    for (float value : position)
        if (!Finite(value)) return Result::Malformed;
    for (float value : rotation)
        if (!Finite(value)) return Result::Malformed;
    Enqueue([id, context, position, rotation] {
        lua_State* L = Script::ActiveVmState();
        if (L) {
            lua_pushnil(L);
            for (float value : position) lua_pushnumber(L, value);
            for (float value : rotation) lua_pushnumber(L, value);
            if (!Script::ActiveVmFireElementEvent(L, "onClientElementDetach", id, 7)) return;
        }
        ServerWorld::SetSyncTimeContext(id, context);
        ServerWorld::NoteAttachTo(id, 0xFFFFFFFFu);
        const bool hasPosition = position[0] != 0.f || position[1] != 0.f || position[2] != 0.f;
        if (hasPosition && std::isfinite(position[0]) && std::isfinite(position[1]) && std::isfinite(position[2])) {
            ServerWorld::NoteElementPosition(id, position[0], position[1], position[2]);
            const char* kind = ServerWorld::ElementKindName(id);
            if (kind && (std::strcmp(kind, "ped") == 0 || std::strcmp(kind, "player") == 0))
                Game::RpcSetPosition(id, position[0], position[1], position[2]);
            else if (kind && std::strcmp(kind, "vehicle") == 0)
                Game::RpcSetVehiclePosition(id, position[0], position[1], position[2]);
        }
        const bool hasRotation = rotation[0] != 0.f || rotation[1] != 0.f || rotation[2] != 0.f;
        if (hasRotation)
            LUA_LOGI("DIAG DETACH_ELEMENTS %u: SetRotationDegrees is not hooked for this element", id);
        LUA_LOGI("DIAG DETACH_ELEMENTS %u: CClientEntity::InternalAttachTo(NULL) is not hooked", id);
    });
    return Result::Handled;
}

// CElementRPCs::SetElementAttachedOffsets (CElementRPCs.cpp:648-656). The radians go in as degrees, the unit
// ServerWorld keeps (see NoteAttachedOffsets).
Result HandleAttachedOffsets(uint32_t id, BitStreamReader& reader) {
    float position[3] = {}, rotation[3] = {};
    if (!ReadFloatPosition(reader, position) || !ReadFloatRotation(reader, rotation)) return Result::Malformed;
    Enqueue([id, position, rotation] {
        ServerWorld::NoteAttachedOffsets(id, position[0], position[1], position[2], rotation[0] * kRadiansToDegrees,
                                         rotation[1] * kRadiansToDegrees, rotation[2] * kRadiansToDegrees);
    });
    return Result::Handled;
}

// CVehicleRPCs::FixVehicle (CVehicleRPCs.cpp:64-79) and BlowVehicle (81-104). Both run on the net thread, like the
// vehicle health and position RPCs, so they keep their order against them.
Result HandleVehicleState(uint8_t func, uint32_t id, BitStreamReader& reader) {
    uint8_t context = 0;
    if (!reader.Read(context)) return Result::Malformed;
    bool withExplosion = true;
    if (func == Rpc::BlowVehicle && !ReadBit(reader, withExplosion)) return Result::Malformed;
    if (!IsKind(id, "vehicle")) return Result::Handled;
    if (func == Rpc::FixVehicle) {
        // CClientVehicle::Fix (CClientVehicle.cpp:911-1006): SetHealth(1000) is the only step with a hook.
        Game::RpcSetVehicleHealth(id, 1000.f);
        LUA_LOGI("DIAG FIX_VEHICLE %u: CVehicle::Fix, door, panel, light and wheel resets are not hooked", id);
    } else {
        // CClientVehicle::Blow (CClientVehicle.cpp:1008-): m_fHealth = 0 and SetHealth(0).
        Game::RpcSetVehicleHealth(id, 0.f);
        LUA_LOGI("DIAG BLOW_VEHICLE %u (explosion=%d): CVehicle::BlowUp and the burst wheels are not hooked", id,
                 withExplosion ? 1 : 0);
    }
    ServerWorld::SetSyncTimeContext(id, context);
    return Result::Handled;
}

// CObjectRPCs.cpp:30-109. Each handler acts on the object only.
Result HandleObject(uint8_t func, uint32_t id, BitStreamReader& reader) {
    if (!IsKind(id, "object")) return Result::Handled;
    switch (func) {
        case Rpc::SetObjectRotation: {
            float rotation[3] = {};
            if (!ReadFloatRotation(reader, rotation)) return Result::Malformed;
            Enqueue([id, rotation] { Script::ServerSetObjectRotation(id, rotation[0], rotation[1], rotation[2]); });
            return Result::Handled;
        }
        case Rpc::MoveObject: {
            Script::ServerObjectMove move;
            if (!ReadMoveAnimation(reader, move)) return Result::Malformed;
            Enqueue([id, move] { Script::ServerMoveObject(id, move); });
            return Result::Handled;
        }
        case Rpc::StopObject: {
            float position[3] = {}, rotation[3] = {};
            if (!ReadFloatRotation(reader, position) || !ReadFloatRotation(reader, rotation)) return Result::Malformed;
            Enqueue([id, position, rotation] { Script::ServerStopObject(id, position, rotation); });
            return Result::Handled;
        }
        case Rpc::SetObjectScale: {
            // The reads are unchecked on desktop: y and z fall back to x when the packet ends early.
            float scale[3] = {};
            if (!ReadFiniteFloat(reader, scale[0])) return Result::Malformed;
            scale[1] = scale[2] = scale[0];
            float y = scale[0], z = scale[0];
            if (reader.Read(y)) {
                if (!Finite(y)) return Result::Malformed;
                scale[1] = y;
            }
            if (reader.Read(z)) {
                if (!Finite(z)) return Result::Malformed;
                scale[2] = z;
            }
            Enqueue([id, scale] { Script::ServerSetObjectScale(id, scale[0], scale[1], scale[2]); });
            return Result::Handled;
        }
        default:
            return Result::NotHandled;
    }
}

// CElementRPCs, CPlayerRPCs, CTeamRPCs, CBlipRPCs, CRadarRPCs, CMarkerRPCs, CPickupRPCs element handlers.
Result HandleEntityMisc(uint8_t func, uint32_t id, BitStreamReader& reader) {
    switch (func) {
        case Rpc::SetElementParent: {
            uint32_t parentId = 0;
            if (!Entity::ReadElementId(reader, parentId)) return Result::Malformed;
            Enqueue([id, parentId] { Script::RpcSetElementParent(id, parentId); });
            return Result::Handled;
        }
        case Rpc::SetElementName: {
            std::string name;
            if (!ReadU16String(reader, name)) return Result::Malformed;
            Enqueue([id, name] { Script::RpcSetElementName(id, name); });
            return Result::Handled;
        }
        case Rpc::SetElementDoubleSided: {
            bool ds = false;
            if (!ReadBit(reader, ds)) return Result::Malformed;
            Enqueue([id, ds] { Script::RpcSetElementDoubleSided(id, ds); });
            return Result::Handled;
        }
        case Rpc::SetPlayerNametagText: {
            std::string text;
            if (!ReadU16String(reader, text)) return Result::Malformed;
            const bool clear = text.empty();
            Enqueue([id, text, clear] { Script::RpcSetPlayerNametagText(id, text, clear); });
            return Result::Handled;
        }
        case Rpc::SetPlayerNametagColor: {
            uint8_t r = 0, g = 0, b = 0;
            if (!reader.Read(r) || !reader.Read(g) || !reader.Read(b)) return Result::Malformed;
            Enqueue([id, r, g, b] { Script::RpcSetPlayerNametagColor(id, r, g, b); });
            return Result::Handled;
        }
        case Rpc::RemovePlayerNametagColor:
            Enqueue([id] { Script::RpcRemovePlayerNametagColor(id); });
            return Result::Handled;
        case Rpc::SetPlayerNametagShowing: {
            uint8_t showing = 0;
            if (!reader.Read(showing)) return Result::Malformed;
            Enqueue([id, showing] { Script::RpcSetPlayerNametagShowing(id, showing == 1); });
            return Result::Handled;
        }
        case Rpc::SetPlayerTeam: {
            uint32_t teamId = 0;
            if (!Entity::ReadElementId(reader, teamId)) return Result::Malformed;
            ServerWorld::NotePlayerTeam(id, teamId);
            return Result::Handled;
        }
        case Rpc::SetTeamName: {
            std::string name;
            if (!ReadU16String(reader, name)) return Result::Malformed;
            ServerWorld::UpdateTeam(id, &name, nullptr, nullptr);
            return Result::Handled;
        }
        case Rpc::SetTeamColor: {
            uint8_t rgb[3] = {};
            if (!reader.Read(rgb[0]) || !reader.Read(rgb[1]) || !reader.Read(rgb[2])) return Result::Malformed;
            ServerWorld::UpdateTeam(id, nullptr, rgb, nullptr);
            return Result::Handled;
        }
        case Rpc::SetTeamFriendlyFire: {
            bool ff = false;
            if (!ReadBit(reader, ff)) return Result::Malformed;
            ServerWorld::UpdateTeam(id, nullptr, nullptr, &ff);
            return Result::Handled;
        }
        case Rpc::SetBlipIcon: {
            uint8_t raw = 0;
            if (!reader.ReadBits(&raw, 6)) return Result::Malformed;
            const int icon = raw & 0x3Fu;
            if (icon <= 63) ServerWorld::UpdateBlip(id, &icon, nullptr, nullptr, nullptr, nullptr);
            return Result::Handled;
        }
        case Rpc::SetBlipSize: {
            uint8_t raw = 0;
            if (!reader.ReadBits(&raw, 5)) return Result::Malformed;
            const int sz = std::min<int>(raw & 0x1Fu, 25);
            ServerWorld::UpdateBlip(id, nullptr, &sz, nullptr, nullptr, nullptr);
            return Result::Handled;
        }
        case Rpc::SetBlipColor: {
            uint8_t rgba[4] = {};
            if (!reader.ReadBits(rgba, 32)) return Result::Malformed;
            ServerWorld::UpdateBlip(id, nullptr, nullptr, rgba, nullptr, nullptr);
            return Result::Handled;
        }
        case Rpc::SetBlipOrdering: {
            uint32_t raw = 0;
            if (!Entity::ReadCompressed(reader, 2, raw)) return Result::Malformed;
            const int16_t ord = static_cast<int16_t>(raw);
            ServerWorld::UpdateBlip(id, nullptr, nullptr, nullptr, &ord, nullptr);
            return Result::Handled;
        }
        case Rpc::SetBlipVisibleDistance: {
            uint8_t bytes[2] = {};
            if (!reader.ReadBits(bytes, 14)) return Result::Malformed;
            const uint16_t dist = static_cast<uint16_t>((bytes[0] | (static_cast<uint16_t>(bytes[1]) << 8)) & 0x3FFFu);
            ServerWorld::UpdateBlip(id, nullptr, nullptr, nullptr, nullptr, &dist);
            return Result::Handled;
        }
        case Rpc::SetRadarAreaSize: {
            float sz[2] = {};
            if (!ReadFiniteFloat(reader, sz[0]) || !ReadFiniteFloat(reader, sz[1])) return Result::Malformed;
            ServerWorld::UpdateRadarArea(id, sz, nullptr, nullptr);
            return Result::Handled;
        }
        case Rpc::SetRadarAreaColor: {
            uint8_t rgba[4] = {};
            if (!reader.Read(rgba[0]) || !reader.Read(rgba[1]) || !reader.Read(rgba[2]) || !reader.Read(rgba[3]))
                return Result::Malformed;
            ServerWorld::UpdateRadarArea(id, nullptr, rgba, nullptr);
            return Result::Handled;
        }
        case Rpc::SetRadarAreaFlashing: {
            bool flashing = false;
            if (!ReadBit(reader, flashing)) return Result::Malformed;
            ServerWorld::UpdateRadarArea(id, nullptr, nullptr, &flashing);
            return Result::Handled;
        }
        case Rpc::SetMarkerType: {
            uint8_t type = 0;
            if (!reader.Read(type)) return Result::Malformed;
            const int t = type;
            ServerWorld::UpdateMarker(id, &t, nullptr, nullptr, nullptr, nullptr, nullptr);
            return Result::Handled;
        }
        case Rpc::SetMarkerColor: {
            // Desktop CMarkerRPCs.cpp:45 reads B, G, R, A.
            uint8_t b = 0, g = 0, r = 0, a = 0;
            if (!reader.Read(b) || !reader.Read(g) || !reader.Read(r) || !reader.Read(a)) return Result::Malformed;
            const uint8_t rgba[4] = {r, g, b, a};
            ServerWorld::UpdateMarker(id, nullptr, rgba, nullptr, nullptr, nullptr, nullptr);
            return Result::Handled;
        }
        case Rpc::SetMarkerSize: {
            float sz = 0.f;
            if (!ReadFiniteFloat(reader, sz)) return Result::Malformed;
            ServerWorld::UpdateMarker(id, nullptr, nullptr, &sz, nullptr, nullptr, nullptr);
            return Result::Handled;
        }
        case Rpc::SetMarkerTarget: {
            uint8_t mode = 0;
            if (!reader.Read(mode)) return Result::Malformed;
            if (mode == 0) {
                const bool has = false;
                const int icon = 0;
                ServerWorld::UpdateMarker(id, nullptr, nullptr, nullptr, &icon, &has, nullptr);
            } else if (mode == 1) {
                float pos[3] = {};
                if (!ReadFiniteFloat(reader, pos[0]) || !ReadFiniteFloat(reader, pos[1]) ||
                    !ReadFiniteFloat(reader, pos[2]))
                    return Result::Malformed;
                const bool has = true;
                const int icon = 1;
                ServerWorld::UpdateMarker(id, nullptr, nullptr, nullptr, &icon, &has, pos);
            }
            return Result::Handled;
        }
        case Rpc::SetMarkerIcon: {
            uint8_t icon = 0;
            if (!reader.Read(icon)) return Result::Malformed;
            const int ic = icon;
            ServerWorld::UpdateMarker(id, nullptr, nullptr, nullptr, &ic, nullptr, nullptr);
            return Result::Handled;
        }
        case Rpc::SetPickupType: {
            uint8_t type = 0;
            if (!reader.Read(type)) return Result::Malformed;
            float amount = 0.f;
            uint8_t weapon = 0;
            uint16_t ammo = 0, model = 0;
            if (type == 0 || type == 1) {
                if (!ReadFiniteFloat(reader, amount)) return Result::Malformed;
                model = (type == 0) ? 1240 : 1242;
            } else if (type == 2) {
                if (!reader.Read(weapon) || !reader.Read(ammo)) return Result::Malformed;
            } else if (type == 3) {
                if (!reader.Read(model)) return Result::Malformed;
            }
            ServerWorld::UpdatePickupType(id, type, amount, weapon, ammo, model);
            return Result::Handled;
        }
        case Rpc::SetPickupVisible: {
            uint8_t visible = 0;
            if (!reader.Read(visible)) return Result::Malformed;
            ServerWorld::NotePickupVisible(id, visible != 0);
            return Result::Handled;
        }
        default:
            return Result::NotHandled;
    }
}

bool ReadU8String(BitStreamReader& reader, std::string& out) {
    uint8_t length = 0;
    out.clear();
    if (!reader.Read(length)) return false;
    if (static_cast<size_t>(length) * 8 > reader.GetRemainingBits()) return false;
    std::vector<uint8_t> bytes(length);
    if (length && !reader.ReadBits(bytes.data(), static_cast<size_t>(length) * 8)) return false;
    out.assign(bytes.begin(), bytes.end());
    return true;
}

// Desktop CWorldRPCs::SetWeaponProperty / COutputRPCs / CPlayerRPCs / CWaterRPCs / CModelRPCs / CInputRPCs.
Result HandleGlobalExtended(uint8_t func, BitStreamReader& reader) {
    switch (func) {
        case Rpc::ForcePlayerMap: {
            uint8_t visible = 0;
            if (!reader.Read(visible)) return Result::Malformed;
            const bool forced = visible == 1;
            Enqueue([forced] { Script::RpcForcePlayerMap(forced); });
            return Result::Handled;
        }
        case Rpc::ForceReconnect: {
            std::string host, password;
            uint16_t port = 0;
            if (!ReadU8String(reader, host) || !reader.Read(port) || !ReadU8String(reader, password))
                return Result::Malformed;
            (void)password;
            LUA_LOGI("DIAG FORCE_RECONNECT: target=%s:%u (auto-reconnect not hooked)", host.c_str(),
                     static_cast<unsigned>(port));
            return Result::Handled;
        }
        case Rpc::ResetMapInfo: {
            Enqueue([] { Script::RpcResetMapInfo(); });
            return Result::Handled;
        }
        case Rpc::ToggleDebugger: {
            uint8_t enable = 0;
            if (!reader.Read(enable)) return Result::Malformed;
            LUA_LOGI("DIAG TOGGLE_DEBUGGER: enable=%u", static_cast<unsigned>(enable != 0));
            return Result::Handled;
        }
        case Rpc::SetAllElementWaterLevel: {
            float level = 0.f;
            if (!ReadFiniteFloat(reader, level)) return Result::Malformed;
            Enqueue([level] { Script::RpcSetAllElementWaterLevel(level); });
            return Result::Handled;
        }
        case Rpc::SetWeaponProperty: {
            uint8_t weapon = 0, prop = 0, skill = 0;
            if (!reader.Read(weapon) || !reader.Read(prop) || !reader.Read(skill)) return Result::Malformed;
            const char* propName = nullptr;
            switch (prop) {
                case 1: propName = "weapon_range"; break;
                case 2: propName = "target_range"; break;
                case 3: propName = "accuracy"; break;
                case 5: propName = "life_span"; break;
                case 6: propName = "firing_speed"; break;
                case 9: propName = "move_speed"; break;
                case 21: propName = "anim_loop_start"; break;
                case 22: propName = "anim_loop_stop"; break;
                case 23: propName = "anim_loop_bullet_fire"; break;
                case 24: propName = "anim2_loop_start"; break;
                case 25: propName = "anim2_loop_stop"; break;
                case 26: propName = "anim2_loop_bullet_fire"; break;
                case 27: propName = "anim_breakout_time"; break;
                default: break;
            }
            if (propName) {
                float v = 0.f;
                if (!ReadFiniteFloat(reader, v)) return Result::Malformed;
                Enqueue([weapon, skill, propName, v] { Script::RpcSetWeaponPropertyScalar(weapon, skill, propName, v); });
                return Result::Handled;
            }
            if (prop == 4 || prop == 8 || prop == 11) {
                uint16_t raw = 0;
                if (!reader.Read(raw)) return Result::Malformed;
                const int16_t v = static_cast<int16_t>(raw);
                const char* name = prop == 4 ? "damage" : (prop == 8 ? "maximum_clip_ammo" : "anim_group");
                Enqueue([weapon, skill, name, v] { Script::RpcSetWeaponPropertyScalar(weapon, skill, name, v); });
                return Result::Handled;
            }
            if (prop == 10) {
                int32_t v = 0;
                if (!reader.Read(v)) return Result::Malformed;
                Enqueue([weapon, skill, v] { Script::RpcSetWeaponPropertyScalar(weapon, skill, "flags", v); });
                return Result::Handled;
            }
            if (prop >= 33 && prop <= 52 && prop != 39 && prop != 40) {
                bool enable = false;
                if (!ReadBit(reader, enable)) return Result::Malformed;
                const int flagBit = 1 << (prop - 33);
                Enqueue([weapon, skill, flagBit, enable] { Script::RpcSetWeaponPropertyFlag(weapon, skill, flagBit, enable); });
                return Result::Handled;
            }
            return Result::Handled;
        }
        case Rpc::TakePlayerScreenShot: {
            uint16_t sizeX = 0, sizeY = 0, maxPacket = 0, resNetId = 0;
            std::string tag;
            uint8_t quality = 0;
            uint32_t maxBw = 0, serverTime = 0;
            if (!reader.Read(sizeX) || !reader.Read(sizeY) || !ReadU16String(reader, tag) || !reader.Read(quality) ||
                !reader.Read(maxBw) || !reader.Read(maxPacket) || !reader.Read(resNetId) || !reader.Read(serverTime))
                return Result::Malformed;
            LUA_LOGI("DIAG TAKE_PLAYER_SCREEN_SHOT: %ux%u tag=%s quality=%u (capture not hooked)",
                     static_cast<unsigned>(sizeX), static_cast<unsigned>(sizeY), tag.c_str(),
                     static_cast<unsigned>(quality));
            return Result::Handled;
        }
        case Rpc::SetSyncIntervals: {
            int32_t intervals[10] = {};
            for (int i = 0; i < 10; ++i) {
                if (!reader.Read(intervals[i])) return Result::Malformed;
            }
            LUA_LOGI("DIAG SET_SYNC_INTERVALS: pure=%d light=%d ped=%d unocc=%d", intervals[0], intervals[1],
                     intervals[3], intervals[4]);
            return Result::Handled;
        }
        case Rpc::SetJetpackWeaponEnabled: {
            uint8_t weaponId = 0;
            bool enabled = false;
            if (!reader.Read(weaponId) || !ReadBit(reader, enabled)) return Result::Malformed;
            Enqueue([weaponId, enabled] { Script::RpcSetJetpackWeaponEnabled(weaponId, enabled); });
            return Result::Handled;
        }
        case Rpc::SetDiscordJoinParameters: {
            LUA_LOGI("DIAG SET_DISCORD_JOIN_PARAMETERS: unused on desktop client");
            return Result::Handled;
        }
        case Rpc::SetTransferBoxVisibility: {
            bool visible = false;
            if (!ReadBit(reader, visible)) return Result::Malformed;
            Enqueue([visible] { Script::RpcSetTransferBoxVisibility(visible); });
            return Result::Handled;
        }
        case Rpc::SetWorldSpecialProperty: {
            uint8_t property = 0;
            bool enabled = false;
            if (!reader.Read(property) || !ReadBit(reader, enabled)) return Result::Malformed;
            Enqueue([property, enabled] { Script::RpcSetWorldSpecialProperty(property, enabled); });
            return Result::Handled;
        }
        case Rpc::SetPlayerScriptDebugLevel: {
            uint8_t level = 0;
            if (!reader.Read(level)) return Result::Malformed;
            Enqueue([level] { Script::RpcSetPlayerScriptDebugLevel(level); });
            return Result::Handled;
        }
        case Rpc::ResetWorldProperties: {
            bool rSpec = false, rWorld = false, rWeather = false, rLods = false, rSounds = false;
            if (!ReadBit(reader, rSpec) || !ReadBit(reader, rWorld) || !ReadBit(reader, rWeather) ||
                !ReadBit(reader, rLods) || !ReadBit(reader, rSounds))
                return Result::Malformed;
            Enqueue([rSpec, rWorld, rWeather, rLods, rSounds] {
                Script::RpcResetWorldProperties(rSpec, rWorld, rWeather, rLods, rSounds);
            });
            return Result::Handled;
        }
        case Rpc::SetWorldSeabedOuterBoundary: {
            float boundary = 0.f;
            if (!ReadFiniteFloat(reader, boundary)) return Result::Malformed;
            Enqueue([boundary] { Script::RpcSetWorldSeabedOuterBoundary(boundary); });
            return Result::Handled;
        }
        case Rpc::AllocateServerModel: {
            uint16_t logicalId = 0, parentId = 0;
            uint8_t rawType = 0;
            if (!reader.Read(logicalId) || !reader.Read(parentId) || !reader.Read(rawType)) return Result::Malformed;
            std::string name;
            if (reader.GetRemainingBits() >= 16) ReadU16String(reader, name);
            LUA_LOGI("DIAG ALLOCATE_SERVER_MODEL: logical=%u parent=%u type=%u name=%s",
                     static_cast<unsigned>(logicalId), static_cast<unsigned>(parentId),
                     static_cast<unsigned>(rawType), name.c_str());
            return Result::Handled;
        }
        case Rpc::FreeServerModel: {
            uint16_t logicalId = 0;
            if (!reader.Read(logicalId)) return Result::Malformed;
            LUA_LOGI("DIAG FREE_SERVER_MODEL: logical=%u", static_cast<unsigned>(logicalId));
            return Result::Handled;
        }
        case Rpc::ToggleDevTools: {
            uint8_t enable = 0;
            if (!reader.Read(enable)) return Result::Malformed;
            LUA_LOGI("DIAG TOGGLE_DEVTOOLS: enable=%u", static_cast<unsigned>(enable != 0));
            return Result::Handled;
        }
        default:
            return Result::NotHandled;
    }
}

// Desktop CPedRPCs, CVehicleRPCs, CHandlingRPCs, CWaterRPCs, CObjectRPCs, CMarkerRPCs, and custom weapon/element RPCs.
Result HandleElementExtended(uint8_t func, uint32_t id, BitStreamReader& reader) {
    switch (func) {
        // ---- Ped RPCs (CPedRPCs.cpp) ----
        case Rpc::GivePedJetpack: {
            Enqueue([id] { Script::RpcSetPedJetpack(id, true); });
            return Result::Handled;
        }
        case Rpc::RemovePedJetpack: {
            Enqueue([id] { Script::RpcSetPedJetpack(id, false); });
            return Result::Handled;
        }
        case Rpc::RemovePedClothes: {
            uint8_t type = 0;
            if (!reader.Read(type)) return Result::Malformed;
            Enqueue([id, type] { Script::RpcRemovePedClothes(id, type); });
            return Result::Handled;
        }
        case Rpc::SetPedGravity: {
            float gravity = 0.f;
            if (!ReadFiniteFloat(reader, gravity)) return Result::Malformed;
            Enqueue([id, gravity] { Script::RpcSetPedGravity(id, gravity); });
            return Result::Handled;
        }
        case Rpc::SetPedChoking: {
            bool choking = false;
            if (!ReadBit(reader, choking)) return Result::Malformed;
            Enqueue([id, choking] { Script::RpcSetPedChoking(id, choking); });
            return Result::Handled;
        }
        case Rpc::SetPedFightingStyle: {
            uint8_t style = 0;
            if (!reader.Read(style)) return Result::Malformed;
            Enqueue([id, style] { Script::RpcSetPedFightingStyle(id, style); });
            return Result::Handled;
        }
        case Rpc::SetPedMoveAnim: {
            uint32_t anim = 0;
            if (!Entity::ReadCompressed(reader, 4, anim)) return Result::Malformed;
            Enqueue([id, anim] { Script::RpcSetPedMoveAnim(id, anim); });
            return Result::Handled;
        }
        case Rpc::SetPedOnFire: {
            bool onFire = false;
            if (!ReadBit(reader, onFire)) return Result::Malformed;
            Enqueue([id, onFire] { Script::RpcSetPedOnFire(id, onFire); });
            return Result::Handled;
        }
        case Rpc::SetPedHeadless: {
            bool headless = false;
            if (!ReadBit(reader, headless)) return Result::Malformed;
            Enqueue([id, headless] { Script::RpcSetPedHeadless(id, headless); });
            return Result::Handled;
        }
        case Rpc::SetPedFrozen: {
            bool frozen = false;
            if (!ReadBit(reader, frozen)) return Result::Malformed;
            Enqueue([id, frozen] { Script::RpcSetPedFrozen(id, frozen); });
            return Result::Handled;
        }
        case Rpc::ReloadPedWeapon: {
            Enqueue([id] { Script::RpcReloadPedWeapon(id); });
            return Result::Handled;
        }

        // ---- Vehicle RPCs (CVehicleRPCs.cpp) ----
        case Rpc::SetVehicleDoorsUndamageable: {
            uint8_t v = 0;
            if (!reader.Read(v)) return Result::Malformed;
            Script::VehicleRpcSetDoorsUndamageable(id, v != 0);
            return Result::Handled;
        }
        case Rpc::SetVehicleSireneOn: {
            uint8_t v = 0;
            if (!reader.Read(v)) return Result::Malformed;
            Script::VehicleRpcSetSirensOn(id, v != 0);
            return Result::Handled;
        }
        case Rpc::SetVehicleLandingGearDown: {
            uint8_t v = 0;
            if (!reader.Read(v)) return Result::Malformed;
            Script::VehicleRpcSetLandingGearDown(id, v != 0);
            return Result::Handled;
        }
        case Rpc::SetHelicopterRotorSpeed: {
            uint8_t raw = 0;
            if (!reader.Read(raw)) return Result::Malformed;
            const float speed = static_cast<float>(raw) / 255.f * 0.22f;
            Script::VehicleRpcSetHeliRotorSpeed(id, speed);
            return Result::Handled;
        }
        case Rpc::SetVehicleDamageState: {
            uint8_t obj = 0, part = 0, state = 0;
            if (!reader.Read(obj) || !reader.Read(part) || !reader.Read(state)) return Result::Malformed;
            bool dummy = false;
            if (obj == 0 && reader.GetRemainingBits() >= 1) ReadBit(reader, dummy);
            if (obj == 3) {
                if (reader.GetRemainingBits() >= 1) ReadBit(reader, dummy);
                if (reader.GetRemainingBits() >= 1) ReadBit(reader, dummy);
            }
            Script::VehicleRpcSetDamageState(id, obj, part, state);
            return Result::Handled;
        }
        case Rpc::SetVehicleOverrideLights: {
            uint8_t lights = 0;
            if (!reader.Read(lights)) return Result::Malformed;
            Script::VehicleRpcSetOverrideLights(id, lights);
            return Result::Handled;
        }
        case Rpc::SetVehicleDirtLevel: {
            float dirt = 0.f;
            if (!ReadFiniteFloat(reader, dirt)) return Result::Malformed;
            Script::VehicleRpcSetDirtLevel(id, dirt);
            return Result::Handled;
        }
        case Rpc::SetVehicleDamageProof: {
            uint8_t v = 0;
            if (!reader.Read(v)) return Result::Malformed;
            Script::VehicleRpcSetDamageProof(id, v != 0);
            return Result::Handled;
        }
        case Rpc::SetVehiclePaintjob: {
            uint8_t pj = 0;
            if (!reader.Read(pj)) return Result::Malformed;
            Script::VehicleRpcSetPaintjob(id, pj);
            return Result::Handled;
        }
        case Rpc::SetVehicleFuelTankExplodable: {
            bool expl = false;
            if (!ReadBit(reader, expl)) return Result::Malformed;
            Script::VehicleRpcSetFuelTankExplodable(id, expl);
            return Result::Handled;
        }
        case Rpc::SetVehicleWheelStates: {
            uint8_t wheels[4] = {};
            if (!reader.ReadBits(wheels, 32)) return Result::Malformed;
            Script::VehicleRpcSetWheelStates(id, wheels);
            return Result::Handled;
        }
        case Rpc::SetTrainDerailed: {
            bool derailed = false;
            if (!ReadBit(reader, derailed)) return Result::Malformed;
            Script::VehicleRpcSetTrainDerailed(id, derailed);
            return Result::Handled;
        }
        case Rpc::SetTrainDerailable: {
            bool derailable = false;
            if (!ReadBit(reader, derailable)) return Result::Malformed;
            Script::VehicleRpcSetTrainDerailable(id, derailable);
            return Result::Handled;
        }
        case Rpc::SetTrainDirection: {
            bool dir = false;
            if (!ReadBit(reader, dir)) return Result::Malformed;
            Script::VehicleRpcSetTrainDirection(id, dir);
            return Result::Handled;
        }
        case Rpc::SetTrainSpeed: {
            float speed = 0.f;
            if (!ReadFiniteFloat(reader, speed)) return Result::Malformed;
            Script::VehicleRpcSetTrainSpeed(id, speed);
            return Result::Handled;
        }
        case Rpc::SetTaxiLightOn: {
            uint8_t on = 0;
            if (!reader.Read(on)) return Result::Malformed;
            Script::VehicleRpcSetTaxiLightOn(id, on != 0);
            return Result::Handled;
        }
        case Rpc::SetVehicleHeadlightColor: {
            uint8_t rgb[3] = {};
            if (!reader.ReadBits(rgb, 24)) return Result::Malformed;
            Script::VehicleRpcSetHeadlightColor(id, rgb[0], rgb[1], rgb[2]);
            return Result::Handled;
        }
        case Rpc::SetVehicleDoorOpenRatio: {
            uint8_t doorRaw = 0;
            bool uncompressed = false;
            if (!reader.ReadBits(&doorRaw, 3) || !ReadBit(reader, uncompressed)) return Result::Malformed;
            float ratio = 0.f;
            if (!uncompressed) {
                bool notZero = false;
                if (!ReadBit(reader, notZero)) return Result::Malformed;
                ratio = notZero ? 1.f : 0.f;
            } else {
                uint8_t rbytes[2] = {};
                if (!reader.ReadBits(rbytes, 10)) return Result::Malformed;
                const uint16_t raw10 = static_cast<uint16_t>((rbytes[0] | (static_cast<uint16_t>(rbytes[1]) << 8)) & 0x3FFu);
                ratio = static_cast<float>(raw10) / 1023.f;
            }
            uint32_t timeMs = 0;
            if (!Entity::ReadCompressed(reader, 4, timeMs)) return Result::Malformed;
            (void)timeMs;
            Script::VehicleRpcSetDoorOpenRatio(id, doorRaw & 0x07u, ratio);
            return Result::Handled;
        }
        case Rpc::SetVehicleTurretPosition: {
            float h = 0.f, v = 0.f;
            if (!ReadFiniteFloat(reader, h) || !ReadFiniteFloat(reader, v)) return Result::Malformed;
            Script::VehicleRpcSetTurretPosition(id, h, v);
            return Result::Handled;
        }
        case Rpc::SetVehicleVariant: {
            uint8_t v1 = 255, v2 = 255;
            if (!reader.Read(v1) || !reader.Read(v2)) return Result::Malformed;
            Script::VehicleRpcSetVariant(id, v1, v2);
            return Result::Handled;
        }
        case Rpc::GiveVehicleSirens: {
            bool overrideSiren = false;
            if (!ReadBit(reader, overrideSiren)) return Result::Malformed;
            if (overrideSiren) {
                uint8_t type = 0, count = 0;
                bool f360 = false, los = true, rnd = true, silent = false;
                if (!reader.Read(type) || !reader.Read(count) || !ReadBit(reader, f360) || !ReadBit(reader, los) ||
                    !ReadBit(reader, rnd) || !ReadBit(reader, silent))
                    return Result::Malformed;
                Script::VehicleRpcGiveSirens(id, count, type, f360, los, rnd, silent);
            }
            return Result::Handled;
        }
        case Rpc::RemoveVehicleSirens: {
            Script::VehicleRpcRemoveSirens(id);
            return Result::Handled;
        }
        case Rpc::SetVehicleSirens: {
            bool overrideSiren = false;
            if (!ReadBit(reader, overrideSiren)) return Result::Malformed;
            if (overrideSiren) {
                uint8_t point = 0;
                float x = 0.f, y = 0.f, z = 0.f;
                uint8_t argb[4] = {};
                uint32_t minAlpha = 0;
                bool f360 = false, los = true, rnd = true, silent = false;
                if (!reader.Read(point) || !ReadFiniteFloat(reader, x) || !ReadFiniteFloat(reader, y) ||
                    !ReadFiniteFloat(reader, z) || !reader.ReadBits(argb, 32) || !reader.Read(minAlpha) ||
                    !ReadBit(reader, f360) || !ReadBit(reader, los) || !ReadBit(reader, rnd) ||
                    !ReadBit(reader, silent))
                    return Result::Malformed;
                // Desktop SColorSync writes B, G, R, A or R, G, B, A; preserve channel order.
                Script::VehicleRpcSetSirenPoint(id, point, x, y, z, argb[0], argb[1], argb[2], argb[3], minAlpha, f360,
                                                los, rnd, silent);
            }
            return Result::Handled;
        }
        case Rpc::SetVehiclePlateText: {
            std::string plate;
            if (!ReadU16String(reader, plate)) return Result::Malformed;
            Script::VehicleRpcSetPlateText(id, plate);
            return Result::Handled;
        }
        case Rpc::SetTrainTrack: {
            uint8_t track = 0;
            if (!reader.Read(track)) return Result::Malformed;
            Script::VehicleRpcSetTrainTrack(id, track);
            return Result::Handled;
        }
        case Rpc::SetTrainPosition: {
            float pos = 0.f;
            if (!ReadFiniteFloat(reader, pos)) return Result::Malformed;
            Script::VehicleRpcSetTrainPosition(id, pos);
            return Result::Handled;
        }
        case Rpc::SpawnVehicleFlyingComponent: {
            uint8_t nodeIdx = 0, colType = 0;
            int32_t removalTime = 0;
            if (!reader.Read(nodeIdx) || !reader.Read(colType) || !reader.Read(removalTime)) return Result::Malformed;
            LUA_LOGI("DIAG SPAWN_VEHICLE_FLYING_COMPONENT: veh=%u node=%u col=%u time=%d", id,
                     static_cast<unsigned>(nodeIdx), static_cast<unsigned>(colType), removalTime);
            return Result::Handled;
        }
        case Rpc::SetVehicleNitroActivated: {
            bool activated = false;
            if (!ReadBit(reader, activated)) return Result::Malformed;
            Enqueue([id, activated] { Script::VehicleRpcSetNitroActivated(id, activated); });
            return Result::Handled;
        }

        // ---- Handling RPCs (CHandlingRPCs.cpp, SyncStructures.h:1981-2002) ----
        case Rpc::SetVehicleHandling: {
            float f = 0.f;
            uint8_t u8 = 0;
            uint32_t u32 = 0;
            bool b = false;
            if (!ReadFloat(reader, f) || !ReadFloat(reader, f) || !ReadFloat(reader, f) ||
                !ReadFloat(reader, f) || !ReadFloat(reader, f) || !ReadFloat(reader, f) ||
                !reader.Read(u8) || !ReadFloat(reader, f) || !reader.Read(u8) || !reader.Read(u8) ||
                !reader.Read(u8) || !ReadFloat(reader, f) || !ReadFloat(reader, f) || !ReadFloat(reader, f) ||
                !ReadFloat(reader, f) || !ReadFloat(reader, f) || !ReadBit(reader, b) ||
                !ReadFloat(reader, f) || !ReadFloat(reader, f) || !ReadFloat(reader, f) ||
                !ReadFloat(reader, f) || !ReadFloat(reader, f) || !ReadFloat(reader, f) ||
                !ReadFloat(reader, f) || !ReadFloat(reader, f) || !ReadFloat(reader, f) ||
                !ReadFloat(reader, f) || !ReadFloat(reader, f) || !reader.Read(u32) || !reader.Read(u32) ||
                !ReadFloat(reader, f) || !reader.Read(u8))
                return Result::Malformed;
            return Result::Handled;
        }
        case Rpc::SetVehicleHandlingProperty: {
            uint8_t prop = 0;
            if (!reader.Read(prop)) return Result::Malformed;
            float f = 0.f;
            uint32_t u32 = 0;
            uint8_t u8 = 0;
            switch (prop) {
                case 4:  // HANDLING_CENTEROFMASS
                    if (!ReadFloat(reader, f) || !ReadFloat(reader, f) || !ReadFloat(reader, f))
                        return Result::Malformed;
                    break;
                case 5:   // HANDLING_PERCENTSUBMERGED
                case 29:  // HANDLING_HANDLINGFLAGS
                case 30:  // HANDLING_MODELFLAGS
                    if (!reader.Read(u32)) return Result::Malformed;
                    break;
                case 7:   // HANDLING_DRIVETYPE
                case 8:   // HANDLING_ENGINETYPE
                case 9:   // HANDLING_NUMOFGEARS
                case 15:  // HANDLING_ABS
                case 33:  // HANDLING_ANIMGROUP
                    if (!reader.Read(u8)) return Result::Malformed;
                    break;
                case 1: case 2: case 3: case 6: case 10: case 11: case 12: case 13: case 14: case 16:
                case 17: case 18: case 19: case 20: case 21: case 22: case 23: case 24: case 25: case 26:
                case 27:
                    if (!ReadFloat(reader, f)) return Result::Malformed;
                    break;
                default:
                    break;
            }
            return Result::Handled;
        }
        case Rpc::ResetVehicleHandlingProperty: {
            uint8_t prop = 0;
            if (!reader.Read(prop)) return Result::Malformed;
            return Result::Handled;
        }
        case Rpc::ResetVehicleHandling: {
            return Result::Handled;
        }

        // ---- Water RPCs (CWaterRPCs.cpp) ----
        case Rpc::SetElementWaterLevel: {
            float level = 0.f;
            if (!ReadFiniteFloat(reader, level)) return Result::Malformed;
            Enqueue([id, level] { Script::RpcSetElementWaterLevel(id, level); });
            return Result::Handled;
        }
        case Rpc::SetWaterVertexPosition: {
            uint8_t idx = 0;
            uint16_t ux = 0, uy = 0;
            float z = 0.f;
            if (!reader.Read(idx) || !reader.Read(ux) || !reader.Read(uy) || !ReadFiniteFloat(reader, z))
                return Result::Malformed;
            const int16_t x = static_cast<int16_t>(ux);
            const int16_t y = static_cast<int16_t>(uy);
            Enqueue([id, idx, x, y, z] { Script::RpcSetWaterVertexPosition(id, idx, x, y, z); });
            return Result::Handled;
        }

        // ---- Element & Object & Marker RPCs ----
        case Rpc::SetLowLodElement: {
            uint32_t lowId = 0;
            if (!Entity::ReadElementId(reader, lowId)) return Result::Malformed;
            Enqueue([id, lowId] { Script::RpcSetLowLodElement(id, lowId); });
            return Result::Handled;
        }
        case Rpc::SetPropagateCallsEnabled: {
            bool enabled = false;
            if (!ReadBit(reader, enabled)) return Result::Malformed;
            Enqueue([id, enabled] { Script::RpcSetCallPropagationEnabled(id, enabled); });
            return Result::Handled;
        }
        case Rpc::SetElementAngularVelocity: {
            float rx = 0.f, ry = 0.f, rz = 0.f;
            if (!ReadFiniteFloat(reader, rx) || !ReadFiniteFloat(reader, ry) || !ReadFiniteFloat(reader, rz))
                return Result::Malformed;
            Enqueue([id, rx, ry, rz] { Script::RpcSetElementAngularVelocity(id, rx, ry, rz); });
            return Result::Handled;
        }
        case Rpc::SetElementOnFire: {
            bool onFire = false;
            if (!ReadBit(reader, onFire)) return Result::Malformed;
            Enqueue([id, onFire] { Script::RpcSetElementOnFire(id, onFire); });
            return Result::Handled;
        }
        case Rpc::SetObjectVisibleInAllDimensions: {
            bool visible = false;
            uint16_t newDim = 0;
            if (!ReadBit(reader, visible) || !reader.Read(newDim)) return Result::Malformed;
            Enqueue([id, visible, newDim] { Script::ServerSetObjectVisibleInAllDimensions(id, visible, newDim); });
            return Result::Handled;
        }
        case Rpc::SetObjectBreakable: {
            bool breakable = false;
            if (!ReadBit(reader, breakable)) return Result::Malformed;
            Enqueue([id, breakable] { Script::ServerSetObjectBreakable(id, breakable); });
            return Result::Handled;
        }
        case Rpc::BreakObject: {
            Enqueue([id] { Script::ServerBreakObject(id); });
            return Result::Handled;
        }
        case Rpc::RespawnObject: {
            Enqueue([id] { Script::ServerRespawnObject(id); });
            return Result::Handled;
        }
        case Rpc::ToggleObjectRespawn: {
            bool enabled = false;
            if (!ReadBit(reader, enabled)) return Result::Malformed;
            Enqueue([id, enabled] { Script::ServerToggleObjectRespawn(id, enabled); });
            return Result::Handled;
        }
        case Rpc::SetObjectDynamicPhysics: {
            bool enabled = false;
            if (!ReadBit(reader, enabled)) return Result::Malformed;
            Enqueue([id, enabled] { Script::ServerSetObjectDynamicPhysics(id, enabled); });
            return Result::Handled;
        }
        case Rpc::SetMarkerTargetArrowProperties: {
            uint8_t rgba[4] = {};
            float size = 0.f;
            if (!reader.ReadBits(rgba, 32) || !ReadFiniteFloat(reader, size)) return Result::Malformed;
            LUA_LOGI("DIAG SET_MARKER_TARGET_ARROW_PROPERTIES: marker=%u rgba=(%u,%u,%u,%u) size=%.2f", id, rgba[0],
                     rgba[1], rgba[2], rgba[3], size);
            return Result::Handled;
        }

        // ---- Custom Weapon RPCs (CWeaponRPCs.cpp / CElementRPCs.cpp 181-190) ----
        case Rpc::FireCustomWeapon: {
            Enqueue([id] { Script::RpcFireCustomWeapon(id); });
            return Result::Handled;
        }
        case Rpc::SetCustomWeaponState: {
            uint8_t raw = 0;
            if (!reader.Read(raw)) return Result::Malformed;
            const int8_t state = static_cast<int8_t>(raw);
            Enqueue([id, state] { Script::RpcSetCustomWeaponState(id, state); });
            return Result::Handled;
        }
        case Rpc::SetCustomWeaponAmmo: {
            int32_t ammo = 0;
            if (!reader.Read(ammo)) return Result::Malformed;
            Enqueue([id, ammo] { Script::RpcSetCustomWeaponAmmo(id, ammo); });
            return Result::Handled;
        }
        case Rpc::SetCustomWeaponClipAmmo: {
            int32_t ammo = 0;
            if (!reader.Read(ammo)) return Result::Malformed;
            Enqueue([id, ammo] { Script::RpcSetCustomWeaponClipAmmo(id, ammo); });
            return Result::Handled;
        }
        case Rpc::SetCustomWeaponTarget: {
            bool isVector = false;
            if (!ReadBit(reader, isVector)) return Result::Malformed;
            if (isVector) {
                float x = 0.f, y = 0.f, z = 0.f;
                if (!ReadFiniteFloat(reader, x) || !ReadFiniteFloat(reader, y) || !ReadFiniteFloat(reader, z))
                    return Result::Malformed;
                Enqueue([id, x, y, z] { Script::RpcSetCustomWeaponTargetPos(id, x, y, z); });
            } else {
                uint32_t targetId = 0;
                uint8_t rawBone = 0;
                if (!Entity::ReadElementId(reader, targetId) || !reader.Read(rawBone)) return Result::Malformed;
                const int8_t bone = static_cast<int8_t>(rawBone);
                Enqueue([id, targetId, bone] { Script::RpcSetCustomWeaponTargetElement(id, targetId, bone); });
            }
            return Result::Handled;
        }
        case Rpc::ResetCustomWeaponTarget: {
            Enqueue([id] { Script::RpcResetCustomWeaponTarget(id); });
            return Result::Handled;
        }
        case Rpc::SetCustomWeaponFlags: {
            bool flags[13] = {};
            for (int i = 0; i < 13; ++i) {
                if (!ReadBit(reader, flags[i])) return Result::Malformed;
            }
            Enqueue([id, flags] {
                Script::RpcSetCustomWeaponFlags(id, flags[0], flags[1], flags[2], flags[3], flags[4], flags[5],
                                                flags[6], flags[7], flags[8], flags[9], flags[10], flags[11],
                                                flags[12]);
            });
            return Result::Handled;
        }
        case Rpc::SetCustomWeaponFiringRate: {
            int32_t rate = 0;
            if (!reader.Read(rate)) return Result::Malformed;
            Enqueue([id, rate] { Script::RpcSetCustomWeaponFiringRate(id, rate); });
            return Result::Handled;
        }
        case Rpc::ResetCustomWeaponFiringRate: {
            Enqueue([id] { Script::RpcResetCustomWeaponFiringRate(id); });
            return Result::Handled;
        }
        case Rpc::SetWeaponOwner: {
            uint32_t ownerId = 0;
            if (!Entity::ReadElementId(reader, ownerId)) return Result::Malformed;
            Enqueue([id, ownerId] { Script::RpcSetWeaponOwner(id, ownerId); });
            return Result::Handled;
        }
        default:
            return Result::NotHandled;
    }
}

}  // namespace

Result HandleGlobal(uint8_t func, Net::BitStreamReader& reader) {
    Result result = HandleWorld(func, reader);
    if (result != Result::NotHandled) return result;
    result = HandleCamera(func, reader);
    if (result != Result::NotHandled) return result;
    result = HandlePlayer(func, reader);
    if (result != Result::NotHandled) return result;
    result = HandleAudio(func, reader);
    if (result != Result::NotHandled) return result;
    result = HandleChat(func, reader);
    if (result != Result::NotHandled) return result;
    result = HandleInput(func, reader);
    if (result != Result::NotHandled) return result;
    return HandleGlobalExtended(func, reader);
}

Result HandleElement(uint8_t func, uint32_t id, Net::BitStreamReader& reader) {
    switch (func) {
        case Rpc::AttachElements: return HandleAttach(id, reader);
        case Rpc::DetachElements: return HandleDetach(id, reader);
        case Rpc::SetElementAttachedOffsets: return HandleAttachedOffsets(id, reader);
        case Rpc::FixVehicle:
        case Rpc::BlowVehicle: return HandleVehicleState(func, id, reader);
        case Rpc::SetObjectRotation:
        case Rpc::MoveObject:
        case Rpc::StopObject:
        case Rpc::SetObjectScale: return HandleObject(func, id, reader);
        default: {
            const Result r = HandleEntityMisc(func, id, reader);
            return r != Result::NotHandled ? r : HandleElementExtended(func, id, reader);
        }
    }
}

void Enqueue(std::function<void()> op) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_queue.size() >= kMaxQueued) {
        if (g_dropped.fetch_add(1) < 4) LUA_LOGE("DIAG server RPC queue full (%zu); dropping an RPC", g_queue.size());
        return;
    }
    g_queue.push_back(std::move(op));
}

void Pump() {
    Script::KeyBindsSyncServerReset();
    std::deque<std::function<void()>> work;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        work.swap(g_queue);
    }
    for (auto& op : work) op();
}

size_t Pending() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_queue.size();
}

void ResetSession() {
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_queue.clear();
    }
    g_cameraContext.store(0);
    g_dropped.store(0);
    Script::KeyBindsServerReset();
    Script::WorldQueryReset();
}

uint8_t CameraSyncTimeContext() { return g_cameraContext; }

}  // namespace WorldRPG::ServerRpc
