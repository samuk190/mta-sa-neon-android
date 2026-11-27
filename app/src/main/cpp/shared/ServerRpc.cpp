#include "ServerRpc.h"

#include "LuaApiExtra.h"
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

#include <atomic>
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

void Enqueue(std::function<void()> op) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_queue.size() >= kMaxQueued) {
        if (g_dropped.fetch_add(1) < 4) LUA_LOGE("DIAG server RPC queue full (%zu); dropping an RPC", g_queue.size());
        return;
    }
    g_queue.push_back(std::move(op));
}

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
    // CAudioRPCs::PlaySound (CAudioRPCs.cpp:20-27): SIntegerSync<uchar, 7>, then PlayFrontEndSound. The other sound
    // types in that function are commented out on desktop.
    uint8_t sound = 0;
    if (!reader.ReadBits(&sound, 7)) return Result::Malformed;
    // DIAG: CAudioEngine::PlayFrontEndSound is not hooked (see playSoundFrontEnd in LuaAudioApi.cpp).
    LUA_LOGI("DIAG PLAY_SOUND %u: CAudioEngine::PlayFrontEndSound is not hooked", sound);
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
                        LUA_LOGI("DIAG BIND_KEY: only hardware key presses fire server binds; the touch controls and "
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
    return HandleInput(func, reader);
}

Result HandleElement(uint8_t func, uint32_t id, Net::BitStreamReader& reader) {
    (void)func;
    (void)id;
    (void)reader;
    return Result::NotHandled;
}

void Pump() {
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
    Script::KeyBindsServerReset();
}

uint8_t CameraSyncTimeContext() { return g_cameraContext; }

}  // namespace WorldRPG::ServerRpc
