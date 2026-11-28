#include "WorldNativeBridge.h"

namespace WorldRPG::WorldNative {
namespace {
Hooks g_hooks;
}  // namespace

void RegisterHooks(const Hooks& hooks) { g_hooks = hooks; }
const Hooks& CurrentHooks() { return g_hooks; }

int GetPoolCapacity(int pool) {
    return g_hooks.getPoolCapacity ? g_hooks.getPoolCapacity(pool) : -1;
}

int GetPoolUsedCapacity(int pool) {
    return g_hooks.getPoolUsedCapacity ? g_hooks.getPoolUsedCapacity(pool) : -1;
}

bool CreateExplosion(float x, float y, float z, int type, bool makeSound, float camShake, bool damaging) {
    return g_hooks.createExplosion ? g_hooks.createExplosion(x, y, z, type, makeSound, camShake, damaging) : false;
}

bool CreateFire(float x, float y, float z, float size) {
    return g_hooks.createFire ? g_hooks.createFire(x, y, z, size) : false;
}

bool ExtinguishFire(float x, float y, float z, float radius) {
    return g_hooks.extinguishFire ? g_hooks.extinguishFire(x, y, z, radius) : false;
}

bool AddPointLight(int type, float x, float y, float z, float dx, float dy, float dz,
                   float radius, float r, float g, float b, bool fog, bool shadow) {
    return g_hooks.addPointLight
               ? g_hooks.addPointLight(type, x, y, z, dx, dy, dz, radius, r, g, b, fog, shadow)
               : false;
}

bool PreloadWorldArea(float x, float y, float z, int mode) {
    return g_hooks.preloadWorldArea ? g_hooks.preloadWorldArea(x, y, z, mode) : false;
}

bool CreateSwatRope(float x, float y, float z, uint32_t durationMs) {
    return g_hooks.createSwatRope ? g_hooks.createSwatRope(x, y, z, durationMs) : false;
}

bool TestSphereAgainstWorld(float x, float y, float z, float radius,
                            bool checkBuildings, bool checkVehicles, bool checkPeds,
                            bool checkObjects, bool checkDummies, bool cameraIgnore,
                            SphereTestResult& out) {
    return g_hooks.testSphereAgainstWorld
               ? g_hooks.testSphereAgainstWorld(x, y, z, radius, checkBuildings, checkVehicles, checkPeds,
                                                checkObjects, checkDummies, cameraIgnore, out)
               : false;
}

bool GetGarageGeometry(int garageId, float pos[3], float sizeWDH[3], float bbox[4]) {
    return g_hooks.getGarageGeometry ? g_hooks.getGarageGeometry(garageId, pos, sizeWDH, bbox) : false;
}

int RemoveWorldModel(uint16_t model, float radius, float x, float y, float z, int8_t interior) {
    return g_hooks.removeWorldModel ? g_hooks.removeWorldModel(model, radius, x, y, z, interior) : -1;
}

int RestoreWorldModel(uint16_t model, float radius, float x, float y, float z, int8_t interior) {
    return g_hooks.restoreWorldModel ? g_hooks.restoreWorldModel(model, radius, x, y, z, interior) : -1;
}

int RestoreAllWorldModels() {
    return g_hooks.restoreAllWorldModels ? g_hooks.restoreAllWorldModels() : -1;
}

bool SetGameSpeed(float speed) {
    return g_hooks.setGameSpeed ? g_hooks.setGameSpeed(speed) : false;
}

bool SetMinuteDuration(uint32_t ms) {
    return g_hooks.setMinuteDuration ? g_hooks.setMinuteDuration(ms) : false;
}

bool SetWeatherBlended(int weather) {
    return g_hooks.setWeatherBlended ? g_hooks.setWeatherBlended(weather) : false;
}

bool SetRainLevel(float level, bool forced) {
    return g_hooks.setRainLevel ? g_hooks.setRainLevel(level, forced) : false;
}

bool SetWindVelocity(float x, float y, float z, bool reset) {
    return g_hooks.setWindVelocity ? g_hooks.setWindVelocity(x, y, z, reset) : false;
}

bool SetWaveHeight(float height) {
    return g_hooks.setWaveHeight ? g_hooks.setWaveHeight(height) : false;
}

bool SetMoonSize(int size) {
    return g_hooks.setMoonSize ? g_hooks.setMoonSize(size) : false;
}

bool SetBlurLevel(int level) {
    return g_hooks.setBlurLevel ? g_hooks.setBlurLevel(level) : false;
}

bool SetGrainLevel(int level) {
    return g_hooks.setGrainLevel ? g_hooks.setGrainLevel(level) : false;
}

bool SetHeatHaze(int intensity, int randomShift, int speedMin, int speedMax,
                 int scanX, int scanY, int renderX, int renderY, bool showInside, bool reset) {
    return g_hooks.setHeatHaze
               ? g_hooks.setHeatHaze(intensity, randomShift, speedMin, speedMax,
                                     scanX, scanY, renderX, renderY, showInside, reset)
               : false;
}

bool SetFarClipDistance(float distance, bool reset) {
    return g_hooks.setFarClipDistance ? g_hooks.setFarClipDistance(distance, reset) : false;
}

bool SetFogDistance(float distance, bool reset) {
    return g_hooks.setFogDistance ? g_hooks.setFogDistance(distance, reset) : false;
}

bool SetSunSize(float size, bool reset) {
    return g_hooks.setSunSize ? g_hooks.setSunSize(size, reset) : false;
}

bool SetSunColor(int coreR, int coreG, int coreB, int coronaR, int coronaG, int coronaB, bool reset) {
    return g_hooks.setSunColor
               ? g_hooks.setSunColor(coreR, coreG, coreB, coronaR, coronaG, coronaB, reset)
               : false;
}

bool SetSkyGradient(int topR, int topG, int topB, int botR, int botG, int botB, bool reset) {
    return g_hooks.setSkyGradient
               ? g_hooks.setSkyGradient(topR, topG, topB, botR, botG, botB, reset)
               : false;
}

bool SetWaterColor(int r, int g, int b, int a, bool reset) {
    return g_hooks.setWaterColor ? g_hooks.setWaterColor(r, g, b, a, reset) : false;
}

bool SetCloudsEnabled(bool enabled) {
    return g_hooks.setCloudsEnabled ? g_hooks.setCloudsEnabled(enabled) : false;
}

bool SetOcclusionsEnabled(bool enabled) {
    return g_hooks.setOcclusionsEnabled ? g_hooks.setOcclusionsEnabled(enabled) : false;
}

bool SetGarageOpen(int garageId, bool open) {
    return g_hooks.setGarageOpen ? g_hooks.setGarageOpen(garageId, open) : false;
}

bool IsGarageOpen(int garageId, bool& outOpen) {
    return g_hooks.isGarageOpen ? g_hooks.isGarageOpen(garageId, outOpen) : false;
}

bool SetCurrentArea(int area) {
    return g_hooks.setCurrentArea ? g_hooks.setCurrentArea(area) : false;
}

bool FxAddBlood(float x, float y, float z, float dx, float dy, float dz, int count, float brightness) {
    return g_hooks.fxAddBlood ? g_hooks.fxAddBlood(x, y, z, dx, dy, dz, count, brightness) : false;
}

bool FxAddBulletImpact(float x, float y, float z, float dx, float dy, float dz, int smokeSize, int sparkCount, float smokeIntensity) {
    return g_hooks.fxAddBulletImpact
               ? g_hooks.fxAddBulletImpact(x, y, z, dx, dy, dz, smokeSize, sparkCount, smokeIntensity)
               : false;
}

bool FxAddSparks(float x, float y, float z, float dx, float dy, float dz, float force, int count,
                 float acrossX, float acrossY, float acrossZ, bool blur, float spread, float life) {
    return g_hooks.fxAddSparks
               ? g_hooks.fxAddSparks(x, y, z, dx, dy, dz, force, count, acrossX, acrossY, acrossZ, blur, spread, life)
               : false;
}

bool FxAddTyreBurst(float x, float y, float z, float dx, float dy, float dz) {
    return g_hooks.fxAddTyreBurst ? g_hooks.fxAddTyreBurst(x, y, z, dx, dy, dz) : false;
}

bool FxAddWood(float x, float y, float z, float dx, float dy, float dz, int count, float brightness) {
    return g_hooks.fxAddWood ? g_hooks.fxAddWood(x, y, z, dx, dy, dz, count, brightness) : false;
}

bool FxAddPunchImpact(float x, float y, float z, float dx, float dy, float dz) {
    return g_hooks.fxAddPunchImpact ? g_hooks.fxAddPunchImpact(x, y, z, dx, dy, dz) : false;
}

bool FxAddDebris(float x, float y, float z, uint8_t r, uint8_t g, uint8_t b, uint8_t a, float scale, int count) {
    return g_hooks.fxAddDebris ? g_hooks.fxAddDebris(x, y, z, r, g, b, a, scale, count) : false;
}

bool FxAddGlass(float x, float y, float z, uint8_t r, uint8_t g, uint8_t b, uint8_t a, float scale, int count) {
    return g_hooks.fxAddGlass ? g_hooks.fxAddGlass(x, y, z, r, g, b, a, scale, count) : false;
}

}  // namespace WorldRPG::WorldNative
