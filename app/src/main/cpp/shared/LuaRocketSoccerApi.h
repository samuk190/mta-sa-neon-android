#pragma once
// rocketSim* (desktop CLuaRocketSoccerDefs.cpp) over RocketSoccerSimulation.

#include <cstdint>

struct lua_State;

namespace WorldRPG::Script {

struct RocketGamepadInput {
    bool connected = false;
    int index = 0;
    uint16_t warButtons = 0;  // WarGamepad bitmask (BIT_A..BIT_R3)
    float axes[6]{};          // LX, LY, RX, RY, LT, RT
};

void InstallRocketSoccerLua(lua_State* L);
// Updates the Android hardware gamepad state reported by rocketSimGamepad().
void RocketSoccerSetGamepad(const RocketGamepadInput& input);
// Frees the scene a resource start created (desktop: lua_close of that
// resource's VM).
void RocketSoccerDropResource(uint32_t generation);
// Frees every scene (new session or VM).
void RocketSoccerReset();

}  // namespace WorldRPG::Script
