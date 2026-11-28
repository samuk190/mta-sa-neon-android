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
