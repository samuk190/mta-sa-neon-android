#pragma once
#include <cstdint>
struct lua_State;
namespace WorldRPG::Script {
void InstallFieldLua(lua_State* L);
void ApplySpatialArea(uint32_t id,uint64_t generation,uint8_t& interior,uint16_t& dimension);
uint16_t ElementDimensionValue(uint32_t id);
// Interior, dimension, collisions, frozen and alpha state scripts set on a client element (defaults otherwise).
void ClientElementWorldState(uint32_t id, uint8_t& interior, uint16_t& dimension, bool& collisions, bool& frozen,
                             uint8_t& alpha);

// Server world RPCs (CWorldRPCs.cpp). They write the state the Lua getters read. VM thread only; ServerRpc::Pump
// calls them. Engine writes that are not hooked stay DIAG, as in the Lua setters.
void RpcSetWeather(int weather);
void RpcSetWeatherBlended(int weather, int hour);
void RpcSetMinuteDuration(uint32_t milliseconds);
void RpcSetGravity(float gravity);
void RpcSetGameSpeed(float speed);
void RpcSetWaveHeight(float height);
void RpcSetBlurLevel(uint8_t level);
void RpcSetCloudsEnabled(bool enabled);
void RpcSetRainLevel(float level);
void RpcResetRainLevel();
void RpcSetSunSize(float size);
void RpcResetSunSize();
void RpcSetSunColor(uint8_t coreR, uint8_t coreG, uint8_t coreB, uint8_t coronaR, uint8_t coronaG, uint8_t coronaB);
void RpcResetSunColor();
void RpcSetWindVelocity(float x, float y, float z);
void RpcResetWindVelocity();
void RpcSetFarClipDistance(float distance);
void RpcResetFarClipDistance();
void RpcSetFogDistance(float distance);
void RpcResetFogDistance();
void RpcSetMoonSize(int size);
void RpcResetMoonSize();
void RpcSetInteriorSoundsEnabled(bool enabled);
void RpcSetOcclusionsEnabled(bool enabled);
void RpcSetAircraftMaxHeight(float height);
void RpcSetAircraftMaxVelocity(float velocity);
void RpcSetJetpackMaxHeight(float height);
void RpcSetSkyGradient(uint8_t topR, uint8_t topG, uint8_t topB, uint8_t bottomR, uint8_t bottomG, uint8_t bottomB);
void RpcResetSkyGradient();
void RpcSetHeatHaze(uint8_t intensity, uint8_t randomShift, uint16_t speedMin, uint16_t speedMax,
                    int16_t scanSizeX, int16_t scanSizeY, uint16_t renderSizeX, uint16_t renderSizeY,
                    bool showInside);
void RpcResetHeatHaze();
void RpcSetFpsLimit(uint16_t fps);
void RpcSetGarageOpen(uint8_t garageId, bool open);
void RpcSetGlitchEnabled(uint8_t glitchId, bool enabled);
void RpcSetWorldSpecialProperty(int propertyIndex, bool enabled);
void RpcSetTrafficLightState(uint8_t state, bool forced);
void RpcSetWaterColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a);
void RpcResetWaterColor();
void RpcSetElementDoubleSided(uint32_t id, bool doubleSided);
// The corona colour setSunColor does not keep in g_sunR..B (Android getSunColor has four slots).
void RpcCopySunCorona(uint8_t& r, uint8_t& g, uint8_t& b);
void RpcResetMapInfo();
void RpcResetWorldProperties(bool resetSpecial, bool resetWorld, bool resetWeather, bool resetLods, bool resetSounds);
void RpcSetJetpackWeaponEnabled(uint8_t weaponId, bool enabled);
}
