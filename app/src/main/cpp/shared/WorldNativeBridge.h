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

    bool (*setWorldSpecialProperty)(const char* name, bool enabled) = nullptr;
    bool (*setCameraViewMode)(int vehicleMode, int pedMode) = nullptr;
    bool (*setCameraTargetEntity)(uint32_t id) = nullptr;
    bool (*setVehicleEngineState)(uint32_t id, bool on) = nullptr;
    bool (*setVehicleOverrideLights)(uint32_t id, uint8_t mode) = nullptr;
    bool (*setVehicleDoorLockMode)(uint32_t id, int mode) = nullptr;
    bool (*setVehicleSirensOn)(uint32_t id, bool on) = nullptr;
    bool (*setVehicleTaxiLightOn)(uint32_t id, bool on) = nullptr;
    bool (*setVehiclePaintjob)(uint32_t id, int paintjob) = nullptr;
    bool (*setVehicleColor)(uint32_t id, uint8_t c1, uint8_t c2, uint8_t c3, uint8_t c4) = nullptr;
    bool (*setVehiclePhysicalProofs)(uint32_t id, bool bullet, bool fire, bool explosion,
                                     bool collision, bool melee) = nullptr;
    bool (*setVehicleTyresCanBurst)(uint32_t id, bool canBurst) = nullptr;
    bool (*setVehicleDirtLevel)(uint32_t id, float level) = nullptr;
    bool (*setVehicleDoorOpenRatio)(uint32_t id, uint8_t door, float ratio) = nullptr;
    bool (*blowVehicle)(uint32_t id, bool withExplosion) = nullptr;
    bool (*burstVehicleTyre)(uint32_t id, uint8_t wheelId) = nullptr;
    bool (*setVehicleUpgrade)(uint32_t id, uint16_t upgrade, bool add) = nullptr;
    bool (*setVehicleNitroState)(uint32_t id, int count, float level) = nullptr;
    bool (*setTrainSpeed)(uint32_t id, float speed) = nullptr;

    // Batch F: Vehicle damage manager, entity universal alpha/collisions/proofs, camera FOV,
    // ped native flags/weapon/tasks/aim/look, mission HUD text, and CWeaponInfo stats.
    bool (*setVehicleDoorState)(uint32_t id, uint8_t door, uint8_t state) = nullptr;
    bool (*setVehicleLightState)(uint32_t id, uint8_t light, uint8_t state) = nullptr;
    bool (*setVehiclePanelState)(uint32_t id, uint8_t panel, uint8_t state) = nullptr;
    bool (*setVehicleWheelState)(uint32_t id, uint8_t wheel, uint8_t state) = nullptr;
    bool (*setEntityAlpha)(uint32_t id, uint8_t alpha) = nullptr;
    bool (*setEntityCollisionsEnabled)(uint32_t id, bool enabled) = nullptr;
    bool (*setEntityPhysicalProofs)(uint32_t id, bool bullet, bool fire, bool explosion,
                                    bool collision, bool melee) = nullptr;
    bool (*setCameraFov)(float fov) = nullptr;
    bool (*getCameraFov)(float& outFov) = nullptr;
    bool (*isPedStanding)(uint32_t id, bool& outStanding) = nullptr;
    bool (*isPedDucking)(uint32_t id, bool& outDucking) = nullptr;
    bool (*isPedFootBloodEnabled)(uint32_t id, bool& outEnabled) = nullptr;
    bool (*setPedFootBloodEnabled)(uint32_t id, bool enabled) = nullptr;
    bool (*setPedBleeding)(uint32_t id, bool bleeding) = nullptr;
    bool (*setPedCanBeKnockedOffBike)(uint32_t id, bool canBeKnocked) = nullptr;
    bool (*setPedMissionActor)(uint32_t id, bool mission) = nullptr;
    bool (*setPedSuffersCriticalHits)(uint32_t id, bool suffers) = nullptr;
    bool (*setPedCanBeDraggedOut)(uint32_t id, bool canDrag) = nullptr;
    bool (*setPedStayInSamePlace)(uint32_t id, bool stay) = nullptr;
    bool (*setPedNeverTargeted)(uint32_t id, bool never) = nullptr;
    bool (*getPedWeaponAccuracy)(uint32_t id, int& outAccuracy) = nullptr;
    bool (*setPedWeaponAccuracy)(uint32_t id, int accuracy) = nullptr;
    bool (*getPedWeaponShootingRate)(uint32_t id, int& outRate) = nullptr;
    bool (*setPedWeaponShootingRate)(uint32_t id, int rate) = nullptr;
    bool (*isPedReloadingWeapon)(uint32_t id, bool& outReloading) = nullptr;
    bool (*doesPedHaveJetPack)(uint32_t id, bool& outHasJetPack) = nullptr;
    bool (*getPedTaskChain)(uint32_t id, bool secondary, int slot, int outTaskIds[4], int& outCount) = nullptr;
    bool (*getPedSimplestTaskId)(uint32_t id, int& outTaskId) = nullptr;
    bool (*setPedAimTarget)(uint32_t id, float x, float y, float z) = nullptr;
    bool (*setPedLookAt)(uint32_t id, float x, float y, float z, int timeMs) = nullptr;
    bool (*loadMissionTextBlock)(const char* blockName) = nullptr;
    bool (*showMissionText)(const char* gxtKey, uint32_t timeMs, uint16_t flags) = nullptr;
    bool (*showMissionHelp)(const char* gxtKey, bool permanent) = nullptr;
    bool (*showMissionBigText)(const char* gxtKey, uint32_t timeMs, uint16_t style) = nullptr;
    bool (*clearMissionTexts)() = nullptr;
    bool (*clearMissionHelp)() = nullptr;
    bool (*setWeaponProperty)(int weaponId, int skill, const char* property, double a, double b, double c) = nullptr;

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

bool SetWorldSpecialProperty(const char* name, bool enabled);
bool SetCameraViewMode(int vehicleMode, int pedMode);
bool SetCameraTargetEntity(uint32_t id);
bool SetVehicleEngineState(uint32_t id, bool on);
bool SetVehicleOverrideLights(uint32_t id, uint8_t mode);
bool SetVehicleDoorLockMode(uint32_t id, int mode);
bool SetVehicleSirensOn(uint32_t id, bool on);
bool SetVehicleTaxiLightOn(uint32_t id, bool on);
bool SetVehiclePaintjob(uint32_t id, int paintjob);
bool SetVehicleColor(uint32_t id, uint8_t c1, uint8_t c2, uint8_t c3, uint8_t c4);
bool SetVehiclePhysicalProofs(uint32_t id, bool bullet, bool fire, bool explosion, bool collision, bool melee);
bool SetVehicleTyresCanBurst(uint32_t id, bool canBurst);
bool SetVehicleDirtLevel(uint32_t id, float level);
bool SetVehicleDoorOpenRatio(uint32_t id, uint8_t door, float ratio);
bool BlowVehicle(uint32_t id, bool withExplosion);
bool BurstVehicleTyre(uint32_t id, uint8_t wheelId);
bool SetVehicleUpgrade(uint32_t id, uint16_t upgrade, bool add);
bool SetVehicleNitroState(uint32_t id, int count, float level);
bool SetTrainSpeed(uint32_t id, float speed);

bool SetVehicleDoorState(uint32_t id, uint8_t door, uint8_t state);
bool SetVehicleLightState(uint32_t id, uint8_t light, uint8_t state);
bool SetVehiclePanelState(uint32_t id, uint8_t panel, uint8_t state);
bool SetVehicleWheelState(uint32_t id, uint8_t wheel, uint8_t state);
bool SetEntityAlpha(uint32_t id, uint8_t alpha);
bool SetEntityCollisionsEnabled(uint32_t id, bool enabled);
bool SetEntityPhysicalProofs(uint32_t id, bool bullet, bool fire, bool explosion, bool collision, bool melee);
bool SetCameraFov(float fov);
bool GetCameraFov(float& outFov);
bool IsPedStanding(uint32_t id, bool& outStanding);
bool IsPedDucking(uint32_t id, bool& outDucking);
bool IsPedFootBloodEnabled(uint32_t id, bool& outEnabled);
bool SetPedFootBloodEnabled(uint32_t id, bool enabled);
bool SetPedBleeding(uint32_t id, bool bleeding);
bool SetPedCanBeKnockedOffBike(uint32_t id, bool canBeKnocked);
bool SetPedMissionActor(uint32_t id, bool mission);
bool SetPedSuffersCriticalHits(uint32_t id, bool suffers);
bool SetPedCanBeDraggedOut(uint32_t id, bool canDrag);
bool SetPedStayInSamePlace(uint32_t id, bool stay);
bool SetPedNeverTargeted(uint32_t id, bool never);
bool GetPedWeaponAccuracy(uint32_t id, int& outAccuracy);
bool SetPedWeaponAccuracy(uint32_t id, int accuracy);
bool GetPedWeaponShootingRate(uint32_t id, int& outRate);
bool SetPedWeaponShootingRate(uint32_t id, int rate);
bool IsPedReloadingWeapon(uint32_t id, bool& outReloading);
bool DoesPedHaveJetPack(uint32_t id, bool& outHasJetPack);
bool GetPedTaskChain(uint32_t id, bool secondary, int slot, int outTaskIds[4], int& outCount);
bool GetPedSimplestTaskId(uint32_t id, int& outTaskId);
bool SetPedAimTarget(uint32_t id, float x, float y, float z);
bool SetPedLookAt(uint32_t id, float x, float y, float z, int timeMs);
bool LoadMissionTextBlock(const char* blockName);
bool ShowMissionText(const char* gxtKey, uint32_t timeMs, uint16_t flags);
bool ShowMissionHelp(const char* gxtKey, bool permanent);
bool ShowMissionBigText(const char* gxtKey, uint32_t timeMs, uint16_t style);
bool ClearMissionTexts();
bool ClearMissionHelp();
bool SetWeaponProperty(int weaponId, int skill, const char* property, double a, double b, double c);

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
