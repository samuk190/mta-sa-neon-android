// Host checks for the server RPCs of PACKET_ID_LUA (76) and PACKET_ID_LUA_ELEMENT_RPC (77) in shared/ServerRpc.cpp:
// wire decoding (truncated, malformed and out of range packets), and the state the Lua getters report afterwards.
// Desktop references: CWorldRPCs.cpp, CCameraRPCs.cpp, CPlayerRPCs.cpp, CAudioRPCs.cpp, CInputRPCs.cpp,
// CElementRPCs.cpp, CVehicleRPCs.cpp, CObjectRPCs.cpp.
#include "game_script.h"
#include "shared/LuaKeyBindApi.h"
#include "shared/LuaApiExtra.h"
#include "shared/LuaVMStub.h"
#include "shared/LuaWorldObjectApi.h"
#include "shared/MtaIds.h"
#include "shared/PacketHandler.h"
#include "shared/ServerRpc.h"
#include "shared/ServerWorld.h"
#include "shared/entity/EntityAddTypes.h"
#include "gtasa/HudComponents.h"
#include "net/raknet/RakNetWrapper.h"

#include "lua.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace WorldRPG::JniBridge {
std::vector<std::string> TakeGameLines(int kind);
}

using namespace WorldRPG;
namespace Rpc = WorldRPG::MtaIds::Rpc;

namespace {
unsigned checks = 0;
void Check(bool yes, const char* label) {
    ++checks;
    if (!yes) {
        std::fprintf(stderr, "FAIL: %s\n", label);
        std::exit(1);
    }
}

bool Near(double a, double b, double eps = 1e-3) { return std::fabs(a - b) <= eps; }

struct Fixture {
    Sync::PacketHandler handler;
    Script::LuaVMStub vm;
    Fixture() {
        ServerWorld::ResetSession();
        Headless::ResetProbe();
        Game::ResetHudComponents();
        Check(vm.initialize(), "VM initializes");
        // The server RPCs of every test start from clean world state.
        Lua("resetRainLevel() resetSunSize() resetSunColor() resetWindVelocity() resetFarClipDistance() "
            "resetFogDistance() resetMoonSize()");
    }
    ~Fixture() { vm.shutdown(); }

    void Pump() { vm.pumpDx(false); }

    void Lua(const std::string& script) {
        auto r = vm.executeBuffer(script.data(), script.size(), "server-rpc-test");
        if (!r.success) std::fprintf(stderr, "%s\n", r.errorMessage.c_str());
        Check(r.success, "Lua check");
    }

    bool Send(const Net::BitStreamWriter& w) { return handler.handlePacket(99, w.GetData(), w.GetByteLength(), w.GetBitLength()); }
};

struct Pkt {
    Net::BitStreamWriter w;
    explicit Pkt(uint8_t func, uint8_t packet = MtaIds::Packet::Lua) {
        w.Write(packet);
        w.Write(func);
    }
    Pkt& U8(uint8_t v) { w.Write(v); return *this; }
    Pkt& U16(uint16_t v) { w.Write(v); return *this; }
    Pkt& U32(uint32_t v) { w.Write(v); return *this; }
    Pkt& I32(int32_t v) { w.Write(v); return *this; }
    Pkt& F(float v) { w.Write(v); return *this; }
    Pkt& Bit(bool v) {
        const uint8_t b = v ? 1 : 0;
        w.WriteBits(&b, 1);
        return *this;
    }
    Pkt& Bits(uint8_t v, unsigned count) { w.WriteBits(&v, count); return *this; }
    Pkt& Id(uint32_t id) {
        uint8_t bytes[4];
        std::memcpy(bytes, &id, 4);
        w.WriteBits(bytes, 17);
        return *this;
    }
    Pkt& Str(const std::string& s) {
        w.Write(uint8_t(s.size()));
        if (!s.empty()) w.WriteBits(reinterpret_cast<const uint8_t*>(s.data()), unsigned(s.size() * 8));
        return *this;
    }
};

// A packet cut after `keepBits` bits, as a short datagram would arrive.
bool SendTruncated(Fixture& f, const Pkt& p, size_t keepBits) {
    const size_t bytes = (keepBits + 7) / 8;
    return f.handler.handlePacket(99, p.w.GetData(), bytes, keepBits);
}

double LuaNumber(Fixture& f, const std::string& expr) {
    const std::string script = "__r = " + expr;
    f.Lua(script);
    lua_State* L = Script::ActiveVmState();
    lua_getglobal(L, "__r");
    const double v = lua_tonumber(L, -1);
    lua_pop(L, 1);
    return v;
}

void World() {
    Fixture f;
    // SET_TIME: hour < 24 and minute < 60; anything else is read and ignored (CWorldRPCs.cpp:95-107).
    Check(f.Send(Pkt(Rpc::SetTime).U8(13).U8(45).w), "SET_TIME decodes");
    Check(Headless::Probe().timeSet == false, "nothing is applied before Pump (net thread only decodes)");
    f.Pump();
    Check(Headless::Probe().hour == 13 && Headless::Probe().minute == 45, "SET_TIME applies 13:45");
    Headless::ResetProbe();
    Check(f.Send(Pkt(Rpc::SetTime).U8(24).U8(0).w), "hour 24 is consumed");
    Check(f.Send(Pkt(Rpc::SetTime).U8(5).U8(60).w), "minute 60 is consumed");
    f.Pump();
    Check(!Headless::Probe().timeSet, "hour 24 and minute 60 are ignored, as on desktop (min < 60)");
    Check(!f.Send(Pkt(Rpc::SetTime).U8(5).w), "SET_TIME without the minute is malformed");

    Check(f.Send(Pkt(Rpc::SetWeather).U8(8).w), "SET_WEATHER decodes");
    f.Pump();
    Check(Headless::Probe().weather == 8, "SET_WEATHER reaches the game layer");
    Check(LuaNumber(f, "getWeather()") == 8, "getWeather reports the server weather");
    Check(f.Send(Pkt(Rpc::SetWeatherBlended).U8(11).U8(7).w), "SET_WEATHER_BLENDED decodes");
    f.Pump();
    {
        f.Lua("local a, b = getWeather(); __a, __b = a, b");
        lua_State* L = Script::ActiveVmState();
        lua_getglobal(L, "__b");
        Check(lua_tointeger(L, -1) == 11, "the blended target is the second getWeather result");
        lua_pop(L, 1);
    }
    Check(!f.Send(Pkt(Rpc::SetWeatherBlended).U8(11).w), "SET_WEATHER_BLENDED needs the hour");

    Check(f.Send(Pkt(Rpc::SetMinuteDuration).U32(2500).w), "SET_MINUTE_DURATION decodes");
    f.Pump();
    Check(LuaNumber(f, "getMinuteDuration()") == 2500, "minute duration");
    Check(!SendTruncated(f, Pkt(Rpc::SetMinuteDuration).U32(7), 16 + 16), "a short minute duration is malformed");

    Check(f.Send(Pkt(Rpc::SetGravity).F(0.004f).w), "SET_GRAVITY");
    Check(f.Send(Pkt(Rpc::SetGameSpeed).F(1.5f).w), "SET_GAME_SPEED");
    Check(f.Send(Pkt(Rpc::SetWaveHeight).F(2.25f).w), "SET_WAVE_HEIGHT");
    Check(f.Send(Pkt(Rpc::SetBlurLevel).U8(80).w), "SET_BLUR_LEVEL");
    Check(f.Send(Pkt(Rpc::SetCloudsEnabled).U8(0).w), "SET_CLOUDS_ENABLED");
    Check(f.Send(Pkt(Rpc::SetJetpackMaxHeight).F(120.f).w), "SET_JETPACK_MAXHEIGHT");
    Check(f.Send(Pkt(Rpc::SetInteriorSoundsEnabled).Bit(false).w), "SET_INTERIOR_SOUNDS_ENABLED");
    Check(f.Send(Pkt(Rpc::SetRainLevel).F(0.75f).w), "SET_RAIN_LEVEL");
    Check(f.Send(Pkt(Rpc::SetSunSize).F(3.f).w), "SET_SUN_SIZE");
    Check(f.Send(Pkt(Rpc::SetSunColor).U8(10).U8(20).U8(30).U8(40).U8(50).U8(60).w), "SET_SUN_COLOR");
    Check(f.Send(Pkt(Rpc::SetWindVelocity).F(1.f).F(2.f).F(3.f).w), "SET_WIND_VELOCITY");
    Check(f.Send(Pkt(Rpc::SetFarClipDistance).F(1500.f).w), "SET_FAR_CLIP_DISTANCE");
    Check(f.Send(Pkt(Rpc::SetFogDistance).F(300.f).w), "SET_FOG_DISTANCE");
    Check(f.Send(Pkt(Rpc::SetAircraftMaxHeight).F(900.f).w), "SET_AIRCRAFT_MAXHEIGHT");
    Check(f.Send(Pkt(Rpc::SetAircraftMaxVelocity).F(0.5f).w), "SET_AIRCRAFT_MAXVELOCITY");
    Check(f.Send(Pkt(Rpc::SetOcclusionsEnabled).Bit(false).w), "SET_OCCLUSIONS_ENABLED");
    Check(f.Send(Pkt(Rpc::SetMoonSize).I32(7).w), "SET_MOON_SIZE");
    f.Pump();
    Check(Near(LuaNumber(f, "getGravity()"), 0.004), "gravity");
    Check(Near(LuaNumber(f, "getGameSpeed()"), 1.5), "game speed");
    Check(Near(LuaNumber(f, "getWaveHeight()"), 2.25), "wave height");
    Check(LuaNumber(f, "getBlurLevel()") == 80, "blur level");
    f.Lua("__c = getCloudsEnabled()");
    {
        lua_State* L = Script::ActiveVmState();
        lua_getglobal(L, "__c");
        Check(lua_toboolean(L, -1) == 0, "clouds disabled");
        lua_pop(L, 1);
    }
    Check(Near(LuaNumber(f, "getJetpackMaxHeight()"), 120), "jetpack max height");
    Check(Near(LuaNumber(f, "getRainLevel()"), 0.75), "rain level");
    Check(Near(LuaNumber(f, "getSunSize()"), 3), "sun size");
    Check(LuaNumber(f, "select(1, getSunColor())") == 10 && LuaNumber(f, "select(3, getSunColor())") == 30,
          "sun core colour");
    Check(Near(LuaNumber(f, "select(2, getWindVelocity())"), 2), "wind velocity");
    Check(Near(LuaNumber(f, "getFarClipDistance()"), 1500), "far clip");
    Check(Near(LuaNumber(f, "getFogDistance()"), 300), "fog distance");
    Check(Near(LuaNumber(f, "getAircraftMaxHeight()"), 900), "aircraft max height");
    Check(Near(LuaNumber(f, "getAircraftMaxVelocity()"), 0.5), "aircraft max velocity");
    Check(LuaNumber(f, "getMoonSize()") == 7, "moon size");

    // Resets carry no payload: the function id alone is a whole packet.
    Check(f.Send(Pkt(Rpc::ResetRainLevel).w), "RESET_RAIN_LEVEL has no payload");
    Check(f.Send(Pkt(Rpc::ResetSunSize).w), "RESET_SUN_SIZE");
    Check(f.Send(Pkt(Rpc::ResetSunColor).w), "RESET_SUN_COLOR");
    Check(f.Send(Pkt(Rpc::ResetWindVelocity).w), "RESET_WIND_VELOCITY");
    Check(f.Send(Pkt(Rpc::ResetFarClipDistance).w), "RESET_FAR_CLIP_DISTANCE");
    Check(f.Send(Pkt(Rpc::ResetFogDistance).w), "RESET_FOG_DISTANCE");
    Check(f.Send(Pkt(Rpc::ResetMoonSize).w), "RESET_MOON_SIZE");
    f.Pump();
    Check(LuaNumber(f, "getRainLevel()") == 0, "rain reset");
    Check(LuaNumber(f, "getSunSize()") == 1, "sun size reset");
    Check(LuaNumber(f, "select(1, getSunColor())") == 0, "sun colour reset");
    Check(LuaNumber(f, "getFarClipDistance()") == 800, "far clip reset");
    Check(LuaNumber(f, "getFogDistance()") == 150, "fog reset");
    Check(LuaNumber(f, "getMoonSize()") == 3, "moon size reset");

    // Malformed input changes nothing.
    Check(!f.Send(Pkt(Rpc::SetGravity).w), "SET_GRAVITY without a float is malformed");
    Check(!f.Send(Pkt(Rpc::SetSunColor).U8(1).U8(2).U8(3).w), "SET_SUN_COLOR short");
    Check(!f.Send(Pkt(Rpc::SetFogDistance).F(std::numeric_limits<float>::quiet_NaN()).w), "NaN fog is rejected");
    Check(!f.Send(Pkt(Rpc::SetWindVelocity).F(1.f).F(std::numeric_limits<float>::infinity()).F(0.f).w),
          "an infinite wind component is rejected");
    f.Pump();
    Check(Near(LuaNumber(f, "getGravity()"), 0.004), "malformed packets left the gravity alone");
    Check(f.handler.handlePacket(99, nullptr, 0, 0) == false, "an empty buffer is rejected");
}

void Camera() {
    Fixture f;
    // CCameraRPCs::SetCameraMatrix (CCameraRPCs.cpp:28-92): time context, position, look at, then roll and FOV
    // when 64 bits remain or roll alone when 32 remain. The camera goes fixed.
    Check(f.Send(Pkt(Rpc::SetCameraMatrix).U8(9).F(10.f).F(20.f).F(30.f).F(11.f).F(21.f).F(31.f).F(-90.f).F(200.f).w),
          "SET_CAMERA_MATRIX with roll and FOV");
    Check(ServerRpc::CameraSyncTimeContext() == 9, "the camera time context is stored");
    f.Pump();
    Check(Headless::Probe().cameraNotes == 1 && Near(Headless::Probe().camera[0], 10) &&
              Near(Headless::Probe().camera[5], 31),
          "the game layer got position and look at");
    Check(Near(LuaNumber(f, "select(7, getCameraMatrix())"), 270), "roll -90 wraps to 270 (remainder, then +360)");
    Check(Near(LuaNumber(f, "select(8, getCameraMatrix())"), 179), "FOV 200 is clamped to 179");
    Check(Near(LuaNumber(f, "select(1, getCameraMatrix())"), 10), "getCameraMatrix position");

    Check(f.Send(Pkt(Rpc::SetCameraMatrix).U8(1).F(1.f).F(2.f).F(3.f).F(4.f).F(5.f).F(6.f).F(45.f).w),
          "SET_CAMERA_MATRIX with roll only");
    f.Pump();
    Check(Near(LuaNumber(f, "select(7, getCameraMatrix())"), 45) && Near(LuaNumber(f, "select(8, getCameraMatrix())"), 70),
          "roll only: FOV stays 70");
    Check(f.Send(Pkt(Rpc::SetCameraMatrix).U8(1).F(1.f).F(2.f).F(3.f).F(4.f).F(5.f).F(6.f).w), "SET_CAMERA_MATRIX bare");
    f.Pump();
    Check(Near(LuaNumber(f, "select(7, getCameraMatrix())"), 0), "bare matrix: roll 0");
    Check(f.Send(Pkt(Rpc::SetCameraMatrix).U8(1).F(1.f).F(2.f).F(3.f).F(4.f).F(5.f).F(6.f).F(0.f).F(-5.f).w), "FOV <= 0");
    f.Pump();
    Check(Near(LuaNumber(f, "select(8, getCameraMatrix())"), 70), "FOV <= 0 becomes 70");

    Headless::ResetProbe();
    Check(f.Send(Pkt(Rpc::SetCameraMatrix).U8(1).F(std::numeric_limits<float>::quiet_NaN()).F(2.f).F(3.f).F(4.f).F(5.f).F(6.f).w),
          "a NaN position is consumed");
    f.Pump();
    Check(Headless::Probe().cameraNotes == 0, "and not applied (CCameraRPCs.cpp:60-66)");
    Check(!f.Send(Pkt(Rpc::SetCameraMatrix).U8(1).F(1.f).F(2.f).w), "a short matrix is malformed");
    Check(!f.Send(Pkt(Rpc::SetCameraMatrix).w), "a missing time context is malformed");

    Check(f.Send(Pkt(Rpc::SetCameraInterior).U8(5).w), "SET_CAMERA_INTERIOR");
    f.Pump();
    Check(LuaNumber(f, "getCameraInterior()") == 5, "camera interior");
    Check(!f.Send(Pkt(Rpc::SetCameraInterior).w), "SET_CAMERA_INTERIOR needs a byte");

    // SET_CAMERA_TARGET: an element the client does not know is ignored after the time context is stored.
    Headless::ResetProbe();
    Check(f.Send(Pkt(Rpc::SetCameraTarget).U8(33).Id(4321).w), "SET_CAMERA_TARGET with an unknown element");
    Check(ServerRpc::CameraSyncTimeContext() == 33, "time context of the target RPC");
    f.Pump();
    Check(Headless::Probe().cameraClears == 0, "an unknown target leaves the camera alone");
    Check(!f.Send(Pkt(Rpc::SetCameraTarget).U8(33).w), "SET_CAMERA_TARGET without an element is malformed");

    // FADE_CAMERA: fade in has no colour, fade out has three bytes (CCameraRPCs.cpp:171-196).
    Check(f.Send(Pkt(Rpc::FadeCamera).U8(0).F(2.f).U8(255).U8(128).U8(0).w), "FADE_CAMERA out");
    f.Pump();
    f.Lua("__f = isCameraFading and isCameraFading() or nil");
    Check(f.Send(Pkt(Rpc::FadeCamera).U8(1).F(1.f).w), "FADE_CAMERA in has no colour bytes");
    Check(!f.Send(Pkt(Rpc::FadeCamera).U8(0).F(1.f).U8(1).w), "FADE_CAMERA out needs the three colour bytes");
    Check(!f.Send(Pkt(Rpc::FadeCamera).U8(1).w), "FADE_CAMERA needs the time");
    Check(f.Send(Pkt(Rpc::FadeCamera).U8(1).F(std::numeric_limits<float>::quiet_NaN()).w), "NaN fade time is consumed");
    f.Pump();
}

void PlayerHudChat() {
    Fixture f;
    Check(f.Send(Pkt(Rpc::SetPlayerMoney).I32(123456).w), "SET_PLAYER_MONEY");
    f.Pump();
    Check(LuaNumber(f, "getPlayerMoney()") == 123456, "getPlayerMoney after the RPC");
    Check(f.Send(Pkt(Rpc::SetPlayerMoney).I32(-50).Bit(true).w), "SET_PLAYER_MONEY with the instant bit");
    f.Pump();
    Check(LuaNumber(f, "getPlayerMoney()") == -50, "a negative amount is kept");
    Check(!f.Send(Pkt(Rpc::SetPlayerMoney).U8(1).U8(2).w), "SET_PLAYER_MONEY with two bytes is malformed");

    // SHOW_PLAYER_HUD_COMPONENT: uchar component, uchar show. Index 5 is money, 13 is all.
    Check(f.Send(Pkt(Rpc::ShowPlayerHudComponent).U8(5).U8(0).w), "hide money");
    f.Pump();
    Check(!Game::HostHudComponentVisible("money"), "money is hidden");
    Check(f.Send(Pkt(Rpc::ShowPlayerHudComponent).U8(5).U8(7).w), "show is any non-zero byte");
    f.Pump();
    Check(Game::HostHudComponentVisible("money"), "money is shown again");
    Check(f.Send(Pkt(Rpc::ShowPlayerHudComponent).U8(9).U8(0).w), "hide clock");
    f.Pump();
    Check(!Game::HostHudComponentVisible("clock"), "clock is hidden");
    Check(f.Send(Pkt(Rpc::ShowPlayerHudComponent).U8(77).U8(0).w), "an unknown component is consumed");
    Check(!f.Send(Pkt(Rpc::ShowPlayerHudComponent).U8(5).w), "SHOW_PLAYER_HUD_COMPONENT needs the flag");
    f.Pump();

    Check(f.Send(Pkt(Rpc::PlaySound).Bits(55, 7).w), "PLAY_SOUND reads seven bits");
    Check(!f.Send(Pkt(Rpc::PlaySound).w), "PLAY_SOUND without an id is malformed");

    Check(f.Send(Pkt(Rpc::ShowChat).U8(0).w), "SHOW_CHAT hide");
    f.Pump();
    {
        const auto lines = JniBridge::TakeGameLines(10);
        Check(!lines.empty() && lines.back() == "0", "the chat view is told to hide");
    }
    f.Lua("__v = isChatVisible()");
    {
        lua_State* L = Script::ActiveVmState();
        lua_getglobal(L, "__v");
        Check(lua_toboolean(L, -1) == 0, "isChatVisible follows the RPC");
        lua_pop(L, 1);
    }
    Check(f.Send(Pkt(Rpc::ShowChat).U8(1).U8(0).w), "SHOW_CHAT show with the input flag");
    f.Pump();
    {
        const auto lines = JniBridge::TakeGameLines(10);
        Check(!lines.empty() && lines.back() == "1", "the chat view is told to show");
    }
    Check(!f.Send(Pkt(Rpc::ShowChat).w), "SHOW_CHAT without the byte is malformed");
}

void Input() {
    Fixture f;
    // BIND_KEY: key length, key, state (down when == 1). A server bind reports a hardware press as KEY_BIND.
    Check(f.Send(Pkt(Rpc::BindKey).Str("m").U8(1).w), "BIND_KEY decodes");
    Check(!Script::KeyBindsServerBound("m", true), "nothing binds before Pump");
    f.Pump();
    Check(Script::KeyBindsServerBound("m", true) && !Script::KeyBindsServerBound("m", false), "bound for down only");
    Check(f.Send(Pkt(Rpc::BindKey).Str("m").U8(1).w), "the same bind again");
    f.Pump();
    Check(f.Send(Pkt(Rpc::UnbindKey).Str("M").U8(0).w), "UNBIND_KEY up leaves the down bind");
    f.Pump();
    Check(Script::KeyBindsServerBound("m", true), "a different state is not removed");

    Script::KeyBindsTakeServerEvents();
    Check(Script::KeyBindsBoundKeys().find("m") != std::string::npos, "BIND_KEY key appears in on-screen bound keys");
    Script::KeyBindsQueueKey("m", true);
    Script::KeyBindsQueueKey("m", false);
    Script::KeyBindsQueueKey("n", true);
    f.Pump();
    f.vm.pumpDx(false);
    {
        const auto events = Script::KeyBindsTakeServerEvents();
        Check(events.size() == 1 && events[0].name == "m" && events[0].down && !events[0].control,
              "one KEY_BIND event for the down press only");
    }
    Check(f.Send(Pkt(Rpc::UnbindKey).Str("m").U8(1).w), "UNBIND_KEY down");
    f.Pump();
    Check(!Script::KeyBindsServerBound("m", true), "removed");
    Check(Script::KeyBindsBoundKeys().find("m") == std::string::npos, "UNBIND_KEY removes key from bound keys");
    Script::KeyBindsQueueKey("m", true);
    f.vm.pumpDx(false);
    Check(Script::KeyBindsTakeServerEvents().empty(), "an unbound key sends nothing");

    // A control name is stored too (CInputRPCs.cpp:55-62), but a key press never reports it.
    Check(f.Send(Pkt(Rpc::BindKey).Str("fire").U8(1).w), "BIND_KEY for a control");
    f.Send(Pkt(Rpc::BindKey).Str("not_a_key").U8(1).w);
    f.Pump();
    Check(Script::KeyBindsServerBound("fire", true), "control bind stored");
    Check(!Script::KeyBindsServerBound("not_a_key", true), "an unknown name is ignored");
    Check(Script::KeyBindsBoundKeys().find("fire") == std::string::npos, "GTA control names do not appear in key list");

    Check(!f.Send(Pkt(Rpc::BindKey).U8(5).U8('a').w), "BIND_KEY with a short key is malformed");
    Check(!f.Send(Pkt(Rpc::BindKey).Str("m").w), "BIND_KEY without the state is malformed");
    Check(!f.Send(Pkt(Rpc::UnbindKey).w), "UNBIND_KEY without a length is malformed");

    // BIND_COMMAND: key, hit state char (0 down, 1 up, 3 both, else down), command, arguments, resource.
    Check(f.Send(Pkt(Rpc::BindCommand).Str("x").U8(3).Str("say").Str("hello").Str("res").w), "BIND_COMMAND both");
    Check(f.Send(Pkt(Rpc::BindCommand).Str("y").U8(9).Str("jump").Str("").Str("res").w), "BIND_COMMAND bad state is down");
    Check(f.Send(Pkt(Rpc::BindCommand).Str("not_a_key").U8(0).Str("a").Str("").Str("res").w), "unknown key consumed");
    f.Pump();
    f.Lua("local d = getCommandsBoundToKey('x', 'down'); local u = getCommandsBoundToKey('x', 'up'); "
          "assert(d.say == 'hello' and u.say == 'hello', 'both states'); "
          "assert(getCommandsBoundToKey('y', 'down').jump == '' and next(getCommandsBoundToKey('y', 'up')) == nil, 'down only'); "
          "assert(getKeyBoundToCommand('a') == false)");
    Script::KeyBindsTakeServerEvents();
    Script::KeyBindsQueueKey("x", true);
    f.vm.pumpDx(false);
    Check(Script::KeyBindsTakeServerEvents().empty(), "a command bind goes as a command, not as KEY_BIND");
    Check(f.Send(Pkt(Rpc::UnbindCommand).Str("x").U8(0).Str("say").Str("other").w), "UNBIND_COMMAND of another resource");
    f.Pump();
    f.Lua("assert(getCommandsBoundToKey('x', 'down').say == 'hello', 'a different resource keeps the bind')");
    Check(f.Send(Pkt(Rpc::UnbindCommand).Str("x").U8(0).Str("say").Str("res").w), "UNBIND_COMMAND down");
    f.Pump();
    f.Lua("assert(next(getCommandsBoundToKey('x', 'down')) == nil and getCommandsBoundToKey('x', 'up').say == 'hello')");
    Check(!f.Send(Pkt(Rpc::BindCommand).Str("x").U8(0).Str("say").w), "a short BIND_COMMAND is malformed");
    Check(!f.Send(Pkt(Rpc::UnbindCommand).Str("x").w), "a short UNBIND_COMMAND is malformed");

    // A new session forgets every server bind (both BIND_KEY and BIND_COMMAND).
    ServerWorld::ResetSession();
    Check(!Script::KeyBindsServerBound("fire", true), "ResetSession drops the server binds");
    Check(Script::KeyBindsBoundKeys().empty(), "ResetSession clears server keys from bound keys immediately");
    f.Lua("assert(next(getCommandsBoundToKey('x', 'up')) == nil and next(getCommandsBoundToKey('y', 'down')) == nil, "
          "'ResetSession drops server BIND_COMMAND entries')");

    Check(f.Send(Pkt(Rpc::SetControlState).Str("forwards").U8(1).w), "SET_CONTROL_STATE");
    Check(f.Send(Pkt(Rpc::SetControlState).Str("").U8(1).w), "an empty control is ignored");
    Check(!f.Send(Pkt(Rpc::SetControlState).Str("jump").w), "SET_CONTROL_STATE without state is malformed");

    // SHOW_CURSOR: show, resource net id (u16), toggle controls. An unknown resource is ignored.
    Check(f.Send(Pkt(Rpc::ShowCursor).U8(1).U16(777).U8(1).w), "SHOW_CURSOR with an unknown resource is consumed");
    f.Pump();
    f.Lua("assert(isCursorShowing() == false)");
    Check(!f.Send(Pkt(Rpc::ShowCursor).U8(1).U16(777).w), "SHOW_CURSOR without the toggle is malformed");
}

void Bits(Net::BitStreamWriter& w, uint32_t value, size_t count) {
    const uint8_t bytes[4] = {uint8_t(value), uint8_t(value >> 8), uint8_t(value >> 16), uint8_t(value >> 24)};
    w.WriteBits(bytes, count);
}

// ENTITY_ADD of one OBJECT (type 3), the layout ReadEntityBodyObject reads.
void AddObject(uint32_t id, uint32_t model) {
    Net::BitStreamWriter w;
    w.WriteCompressed(1u, 4);
    Bits(w, id, 17); w.Write(uint8_t(3)); Bits(w, 0, 17);
    w.Write(uint8_t(0)); w.WriteCompressed(0u, 2);
    Bits(w, 0, 1); Bits(w, 1, 1);
    w.WriteCompressed(0u, 2); w.WriteCompressed(0u, 2);
    Bits(w, 1024, 24); Bits(w, 2048, 24); w.Write(3.f);
    Bits(w, 0, 16); Bits(w, 0, 16); Bits(w, 0, 16);
    w.WriteCompressed(model, 2); w.WriteCompressed(0u, 1);
    Bits(w, 0, 1); Bits(w, 0x1FFFF, 17);
    Bits(w, 0, 1); Bits(w, 0, 1); Bits(w, 0, 1); Bits(w, 0, 1);
    Bits(w, 1, 1); Bits(w, 1, 1); Bits(w, 0, 1);
    Bits(w, 2000, 11); Bits(w, 0, 1); Bits(w, 1, 1);
    ServerWorld::OnEntityAdd(w.GetData(), w.GetByteLength(), w.GetBitLength());
}

// ENTITY_ADD of one VEHICLE (type 2), the layout of resource_tests CheckVehiclePackets.
void AddVehicle(uint32_t id) {
    Net::BitStreamWriter spawn;
    spawn.WriteCompressed(1u, 4);
    Bits(spawn, id, 17); spawn.Write(uint8_t(Entity::kTypeVehicle)); Bits(spawn, 0, 17);
    spawn.Write(uint8_t(0)); spawn.WriteCompressed(0u, 2);
    Bits(spawn, 0, 1); Bits(spawn, 1, 1);
    spawn.WriteCompressed(0u, 2); spawn.WriteCompressed(0u, 2);
    Bits(spawn, 0, 24); Bits(spawn, 0, 24); spawn.Write(10.f);
    Bits(spawn, 0, 16); Bits(spawn, 0, 16); Bits(spawn, 0, 16);
    spawn.Write(uint16_t(536));
    Bits(spawn, 2000, 12); Bits(spawn, 0, 2);
    Bits(spawn, 1, 2);
    for (uint8_t component : {19, 42, 201, 9, 77, 123}) spawn.Write(component);
    Bits(spawn, 0, 2);
    Bits(spawn, 0, 18); Bits(spawn, 0, 8); Bits(spawn, 0, 14); Bits(spawn, 0, 8);
    Bits(spawn, 0, 16); Bits(spawn, 0, 12);
    spawn.Write(uint8_t(2)); spawn.Write(uint8_t(10)); spawn.Write(uint8_t(86));
    Bits(spawn, 0, 32); Bits(spawn, 0, 32);
    Bits(spawn, 0, 2);
    for (int flag = 0; flag < 12; ++flag) Bits(spawn, flag == 4, 1);
    spawn.WriteCompressed(0u, 1); Bits(spawn, 0, 3);
    ServerWorld::OnEntityAdd(spawn.GetData(), spawn.GetByteLength(), spawn.GetBitLength());
}

Pkt& Fixed(Pkt& p, float x, float y, float z) {
    Bits(p.w, uint32_t(int32_t(x * 1024.f)) & 0xFFFFFFu, 24);
    Bits(p.w, uint32_t(int32_t(y * 1024.f)) & 0xFFFFFFu, 24);
    p.w.Write(z);
    return p;
}

Pkt& Compressed(Pkt& p, uint32_t value) {
    p.w.WriteCompressed(value, 4);
    return p;
}

Pkt& Double(Pkt& p, double value) {
    uint8_t bytes[8];
    std::memcpy(bytes, &value, 8);
    p.w.WriteBits(bytes, 64);
    return p;
}

Pkt& EasingString(Pkt& p, const std::string& name) {
    p.w.Write(uint16_t(name.size()));
    if (!name.empty()) p.w.WriteBits(reinterpret_cast<const uint8_t*>(name.data()), unsigned(name.size() * 8));
    return p;
}

struct EPkt : Pkt {
    EPkt(uint8_t func, uint32_t id) : Pkt(func, MtaIds::Packet::LuaElementRpc) { Id(id); }
};

void SetGlobalElement(const char* name, uint32_t id) {
    lua_State* L = Script::ActiveVmState();
    Script::PushElement(L, id);
    lua_setglobal(L, name);
}

constexpr uint32_t kVehicle = 0x1234;
constexpr uint32_t kObject = 2900;
constexpr uint32_t kObject2 = 2901;

void Attach() {
    Fixture f;
    AddVehicle(kVehicle);
    AddObject(kObject, 1338);
    f.Lua("events = {} addEventHandler('onClientElementAttach', root, function(parent, x, y, z, rx, ry, rz) "
          "events[#events + 1] = {'attach', parent, x, y, z, rx, ry, rz} end) "
          "addEventHandler('onClientElementDetach', root, function(parent, x, y, z, rx, ry, rz) "
          "events[#events + 1] = {'detach', parent, x, y, z, rx, ry, rz} end)");
    SetGlobalElement("obj", kObject);
    SetGlobalElement("veh", kVehicle);

    // ATTACH_ELEMENTS: attached-to id, position floats, rotation radians.
    Check(f.Send(EPkt(Rpc::AttachElements, kObject).Id(kVehicle).F(1.f).F(2.f).F(3.f).F(0.f).F(0.f).F(1.5707964f).w),
          "ATTACH_ELEMENTS decodes");
    f.Pump();
    f.Lua("assert(getElementAttachedTo(obj) == veh, 'parent')");
    f.Lua("local x, y, z, rx, ry, rz = getElementAttachedOffsets(obj) "
          "assert(x == 1 and y == 2 and z == 3 and math.abs(rz - 90) < 0.01, 'offsets')");
    f.Lua("assert(#events == 1 and events[1][1] == 'attach' and events[1][2] == veh and events[1][5] == 3 "
          "and math.abs(events[1][8] - 90) < 0.01, 'event in degrees')");
    Check(ServerWorld::CopyAttachedElements(kVehicle).size() == 1, "the vehicle lists the attached object");

    // SET_ELEMENT_ATTACHED_OFFSETS: floats in radians, the parent stays.
    Check(f.Send(EPkt(Rpc::SetElementAttachedOffsets, kObject).F(4.f).F(5.f).F(6.f).F(3.1415927f).F(0.f).F(0.f).w),
          "SET_ELEMENT_ATTACHED_OFFSETS decodes");
    f.Pump();
    f.Lua("local x, y, z, rx = getElementAttachedOffsets(obj) assert(x == 4 and z == 6 and math.abs(rx - 180) < 0.01, 'new offsets')");
    f.Lua("assert(getElementAttachedTo(obj) == veh, 'parent kept')");
    Check(!f.Send(EPkt(Rpc::SetElementAttachedOffsets, kObject).F(4.f).F(5.f).F(6.f).F(0.f).w),
          "a short offsets packet is malformed");
    Check(!f.Send(EPkt(Rpc::SetElementAttachedOffsets, kObject).F(200000.f).F(0.f).F(0.f).F(0.f).F(0.f).F(0.f).w),
          "an offset past SYNC_POSITION_LIMIT is rejected");

    // An unknown attached-to element drops the packet.
    Check(f.Send(EPkt(Rpc::AttachElements, kObject).Id(777).F(0.f).F(0.f).F(0.f).F(0.f).F(0.f).F(0.f).w),
          "ATTACH_ELEMENTS to an unknown element is consumed");
    f.Pump();
    f.Lua("assert(getElementAttachedTo(obj) == veh and #events == 1, 'unknown parent changes nothing')");
    Check(!f.Send(EPkt(Rpc::AttachElements, kObject).Id(kVehicle).F(1.f).F(2.f).w), "a short ATTACH is malformed");

    // A handler that cancels the event keeps the element detached from the new parent.
    f.Lua("addEventHandler('onClientElementAttach', root, function() cancelEvent() end)");
    AddObject(kObject2, 1337);
    SetGlobalElement("obj2", kObject2);
    f.Send(EPkt(Rpc::AttachElements, kObject2).Id(kVehicle).F(0.f).F(0.f).F(0.f).F(0.f).F(0.f).F(0.f).w);
    f.Pump();
    f.Lua("assert(getElementAttachedTo(obj2) == false, 'cancelled attach')");

    // DETACH_ELEMENTS: time context, then position and rotation floats; no attached-to id is read.
    Check(f.Send(EPkt(Rpc::DetachElements, kObject).U8(7).F(50.f).F(60.f).F(70.f).F(0.f).F(0.f).F(0.f).w),
          "DETACH_ELEMENTS decodes");
    f.Pump();
    f.Lua("assert(getElementAttachedTo(obj) == false, 'detached')");
    Check(ServerWorld::CopyAttachedElements(kVehicle).empty(), "the vehicle lists nothing after the detach");
    float x = 0, y = 0, z = 0;
    Check(ServerWorld::ElementPose(kObject, x, y, z) && Near(x, 50) && Near(y, 60) && Near(z, 70), "a non-zero position is applied");
    f.Lua("local e = events[#events] assert(e[1] == 'detach' and e[2] == nil and e[3] == 50 and e[4] == 60, 'detach event')");
    Check(!f.Send(EPkt(Rpc::DetachElements, kObject).w), "DETACH without a time context is malformed");
    // A truncated tail is read as zeros (the reads are unchecked on desktop): detach still happens, no move.
    f.Send(EPkt(Rpc::AttachElements, kObject).Id(kVehicle).F(0.f).F(0.f).F(0.f).F(0.f).F(0.f).F(0.f).w);
    f.Lua("events = {}");
    f.Pump();
    Check(f.Send(EPkt(Rpc::DetachElements, kObject).U8(8).w), "DETACH with no payload after the context");
    f.Pump();
    Check(ServerWorld::ElementPose(kObject, x, y, z) && Near(x, 50), "a zero position leaves the element in place");
}

void Vehicle() {
    Fixture f;
    AddVehicle(kVehicle);
    AddObject(kObject, 1338);
    // FIX_VEHICLE: time context. SetHealth(1000) is the hooked step.
    Check(f.Send(EPkt(Rpc::FixVehicle, kVehicle).U8(3).w), "FIX_VEHICLE decodes");
    Check(Headless::Probe().vehicleHealthCalls == 1 && Headless::Probe().vehicleHealthId == kVehicle &&
              Near(Headless::Probe().vehicleHealth, 1000.f),
          "FIX_VEHICLE sets full health");
    Check(!f.Send(EPkt(Rpc::FixVehicle, kVehicle).w), "FIX_VEHICLE without a context is malformed");
    // BLOW_VEHICLE: time context, with-explosion bit.
    Check(f.Send(EPkt(Rpc::BlowVehicle, kVehicle).U8(4).Bit(true).w), "BLOW_VEHICLE decodes");
    Check(Headless::Probe().vehicleHealthCalls == 2 && Near(Headless::Probe().vehicleHealth, 0.f), "BLOW_VEHICLE zeroes health");
    Check(!f.Send(EPkt(Rpc::BlowVehicle, kVehicle).U8(4).w), "BLOW_VEHICLE without the explosion bit is malformed");
    // A non-vehicle source is read and ignored.
    Check(f.Send(EPkt(Rpc::FixVehicle, kObject).U8(3).w), "FIX_VEHICLE on an object is consumed");
    Check(Headless::Probe().vehicleHealthCalls == 2, "FIX_VEHICLE on an object changes nothing");
}

void Objects() {
    Fixture f;
    AddObject(kObject, 1338);
    float pos[3], rot[3], scale[3];
    bool moving = false;

    // SET_OBJECT_ROTATION: three float radians.
    Check(f.Send(EPkt(Rpc::SetObjectRotation, kObject).F(0.1f).F(0.2f).F(0.3f).w), "SET_OBJECT_ROTATION decodes");
    f.Pump();
    Check(Script::CopyObjectState(kObject, pos, rot, scale, moving) && Near(rot[0], 0.1) && Near(rot[2], 0.3), "rotation applied");
    Check(!f.Send(EPkt(Rpc::SetObjectRotation, kObject).F(0.1f).w), "a short rotation is malformed");

    // SET_OBJECT_SCALE: x, then y and z default to x when the packet ends.
    Check(f.Send(EPkt(Rpc::SetObjectScale, kObject).F(2.f).w), "SET_OBJECT_SCALE with one float");
    f.Pump();
    Script::CopyObjectState(kObject, pos, rot, scale, moving);
    Check(Near(scale[0], 2) && Near(scale[1], 2) && Near(scale[2], 2), "uniform scale");
    Check(f.Send(EPkt(Rpc::SetObjectScale, kObject).F(1.f).F(2.f).F(3.f).w), "SET_OBJECT_SCALE with three floats");
    f.Pump();
    Script::CopyObjectState(kObject, pos, rot, scale, moving);
    Check(Near(scale[0], 1) && Near(scale[1], 2) && Near(scale[2], 3), "per-axis scale");
    Check(!f.Send(EPkt(Rpc::SetObjectScale, kObject).w), "an empty scale packet is malformed");

    // MOVE_OBJECT, duration form, linear.
    EPkt move(Rpc::MoveObject, kObject);
    move.Bit(false);
    Compressed(move, 60000);
    Fixed(move, 1.f, 2.f, 3.f).F(0.f).F(0.f).F(0.f);
    Fixed(move, 11.f, 22.f, 33.f).Bit(false).F(0.f).F(0.f).F(1.f);
    EasingString(move, "Linear");
    Double(move, 0.3); Double(move, 1.0); Double(move, 1.70158);
    Check(f.Send(move.w), "MOVE_OBJECT decodes");
    f.Pump();
    Check(Script::CopyObjectState(kObject, pos, rot, scale, moving) && moving, "the object is moving");
    Check(Near(pos[0], 1, 0.5) && Near(pos[2], 3, 0.5), "the move starts at the source");
    // A move cut anywhere is malformed and changes nothing.
    for (size_t cut = 8 + 8 + 17 + 1; cut < move.w.GetBitLength(); cut += 37) {
        Check(!SendTruncated(f, move, cut), "a truncated MOVE_OBJECT is malformed");
    }
    // STOP_OBJECT: position and rotation floats; the move ends at those values.
    Check(f.Send(EPkt(Rpc::StopObject, kObject).F(5.f).F(6.f).F(7.f).F(0.5f).F(0.f).F(0.f).w), "STOP_OBJECT decodes");
    f.Pump();
    Script::CopyObjectState(kObject, pos, rot, scale, moving);
    Check(!moving && Near(pos[0], 5) && Near(pos[1], 6) && Near(pos[2], 7) && Near(rot[0], 0.5), "STOP_OBJECT places the object");
    Check(!f.Send(EPkt(Rpc::StopObject, kObject).F(5.f).F(6.f).w), "a short STOP_OBJECT is malformed");

    // MOVE_OBJECT, resume form with nothing left, delta rotation: the final value is applied at once.
    EPkt done(Rpc::MoveObject, kObject);
    done.Bit(true);
    Compressed(done, 500);
    Compressed(done, 0);
    Fixed(done, 0.f, 0.f, 0.f).F(1.f).F(0.f).F(0.f);
    Fixed(done, 9.f, 8.f, 7.f).Bit(true).F(0.5f).F(0.f).F(0.f);
    EasingString(done, "Bogus");
    Double(done, 0.3); Double(done, 1.0); Double(done, 1.70158);
    Check(f.Send(done.w), "MOVE_OBJECT resume form decodes");
    f.Pump();
    Script::CopyObjectState(kObject, pos, rot, scale, moving);
    Check(!moving && Near(pos[0], 9) && Near(pos[1], 8) && Near(rot[0], 1.5), "a finished move snaps to source + delta");

    // A resume move already part way keeps its elapsed time.
    EPkt part(Rpc::MoveObject, kObject);
    part.Bit(true);
    Compressed(part, 30000);
    Compressed(part, 30000);
    Fixed(part, 0.f, 0.f, 0.f).F(0.f).F(0.f).F(0.f);
    Fixed(part, 100.f, 0.f, 0.f).Bit(false).F(0.f).F(0.f).F(0.f);
    EasingString(part, "Linear");
    Double(part, 0.3); Double(part, 1.0); Double(part, 1.70158);
    Check(f.Send(part.w), "MOVE_OBJECT part way decodes");
    f.Pump();
    Script::CopyObjectState(kObject, pos, rot, scale, moving);
    Check(moving && Near(pos[0], 50, 2), "a resumed move starts half way");

    // Unknown ids and non-object sources are consumed without effect.
    Check(f.Send(EPkt(Rpc::SetObjectScale, 4000).F(9.f).w), "SET_OBJECT_SCALE on an unknown element");
    f.Pump();

    // Non-finite floats and bogus oversized easing lengths are rejected as malformed.
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    Check(!f.Send(EPkt(Rpc::SetObjectRotation, kObject).F(nan).F(0.f).F(0.f).w), "NaN object rotation rejected");
    Check(!f.Send(EPkt(Rpc::StopObject, kObject).F(0.f).F(inf).F(0.f).F(0.f).F(0.f).F(0.f).w), "Inf STOP_OBJECT rejected");
    Check(!f.Send(EPkt(Rpc::SetObjectScale, kObject).F(nan).w), "NaN scale x rejected");
    Check(!f.Send(EPkt(Rpc::SetObjectScale, kObject).F(1.f).F(inf).w), "Inf scale y rejected");
    Check(!f.Send(EPkt(Rpc::AttachElements, kObject).Id(ServerWorld::LocalPlayer()).F(nan).F(0.f).F(0.f).F(0.f).F(0.f).F(0.f).w),
          "NaN ATTACH_ELEMENTS rejected");
    Check(!f.Send(EPkt(Rpc::SetElementAttachedOffsets, kObject).F(0.f).F(0.f).F(0.f).F(inf).F(0.f).F(0.f).w),
          "Inf SET_ELEMENT_ATTACHED_OFFSETS rejected");
    Check(!f.Send(EPkt(Rpc::DetachElements, kObject).U8(1).F(0.f).F(0.f).F(0.f).F(nan).F(0.f).F(0.f).w),
          "NaN DETACH_ELEMENTS rotation rejected");
    EPkt badMove(Rpc::MoveObject, kObject);
    badMove.Bit(false);
    Compressed(badMove, 1000);
    Fixed(badMove, 0.f, 0.f, 0.f).F(0.f).F(0.f).F(0.f);
    Fixed(badMove, 1.f, 0.f, 0.f).Bit(false).F(0.f).F(0.f).F(0.f);
    badMove.U16(65535);
    Check(!f.Send(badMove.w), "oversized MOVE_OBJECT easing length rejected without over-allocation");
}

void QueueAndSession() {
    Fixture f;
    for (int i = 0; i < 600; ++i) f.Send(Pkt(Rpc::SetPlayerMoney).I32(i).w);
    Check(ServerRpc::Pending() <= 512, "the queue is bounded");
    f.Pump();
    Check(ServerRpc::Pending() == 0, "Pump drains the queue");
    Check(LuaNumber(f, "getPlayerMoney()") == 511, "operations run in arrival order, the overflow is dropped");
    Check(f.Send(Pkt(Rpc::SetCameraTarget).U8(17).Id(ServerWorld::LocalPlayer()).w), "SET_CAMERA_TARGET bumps context");
    Check(ServerRpc::CameraSyncTimeContext() == 17, "camera context stored");
    f.Send(Pkt(Rpc::SetPlayerMoney).I32(9).w);
    ServerWorld::ResetSession();
    Check(ServerRpc::Pending() == 0, "a new session drops the queued operations");
    Check(ServerRpc::CameraSyncTimeContext() == 0, "a new session resets camera sync context");
}

void ExtendedGlobalAndElementRpcs() {
    Fixture f;
    // Global world & player RPCs
    Check(f.Send(Pkt(Rpc::SetWantedLevel).U8(4).w), "SET_WANTED_LEVEL");
    Check(f.Send(Pkt(Rpc::SetSkyGradient).U8(10).U8(20).U8(30).U8(40).U8(50).U8(60).w), "SET_SKY_GRADIENT");
    Check(f.Send(Pkt(Rpc::SetFpsLimit).U16(60).w), "SET_FPS_LIMIT");
    Check(f.Send(Pkt(Rpc::SetGarageOpen).U8(5).U8(1).w), "SET_GARAGE_OPEN");
    Check(f.Send(Pkt(Rpc::SetWaterColor).U8(11).U8(22).U8(33).U8(144).w), "SET_WATER_COLOR");
    {
        Pkt p(Rpc::SetTrafficLightState);
        uint8_t st = 3;
        p.w.WriteBits(&st, 4);
        p.Bit(true);
        Check(f.Send(p.w), "SET_TRAFFIC_LIGHT_STATE");
    }
    {
        Pkt p(Rpc::SetWorldWaterLevel);
        p.F(12.5f).Bit(true).Bit(true).Bit(false);
        Check(f.Send(p.w), "SET_WORLD_WATER_LEVEL");
    }
    {
        Pkt p(Rpc::RemoveWorldModel);
        p.U16(1234).F(50.f).F(100.f).F(200.f).F(15.f).U8(0);
        Check(f.Send(p.w), "REMOVE_WORLD_MODEL");
    }
    f.Pump();

    Check(LuaNumber(f, "getPlayerWantedLevel()") == 4, "wanted level applied");
    f.Lua("local tr, tg, tb, br, bg, bb = getSkyGradient() assert(tr == 10 and tg == 20 and tb == 30 and br == 40 and bg == 50 and bb == 60)");
    Check(LuaNumber(f, "getFPSLimit()") == 60, "fps limit applied");
    f.Lua("assert(isGarageOpen(5) == true, 'garage opened')");
    f.Lua("local r, g, b, a = getWaterColor() assert(r == 11 and g == 22 and b == 33 and a == 144)");
    Check(LuaNumber(f, "getTrafficLightState()") == 3, "traffic light state applied");

    Check(f.Send(Pkt(Rpc::ResetSkyGradient).w), "RESET_SKY_GRADIENT");
    Check(f.Send(Pkt(Rpc::ResetWaterColor).w), "RESET_WATER_COLOR");
    Check(f.Send(Pkt(Rpc::ResetWorldWaterLevel).w), "RESET_WORLD_WATER_LEVEL");
    Check(f.Send(Pkt(Rpc::RestoreAllWorldModels).w), "RESTORE_ALL_WORLD_MODELS");
    f.Pump();

    // Element nametag & name/parent RPCs on local player
    ServerWorld::SetLocalPlayer(10);
    const uint32_t local = ServerWorld::LocalPlayer();
    SetGlobalElement("lp", local);
    {
        EPkt p(Rpc::SetPlayerNametagText, local);
        EasingString(p, "HeroTag");
        Check(f.Send(p.w), "SET_PLAYER_NAMETAG_TEXT");
    }
    Check(f.Send(EPkt(Rpc::SetPlayerNametagColor, local).U8(99).U8(88).U8(77).w), "SET_PLAYER_NAMETAG_COLOR");
    Check(f.Send(EPkt(Rpc::SetPlayerNametagShowing, local).U8(0).w), "SET_PLAYER_NAMETAG_SHOWING");
    {
        EPkt p(Rpc::SetElementName, local);
        EasingString(p, "hero_elem");
        Check(f.Send(p.w), "SET_ELEMENT_NAME");
    }
    f.Pump();
    f.Lua("assert(getPlayerNametagText(lp) == 'HeroTag', 'nametag text')");
    f.Lua("local r, g, b = getPlayerNametagColor(lp) assert(r == 99 and g == 88 and b == 77, 'nametag color')");
    f.Lua("assert(isPlayerNametagShowing(lp) == false, 'nametag showing false')");
    f.Lua("assert(getElementID(lp) == 'hero_elem', 'element name updated')");

    // Element RPCs for blip, radar area, marker, team
    {
        auto addSimple = [&](uint32_t id, uint8_t type, auto writeBody) {
            WorldRPG::Net::BitStreamWriter w;
            w.WriteCompressed(1u, 4);
            Bits(w, id, 17);
            w.Write(type);
            Bits(w, 0, 17);
            w.Write(uint8_t(0));
            w.WriteCompressed(0u, 2);
            Bits(w, 0, 1);  // not attached
            Bits(w, 1, 1);  // collisions
            w.WriteCompressed(0u, 2);  // customData count = 0
            w.WriteCompressed(0u, 2);  // name length = 0
            writeBody(w);
            ServerWorld::OnEntityAdd(w.GetData(), w.GetByteLength(), w.GetBitLength());
        };
        addSimple(3100, 5, [](WorldRPG::Net::BitStreamWriter& w) {
            // Blip: SPositionSync (24,24,f32), ordering compressed(2), visibleDistance (14), icon(6), size(5), color(32)
            Bits(w, 1024, 24); Bits(w, 2048, 24); w.Write(5.f);
            w.WriteCompressed(0u, 2);
            Bits(w, 500, 14);
            Bits(w, 1, 6);
            Bits(w, 2, 5);
            w.Write(uint8_t(255)); w.Write(uint8_t(0)); w.Write(uint8_t(0)); w.Write(uint8_t(255));
        });
        addSimple(3101, 7, [](WorldRPG::Net::BitStreamWriter& w) {
            w.Write(10.f); w.Write(20.f); w.Write(30.f); w.Write(40.f);
            w.Write(uint8_t(1)); w.Write(uint8_t(2)); w.Write(uint8_t(3)); w.Write(uint8_t(4));
            Bits(w, 0, 1);
        });
        addSimple(3103, 13, [](WorldRPG::Net::BitStreamWriter& w) {
            w.WriteCompressed(4u, 2);
            w.WriteBits(reinterpret_cast<const uint8_t*>("Blue"), 32);
            w.Write(uint8_t(0)); w.Write(uint8_t(0)); w.Write(uint8_t(255));
            Bits(w, 0, 1);
            w.Write(uint32_t(0));
        });
    }
    SetGlobalElement("bl", 3100);
    SetGlobalElement("ra", 3101);
    SetGlobalElement("tm", 3103);
    Check(f.Send(EPkt(Rpc::SetBlipIcon, 3100).Bits(12, 6).w), "SET_BLIP_ICON");
    Check(f.Send(EPkt(Rpc::SetBlipSize, 3100).Bits(4, 5).w), "SET_BLIP_SIZE");
    Check(f.Send(EPkt(Rpc::SetBlipColor, 3100).U8(10).U8(20).U8(30).U8(240).w), "SET_BLIP_COLOR");
    {
        EPkt p(Rpc::SetBlipOrdering, 3100);
        p.w.WriteCompressed(7u, 2);
        Check(f.Send(p.w), "SET_BLIP_ORDERING");
    }
    {
        EPkt p(Rpc::SetBlipVisibleDistance, 3100);
        Bits(p.w, 1234, 14);
        Check(f.Send(p.w), "SET_BLIP_VISIBLE_DISTANCE");
    }
    Check(f.Send(EPkt(Rpc::SetRadarAreaSize, 3101).F(55.f).F(66.f).w), "SET_RADAR_AREA_SIZE");
    Check(f.Send(EPkt(Rpc::SetRadarAreaColor, 3101).U8(9).U8(8).U8(7).U8(200).w), "SET_RADAR_AREA_COLOR");
    Check(f.Send(EPkt(Rpc::SetRadarAreaFlashing, 3101).Bit(true).w), "SET_RADAR_AREA_FLASHING");
    {
        EPkt p(Rpc::SetTeamName, 3103);
        EasingString(p, "Gold");
        Check(f.Send(p.w), "SET_TEAM_NAME");
    }
    Check(f.Send(EPkt(Rpc::SetTeamColor, 3103).U8(255).U8(215).U8(0).w), "SET_TEAM_COLOR");
    Check(f.Send(EPkt(Rpc::SetTeamFriendlyFire, 3103).Bit(true).w), "SET_TEAM_FRIENDLY_FIRE");
    Check(f.Send(EPkt(Rpc::SetPlayerTeam, local).Id(3103).w), "SET_PLAYER_TEAM");
    f.Pump();

    f.Lua("assert(getBlipIcon(bl) == 12 and getBlipSize(bl) == 4, 'blip icon/size')");
    f.Lua("local r, g, b, a = getBlipColor(bl) assert(r == 10 and g == 20 and b == 30 and a == 240, 'blip color')");
    f.Lua("assert(getBlipOrdering(bl) == 7 and getBlipVisibleDistance(bl) == 1234, 'blip ordering/distance')");
    f.Lua("local w, h = getRadarAreaSize(ra) assert(w == 55 and h == 66, 'radar size')");
    f.Lua("local r, g, b, a = getRadarAreaColor(ra) assert(r == 9 and g == 8 and b == 7 and a == 200, 'radar color')");
    f.Lua("assert(isRadarAreaFlashing(ra) == true, 'radar flashing')");
    f.Lua("assert(getTeamName(tm) == 'Gold', 'team name')");
    f.Lua("local r, g, b = getTeamColor(tm) assert(r == 255 and g == 215 and b == 0, 'team color')");
    f.Lua("assert(getTeamFriendlyFire(tm) == true, 'team friendly fire')");
    f.Lua("assert(getPlayerTeam(lp) == tm, 'player team set')");

    Check(f.Send(Pkt(Rpc::DestroyAllBlips).w), "DESTROY_ALL_BLIPS");
    Check(f.Send(Pkt(Rpc::DestroyAllRadarAreas).w), "DESTROY_ALL_RADAR_AREAS");
    f.Pump();
    Check(ServerWorld::CountKind(5) == 0, "all blips destroyed");
    Check(ServerWorld::CountKind(7) == 0, "all radar areas destroyed");
}

void AllRemaining84Rpcs() {
    Fixture f;
    constexpr uint32_t local = 500;
    constexpr uint32_t vehId = 501;
    constexpr uint32_t objId = 502;
    f.Lua("g_rpcEvents = {}\n"
          "addEventHandler('onClientTransferBoxVisibilityChange', root, function(v) g_rpcEvents.transfer = v end)\n"
          "addEventHandler('onClientVehicleNitroStateChange', root, function(s) g_rpcEvents.nitro = s end)\n"
          "addEventHandler('onClientPlayerWeaponReload', root, function() g_rpcEvents.reloaded = true end)\n");
    ServerWorld::SetLocalPlayer(local);
    AddVehicle(vehId);
    AddObject(objId, 1338);
    SetGlobalElement("lp", local);
    SetGlobalElement("veh", vehId);
    SetGlobalElement("obj", objId);

    // ---- All 18 remaining global RPCs ----
    Check(f.Send(Pkt(Rpc::ForcePlayerMap).U8(1).w), "FORCE_PLAYER_MAP");
    Check(f.Send(Pkt(Rpc::ForceReconnect).Str("127.0.0.1").U16(22003).Str("pw").w), "FORCE_RECONNECT");
    Check(f.Send(Pkt(Rpc::ResetMapInfo).w), "RESET_MAP_INFO");
    Check(f.Send(Pkt(Rpc::ToggleDebugger).U8(1).w), "TOGGLE_DEBUGGER");
    Check(f.Send(Pkt(Rpc::SetAllElementWaterLevel).F(3.5f).w), "SET_ALL_ELEMENT_WATER_LEVEL");
    Check(f.Send(Pkt(Rpc::SetWeaponProperty).U8(24).U8(4).U8(1).U16(99).w), "SET_WEAPON_PROPERTY damage");
    Check(f.Send(Pkt(Rpc::SetWeaponProperty).U8(24).U8(1).U8(1).F(55.f).w), "SET_WEAPON_PROPERTY range");
    {
        Pkt p(Rpc::TakePlayerScreenShot);
        p.U16(320).U16(240);
        EasingString(p, "tag1");
        p.U8(75).U32(50000).U16(1024).U16(1).U32(1000);
        Check(f.Send(p.w), "TAKE_PLAYER_SCREEN_SHOT");
    }
    {
        Pkt p(Rpc::SetSyncIntervals);
        for (int i = 0; i < 10; ++i) p.U32(100 + i);
        Check(f.Send(p.w), "SET_SYNC_INTERVALS");
    }
    Check(f.Send(Pkt(Rpc::SetJetpackWeaponEnabled).U8(28).Bit(true).w), "SET_JETPACK_WEAPON_ENABLED");
    Check(f.Send(Pkt(Rpc::SetDiscordJoinParameters).w), "SET_DISCORD_JOIN_PARAMETERS");
    Check(f.Send(Pkt(Rpc::SetTransferBoxVisibility).Bit(true).w), "SET_TRANSFER_BOX_VISIBILITY");
    Check(f.Send(Pkt(Rpc::SetWorldSpecialProperty).U8(0).Bit(true).w), "SET_WORLD_SPECIAL_PROPERTY");
    Check(f.Send(Pkt(Rpc::SetPlayerScriptDebugLevel).U8(3).w), "SET_PLAYER_SCRIPT_DEBUG_LEVEL");
    {
        Pkt p(Rpc::ResetWorldProperties);
        p.Bit(false).Bit(true).Bit(true).Bit(true).Bit(true);
        Check(f.Send(p.w), "RESET_WORLD_PROPERTIES");
    }
    Check(f.Send(Pkt(Rpc::SetWorldSeabedOuterBoundary).F(4500.f).w), "SET_WORLD_SEABED_OUTER_BOUNDARY");
    {
        Pkt p(Rpc::AllocateServerModel);
        p.U16(20001).U16(411).U8(0);
        EasingString(p, "custom_car");
        Check(f.Send(p.w), "ALLOCATE_SERVER_MODEL");
    }
    Check(f.Send(Pkt(Rpc::FreeServerModel).U16(20001).w), "FREE_SERVER_MODEL");
    Check(f.Send(Pkt(Rpc::ToggleDevTools).U8(1).w), "TOGGLE_DEVTOOLS");
    f.Pump();

    f.Lua("assert(isPlayerMapForced() == true, 'forcePlayerMap')");
    f.Lua("assert(getPlayerScriptDebugLevel() == 3, 'scriptDebugLevel')");
    f.Lua("assert(isTransferBoxVisible() == true and g_rpcEvents.transfer == true, 'transferBox')");
    f.Lua("assert(isWorldSpecialPropertyEnabled('hovercars') == true, 'worldSpecialProp')");
    f.Lua("assert(getWeaponProperty(24, 'std', 'damage') == 99, 'weaponProperty damage')");
    f.Lua("assert(math.abs(getWeaponProperty(24, 'std', 'weapon_range') - 55) < 0.01, 'weaponProperty range')");

    // ---- All 11 Ped RPCs ----
    Check(f.Send(EPkt(Rpc::GivePedJetpack, local).w), "GIVE_PED_JETPACK");
    f.Pump();
    f.Lua("assert(doesPedHaveJetPack(lp) == true, 'givePedJetpack')");
    Check(f.Send(EPkt(Rpc::RemovePedJetpack, local).w), "REMOVE_PED_JETPACK");
    Check(f.Send(EPkt(Rpc::RemovePedClothes, local).U8(0).w), "REMOVE_PED_CLOTHES");
    Check(f.Send(EPkt(Rpc::SetPedGravity, local).F(0.012f).w), "SET_PED_GRAVITY");
    Check(f.Send(EPkt(Rpc::SetPedChoking, local).Bit(true).w), "SET_PED_CHOKING");
    Check(f.Send(EPkt(Rpc::SetPedFightingStyle, local).U8(5).w), "SET_PED_FIGHTING_STYLE");
    {
        EPkt p(Rpc::SetPedMoveAnim, local);
        p.w.WriteCompressed(118u, 4);
        Check(f.Send(p.w), "SET_PED_MOVE_ANIM");
    }
    Check(f.Send(EPkt(Rpc::SetPedOnFire, local).Bit(true).w), "SET_PED_ON_FIRE");
    Check(f.Send(EPkt(Rpc::SetPedHeadless, local).Bit(true).w), "SET_PED_HEADLESS");
    Check(f.Send(EPkt(Rpc::SetPedFrozen, local).Bit(true).w), "SET_PED_FROZEN");
    Check(f.Send(EPkt(Rpc::ReloadPedWeapon, local).w), "RELOAD_PED_WEAPON");
    f.Pump();
    f.Lua("assert(doesPedHaveJetPack(lp) == false, 'removePedJetpack')");
    f.Lua("assert(isPedChoking(lp) == true, 'pedChoking')");
    f.Lua("assert(isPedOnFire(lp) == true, 'pedOnFire')");
    f.Lua("assert(isPedHeadless(lp) == true, 'pedHeadless')");
    f.Lua("assert(g_rpcEvents.reloaded == true, 'reloadPedWeapon')");

    // ---- All 31 Vehicle & Handling RPCs ----
    Check(f.Send(EPkt(Rpc::SetVehicleDoorsUndamageable, vehId).U8(1).w), "SET_VEHICLE_DOORS_UNDAMAGEABLE");
    Check(f.Send(EPkt(Rpc::SetVehicleSireneOn, vehId).U8(1).w), "SET_VEHICLE_SIRENE_ON");
    Check(f.Send(EPkt(Rpc::SetVehicleLandingGearDown, vehId).U8(0).w), "SET_VEHICLE_LANDING_GEAR_DOWN");
    Check(f.Send(EPkt(Rpc::SetHelicopterRotorSpeed, vehId).U8(255).w), "SET_HELICOPTER_ROTOR_SPEED");
    Check(f.Send(EPkt(Rpc::SetVehicleDamageState, vehId).U8(0).U8(2).U8(3).Bit(false).w), "SET_VEHICLE_DAMAGE_STATE");
    Check(f.Send(EPkt(Rpc::SetVehicleOverrideLights, vehId).U8(2).w), "SET_VEHICLE_OVERRIDE_LIGHTS");
    Check(f.Send(EPkt(Rpc::SetVehicleDirtLevel, vehId).F(7.5f).w), "SET_VEHICLE_DIRT_LEVEL");
    Check(f.Send(EPkt(Rpc::SetVehicleDamageProof, vehId).U8(1).w), "SET_VEHICLE_DAMAGE_PROOF");
    Check(f.Send(EPkt(Rpc::SetVehiclePaintjob, vehId).U8(2).w), "SET_VEHICLE_PAINTJOB");
    Check(f.Send(EPkt(Rpc::SetVehicleFuelTankExplodable, vehId).Bit(false).w), "SET_VEHICLE_FUEL_TANK_EXPLODABLE");
    Check(f.Send(EPkt(Rpc::SetVehicleWheelStates, vehId).U8(1).U8(2).U8(0).U8(1).w), "SET_VEHICLE_WHEEL_STATES");
    Check(f.Send(EPkt(Rpc::SetTrainDerailed, vehId).Bit(true).w), "SET_TRAIN_DERAILED");
    Check(f.Send(EPkt(Rpc::SetTrainDerailable, vehId).Bit(false).w), "SET_TRAIN_DERAILABLE");
    Check(f.Send(EPkt(Rpc::SetTrainDirection, vehId).Bit(true).w), "SET_TRAIN_DIRECTION");
    Check(f.Send(EPkt(Rpc::SetTrainSpeed, vehId).F(1.25f).w), "SET_TRAIN_SPEED");
    Check(f.Send(EPkt(Rpc::SetTaxiLightOn, vehId).U8(1).w), "SET_TAXI_LIGHT_ON");
    Check(f.Send(EPkt(Rpc::SetVehicleHeadlightColor, vehId).U8(10).U8(20).U8(30).w), "SET_VEHICLE_HEADLIGHT_COLOR");
    {
        EPkt p(Rpc::SetVehicleDoorOpenRatio, vehId);
        p.Bits(1, 3).Bit(false).Bit(true);
        p.w.WriteCompressed(250u, 4);
        Check(f.Send(p.w), "SET_VEHICLE_DOOR_OPEN_RATIO");
    }
    Check(f.Send(EPkt(Rpc::SetVehicleTurretPosition, vehId).F(0.5f).F(0.25f).w), "SET_VEHICLE_TURRET_POSITION");
    Check(f.Send(EPkt(Rpc::SetVehicleVariant, vehId).U8(1).U8(2).w), "SET_VEHICLE_VARIANT");
    Check(f.Send(EPkt(Rpc::GiveVehicleSirens, vehId).Bit(true).U8(2).U8(4).Bit(true).Bit(true).Bit(false).Bit(false).w),
          "GIVE_VEHICLE_SIRENS");
    f.Lua("assert(getVehicleSirensOn(veh) == true, 'sirensOn')");
    Check(f.Send(EPkt(Rpc::SetVehicleSirens, vehId)
                     .Bit(true)
                     .U8(0)
                     .F(0.5f)
                     .F(0.f)
                     .F(1.f)
                     .U8(255)
                     .U8(0)
                     .U8(0)
                     .U8(200)
                     .U32(50)
                     .Bit(true)
                     .Bit(true)
                     .Bit(false)
                     .Bit(false)
                     .w),
          "SET_VEHICLE_SIRENS");
    Check(f.Send(EPkt(Rpc::RemoveVehicleSirens, vehId).w), "REMOVE_VEHICLE_SIRENS");
    {
        EPkt p(Rpc::SetVehiclePlateText, vehId);
        EasingString(p, "NEON99");
        Check(f.Send(p.w), "SET_VEHICLE_PLATE_TEXT");
    }
    Check(f.Send(EPkt(Rpc::SetTrainTrack, vehId).U8(1).w), "SET_TRAIN_TRACK");
    Check(f.Send(EPkt(Rpc::SetTrainPosition, vehId).F(42.f).w), "SET_TRAIN_POSITION");
    Check(f.Send(EPkt(Rpc::SpawnVehicleFlyingComponent, vehId).U8(2).U8(1).U32(5000).w),
          "SPAWN_VEHICLE_FLYING_COMPONENT");
    Check(f.Send(EPkt(Rpc::SetVehicleNitroActivated, vehId).Bit(true).w), "SET_VEHICLE_NITRO_ACTIVATED");
    {
        EPkt p(Rpc::SetVehicleHandling, vehId);
        p.F(1500.f).F(3000.f).F(2.f).F(0.f).F(0.f).F(0.f).U8(75).F(0.75f).U8('R').U8('P').U8(5);
        p.F(10.f).F(10.f).F(200.f).F(8.f).F(0.5f).Bit(false).F(30.f).F(0.85f).F(0.5f);
        p.F(1.2f).F(0.15f).F(0.f).F(0.25f).F(-0.15f).F(0.5f).F(0.3f).F(0.5f).U32(0).U32(0).F(0.2f).U8(0);
        Check(f.Send(p.w), "SET_VEHICLE_HANDLING");
    }
    Check(f.Send(EPkt(Rpc::SetVehicleHandlingProperty, vehId).U8(1).F(1800.f).w), "SET_VEHICLE_HANDLING_PROPERTY");
    Check(f.Send(EPkt(Rpc::ResetVehicleHandlingProperty, vehId).U8(1).w), "RESET_VEHICLE_HANDLING_PROPERTY");
    Check(f.Send(EPkt(Rpc::ResetVehicleHandling, vehId).w), "RESET_VEHICLE_HANDLING");
    f.Pump();

    f.Lua("assert(getVehicleOverrideLights(veh) == 2, 'overrideLights')");
    f.Lua("assert(isVehicleDamageProof(veh) == true, 'damageProof')");
    f.Lua("assert(getVehiclePaintjob(veh) == 2, 'paintjob')");
    f.Lua("assert(getVehiclePlateText(veh) == 'NEON99', 'plateText')");
    f.Lua("local r, g, b = getVehicleHeadLightColor(veh) assert(r == 10 and g == 20 and b == 30, 'headlights')");
    f.Lua("assert(getVehicleDoorOpenRatio(veh, 1) > 0.99, 'doorOpenRatio')");
    f.Lua("assert(g_rpcEvents.nitro == true, 'nitro event')");

    // ---- Water / Object / Marker / Element / Custom Weapon RPCs (24) ----
    Check(f.Send(EPkt(Rpc::SetElementWaterLevel, objId).F(4.f).w), "SET_ELEMENT_WATER_LEVEL");
    Check(f.Send(EPkt(Rpc::SetWaterVertexPosition, objId).U8(0).U16(10).U16(20).F(3.f).w), "SET_WATER_VERTEX_POSITION");
    Check(f.Send(EPkt(Rpc::SetLowLodElement, objId).Id(0).w), "SET_LOW_LOD_ELEMENT");
    Check(f.Send(EPkt(Rpc::SetPropagateCallsEnabled, vehId).Bit(false).w), "SET_PROPAGATE_CALLS_ENABLED");
    Check(f.Send(EPkt(Rpc::SetElementAngularVelocity, vehId).F(0.1f).F(0.2f).F(0.3f).w), "SET_ELEMENT_ANGULAR_VELOCITY");
    Check(f.Send(EPkt(Rpc::SetElementOnFire, local).Bit(false).w), "SET_ELEMENT_ON_FIRE");
    Check(f.Send(EPkt(Rpc::SetObjectVisibleInAllDimensions, objId).Bit(true).U16(5).w),
          "SET_OBJECT_VISIBLE_IN_ALL_DIMENSIONS");
    Check(f.Send(EPkt(Rpc::SetObjectBreakable, objId).Bit(false).w), "SET_OBJECT_BREAKABLE");
    Check(f.Send(EPkt(Rpc::BreakObject, objId).w), "BREAK_OBJECT");
    Check(f.Send(EPkt(Rpc::RespawnObject, objId).w), "RESPAWN_OBJECT");
    Check(f.Send(EPkt(Rpc::ToggleObjectRespawn, objId).Bit(false).w), "TOGGLE_OBJECT_RESPAWN");
    Check(f.Send(EPkt(Rpc::SetObjectDynamicPhysics, objId).Bit(true).w), "SET_OBJECT_DYNAMIC_PHYSICS");
    Check(f.Send(EPkt(Rpc::SetMarkerTargetArrowProperties, objId).U8(1).U8(2).U8(3).U8(255).F(2.5f).w),
          "SET_MARKER_TARGET_ARROW_PROPERTIES");
    Check(f.Send(EPkt(Rpc::FireCustomWeapon, objId).w), "FIRE_CUSTOM_WEAPON");
    Check(f.Send(EPkt(Rpc::SetCustomWeaponState, objId).U8(1).w), "SET_CUSTOM_WEAPON_STATE");
    Check(f.Send(EPkt(Rpc::SetCustomWeaponAmmo, objId).U32(120).w), "SET_CUSTOM_WEAPON_AMMO");
    Check(f.Send(EPkt(Rpc::SetCustomWeaponClipAmmo, objId).U32(30).w), "SET_CUSTOM_WEAPON_CLIP_AMMO");
    Check(f.Send(EPkt(Rpc::SetCustomWeaponTarget, objId).Bit(true).F(1.f).F(2.f).F(3.f).w),
          "SET_CUSTOM_WEAPON_TARGET pos");
    Check(f.Send(EPkt(Rpc::SetCustomWeaponTarget, objId).Bit(false).Id(local).U8(8).w), "SET_CUSTOM_WEAPON_TARGET el");
    Check(f.Send(EPkt(Rpc::ResetCustomWeaponTarget, objId).w), "RESET_CUSTOM_WEAPON_TARGET");
    {
        EPkt p(Rpc::SetCustomWeaponFlags, objId);
        for (int i = 0; i < 13; ++i) p.Bit((i & 1) != 0);
        Check(f.Send(p.w), "SET_CUSTOM_WEAPON_FLAGS");
    }
    Check(f.Send(EPkt(Rpc::SetCustomWeaponFiringRate, objId).U32(150).w), "SET_CUSTOM_WEAPON_FIRING_RATE");
    Check(f.Send(EPkt(Rpc::ResetCustomWeaponFiringRate, objId).w), "RESET_CUSTOM_WEAPON_FIRING_RATE");
    Check(f.Send(EPkt(Rpc::SetWeaponOwner, objId).Id(local).w), "SET_WEAPON_OWNER");
    f.Pump();

    f.Lua("assert(isElementCallPropagationEnabled(veh) == false, 'callProp')");
    f.Lua("assert(isPedOnFire(lp) == false, 'elementOnFire cleared')");
    f.Lua("local rx, ry, rz = getElementAngularVelocity(veh) "
          "assert(math.abs(rx - 0.1) < 0.01 and math.abs(ry - 0.2) < 0.01 and math.abs(rz - 0.3) < 0.01, 'angVel')");
}
}  // namespace

int main() {
    World();
    Camera();
    PlayerHudChat();
    Input();
    Attach();
    Vehicle();
    Objects();
    QueueAndSession();
    ExtendedGlobalAndElementRpcs();
    AllRemaining84Rpcs();
    std::printf("server RPC tests passed (%u checks)\n", checks);
    return 0;
}
