// Per-ped native task presentation state received through PED_SYNC.
//
// This layer records what the syncing client reported for a remote ped:
// locomotion mode, weapon presentation, animation presentation, physical
// takeover seed, camera rotation, onfire/inwater bits and health/armor locks.
// Applying these values to GTA peds is a separate step that reads the same
// records; this file has no GTA native calls.

#include "GameEngine.h"

#include <cstdint>
#include <mutex>
#include <unordered_map>

namespace WorldRPG::Game {
namespace {

struct PedNativeTaskState {
    unsigned int locomotionMode = 0;
    uint8_t weaponType = 0;
    uint16_t animGroup = 0;
    uint16_t animId = 0;
    bool physicalTakeover = false;
    unsigned int takeoverMode = 0;
    uint16_t takeoverAnimGroup = 0;
    uint16_t takeoverAnimId = 0;
    float takeoverProgress = 0.f;
    float cameraRotation = 0.f;
    bool onFire = false;
    bool inWater = false;
    bool healthLocked = false;
    float health = 0.f;
    bool armorLocked = false;
    float armor = 0.f;
};

std::mutex gPedNativeTaskMutex;
std::unordered_map<uint32_t, PedNativeTaskState> gPedNativeTaskStates;

// Callers must hold gPedNativeTaskMutex.
PedNativeTaskState& StateFor(uint32_t id) {
    return gPedNativeTaskStates[id];
}

} // namespace

void SetPedNativeTaskLocomotion(uint32_t id, unsigned int mode) {
    std::lock_guard<std::mutex> lock(gPedNativeTaskMutex);
    StateFor(id).locomotionMode = mode;
}

void SetPedNativeTaskWeapon(uint32_t id, uint8_t weaponType) {
    std::lock_guard<std::mutex> lock(gPedNativeTaskMutex);
    StateFor(id).weaponType = weaponType;
}

void SetPedNativeTaskAnimation(uint32_t id, uint16_t animGroup, uint16_t animId) {
    std::lock_guard<std::mutex> lock(gPedNativeTaskMutex);
    PedNativeTaskState& state = StateFor(id);
    state.animGroup = animGroup;
    state.animId = animId;
}

void SetPedNativeTaskPhysicalTakeover(uint32_t id, unsigned int mode,
    uint16_t animGroup, uint16_t animId, float progress) {
    std::lock_guard<std::mutex> lock(gPedNativeTaskMutex);
    PedNativeTaskState& state = StateFor(id);
    state.physicalTakeover = true;
    state.takeoverMode = mode;
    state.takeoverAnimGroup = animGroup;
    state.takeoverAnimId = animId;
    state.takeoverProgress = progress;
}

void SetPedCameraRotation(uint32_t id, float rotation) {
    std::lock_guard<std::mutex> lock(gPedNativeTaskMutex);
    StateFor(id).cameraRotation = rotation;
}

void SetPedOnFire(uint32_t id, bool onFire) {
    std::lock_guard<std::mutex> lock(gPedNativeTaskMutex);
    StateFor(id).onFire = onFire;
}

void SetPedInWater(uint32_t id, bool inWater) {
    std::lock_guard<std::mutex> lock(gPedNativeTaskMutex);
    StateFor(id).inWater = inWater;
}

void KillPedAnimation(uint32_t id) {
    std::lock_guard<std::mutex> lock(gPedNativeTaskMutex);
    PedNativeTaskState& state = StateFor(id);
    state.animGroup = 0;
    state.animId = 0;
    state.physicalTakeover = false;
    state.takeoverMode = 0;
    state.takeoverAnimGroup = 0;
    state.takeoverAnimId = 0;
    state.takeoverProgress = 0.f;
}

void LockPedHealth(uint32_t id, float health) {
    std::lock_guard<std::mutex> lock(gPedNativeTaskMutex);
    PedNativeTaskState& state = StateFor(id);
    state.healthLocked = true;
    state.health = health;
}

void LockPedArmor(uint32_t id, float armor) {
    std::lock_guard<std::mutex> lock(gPedNativeTaskMutex);
    PedNativeTaskState& state = StateFor(id);
    state.armorLocked = true;
    state.armor = armor;
}

void LockPedHealthCurrent(uint32_t id) {
    std::lock_guard<std::mutex> lock(gPedNativeTaskMutex);
    StateFor(id).healthLocked = true;
}

void LockPedArmorCurrent(uint32_t id) {
    std::lock_guard<std::mutex> lock(gPedNativeTaskMutex);
    StateFor(id).armorLocked = true;
}

} // namespace WorldRPG::Game
