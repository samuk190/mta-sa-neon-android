#pragma once

// Cross-platform bridge from shared Lua world/pool/fx/fire/light APIs to GTA:SA
// native symbols in libGTASA.so on Android (implemented via dlsym in
// gtasa/WorldNativeHooks.cpp). Headless host builds use the default no-op table
// in shared/WorldNativeBridge.cpp so every script test remains deterministic.

#include <cstdint>

namespace WorldRPG::WorldNative {

struct SphereTestResult {
    bool hit = false;
    uint16_t model = 0;
    float x = 0.f;
    float y = 0.f;
    float z = 0.f;
    float rxDeg = 0.f;
    float ryDeg = 0.f;
    float rzDeg = 0.f;
    uint16_t lod = 0;
    uint8_t entityType = 0;  // GTA eEntityType: 1=building, 2=vehicle, 3=ped, 4=object, 5=dummy
};

struct Hooks {
    // Returns live CPool capacity/used count for pool index 0..19, or -1 if unavailable.
    int (*getPoolCapacity)(int pool) = nullptr;
    int (*getPoolUsedCapacity)(int pool) = nullptr;

    // World actions; return true if the native GTA call was resolved and invoked.
    bool (*createExplosion)(float x, float y, float z, int type, bool makeSound, float camShake, bool damaging) = nullptr;
    bool (*createFire)(float x, float y, float z, float size) = nullptr;
    bool (*extinguishFire)(float x, float y, float z, float radius) = nullptr;
    bool (*addPointLight)(int type, float x, float y, float z, float dx, float dy, float dz,
                          float radius, float r, float g, float b, bool fog, bool shadow) = nullptr;
    bool (*preloadWorldArea)(float x, float y, float z, int mode) = nullptr;
    // mode: 0 = all, 1 = models, 2 = collisions
    bool (*createSwatRope)(float x, float y, float z, uint32_t durationMs) = nullptr;

    // World geometry queries; return true when the native hook is installed and succeeded.
    bool (*testSphereAgainstWorld)(float x, float y, float z, float radius,
                                   bool checkBuildings, bool checkVehicles, bool checkPeds,
                                   bool checkObjects, bool checkDummies, bool cameraIgnore,
                                   SphereTestResult& out) = nullptr;
    bool (*getGarageGeometry)(int garageId, float pos[3], float sizeWDH[3], float bbox[4]) = nullptr;

    // Building/dummy removal via CPools::ms_pBuildingPool + ms_pDummyPool + CWorld::Remove/Add;
    // returns count removed/restored, or -1 if unhooked.
    int (*removeWorldModel)(uint16_t model, float radius, float x, float y, float z, int8_t interior) = nullptr;
    int (*restoreWorldModel)(uint16_t model, float radius, float x, float y, float z, int8_t interior) = nullptr;
    int (*restoreAllWorldModels)() = nullptr;

    // World/weather/clock/timecycle/postfx/garage/area native hooks (all game-thread safe).
    bool (*setGameSpeed)(float speed) = nullptr;
    bool (*setMinuteDuration)(uint32_t ms) = nullptr;
    bool (*setWeatherBlended)(int weather) = nullptr;
    bool (*setRainLevel)(float level, bool forced) = nullptr;
    bool (*setWindVelocity)(float x, float y, float z, bool reset) = nullptr;
    bool (*setWaveHeight)(float height) = nullptr;
    bool (*setMoonSize)(int size) = nullptr;
    bool (*setBlurLevel)(int level) = nullptr;
    bool (*setGrainLevel)(int level) = nullptr;
    bool (*setHeatHaze)(int intensity, int randomShift, int speedMin, int speedMax,
                        int scanX, int scanY, int renderX, int renderY, bool showInside, bool reset) = nullptr;
    bool (*setFarClipDistance)(float distance, bool reset) = nullptr;
    bool (*setFogDistance)(float distance, bool reset) = nullptr;
    bool (*setSunSize)(float size, bool reset) = nullptr;
    bool (*setSunColor)(int coreR, int coreG, int coreB, int coronaR, int coronaG, int coronaB, bool reset) = nullptr;
    bool (*setSkyGradient)(int topR, int topG, int topB, int botR, int botG, int botB, bool reset) = nullptr;
    bool (*setWaterColor)(int r, int g, int b, int a, bool reset) = nullptr;
    bool (*setCloudsEnabled)(bool enabled) = nullptr;
    bool (*setOcclusionsEnabled)(bool enabled) = nullptr;
    bool (*setGarageOpen)(int garageId, bool open) = nullptr;
    bool (*isGarageOpen)(int garageId, bool& outOpen) = nullptr;
    bool (*setCurrentArea)(int area) = nullptr;

    // Camera, PostFX color filter, and Water native hooks (all game-thread safe).
    bool (*fadeCamera)(bool fadeIn, float duration, uint8_t r, uint8_t g, uint8_t b) = nullptr;
    bool (*shakeCamera)(float strength, float x, float y, float z) = nullptr;
    bool (*setNearClipDistance)(float distance, bool reset) = nullptr;
    // mode: 0 = normal, 1 = nightvision, 2 = thermalvision
    bool (*setCameraGoggleEffect)(int mode) = nullptr;
    bool (*setCameraDrunkLevel)(int level) = nullptr;
    bool (*setColorFilter)(int r1, int g1, int b1, int a1, int r2, int g2, int b2, int a2, bool reset) = nullptr;
    bool (*getWaterLevel)(float x, float y, float z, bool ignoreWaves, float& outHeight) = nullptr;
    bool (*testLineAgainstWater)(float x1, float y1, float z1, float x2, float y2, float z2,
                                 float& ox, float& oy, float& oz) = nullptr;
    bool (*setWorldWaterLevel)(float height, bool reset) = nullptr;
    bool (*breakObject)(uint32_t id) = nullptr;

    // Player, Ped, Entity, and Vehicle native hooks (all game-thread safe).
    bool (*setPlayerMoney)(int32_t amount, bool instant) = nullptr;
    bool (*getPedBonePosition)(uint32_t id, uint32_t boneId, float outPos[3]) = nullptr;
    bool (*getElementBoneMatrix)(uint32_t id, uint32_t boneId, float outMat[16]) = nullptr;
    bool (*setElementBoneMatrix)(uint32_t id, uint32_t boneId, const float inMat[16]) = nullptr;
    bool (*updateElementRpHAnim)(uint32_t id) = nullptr;
    bool (*getStatValue)(uint16_t stat, float& outValue) = nullptr;
    bool (*setStatValue)(uint16_t stat, float value) = nullptr;
    bool (*getPedOxygenLevel)(uint32_t id, float& outOxygen) = nullptr;
    bool (*setPedOxygenLevel)(uint32_t id, float oxygen) = nullptr;
    bool (*setPedFightingStyle)(uint32_t id, uint8_t style) = nullptr;
    bool (*setPedWalkingStyle)(uint32_t id, int32_t animGroup) = nullptr;
    bool (*setPedHeadless)(uint32_t id, bool headless) = nullptr;
    bool (*playPedVoiceLine)(uint32_t id, uint16_t speechId, float probability) = nullptr;
    bool (*setPedAnimation)(uint32_t id, const char* block, const char* name, int timeMs,
                            bool loop, bool updatePos, bool interruptible, bool freezeLastFrame) = nullptr;
    bool (*setPedAnimationProgress)(uint32_t id, const char* name, float progress) = nullptr;
    bool (*setPedAnimationSpeed)(uint32_t id, const char* name, float speed) = nullptr;
    bool (*setEntityOnFire)(uint32_t id, bool onFire) = nullptr;
    bool (*setEntityVelocity)(uint32_t id, float x, float y, float z, bool angular) = nullptr;
    bool (*getModelBoundingBox)(uint16_t model, float minOut[3], float maxOut[3], float& radiusOut) = nullptr;
    bool (*getEntityComDistance)(uint32_t id, float& outDist) = nullptr;
    bool (*isEntityOnScreen)(uint32_t id, bool& outOnScreen) = nullptr;
    bool (*getVehicleContactWheels)(uint32_t id, uint8_t& outWheels) = nullptr;
    bool (*fixVehicle)(uint32_t id) = nullptr;

    // Immediate Fx_c particle emitters; return true if invoked.
    bool (*fxAddBlood)(float x, float y, float z, float dx, float dy, float dz, int count, float brightness) = nullptr;
    bool (*fxAddBulletImpact)(float x, float y, float z, float dx, float dy, float dz, int smokeSize, int sparkCount, float smokeIntensity) = nullptr;
    bool (*fxAddSparks)(float x, float y, float z, float dx, float dy, float dz, float force, int count,
                        float acrossX, float acrossY, float acrossZ, bool blur, float spread, float life) = nullptr;
    bool (*fxAddTyreBurst)(float x, float y, float z, float dx, float dy, float dz) = nullptr;
    bool (*fxAddWood)(float x, float y, float z, float dx, float dy, float dz, int count, float brightness) = nullptr;
    bool (*fxAddPunchImpact)(float x, float y, float z, float dx, float dy, float dz) = nullptr;
    bool (*fxAddDebris)(float x, float y, float z, uint8_t r, uint8_t g, uint8_t b, uint8_t a, float scale, int count) = nullptr;
    bool (*fxAddGlass)(float x, float y, float z, uint8_t r, uint8_t g, uint8_t b, uint8_t a, float scale, int count) = nullptr;
};

void RegisterHooks(const Hooks& hooks);
const Hooks& CurrentHooks();

int GetPoolCapacity(int pool);
int GetPoolUsedCapacity(int pool);
bool CreateExplosion(float x, float y, float z, int type, bool makeSound, float camShake, bool damaging);
bool CreateFire(float x, float y, float z, float size);
bool ExtinguishFire(float x, float y, float z, float radius);
bool AddPointLight(int type, float x, float y, float z, float dx, float dy, float dz,
                   float radius, float r, float g, float b, bool fog, bool shadow);
bool PreloadWorldArea(float x, float y, float z, int mode);
bool CreateSwatRope(float x, float y, float z, uint32_t durationMs);
bool TestSphereAgainstWorld(float x, float y, float z, float radius,
                            bool checkBuildings, bool checkVehicles, bool checkPeds,
                            bool checkObjects, bool checkDummies, bool cameraIgnore,
                            SphereTestResult& out);
bool GetGarageGeometry(int garageId, float pos[3], float sizeWDH[3], float bbox[4]);
int RemoveWorldModel(uint16_t model, float radius, float x, float y, float z, int8_t interior);
int RestoreWorldModel(uint16_t model, float radius, float x, float y, float z, int8_t interior);
int RestoreAllWorldModels();

bool SetGameSpeed(float speed);
bool SetMinuteDuration(uint32_t ms);
bool SetWeatherBlended(int weather);
bool SetRainLevel(float level, bool forced);
bool SetWindVelocity(float x, float y, float z, bool reset);
bool SetWaveHeight(float height);
bool SetMoonSize(int size);
bool SetBlurLevel(int level);
bool SetGrainLevel(int level);
bool SetHeatHaze(int intensity, int randomShift, int speedMin, int speedMax,
                 int scanX, int scanY, int renderX, int renderY, bool showInside, bool reset);
bool SetFarClipDistance(float distance, bool reset);
bool SetFogDistance(float distance, bool reset);
bool SetSunSize(float size, bool reset);
bool SetSunColor(int coreR, int coreG, int coreB, int coronaR, int coronaG, int coronaB, bool reset);
bool SetSkyGradient(int topR, int topG, int topB, int botR, int botG, int botB, bool reset);
bool SetWaterColor(int r, int g, int b, int a, bool reset);
bool SetCloudsEnabled(bool enabled);
bool SetOcclusionsEnabled(bool enabled);
bool SetGarageOpen(int garageId, bool open);
bool IsGarageOpen(int garageId, bool& outOpen);
bool SetCurrentArea(int area);
bool FadeCamera(bool fadeIn, float duration, uint8_t r, uint8_t g, uint8_t b);
bool ShakeCamera(float strength, float x, float y, float z);
bool SetNearClipDistance(float distance, bool reset);
bool SetCameraGoggleEffect(int mode);
bool SetCameraDrunkLevel(int level);
bool SetColorFilter(int r1, int g1, int b1, int a1, int r2, int g2, int b2, int a2, bool reset);
bool GetWaterLevel(float x, float y, float z, bool ignoreWaves, float& outHeight);
bool TestLineAgainstWater(float x1, float y1, float z1, float x2, float y2, float z2,
                          float& ox, float& oy, float& oz);
bool SetWorldWaterLevel(float height, bool reset);
bool BreakObject(uint32_t id);

bool SetPlayerMoney(int32_t amount, bool instant);
bool GetPedBonePosition(uint32_t id, uint32_t boneId, float outPos[3]);
bool GetElementBoneMatrix(uint32_t id, uint32_t boneId, float outMat[16]);
bool SetElementBoneMatrix(uint32_t id, uint32_t boneId, const float inMat[16]);
bool UpdateElementRpHAnim(uint32_t id);
bool GetStatValue(uint16_t stat, float& outValue);
bool SetStatValue(uint16_t stat, float value);
bool GetPedOxygenLevel(uint32_t id, float& outOxygen);
bool SetPedOxygenLevel(uint32_t id, float oxygen);
bool SetPedFightingStyle(uint32_t id, uint8_t style);
bool SetPedWalkingStyle(uint32_t id, int32_t animGroup);
bool SetPedHeadless(uint32_t id, bool headless);
bool PlayPedVoiceLine(uint32_t id, uint16_t speechId, float probability);
bool SetPedAnimation(uint32_t id, const char* block, const char* name, int timeMs,
                     bool loop, bool updatePos, bool interruptible, bool freezeLastFrame);
bool SetPedAnimationProgress(uint32_t id, const char* name, float progress);
bool SetPedAnimationSpeed(uint32_t id, const char* name, float speed);
bool SetEntityOnFire(uint32_t id, bool onFire);
bool SetEntityVelocity(uint32_t id, float x, float y, float z, bool angular);
bool GetModelBoundingBox(uint16_t model, float minOut[3], float maxOut[3], float& radiusOut);
bool GetEntityComDistance(uint32_t id, float& outDist);
bool IsEntityOnScreen(uint32_t id, bool& outOnScreen);
bool GetVehicleContactWheels(uint32_t id, uint8_t& outWheels);
bool FixVehicle(uint32_t id);

bool FxAddBlood(float x, float y, float z, float dx, float dy, float dz, int count, float brightness);
bool FxAddBulletImpact(float x, float y, float z, float dx, float dy, float dz, int smokeSize, int sparkCount, float smokeIntensity);
bool FxAddSparks(float x, float y, float z, float dx, float dy, float dz, float force, int count,
                 float acrossX, float acrossY, float acrossZ, bool blur, float spread, float life);
bool FxAddTyreBurst(float x, float y, float z, float dx, float dy, float dz);
bool FxAddWood(float x, float y, float z, float dx, float dy, float dz, int count, float brightness);
bool FxAddPunchImpact(float x, float y, float z, float dx, float dy, float dz);
bool FxAddDebris(float x, float y, float z, uint8_t r, uint8_t g, uint8_t b, uint8_t a, float scale, int count);
bool FxAddGlass(float x, float y, float z, uint8_t r, uint8_t g, uint8_t b, uint8_t a, float scale, int count);

}  // namespace WorldRPG::WorldNative
