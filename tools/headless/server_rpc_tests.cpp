// Host checks for the server RPCs of PACKET_ID_LUA (76) and PACKET_ID_LUA_ELEMENT_RPC (77) in shared/ServerRpc.cpp:
// wire decoding (truncated, malformed and out of range packets), and the state the Lua getters report afterwards.
// Desktop references: CWorldRPCs.cpp, CCameraRPCs.cpp, CPlayerRPCs.cpp, CAudioRPCs.cpp, CInputRPCs.cpp,
// CElementRPCs.cpp, CVehicleRPCs.cpp, CObjectRPCs.cpp.
#include "game_script.h"
#include "shared/LuaVMStub.h"
#include "shared/MtaIds.h"
#include "shared/PacketHandler.h"
#include "shared/ServerRpc.h"
#include "shared/ServerWorld.h"
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
    Check(!Game::IsHudComponentVisible("money"), "money is hidden");
    Check(f.Send(Pkt(Rpc::ShowPlayerHudComponent).U8(5).U8(7).w), "show is any non-zero byte");
    f.Pump();
    Check(Game::IsHudComponentVisible("money"), "money is shown again");
    Check(f.Send(Pkt(Rpc::ShowPlayerHudComponent).U8(9).U8(0).w), "hide clock");
    f.Pump();
    Check(!Game::IsHudComponentVisible("clock"), "clock is hidden");
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

void QueueAndSession() {
    Fixture f;
    for (int i = 0; i < 600; ++i) f.Send(Pkt(Rpc::SetPlayerMoney).I32(i).w);
    Check(ServerRpc::Pending() <= 512, "the queue is bounded");
    f.Pump();
    Check(ServerRpc::Pending() == 0, "Pump drains the queue");
    Check(LuaNumber(f, "getPlayerMoney()") == 511, "operations run in arrival order, the overflow is dropped");
    f.Send(Pkt(Rpc::SetPlayerMoney).I32(9).w);
    ServerWorld::ResetSession();
    Check(ServerRpc::Pending() == 0, "a new session drops the queued operations");
}
}  // namespace

int main() {
    World();
    Camera();
    PlayerHudChat();
    QueueAndSession();
    std::printf("server RPC tests passed (%u checks)\n", checks);
    return 0;
}
