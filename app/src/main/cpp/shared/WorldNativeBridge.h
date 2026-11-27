#pragma once

// Cross-platform bridge from shared Lua world/pool/fx/fire/light APIs to GTA:SA
// native symbols in libGTASA.so on Android (implemented via dlsym in
// gtasa/WorldNativeHooks.cpp). Headless host builds use the default no-op table
// in shared/WorldNativeBridge.cpp so every script test remains deterministic.

#include <cstdint>

namespace WorldRPG::WorldNative {

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

    // Building removal via CPools::ms_pBuildingPool + CWorld::Remove/Add; returns count removed/restored, or -1 if unhooked.
    int (*removeWorldModel)(uint16_t model, float radius, float x, float y, float z, int8_t interior) = nullptr;
    int (*restoreWorldModel)(uint16_t model, float radius, float x, float y, float z, int8_t interior) = nullptr;
    int (*restoreAllWorldModels)() = nullptr;

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
int RemoveWorldModel(uint16_t model, float radius, float x, float y, float z, int8_t interior);
int RestoreWorldModel(uint16_t model, float radius, float x, float y, float z, int8_t interior);
int RestoreAllWorldModels();

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
