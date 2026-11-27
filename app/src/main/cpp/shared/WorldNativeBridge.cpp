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

int RemoveWorldModel(uint16_t model, float radius, float x, float y, float z, int8_t interior) {
    return g_hooks.removeWorldModel ? g_hooks.removeWorldModel(model, radius, x, y, z, interior) : -1;
}

int RestoreWorldModel(uint16_t model, float radius, float x, float y, float z, int8_t interior) {
    return g_hooks.restoreWorldModel ? g_hooks.restoreWorldModel(model, radius, x, y, z, interior) : -1;
}

int RestoreAllWorldModels() {
    return g_hooks.restoreAllWorldModels ? g_hooks.restoreAllWorldModels() : -1;
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
