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

bool FadeCamera(bool fadeIn, float duration, uint8_t r, uint8_t g, uint8_t b) {
    return g_hooks.fadeCamera ? g_hooks.fadeCamera(fadeIn, duration, r, g, b) : false;
}

bool ShakeCamera(float strength, float x, float y, float z) {
    return g_hooks.shakeCamera ? g_hooks.shakeCamera(strength, x, y, z) : false;
}

bool SetNearClipDistance(float distance, bool reset) {
    return g_hooks.setNearClipDistance ? g_hooks.setNearClipDistance(distance, reset) : false;
}

bool SetCameraGoggleEffect(int mode) {
    return g_hooks.setCameraGoggleEffect ? g_hooks.setCameraGoggleEffect(mode) : false;
}

bool SetCameraDrunkLevel(int level) {
    return g_hooks.setCameraDrunkLevel ? g_hooks.setCameraDrunkLevel(level) : false;
}

bool SetColorFilter(int r1, int g1, int b1, int a1, int r2, int g2, int b2, int a2, bool reset) {
    return g_hooks.setColorFilter ? g_hooks.setColorFilter(r1, g1, b1, a1, r2, g2, b2, a2, reset) : false;
}

bool GetWaterLevel(float x, float y, float z, bool ignoreWaves, float& outHeight) {
    return g_hooks.getWaterLevel ? g_hooks.getWaterLevel(x, y, z, ignoreWaves, outHeight) : false;
}

bool TestLineAgainstWater(float x1, float y1, float z1, float x2, float y2, float z2,
                          float& ox, float& oy, float& oz) {
    return g_hooks.testLineAgainstWater
               ? g_hooks.testLineAgainstWater(x1, y1, z1, x2, y2, z2, ox, oy, oz)
               : false;
}

bool SetWorldWaterLevel(float height, bool reset) {
    return g_hooks.setWorldWaterLevel ? g_hooks.setWorldWaterLevel(height, reset) : false;
}

bool BreakObject(uint32_t id) {
    return g_hooks.breakObject ? g_hooks.breakObject(id) : false;
}

bool SetPlayerMoney(int32_t amount, bool instant) {
    return g_hooks.setPlayerMoney ? g_hooks.setPlayerMoney(amount, instant) : false;
}

bool GetPedBonePosition(uint32_t id, uint32_t boneId, float outPos[3]) {
    return g_hooks.getPedBonePosition ? g_hooks.getPedBonePosition(id, boneId, outPos) : false;
}

bool GetElementBoneMatrix(uint32_t id, uint32_t boneId, float outMat[16]) {
    return g_hooks.getElementBoneMatrix ? g_hooks.getElementBoneMatrix(id, boneId, outMat) : false;
}

bool SetElementBoneMatrix(uint32_t id, uint32_t boneId, const float inMat[16]) {
    return g_hooks.setElementBoneMatrix ? g_hooks.setElementBoneMatrix(id, boneId, inMat) : false;
}

bool UpdateElementRpHAnim(uint32_t id) {
    return g_hooks.updateElementRpHAnim ? g_hooks.updateElementRpHAnim(id) : false;
}

bool GetStatValue(uint16_t stat, float& outValue) {
    return g_hooks.getStatValue ? g_hooks.getStatValue(stat, outValue) : false;
}

bool SetStatValue(uint16_t stat, float value) {
    return g_hooks.setStatValue ? g_hooks.setStatValue(stat, value) : false;
}

bool GetPedOxygenLevel(uint32_t id, float& outOxygen) {
    return g_hooks.getPedOxygenLevel ? g_hooks.getPedOxygenLevel(id, outOxygen) : false;
}

bool SetPedOxygenLevel(uint32_t id, float oxygen) {
    return g_hooks.setPedOxygenLevel ? g_hooks.setPedOxygenLevel(id, oxygen) : false;
}

bool SetPedFightingStyle(uint32_t id, uint8_t style) {
    return g_hooks.setPedFightingStyle ? g_hooks.setPedFightingStyle(id, style) : false;
}

bool SetPedWalkingStyle(uint32_t id, int32_t animGroup) {
    return g_hooks.setPedWalkingStyle ? g_hooks.setPedWalkingStyle(id, animGroup) : false;
}

bool SetPedHeadless(uint32_t id, bool headless) {
    return g_hooks.setPedHeadless ? g_hooks.setPedHeadless(id, headless) : false;
}

bool PlayPedVoiceLine(uint32_t id, uint16_t speechId, float probability) {
    return g_hooks.playPedVoiceLine ? g_hooks.playPedVoiceLine(id, speechId, probability) : false;
}

bool SetPedAnimation(uint32_t id, const char* block, const char* name, int timeMs,
                     bool loop, bool updatePos, bool interruptible, bool freezeLastFrame) {
    return g_hooks.setPedAnimation
               ? g_hooks.setPedAnimation(id, block, name, timeMs, loop, updatePos, interruptible, freezeLastFrame)
               : false;
}

bool SetPedAnimationProgress(uint32_t id, const char* name, float progress) {
    return g_hooks.setPedAnimationProgress ? g_hooks.setPedAnimationProgress(id, name, progress) : false;
}

bool SetPedAnimationSpeed(uint32_t id, const char* name, float speed) {
    return g_hooks.setPedAnimationSpeed ? g_hooks.setPedAnimationSpeed(id, name, speed) : false;
}

bool SetEntityOnFire(uint32_t id, bool onFire) {
    return g_hooks.setEntityOnFire ? g_hooks.setEntityOnFire(id, onFire) : false;
}

bool SetEntityVelocity(uint32_t id, float x, float y, float z, bool angular) {
    return g_hooks.setEntityVelocity ? g_hooks.setEntityVelocity(id, x, y, z, angular) : false;
}

bool GetModelBoundingBox(uint16_t model, float minOut[3], float maxOut[3], float& radiusOut) {
    return g_hooks.getModelBoundingBox ? g_hooks.getModelBoundingBox(model, minOut, maxOut, radiusOut) : false;
}

bool GetEntityComDistance(uint32_t id, float& outDist) {
    return g_hooks.getEntityComDistance ? g_hooks.getEntityComDistance(id, outDist) : false;
}

bool IsEntityOnScreen(uint32_t id, bool& outOnScreen) {
    return g_hooks.isEntityOnScreen ? g_hooks.isEntityOnScreen(id, outOnScreen) : false;
}

bool GetVehicleContactWheels(uint32_t id, uint8_t& outWheels) {
    return g_hooks.getVehicleContactWheels ? g_hooks.getVehicleContactWheels(id, outWheels) : false;
}

bool FixVehicle(uint32_t id) {
    return g_hooks.fixVehicle ? g_hooks.fixVehicle(id) : false;
}

bool SetWorldSpecialProperty(const char* name, bool enabled) {
    return g_hooks.setWorldSpecialProperty ? g_hooks.setWorldSpecialProperty(name, enabled) : false;
}

bool SetCameraViewMode(int vehicleMode, int pedMode) {
    return g_hooks.setCameraViewMode ? g_hooks.setCameraViewMode(vehicleMode, pedMode) : false;
}

bool SetCameraTargetEntity(uint32_t id) {
    return g_hooks.setCameraTargetEntity ? g_hooks.setCameraTargetEntity(id) : false;
}

bool SetVehicleEngineState(uint32_t id, bool on) {
    return g_hooks.setVehicleEngineState ? g_hooks.setVehicleEngineState(id, on) : false;
}

bool SetVehicleOverrideLights(uint32_t id, uint8_t mode) {
    return g_hooks.setVehicleOverrideLights ? g_hooks.setVehicleOverrideLights(id, mode) : false;
}

bool SetVehicleDoorLockMode(uint32_t id, int mode) {
    return g_hooks.setVehicleDoorLockMode ? g_hooks.setVehicleDoorLockMode(id, mode) : false;
}

bool SetVehicleSirensOn(uint32_t id, bool on) {
    return g_hooks.setVehicleSirensOn ? g_hooks.setVehicleSirensOn(id, on) : false;
}

bool SetVehicleTaxiLightOn(uint32_t id, bool on) {
    return g_hooks.setVehicleTaxiLightOn ? g_hooks.setVehicleTaxiLightOn(id, on) : false;
}

bool SetVehiclePaintjob(uint32_t id, int paintjob) {
    return g_hooks.setVehiclePaintjob ? g_hooks.setVehiclePaintjob(id, paintjob) : false;
}

bool SetVehicleColor(uint32_t id, uint8_t c1, uint8_t c2, uint8_t c3, uint8_t c4) {
    return g_hooks.setVehicleColor ? g_hooks.setVehicleColor(id, c1, c2, c3, c4) : false;
}

bool SetVehiclePhysicalProofs(uint32_t id, bool bullet, bool fire, bool explosion, bool collision, bool melee) {
    return g_hooks.setVehiclePhysicalProofs
               ? g_hooks.setVehiclePhysicalProofs(id, bullet, fire, explosion, collision, melee)
               : false;
}

bool SetVehicleTyresCanBurst(uint32_t id, bool canBurst) {
    return g_hooks.setVehicleTyresCanBurst ? g_hooks.setVehicleTyresCanBurst(id, canBurst) : false;
}

bool SetVehicleDirtLevel(uint32_t id, float level) {
    return g_hooks.setVehicleDirtLevel ? g_hooks.setVehicleDirtLevel(id, level) : false;
}

bool SetVehicleDoorOpenRatio(uint32_t id, uint8_t door, float ratio) {
    return g_hooks.setVehicleDoorOpenRatio ? g_hooks.setVehicleDoorOpenRatio(id, door, ratio) : false;
}

bool BlowVehicle(uint32_t id, bool withExplosion) {
    return g_hooks.blowVehicle ? g_hooks.blowVehicle(id, withExplosion) : false;
}

bool BurstVehicleTyre(uint32_t id, uint8_t wheelId) {
    return g_hooks.burstVehicleTyre ? g_hooks.burstVehicleTyre(id, wheelId) : false;
}

bool SetVehicleUpgrade(uint32_t id, uint16_t upgrade, bool add) {
    return g_hooks.setVehicleUpgrade ? g_hooks.setVehicleUpgrade(id, upgrade, add) : false;
}

bool SetVehicleNitroState(uint32_t id, int count, float level) {
    return g_hooks.setVehicleNitroState ? g_hooks.setVehicleNitroState(id, count, level) : false;
}

bool SetTrainSpeed(uint32_t id, float speed) {
    return g_hooks.setTrainSpeed ? g_hooks.setTrainSpeed(id, speed) : false;
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
