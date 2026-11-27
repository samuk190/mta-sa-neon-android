#include "../shared/DebugFlags.h"
#include "GameEngine.h"
#include "../shared/ControlToggles.h"
#include "GameImports.h"
#include "WorldObjects.h"
#include "GameTasks.h"
#include "VehiclePaint.h"
#include "SpatialNative.h"
#include "ProjectileEngine.h"
#include "PlayerPedPort.h"
#include "PlayerClothesTable.h"
#include "RenderGuard.h"
#include "GTASAOffsets.h"
#include "../shared/ServerWorld.h"
#include "PedInterpolator.h"
#include "../shared/VehicleSync.h"
#include "../shared/entity/EntityVehicleModels.h"

namespace WorldRPG::JniBridge { void PostGameLine(int kind, const char* text); }
#include "../hook/SymbolResolver.h"
#include "../hook/MemoryPatcher.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <strings.h>
#include <array>
#include <atomic>
#include <mutex>
#include <optional>
#include <deque>
#include <unordered_map>
#include <utility>
#include <vector>
#include <memory>

#ifdef __ANDROID__
#include <dlfcn.h>
#include <sys/mman.h>
#include <unistd.h>
#include <sched.h>
#include <semaphore.h>
#include <android/log.h>
#define GTASA_LOG(...) __android_log_print(ANDROID_LOG_INFO, "GameEngine", __VA_ARGS__)
#endif

namespace WorldRPG::Game {

#ifdef __ANDROID__
struct Vec3 { float x, y, z; };
using SetMoveSpeedFn = void (*)(void*, Vec3);
using RequestSpecialModelFn = void (*)(int, const char*, int);
using LoadAllRequestedModelsFn = void (*)(bool);
using SetupPlayerPedFn = void (*)(int);
using FindPlayerPedFn = void* (*)(int);
using TeleportFn = void (*)(void*, Vec3, unsigned char);
using LoadSceneCollisionFn = void (*)(const Vec3&);
using EndUserPauseFn = void (*)();
using RemoveTopFn = void (*)(void*, int, int);
using SwitchOffFn = void (*)();

static RequestSpecialModelFn g_requestSpecial = nullptr;
static LoadAllRequestedModelsFn g_loadAll = nullptr;
static SetupPlayerPedFn g_setupPlayer = nullptr;
static FindPlayerPedFn g_findPlayer = nullptr;
using FindPlayerCoorsFn = Vec3 (*)(int);
static FindPlayerCoorsFn g_findCoors = nullptr;
using RequestModelFn = void (*)(int, int);
static RequestModelFn g_requestModel = nullptr;
using HasModelFn = bool (*)(int);
static HasModelFn g_hasModel = nullptr;
using WorldAddFn = void (*)(void*);
static WorldAddFn g_worldAdd = nullptr;
using SetHeadingFn = void (*)(void*, float);
static SetHeadingFn g_setHeading = nullptr;
static std::atomic<float> g_poseX{1754.f};
static std::atomic<float> g_poseY{-1894.f};
static std::atomic<float> g_poseZ{14.f};
static std::atomic<float> g_poseH{0.f};
// FindPlayerHeading, for the radar arrow only. g_poseH stays the sync sample.
static std::atomic<float> g_radarH{0.f};
static std::atomic<float> g_velX{0.f};
static std::atomic<float> g_velY{0.f};
static std::atomic<float> g_velZ{0.f};
static std::atomic<float> g_health{100.f};
static std::atomic<float> g_armor{0.f};
// Server and Lua vitals writes are applied once to the body on the game thread.
static std::atomic<float> g_pendingLocalHealth{-1.f}, g_pendingLocalArmor{-1.f};
static std::atomic<uint64_t> g_localLife{0};
// Publish the attacker, weapon and body part from one damage event as a unit.
static Combat::PendingDamage g_pendingDamage;
static uint64_t g_nativeDamageEvents = 0;  // game thread, including synchronous replay callbacks
static std::atomic<bool> g_localFire{false};
static std::atomic<bool> g_localDuck{false};
// The game thread publishes the position after all native work for this request.
// The network thread waits for that acknowledgement before sending its next sample.
static std::mutex g_localPlacementMutex;
static Vec3 g_localPlacement{1754.f, -1894.f, 14.f};
static uint64_t g_requestedPlacement = 0;
static uint64_t g_appliedPlacement = 0;
static std::atomic<int> g_pendingLocalInterior{-1};
// ButtonCross (sprint) held, and a ButtonSquare (jump) press waiting for the next on-foot packet.
static std::atomic<bool> g_localSprint{false};
static uint64_t g_controlPressSequence = 0;  // game-thread producer for on-foot input
static std::atomic<uint64_t> g_localJumpLatch{0};
// ButtonCircle: the attack widget held now, and a tap waiting for the next on-foot packet.
static std::atomic<bool> g_localAttackHeld{false};
static std::atomic<uint64_t> g_localAttackLatch{0};
// CPad::GetPad(0) NewState shorts. LeftStickX is +0, LeftStickY is +2
// (PlayerPedPort.cpp, CPad::GetPedWalkLeftRight / GetPedWalkUpDown).
static std::atomic<int> g_padStickX{0};
static std::atomic<int> g_padStickY{0};
static std::atomic<bool> g_padStickValid{false};
// CCamera::Orientation. CCamera::Process (0x4bbcc0) stores it at this+0x154.
// CTaskSimplePlayerOnFoot::PlayerControlZelda (0x655a28) subtracts that float
// from the stick angle. PC CCameraSA::GetCameraRotation reads VAR_CameraRotation.
static std::atomic<float> g_cameraRot{0.f};
static std::atomic<bool> g_cameraRotValid{false};
static TeleportFn g_teleport = nullptr;
static LoadSceneCollisionFn g_loadSceneCollision = nullptr;
static int* g_currentArea = nullptr;
static EndUserPauseFn g_endPause = nullptr;
static RemoveTopFn g_removeTop = nullptr;
static void (*g_processMenuPending)(void*) = nullptr;
static SwitchOffFn g_switchOff = nullptr;
static void* g_mobileMenu = nullptr;
using ScriptProcessFn = void (*)(void*);
static ScriptProcessFn g_originalScript = nullptr;

static void AllowWrite(void* address, size_t size) {
    long page = sysconf(_SC_PAGESIZE);
    if (page <= 0) page = 4096;
    auto start = reinterpret_cast<uintptr_t>(address) & ~(static_cast<uintptr_t>(page) - 1);
    auto end = (reinterpret_cast<uintptr_t>(address) + size + static_cast<uintptr_t>(page) - 1)
        & ~(static_cast<uintptr_t>(page) - 1);
    mprotect(reinterpret_cast<void*>(start), end - start, PROT_READ | PROT_WRITE);
}

static void ResolvePlayerFns() {
    void* handle = dlopen("libGTASA.so", RTLD_NOLOAD | RTLD_GLOBAL);
    if (!handle) return;
    g_requestSpecial = reinterpret_cast<RequestSpecialModelFn>(
        dlsym(handle, "_ZN10CStreaming19RequestSpecialModelEiPKci"));
    g_loadAll = reinterpret_cast<LoadAllRequestedModelsFn>(
        dlsym(handle, "_ZN10CStreaming22LoadAllRequestedModelsEb"));
    g_setupPlayer = reinterpret_cast<SetupPlayerPedFn>(
        dlsym(handle, "_ZN10CPlayerPed14SetupPlayerPedEi"));
    g_findPlayer = reinterpret_cast<FindPlayerPedFn>(dlsym(handle, "_Z13FindPlayerPedi"));
    g_findCoors = reinterpret_cast<FindPlayerCoorsFn>(dlsym(handle, "_Z15FindPlayerCoorsi"));
    g_requestModel = reinterpret_cast<RequestModelFn>(dlsym(handle, "_ZN10CStreaming12RequestModelEii"));
    g_hasModel = reinterpret_cast<HasModelFn>(dlsym(handle, "_ZN10CStreaming14HasModelLoadedEi"));
    g_worldAdd = reinterpret_cast<WorldAddFn>(dlsym(handle, "_ZN6CWorld3AddEP7CEntity"));
    g_setHeading = reinterpret_cast<SetHeadingFn>(dlsym(handle, "_ZN10CPlaceable10SetHeadingEf"));
    g_teleport = reinterpret_cast<TeleportFn>(dlsym(handle, "_ZN4CPed8TeleportE7CVectorh"));
    g_loadSceneCollision = reinterpret_cast<LoadSceneCollisionFn>(
        dlsym(handle, "_ZN10CStreaming18LoadSceneCollisionERK7CVector"));
    g_currentArea = static_cast<int*>(dlsym(handle, "_ZN5CGame8currAreaE"));
    if (!g_loadSceneCollision) ENGINE_LOGI("DIAG local teleport: CStreaming::LoadSceneCollision is unavailable");
    if (!g_currentArea) ENGINE_LOGI("DIAG local interior: CGame::currArea is unavailable");
    g_endPause = reinterpret_cast<EndUserPauseFn>(dlsym(handle, "_ZN6CTimer12EndUserPauseEv"));
    g_removeTop = reinterpret_cast<RemoveTopFn>(dlsym(handle, "_ZN10MobileMenu15RemoveTopScreenEbb"));
    g_processMenuPending = reinterpret_cast<void (*)(void*)>(dlsym(handle, "_ZN10MobileMenu14ProcessPendingEv"));
    g_switchOff = reinterpret_cast<SwitchOffFn>(dlsym(handle, "_Z20Menu_SwitchOffToGamev"));
}

static void QueueLocalPlacement(float x, float y, float z, bool newLife = false) {
    std::lock_guard lock(g_localPlacementMutex);
    g_localPlacement = {x, y, z};
    ++g_requestedPlacement;
    if (newLife) ++g_localLife;
}

bool LocalPlacementPending() {
    std::lock_guard lock(g_localPlacementMutex);
    return g_requestedPlacement != g_appliedPlacement;
}

static void PlacePlayer(bool initial = false) {
    Vec3 pos;
    uint64_t request;
    {
        std::lock_guard lock(g_localPlacementMutex);
        request = g_requestedPlacement;
        if (!initial && request == g_appliedPlacement) return;
        pos = g_localPlacement;
    }
    if (!g_findPlayer || !g_teleport) return;
    void* ped = g_findPlayer(0);
    if (!ped) return;
    // Desktop CClientPed::Teleport waits for ground collision on jumps over
    // 50 units. Arm64 LoadSceneCollision loads COL and IPLs at the target.
    const float dx = pos.x - g_poseX.load();
    const float dy = pos.y - g_poseY.load();
    const float dz = pos.z - g_poseZ.load();
    if (g_loadSceneCollision && dx * dx + dy * dy + dz * dz > 2500.f)
        g_loadSceneCollision(pos);
    g_teleport(ped, pos, 0);
    g_poseX = pos.x;
    g_poseY = pos.y;
    g_poseZ = pos.z;
    g_velX = 0.f;
    g_velY = 0.f;
    g_velZ = 0.f;
    std::lock_guard lock(g_localPlacementMutex);
    g_appliedPlacement = std::max(g_appliedPlacement, request);
}

// Same replacement the PC client installs on CRunningScript::Process: the
// original is never called, so main.scm cannot start the intro. The first
// call only builds the local player. MobileSA reaches this by DoGameState
// case 8 (InitialiseGame, then gGameState = 9) and does not set MobileMenu
// +0x6d. That byte is the New Game flag: it sets +0x6c, and MainGameFunc
// then calls DoGameRestart, which is the single-player story.
static bool PlausiblePosition(float x, float y, float z) {
    return std::fabs(x) < 20000.f && std::fabs(y) < 20000.f && z > -200.f && z < 4000.f
        && !(x == 0.f && y == 0.f && z == 0.f);
}

// CPlaceable on 2.10: matrix pointer at +0x18 (position at matrix+0x30),
// otherwise the embedded transform at +0x08 and heading at +0x14.
static void SampleLocalTasks(void* ped);
static void SampleLocalWeapon(void* ped, Vec3 position, float heading);
static void* g_sampledLocalPed = nullptr;  // game thread only
static uint64_t g_sampledLife = UINT64_MAX;
static void SampleLocalPose() {
    if (!g_findPlayer) return;
    auto* ped = static_cast<unsigned char*>(g_findPlayer(0));
    g_sampledLocalPed = ped;
    if (!ped) return;
    {
        std::lock_guard lock(g_localPlacementMutex);
        if (g_requestedPlacement == g_appliedPlacement) g_sampledLife = g_localLife.load();
    }
    float x = 0, y = 0, z = 0, heading = g_poseH.load(), radarHeading = g_radarH.load();
    auto* matrix = *reinterpret_cast<unsigned char**>(ped + 0x18);
    const auto matrixAddr = reinterpret_cast<uintptr_t>(matrix);
    if (matrix && (matrixAddr & 3u) == 0 && matrixAddr > 0x10000u) {
        x = *reinterpret_cast<float*>(matrix + 0x30);
        y = *reinterpret_cast<float*>(matrix + 0x34);
        z = *reinterpret_cast<float*>(matrix + 0x38);
        // Pure-sync still publishes this sample. The radar arrow does not use it:
        // row +0x20 is up, so its X is ~0 (east/west never turns the arrow) and
        // its Y is only pitch (north/south comes out backwards).
        heading = std::atan2(-*reinterpret_cast<float*>(matrix + 0x20),
                              *reinterpret_cast<float*>(matrix + 0x24));
        // FindPlayerHeading (libGTASA 2.10, 0x4f0720): ldp s0,s1,[matrix,#0x10];
        // fneg s0; atan2f. Forward row, atan2(-forward.x, forward.y). 0 is north,
        // east is -pi/2. Position stays +0x30/+0x34 (CPed::Teleport stores X, then Y).
        radarHeading = std::atan2(-*reinterpret_cast<float*>(matrix + 0x10),
                                   *reinterpret_cast<float*>(matrix + 0x14));
        // CNetAPI::WritePlayerPuresync and WriteKeysync send GetCurrentRotation (CPedSA m_fCurrentRotation), which is
        // CPed+0x6C4 on libGTASA 2.10 (CPed::UpdatePosition 0x597b3c; the same field SetRemotePedHeading writes and
        // the hosted-ped snapshot reads). The value above came from the up row (+0x20), whose X is ~0, so the heading
        // sent to desktop barely changed: desktop did not see the phone player turn (device 2026-11-14).
        // The forward-row heading is the fallback.
        const float current = *reinterpret_cast<const float*>(ped + 0x6C4);
        heading = std::isfinite(current) ? current : radarHeading;
    } else {
        x = *reinterpret_cast<float*>(ped + 0x08);
        y = *reinterpret_cast<float*>(ped + 0x0C);
        z = *reinterpret_cast<float*>(ped + 0x10);
        heading = *reinterpret_cast<float*>(ped + 0x14);
        radarHeading = heading;
    }
    if (!PlausiblePosition(x, y, z)) return;
    auto* health = reinterpret_cast<float*>(ped + 0x6AC);
    // CPed armour on libGTASA 2.10 arm64, not the PC field at +0x548.
    // CWidgetPlayerInfo::RenderHealthBar (0x37d770) loads health with
    // `ldr s0, [ped, #0x6ac]`. RenderArmorBar (0x37d93c) loads armour from
    // that same ped pointer with `ldr s1, [ped, #0x6b4]`.
    // CPedDamageResponseCalculator::AccountForPedArmour (0x444044) reads and
    // writes `CPed*` (x1) at the same +0x6B4.
    auto* armor = reinterpret_cast<float*>(ped + 0x6B4);
    auto* speed = reinterpret_cast<float*>(ped + 0x68);
    if (float value = g_pendingLocalHealth.exchange(-1.f); value >= 0.f) *health = value;
    if (float value = g_pendingLocalArmor.exchange(-1.f); value >= 0.f) *armor = value;
    if (*health >= 0.f && *health < 1000.f) g_health = *health;
    if (std::isfinite(*armor) && *armor >= 0.f && *armor < 1000.f) g_armor = *armor;
    g_velX = speed[0];
    g_velY = speed[1];
    g_velZ = speed[2];
    const float dx = x - g_poseX.load();
    const float dy = y - g_poseY.load();
    g_poseX = x;
    g_poseY = y;
    g_poseZ = z;
    g_poseH = heading;
    g_radarH = radarHeading;
    SampleLocalTasks(ped);
    SampleLocalWeapon(ped, {x, y, z}, heading);
    if (dx * dx + dy * dy > 9.f) {
        static int reports = 0;
        if (reports < 8) {
            ++reports;
            ENGINE_LOGI("Local pose %.0f %.0f %.0f heading %.2f", x, y, z, heading);
        }
    }
}

struct RemotePed {
    float x = 0, y = 0, z = 0, heading = 0;
    float vx = 0, vy = 0, vz = 0;
    float shownX = 0, shownY = 0, shownZ = 0, shownH = 0;
    float health = 100.f;
    float armor = 0.f;
    float cameraRotation = 0.f;
    PlayerWeaponSync::RemoteWeapon syncWeapon;
    uint64_t weaponRevision = 0;
    uint64_t aimRevision = 0;
    bool gangDriveby = false;
    // Native task presentation — matches mta_neon SLastSyncedPedData fields.
    unsigned int nativeTaskLocomotionMode = 0;   // SNativeTaskLocomotionSync::eMode
    uint8_t nativeTaskWeaponType = 0;             // SNativeTaskWeaponPresentationSync weapon type
    uint16_t nativeTaskAnimGroup = 0;             // SNativeTaskAnimationPresentationSync group
    uint16_t nativeTaskAnimId = 0;                // SNativeTaskAnimationPresentationSync id
    unsigned int physicalTakeoverMode = 0;        // Physical takeover mode from PED_START_SYNC
    uint16_t physicalTakeoverAnimGroup = 0;
    uint16_t physicalTakeoverAnimId = 0;
    float physicalTakeoverProgress = 0.f;
    bool physicalTakeoverPending = false;
    bool dirty = false;
    bool shown = false;
    bool fire = false;
    bool inWater = false;
    bool duck = false;
    bool hosted = false;
    bool animationKilled = false;
    bool healthLocked = false;
    bool armorLocked = false;
    // ENTITY_ADD interior. CClientPed::SetInterior writes the area code at CEntity+0x4B
    // (CClientPed.cpp 1101-1119). CColStore::RequestCollision takes the same value.
    uint8_t interior = 0;
    // Last keysync stick (PC units, -128..128). Drives CPlayerPed::ProcessControl
    // once the controller hook is installed. keysync is false for script peds.
    int stickX = 0;
    int stickY = 0;
    bool keysync = false;
    // ReadPlayerPuresync wrote SVelocitySync (including a zero vector). Applied once.
    bool syncMoveSpeed = false;
    // SPlayerHealthSync / SPlayerArmorSync decoded. Applied once to the body fields.
    bool syncHealth = false;
    bool syncArmor = false;
    // Server ped from the ENTITY_ADD actor list (kind 14).
    bool serverPed = false;
    // ENTITY_ADD ped record (ServerWorld::CopyPedSpawn) applied once per element. A later stream-in keeps the
    // current state, as desktop _CreateModel restores m_fHealth and friends, not the ENTITY_ADD values.
    bool spawnApplied = false;
    // CClientPed::SetFrozen state of the element (ENTITY_ADD bIsFrozen, later SET_ELEMENT_FROZEN).
    bool frozen = false;
    bool frozenChanged = false;
    // A PLAYER_PURESYNC position not yet handed to SetTargetPosition (ReadPlayerPuresync, CNetAPI.cpp 998).
    bool newTarget = false;
    // Lua controller state for an owned server ped (traffic routes).
    uint8_t ownedControls = 0;
    bool observerHasPosition = false;
    bool observerHasHeading = false;
    uint64_t observerPositionInterval = 0;
    uint64_t observerHeadingInterval = 0;
    int64_t lastObserverPositionMs = 0;
    int64_t lastObserverMotionMs = 0;
    int64_t observerMotionUntilMs = 0;
    // CClientPlayerClothes from PLAYER_CLOTHES (Packet_PlayerClothes, CPacketHandler.cpp 5160-5195). Empty: the
    // desktop default outfit (CClientPlayerClothes::DefaultClothes 467-474). Applied when the body is model 0.
    std::vector<WorldRPG::Entity::PedClothes> clothes;
    bool clothesDirty = true;
    // Keysync / puresync button flags (NoteRemoteButtons).
    uint8_t buttons = 0;
    // SET_ELEMENT_POSITION / SET_ELEMENT_INTERIOR with a position: CClientPed::SetPosition with interpolation reset.
    bool teleport = false;
    Vec3 teleportPosition{};
    // SET_PED_ROTATION: SetCurrentRotation at once.
    bool headingSnap = false;
    float snapHeading = 0;
    // PED_WASTED / PLAYER_WASTED (Packet_PlayerWasted -> CClientPed::Kill): a kill waiting for the game thread, and the
    // m_bDead state after it. PLAYER_SPAWN clears dead and replaces the body.
    bool killPending = false;
    bool dead = false;
    int killWeapon = 0;
    int killAnimGroup = 0;
    int killAnimId = 0;
    // CClientPed::m_ucAlpha (SetAlpha) and m_bUsesCollision (SetUsesCollision): ENTITY_ADD, then SET_ELEMENT_ALPHA /
    // SET_ELEMENT_COLLISIONS_ENABLED. Every new body gets them (_CreateModel).
    uint8_t alpha = 255;
    bool alphaChanged = false;
    bool collisions = true;
    bool collisionsChanged = false;
    // CClientPed::m_AnimationCache: the last named animation, run again on a new body (RunAnimationFromCache).
    bool hasAnimCache = false;
    WorldRPG::Game::PedRpcOp animCache;
};
static std::mutex g_remoteMutex;
static std::unordered_map<uint32_t, RemotePed> g_remotePeds;
// Phase 6 animation and weapon RPCs per element, in arrival order (QueuePedRpc). Guarded by g_remoteMutex.
static std::unordered_map<uint32_t, std::deque<PedRpcOp>> g_pedRpcOps;
static constexpr size_t kMaxPedRpcOps = 32;
// The local player's alpha and collisions (SetAlpha / SetUsesCollision on the local CClientPed).
static std::atomic<int> g_localAlpha{255};
static std::atomic<int> g_localCollisions{1};
struct RemoteBody {
    bool networkPlayer = false;
    uint64_t queryGeneration = 0;
    uint64_t spatialGeneration = 0;
    void* ped = nullptr;
    std::unique_ptr<std::byte[]> owned;
    uint64_t appliedWeaponRevision = 0;
    uint64_t aimRevision = 0;
    PlayerWeaponSync::AimInterpolation aimInterpolation;
    int aimSlot = -1;
    int trafficTask = 0;
    bool ducked = false;
    bool observerWalking = false;
    int64_t lastFight = 0;
    // CClientPed::ArmRemoteStreamInTransformFence (CClientPed.cpp 5914-5945) holds a remote
    // ped until collision is loaded. Owners use the same bIsStaticWaitingForCollision bit
    // in UpdateNativeCollisionAuthorityFence (7074-7108).
    bool collisionFence = false;
    int64_t fenceAtMs = 0;
    // m_remoteStreamInFencePreviousStaticWaitingForCollision (CClientPed.cpp 5934), restored on release (6038).
    bool fencePrevStaticWaiting = false;
    // m_remoteReplicaPhysicsFenceActive (CClientPed::UpdateRemoteReplicaPhysicsFence, CClientPed.cpp 7140-7152).
    bool replicaFence = false;
    // Model the body was created with (after the model-7 stand-in rule).
    int model = -1;
    // RemotePed::frozen last written through ApplyPedFreezeState (-1: not yet).
    int frozenApplied = -1;
    // Hosted ped: health seen by the damage hook, and whether PED_WASTED went out for this death.
    float lastHealth = -1.f;
    bool wastedSent = false;
    // CClientPed::m_interp.pos (SetTargetPosition / UpdateTargetPosition, CClientPed.cpp 8657-8822).
    struct {
        float targetX = 0, targetY = 0, targetZ = 0;
        float errorX = 0, errorY = 0, errorZ = 0;
        int64_t startMs = 0;
        int64_t finishMs = 0;  // 0: no target (HasTargetPosition false)
        float lastAlpha = 0;
    } interp;
    Interp::RotationInterp rotationInterp;
    // Last area code written by ApplyPedInterior. Unset until the first write.
    bool interiorWritten = false;
    uint8_t interior = 0;
    // Last effective alpha / collision state written to this body (-1: not yet).
    int alphaApplied = -1;
    int collisionsApplied = -1;
};
// Desktop CPacketHandler::Packet_EntityAdd (vehicle: SetRotationDegrees, SetHealth) and
// CUnoccupiedVehicleSync::Packet_UnoccupiedVehicleStartSync (CUnoccupiedVehicleSync.cpp 104-158: SetTargetPosition,
// SetTargetRotation, SetMoveSpeed, SetTurnSpeed, SetHealth). Rotation is in degrees, as on the wire.
struct CarSeed {
    bool pending = false;
    bool hasPose = false;
    float x = 0, y = 0, z = 0;
    bool hasRotation = false;
    float rotX = 0, rotY = 0, rotZ = 0;
    bool hasMoveSpeed = false;
    bool hasTurnSpeed = false;
    float move[3] = {0, 0, 0};
    float turn[3] = {0, 0, 0};
    bool hasHealth = false;
    float health = 0;
};
struct CarInterpolation {
    VehicleSync::Vec3 target{};
    VehicleSync::Vec3 error{};
    int64_t startMs = 0;
    float lastAlpha = 0.f;
};
struct RemoteCar {
    uint64_t spatialGeneration = 0;
    void* body = nullptr;
    std::unique_ptr<std::byte[]> owned;
    int model = 0;
    float x = 0, y = 0, z = 0, heading = 0;
    float shownX = 0, shownY = 0, shownZ = 0, shownH = 0;
    bool shown = false;
    bool hosted = false;
    bool dirty = false;
    // Packet 33 driver block: move and turn speed, written to the body once per packet (motionPending, g_remoteMutex).
    float moveX = 0, moveY = 0, moveZ = 0;
    float turnX = 0, turnY = 0, turnZ = 0;
    bool motionPending = false;
    // Server state the body has to take once (ApplyCarSeed). Filled from the ENTITY_ADD actor (rotation, health)
    // and from VEHICLE_STARTSYNC (everything). Guarded by g_remoteMutex.
    CarSeed seed;
    std::optional<VehicleSync::UnoccupiedUpdate> observedVehicle;
    bool useUnoccupiedSync = false; // guarded by g_remoteMutex
    // Only the game thread reads or writes the active interpolation.
    CarInterpolation unoccupiedPosition;
    CarInterpolation unoccupiedRotation;
    bool actorSeeded = false;
    std::optional<bool> frozen;
    std::optional<bool> engineOn;
    std::optional<bool> loadCollision;
    std::optional<uint8_t> alpha;
    int appliedFrozen = -1;
    int appliedEngineOn = -1;
    int appliedLoadCollision = -1;
    int appliedAlpha = -1;
    std::optional<HostedCar> lastBodyPose;
    int doorLockMode = 1;
    int appliedDoorLockMode = -1;
    std::optional<VehicleColors> colors;
    std::optional<std::array<VehicleRgb, 4>> appliedColors;
    void* paintClump = nullptr;
    uint16_t nitroUpgrade = 0;
    uint64_t nitroRevision = 0;
    uint16_t appliedNitroUpgrade = 0;
    uint64_t appliedNitroRevision = 0;
};
static std::unordered_map<uint32_t, RemoteCar> g_remoteCars;
static std::vector<uint32_t> g_forget;
static std::atomic<bool> g_remoteResetPending{false};
static std::vector<HostedPed> g_hostedSnap;
static std::vector<HostedCar> g_hostedCarSnap;
// Immutable IDs from the last completed game-thread streaming pass.
static std::vector<std::pair<uint32_t, uint64_t>> g_streamedActors;
static std::unordered_map<uint32_t, RemoteBody> g_remoteBodies;
// Phone body caps, kept apart the way desktop keeps the two streamers apart
// (CClientManager.cpp 27-28, Common.h 16-17). Desktop stops at 110 peds and 64 vehicles.
// The phone stays at 48 for each. CopyActors quotas use the same numbers, so a full car
// list cannot take the slots the ped bodies are drawn from.
static constexpr int kMaxRemotePedBodies = 48;
static constexpr int kMaxRemoteVehicleRecords = 48;
// Local player vehicle state and the queue for VEHICLE_INOUT. Both guarded by g_remoteMutex.
static constexpr uint32_t kNoVehicle = 0xFFFFFFFFu;
static uint32_t g_localVehicleId = kNoVehicle;
static uint8_t g_localSeat = 0;
// Live sample of that car for packet 33 (CopyLocalCar). Written in WatchLocalVehicle, under g_remoteMutex.
static LocalCarState g_localCar;
static bool g_localCarValid = false;
static uint64_t g_vehicleFireSequence = 0;
static std::vector<VehicleTouchRect> g_vehicleTouchBounds;
static std::vector<LocalVehicleEvent> g_localVehicleEvents;
static constexpr size_t kMaxLocalVehicleEvents = 16;
// Local bullet-weapon shots (NoteLocalShot / PeekLocalShot). Guarded by g_remoteMutex.
static std::vector<LocalShot> g_localShots;
static uint64_t g_localShotSerial = 0;
static PlayerWeaponSync::LocalWeapon g_localWeapon;
static int g_localDrivebyRequest = -1;  // protected by g_remoteMutex
static PlayerWeaponSync::Aim g_lastLocalShotAim;
static int g_lastLocalShotWeapon = 0;
static std::chrono::steady_clock::time_point g_lastLocalShotTime;
static constexpr size_t kMaxLocalShots = 32;
struct RemoteBullet {
    uint64_t localLife = 0;
    Combat::SBulletSync shot;
    uint64_t generation = 0;
    std::chrono::steady_clock::time_point deadline;
    PedRpcOp weaponRequest;
};
static std::deque<RemoteBullet> g_remoteBullets;
static constexpr size_t kMaxRemoteBullets = 64;

// HasModelLoaded is not exported. HasVehicleUpgradeLoaded (0x395098) reads
// ms_aInfoForModel[id] with stride 0x14 and treats the byte at +0x10 as loaded
// when it equals 1. A null function pointer used to fail this check forever.
static int StreamingLoadState(int model) {
    if (model < 0 || model > 19999) return -1;
    static unsigned char* table = nullptr;
    static bool looked = false;
    if (!looked) {
        looked = true;
        void* handle = dlopen("libGTASA.so", RTLD_NOLOAD | RTLD_GLOBAL);
        if (handle)
            table = static_cast<unsigned char*>(dlsym(handle, "_ZN10CStreaming16ms_aInfoForModelE"));
        if (!table) ENGINE_LOGI("ms_aInfoForModel missing; remote models stay unloaded");
    }
    if (!table) return g_hasModel && g_hasModel(model) ? 1 : -1;
    return table[static_cast<size_t>(model) * 0x14u + 0x10u];
}

static bool EnsureStreamedModel(int model, int limit) {
    if (model < 0 || model > limit) return false;
    if (StreamingLoadState(model) == 1) return true;
    if (g_requestModel) g_requestModel(model, 0x12);
    // A few priority flushes. Later ticks keep requesting until the state
    // flips, so a model that is still streaming is not dropped.
    static int pumps = 0;
    if (g_loadAll && pumps < 6 && StreamingLoadState(model) != 1) {
        ++pumps;
        g_loadAll(true);
    }
    return StreamingLoadState(model) == 1;
}

static bool EnsurePedModel(int model) {
    if (model == 0) return false;
    return EnsureStreamedModel(model, 312);
}

struct ClothesApi {
    bool resolved = false;
    void (*init)() = nullptr;
    void (*rebuild)(void*, bool) = nullptr;
    void (*setTex)(void*, const char*, const char*, int) = nullptr;
    void (*setStat)(unsigned short, float) = nullptr;
    void (*allocData)(void*) = nullptr;
};

static ClothesApi& Clothes() {
    static ClothesApi api;
    if (api.resolved) return api;
    api.resolved = true;
    void* handle = dlopen("libGTASA.so", RTLD_NOLOAD | RTLD_GLOBAL);
    if (!handle) return api;
    api.init = reinterpret_cast<void (*)()>(dlsym(handle, "_ZN8CClothes4InitEv"));
    api.rebuild = reinterpret_cast<void (*)(void*, bool)>(
        dlsym(handle, "_ZN8CClothes13RebuildPlayerEP10CPlayerPedb"));
    api.setTex = reinterpret_cast<void (*)(void*, const char*, const char*, int)>(
        dlsym(handle, "_ZN15CPedClothesDesc18SetTextureAndModelEPKcS1_i"));
    api.setStat = reinterpret_cast<void (*)(unsigned short, float)>(
        dlsym(handle, "_ZN6CStats12SetStatValueEtf"));
    api.allocData = reinterpret_cast<void (*)(void*)>(
        dlsym(handle, "_ZN14CPlayerPedData12AllocateDataEv"));
    if (api.init) api.init();
    return api;
}

static std::unordered_map<void*, std::unique_ptr<std::byte[]>> g_privatePlayerData;

static void* PlayerDataOf(void* ped) {
    if (!ped) return nullptr;
    void* data = *reinterpret_cast<void**>(static_cast<unsigned char*>(ped) + 0x540);
    const auto addr = reinterpret_cast<uintptr_t>(data);
    if (!data || (addr & 7u) != 0 || addr < 0x10000u) return nullptr;
    return data;
}

static void* EnsurePlayerData(void* ped, bool createData) {
    if (void* data = PlayerDataOf(ped)) return data;
    if (!createData) return nullptr;
    auto block = std::make_unique<std::byte[]>(0x80);
    std::memset(block.get(), 0, 0x80);
    *reinterpret_cast<void**>(static_cast<unsigned char*>(ped) + 0x540) = block.get();
    g_privatePlayerData[ped] = std::move(block);
    return PlayerDataOf(ped);
}

static void DressCj(void* ped, bool createData) {
    if (!ped) return;
    auto& api = Clothes();
    if (!api.rebuild || !api.setTex) {
        ENGINE_LOGI("CJ clothes missing rebuild=%p set=%p",
            reinterpret_cast<void*>(api.rebuild), reinterpret_cast<void*>(api.setTex));
        return;
    }
    if (api.setStat) {
        api.setStat(21, 0.f);
        api.setStat(23, 0.f);
    }
    void* data = EnsurePlayerData(ped, createData);
    if (data && api.allocData) api.allocData(data);
    void* desc = data ? *reinterpret_cast<void**>(static_cast<unsigned char*>(data) + 8) : nullptr;
    if (!desc) {
        ENGINE_LOGI("CJ clothes desc missing ped=%p", ped);
        return;
    }
    api.setTex(desc, "vestblack", "vest", 0);
    api.setTex(desc, "PLAYER_FACE", "HEAD", 1);
    api.setTex(desc, "JEANSDENIM", "JEANS", 2);
    api.setTex(desc, "SNEAKERBINCBLK", "SNEAKER", 3);
    if (g_requestSpecial) g_requestSpecial(0, "player", 26);
    if (g_loadAll) g_loadAll(true);
    api.rebuild(ped, false);
    if (g_loadAll) g_loadAll(true);
    ENGINE_LOGI("CJ dressed ped=%p desc=%p", ped, desc);
}

// CClientPed::RebuildModel for a remote CJ (CClientPed.cpp 6584-6626): AddAllToModel writes each slot into the ped's
// own CPedClothesDesc, then ProcessRebuildPlayer -> CMultiplayerSA::RebuildMultiplayerPlayer (CMultiplayerSA.cpp
// 6015-6047) swaps in the remote player's stats, runs CClothes::RebuildPlayer(ped, false) and restores them.
// Needs the body's private player data (PlayerPedPort AttachPrivatePlayerData): with the shared slot this rebuild
// would change every remote body. Deviation: PLAYER_STATS is not read, so there are no remote stats to swap in;
// RebuildPlayer runs with bIgnoreFatAndMuscle = true and the local stats are never touched.
// CClothes::RebuildPlayer (gta-reversed Clothes.cpp 194-211) rebuilds the model-0 template from this descriptor and
// re-dresses only this ped; the local player keeps the clump it already has, as on desktop.
// CClothesBuilder::ConstructGeometryArray requests each clothing DFF from PLAYER.IMG, calls
// CStreaming::LoadAllRequestedModels(true) and then uses the model's clump with no null check. With streaming still busy
// (login: world, resources and the local player's own rebuild) the clump came back null and BlendGeometry crashed, with
// all four pieces valid (device 2026-11-14 04:15:06 and 04:26:47, both within 0.3 s of the local "CJ dressed"). Later
// rebuilds in a session never failed. A remote rebuild therefore waits until streaming is idle:
// CStreaming::IsVeryBusy (0x392dc4: a channel active or more than 5 requests) is false and
// CStreaming::ms_numModelsRequested is 0. Desktop's load is effectively synchronous, so it has no such wait.
static bool StreamingIdle() {
    static bool (*veryBusy)() = nullptr;
    static const int* requested = nullptr;
    static bool looked = false;
    if (!looked) {
        looked = true;
        if (void* handle = dlopen("libGTASA.so", RTLD_NOLOAD | RTLD_GLOBAL)) {
            veryBusy = reinterpret_cast<bool (*)()>(dlsym(handle, "_ZN10CStreaming10IsVeryBusyEv"));
            requested = static_cast<const int*>(dlsym(handle, "_ZN10CStreaming21ms_numModelsRequestedE"));
        }
        if (!veryBusy || !requested) ENGINE_LOGE("DIAG StreamingIdle: IsVeryBusy=%p ms_numModelsRequested=%p",
            reinterpret_cast<void*>(veryBusy), static_cast<const void*>(requested));
    }
    if (!veryBusy || !requested) return false;  // unknown: never rebuild blind
    return !veryBusy() && *requested == 0;
}

static bool DressRemoteCj(void* ped, const std::vector<WorldRPG::Entity::PedClothes>& clothes) {
    if (!ped || !HasPrivatePlayerData(ped)) return false;
    // Nothing is requested while streaming is busy, so a waiting rebuild does not keep the queue full.
    if (!StreamingIdle()) return false;
    auto& api = Clothes();
    if (!api.rebuild || !api.setTex) return false;
    void* data = PlayerDataOf(ped);
    void* desc = data ? *reinterpret_cast<void**>(static_cast<unsigned char*>(data) + 8) : nullptr;
    if (!desc) return false;
    if (clothes.empty()) {
        api.setTex(desc, "vestblack", "vest", 0);
        api.setTex(desc, "PLAYER_FACE", "HEAD", 1);
        api.setTex(desc, "JEANSDENIM", "JEANS", 2);
        api.setTex(desc, "SNEAKERBINCBLK", "SNEAKER", 3);
    } else {
        // CClientPlayerClothes::AddClothes -> GetClothing (CClientPlayerClothes.cpp 344-360, GetClothing by name):
        // only a (texture, model) pair listed for that slot in m_DefaultClothes is kept, compared case-insensitively.
        // CClothesBuilder::ConstructGeometryArray finds each model in PLAYER.IMG with only a VERIFY, so an unlisted name
        // loads nothing and BlendGeometry walks a null clump (device 2026-11-14 04:15:06, RpClumpForAllAtomics with
        // x0 = 0 under CClothes::RebuildPlayer). The phone passed PLAYER_CLOTHES through unchecked.
        for (const auto& item : clothes) {
            bool listed = false;
            if (item.type < WorldRPG::Game::PlayerClothesTable::kSlots) {
                const auto& slot = WorldRPG::Game::PlayerClothesTable::kTable[item.type];
                for (int i = 0; i < slot.count && !listed; ++i)
                    listed = strcasecmp(slot.items[i].texture, item.texture.c_str()) == 0 &&
                             strcasecmp(slot.items[i].model, item.model.c_str()) == 0;
            }
            static int itemReports = 0;
            if (itemReports < 24) {
                ++itemReports;
                ENGINE_LOGI("DIAG remote CJ clothes ped=%p type=%u texture=%s model=%s %s", ped,
                    static_cast<unsigned>(item.type), item.texture.c_str(), item.model.c_str(), listed ? "applied" : "rejected");
            }
            if (listed) api.setTex(desc, item.texture.c_str(), item.model.c_str(), item.type);
        }
    }
    if (g_requestSpecial) g_requestSpecial(0, "player", 26);
    if (g_loadAll) g_loadAll(true);
    if (!StreamingIdle()) {
        static int waits = 0;
        if (waits < 6) {
            ++waits;
            ENGINE_LOGI("DIAG remote CJ rebuild ped=%p waits: streaming busy", ped);
        }
        return false;  // the caller keeps the clothes dirty and tries again on a later frame
    }
    api.rebuild(ped, true);
    static int noted = 0;
    if (noted < 4) {
        ++noted;
        ENGINE_LOGI("DIAG remote CJ rebuilt ped=%p clothes=%zu (default outfit when 0)", ped, clothes.size());
    }
    return true;
}

static void DressLocalPlayer() {
    if (!g_findPlayer) return;
    DressCj(g_findPlayer(0), false);
}

struct TaskFns {
    bool ready = false;
    void* (*taskNew)(unsigned long) = nullptr;
    void (*wanderCtor)(void*, int, unsigned char, bool, float) = nullptr;
    void (*driveWanderCtor)(void*, void*, int, float) = nullptr;
    void* (*findTaskByType)(const void*, int) = nullptr;
    void (*setPrimary)(void*, void*, int, bool) = nullptr;
    void (*addPrimary)(void*, void*, bool) = nullptr;
    void (*setSecondary)(void*, void*, int) = nullptr;
    void* (*getSecondary)(void*, int) = nullptr;
    void (*duck)(void*, unsigned short) = nullptr;
    void (*clearDuck)(void*) = nullptr;
    void (*clearTasks)(void*, bool, bool) = nullptr;
    void (*fightCtor)(void*, void*, int, unsigned) = nullptr;
    void (*setMoveState)(void*, int) = nullptr;
    void (*setMoveAnim)(void*) = nullptr;
    void (*worldRemove)(void*) = nullptr;
    void (*carCtor)(void*, int, unsigned char, unsigned char) = nullptr;
    void (*carDtor)(void*) = nullptr;
    // CCarCtrl::CreateCarForScript(int, CVector, unsigned char). Vec3 has the same layout as CVector (3 floats).
    void* (*createCarForScript)(int, Vec3, unsigned char) = nullptr;
    // CVehicle* FindPlayerVehicle(int playerId, bool bIncludeRemote). playerId -1 is the local player.
    void* (*findPlayerVehicle)(int, bool) = nullptr;
    bool (*isVehicleDriver)(const void*, const void*) = nullptr;
    void (*requestVehicleUpgrade)(int, int) = nullptr;
    bool (*hasVehicleUpgradeLoaded)(int) = nullptr;
    int (*addVehicleUpgrade)(void*, int) = nullptr;
    void (*removeVehicleUpgrade)(void*, int) = nullptr;
    // CPad (libGTASA 2.10). GetPad(0) is the local pad; the getters are CPad members, so they take the pad.
    void* (*getPad)(int) = nullptr;
    int (*padSteering)(void*) = nullptr;       // CPad::GetSteeringLeftRight, -128..128
    int (*padAccelerate)(void*) = nullptr;     // CPad::GetAccelerate, 0..255
    int (*padBrake)(void*) = nullptr;          // CPad::GetBrake, 0..255
    int (*padHandBrake)(void*) = nullptr;      // CPad::GetHandBrake, nonzero when pressed
    bool (*padHorn)(void*, bool) = nullptr;    // CPad::GetHorn(bool), the bool is unused on libGTASA 2.10
    bool (*padLookLeft)(void*, bool) = nullptr;   // CPad::GetLookLeft(bool), true includes the touch look widget
    bool (*padLookRight)(void*, bool) = nullptr;  // CPad::GetLookRight(bool), same
    bool (*padNitroFired)(void*) = nullptr;       // CPad::GetNitroFired refreshes the native nitro widget
    void* wanderVtable = nullptr;
};

static TaskFns& Tasks() {
    static TaskFns api;
    if (api.ready) return api;
    api.ready = true;
    void* handle = dlopen("libGTASA.so", RTLD_NOLOAD | RTLD_GLOBAL);
    if (!handle) return api;
    api.taskNew = reinterpret_cast<void* (*)(unsigned long)>(dlsym(handle, "_ZN5CTasknwEm"));
    api.wanderCtor = reinterpret_cast<void (*)(void*, int, unsigned char, bool, float)>(
        dlsym(handle, "_ZN18CTaskComplexWanderC2Eihbf"));
    api.addPrimary = reinterpret_cast<void (*)(void*, void*, bool)>(
        dlsym(handle, "_ZN16CPedIntelligence26AddTaskPrimaryMaybeInGroupEP5CTaskb"));
    api.setSecondary = reinterpret_cast<void (*)(void*, void*, int)>(
        dlsym(handle, "_ZN12CTaskManager16SetTaskSecondaryEP5CTaski"));
    api.getSecondary = reinterpret_cast<void* (*)(void*, int)>(
        dlsym(handle, "_ZNK12CTaskManager16GetTaskSecondaryEi"));
    api.duck = reinterpret_cast<void (*)(void*, unsigned short)>(
        dlsym(handle, "_ZN16CPedIntelligence20SetTaskDuckSecondaryEt"));
    api.clearDuck = reinterpret_cast<void (*)(void*)>(
        dlsym(handle, "_ZN16CPedIntelligence22ClearTaskDuckSecondaryEv"));
    api.clearTasks = reinterpret_cast<void (*)(void*, bool, bool)>(
        dlsym(handle, "_ZN16CPedIntelligence10ClearTasksEbb"));
    api.fightCtor = reinterpret_cast<void (*)(void*, void*, int, unsigned)>(
        dlsym(handle, "_ZN16CTaskSimpleFightC1EP7CEntityij"));
    api.setMoveState = reinterpret_cast<void (*)(void*, int)>(dlsym(handle, "_ZN4CPed12SetMoveStateE10eMoveState"));
    api.setMoveAnim = reinterpret_cast<void (*)(void*)>(dlsym(handle, "_ZN4CPed11SetMoveAnimEv"));
    api.worldRemove = reinterpret_cast<void (*)(void*)>(dlsym(handle, "_ZN6CWorld6RemoveEP7CEntity"));
    api.carCtor = reinterpret_cast<void (*)(void*, int, unsigned char, unsigned char)>(
        dlsym(handle, "_ZN11CAutomobileC1Eihh"));
    api.carDtor = reinterpret_cast<void (*)(void*)>(dlsym(handle, "_ZN11CAutomobileD1Ev"));
    api.createCarForScript = reinterpret_cast<void* (*)(int, Vec3, unsigned char)>(
        dlsym(handle, "_ZN8CCarCtrl18CreateCarForScriptEi7CVectorh"));
    api.findPlayerVehicle = reinterpret_cast<void* (*)(int, bool)>(dlsym(handle, "_Z17FindPlayerVehicleib"));
    api.isVehicleDriver = reinterpret_cast<decltype(api.isVehicleDriver)>(dlsym(handle, "_ZNK8CVehicle8IsDriverEPK4CPed"));
    api.requestVehicleUpgrade = reinterpret_cast<decltype(api.requestVehicleUpgrade)>(dlsym(handle, "_ZN10CStreaming21RequestVehicleUpgradeEii"));
    api.hasVehicleUpgradeLoaded = reinterpret_cast<decltype(api.hasVehicleUpgradeLoaded)>(dlsym(handle, "_ZN10CStreaming23HasVehicleUpgradeLoadedEi"));
    api.addVehicleUpgrade = reinterpret_cast<decltype(api.addVehicleUpgrade)>(dlsym(handle, "_ZN8CVehicle17AddVehicleUpgradeEi"));
    api.removeVehicleUpgrade = reinterpret_cast<decltype(api.removeVehicleUpgrade)>(dlsym(handle, "_ZN8CVehicle20RemoveVehicleUpgradeEi"));
    api.getPad = reinterpret_cast<void* (*)(int)>(dlsym(handle, "_ZN4CPad6GetPadEi"));
    api.padSteering = reinterpret_cast<int (*)(void*)>(dlsym(handle, "_ZN4CPad20GetSteeringLeftRightEv"));
    api.padAccelerate = reinterpret_cast<int (*)(void*)>(dlsym(handle, "_ZN4CPad13GetAccelerateEv"));
    api.padBrake = reinterpret_cast<int (*)(void*)>(dlsym(handle, "_ZN4CPad8GetBrakeEv"));
    api.padHandBrake = reinterpret_cast<int (*)(void*)>(dlsym(handle, "_ZN4CPad12GetHandBrakeEv"));
    api.padHorn = reinterpret_cast<bool (*)(void*, bool)>(dlsym(handle, "_ZN4CPad7GetHornEb"));
    api.padLookLeft = reinterpret_cast<bool (*)(void*, bool)>(dlsym(handle, "_ZN4CPad11GetLookLeftEb"));
    api.padLookRight = reinterpret_cast<bool (*)(void*, bool)>(dlsym(handle, "_ZN4CPad12GetLookRightEb"));
    api.padNitroFired = reinterpret_cast<decltype(api.padNitroFired)>(dlsym(handle, "_ZN4CPad13GetNitroFiredEv"));
    api.driveWanderCtor = reinterpret_cast<decltype(api.driveWanderCtor)>(
        dlsym(handle, "_ZN26CTaskComplexCarDriveWanderC1EP8CVehicleif"));
    if (!api.driveWanderCtor)
        api.driveWanderCtor = reinterpret_cast<decltype(api.driveWanderCtor)>(
            dlsym(handle, "_ZN26CTaskComplexCarDriveWanderC2EP8CVehicleif"));
    api.findTaskByType = reinterpret_cast<decltype(api.findTaskByType)>(
        dlsym(handle, "_ZNK16CPedIntelligence14FindTaskByTypeEi"));
    api.setPrimary = reinterpret_cast<decltype(api.setPrimary)>(
        dlsym(handle, "_ZN12CTaskManager7SetTaskEP5CTaskib"));
    api.wanderVtable = dlsym(handle, "_ZTV26CTaskComplexWanderStandard");
    return api;
}

// Driver controls for LocalCarState, read from the local pad on the game thread. A missing getter or pad leaves
// its field at zero. The pad is the same CPad::GetPad(0) that PlayerPedPort.cpp uses.
static void SamplePadControls(const TaskFns& api, LocalCarState& c) {
    if (!api.getPad) return;
    void* pad = api.getPad(0);
    if (!pad) return;
    // These getters are called through dlsym, which skips the GOT mask of ControlMask.cpp, so toggleControl is
    // applied here. Desktop sends the pad after CClientPad::ProcessAllToggledControls (CClientPad.cpp:781-870).
    using Controls::Getter;
    using Controls::MaskGetter;
    if (api.padSteering) c.steering = static_cast<int16_t>(std::clamp(MaskGetter(Getter::SteeringLeftRight, api.padSteering(pad)), -128, 128));
    if (api.padAccelerate) c.accelerate = static_cast<uint8_t>(std::clamp(MaskGetter(Getter::Accelerate, api.padAccelerate(pad)), 0, 255));
    if (api.padBrake) c.brake = static_cast<uint8_t>(std::clamp(MaskGetter(Getter::Brake, api.padBrake(pad)), 0, 255));
    if (api.padHandBrake) c.handBrake = MaskGetter(Getter::HandBrake, api.padHandBrake(pad)) != 0;
    if (api.padHorn) c.horn = MaskGetter(Getter::Horn, api.padHorn(pad, false)) != 0;
    if (api.padLookLeft) c.lookLeft = MaskGetter(Getter::LookLeft, api.padLookLeft(pad, true)) != 0;
    if (api.padLookRight) c.lookRight = MaskGetter(Getter::LookRight, api.padLookRight(pad, true)) != 0;
}

// On-foot stick and camera, sampled on the game thread for SendPureSync / SendKeySync.
// TheCamera is the verified global (GTASAOffsets.h, nm "TheCamera" at 0xBBA8D0).
// Orientation is +0x154 (CCamera::Process store at 0x4bbcc0; PlayerControlZelda load at 0x655a28).
static void SampleLocalPadAndCamera() {
    TaskFns& api = Tasks();
    if (api.findPlayerVehicle && api.findPlayerVehicle(-1, false)) {
        // GetPedWalk* and GetSprint call IsTouched(..., 1), which enables
        // their widgets (0x36fcd4-0x36fcf4). Sample only the driving pad in a car.
        g_padStickValid = false;
        g_localSprint = false;
        g_localAttackHeld = false;
        g_localAttackLatch = 0;
    } else if (!api.getPad) {
        g_padStickValid = false;
        static bool loggedPad = false;
        if (!loggedPad) {
            loggedPad = true;
            ENGINE_LOGI("DIAG on-foot stick: CPad::GetPad (_ZN4CPad6GetPadEi) is missing; "
                        "CopyOnFoot still builds the stick from move speed");
        }
    } else {
        void* pad = api.getPad(0);
        if (!pad) {
            g_padStickValid = false;
        } else {
            // CNetAPI::WriteKeysync / WritePlayerPuresync send GetCurrentControllerState, the stick GTA itself walks
            // the local ped with. On a touch device that is not NewState at pad+0 / +2 (always 0 there): the walk
            // getters return the touch joystick (CPad::GetPedWalkLeftRight 0x4dcd40 / GetPedWalkUpDown 0x4dcdd4,
            // IsTouched widget 0xa7), so they are called here. Desktop observers saw a released stick and no walk
            // cycle for the phone player (device 2026-11-14). The NewState shorts remain the fallback.
            // CPad::GetPedWalkLeftRight / GetPedWalkUpDown return a short in w0 via ldrh on the non-touch path
            // (zero-extended into w0); declaring int16_t ensures negative values are sign-extended before clamping.
            using WalkFn = int16_t (*)(void*);
            static WalkFn walkLR = nullptr, walkUD = nullptr;
            static bool (*getSprint)(void*, int) = nullptr;
            static bool lookedWalk = false;
            if (!lookedWalk) {
                lookedWalk = true;
                if (void* handle = dlopen("libGTASA.so", RTLD_NOLOAD | RTLD_GLOBAL)) {
                    walkLR = reinterpret_cast<WalkFn>(dlsym(handle, "_ZN4CPad19GetPedWalkLeftRightEv"));
                    walkUD = reinterpret_cast<WalkFn>(dlsym(handle, "_ZN4CPad16GetPedWalkUpDownEv"));
                    getSprint = reinterpret_cast<bool (*)(void*, int)>(dlsym(handle, "_ZN4CPad9GetSprintEi"));
                }
                if (!walkLR || !walkUD || !getSprint)
                    ENGINE_LOGI("DIAG local controller: GetPedWalkLeftRight=%p GetPedWalkUpDown=%p GetSprint=%p",
                        reinterpret_cast<void*>(walkLR), reinterpret_cast<void*>(walkUD), reinterpret_cast<void*>(getSprint));
            }
            int16_t sx = 0, sy = 0;
            if (walkLR && walkUD) {
                sx = static_cast<int16_t>(std::clamp(static_cast<int>(walkLR(pad)), -128, 128));
                sy = static_cast<int16_t>(std::clamp(static_cast<int>(walkUD(pad)), -128, 128));
            } else {
                std::memcpy(&sx, static_cast<unsigned char*>(pad) + 0, sizeof sx);
                std::memcpy(&sy, static_cast<unsigned char*>(pad) + 2, sizeof sy);
            }
            // The same dlsym bypass: apply left/right/forwards/backwards and sprint (CClientPad.cpp:803-870).
            sx = static_cast<int16_t>(Controls::MaskGetter(Controls::Getter::PedWalkLeftRight, sx));
            sy = static_cast<int16_t>(Controls::MaskGetter(Controls::Getter::PedWalkUpDown, sy));
            g_padStickX = sx;
            g_padStickY = sy;
            g_padStickValid = true;
            // ButtonCross: CPad::GetSprint (IsTouched 0xa8 / 0x1f). The int argument is the
            // sprint type the game passes; 0 is the on-foot form.
            g_localSprint = getSprint && Controls::MaskGetter(Controls::Getter::Sprint, getSprint(pad, 0)) != 0;
            // ButtonCircle: the attack widget (0x1) that CPad::GetMeleeAttack and GetWeapon read with IsTouched.
            // Desktop sends the button, so each tap is a separate press; the phone used to send "an attack task
            // exists", which stays set through a whole combo, so desktop only played the first punch.
            static bool (*isTouched)(int, void*, int) = nullptr;
            static int (*getWeapon)(void*, void*, bool) = nullptr;
            static bool lookedTouch = false;
            if (!lookedTouch) {
                lookedTouch = true;
                if (void* handle = dlopen("libGTASA.so", RTLD_NOLOAD | RTLD_GLOBAL)) {
                    isTouched = reinterpret_cast<bool (*)(int, void*, int)>(
                        dlsym(handle, "_ZN15CTouchInterface9IsTouchedENS_9WidgetIDsEP9CVector2Di"));
                    getWeapon = reinterpret_cast<int (*)(void*, void*, bool)>(
                        dlsym(handle, "_ZN4CPad9GetWeaponEP4CPedb"));
                }
            }
            if (isTouched || getWeapon) {
                float vec[2] = {0.f, 0.f};
                void* ped = g_findPlayer ? g_findPlayer(0) : nullptr;
                // GetWeapon reads the fire widgets used with a weapon camera in
                // addition to widget 1 (libGTASA 2.10, 0x4ddb20-0x4ddcf8).
                const bool held = !Controls::IsGtaControlDisabled(Controls::Gta::Fire) &&
                    ((getWeapon && ped && getWeapon(pad, ped, false)) || (isTouched && isTouched(0x1, vec, 0)));
                if (held && !g_localAttackHeld.load()) g_localAttackLatch = ++g_controlPressSequence;
                g_localAttackHeld = held;
            }
            static int stickReports = 0;
            if ((sx != 0 || sy != 0) && stickReports < 3) {
                ++stickReports;
                ENGINE_LOGI("DIAG local controller stick %d %d sprint=%d", sx, sy, g_localSprint.load() ? 1 : 0);
            }
        }
    }
    static uintptr_t base = 0;
    static bool looked = false;
    if (!looked) {
        looked = true;
        base = Hook::MemoryPatcher::getLibraryBaseAddress("libGTASA.so");
    }
    if (!base) {
        g_cameraRotValid = false;
        static bool loggedCam = false;
        if (!loggedCam) {
            loggedCam = true;
            ENGINE_LOGI("DIAG GetCameraRotation: libGTASA base missing, TheCamera+0x154 "
                        "(CCameraSA::GetCameraRotation, CCameraSA.cpp 651-654) is not read");
        }
        return;
    }
    const float orient = *reinterpret_cast<const float*>(base + 0xBBA8D0 + 0x154);
    if (!std::isfinite(orient)) {
        g_cameraRotValid = false;
        return;
    }
    g_cameraRot = orient;
    g_cameraRotValid = true;
}

static void* PedIntel(void* ped) {
    if (!ped) return nullptr;
    auto* raw = static_cast<unsigned char*>(ped);
    void* intel = *reinterpret_cast<void**>(raw + 0x538);
    const auto addr = reinterpret_cast<uintptr_t>(intel);
    if (!intel || (addr & 3u) != 0 || addr < 0x10000u) return nullptr;
    return intel;
}

int NativeVehicleSeat(void* vehicle, void* ped) {
    if (!vehicle || !ped) return -1;
    auto& api = Tasks();
    if (api.isVehicleDriver && api.isVehicleDriver(vehicle, ped)) return 0;
    static void* lib = dlopen("libGTASA.so", RTLD_NOLOAD | RTLD_GLOBAL);
    static auto* code = lib ? static_cast<const uint32_t*>(dlsym(lib, "_ZNK8CVehicle11IsPassengerEPK4CPed")) : nullptr;
    // Eight sequential passenger pointers in CVehicle::IsPassenger(CPed const*).
    if (!code || code[1] != 0xf942d408 || code[0x58 / 4] != 0xf942f008) return -1;
    auto seats = reinterpret_cast<void* const*>(static_cast<const uint8_t*>(vehicle) + 0x5a8);
    for (int i = 0; i < 8; ++i) if (seats[i] == ped) return i + 1;
    return -1;
}

void* NativeDrivebyTask(void* ped) {
    void* intel = PedIntel(ped);
    if (!intel) return nullptr;
    void* task = reinterpret_cast<void**>(static_cast<uint8_t*>(intel) + 8)[3];
    static void* lib = dlopen("libGTASA.so", RTLD_NOLOAD | RTLD_GLOBAL);
    static auto* vtable = lib ? static_cast<void**>(dlsym(lib, "_ZTV22CTaskSimpleGangDriveBy")) : nullptr;
    return task && vtable && *static_cast<void***>(task) == vtable + 2 ? task : nullptr;
}

bool HasNativeDriveby(void* ped) { return NativeDrivebyTask(ped) != nullptr; }

void SetNativeDriveby(void* ped, void* vehicle, uint8_t seat, bool enabled) {
    auto& api = Tasks();
    void* intel = PedIntel(ped);
    if (!intel) return;
    static void* lib = dlopen("libGTASA.so", RTLD_NOLOAD | RTLD_GLOBAL);
    if (!lib) return;
    using Abort = bool (*)(void*, void*, int, const void*);
    using Construct = void (*)(void*, void*, const Vec3*, float, int8_t, int8_t, bool);
    using Open = void (*)(void*, uint8_t);
    static const auto abort = reinterpret_cast<Abort>(dlsym(lib, "_ZN22CTaskSimpleGangDriveBy13MakeAbortableEP4CPediPK6CEvent"));
    static const auto construct = reinterpret_cast<Construct>(dlsym(lib, "_ZN22CTaskSimpleGangDriveByC1EP7CEntityPK7CVectorfaab"));
    static const auto window = reinterpret_cast<Open>(dlsym(lib, "_ZN8CVehicle17SetWindowOpenFlagEh"));
    if (void* task = NativeDrivebyTask(ped)) {
        if (!enabled && abort) {
            SeatPlayerContext context(ped);
            if (context.ready) abort(task, ped, 1, nullptr);
        }
        return;
    }
    static const auto* clone = static_cast<const uint32_t*>(dlsym(lib, "_ZNK22CTaskSimpleGangDriveBy5CloneEv"));
    if (!enabled || !vehicle || !DrivebyAimHookReady() || !clone || clone[0x1c / 4] != 0x321b07e0 ||
        !api.taskNew || !api.setPrimary || !construct) return;
    SeatPlayerContext context(ped);
    if (!context.ready) return;
    if (void* task = api.taskNew(0x60)) {
        construct(task, nullptr, nullptr, 0.f, 0, 0, (seat & 1u) != 0);
        api.setPrimary(static_cast<uint8_t*>(intel) + 8, task, 3, false);
        constexpr uint8_t windowNodes[] = {10, 8, 11, 9};
        if (seat < 4 && window) window(vehicle, windowNodes[seat]);
    }
}

static void SampleLocalTasks(void* ped) {
    TaskFns& api = Tasks();
    void* intel = PedIntel(ped);
    if (!intel || !api.getSecondary) {
        g_localFire = false;
        g_localDuck = false;
        return;
    }
    void* manager = static_cast<unsigned char*>(intel) + 8;
    g_localFire = api.getSecondary(manager, 0) != nullptr;
    g_localDuck = api.getSecondary(manager, 1) != nullptr;
    // ButtonSquare (jump). CPad::GetJump / JumpJustDown go through CTouchInterface::IsDoubleTapped with the clear flag
    // set, so calling them here could take the tap away from the game. The jump is read from its result instead:
    // CPedIntelligence::GetTaskInAir becomes non-null while the ped is airborne, and a jump leaves the ground with
    // upward speed (CPhysical+0x68 z), a fall does not. The press is latched until one on-foot packet carries it.
    static void* (*getInAir)(void*) = nullptr;
    static bool lookedInAir = false;
    if (!lookedInAir) {
        lookedInAir = true;
        if (void* handle = dlopen("libGTASA.so", RTLD_NOLOAD | RTLD_GLOBAL))
            getInAir = reinterpret_cast<void* (*)(void*)>(dlsym(handle, "_ZNK16CPedIntelligence12GetTaskInAirEv"));
    }
    static bool wasInAir = false;
    const bool inAir = getInAir && getInAir(intel) != nullptr;
    const float vz = reinterpret_cast<const float*>(static_cast<unsigned char*>(ped) + 0x68)[2];
    if (inAir && !wasInAir && vz > 0.04f) g_localJumpLatch = ++g_controlPressSequence;
    wasInAir = inAir;
}

static bool StartWander(void* body, int moveState, int direction, bool sensible) {
    TaskFns& api = Tasks();
    void* intel = PedIntel(body);
    if (!intel || !api.taskNew || !api.wanderCtor || !api.wanderVtable || !api.addPrimary) {
        static bool reported = false;
        if (!reported) {
            reported = true;
            ENGINE_LOGI("DIAG setPedWander: native WanderStandard dispatch is unavailable");
        }
        return false;
    }
    void* task = api.taskNew(0x40);
    if (!task) return false;
    std::memset(task, 0, 0x40);
    api.wanderCtor(task, moveState, static_cast<unsigned char>(direction), sensible, 0.f);
    *reinterpret_cast<void**>(task) = static_cast<unsigned char*>(api.wanderVtable) + 16;
    // CPedIntelligence::AddTaskPrimaryMaybeInGroup owns and destroys the input task.
    api.addPrimary(intel, task, false);
    return true;
}

static void StopLostTrafficTask(RemoteBody& stored) {
    if (!stored.trafficTask) return;
    void* intel = PedIntel(stored.ped);
    auto& api = Tasks();
    if (intel && api.findTaskByType && api.setPrimary && api.findTaskByType(intel, stored.trafficTask))
        api.setPrimary(static_cast<unsigned char*>(intel) + 8, nullptr, 3, false);
    stored.trafficTask = 0;
}

static void PlayPunch(void* body) {
    TaskFns& api = Tasks();
    void* intel = PedIntel(body);
    if (!intel || !api.taskNew || !api.fightCtor || !api.setSecondary) return;
    void* task = api.taskNew(0x80);
    if (!task) return;
    std::memset(task, 0, 0x80);
    void* target = g_findPlayer ? g_findPlayer(0) : nullptr;
    api.fightCtor(task, target, 0, 0u);
    api.setSecondary(static_cast<unsigned char*>(intel) + 8, task, 0);
}

// Optional on-device verification reads the actual gait association left by the previous
// GTA frame. SetBlend (0x4620e4) uses +0x30 and SetCurrentTime (0x461fc4) uses +0x38.
static void TraceObserverGait(uint32_t id, void* body, float speed, bool driving, int64_t lease, int64_t now) {
    static const bool enabled = Debug::Flag("traceobservergait");
    static int64_t nextMoving = 0, nextIdle = 0;
    auto& next = speed > 0.009f ? nextMoving : nextIdle;
    if (!enabled || now < next) return;
    next = now + (speed > 0.009f ? 1000 : 10000);
    using Lookup = void* (*)(void*, unsigned);
    static const Lookup association = [] {
        void* handle = dlopen("libGTASA.so", RTLD_NOLOAD | RTLD_GLOBAL);
        return handle ? reinterpret_cast<Lookup>(dlsym(handle, "_Z30RpAnimBlendClumpGetAssociationP7RpClumpj")) : nullptr;
    }();
    void* clump = *reinterpret_cast<void**>(static_cast<uint8_t*>(body) + 0x20);
    ENGINE_LOGI("Observer controller id=%u speed=%.3f driving=%d lease=%lld lookup=%d", id, speed,
        int(driving), static_cast<long long>(lease - now), int(association != nullptr));
    if (!clump || !association) return;
    for (unsigned animation = 0; animation <= 3; ++animation) {
        auto* assoc = static_cast<uint8_t*>(association(clump, animation));
        if (assoc)
            ENGINE_LOGI("Observer gait id=%u speed=%.3f anim=%u blend=%.3f phase=%.3f", id, speed, animation,
                *reinterpret_cast<float*>(assoc + 0x30), *reinterpret_cast<float*>(assoc + 0x38));
    }
}

static void PresentOnFoot(void* body, RemoteBody& stored, const RemotePed& ped, bool scriptedPunch = true) {
    TaskFns& api = Tasks();
    const float speed = std::sqrt(ped.vx * ped.vx + ped.vy * ped.vy);
    // The 0x0030 peer sees spatial velocity only. Match the server's legacy
    // traffic presentation thresholds and settle to idle once after a stop.
    if (api.setMoveState && api.setMoveAnim && !ped.hasAnimCache &&
        !(ped.serverPed && RemoteControlHookActive())) {
        const bool moving = std::isfinite(speed) && speed > (ped.serverPed ? 0.02f : 0.03f);
        if (moving || (ped.serverPed && stored.observerWalking)) {
            const int state = !moving ? 1 : speed > 0.27f && ped.serverPed ? 7 : speed > 0.14f ? 6 : 4;
            api.setMoveState(body, state);
            api.setMoveAnim(body);
        }
        stored.observerWalking = moving;
    }
    void* intel = PedIntel(body);
    if (!intel) return;
    if (ped.duck && !stored.ducked && api.duck) api.duck(intel, 0);
    if (!ped.duck && stored.ducked && api.clearDuck) api.clearDuck(intel);
    stored.ducked = ped.duck;
    if (!ped.fire || !scriptedPunch) return;
    const int64_t now = static_cast<int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
    if (now - stored.lastFight < 650) return;
    stored.lastFight = now;
    PlayPunch(body);
}

static bool ReadBodyPos(void* body, float& x, float& y, float& z, float& heading) {
    auto* raw = static_cast<unsigned char*>(body);
    auto* matrix = *reinterpret_cast<unsigned char**>(raw + 0x18);
    const auto matrixAddr = reinterpret_cast<uintptr_t>(matrix);
    if (!matrix || (matrixAddr & 3u) != 0 || matrixAddr < 0x10000u) return false;
    x = *reinterpret_cast<float*>(matrix + 0x30);
    y = *reinterpret_cast<float*>(matrix + 0x34);
    z = *reinterpret_cast<float*>(matrix + 0x38);
    // FindPlayerHeading (libGTASA 2.10, 0x4f0720) reads the forward row at +0x10.
    // +0x20 is up. CMatrix::SetRotateZOnly stores (0, 0, 1) there, so a heading
    // taken from that row is 0 for an upright ped.
    heading = std::atan2(-*reinterpret_cast<float*>(matrix + 0x10), *reinterpret_cast<float*>(matrix + 0x14));
    return PlausiblePosition(x, y, z);
}

static void WriteBodyPos(void* body, float x, float y, float z) {
    auto* raw = static_cast<unsigned char*>(body);
    auto* matrix = *reinterpret_cast<unsigned char**>(raw + 0x18);
    const auto matrixAddr = reinterpret_cast<uintptr_t>(matrix);
    if (matrix && (matrixAddr & 3u) == 0 && matrixAddr > 0x10000u) {
        *reinterpret_cast<float*>(matrix + 0x30) = x;
        *reinterpret_cast<float*>(matrix + 0x34) = y;
        *reinterpret_cast<float*>(matrix + 0x38) = z;
    }
    *reinterpret_cast<float*>(raw + 0x08) = x;
    *reinterpret_cast<float*>(raw + 0x0C) = y;
    *reinterpret_cast<float*>(raw + 0x10) = z;
}

// Desktop CClientVehicle::SetRotationDegrees. The matrix rows are those of CMatrix::SetRotation (Shared/sdk/CMatrix.h
// 253-279), the inverse of the CMatrix::GetRotation that SampleHostedCar reads back, and the current row lengths
// (scale) are kept, as SetRotation does. Rows: right (+0x00), forward (+0x10), up (+0x20), as in SampleHostedCar.
static bool WriteBodyRotation(void* body, float rotXDeg, float rotYDeg, float rotZDeg) {
    auto* raw = static_cast<unsigned char*>(body);
    auto* matrix = *reinterpret_cast<unsigned char**>(raw + 0x18);
    const auto matrixAddr = reinterpret_cast<uintptr_t>(matrix);
    if (!matrix || (matrixAddr & 3u) != 0 || matrixAddr < 0x10000u) return false;
    if (!std::isfinite(rotXDeg) || !std::isfinite(rotYDeg) || !std::isfinite(rotZDeg)) return false;
    constexpr float kDegToRad = 3.14159265f / 180.f;
    const float fCosX = std::cos(-rotXDeg * kDegToRad);
    const float fCosY = std::cos(-rotYDeg * kDegToRad);
    const float fCosZ = std::cos(rotZDeg * kDegToRad);
    const float fSinX = std::sin(-rotXDeg * kDegToRad);
    const float fSinY = std::sin(-rotYDeg * kDegToRad);
    const float fSinZ = std::sin(rotZDeg * kDegToRad);
    auto scaleOf = [matrix](unsigned offset) {
        const auto* v = reinterpret_cast<const float*>(matrix + offset);
        const float len = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        return (len > 1e-6f && std::isfinite(len)) ? len : 1.f;
    };
    const float sx = scaleOf(0x00), sy = scaleOf(0x10), sz = scaleOf(0x20);
    auto* right = reinterpret_cast<float*>(matrix + 0x00);
    auto* front = reinterpret_cast<float*>(matrix + 0x10);
    auto* up = reinterpret_cast<float*>(matrix + 0x20);
    right[0] = fCosY * fCosZ * sx;
    right[1] = fCosY * fSinZ * sx;
    right[2] = fSinY * sx;
    front[0] = (fSinX * fSinY * fCosZ - fCosX * fSinZ) * sy;
    front[1] = (fSinX * fSinY * fSinZ + fCosX * fCosZ) * sy;
    front[2] = -fSinX * fCosY * sy;
    up[0] = -(fCosX * fSinY * fCosZ + fSinX * fSinZ) * sz;
    up[1] = (fCosZ * fSinX - fCosX * fSinY * fSinZ) * sz;
    up[2] = fCosX * fCosY * sz;
    return true;
}

// Desktop CClientVehicle::SetHealth. CVehicle::m_fHealth is at +0x634 (see SampleHostedCar). The wire range of
// SVehicleHealthSync is 0 - 2047.5.
static void WriteBodyHealth(void* body, float health) {
    if (!std::isfinite(health) || health < 0.f) return;
    *reinterpret_cast<float*>(static_cast<unsigned char*>(body) + 0x634) = health;
}

// Live values for packet 56, read on the game thread like the ped snapshot. Desktop
// CUnoccupiedVehicleSync::WriteVehicleInformation uses CClientVehicle::GetPosition, GetRotationDegrees,
// GetMoveSpeed and GetTurnSpeed. GetRotationDegrees (mta_neon_source/Client/mods/deathmatch/logic/
// CClientVehicle.cpp 464) is 2*PI - ConvertMatrixToEulerAngles, wrapped to [0, 360). That equals the
// negated x and y of CMatrix::GetRotation (Shared/sdk/CMatrix.h 236, "0 - ConvertToEulerAngles") modulo
// 360. As in GetRotationMatrix (CMatrix.h 200), each matrix row is divided by its length, so scale is
// ignored. Health is CVehicle::m_fHealth at +0x634: CVehicle::InflictDamage (0x6a7848) does
// `health -= damage` on [this, #1588] and CAutomobile::BlowUpCar (0x67d3ac) stores 0 there.
static float WrapDegrees360(float value) {
    return value - 360.f * std::floor(value / 360.f);
}

static bool SampleHostedCar(void* body, HostedCar& out) {
    float x = 0, y = 0, z = 0, heading = 0;
    if (!ReadBodyPos(body, x, y, z, heading)) return false;
    auto* raw = static_cast<unsigned char*>(body);
    auto* matrix = *reinterpret_cast<unsigned char**>(raw + 0x18);
    // Rows: right (+0x00), forward (+0x10), up (+0x20). Position (+0x30) came from ReadBodyPos.
    auto unitRow = [matrix](unsigned offset, float (&row)[3]) {
        const auto* v = reinterpret_cast<const float*>(matrix + offset);
        const float len = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        if (!(len > 1e-6f) || !std::isfinite(len)) return false;
        row[0] = v[0] / len;
        row[1] = v[1] / len;
        row[2] = v[2] / len;
        return true;
    };
    float right[3] = {}, front[3] = {}, up[3] = {};
    if (!unitRow(0x00, right) || !unitRow(0x10, front) || !unitRow(0x20, up)) return false;

    // CMatrix::GetRotation (CMatrix.h 236): (-fRotX, -fRotY, fRotZ) in radians.
    const float fRotY = std::atan2(right[2], std::sqrt(right[0] * right[0] + right[1] * right[1]));
    const float fRotZ = std::atan2(right[1], right[0]);
    const float fSinZ = -std::sin(fRotZ);
    const float fCosZ = std::cos(fRotZ);
    const float fRotX = std::atan2(up[0] * fSinZ + up[1] * fCosZ, front[0] * fSinZ + front[1] * fCosZ);
    constexpr float kRadToDeg = 180.f / 3.14159265f;

    out.x = x;
    out.y = y;
    out.z = z;
    out.rotX = WrapDegrees360(-fRotX * kRadToDeg);
    out.rotY = WrapDegrees360(-fRotY * kRadToDeg);
    out.rotZ = WrapDegrees360(fRotZ * kRadToDeg);

    // CPhysical move speed (+0x68) and turn speed (+0x74), world units per frame.
    const auto* move = reinterpret_cast<const float*>(raw + 0x68);
    const auto* turn = reinterpret_cast<const float*>(raw + 0x74);
    out.vx = move[0];
    out.vy = move[1];
    out.vz = move[2];
    out.tx = turn[0];
    out.ty = turn[1];
    out.tz = turn[2];
    out.health = *reinterpret_cast<const float*>(raw + 0x634);
    // CAEVehicleAudioEntity::ProcessDummyVehicleEngine reads bit 4 at +0x568 (0x48e85c-0x48e860).
    out.engineOn = (raw[0x568] & 0x10) != 0;

    const float values[] = {out.health, out.x, out.y, out.z, out.rotX, out.rotY, out.rotZ, out.vx, out.vy, out.vz, out.tx, out.ty, out.tz};
    for (float v : values) {
        if (!std::isfinite(v)) return false;
    }
    return true;
}

// libGTASA 2.10 arm64. CPlaceable::CPlaceable (0x4ebd58) stores _ZTV10CPlaceable+0x10
// (Itanium: the object vptr skips the offset-to-top and typeinfo). Desktop
// IsPlaceableVTBL compares against that pointer (CPlaceableSA.h:29, 0x863C40 on x86).
static bool IsPlaceableVtable(void* body) {
    static void* placeable = nullptr;
    static bool looked = false;
    if (!looked) {
        looked = true;
        void* handle = dlopen("libGTASA.so", RTLD_NOLOAD | RTLD_GLOBAL);
        auto* symbol = handle ? static_cast<unsigned char*>(dlsym(handle, "_ZTV10CPlaceable")) : nullptr;
        if (symbol) placeable = symbol + 16;
    }
    if (!placeable || !body) return false;
    return *reinterpret_cast<void**>(body) == placeable;
}

static void DropBody(void* body, bool car, bool pooled = false) {
    if (!body) return;
    // Desktop CVehicleSA::~CVehicleSA (CVehicleSA.cpp:400-438) and
    // CPlayerPedSA::~CPlayerPedSA (CPlayerPedSA.cpp:141-148) skip CWorld::Remove
    // and the game destructor once the interface vtable is CPlaceable: the game
    // has already destroyed it. Remove's first virtual call is vtable+0x20
    // (CWorld::Remove at 0x5073b4). Past CPlaceable's two slots that word is
    // MI_CRANE_HARNESS (0x8862d6), and the branch SIGBUSes.
    // Device: 2026-11-13 18:46:58, libGTASA base 0x6cfae14000, fault
    // 0x6cfb69a2d6 = base+0x8862d6, from DropCarLocked.
    if (IsPlaceableVtable(body)) {
        ENGINE_LOGI("DIAG drop skipped: body %p vtable is CPlaceable", body);
        return;
    }
    TaskFns& api = Tasks();
    if (!car) {
        // A remote ped body is a pool CPlayerPed (CreateRemotePlayerPed). It must be destroyed, not only removed
        // from the world, or its pool slot and tasks leak (PlayerPedPort.cpp DestroyRemotePlayerPed).
        DestroyRemotePlayerPed(body);
        return;
    }
    if (api.worldRemove) api.worldRemove(body);
    if (pooled) {
        // Pool-owned vehicle (CreateCarForScript): call the deleting destructor (Itanium ABI vtable slot 1)
        // so the pool slot is released, like the desktop's delete.
        auto** vtable = *reinterpret_cast<void***>(body);
        reinterpret_cast<void (*)(void*)>(vtable[1])(body);
    } else if (api.carDtor) {
        api.carDtor(body);
    }
}

static bool EnsureWorldModel(int model) {
    return EnsureStreamedModel(model, 611);
}

static int g_gameFrame = 0;

static void NoteGameFrame() {
    static unsigned* counter = nullptr;
    static bool looked = false;
    if (!looked) {
        looked = true;
        void* handle = dlopen("libGTASA.so", RTLD_NOLOAD | RTLD_GLOBAL);
        if (handle)
            counter = static_cast<unsigned*>(dlsym(handle, "_ZN6CTimer14m_FrameCounterE"));
    }
    if (counter) g_gameFrame = static_cast<int>(*counter);
    else ++g_gameFrame;
}

static bool PedModelSettled(int model) {
    static std::unordered_map<int, int> readyAt;
    if (StreamingLoadState(model) != 1) {
        readyAt.erase(model);
        EnsurePedModel(model);
        return false;
    }
    auto it = readyAt.find(model);
    if (it == readyAt.end() || g_gameFrame < it->second) {
        if (g_loadAll) g_loadAll(true);
        readyAt[model] = g_gameFrame;
        return false;
    }
    return g_gameFrame - it->second >= 3;
}
struct GroundApi {
    bool looked = false;
    void (*setState)(void*, int) = nullptr;
    float (*findGround)(float, float, float, bool*, void**) = nullptr;
    void (*standCtor)(void*, int, bool, bool, float) = nullptr;
};

static GroundApi& Ground() {
    static GroundApi api;
    if (api.looked) return api;
    api.looked = true;
    void* handle = dlopen("libGTASA.so", RTLD_NOLOAD | RTLD_GLOBAL);
    if (!handle) return api;
    api.setState = reinterpret_cast<void (*)(void*, int)>(
        dlsym(handle, "_ZN4CPed11SetPedStateE9ePedState"));
    api.findGround = reinterpret_cast<float (*)(float, float, float, bool*, void**)>(
        dlsym(handle, "_ZN6CWorld21FindGroundZFor3DCoordEfffPbPP7CEntity"));
    api.standCtor = reinterpret_cast<void (*)(void*, int, bool, bool, float)>(
        dlsym(handle, "_ZN21CTaskSimpleStandStillC1Eibbf"));
    return api;
}

// CClientPed::ArmRemoteStreamInTransformFence (CClientPed.cpp 5914-5945) and
// ClearRemoteStreamInTransformFence release (5990-5996). Minimum 100 ms, timeout
// 1500 ms (CClientPed.cpp 62-63). The entity flag bit is bIsStaticWaitingForCollision
// (CEntitySA.h 131). CEntity::SetIsStatic (libGTASA 0x33ad14) writes bit 2 of the
// flag word at +0x28, so bit 18 of that word is the same field.
struct ColStoreApi {
    bool looked = false;
    void (*request)(const Vec3*, int) = nullptr;
    bool (*loaded)(const Vec3*, int) = nullptr;
};

static ColStoreApi& ColStore() {
    static ColStoreApi api;
    if (api.looked) return api;
    api.looked = true;
    void* handle = dlopen("libGTASA.so", RTLD_NOLOAD | RTLD_GLOBAL);
    if (!handle) return api;
    api.request = reinterpret_cast<void (*)(const Vec3*, int)>(
        dlsym(handle, "_ZN9CColStore16RequestCollisionERK7CVectori"));
    api.loaded = reinterpret_cast<bool (*)(const Vec3*, int)>(
        dlsym(handle, "_ZN9CColStore18HasCollisionLoadedERK7CVectori"));
    return api;
}

static void SetStaticWaitingForCollision(void* body, bool waiting) {
    if (!body) return;
    auto* flags = reinterpret_cast<uint64_t*>(static_cast<unsigned char*>(body) + 0x28);
    if (waiting) *flags |= (1ull << 18);
    else *flags &= ~(1ull << 18);
}

// CEntitySA::IsStaticWaitingForCollision (CEntitySA.h 304). Same bit as the store above.
// CPhysical::AddToMovingList (0x4e0bd4) loads it as ldrb [x0, #0x2a], tbnz #2.
static bool IsStaticWaitingForCollision(void* body) {
    if (!body) return false;
    return (*reinterpret_cast<uint64_t*>(static_cast<unsigned char*>(body) + 0x28) & (1ull << 18)) != 0;
}

// CEntitySA::IsStatic (CEntitySA.h 289, bIsStatic is bit 2 of the flag word).
// CEntity::SetIsStatic (0x33ad14) stores it: ldr x8, [x0, #0x28]; bfi x8, x9, #2, #1; str x8, [x0, #0x28].
static bool IsEntityStatic(void* body) {
    if (!body) return false;
    return (*reinterpret_cast<uint64_t*>(static_cast<unsigned char*>(body) + 0x28) & (1ull << 2)) != 0;
}

// The moving-list node of a CPhysical. CPhysical::AddToMovingList (0x4e0bcc) loads it with
// ldr x8, [x19, #0xe0] and returns when it is set; it stores the new node there at 0x4e0be8.
static bool OnMovingList(void* body) {
    if (!body) return false;
    return *reinterpret_cast<void**>(static_cast<unsigned char*>(body) + 0xE0) != nullptr;
}

// CPhysicalSA::AddToMovingList (CPhysicalSA.cpp 244). libGTASA 2.10 0x4e0bbc returns early when the
// node is already set or the ped is still static-waiting, so a second call is a no-op.
static void AddToMovingList(void* body) {
    using Fn = void (*)(void*);
    static Fn fn = nullptr;
    static bool looked = false;
    if (!looked) {
        looked = true;
        if (void* handle = dlopen("libGTASA.so", RTLD_NOLOAD | RTLD_GLOBAL))
            fn = reinterpret_cast<Fn>(dlsym(handle, "_ZN9CPhysical15AddToMovingListEv"));
        if (!fn) ENGINE_LOGI("DIAG CPhysical::AddToMovingList missing; fenced peds stay off the moving list");
    }
    if (fn && body) fn(body);
}

// CPhysicalSA::SetFrozen (CPhysicalSA.cpp 389-396).
// libGTASA 2.10 stores the physical flag word at CPhysical+0x64
// (CPhysical::CPhysical writes it at 0x4e04d8). CPhysical::ApplyMoveSpeed (0x4e1104)
// skips integration when bit 5 or bit 13 is set (mask 0x2020). Bit 13 is
// bDontApplySpeed and bit 2 is bDisableFriction (CPhysicalSA.h 42-55).
// Mass is the float at +0xB0: ApplyMoveForce (0x4e0df0) divides by it, and the
// constructor stores q0 there (0x4e04a0). PHYSICAL_MAXMASS is 99999.
static void SetPhysicalFrozen(void* body, bool frozen) {
    if (!body) return;
    auto* raw = static_cast<unsigned char*>(body);
    auto* flags = reinterpret_cast<uint32_t*>(raw + 0x64);
    const float mass = *reinterpret_cast<float*>(raw + 0xB0);
    constexpr uint32_t kDontApplySpeed = 1u << 13;
    constexpr uint32_t kDisableFriction = 1u << 2;
    constexpr float kMaxMass = 99999.f;
    if (frozen) *flags |= kDontApplySpeed;
    else *flags &= ~kDontApplySpeed;
    if (frozen || mass >= kMaxMass) *flags |= kDisableFriction;
    else *flags &= ~kDisableFriction;
}

// CClientPed::ApplyPhysicalFreezeState (CClientPed.cpp 7133-7138): the element freeze, the stream-in fence and the
// observer replica fence share bDontApplySpeed, so the bit is always the union. The ambient-owner and
// collision-authority fences belong to native traffic and are not ported.
static void ApplyPedFreezeState(void* body, const RemoteBody& stored, const RemotePed& ped) {
    SetPhysicalFrozen(body, ped.frozen || stored.collisionFence || stored.replicaFence);
}

// CClientPed::UpdateRemoteReplicaPhysicsFence (CClientPed.cpp 7140-7152). An observed server ped (not a player,
// not owned here, not frozen, not in a vehicle) has GTA's integrator off; interpolation alone moves it.
// Not tracked yet, so not in the condition: attached, entering or leaving a vehicle, synced position anim,
// physical native-task presentation (needs bitstream 0x3D).
static void UpdateRemoteReplicaPhysicsFence(void* body, RemoteBody& stored, const RemotePed& ped, bool seated) {
    const bool shouldFence = body && ped.serverPed && !ped.hosted && !ped.frozen && !seated;
    if (shouldFence == stored.replicaFence) return;
    stored.replicaFence = shouldFence;
    ApplyPedFreezeState(body, stored, ped);
    static int noted = 0;
    if (noted < 8) {
        ++noted;
        ENGINE_LOGI("DIAG ped replica physics fence %s", shouldFence ? "fenced" : "released");
    }
}

// CClientPed::SetInterior (CClientPed.cpp 1101-1119) writes CEntity::SetAreaCode.
// CEntryExitManager::SetAreaCodeForVisibleObjects reads and writes the byte at
// +0x4B (0x3cc420, 0x3cc43c). CTaskSimpleWaitUntilAreaCodesMatch::ProcessPed
// compares the same byte (0x5f938c).
static void ApplyPedInterior(void* body, RemoteBody& stored, uint8_t interior) {
    if (!body) return;
    const bool changed = !stored.interiorWritten || stored.interior != interior;
    static_cast<unsigned char*>(body)[0x4B] = interior;
    if (!changed) return;
    const unsigned previous = stored.interiorWritten ? stored.interior : 0u;
    stored.interiorWritten = true;
    stored.interior = interior;
    if (previous == interior) return;
    static int noted = 0;
    if (noted < 4) {
        ++noted;
        // CClientEntity::SetInterior also fires onClientElementInteriorChange
        // (CClientEntity.cpp 1332-1338). The local-player branch calls
        // CWorld::SetCurrentArea (CClientPed.cpp 1109-1116) through CGame::currArea.
        ENGINE_LOGI("DIAG CClientPed::SetInterior wrote area %u (was %u) at CEntity+0x4B. "
                    "onClientElementInteriorChange is not fired (CClientEntity::SetInterior, CClientEntity.cpp 1332-1338). "
                    "CWorld::SetCurrentArea is the local-player branch (CClientPed.cpp 1109-1116).",
                    static_cast<unsigned>(interior), previous);
    }
}

static int64_t GameNowMs() {
    return static_cast<int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

// Holds the ped at the network pose until CColStore::HasCollisionLoaded, or 1500 ms.
// Returns true while the fence is still holding.
static bool HoldPedCollisionFence(void* body, RemoteBody& stored, RemotePed& ped) {
    if (!stored.collisionFence) return false;
    constexpr int64_t kMinMs = 100;
    constexpr int64_t kTimeoutMs = 1500;
    const int64_t elapsed = GameNowMs() - stored.fenceAtMs;
    Vec3 pos{ped.x, ped.y, ped.z};
    const bool loaded = ColStore().loaded && ColStore().loaded(&pos, ped.interior);
    if (elapsed >= kTimeoutMs || (elapsed >= kMinMs && loaded)) {
        // ClearRemoteStreamInTransformFence (CClientPed.cpp 6038): restore the flag saved at arm.
        SetStaticWaitingForCollision(body, stored.fencePrevStaticWaiting);
        // ClearRemoteStreamInTransformFence calls ApplyPhysicalFreezeState while the fence flag is still set
        // (CClientPed.cpp 6038-6048), and m_remoteStreamInFenceActive is cleared right after (6049). The union is
        // recomputed here once the flag is off; the replica fence (UpdateRemoteReplicaPhysicsFence) then owns the bit.
        stored.collisionFence = false;
        ApplyPedFreezeState(body, stored, ped);
        // CClientPed.cpp 6040-6046: "GTA's stock mission collision gate puts a physical back on the moving list
        // after clearing bStaticWaitingForCollision ... a required resume when construction left it out of the list."
        // Without this the body is never processed after the hold (ROADMAP_PEDS_SYNC suspect 1).
        const bool listedBefore = OnMovingList(body);
        if (!stored.fencePrevStaticWaiting && !IsEntityStatic(body) && !WorldRPG::Debug::Flag("nomovelist"))
            AddToMovingList(body);
        static int released = 0;
        if (released < 8) {
            ++released;
            ENGINE_LOGI("DIAG ped collision fence released elapsed=%lld loaded=%d interior=%u static=%d movingList=%d->%d",
                static_cast<long long>(elapsed), loaded ? 1 : 0, static_cast<unsigned>(ped.interior),
                IsEntityStatic(body) ? 1 : 0, listedBefore ? 1 : 0, OnMovingList(body) ? 1 : 0);
        }
        return false;
    }
    PlaceRemotePlayerPed(body, ped.x, ped.y, ped.z, ped.heading);
    return true;
}

static void ArmPedCollisionFence(void* body, const RemotePed& ped, RemoteBody& stored) {
    // ArmRemoteStreamInTransformFence returns unless GetType() == CCLIENTPED, and for the syncer and a frozen ped
    // (CClientPed.cpp 5917). A remote player is never fenced on desktop.
    // The other early-outs (in a vehicle, attached, synced position anim) are not tracked here yet.
    if (!ped.serverPed || ped.hosted || ped.frozen) return;
    stored.collisionFence = true;
    stored.fenceAtMs = GameNowMs();
    stored.fencePrevStaticWaiting = IsStaticWaitingForCollision(body);
    SetStaticWaitingForCollision(body, true);
    // ArmRemoteStreamInTransformFence (CClientPed.cpp 5937-5938): static-waiting, then SetFrozen(true).
    ApplyPedFreezeState(body, stored, ped);
    auto& cols = ColStore();
    if (!cols.request || !cols.loaded) {
        static bool missing = false;
        if (!missing) {
            missing = true;
            ENGINE_LOGI("DIAG ped collision fence: CColStore::RequestCollision=%p HasCollisionLoaded=%p",
                reinterpret_cast<void*>(cols.request), reinterpret_cast<void*>(cols.loaded));
        }
        return;
    }
    const Vec3 pos{ped.x, ped.y, ped.z};
    cols.request(&pos, ped.interior);
}

// Lua runs on mta-net. The game thread services GTA world and camera queries
// from WorldRPG_NoStoryScript, where the collision and camera state are valid.
enum class WorldQuery { GroundZ, SphereVisible };

static bool RunWorldQuery(WorldQuery kind, const Vec3& position, float radius, float& result) {
    if (kind == WorldQuery::GroundZ) {
        auto findGround = Ground().findGround;
        if (!findGround) return false;
        // CWorldSA.cpp:708-729. Both pointers are optional in libGTASA 2.10 (0x50f460, 0x50f46c).
        result = findGround(position.x, position.y, position.z, nullptr, nullptr);
        return true;
    }
    // CGameSA.cpp:2272-2279. The ARM64 member takes a CVector const& and the radius.
    struct CameraApi {
        void* camera = nullptr;
        bool (*sphereVisible)(void*, const Vec3&, float) = nullptr;
    };
    static const auto camera = [] {
        CameraApi api;
        if (void* handle = dlopen("libGTASA.so", RTLD_NOLOAD | RTLD_GLOBAL)) {
            api.camera = dlsym(handle, "TheCamera");
            api.sphereVisible = reinterpret_cast<decltype(api.sphereVisible)>(
                dlsym(handle, "_ZN7CCamera15IsSphereVisibleERK7CVectorf"));
        }
        return api;
    }();
    if (!camera.camera || !camera.sphereVisible) return false;
    result = camera.sphereVisible(camera.camera, position, radius) ? 1.f : 0.f;
    return true;
}

static bool QueryGameWorld(WorldQuery kind, const Vec3& position, float radius, float& out) {
    bool ok = false;
    return GameTasks::RunQuery([&] { ok = RunWorldQuery(kind, position, radius, out); }) && ok;
}

bool FindGroundZFor3DPosition(float x, float y, float z, float& out) {
    if (QueryGameWorld(WorldQuery::GroundZ, {x, y, z}, 0, out)) return true;
    static std::atomic_bool reported{false};
    if (!reported.exchange(true)) ENGINE_LOGE("DIAG getGroundPosition: game-thread collision query unavailable");
    return false;
}

bool IsAmbientSphereVisible(float x, float y, float z, float radius, bool& visible) {
    float result = 0;
    if (!QueryGameWorld(WorldQuery::SphereVisible, {x, y, z}, radius, result)) return false;
    visible = result != 0;
    return true;
}

// VEHICLE_INOUT seat requests for other players (NoteRemoteSeat / NoteRemoteUnseat). The network thread writes them under
// g_remoteMutex. ApplyRemoteSeats runs on the game thread and removes a request once it is applied.
struct RemoteSeatRequest {
    uint32_t vehicleId = 0xFFFFFFFFu;
    uint8_t seat = 0;
    bool seated = false;
    uint32_t serial = 0;
};
static std::unordered_map<uint32_t, RemoteSeatRequest> g_seatRequests;
static uint32_t g_seatSerial = 0;
// Game thread only: the car each remote ped is seated in by ApplyRemoteSeat.
struct SeatedPed {
    uint32_t vehicleId = 0xFFFFFFFFu;
    uint8_t seat = 0;
};
static std::unordered_map<uint32_t, SeatedPed> g_seatedPeds;
// Game-thread-only reverse lookup for native damage callbacks, including those during a seat task.
static std::unordered_map<void*, uint32_t> g_vehicleBodyIds;

// Ends a seat this thread applied earlier. The caller holds g_remoteMutex. The car body must still exist; without it
// the record is only forgotten. Reads g_remoteBodies and g_remoteCars directly.
static void UnseatLocked(uint32_t pedId) {
    auto seated = g_seatedPeds.find(pedId);
    if (seated == g_seatedPeds.end()) return;
    const uint32_t vehicleId = seated->second.vehicleId;
    g_seatedPeds.erase(seated);
    auto b = g_remoteBodies.find(pedId);
    auto c = g_remoteCars.find(vehicleId);
    if (b == g_remoteBodies.end() || !b->second.ped || IsPlaceableVtable(b->second.ped)) return;
    if (c == g_remoteCars.end() || !c->second.body || IsPlaceableVtable(c->second.body)) return;
    SetNativeDriveby(b->second.ped, c->second.body, 0, false);
    ClearRemoteStick(b->second.ped);
    UnseatRemotePedFromCar(b->second.ped, c->second.body);
}

static void UnseatApplied(uint32_t pedId) {
    std::lock_guard<std::mutex> lock(g_remoteMutex);
    UnseatLocked(pedId);
}

// Caller holds g_remoteMutex. Called before a car body is dropped: every ped seated in it leaves the seat first.
static void UnseatPedsInCarLocked(uint32_t vehicleId) {
    std::vector<uint32_t> peds;
    for (const auto& [pedId, seated] : g_seatedPeds) {
        if (seated.vehicleId == vehicleId) peds.push_back(pedId);
    }
    for (uint32_t pedId : peds) UnseatLocked(pedId);
}

// Game thread. The local player's ped leaves `body` when it is inside it, with the same task the remote peds use
// (desktop InternalRemoveFromVehicle, see UnseatRemotePedFromCar). True when the ped is not in that vehicle
// afterwards, so the body may be destroyed. False when the ped could not be taken out.
static bool EjectLocalPedFrom(void* body) {
    TaskFns& api = Tasks();
    if (!body || !api.findPlayerVehicle) return true;
    if (api.findPlayerVehicle(-1, false) != body) return true;
    void* ped = g_findPlayer ? g_findPlayer(0) : nullptr;
    if (!ped || !UnseatRemotePedFromCar(ped, body)) return false;
    return api.findPlayerVehicle(-1, false) != body;
}

// Game thread, caller holds g_remoteMutex. Destroys the body of a server car that is going away (ENTITY_REMOVE or
// the reconnect reset). The desktop's vehicle destructor removes every occupant first (CClientVehicle::~CClientVehicle
// calls RemoveFromVehicle on each one); here the remote peds leave through UnseatPedsInCarLocked and the local ped
// through EjectLocalPedFrom. When the local ped cannot be taken out the body is left in the world instead of being
// destroyed under it: a destroyed vehicle with the player inside leaves the ped with a dangling vehicle pointer.
static void DropCarLocked(uint32_t id, RemoteCar& car) {
    if (!car.body) return;
    VehiclePaint::Forget(car.paintClump);
    car.paintClump = nullptr;
    car.appliedColors.reset();
    if (EjectLocalPedFrom(car.body)) {
        DropBody(car.body, true, !car.owned);
    } else {
        ENGINE_LOGE("DIAG vehicle %u body kept: the local ped is still inside (no seat-out task available)", id);
        (void)car.owned.release();  // the game still uses this memory
    }
    g_vehicleBodyIds.erase(car.body);
    car.body = nullptr;
}

// One request on the game thread. True when it is finished (applied, or nothing to do). False keeps it for the next tick.
static bool ApplyRemoteSeat(uint32_t pedId, const RemoteSeatRequest& req) {
    if (pedId == ServerWorld::LocalPlayer()) {
        void* ped = g_findPlayer ? g_findPlayer(0) : nullptr;
        auto& api = Tasks();
        if (!ped || !api.findPlayerVehicle) return false;
        void* old = api.findPlayerVehicle(-1, false);
        if (!req.seated) return !old || EjectLocalPedFrom(old);
        void* car = nullptr;
        int model = 0;
        {
            std::lock_guard lock(g_remoteMutex);
            auto it = g_remoteCars.find(req.vehicleId);
            if (it == g_remoteCars.end() || !it->second.body) return false;
            car = it->second.body;
            model = it->second.model;
        }
        if (old == car && NativeVehicleSeat(car, ped) == req.seat) return true;
        const uint8_t door = req.seat ? PassengerGameDoor(model, req.seat) : 0;
        if (door == 0xff) return true;
        if (old && !EjectLocalPedFrom(old)) return false;
        return req.seat ? SeatRemotePedAsPassenger(ped, car, door) : SeatRemotePedInCar(ped, car);
    }
    if (!req.seated) {
        UnseatApplied(pedId);
        return true;
    }
    auto seated = g_seatedPeds.find(pedId);
    if (seated != g_seatedPeds.end() && seated->second.vehicleId == req.vehicleId && seated->second.seat == req.seat) {
        return true;
    }
    void* body = nullptr;
    void* car = nullptr;
    int model = 0;
    {
        std::lock_guard<std::mutex> lock(g_remoteMutex);
        auto b = g_remoteBodies.find(pedId);
        if (b != g_remoteBodies.end()) body = b->second.ped;
        auto c = g_remoteCars.find(req.vehicleId);
        if (c != g_remoteCars.end()) {
            car = c->second.body;
            model = c->second.model;
        }
    }
    if (!body || !car || IsPlaceableVtable(body) || IsPlaceableVtable(car)) return false;
    // CPedRPCs::WarpPedIntoVehicle releases the on-foot collision fence first.
    auto stored = g_remoteBodies.find(pedId);
    RemoteBody& remoteBody = stored->second;
    if (remoteBody.collisionFence) {
        SetStaticWaitingForCollision(body, remoteBody.fencePrevStaticWaiting);
        remoteBody.collisionFence = false;
    }
    remoteBody.replicaFence = false;
    remoteBody.interp.finishMs = 0;
    remoteBody.rotationInterp = {};
    {
        std::lock_guard lock(g_remoteMutex);
        auto remote = g_remotePeds.find(pedId);
        if (remote != g_remotePeds.end()) ApplyPedFreezeState(body, remoteBody, remote->second);
    }
    // Desktop WarpIntoVehicle (CClientPed.cpp 3383) calls RemoveFromVehicle before it seats the ped.
    if (seated != g_seatedPeds.end()) UnseatApplied(pedId);
    // Driver seat 0 is the driver door. A passenger seat is mapped to its game door by the desktop table, and a seat
    // the model does not have is dropped. The ped stays out of the car in that case.
    unsigned char gameDoor = 0;
    if (req.seat != 0) {
        gameDoor = PassengerGameDoor(model, req.seat);
        if (gameDoor == 0xFF) {
            ENGINE_LOGI("VehInOut: ped %u seat %u has no seat on model %d", pedId, static_cast<unsigned>(req.seat), model);
            return true;
        }
    }
    const bool seatedOk = req.seat == 0 ? SeatRemotePedInCar(body, car)
                                        : SeatRemotePedAsPassenger(body, car, static_cast<int>(gameDoor));
    if (!seatedOk) return false;
    // The seated skip in ApplyRemotePeds returns before its ClearRemoteStick, so a stick set before the seat is cleared here.
    ClearRemoteStick(body);
    g_seatedPeds[pedId] = SeatedPed{req.vehicleId, req.seat};
    ENGINE_LOGI("VehInOut: ped %u seated in vehicle %u seat %u", pedId, req.vehicleId, static_cast<unsigned>(req.seat));
    return true;
}

static void ApplyRemoteSeats() {
    std::vector<std::pair<uint32_t, RemoteSeatRequest>> work;
    {
        std::lock_guard<std::mutex> lock(g_remoteMutex);
        for (const auto& [pedId, req] : g_seatRequests) work.emplace_back(pedId, req);
    }
    // A seat reassignment can enqueue an exit and an entry in the same tick.
    std::stable_partition(work.begin(), work.end(), [](const auto& entry) { return !entry.second.seated; });
    for (const auto& [pedId, req] : work) {
        const bool done = ApplyRemoteSeat(pedId, req);
        std::lock_guard<std::mutex> lock(g_remoteMutex);
        auto it = g_seatRequests.find(pedId);
        if (it == g_seatRequests.end() || it->second.serial != req.serial) continue;  // replaced while applying
        if (done) g_seatRequests.erase(it);
    }
}

// Writes a pending CarSeed into the body, once. Game thread. `localVehicle` is the car the local ped is in: its
// pose and speeds belong to the local driver (the desktop never applies remote state to the vehicle the local player
// drives), so only the health is taken for it.
static void ApplyCarSeed(uint32_t vehicleId, void* body, bool localVehicle) {
    CarSeed seed;
    {
        std::lock_guard<std::mutex> lock(g_remoteMutex);
        auto found = g_remoteCars.find(vehicleId);
        if (found == g_remoteCars.end() || !found->second.seed.pending) return;
        seed = found->second.seed;
        found->second.seed = CarSeed{};
        if (seed.hasRotation && !localVehicle) {
            found->second.unoccupiedRotation = {};
            found->second.shownH = seed.rotZ * (3.14159265f / 180.f);
        }
        if (seed.hasPose && !localVehicle) {
            found->second.unoccupiedPosition = {};
            found->second.shownX = seed.x;
            found->second.shownY = seed.y;
            found->second.shownZ = seed.z;
        }
    }
    if (!localVehicle) {
        if (seed.hasPose) {
            WriteBodyPos(body, seed.x, seed.y, seed.z);
        }
        if (seed.hasRotation) WriteBodyRotation(body, seed.rotX, seed.rotY, seed.rotZ);
        auto* raw = static_cast<unsigned char*>(body);
        if (seed.hasMoveSpeed) std::memcpy(raw + 0x68, seed.move, sizeof seed.move);
        if (seed.hasTurnSpeed) std::memcpy(raw + 0x74, seed.turn, sizeof seed.turn);
    }
    if (seed.hasHealth) WriteBodyHealth(body, seed.health);
    static int reported = 0;
    if (reported < 6) {
        ++reported;
        ENGINE_LOGI("Server vehicle %u seeded: pose %d rot %d (%.1f %.1f %.1f) motion %d health %d (%.1f)%s", vehicleId,
            seed.hasPose ? 1 : 0, seed.hasRotation ? 1 : 0, seed.rotX, seed.rotY, seed.rotZ, seed.hasMoveSpeed || seed.hasTurnSpeed,
            seed.hasHealth ? 1 : 0, seed.health, localVehicle ? " [local vehicle: health only]" : "");
    }
}

// CClientVehicle.cpp:3925-4157. At iUnoccupiedVehicle=400, the full
// error is applied over 400 ms, on top of the body's native movement.
constexpr float kUnoccupiedVehicleInterval = 400.f;

static float NextCarCompensation(CarInterpolation& state, int64_t now, float maxAlpha) {
    const float alpha = std::clamp((now - state.startMs) / kUnoccupiedVehicleInterval, 0.f, maxAlpha);
    const float step = alpha - state.lastAlpha;
    state.lastAlpha = alpha;
    if (alpha == maxAlpha) state.startMs = 0;
    return step;
}

static void ApplyCarObserverTargets(RemoteCar& car, void* body, HostedCar& pose, int64_t now) {
    auto& position = car.unoccupiedPosition;
    auto& rotation = car.unoccupiedRotation;
    if (position.startMs) {
        const float step = NextCarCompensation(position, now, 1.5f);
        const float error = std::hypot(pose.x - position.target.x, pose.y - position.target.y, pose.z - position.target.z);
        const float threshold = 15.f + 10.f * std::hypot(pose.vx, pose.vy, pose.vz);
        if (error <= threshold) {
            pose.x += position.error.x * step;
            pose.y += position.error.y * step;
            pose.z += position.error.z * step;
        } else {
            pose.x = position.target.x;
            pose.y = position.target.y;
            pose.z = position.target.z;
            position.startMs = 0;
            if (rotation.startMs) {
                WriteBodyRotation(body, rotation.target.x, rotation.target.y, rotation.target.z);
                pose.rotX = rotation.target.x;
                pose.rotY = rotation.target.y;
                pose.rotZ = rotation.target.z;
                rotation.startMs = 0;
            }
        }
        WriteBodyPos(body, pose.x, pose.y, pose.z);
    }
    if (rotation.startMs) {
        const float step = NextCarCompensation(rotation, now, 1.f);
        pose.rotX += rotation.error.x * step;
        pose.rotY += rotation.error.y * step;
        pose.rotZ += rotation.error.z * step;
        WriteBodyRotation(body, pose.rotX, pose.rotY, pose.rotZ);
    }
}

static void ApplyUnoccupiedVehicleSync(uint32_t id, int model, RemoteCar& car, void* body) {
    auto* raw = static_cast<unsigned char*>(body);
    const auto now = GameNowMs();
    HostedCar pose;
    if (!SampleHostedCar(body, pose)) return;
    std::optional<VehicleSync::UnoccupiedUpdate> update;
    {
        std::lock_guard lock(g_remoteMutex);
        update = std::exchange(car.observedVehicle, std::nullopt);
    }
    ApplyCarObserverTargets(car, body, pose, now);
    if (update) {
        const uint8_t context = ServerWorld::SyncTimeContext(id);
        if (context && update->timeContext && context != update->timeContext) update.reset();
    }
    if (update) {
        const auto& u = *update;
        if (u.syncPosition) {
            car.unoccupiedPosition = {u.position, {u.position.x - pose.x, u.position.y - pose.y, u.position.z - pose.z}, now};
        }
        if (u.syncRotation) {
            car.unoccupiedRotation = {u.rotation, {
                std::remainder(u.rotation.x - pose.rotX, 360.f), std::remainder(u.rotation.y - pose.rotY, 360.f),
                std::remainder(u.rotation.z - pose.rotZ, 360.f)}, now};
        }
        if (u.syncVelocity) {
            const float velocity[] = {u.velocity.x, u.velocity.y, u.velocity.z};
            std::memcpy(raw + 0x68, velocity, sizeof velocity);
        }
        if (u.syncTurnVelocity) {
            const float turnSpeed[] = {u.turnVelocity.x, u.turnVelocity.y, u.turnVelocity.z};
            std::memcpy(raw + 0x74, turnSpeed, sizeof turnSpeed);
        }
        if (u.syncHealth) WriteBodyHealth(body, u.health);
        // CTaskSimpleCarSetPedInAsDriver::ProcessPed writes this bit at 0x611488-0x6114a4.
        raw[0x568] = (raw[0x568] & ~0x10) | (u.engineOn ? 0x10 : 0);
        if (Entity::VehicleModels::IsTrainModel(static_cast<uint32_t>(model))) {
            static bool reported = false;
            if (!reported) {
                reported = true;
                ENGINE_LOGI("DIAG CClientVehicle::SetDerailed: train engine hook is unavailable");
            }
        }
    }
    std::lock_guard lock(g_remoteMutex);
    car.shownX = pose.x;
    car.shownY = pose.y;
    car.shownZ = pose.z;
    car.shownH = pose.rotZ * (3.14159265f / 180.f);
}

// Packet 33 driver block: desktop CClientVehicle::SetMoveSpeed and SetTurnSpeed, called from CNetAPI::ReadVehiclePuresync
// (CNetAPI.cpp 1420-1421). CPhysical::SetMoveSpeed is not exported, so the CPhysical fields are written at +0x68 (move)
// and +0x74 (turn), the offsets PlaceRemotePlayerPed and SampleHostedCar already use.
static void ApplyCarMotion(uint32_t vehicleId, void* body) {
    float move[3] = {0, 0, 0};
    float turn[3] = {0, 0, 0};
    {
        std::lock_guard<std::mutex> lock(g_remoteMutex);
        auto found = g_remoteCars.find(vehicleId);
        if (found == g_remoteCars.end() || !found->second.motionPending) return;
        found->second.motionPending = false;
        move[0] = found->second.moveX;
        move[1] = found->second.moveY;
        move[2] = found->second.moveZ;
        turn[0] = found->second.turnX;
        turn[1] = found->second.turnY;
        turn[2] = found->second.turnZ;
    }
    auto* raw = static_cast<unsigned char*>(body);
    std::memcpy(raw + 0x68, move, sizeof move);
    std::memcpy(raw + 0x74, turn, sizeof turn);
}

static void ApplyVehiclePresentation(uint32_t id, RemoteCar& car, void* body) {
    // CVehicle::SetupRender (0x6a5f1c) obtains this instance's RpClump from CEntity+0x20.
    void* clump = *reinterpret_cast<void**>(static_cast<uint8_t*>(body) + 0x20);
    if (clump != car.paintClump) {
        VehiclePaint::Forget(car.paintClump);
        car.paintClump = clump;
        car.appliedColors.reset();
    }
    if (clump && car.colors && car.appliedColors != car.colors->rgb) {
        VehiclePaint::Track(clump, id, *car.colors);
        car.appliedColors = car.colors->rgb;
    }
    std::optional<bool> frozen, engine, loadCollision;
    std::optional<uint8_t> alpha;
    {
        std::lock_guard lock(g_remoteMutex);
        frozen = car.frozen;
        engine = car.engineOn;
        loadCollision = car.loadCollision;
        alpha = car.alpha;
    }
    auto* raw = static_cast<unsigned char*>(body);
    if (frozen && car.appliedFrozen != *frozen) {
        car.appliedFrozen = *frozen;
        SetPhysicalFrozen(body, *frozen);
        if (*frozen) {
            const float zero[3] = {};
            std::memcpy(raw + 0x68, zero, sizeof zero);
            std::memcpy(raw + 0x74, zero, sizeof zero);
        }
    }
    if (engine && car.appliedEngineOn != *engine) {
        car.appliedEngineOn = *engine;
        // Driver ProcessPed at 0x611488-0x6114a4, audio reader at 0x48e85c-0x48e860.
        raw[0x568] = (raw[0x568] & ~0x10) | (*engine ? 0x10 : 0);
    }
    if (loadCollision && car.appliedLoadCollision != *loadCollision) {
        car.appliedLoadCollision = *loadCollision;
        // CRunningScript::ProcessCommands1400To1499 writes CPhysical flags 0x4000 at 0x420acc-0x420ad0.
        auto& flags = *reinterpret_cast<uint32_t*>(raw + 0x64);
        flags = (flags & ~0x4000u) | (*loadCollision ? 0 : 0x4000u);
    }
    if (alpha && car.appliedAlpha != *alpha) {
        using SetAlpha = void (*)(void*, int);
        static auto setAlpha = [] {
            void* handle = dlopen("libGTASA.so", RTLD_NOLOAD | RTLD_GLOBAL);
            return handle ? reinterpret_cast<SetAlpha>(dlsym(handle, "_ZN18CVisibilityPlugins13SetClumpAlphaEP7RpClumpi")) : nullptr;
        }();
        auto* clump = *reinterpret_cast<void**>(raw + 0x20);
        if (clump && setAlpha) {
            setAlpha(clump, *alpha);
            auto& flags = *reinterpret_cast<uint32_t*>(raw + 0x28);
            flags = (flags & ~(1u << 7)) | (*alpha ? (1u << 7) : 0);
            car.appliedAlpha = *alpha;
        }
    }
}

static void ApplyCarDoorLock(RemoteCar& car) {
    if (car.doorLockMode == car.appliedDoorLockMode) return;
    // CVehicle::CanPedOpenLocks loads CVehicle+0x684 (0x6a8c78).
    // CCarGenerator::DoInternalProcessing sets DOOR_LOCK_LOCKED there (0x68fc20-0x68fc24).
    auto* mode = reinterpret_cast<int32_t*>(static_cast<unsigned char*>(car.body) + 0x684);
    *mode = car.doorLockMode;
    car.appliedDoorLockMode = car.doorLockMode;
}

static bool HasInstalledNitro(const void* vehicle) {
    // CAutomobile::ProcessControl (0x674c3c) tests this flag before calling NitrousControl.
    return (static_cast<const uint8_t*>(vehicle)[0x4ba] & 0x08) != 0;
}

static void ApplyCarNitro(uint32_t id, RemoteCar& car, const TaskFns& api) {
    if (car.nitroUpgrade == car.appliedNitroUpgrade && car.nitroRevision == car.appliedNitroRevision) return;
    if (IsPlaceableVtable(car.body)) return;
    // CVehicle::SetVehicleUpgradeFlags (0x6b0238) only installs nitro on native automobiles.
    const auto* raw = static_cast<const uint8_t*>(car.body);
    if (*reinterpret_cast<const int32_t*>(raw + 0x734) != 0) return;
    if (!api.requestVehicleUpgrade || !api.hasVehicleUpgradeLoaded || !api.addVehicleUpgrade || !api.removeVehicleUpgrade) {
        static bool loggedMissing = false;
        if (!loggedMissing) {
            loggedMissing = true;
            ENGINE_LOGI("DIAG vehicle nitro: native upgrade functions unavailable");
        }
        return;
    }
    if (car.nitroUpgrade && !api.hasVehicleUpgradeLoaded(car.nitroUpgrade)) {
        api.requestVehicleUpgrade(car.nitroUpgrade, 0);
        return;
    }
    if (car.appliedNitroUpgrade && car.appliedNitroUpgrade != car.nitroUpgrade) {
        api.removeVehicleUpgrade(car.body, car.appliedNitroUpgrade);
    }
    if (car.nitroUpgrade) api.addVehicleUpgrade(car.body, car.nitroUpgrade);
    car.appliedNitroUpgrade = car.nitroUpgrade;
    car.appliedNitroRevision = car.nitroRevision;
    ENGINE_LOGI("Vehicle nitro id=%u upgrade=%u active=%d", id, unsigned(car.nitroUpgrade), HasInstalledNitro(car.body));
}

static void RefreshNitroButton(void* vehicle, const TaskFns& api) {
    if (!vehicle || !HasInstalledNitro(vehicle) || !g_findPlayer || !api.isVehicleDriver ||
        !api.getPad || !api.padNitroFired) return;
    void* ped = g_findPlayer(0);
    if (!ped || !api.isVehicleDriver(vehicle, ped)) return;
    // CPad::GetNitroFired enables touch widget 0x1e at 0x4dd460. Refresh it
    // during recharge too. GTA's NitrousControl still handles activation.
    if (void* pad = api.getPad(0)) api.padNitroFired(pad);
}

// Called on the game thread with g_remoteMutex held, after the driving pad enabled its widgets.
static void SampleVehicleTouchControlBounds(bool inVehicle) {
    g_vehicleTouchBounds.clear();
    if (!inVehicle) return;
    static const auto native = [] {
        struct {
            const unsigned char* screen = nullptr;
            const unsigned char* const* widgets = nullptr;
            std::array<void*, 4> buttonDrawMethods{};
            void* steerDraw = nullptr;
            bool (*isTouchDown)(int) = nullptr;
        } result;
        void* library = dlopen("libGTASA.so", RTLD_NOLOAD | RTLD_GLOBAL);
        if (!library) return result;
        result.screen = static_cast<const unsigned char*>(dlsym(library, "RsGlobal"));
        result.widgets = static_cast<const unsigned char* const*>(
            dlsym(library, "_ZN15CTouchInterface10m_pWidgetsE"));
        const char* names[] = {
            "_ZN13CWidgetButton4DrawEv", "_ZN21CWidgetButtonEnterCar4DrawEv",
            "_ZN21CWidgetButtonAnimated4DrawEv", "_ZN23CWidgetButtonHydraulics4DrawEv",
        };
        for (size_t i = 0; i < result.buttonDrawMethods.size(); ++i)
            result.buttonDrawMethods[i] = dlsym(library, names[i]);
        result.steerDraw = dlsym(library, "_ZN18CWidgetRegionSteer4DrawEv");
        result.isTouchDown = reinterpret_cast<decltype(result.isTouchDown)>(
            dlsym(library, "_ZN15CTouchInterface11IsTouchDownEi"));
        return result;
    }();
    if (!native.screen || !native.widgets) return;

    // CWidget::SetScreenRect (0x373134, 0x373174) reads the render width and height from RsGlobal.
    int32_t screen[2];
    std::memcpy(screen, native.screen + 0x8, sizeof(screen));
    if (screen[0] <= 0 || screen[1] <= 0) return;
    // DrawAll (0x36fbb0, 0x36fbc8) uses vtable slot 5 and a 0x5f0-byte widget array.
    constexpr size_t widgetCount = 190;
    for (size_t i = 0; i < widgetCount; ++i) {
        const auto* widget = native.widgets[i];
        // CWidgetButton::Draw (0x3736b0) tests draw alpha. GetEnabled (0x3725c8) reads +0x59.
        if (!widget || (widget[0x58] == 0 && widget[0x59] == 0)) continue;
        auto* vtable = *reinterpret_cast<void* const* const*>(widget);
        void* draw = vtable[5];
        float rect[4];
        const auto& buttonMethods = native.buttonDrawMethods;
        if (draw && std::find(buttonMethods.begin(), buttonMethods.end(), draw) != buttonMethods.end()) {
            // Draw passes CRect at +0x2c to CSprite2d (0x3736c8). Its order is left, bottom, right, top.
            std::memcpy(rect, widget + 0x2c, sizeof(rect));
        } else if (draw && draw == native.steerDraw && native.isTouchDown && widget[0x58] != 0) {
            // The steering region spans more of the screen than the circle it draws. Use the actual
            // center and radius from CWidgetRegionSteer::Draw (0x383cc8-0x383d18) while touched.
            int32_t touch;
            std::memcpy(&touch, widget + 0x84, sizeof(touch));  // IsTouched (0x383e9c).
            // LIB_PointerGetButton (0x327040) has two pointer slots.
            if (touch < 0 || touch > 1 || !native.isTouchDown(touch)) continue;
            float centers[4], width, radiusScale;
            std::memcpy(centers, widget + 0xb0, sizeof(centers));
            std::memcpy(&width, widget + 0x20, sizeof(width));
            std::memcpy(&radiusScale, widget + 0xa4, sizeof(radiusScale));
            const float scaledWidth = width * (screen[0] / 640.0f);
            const float radius = scaledWidth * radiusScale;
            if (radius <= 0) continue;
            // The moving knob uses 15 percent of that width (0x383d20-0x383d64).
            const float knob = scaledWidth * 0.15f;
            rect[0] = std::min(centers[2] - radius, centers[0] - knob);
            rect[1] = std::max(centers[3] + radius, centers[1] + knob);
            rect[2] = std::max(centers[2] + radius, centers[0] + knob);
            rect[3] = std::min(centers[3] - radius, centers[1] - knob);
        } else {
            continue;
        }
        VehicleTouchRect bounds{
            std::min(rect[0], rect[2]) / screen[0], std::min(rect[1], rect[3]) / screen[1],
            std::max(rect[0], rect[2]) / screen[0], std::max(rect[1], rect[3]) / screen[1],
        };
        if (std::isfinite(bounds.left) && std::isfinite(bounds.top) &&
            std::isfinite(bounds.right) && std::isfinite(bounds.bottom) &&
            bounds.right > bounds.left && bounds.bottom > bounds.top)
            g_vehicleTouchBounds.push_back(bounds);
    }
}

using EnterCarFirstTask = void* (*)(void*, void*);
static EnterCarFirstTask g_enterCarFirstTask = nullptr;
static void* (*g_createEnterCarSubTask)(void*, int, void*) = nullptr;
static bool (*g_canPedOpenLocks)(const void*, const void*) = nullptr;

static void* StartEnterCarTask(void* task, void* ped) {
    if (ped && !InRemoteControlContext() && g_findPlayer && ped == g_findPlayer(0)) {
        // CTaskComplexEnterCar::CreateFirstSubTask loads the target CVehicle at +0x18 (0x5fdfb8).
        void* vehicle = *reinterpret_cast<void**>(static_cast<unsigned char*>(task) + 0x18);
        uint32_t id = kNoVehicle;
        if (vehicle) {
            std::lock_guard lock(g_remoteMutex);
            for (const auto& [vehicleId, car] : g_remoteCars) {
                if (car.body == vehicle) {
                    id = vehicleId;
                    break;
                }
            }
        }
        if (id != kNoVehicle) {
            // The server's native-vehicle-traffic onVehicleStartEnter cancels
            // entry into active traffic. Apply the synced protection before GTA
            // starts a task, including vehicle types that skip door checks.
            double traffic = 0;
            std::string text;
            bool numeric = false;
            bool boolean = false;
            const bool protectedTraffic = ServerWorld::GetData(id, "neon:ambientVehicleTraffic",
                traffic, text, numeric, &boolean) && boolean && traffic != 0;
            if (protectedTraffic || !g_canPedOpenLocks(vehicle, ped)) {
                // Same finish path as CreateFirstSubTask's invalid-target branch (0x5fe0ec).
                ENGINE_LOGI("VehInOut: blocked entry into locked server vehicle %u", id);
                return g_createEnterCarSubTask(task, 1302, ped);
            }
        }
    }
    return g_enterCarFirstTask(task, ped);
}

static void InstallVehicleEntryGuard(void* library) {
    auto* target = dlsym(library, "_ZN20CTaskComplexEnterCar18CreateFirstSubTaskEP4CPed");
    g_createEnterCarSubTask = reinterpret_cast<decltype(g_createEnterCarSubTask)>(
        dlsym(library, "_ZN20CTaskComplexEnterCar13CreateSubTaskEiP4CPed"));
    g_canPedOpenLocks = reinterpret_cast<decltype(g_canPedOpenLocks)>(
        dlsym(library, "_ZNK8CVehicle15CanPedOpenLocksEPK4CPed"));
    constexpr std::array<uint32_t, 4> kPrologue{0xd10183ff, 0xf90013f7, 0xa90357f6, 0xa9044ff4};
    if (!target || !g_createEnterCarSubTask || !g_canPedOpenLocks ||
        std::memcmp(target, kPrologue.data(), sizeof(kPrologue)) != 0) {
        ENGINE_LOGE("DIAG vehicle entry guard: GTA symbols or entry prologue unavailable");
        return;
    }
    static Hook::MemoryPatcher patcher;
    const auto result = patcher.installHook(reinterpret_cast<uintptr_t>(target),
                                            reinterpret_cast<uintptr_t>(&StartEnterCarTask));
    if (result.success) {
        g_enterCarFirstTask = reinterpret_cast<EnterCarFirstTask>(result.trampolineAddress);
        ENGINE_LOGI("Vehicle entry guard installed");
    } else {
        ENGINE_LOGE("DIAG vehicle entry guard: %s", result.errorMessage.c_str());
    }
}

// Desktop CClientPed::EnterVehicle (CClientPed.cpp:9886, request written at 10061) and ExitVehicle
// (CClientPed.cpp:10096, request written at 10178) send REQUEST_IN / REQUEST_OUT only after the local ped
// is allowed to act. On Android the native enter button
// starts GTA's animation first, so the change is watched here instead. Each frame FindPlayerVehicle(-1, false)
// returns the CVehicle the local ped is in; it is mapped to a server ElementID through g_remoteCars. A change
// of mapped vehicle queues a leave for the old one and an enter for the new one. The network thread turns
// those events into REQUEST_IN / REQUEST_OUT (NeonJoinProtocol.cpp, SendVehicleInOut).
static void WatchLocalVehicle() {
    TaskFns& api = Tasks();
    if (!api.findPlayerVehicle) return;
    void* vehicle = api.findPlayerVehicle(-1, false);
    RefreshNitroButton(vehicle, api);
    std::lock_guard<std::mutex> lock(g_remoteMutex);
    uint32_t current = kNoVehicle;
    int model = 0;
    if (vehicle) {
        for (const auto& [id, car] : g_remoteCars) {
            if (car.body == vehicle) {
                current = id;
                model = car.model;
                break;
            }
        }
    }
    // Packet 33 source (CopyLocalCar). Sampled every frame while the ped is in a mapped standard car, before
    // the unchanged-vehicle return below, so the snapshot stays current. SampleHostedCar does not take the mutex.
    const int seat = vehicle ? NativeVehicleSeat(vehicle, g_sampledLocalPed) : -1;
    if (seat < 0) current = kNoVehicle;
    if (g_localVehicleId != kNoVehicle && current == kNoVehicle) g_localDrivebyRequest = 0;
    // A Lua drive-by toggle and its weapon selection can arrive on opposite
    // sides of a game frame. Start once the selected slot is available.
    const bool armed = g_localWeapon.valid && PlayerWeaponSync::SlotHasAmmo(g_localWeapon.slot);
    if (g_localDrivebyRequest == 0 || (g_localDrivebyRequest == 1 && seat >= 0 && armed)) {
        const bool enable = g_localDrivebyRequest != 0;
        SetNativeDriveby(g_sampledLocalPed, vehicle, seat >= 0 ? seat : 0, enable);
        if (HasNativeDriveby(g_sampledLocalPed) == enable) {
            g_localWeapon.gangDriveby = enable;
            g_localDrivebyRequest = -1;
        }
    }
    HostedCar sample;
    const bool sampled = current != kNoVehicle && model >= 400 && model <= 611 && SampleHostedCar(vehicle, sample);
    bool pressed = false;
    const bool driveby = sampled && g_localWeapon.gangDriveby;
    const bool held = SampleDrivebyButton(driveby ? g_sampledLocalPed : nullptr, pressed);
    const bool sameSeat = g_localCarValid && g_localCar.vehicleId == current && g_localCar.seat == seat;
    const uint64_t pendingPress = driveby && sameSeat ? g_localCar.firePress : 0;
    g_localCarValid = sampled;
    if (sampled) {
        LocalCarState& c = g_localCar;
        c = LocalCarState{};
        c.vehicleId = current;
        c.seat = static_cast<uint8_t>(seat);
        c.camera = g_cameraRotValid ? g_cameraRot.load() : -sample.rotZ * 3.14159265f / 180.f;
        c.model = model;
        c.x = sample.x;
        c.y = sample.y;
        c.z = sample.z;
        c.rotX = sample.rotX;
        c.rotY = sample.rotY;
        c.rotZ = sample.rotZ;
        c.vx = sample.vx;
        c.vy = sample.vy;
        c.vz = sample.vz;
        c.tx = sample.tx;
        c.ty = sample.ty;
        c.tz = sample.tz;
        c.health = sample.health;
        if (seat == 0) SamplePadControls(api, c);
        c.fire = held;
        c.firePress = pressed ? ++g_vehicleFireSequence : pendingPress;
    }
    SampleVehicleTouchControlBounds(vehicle != nullptr);
    if (current == g_localVehicleId && (current == kNoVehicle || seat == g_localSeat)) return;
    g_localAttackLatch = 0;
    if (g_localVehicleId != kNoVehicle) {
        g_localVehicleEvents.push_back(LocalVehicleEvent{g_localVehicleId, false, g_localSeat});
        ENGINE_LOGI("VehInOut: local ped left vehicle %u", g_localVehicleId);
    }
    if (current != kNoVehicle) {
        g_localVehicleEvents.push_back(LocalVehicleEvent{current, true, static_cast<uint8_t>(seat)});
        ENGINE_LOGI("VehInOut: local ped entered vehicle %u", current);
    }
    if (g_localVehicleEvents.size() > kMaxLocalVehicleEvents) {
        ENGINE_LOGI("VehInOut: event queue full, dropping oldest");
        g_localVehicleEvents.erase(g_localVehicleEvents.begin());
    }
    g_localVehicleId = current;
    g_localSeat = seat >= 0 ? static_cast<uint8_t>(seat) : 0;
}

// CNetAPI::ReadPlayerPuresync writes that arrived on the network thread.
// SetMoveSpeed is CPhysical +0x68 (the same field SampleLocalPose reads).
// SetHealth / SetArmor are CPed +0x6AC / +0x6B4. Each flag is consumed once.
static void ApplySyncedPlayerFields(uint32_t id, void* body) {
    if (!body) return;
    bool doSpeed = false, doHealth = false, doArmor = false;
    float vx = 0.f, vy = 0.f, vz = 0.f, health = 0.f, armor = 0.f;
    {
        std::lock_guard<std::mutex> lock(g_remoteMutex);
        auto found = g_remotePeds.find(id);
        if (found == g_remotePeds.end()) return;
        RemotePed& ped = found->second;
        if (ped.syncMoveSpeed) {
            doSpeed = true;
            vx = ped.vx;
            vy = ped.vy;
            vz = ped.vz;
            ped.syncMoveSpeed = false;
        }
        if (ped.syncHealth) {
            doHealth = true;
            health = ped.health;
            ped.syncHealth = false;
        }
        if (ped.syncArmor) {
            doArmor = true;
            armor = ped.armor;
            ped.syncArmor = false;
        }
    }
    auto* raw = static_cast<unsigned char*>(body);
    if (doSpeed) {
        auto* speed = reinterpret_cast<float*>(raw + 0x68);
        speed[0] = vx;
        speed[1] = vy;
        speed[2] = vz;
    }
    if (doHealth && std::isfinite(health)) *reinterpret_cast<float*>(raw + 0x6AC) = health;
    if (doArmor && std::isfinite(armor)) *reinterpret_cast<float*>(raw + 0x6B4) = armor;
    // CClientPed::StreamedInPulse (CClientPed.cpp 4738-4748) writes a locked health
    // through InternalSetHealth (3677-3718) and a locked armor through CPed::SetArmor.
    // Both game setters store the float (CPedSA.h 407 and 410). The dead-to-alive
    // branch of InternalSetHealth recreates the ped; that hook is logged below.
    bool healthLocked = false, armorLocked = false;
    float lockedHealth = 0.f, lockedArmor = 0.f;
    // Packet_PedStopSync: LockHealth(GetHealth()), LockArmor(GetArmor()) (CPedSync.cpp 280-281). The current value is
    // the body's. Loads only: +0x6AC as RenderHealthBar (0x37d770), +0x6B4 as RenderArmorBar (0x37d93c). Writing the
    // armor value back below stores the same bits that were loaded.
    ResolvePedVitalsLockCurrent(id, *reinterpret_cast<float*>(raw + 0x6AC), *reinterpret_cast<float*>(raw + 0x6B4));
    if (!CopyPedVitalsLock(id, healthLocked, lockedHealth, armorLocked, lockedArmor)) return;
    if (healthLocked && std::isfinite(lockedHealth)) {
        auto* current = reinterpret_cast<float*>(raw + 0x6AC);
        if (*current <= 0.f && lockedHealth > 0.f) {
            static int noted = 0;
            if (noted < 4) {
                ++noted;
                ENGINE_LOGI("DIAG CClientPed::InternalSetHealth dead-to-alive path is not run "
                            "(CClientPed.cpp 3682-3709: RemoveFromVehicle, Respawn, ReCreateModel, WarpIntoVehicle). "
                            "The health field is still written.");
            }
        }
        *current = lockedHealth;
    }
    if (armorLocked && std::isfinite(lockedArmor))
        *reinterpret_cast<float*>(raw + 0x6B4) = lockedArmor;
}

// CClientPed::UpdateTargetPosition (CClientPed.cpp 8765-8822), run every frame from Interpolate. The error captured by
// SetTargetPosition is added in the share due for the elapsed part of the interval, on top of whatever GTA physics did
// this frame. Past (5 + 5 * |speed|) * gameSpeed * TICK_RATE / 100 the ped warps to the target
// (PED_INTERPOLATION_WARP_THRESHOLD and _FOR_SPEED, CClientPed.cpp 57-58). Game speed is taken as 1.
// Contact-entity origins and UpdateUnderFloorFix are not ported.
constexpr int64_t kPureSyncTickMs = 100;  // TICK_RATE = g_TickRateSettings.iPureSync, default 100 (CTickRateSettings.h 16)
static void UpdateTargetPosition(void* body, RemoteBody& stored) {
    auto& in = stored.interp;
    if (!body || in.finishMs == 0) return;
    float cx = 0, cy = 0, cz = 0, ch = 0;
    if (!ReadBodyPos(body, cx, cy, cz, ch)) return;
    const int64_t now = GameNowMs();
    const float span = static_cast<float>(in.finishMs - in.startMs);
    float alpha = span > 0.f ? static_cast<float>(now - in.startMs) / span : 1.f;
    alpha = std::clamp(alpha, 0.f, 1.f);
    const float step = alpha - in.lastAlpha;
    in.lastAlpha = alpha;
    float nx = cx + in.errorX * step;
    float ny = cy + in.errorY * step;
    float nz = cz + in.errorZ * step;
    if (alpha == 1.f) in.finishMs = 0;
    const auto* speed = reinterpret_cast<const float*>(static_cast<unsigned char*>(body) + 0x68);
    const float v = std::sqrt(speed[0] * speed[0] + speed[1] * speed[1] + speed[2] * speed[2]);
    const float threshold = (5.f + 5.f * v) * static_cast<float>(kPureSyncTickMs) / 100.f;
    const float dx = cx - in.targetX, dy = cy - in.targetY, dz = cz - in.targetZ;
    const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (!(dist <= threshold)) {
        in.finishMs = 0;
        nx = in.targetX;
        ny = in.targetY;
        nz = in.targetZ;
    }
    // CClientPed::SetPosition(pos, false): CPed::SetPosition on the body (CClientPed.cpp 1064-1098).
    WriteBodyPos(body, nx, ny, nz);
}

// CClientPed::SetTargetPosition (CClientPed.cpp 8657-8709) for a streamed-in ped.
static void SetTargetPosition(void* body, RemoteBody& stored, float x, float y, float z, int64_t delayMs) {
    UpdateTargetPosition(body, stored);
    float cx = 0, cy = 0, cz = 0, ch = 0;
    if (!ReadBodyPos(body, cx, cy, cz, ch)) return;
    auto& in = stored.interp;
    in.targetX = x;
    in.targetY = y;
    in.targetZ = z;
    in.errorX = x - cx;
    in.errorY = y - cy;
    in.errorZ = z - cz;
    in.startMs = GameNowMs();
    in.finishMs = in.startMs + delayMs;
    in.lastAlpha = 0.f;
}

// CClientPed::Kill (CClientPed.cpp 3817-3887) for a remote body: when not already dead, CTasksSA::CreateTaskComplexDie
// (weapon, animGroup, animID, 4.0, 1.0) as TASK_PRIORITY_EVENT_RESPONSE_NONTEMP (2, CTaskManager.h 16-24), then
// LockHealth(0) and LockArmor(0). CTasksSA refuses an animation CAnimManagerSA::IsValidAnim rejects (CTasksSA.cpp
// 271-277); here: group below CAnimManager::ms_numAnimAssocDefinitions (GetAnimAssociation 0x466b34 indexes the group
// array without a check) and a non-null CAnimManager::GetAnimAssociation. Otherwise the default death (group 0, anim 0,
// ANIM_STD_KO_FRONT, the CTasks.h default) is used.
// Not run: TASK_COMPLEX_IN_WATER abort, jetpack removal, satchels, stealth kill, the wasted Lua event.
// Runs CTaskComplexDie on any valid CPed body (shared by remote observer kills and server-ordered local kills).
static void RunDieTaskOnBody(uint32_t id, void* body, int weapon, int group, int anim) {
    struct Fns {
        bool looked = false;
        void* (*taskNew)(unsigned long) = nullptr;
        void (*dieCtor)(void*, int, int, int, float, float, bool, bool, int, bool) = nullptr;
        void (*setTask)(void*, void*, int, bool) = nullptr;
        void* (*getAnim)(int, int) = nullptr;
        const int* numGroups = nullptr;
    };
    static Fns f;
    if (!f.looked) {
        f.looked = true;
        if (void* handle = dlopen("libGTASA.so", RTLD_NOLOAD | RTLD_GLOBAL)) {
            f.taskNew = reinterpret_cast<void* (*)(unsigned long)>(dlsym(handle, "_ZN5CTasknwEm"));
            f.dieCtor = reinterpret_cast<void (*)(void*, int, int, int, float, float, bool, bool, int, bool)>(
                dlsym(handle, "_ZN15CTaskComplexDieC1E11eWeaponType12AssocGroupId11AnimationIdffbbib"));
            f.setTask = reinterpret_cast<void (*)(void*, void*, int, bool)>(dlsym(handle, "_ZN12CTaskManager7SetTaskEP5CTaskib"));
            f.getAnim = reinterpret_cast<void* (*)(int, int)>(
                dlsym(handle, "_ZN12CAnimManager18GetAnimAssociationE12AssocGroupId11AnimationId"));
            f.numGroups = static_cast<const int*>(dlsym(handle, "_ZN12CAnimManager26ms_numAnimAssocDefinitionsE"));
        }
        if (!f.taskNew || !f.dieCtor || !f.setTask)
            ENGINE_LOGE("DIAG Kill: CTask::new=%p CTaskComplexDie=%p SetTask=%p", reinterpret_cast<void*>(f.taskNew),
                reinterpret_cast<void*>(f.dieCtor), reinterpret_cast<void*>(f.setTask));
    }
    if (body && f.taskNew && f.dieCtor && f.setTask) {
        const bool groupOk = f.numGroups && group >= 0 && group < *f.numGroups;
        const bool animOk = groupOk && anim >= 0 && anim < 391 && f.getAnim && f.getAnim(group, anim) != nullptr;
        if (!animOk) {
            group = 0;
            anim = 0;
        }
        void* intel = PedIntel(body);
        void* task = intel ? f.taskNew(0x80) : nullptr;  // CTask::operator new ignores the size (pool slot)
        if (task) {
            f.dieCtor(task, weapon, group, anim, 4.f, 1.f, false, false, 0, false);
            f.setTask(static_cast<unsigned char*>(intel) + 8, task, 2, false);
        } else {
            ENGINE_LOGI("DIAG Kill id=%u: no task slot or intelligence; health only", id);
        }
    }
}

static void KillRemoteBody(uint32_t id, void* body, int weapon, int group, int anim) {
    RunDieTaskOnBody(id, body, weapon, group, anim);
    LockPedHealth(id, 0.f);
    LockPedArmor(id, 0.f);
    static int reports = 0;
    if (reports < 6) {
        ++reports;
        ENGINE_LOGI("DIAG Kill id=%u weapon=%d anim=%d/%d", id, weapon, group, anim);
    }
}

struct PendingLocalWasted {
    bool pending = false;
    uint64_t life = 0;
    int weapon = 0;
    int animGroup = 0;
    int animId = 0;
};
static PendingLocalWasted g_pendingLocalWasted;  // guarded by g_remoteMutex

// CMultiplayerSA ProcessDamageEvent (multiplayer_shotsync.cpp 1143-1190) wraps CEventDamage::AffectsPed and asks
// CClientGame::DamageHandler -> ApplyPedDamageFromGame (CClientGame.cpp 4478-4852). For a ped whose health or armor is
// locked (a remote player from pure sync, an observed server ped from PED_SYNC) it restores both and refuses the event
// when the hit was fatal ("Don't play the animation if it's going to be a death one", 4739-4757; GLITCH_HITANIM off,
// so bOldBehaviour), otherwise it lets the hit animation play. The damage itself was applied before AffectsPed by
// CPedDamageResponseCalculator::ComputeDamageResponse, which is why the values are restored here.
// The phone had no such hook: a car or a punch on a remote body killed it locally, the blood pool appeared and the
// body stayed down after the real player stood up (device 2026-11-14). Unlocked peds (owned here) keep GTA's damage.
// Not ported: onClientPlayerDamage / onClientPedDamage events, team friendly fire, the bullet-sync rejection branch.
using AffectsPedFn = bool (*)(const void*, void*);
static AffectsPedFn g_origAffectsPed = nullptr;
static std::vector<HostedWasted> g_hostedWasted;  // g_remoteMutex
static std::mutex g_localDeathMutex;
static uint64_t g_deathLife = UINT64_MAX;
static HostedWasted g_localDeath;
bool CopyLocalCombatDeath(uint64_t& life, HostedWasted& out) {
    std::lock_guard lock(g_localDeathMutex);
    if (g_deathLife != g_localLife.load() || life == g_deathLife) return false;
    life = g_deathLife;
    out = g_localDeath;
    return true;
}

// CEventDamage on libGTASA 2.10 (ctor C2 0x444628): inflictor CEntity* at +0x18 (str x1, [x8, #0x18]!, registered as a
// reference), weapon at +0x24 (w3), ped piece at +0x28 (w4), death anim group / id at +0x30 / +0x34 (the ctor stores 0 and
// 0xbf = ANIM_ID_NO_ANIMATION_SET, as PC). CClientGame's GetDeathAnim (CClientGame.cpp 4446-4460) calls
// ComputeDeathAnim(ped, false) and reads them.
bool IsRemoteNetworkPlayer(void* entity) {
    if (!entity) return false;
    for (const auto& [id, body] : g_remoteBodies)
        if (body.ped == entity) return body.networkPlayer;
    return false;
}
uint32_t EntityElementId(void* entity) {
    if (!entity) return 0xFFFFFFFFu;
    for (const auto& [bodyId, stored] : g_remoteBodies)
        if (stored.ped == entity) return bodyId;
    if (entity == g_sampledLocalPed) return ServerWorld::LocalPlayer();
    auto car = g_vehicleBodyIds.find(entity);
    if (car != g_vehicleBodyIds.end()) return car->second;
    return WorldObjects::ElementOf(entity);
}

void* ElementEntity(uint32_t id) {
    if (id == 0 || id == 0xFFFFFFFFu) return nullptr;
    if (id == ServerWorld::LocalPlayer() && g_sampledLocalPed) return g_sampledLocalPed;
    if (const auto body = g_remoteBodies.find(id); body != g_remoteBodies.end() && body->second.ped) return body->second.ped;
    for (const auto& [entity, element] : g_vehicleBodyIds)
        if (element == id) return entity;
    return WorldObjects::EntityOf(id);
}

static HostedWasted ReadDeathEvent(uint32_t id, void* event, void* ped) {
    static void (*computeDeathAnim)(void*, void*, bool) = nullptr;
    static bool looked = false;
    if (!looked) {
        looked = true;
        if (void* handle = dlopen("libGTASA.so", RTLD_NOLOAD | RTLD_GLOBAL))
            computeDeathAnim = reinterpret_cast<void (*)(void*, void*, bool)>(
                dlsym(handle, "_ZN12CEventDamage16ComputeDeathAnimEP4CPedb"));
    }
    auto* ev = static_cast<unsigned char*>(event);
    HostedWasted out;
    out.id = id;
    out.damager = EntityElementId(*reinterpret_cast<void**>(ev + 0x18));
    out.weapon = *reinterpret_cast<int*>(ev + 0x24) & 0x3F;
    out.bodyPart = *reinterpret_cast<int*>(ev + 0x28);
    if (computeDeathAnim) {
        computeDeathAnim(event, ped, false);
        out.animGroup = *reinterpret_cast<int*>(ev + 0x30);
        out.animId = *reinterpret_cast<int*>(ev + 0x34);
    }
    float heading = 0.f;
    ReadBodyPos(ped, out.x, out.y, out.z, heading);
    return out;
}

static void NoteLocalCombatDeath(void* ped, void* event, uint32_t attacker, uint8_t weapon, uint8_t zone) {
    if (*reinterpret_cast<float*>(static_cast<unsigned char*>(ped) + 0x6AC) > 0.f) return;
    std::lock_guard lock(g_localDeathMutex);
    const auto life = g_localLife.load();
    if (g_sampledLife != life || g_deathLife == life || !ServerWorld::LocalPlayer()) return;
    g_deathLife = life;
    g_localDeath = event ? ReadDeathEvent(ServerWorld::LocalPlayer(), event, ped) : HostedWasted{};
    g_localDeath.id = ServerWorld::LocalPlayer();
    g_localDeath.damager = attacker;
    g_localDeath.weapon = weapon;
    g_localDeath.bodyPart = zone;
    if (!event) {
        float heading = 0.f;
        ReadBodyPos(ped, g_localDeath.x, g_localDeath.y, g_localDeath.z, heading);
    }
}

// ApplyPedDamageFromGame for a ped this client syncs (CClientGame.cpp 4813-4826): when its health goes from above 0 to
// 0, GetDeathAnim then SendPedWastedPacket (6531-6578; only while IsSyncing). GTA keeps its own death on this body.
static void NoteHostedDeath(uint32_t id, void* event, void* ped, RemoteBody& stored, float current) {
    if (current > 0.f) {
        stored.lastHealth = current;
        stored.wastedSent = false;
        return;
    }
    const bool wasAlive = stored.lastHealth != 0.f;  // -1 (never seen) counts as alive, as m_fHealth starts at 100
    stored.lastHealth = current;
    if (!wasAlive || stored.wastedSent) return;
    stored.wastedSent = true;
    PedQueries::Wasted(id);
    const auto out = ReadDeathEvent(id, event, ped);
    g_hostedWasted.push_back(out);  // caller holds g_remoteMutex
    ENGINE_LOGI("DIAG hosted ped %u died: damager=%u weapon=%d part=%d anim=%d/%d", id, out.damager, out.weapon,
        out.bodyPart, out.animGroup, out.animId);
}

static bool HookAffectsPed(const void* event, void* ped) {
    const bool affects = g_origAffectsPed ? g_origAffectsPed(event, ped) : false;
    if (!affects || !ped) return affects;
    uint32_t id = 0;
    RemoteBody* storedBody = nullptr;
    for (auto& [bodyId, stored] : g_remoteBodies) {
        if (stored.ped == ped) {
            id = bodyId;
            storedBody = &stored;
            break;
        }
    }
    // On 2.10 CEventDamage::AffectsPed (0x444bc4) tests the calculated flag
    // at +0x4A. The ctor clears the two damage floats at +0x40 (0x44469c).
    const auto* ev = static_cast<const unsigned char*>(event);
    if (ev[0x4A]) {
        const float amount = *reinterpret_cast<const float*>(ev + 0x40)
                           + *reinterpret_cast<const float*>(ev + 0x44);
        const int weapon = *reinterpret_cast<const int*>(ev + 0x24);
        const int zone = *reinterpret_cast<const int*>(ev + 0x28);
        void* inflictor = *reinterpret_cast<void* const*>(ev + 0x18);
        if (std::isfinite(amount) && amount > 0.f && weapon >= 0 && weapon <= 63 && zone >= 0 && zone <= 9) {
            if (storedBody && storedBody->networkPlayer) {
                CaptureLocalBulletHit(inflictor, weapon, {id, amount, static_cast<uint8_t>(zone)});
            } else if (!id && ped == g_sampledLocalPed && g_sampledLife == g_localLife.load()) {
                // FindPlayerPed can refer to the attacker inside remote ProcessControl.
                // Only the actual local body sampled on the game thread owns our health.
                const auto* raw = static_cast<const unsigned char*>(ped);
                const float health = *reinterpret_cast<const float*>(raw + 0x6AC);
                const float armor = *reinterpret_cast<const float*>(raw + 0x6B4);
                if (health < g_health.load() || armor < g_armor.load()) {
                    g_health = health;
                    g_armor = armor;
                    const auto attacker = EntityElementId(inflictor);
                    if (attacker && attacker < 0x1FFFFu) ++g_nativeDamageEvents;
                    g_pendingDamage.record(attacker, weapon, zone);
                    NoteLocalCombatDeath(ped, const_cast<void*>(event), attacker, weapon, zone);
                }
            }
        }
    }
    if (id == 0) return affects;
    bool healthLocked = false, armorLocked = false;
    float lockedHealth = 0.f, lockedArmor = 0.f;
    if (!CopyPedVitalsLock(id, healthLocked, lockedHealth, armorLocked, lockedArmor)) {
        // Unlocked: a ped this phone hosts takes GTA's damage. try_lock: this hook can run inside a game call made while
        // the game thread already holds g_remoteMutex, and std::mutex is not recursive.
        std::unique_lock<std::mutex> lock(g_remoteMutex, std::try_to_lock);
        if (lock.owns_lock()) {
            auto live = g_remotePeds.find(id);
            if (live != g_remotePeds.end() && live->second.hosted)
                NoteHostedDeath(id, const_cast<void*>(event), ped, *storedBody,
                    *reinterpret_cast<float*>(static_cast<unsigned char*>(ped) + 0x6AC));
        }
        return affects;
    }
    auto* raw = static_cast<unsigned char*>(ped);
    const float current = *reinterpret_cast<float*>(raw + 0x6AC);
    // Restore through the store sites ApplySyncedPlayerFields already uses (+0x6AC / +0x6B4).
    if (healthLocked && std::isfinite(lockedHealth)) *reinterpret_cast<float*>(raw + 0x6AC) = lockedHealth;
    if (armorLocked && std::isfinite(lockedArmor)) *reinterpret_cast<float*>(raw + 0x6B4) = lockedArmor;
    static int reports = 0;
    if (reports < 6) {
        ++reports;
        ENGINE_LOGI("DIAG damage on locked ped %u: health %.1f restored to %.1f, %s", id, current, lockedHealth,
            current <= 0.f ? "death blocked" : "hit animation allowed");
    }
    return current > 0.f;
}

// The ENTITY_ADD ped record, applied once per element on the game thread before the stream-in fence is armed.
// Desktop: CPacketHandler::Packet_EntityAdd case PED (CPacketHandler.cpp 4054-4184) stores the state, and
// CClientPed::_CreateModel (CClientPed.cpp 5669-6224) writes it to the GTA body.
static void ApplyPedSpawnRecord(uint32_t id, RemotePed& live) {
    if (live.spawnApplied) return;
    WorldRPG::Entity::PedSpawn spawn;
    if (!ServerWorld::CopyPedSpawn(id, spawn)) return;  // not an ENTITY_ADD ped (a player, or already removed)
    {
        std::lock_guard<std::mutex> lock(g_remoteMutex);
        live.spawnApplied = true;
        // SetFrozen (4068). _CreateModel applies it through ApplyPhysicalFreezeState.
        if (!live.frozenChanged) live.frozen = spawn.frozen;
        // SetAlpha (4073) and SetUsesCollision (4148): element state, written to the body by ApplyRemotePeds.
        if (!live.alphaChanged) live.alpha = spawn.alpha;
        if (!live.collisionsChanged) live.collisions = spawn.collisions;
        auto& queue = g_pedRpcOps[id];
        if (!live.hasAnimCache && spawn.animation.present && !spawn.animation.block.empty() && !spawn.animation.name.empty()) {
            PedRpcOp anim;
            anim.kind = PedRpcOp::Kind::Animation;
            anim.block = spawn.animation.block;
            anim.name = spawn.animation.name;
            anim.time = spawn.animation.time;
            anim.blend = spawn.animation.blendTime;
            anim.loop = spawn.animation.looped;
            anim.updatePosition = spawn.animation.updatePosition;
            anim.interruptible = spawn.animation.interruptable;
            anim.freezeLastFrame = spawn.animation.freezeLastFrame;
            queue.push_back(anim);
            live.hasAnimCache = true;
            live.animCache = anim;
        }
    }
    // SetHealth, SetArmor (4057-4058): written next tick by ApplySyncedPlayerFields (+0x6AC / +0x6B4, the
    // store sites already used for player sync). Then LockHealth / LockArmor when bSynced (4059-4063).
    bool healthLocked = false, armorLocked = false;
    float health = 0, armor = 0;
    CopyPedVitalsLock(id, healthLocked, health, armorLocked, armor);
    bool lockSpawnHealth = false, lockSpawnArmor = false;
    {
        std::lock_guard lock(g_remoteMutex);
        if (!live.syncHealth && !healthLocked) {
            live.health = spawn.health;
            live.syncHealth = true;
            lockSpawnHealth = spawn.synced && !live.hosted;
        }
        if (!live.syncArmor && !armorLocked) {
            live.armor = spawn.armor;
            live.syncArmor = true;
            lockSpawnArmor = spawn.synced && !live.hosted;
        }
    }
    if (lockSpawnHealth) LockPedHealth(id, spawn.health);
    if (lockSpawnArmor) LockPedArmor(id, spawn.armor);

    // Steps that need a hook the phone does not have yet. Each keeps its place in the desktop order.
    static int noted = 0;
    if (noted < 6) {
        ++noted;
        ENGINE_LOGI("DIAG ped spawn record id=%u applied health=%.1f armor=%.1f synced=%d frozen=%d vehicle=%u seat=%u. "
                    "Queued: collisions=%d, alpha=%u, weapons=%zu slot=%u, anim=%s/%s; dimension=%u streams the body. "
                    "Not applied (no verified hook): SetHasJetPack=%d, SetHeadless=%d, SetMoveAnim=%u (CPed::SetMoveAnim "
                    "needs the move-anim field), clothes=%zu (RebuildModel, model 0 only)",
                    id, spawn.health, spawn.armor, spawn.synced ? 1 : 0, spawn.frozen ? 1 : 0, spawn.vehicleId,
                    static_cast<unsigned>(spawn.seat), spawn.collisions ? 1 : 0, static_cast<unsigned>(spawn.alpha),
                    spawn.weapons.size(), static_cast<unsigned>(spawn.currentSlot),
                    spawn.animation.present ? spawn.animation.block.c_str() : "-",
                    spawn.animation.present ? spawn.animation.name.c_str() : "-", spawn.dimension,
                    spawn.hasJetpack ? 1 : 0, spawn.headless ? 1 : 0, static_cast<unsigned>(spawn.moveAnim),
                    spawn.clothes.size());
    }
}

// #region agent log
static void DebugPedPose(const char* hypothesis, const char* branch, uint32_t id, void* body, const RemotePed& ped, bool fence) {
    static int ticks = 0;
    if ((++ticks % 30) != 1) return;
    float bx = 0, by = 0, bz = 0, frontZ = 0, upZ = 0, placeZ = 0;
    uint32_t phys = 0;
    if (body) {
        auto* raw = static_cast<unsigned char*>(body);
        placeZ = *reinterpret_cast<float*>(raw + 0x10);
        phys = *reinterpret_cast<uint32_t*>(raw + 0x64);
        auto* matrix = *reinterpret_cast<unsigned char**>(raw + 0x18);
        const auto matrixAddr = reinterpret_cast<uintptr_t>(matrix);
        if (matrix && (matrixAddr & 3u) == 0 && matrixAddr > 0x10000u) {
            bx = *reinterpret_cast<float*>(matrix + 0x30);
            by = *reinterpret_cast<float*>(matrix + 0x34);
            bz = *reinterpret_cast<float*>(matrix + 0x38);
            frontZ = *reinterpret_cast<float*>(matrix + 0x18);
            upZ = *reinterpret_cast<float*>(matrix + 0x28);
        }
    }
    const long long now = static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
    char line[640];
    std::snprintf(line, sizeof(line),
        "{\"sessionId\":\"dc8159\",\"hypothesisId\":\"%s\",\"location\":\"GameEngine.cpp:ApplyRemotePeds\","
        "\"message\":\"ped pose\",\"data\":{\"branch\":\"%s\",\"id\":%u,\"fence\":%s,\"hosted\":%s,\"keysync\":%s,"
        "\"nx\":%.2f,\"ny\":%.2f,\"nz\":%.2f,\"bx\":%.2f,\"by\":%.2f,\"bz\":%.2f,\"placeZ\":%.2f,"
        "\"frontZ\":%.3f,\"upZ\":%.3f,\"phys\":%u,\"vx\":%.3f,\"vy\":%.3f,\"sx\":%d,\"sy\":%d},\"timestamp\":%lld}\n",
        hypothesis, branch, id, fence ? "true" : "false", ped.hosted ? "true" : "false", ped.keysync ? "true" : "false",
        ped.x, ped.y, ped.z, bx, by, bz, placeZ, frontZ, upZ, phys, ped.vx, ped.vy, ped.stickX, ped.stickY, now);
#ifdef __ANDROID__
    __android_log_print(ANDROID_LOG_INFO, "DBGDC8159", "%s", line);
#endif
    // ROADMAP_PEDS_SYNC 0.4: no file I/O on the game thread. The line stays in logcat (tag DBGDC8159).
}
// #endregion

// Task-pool occupancy, logged every 5 s. CTask::operator new (0x5d7414) returns null when CPools::ms_pTaskPool is full,
// and the next caller crashes (CTaskSimple ctor with this == 0, or CPlayerInfo::Process calling through a null
// GetSimplestActiveTask; device 2026-11-14 02:16-02:22). Layout from that function: [pool] objects, [pool+8] byte map,
// [pool+0x10] size, [pool+0x14] first free; a slot is free while bit 7 of its byte is set (tbz w14, #0x1f).
static int CountPoolUsed(const char* symbol, int& size) {
    size = 0;
    static void* handle = dlopen("libGTASA.so", RTLD_NOLOAD | RTLD_GLOBAL);
    if (!handle) return -1;
    auto** poolPtr = reinterpret_cast<unsigned char**>(dlsym(handle, symbol));
    if (!poolPtr || !*poolPtr) return -1;
    unsigned char* pool = *poolPtr;
    const auto* map = *reinterpret_cast<const signed char**>(pool + 8);
    size = *reinterpret_cast<const int*>(pool + 0x10);
    if (!map || size <= 0 || size > 100000) return -1;
    int used = 0;
    for (int i = 0; i < size; ++i)
        if (map[i] >= 0) ++used;
    return used;
}

static void LogPoolUsage(size_t bodies) {
    static int64_t last = 0;
    const int64_t now = static_cast<int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
    if (now - last < 5000) return;
    last = now;
    int taskSize = 0, pedSize = 0, eventSize = 0, intelSize = 0;
    const int tasks = CountPoolUsed("_ZN6CPools12ms_pTaskPoolE", taskSize);
    const int peds = CountPoolUsed("_ZN6CPools11ms_pPedPoolE", pedSize);
    const int events = CountPoolUsed("_ZN6CPools13ms_pEventPoolE", eventSize);
    const int intel = CountPoolUsed("_ZN6CPools23ms_pPedIntelligencePoolE", intelSize);
    ENGINE_LOGI("DIAG pools tasks=%d/%d peds=%d/%d events=%d/%d intel=%d/%d remoteBodies=%zu",
                tasks, taskSize, peds, pedSize, events, eventSize, intel, intelSize, bodies);
}

// ---------------------------------------------------------------------------------------------------------------------
// Phase 6 extras: animation, weapon, alpha and collision RPCs on the game thread.
// libGTASA 2.10 arm64 layout used here (disassembly, 2026-11-14):
//   CEntity +0x20 m_pRwObject (clump), +0x28 flags (bit 0 bUsesCollision, bit 7 bIsVisible; the same word whose bits 2
//   and 18 the collision fence already uses). CPed flags at +0x584: bInVehicle is +0x585 bit 0 (CPed::GiveWeapon
//   0x595350), so bIsLanding (flag 10) is +0x585 bit 2. CPed +0x190 real-time shadow, +0x538 intelligence (task manager
//   at intelligence+8), +0x540 player data, +0x730 CWeapon[13] (stride 0x20: type +0, state +4, ammo in clip +8, ammo
//   total +0xC), +0x8dc current slot (signed byte). CWeaponInfo +0xC model, +0x14 slot (CPed::GiveWeapon 0x59528c,
//   0x5952ec). CAnimBlock stride 0x20: loaded byte +0x10 (CAnimManager::RemoveAnimBlock 0x4688a4); IFP streaming ids
//   start at 25575 (CRunningScript 0x41d86c). CTaskSimpleRunNamedAnim keeps the hierarchy at +0x48, null when the block
//   is not loaded or has no such animation (ctor 0x5d79ec). CAnimBlendAssociation +0x28 hierarchy (total time at
//   hierarchy+0x18), +0x38 current time, +0x3c speed (UpdateTimeStep 0x464484). CTask vtable +0x28 GetTaskType, +0x38
//   MakeAbortable (_ZTV23CTaskSimpleRunNamedAnim). CTaskManager::SetTask deletes the task it replaces (0x64e0bc).
struct PedRpcFns {
    bool looked = false;
    void* (*taskNew)(unsigned long) = nullptr;
    void (*runNamedAnimCtor)(void*, const char*, const char*, int, float, int, bool, bool, bool, bool) = nullptr;
    void (*setTask)(void*, void*, int, bool) = nullptr;
    void* (*getAnimBlock)(const char*) = nullptr;
    int (*getAnimBlockIndex)(const char*) = nullptr;
    void* (*getAnimation)(const char*, const void*) = nullptr;
    void (*addAnimBlockRef)(int) = nullptr;
    void* (*clumpGetAssociation)(void*, const char*) = nullptr;
    void (*setCurrentTime)(void*, float) = nullptr;
    void (*setClumpAlpha)(void*, int) = nullptr;
    void (*returnShadow)(void*, void*) = nullptr;
    void* shadowManager = nullptr;
    const int* currArea = nullptr;
    void* (*weaponInfo)(int, signed char) = nullptr;
    int (*giveWeapon)(void*, int, unsigned, bool) = nullptr;
    void (*clearWeapon)(void*, int) = nullptr;
    void (*removeWeaponModel)(void*, int) = nullptr;
    void (*setCurrentWeapon)(void*, int) = nullptr;
    void (*makeChangesForNewWeapon)(void*, int) = nullptr;
    void (*weaponShutdown)(void*) = nullptr;
};

static PedRpcFns& PedRpc() {
    static PedRpcFns f;
    if (f.looked) return f;
    f.looked = true;
    void* h = dlopen("libGTASA.so", RTLD_NOLOAD | RTLD_GLOBAL);
    if (!h) return f;
    auto sym = [&](auto& out, const char* name) { out = reinterpret_cast<std::remove_reference_t<decltype(out)>>(dlsym(h, name)); };
    sym(f.taskNew, "_ZN5CTasknwEm");
    sym(f.runNamedAnimCtor, "_ZN23CTaskSimpleRunNamedAnimC1EPKcS1_ifibbbb");
    sym(f.setTask, "_ZN12CTaskManager7SetTaskEP5CTaskib");
    sym(f.getAnimBlock, "_ZN12CAnimManager17GetAnimationBlockEPKc");
    sym(f.getAnimBlockIndex, "_ZN12CAnimManager22GetAnimationBlockIndexEPKc");
    sym(f.getAnimation, "_ZN12CAnimManager12GetAnimationEPKcPK10CAnimBlock");
    sym(f.addAnimBlockRef, "_ZN12CAnimManager15AddAnimBlockRefEi");
    sym(f.clumpGetAssociation, "_Z30RpAnimBlendClumpGetAssociationP7RpClumpPKc");
    sym(f.setCurrentTime, "_ZN21CAnimBlendAssociation14SetCurrentTimeEf");
    sym(f.setClumpAlpha, "_ZN18CVisibilityPlugins13SetClumpAlphaEP7RpClumpi");
    sym(f.returnShadow, "_ZN22CRealTimeShadowManager20ReturnRealTimeShadowEP15CRealTimeShadow");
    f.shadowManager = dlsym(h, "g_realTimeShadowMan");
    f.currArea = static_cast<const int*>(dlsym(h, "_ZN5CGame8currAreaE"));
    sym(f.weaponInfo, "_ZN11CWeaponInfo13GetWeaponInfoE11eWeaponTypea");
    sym(f.giveWeapon, "_ZN4CPed10GiveWeaponE11eWeaponTypejb");
    sym(f.clearWeapon, "_ZN4CPed11ClearWeaponE11eWeaponType");
    sym(f.removeWeaponModel, "_ZN4CPed17RemoveWeaponModelEi");
    sym(f.setCurrentWeapon, "_ZN4CPed16SetCurrentWeaponEi");
    sym(f.makeChangesForNewWeapon, "_ZN10CPlayerPed23MakeChangesForNewWeaponEi");
    sym(f.weaponShutdown, "_ZN7CWeapon8ShutdownEv");
    ENGINE_LOGI("DIAG ped RPC natives: anim=%d/%d/%d/%d assoc=%d/%d alpha=%d shadow=%d/%d area=%d weapons=%d/%d/%d/%d/%d/%d/%d",
        f.runNamedAnimCtor != nullptr, f.getAnimBlock != nullptr, f.getAnimation != nullptr, f.setTask != nullptr,
        f.clumpGetAssociation != nullptr, f.setCurrentTime != nullptr, f.setClumpAlpha != nullptr, f.returnShadow != nullptr,
        f.shadowManager != nullptr, f.currArea != nullptr, f.weaponInfo != nullptr, f.giveWeapon != nullptr,
        f.clearWeapon != nullptr, f.removeWeaponModel != nullptr, f.setCurrentWeapon != nullptr,
        f.makeChangesForNewWeapon != nullptr, f.weaponShutdown != nullptr);
    return f;
}

static constexpr int kWeaponSlots = 13;      // WEAPONSLOT_MAX
static constexpr int kIfpStreamingBase = 25575;

static unsigned char* PedWeaponAt(void* ped, int slot) {
    return static_cast<unsigned char*>(ped) + 0x730 + static_cast<size_t>(slot) * 0x20;
}
static int& WeaponType(unsigned char* w) { return *reinterpret_cast<int*>(w); }
static int& WeaponClip(unsigned char* w) { return *reinterpret_cast<int*>(w + 0x8); }
static int& WeaponTotal(unsigned char* w) { return *reinterpret_cast<int*>(w + 0xC); }
// CWeapon::Update (0x707778 / 0x7078c0) loads and stores the weapon state at +4.
static bool WeaponReloading(const unsigned char* w) { return *reinterpret_cast<const int*>(w + 4) == 2; }
static int CurrentWeaponSlot(void* ped) { return *(reinterpret_cast<signed char*>(ped) + 0x8dc); }

// Read-only Lua inventory queries use the same verified 2.10 layout as the RPCs.
// SetCurrentWeapon+0x1c/0x24 (0x59b7cc/0x59b7d4) loads slot/type;
// CWeapon::Reload+0x10/+0x50 (0x700a3c/0x700a7c) loads total/stores clip.
// Refuse other layouts instead of reading a guessed CPed structure.
static bool WeaponLayoutValid() {
    static const bool layout = [] {
        void* h = dlopen("libGTASA.so", RTLD_NOLOAD | RTLD_GLOBAL);
        auto* set = h ? static_cast<const unsigned char*>(dlsym(h, "_ZN4CPed16SetCurrentWeaponEi")) : nullptr;
        auto* reload = h ? static_cast<const unsigned char*>(dlsym(h, "_ZN7CWeapon6ReloadEP4CPed")) : nullptr;
        const bool ok = set && reload && *reinterpret_cast<const uint32_t*>(set + 0x1c) == 0x39a37268 &&
            *reinterpret_cast<const uint32_t*>(set + 0x24) == 0xb9473120 &&
            *reinterpret_cast<const uint32_t*>(reload + 0x10) == 0xb9400e68 &&
            *reinterpret_cast<const uint32_t*>(reload + 0x50) == 0xb9000a68;
        if (!ok) ENGINE_LOGI("DIAG ped query: unsupported weapon layout");
        return ok;
    }();
    return layout;
}

static bool ReadPedInventory(void* ped, PedQueries::Snapshot& out) {
    if (!ped || !WeaponLayoutValid()) return false;
    const int slot = CurrentWeaponSlot(ped);
    if (slot < 0 || slot >= kWeaponSlots) return false;
    out.currentSlot = slot;
    for (unsigned i = 0; i < PedQueries::Slots; ++i) {
        auto* w = PedWeaponAt(ped, i);
        if (WeaponType(w) < 0 || WeaponType(w) > 46) return false;
        out.weapons[i] = {static_cast<uint32_t>(WeaponType(w)), static_cast<uint32_t>(WeaponTotal(w)),
            static_cast<uint32_t>(WeaponClip(w))};
    }
    out.streamed = true;
    return true;
}
static void SavePedInventory(uint32_t id, const RemoteBody& body) {
    if (!body.ped || IsPlaceableVtable(body.ped)) return;
    PedQueries::Snapshot state;
    if (!PedQueries::Copy(id, state) || state.generation != body.queryGeneration) return;
    if (ReadPedInventory(body.ped, state)) PedQueries::StreamOut(id, state);
}

// CWeaponInfo for the standard skill, or null.
static const unsigned char* WeaponInfoOf(int type) {
    auto& f = PedRpc();
    if (!f.weaponInfo || type < 0 || type > 46) return nullptr;
    return static_cast<const unsigned char*>(f.weaponInfo(type, 1));
}
float NativeWeaponRange(int type) {
    const auto* info = WeaponInfoOf(type);
    // FireInstantHit (0x7018b0) reads CWeaponInfo::m_fWeaponRange.
    return info ? *reinterpret_cast<const float*>(info + 0x08) : 0.f;
}

// CClientPed::GetShotData and AdjustShotOriginForWalls (8020-8139).
static bool TraceWeaponAim(const PlayerWeaponSync::Vector& start, const PlayerWeaponSync::Vector& end,
                           void* ignorePed, PlayerWeaponSync::Vector& hit) {
    using Vector = PlayerWeaponSync::Vector;
    struct alignas(16) CollisionPoint { Vector position; uint8_t rest[0x30 - sizeof(Vector)]; };
    using Trace = bool (*)(const Vector&, const Vector&, CollisionPoint&, void*&, bool, bool, bool, bool, bool, bool, bool, bool);
    static void* const lib = dlopen("libGTASA.so", RTLD_NOLOAD | RTLD_GLOBAL);
    static const auto trace = lib ? reinterpret_cast<Trace>(dlsym(lib,
        "_ZN6CWorld18ProcessLineOfSightERK7CVectorS2_R9CColPointRP7CEntitybbbbbbbb")) : nullptr;
    static auto** const ignore = lib ? static_cast<void**>(dlsym(lib, "_ZN6CWorld13pIgnoreEntityE")) : nullptr;
    // CColPoint::operator= copies XYZ at +0, +4, +8 and depth at +0x28 (ARM64 0x3a4ca8-0x3a4cec).
    static const bool layout = [] {
        const auto* copy = lib ? static_cast<const uint32_t*>(dlsym(lib, "_ZN9CColPointaSERKS_")) : nullptr;
        return copy && copy[1] == 0xb9000808 && copy[3] == 0xf9000008 && copy[0x44 / 4] == 0xb9002808;
    }();
    if (!trace || !ignore || !layout) {
        static bool logged = false;
        if (!logged) ENGINE_LOGI("DIAG weapon aim collision query unavailable");
        logged = true;
        return false;
    }
    CollisionPoint point{};
    void* entity = nullptr;
    void* previous = *ignore;
    *ignore = ignorePed;
    const bool found = trace(start, end, point, entity, true, true, true, true, true, false, false, false);
    *ignore = previous;
    if (found) hit = point.position;
    return found;
}

static void SampleLocalWeapon(void* ped, Vec3 position, float heading) {
    using namespace PlayerWeaponSync;
    LocalWeapon weapon;
    if (WeaponLayoutValid()) {
        const int slot = CurrentWeaponSlot(ped);
        if (slot >= 0 && slot < kWeaponSlots) {
            auto* native = PedWeaponAt(ped, slot);
            const int type = WeaponType(native);
            if (type >= 0 && type <= 46) {
                weapon.valid = true;
                weapon.slot = static_cast<uint8_t>(slot);
                weapon.type = static_cast<uint8_t>(type);
                weapon.clip = static_cast<uint16_t>(std::clamp(WeaponClip(native), 0, 65535));
                weapon.reloading = WeaponReloading(native);
                for (int i = 0; i < kWeaponSlots; ++i)
                    weapon.totalBySlot[i] = static_cast<uint16_t>(std::clamp(WeaponTotal(PedWeaponAt(ped, i)), 0, 65535));
                weapon.total = weapon.totalBySlot[slot];
            }
        }
    }
    bool firstPersonAim = false;
    weapon.gangDriveby = HasNativeDriveby(ped);
    if (weapon.valid && SlotHasAmmo(weapon.slot)) {
        using BoneFn = void (*)(void*, Vector&, unsigned, bool);
        using TargetVectorFn = void (*)(void*, float, Vector, Vector&, Vector&);
        using TargetingFn = bool (*)(void*);
        static void* const lib = dlopen("libGTASA.so", RTLD_NOLOAD | RTLD_GLOBAL);
        static void* const camera = lib ? dlsym(lib, "TheCamera") : nullptr;
        static const auto bone = lib ? reinterpret_cast<BoneFn>(
            dlsym(lib, "_ZN4CPed26GetTransformedBonePositionER5RwV3djb")) : nullptr;
        static const auto targetVector = lib ? reinterpret_cast<TargetVectorFn>(
            dlsym(lib, "_ZN7CCamera28Find3rdPersonCamTargetVectorEf7CVectorRS0_S1_")) : nullptr;
        static const auto targeting = lib ? reinterpret_cast<TargetingFn>(
            dlsym(lib, "_ZN7CCamera17IsTargetingActiveEv")) : nullptr;
        static const bool haveAimHooks = camera && bone && targetVector && targeting;
        if (!haveAimHooks) {
            static bool logged = false;
            if (!logged) ENGINE_LOGI("DIAG GetShotData: missing camera or muzzle symbol; using the ped heading");
            logged = true;
        }

        // CClientPed::GetShotData (7995-8107): muzzle at the right wrist, then
        // crosshair ray if aiming, or ped heading when firing from the hip.
        // FireInstantHit (0x701624 / 0x7018b0) reads CWeaponInfo range at +0x8;
        // CTaskSimpleUseGun::FireGun (0x5dfd7c-0x5dfd88) loads the offset at +0x24.
        const auto* info = WeaponInfoOf(weapon.type);
        Vector muzzle{position.x, position.y, position.z + 0.7f};
        float range = 1.f;
        if (info) {
            const float nativeRange = *reinterpret_cast<const float*>(info + 0x8);
            if (std::isfinite(nativeRange) && nativeRange > 0.f) range = nativeRange;
            const auto* rawPed = static_cast<const unsigned char*>(ped);
            if (bone && *reinterpret_cast<void* const*>(rawPed + 0x20) &&
                *reinterpret_cast<void* const*>(rawPed + 0x18)) {
                std::memcpy(&muzzle, info + 0x24, sizeof muzzle);
                bone(ped, muzzle, 24, false);  // BONE_RIGHTWRIST
            }
        }
        // CPad::GetTarget itself mutates native vehicle and touch state. Use
        // the genuine getter result captured while GTA processed local input.
        weapon.aiming = LocalTargetButtonHeld() || (camera && targeting && targeting(camera));
        Vector origin = muzzle;
        Vector target{muzzle.x - std::sin(heading) * range, muzzle.y + std::cos(heading) * range, muzzle.z};
        // LoadWeaponData 0x5613d8 stores the weapon flags. FireSniper 0x702bac-0x702c14
        // selects the active CCam and copies its front and source into the projectile call.
        static const bool cameraLayout = [] {
            const auto* sniper = lib ? static_cast<const uint32_t*>(dlsym(lib, "_ZN7CWeapon10FireSniperEP4CPedP7CEntityP7CVector")) : nullptr;
            const auto* load = lib ? static_cast<const uint32_t*>(dlsym(lib, "_ZN11CWeaponInfo14LoadWeaponDataEv")) : nullptr;
            const bool valid = sniper && load && load[0x530 / 4] == 0xb9001b69 &&
                sniper[0x8c / 4] == 0x52804509 && sniper[0x98 / 4] == 0x39417d0a &&
                sniper[0xcc / 4] == 0x910bb10a && sniper[0xdc / 4] == 0xf9417108;
            if (!valid) ENGINE_LOGI("DIAG first-person camera layout unavailable");
            return valid;
        }();
        if (info && camera && cameraLayout && (*reinterpret_cast<const uint32_t*>(info + 0x18) & 4u)) {
            const auto* bytes = static_cast<const uint8_t*>(camera);
            const unsigned index = bytes[0x5f];
            if (index < 3) {
                const auto* cam = bytes + index * 0x228;
                Vector front, source;
                std::memcpy(&front, cam + 0x2e0, sizeof front);
                std::memcpy(&source, cam + 0x2ec, sizeof source);
                firstPersonAim = SetFirstPersonAim(weapon.aim, source, front);
            }
        }
        if (!firstPersonAim) {
            if (camera && targetVector && weapon.aiming) {
                targetVector(camera, range, muzzle, origin, target);
            } else if (camera && targetVector && weapon.gangDriveby) {
                using CameraPosition = const Vector& (*)(void*);
                static const auto cameraPosition = reinterpret_cast<CameraPosition>(
                    dlsym(lib, "_ZN7CCamera18GetGameCamPositionEv"));
                if (cameraPosition) {
                    const Vector source = cameraPosition(camera);
                    Vector rayOrigin = source, collision;
                    targetVector(camera, range, source, rayOrigin, target);
                    if (TraceWeaponAim(source, target, nullptr, collision)) target = collision;
                } else {
                    static bool logged = false;
                    if (!logged) ENGINE_LOGI("DIAG drive-by camera position unavailable");
                    logged = true;
                    targetVector(camera, range, muzzle, origin, target);
                }
            }
            SetAimLine(weapon.aim, origin, target);
        }
        if (weapon.aim.full && (weapon.aiming || firstPersonAim)) {
            const float pullback = firstPersonAim ? 2.5f : 0.5f;
            const Vector dir = weapon.aim.direction;
            const Vector source = weapon.aim.origin;
            const Vector behind{source.x - pullback * dir.x, source.y - pullback * dir.y, source.z - pullback * dir.z};
            const Vector ahead{source.x + dir.x, source.y + dir.y, source.z + dir.z};
            Vector hit;
            if (TraceWeaponAim(behind, ahead, ped, hit)) {
                const float dist = std::hypot(hit.x - behind.x, hit.y - behind.y, hit.z - behind.z);
                if (dist < pullback) weapon.aim.origin = hit;
            }
        }
    }

    std::lock_guard lock(g_remoteMutex);
    // A shot captured from the GTA engine gives a more exact source than a
    // pose sampled before the shot. Restrict the fallback to the same weapon.
    if (weapon.valid && !weapon.aiming && !firstPersonAim && g_lastLocalShotWeapon == weapon.type &&
        std::chrono::steady_clock::now() - g_lastLocalShotTime < std::chrono::milliseconds(150)
        && g_lastLocalShotAim.full)
        weapon.aim = g_lastLocalShotAim;
    if (weapon.aim.full) {
        const auto& dir = weapon.aim.direction;
        const float pitch = std::atan2(dir.z, std::hypot(dir.x, dir.y));
        weapon.aim.arm = static_cast<int16_t>(pitch * (90.f * 180.f / 3.14159265f));
    }
    if (weapon.aim.full) SampleNativeWeaponAim(ped, weapon, firstPersonAim);
    if (g_sampledLife == g_localLife.load()) g_localWeapon = weapon;
}

static int WeaponInfoModel(int type) {
    const unsigned char* info = WeaponInfoOf(type);
    return info ? *reinterpret_cast<const int*>(info + 0xC) : -1;
}
static int WeaponInfoSlot(int type) {
    const unsigned char* info = WeaponInfoOf(type);
    const int slot = info ? *reinterpret_cast<const int*>(info + 0x14) : -1;
    return slot >= 0 && slot < kWeaponSlots ? slot : -1;
}
// CPedSA::GetWeapon(eWeaponType): the weapon in that type's slot when it holds that type.
static unsigned char* PedWeaponOfType(void* ped, int type) {
    const int slot = WeaponInfoSlot(type);
    if (slot < 0) return nullptr;
    unsigned char* w = PedWeaponAt(ped, slot);
    return WeaponType(w) == type ? w : nullptr;
}

// CPedSA::SetCurrentWeaponSlot (CPedSA.cpp 717-760).
static void PedSetCurrentWeaponSlotSA(void* ped, int slot, bool local) {
    auto& f = PedRpc();
    if (slot < 0 || slot >= kWeaponSlots || !f.removeWeaponModel || !f.setCurrentWeapon) return;
    const int current = CurrentWeaponSlot(ped);
    if (slot == current) return;
    if (current >= 0 && current < kWeaponSlots) {
        const int model = WeaponInfoModel(WeaponType(PedWeaponAt(ped, current)));
        f.removeWeaponModel(ped, model);
    }
    *(reinterpret_cast<signed char*>(ped) + 0x8dc) = static_cast<signed char>(slot);
    if (local && f.makeChangesForNewWeapon) {
        // PlayerPedData.m_nChosenWeapon (pPlayerData +0x2c, CPed::SetCurrentWeapon 0x59b824), then
        // CPlayerPed::MakeChangesForNewWeapon(int).
        if (auto* data = *reinterpret_cast<unsigned char**>(static_cast<unsigned char*>(ped) + 0x540))
            data[0x2c] = static_cast<unsigned char>(slot);
        f.makeChangesForNewWeapon(ped, slot);
    } else {
        f.setCurrentWeapon(ped, slot);
    }
}

// CWeaponSA::Remove (CWeaponSA.cpp 41-56).
static void PedWeaponRemove(void* ped, int slot, bool local) {
    auto& f = PedRpc();
    if (!f.weaponShutdown) return;
    unsigned char* w = PedWeaponAt(ped, slot);
    f.weaponShutdown(w);
    if (CurrentWeaponSlot(ped) == slot) {
        if (f.removeWeaponModel) f.removeWeaponModel(ped, WeaponInfoModel(WeaponType(w)));
        PedSetCurrentWeaponSlotSA(ped, 0, local);
    }
}

// CClientPed::RemoveWeapon (CClientPed.cpp 4191-4220). Desktop passes the weapon type as the model id to
// RemoveWeaponModel; kept as is.
static void PedRemoveWeapon(void* ped, int type, bool local) {
    auto& f = PedRpc();
    if (!f.clearWeapon) return;
    f.clearWeapon(ped, type);
    if (f.removeWeaponModel) f.removeWeaponModel(ped, type);
    if (unsigned char* w = PedWeaponOfType(ped, type)) {
        WeaponType(w) = 0;
        WeaponClip(w) = 0;
        WeaponTotal(w) = 0;
        PedWeaponRemove(ped, static_cast<int>((w - PedWeaponAt(ped, 0)) / 0x20), local);
    }
}

// CPedSA::GiveWeapon (CPedSA.cpp 614-641) + CClientPed::GiveWeapon (CClientPed.cpp 3989-4076). Desktop loads the weapon
// model with a blocking request first. Here: one request plus one LoadAllRequestedModels per frame, and the give waits
// (false) until the model is in; after 120 frames it runs anyway (CPed::AddWeaponModel stores a null clump when the
// model is missing, 0x59b1d8). The detonator for satchels is given too. Returns false while waiting.
static bool PedGiveWeapon(void* ped, int type, unsigned ammo, bool setCurrent, bool local, PedRpcOp* wait) {
    auto& f = PedRpc();
    if (!f.giveWeapon || !WeaponInfoOf(type)) return true;
    if (type != 0) {
        const int model = WeaponInfoModel(type);
        if (model > 0 && StreamingLoadState(model) != 1 && wait && wait->waits < 120) {
            if (g_requestModel) g_requestModel(model, 0x14);
            if (g_loadAll && !wait->requested) g_loadAll(true);
            wait->requested = true;
            ++wait->waits;
            if (StreamingLoadState(model) != 1) return false;
        }
        if (type == 39) PedGiveWeapon(ped, 40, 1, false, local, nullptr);  // WEAPONTYPE_REMOTE_SATCHEL_CHARGE
    }
    unsigned prevTotal = 0, prevClip = 0;
    int prevType = -1;
    if (unsigned char* prev = PedWeaponOfType(ped, type)) {
        prevTotal = static_cast<unsigned>(WeaponTotal(prev));
        prevClip = static_cast<unsigned>(WeaponClip(prev));
        prevType = WeaponType(prev);
    }
    const int slot = f.giveWeapon(ped, type, ammo, true);
    if (slot < 0 || slot >= kWeaponSlots) return true;
    unsigned char* w = PedWeaponAt(ped, slot);
    if (prevClip) {
        unsigned total = 0;
        if (WeaponType(w) != prevType) {
            if (slot <= 1 || slot >= 10) total = 1;
            else if (slot >= 3 && slot <= 5) total = prevTotal + ammo;
            else total = ammo;
        } else {
            total = prevTotal + ammo;
        }
        if (prevClip > total) prevClip = total;
        WeaponTotal(w) = static_cast<int>(total);
        WeaponClip(w) = static_cast<int>(prevClip);
    }
    if (setCurrent) PedSetCurrentWeaponSlotSA(ped, slot, local);  // CWeaponSA::SetAsCurrentWeapon
    return true;
}

// CClientPed::SetCurrentWeaponSlot (CClientPed.cpp 4078-4130), streamed-in branch.
static void PedSetCurrentWeaponSlot(void* ped, int slot, bool local) {
    if (slot < 0 || slot >= kWeaponSlots || slot == CurrentWeaponSlot(ped)) return;
    if (slot == 0) {
        const int current = CurrentWeaponSlot(ped);
        if (current < 0 || current >= kWeaponSlots) return;
        unsigned char* old = PedWeaponAt(ped, current);
        const int clip = WeaponClip(old);
        const int total = WeaponTotal(old);
        const int type = WeaponType(old);
        const bool goggles = current == 11 && (type == 44 || type == 45);  // parachute slot, night / infrared
        if (!goggles) PedRemoveWeapon(ped, type, local);
        PedSetCurrentWeaponSlotSA(ped, 0, local);
        if (!goggles) {
            PedGiveWeapon(ped, type, static_cast<unsigned>(total), false, local, nullptr);
            if (unsigned char* again = PedWeaponOfType(ped, type)) {
                WeaponClip(again) = clip;
                WeaponTotal(again) = total;
            }
        }
        if (HasNativeDriveby(ped)) SetNativeDriveby(ped, nullptr, 0, false);
        return;
    }
    unsigned char* w = PedWeaponAt(ped, slot);
    if (WeaponType(w) != 0 && WeaponTotal(w) != 0) PedSetCurrentWeaponSlotSA(ped, slot, local);
}

// CClientPed::KillTask(priority, true) (CClientPed.cpp 4505-4522): MakeAbortable(ABORT_PRIORITY_IMMEDIATE), then
// RemoveTask. CTaskManager::SetTask(nullptr) deletes the old task (0x64e12c), which is CTaskSA::Destroy's job on desktop.
static void KillPedTask(void* ped, int priority) {
    auto& f = PedRpc();
    void* intel = PedIntel(ped);
    if (!intel || !f.setTask) return;
    auto* manager = static_cast<unsigned char*>(intel) + 8;
    void* task = reinterpret_cast<void**>(manager)[priority];
    if (!task) return;
    using AbortFn = bool (*)(void*, void*, int, const void*);
    auto abortable = reinterpret_cast<AbortFn>((*reinterpret_cast<void***>(task))[0x38 / 8]);
    abortable(task, ped, 2, nullptr);
    f.setTask(manager, nullptr, priority, false);
}

// CClientPed::RunNamedAnimation (CClientPed.cpp 9139-9224). Returns false while the anim block is still streaming.
// CAnimBlockSA::Request(BLOCKING) is RequestModel(id, 0x14), AddRef, LoadAllRequestedModels(true); a block that is still
// not loaded keeps the request queued for later frames (up to 120) instead of the desktop's "Blocking anim load fail".
// Not run: jetpack removal and choking (not synced to remote bodies here), InsertRunNamedAnimTaskToMap (the
// custom-IFP map), the duck task restore on anim end, custom IFP blocks.
static bool PedRunNamedAnimation(uint32_t id, void* ped, PedRpcOp& op) {
    auto& f = PedRpc();
    if (!f.getAnimBlock || !f.getAnimBlockIndex || !f.runNamedAnimCtor || !f.taskNew || !f.setTask || !f.getAnimation)
        return true;
    auto* block = static_cast<unsigned char*>(f.getAnimBlock(op.block.c_str()));
    if (!block) {
        ENGINE_LOGI("DIAG setPedAnimation id=%u block %s unknown (custom IFP not ported)", id, op.block.c_str());
        return true;
    }
    if (!block[0x10]) {
        const int index = f.getAnimBlockIndex(op.block.c_str());
        if (index < 0) return true;
        if (!op.requested) {
            op.requested = true;
            if (g_requestModel) g_requestModel(kIfpStreamingBase + index, 0x14);
            if (f.addAnimBlockRef) f.addAnimBlockRef(index);
            if (g_loadAll) g_loadAll(true);
        } else if (g_requestModel) {
            g_requestModel(kIfpStreamingBase + index, 0x14);
        }
        if (!block[0x10]) {
            if (++op.waits < 120) return false;
            ENGINE_LOGI("DIAG setPedAnimation id=%u: anim block %s did not load", id, op.block.c_str());
            return true;
        }
    }
    // The task's constructor resolves the hierarchy and StartAnim uses it; a name the block lacks is refused here.
    if (!f.getAnimation(op.name.c_str(), block)) {
        ENGINE_LOGI("DIAG setPedAnimation id=%u: %s/%s not in the block", id, op.block.c_str(), op.name.c_str());
        return true;
    }
    auto* raw = static_cast<unsigned char*>(ped);
    raw[0x585] &= static_cast<unsigned char>(~0x04u);  // SetLanding(false), Fix #366
    int flags = 0x10;
    if (op.loop) flags |= 0x2;
    if (op.updatePosition) flags |= 0x40 | 0x80;
    if (!op.interruptible) {
        KillPedTask(ped, 0);  // TASK_PRIORITY_PHYSICAL_RESPONSE
        KillPedTask(ped, 1);  // TASK_PRIORITY_EVENT_RESPONSE_TEMP
        KillPedTask(ped, 2);  // TASK_PRIORITY_EVENT_RESPONSE_NONTEMP
    }
    if (!op.freezeLastFrame) flags |= 0x08;
    const float blendDelta = 1.f / std::max(static_cast<float>(op.blend), 1.f) * 1000.f;
    void* intel = PedIntel(ped);
    void* task = intel ? f.taskNew(0x80) : nullptr;  // CTask::operator new ignores the size (pool slot)
    if (!task) {
        ENGINE_LOGI("DIAG setPedAnimation id=%u: no task slot", id);
        return true;
    }
    f.runNamedAnimCtor(task, op.name.c_str(), op.block.c_str(), flags, blendDelta, op.time, !op.interruptible, false,
                       false, false);
    f.setTask(static_cast<unsigned char*>(intel) + 8, task, 3, false);  // TASK_PRIORITY_PRIMARY
    static int reports = 0;
    if (reports < 8) {
        ++reports;
        ENGINE_LOGI("DIAG setPedAnimation id=%u %s/%s time=%d blend=%d loop=%d updatePos=%d interruptible=%d freeze=%d",
            id, op.block.c_str(), op.name.c_str(), op.time, op.blend, op.loop, op.updatePosition, op.interruptible,
            op.freezeLastFrame);
    }
    return true;
}

// CClientPed::KillAnimation (CClientPed.cpp 9226-9245): a primary TASK_SIMPLE_NAMED_ANIM (401) or TASK_SIMPLE_ANIM (400)
// is aborted and removed.
static void PedKillAnimation(void* ped) {
    auto& f = PedRpc();
    void* intel = PedIntel(ped);
    if (!intel || !f.setTask) return;
    auto* manager = static_cast<unsigned char*>(intel) + 8;
    void* task = reinterpret_cast<void**>(manager)[3];
    if (!task) return;
    using TypeFn = int (*)(void*);
    const int type = reinterpret_cast<TypeFn>((*reinterpret_cast<void***>(task))[0x28 / 8])(task);
    if (type == 400 || type == 401) KillPedTask(ped, 3);
}

// CPedRPCs::SetPedAnimationProgress / SetPedAnimationSpeed (CPedRPCs.cpp 322-375) through
// RpAnimBlendClumpGetAssociation and CAnimBlendAssociationSA::SetCurrentProgress / SetCurrentSpeed.
static void PedAnimationProgressOrSpeed(void* ped, const PedRpcOp& op) {
    auto& f = PedRpc();
    void* clump = *reinterpret_cast<void**>(static_cast<unsigned char*>(ped) + 0x20);
    if (!clump || !f.clumpGetAssociation || !std::isfinite(op.value)) return;
    auto* assoc = static_cast<unsigned char*>(f.clumpGetAssociation(clump, op.name.c_str()));
    if (!assoc) return;
    if (op.kind == PedRpcOp::Kind::AnimationSpeed) {
        *reinterpret_cast<float*>(assoc + 0x3c) = op.value;
        return;
    }
    auto* hierarchy = *reinterpret_cast<unsigned char**>(assoc + 0x28);
    if (!hierarchy || !f.setCurrentTime) return;
    f.setCurrentTime(assoc, *reinterpret_cast<float*>(hierarchy + 0x18) * op.value);
}

// CClientPlayer::DischargeWeapon (260-296), CWeaponSA::FireBullet (95-144).
// Explicitly replay the received ray. The control hook still animates the gun,
// but suppresses the separate native fire call for weapons using bullet sync.
static bool DischargeRemoteBullet(void* body, RemoteBullet& bullet) {
    using Vector = PlayerWeaponSync::Vector;
    using FireFn = bool (*)(void*, void*, Vector*, Vector*, void*, Vector*, Vector*, bool, bool);
    using BoneFn = void (*)(void*, Vector&, unsigned, bool);
    static void* const lib = dlopen("libGTASA.so", RTLD_NOLOAD | RTLD_GLOBAL);
    static const auto fire = lib ? reinterpret_cast<FireFn>(
        dlsym(lib, "_ZN7CWeapon14FireInstantHitEP7CEntityP7CVectorS3_S1_S3_S3_bb")) : nullptr;
    static const auto bone = lib ? reinterpret_cast<BoneFn>(
        dlsym(lib, "_ZN4CPed26GetTransformedBonePositionER5RwV3djb")) : nullptr;
    static auto** const ignore = lib ? static_cast<void**>(dlsym(lib, "_ZN6CWorld13pIgnoreEntityE")) : nullptr;
    if (!fire || !ignore || !WeaponLayoutValid()) {
        static bool logged = false;
        if (!logged) ENGINE_LOGI("DIAG FireBullet: instant-hit or ignore-entity hook is unavailable");
        logged = true;
        return false;
    }
    const auto& shot = bullet.shot;
    // CClientPlayer::DischargeWeapon checks the live server settings after receiving the packet.
    // A settings change can enable native pad fire while an earlier bullet is still queued.
    if (!Combat::WeaponUsesBulletSync(shot.weapon)) {
        const float remaining = Combat::RemainingBackupDamage(shot, ServerWorld::LocalPlayer(), 0.f);
        if (remaining > 0.f) NoteDamage(shot.shooter, remaining, shot.weapon, shot.zone);
        return true;
    }
    auto* weapon = PedWeaponOfType(body, shot.weapon);
    const int slot = WeaponInfoSlot(shot.weapon);
    if (!weapon || CurrentWeaponSlot(body) != slot) {
        if (!PedGiveWeapon(body, shot.weapon, 99, true, false, &bullet.weaponRequest)) return false;
        weapon = PedWeaponOfType(body, shot.weapon);
        if (!weapon || CurrentWeaponSlot(body) != slot) return false;
    }

    Vector start{shot.start[0], shot.start[1], shot.start[2]};
    Vector target{shot.end[0], shot.end[1], shot.end[2]};
    Vector muzzle = start;
    const auto* rawPed = static_cast<const unsigned char*>(body);
    if (const auto* info = WeaponInfoOf(shot.weapon);
        info && bone && *reinterpret_cast<void* const*>(rawPed + 0x20) &&
        *reinterpret_cast<void* const*>(rawPed + 0x18)) {
        std::memcpy(&muzzle, info + 0x24, sizeof muzzle);
        bone(body, muzzle, 24, false);
    }
    if (!SetBulletReplayRay(&shot)) return false;
    const auto local = ServerWorld::LocalPlayer();
    auto* victim = g_findPlayer ? static_cast<unsigned char*>(g_findPlayer(0)) : nullptr;
    if (victim != g_sampledLocalPed) victim = nullptr;
    const auto damageBefore = g_nativeDamageEvents;
    auto vitals = [victim]() {
        return victim ? *reinterpret_cast<const float*>(victim + 0x6AC) +
                        *reinterpret_cast<const float*>(victim + 0x6B4) : 0.f;
    };
    const float before = vitals();
    void* previous = *ignore;
    *ignore = body;
    fire(weapon, body, &start, &muzzle, nullptr, &target, nullptr, false, true);
    SetBulletReplayRay(nullptr);
    *ignore = previous;
    if (victim && bullet.localLife == g_localLife.load()) {
        const float nativeDamage = std::max(0.f, before - vitals());
        if (nativeDamage > 0.f) {
            // A native ray can hit us even if the shooter did not report a victim.
            // Keep the body part from AffectsPed when it reported this same hit.
            g_health = *reinterpret_cast<const float*>(victim + 0x6AC);
            g_armor = *reinterpret_cast<const float*>(victim + 0x6B4);
            const uint8_t zone = shot.hit && shot.victim == local && shot.zone >= 3 && shot.zone <= 9 ? shot.zone : 3;
            if (g_nativeDamageEvents == damageBefore)
                g_pendingDamage.record(shot.shooter, shot.weapon, zone);
            NoteLocalCombatDeath(victim, nullptr, shot.shooter, shot.weapon, zone);
        }
        const float remaining = Combat::RemainingBackupDamage(shot, local, nativeDamage);
        if (remaining > 0.f) NoteDamage(shot.shooter, remaining, shot.weapon, shot.zone);
    }
    // CPedSA::AddWeaponAudioEvent(FIRE). Verified against Android CWeapon::Fire:
    // 0x7011a0 adds 0x470 to the ped, 0x7011a4 selects event 0x91.
    using AudioFn = void (*)(void*, int);
    static const auto audio = reinterpret_cast<AudioFn>(dlsym(lib, "_ZN23CAEPedWeaponAudioEntity13AddAudioEventEi"));
    if (audio) audio(static_cast<unsigned char*>(body) + 0x470, 0x91);
    else {
        static bool logged = false;
        if (!logged) ENGINE_LOGI("DIAG remote weapon fire audio symbol unavailable");
        logged = true;
    }
    return true;
}

static void ApplyRemoteBullets() {
    std::deque<RemoteBullet> bullets;
    {
        std::lock_guard lock(g_remoteMutex);
        bullets.swap(g_remoteBullets);
    }
    const auto now = std::chrono::steady_clock::now();
    for (auto& bullet : bullets) {
        if (now >= bullet.deadline || bullet.localLife != g_localLife.load() ||
            !PedQueries::Current(bullet.shot.shooter, bullet.generation) ||
            ServerWorld::DimensionOf(bullet.shot.shooter) != ServerWorld::LocalDimension()) continue;
        const auto body = g_remoteBodies.find(bullet.shot.shooter);
        if (g_sampledLife == bullet.localLife && body != g_remoteBodies.end() &&
            body->second.queryGeneration == bullet.generation &&
            !IsPlaceableVtable(body->second.ped) &&
            DischargeRemoteBullet(body->second.ped, bullet)) continue;
        std::lock_guard lock(g_remoteMutex);
        if (g_remoteBullets.size() < kMaxRemoteBullets) g_remoteBullets.push_back(std::move(bullet));
    }
}

// CClientPed::AddChangeWeapon / UpdateKeysync (2864-2878, 5605-5631).
// Read the type from the inventory created by PLAYER_LIST and weapon RPCs.
// If the weapon is still loading, leave the revision pending for another frame.
static bool ApplyRemotePlayerWeapon(void* body, RemoteBody& stored, const RemotePed& ped) {
    if (!ped.weaponRevision) return true;
    if (!WeaponLayoutValid() || ped.syncWeapon.slot >= kWeaponSlots) return false;

    const auto& sync = ped.syncWeapon;
    auto* weapon = PedWeaponAt(body, sync.slot);
    const int type = WeaponType(weapon);
    if (sync.slot && (type <= 0 || type > 46)) return false;

    if (stored.appliedWeaponRevision != ped.weaponRevision) {
        if (sync.ammoSynced) {
            WeaponTotal(weapon) = sync.total;
            // Desktop avoids overwriting the clip while GTA plays a reload.
            if (!WeaponReloading(weapon)) WeaponClip(weapon) = sync.clip;
        } else if (sync.slot && !PlayerWeaponSync::SlotHasAmmo(sync.slot) && WeaponTotal(weapon) == 0) {
            // PLAYER_LIST carries the type only. GTA requires one unit to select even a melee weapon.
            WeaponTotal(weapon) = 1;
        }
        PedSetCurrentWeaponSlot(body, sync.slot, false);
        if (CurrentWeaponSlot(body) != sync.slot) return false;
        stored.appliedWeaponRevision = ped.weaponRevision;
    }
    // Goggles, detonators and cameras must not use the generic fire input.
    return CurrentWeaponSlot(body) == sync.slot && sync.slot != 11 && sync.slot != 12 && type != 43;
}

// False keeps this RPC at the head of the queue while its model streams.
static bool RunPedRpcOp(uint32_t id, void* ped, PedRpcOp& op, bool local, RemoteBody* stored) {
    using K = PedRpcOp::Kind;
    switch (op.kind) {
        case K::Animation: {
            if (!PedRunNamedAnimation(id, ped, op)) return false;
            // A non-root-motion synced animation owns the transform sent before it (ResetInterpolation).
            if (!op.updatePosition && stored) stored->interp.finishMs = 0;
            return true;
        }
        case K::KillAnimation:
            PedKillAnimation(ped);
            return true;
        case K::AnimationProgress:
        case K::AnimationSpeed:
            PedAnimationProgressOrSpeed(ped, op);
            return true;
        case K::GiveWeapon: {
            // CWeaponRPCs::GiveWeapon (26-111). Weapon 0 is the brass-knuckles workaround.
            if (op.weapon != 0) return PedGiveWeapon(ped, op.weapon, op.ammo, op.setCurrent, local, &op);
            unsigned char* unarmed = PedWeaponAt(ped, 0);
            const int unarmedType = WeaponType(unarmed);
            PedRemoveWeapon(ped, unarmedType, local);
            if (op.setCurrent || CurrentWeaponSlot(ped) == 0) {
                bool madeGolfClub = false;
                if (unarmedType == 1) {  // WEAPONTYPE_BRASSKNUCKLE
                    unsigned char* melee = PedWeaponAt(ped, 1);
                    if (WeaponType(melee) == 0) {
                        PedGiveWeapon(ped, 2, 100, false, local, nullptr);  // WEAPONTYPE_GOLFCLUB
                        madeGolfClub = true;
                    }
                    PedSetCurrentWeaponSlot(ped, 1, local);
                }
                PedSetCurrentWeaponSlot(ped, 0, local);
                if (madeGolfClub) PedWeaponRemove(ped, 1, local);
            }
            return true;
        }
        case K::TakeWeaponAmmo: {
            // CWeaponRPCs::TakeWeapon with ammo (128-166).
            unsigned char* w = PedWeaponOfType(ped, op.weapon);
            if (!w) return true;
            const int clip = WeaponClip(w) & 0xFF;
            WeaponClip(w) = 0;
            unsigned total = static_cast<unsigned>(WeaponTotal(w));
            total = op.ammo > total ? 0 : total - op.ammo;
            WeaponTotal(w) = static_cast<int>(total);
            WeaponClip(w) = WeaponTotal(w) > clip ? clip : WeaponTotal(w);
            return true;
        }
        case K::RemoveWeapon:
            PedRemoveWeapon(ped, op.weapon, local);
            return true;
        case K::TakeAllWeapons:
            // CClientPed::RemoveAllWeapons -> CPedSA::ClearWeapons (CPedSA.cpp 657-666). The local ResetAmmoInClip and
            // night / thermal vision reset are not ported.
            for (int slot = 0; slot < kWeaponSlots; ++slot) {
                unsigned char* w = PedWeaponAt(ped, slot);
                WeaponClip(w) = 0;
                WeaponTotal(w) = 0;
                PedWeaponRemove(ped, slot, local);
            }
            return true;
        case K::SetWeaponAmmo: {
            // CWeaponRPCs::SetWeaponAmmo (211-260).
            unsigned char* w = PedWeaponOfType(ped, op.weapon);
            if (!w) return true;
            const unsigned clip = static_cast<unsigned>(WeaponClip(w)) & 0xFF;
            WeaponClip(w) = static_cast<int>(op.ammoInClip);
            WeaponTotal(w) = static_cast<int>(op.ammo);
            if (!op.ammoInClip) WeaponClip(w) = static_cast<int>(op.ammo > clip ? clip : op.ammo);
            return true;
        }
        case K::SetWeaponSlot:
            PedSetCurrentWeaponSlot(ped, op.slot, local);
            return true;
    }
    return true;
}

static void RunPedRpcOps(uint32_t id, void* ped, bool local, RemoteBody* stored) {
    if (!ped) return;
    for (int budget = 0; budget < 8; ++budget) {
        PedRpcOp op;
        {
            std::lock_guard<std::mutex> lock(g_remoteMutex);
            auto queue = g_pedRpcOps.find(id);
            if (queue == g_pedRpcOps.end() || queue->second.empty()) return;
            op = queue->second.front();
            queue->second.pop_front(); // network arrival cannot evict the op currently executing
        }
        const bool stale = op.queryGeneration && !PedQueries::Current(id, op.queryGeneration);
        const bool done = stale || RunPedRpcOp(id, ped, op, local, stored);
        if (!done) {
            std::lock_guard<std::mutex> lock(g_remoteMutex);
            g_pedRpcOps[id].push_front(op);
            return;
        }
        if (!stale && op.queryGeneration) PedQueries::WeaponRpc(id, op);
    }
}

// Offscreen CClientPed weapon RPCs update the client inventory without a GTA
// weapon. Applying them later to an old native inventory changes replacement
// and shared-ammo semantics. Consume their cache branch before _CreateModel.
// Called on the game thread; all queue access remains under the registry lock.
static void ApplyUnstreamedWeaponRpcs(uint32_t id) {
    std::lock_guard lock(g_remoteMutex);
    auto found = g_pedRpcOps.find(id);
    if (found == g_pedRpcOps.end()) return;
    auto& queue = found->second;
    for (auto it = queue.begin(); it != queue.end();) {
        using K = PedRpcOp::Kind;
        if (it->kind >= K::GiveWeapon && it->kind <= K::SetWeaponSlot) {
            if (it->queryGeneration && PedQueries::Current(id, it->queryGeneration))
                PedQueries::WeaponRpc(id, *it);
            // Internal restoration commands have no receipt token; replacing a
            // body restores the client inventory again, rather than duplicating them.
            it = queue.erase(it);
        } else ++it;
    }
}

// CClientPed::UpdateAlphaAndVisibility (CClientPed.cpp 8522-8541): alpha 0 outside the current area, SetClumpAlpha,
// and the ped's bIsVisible follows it (the real-time shadow is returned first when hidden).
static void ApplyPedAlphaAndVisibility(void* ped, uint8_t alpha, uint8_t interior, int& applied) {
    auto& f = PedRpc();
    int effective = alpha;
    if (f.currArea && interior != static_cast<uint8_t>(*f.currArea)) effective = 0;
    if (effective == applied) return;
    // Opaque and never written: nothing to change on a fresh body.
    if (applied < 0 && effective == 255) {
        applied = effective;
        return;
    }
    auto* raw = static_cast<unsigned char*>(ped);
    void* clump = *reinterpret_cast<void**>(raw + 0x20);
    if (!clump || !f.setClumpAlpha) return;
    applied = effective;
    f.setClumpAlpha(clump, effective);
    auto& flags = *reinterpret_cast<uint32_t*>(raw + 0x28);
    if (effective == 0) {
        // ReleaseRealTimeShadow. ReturnRealTimeShadow writes shadow->owner+0x190 without a null check (crashes.md 9).
        void* shadow = *reinterpret_cast<void**>(raw + 0x190);
        if (shadow && f.returnShadow && f.shadowManager && *reinterpret_cast<void**>(shadow) == ped)
            f.returnShadow(f.shadowManager, shadow);
        flags &= ~(1u << 7);
    } else {
        flags |= 1u << 7;
    }
}

// CClientPed::SetUsesCollision (CClientPed.cpp 3621-3631): written once per change, the game edits the bit itself.
static void ApplyPedCollisions(void* ped, bool enabled, int& applied) {
    if (applied == (enabled ? 1 : 0)) return;
    const bool fresh = applied < 0;
    applied = enabled ? 1 : 0;
    if (fresh && enabled) return;  // a new body already uses collision
    auto& flags = *reinterpret_cast<uint32_t*>(static_cast<unsigned char*>(ped) + 0x28);
    if (enabled) flags |= 1u;
    else flags &= ~1u;
}

// The local player's own body (RPCs whose source is the local player).
static void ApplyLocalPedRpcs() {
    if (!g_findPlayer) return;
    const uint32_t local = ServerWorld::LocalPlayer();
    if (local == 0) return;
    void* ped = g_findPlayer(0);
    if (!ped) return;
    if (const int interior = g_pendingLocalInterior.exchange(-1); interior >= 0) {
        // CClientPed::SetInterior changes the camera area if it followed the ped.
        const auto previous = static_cast<const unsigned char*>(ped)[0x4B];
        static RemoteBody localArea;
        ApplyPedInterior(ped, localArea, static_cast<uint8_t>(interior));
        if (g_currentArea && *g_currentArea == previous) *g_currentArea = interior;
    }
    RunPedRpcOps(local, ped, true, nullptr);
    PendingLocalWasted localKill;
    {
        std::lock_guard<std::mutex> lock(g_remoteMutex);
        if (g_pendingLocalWasted.pending) {
            localKill = g_pendingLocalWasted;
            g_pendingLocalWasted.pending = false;
        }
    }
    if (localKill.pending && localKill.life == g_localLife.load()) {
        {
            std::lock_guard lock(g_localDeathMutex);
            g_deathLife = UINT64_MAX;
            g_localDeath = {};
        }
        auto* health = reinterpret_cast<float*>(static_cast<unsigned char*>(ped) + 0x6AC);
        if (*health > 0.f) {
            *health = 0.f;
            g_health = 0.f;
            RunDieTaskOnBody(local, ped, localKill.weapon, localKill.animGroup, localKill.animId);
            ENGINE_LOGI("DIAG Kill local id=%u weapon=%d anim=%d/%d", local, localKill.weapon, localKill.animGroup,
                        localKill.animId);
        }
    }
    static int alphaApplied = -1, collisionsApplied = -1;
    const auto* raw = static_cast<const unsigned char*>(ped);
    ApplyPedAlphaAndVisibility(ped, static_cast<uint8_t>(g_localAlpha.load()), raw[0x4B], alphaApplied);
    ApplyPedCollisions(ped, g_localCollisions.load() != 0, collisionsApplied);
}

static void ApplyRemotePeds() {
    NoteGameFrame();
    LogPoolUsage(g_remoteBodies.size());
    if (WorldRPG::Debug::Flag("noremote")) return;
    if (g_remoteResetPending.exchange(false)) {
        // The whole reset runs under g_remoteMutex: the network thread adds cars and seat requests of the new
        // session under the same mutex (NoteRemoteVehicle, NoteVehicleStartSync, NoteRemoteSeat). A record it added
        // before this tick is dropped here with the rest; cars and peds come back from the ServerWorld actor table
        // on this same tick (CopyActors below), which JNI nativeConnect cleared before the new session started.
        std::lock_guard<std::mutex> lock(g_remoteMutex);
        // Leave every seat while the car bodies still exist, then drop the requests.
        std::vector<uint32_t> seated;
        for (const auto& entry : g_seatedPeds) seated.push_back(entry.first);
        for (uint32_t pedId : seated) UnseatLocked(pedId);
        g_seatedPeds.clear();
        g_seatRequests.clear();
        for (auto& entry : g_remoteBodies) {
            ClearRemoteStick(entry.second.ped);
            DropBody(entry.second.ped, false);
        }
        g_remoteBodies.clear();
        // The local ped leaves its car before any car body is destroyed (DropCarLocked).
        for (auto& entry : g_remoteCars) DropCarLocked(entry.first, entry.second);
        g_remoteCars.clear();
        // The old session's objects leave GTA's world with it.
        WorldObjects::Reset();
        // A new session starts on foot: drop the stale vehicle, its packet 33 sample, any queued VEHICLE_INOUT
        // events and shots, and the hosted snapshots of the old session's element ids.
        g_localVehicleId = kNoVehicle;
        g_localCarValid = false;
        g_vehicleTouchBounds.clear();
        g_localVehicleEvents.clear();
        g_localShots.clear();
        g_remoteBullets.clear();
        g_localWeapon = {};
        g_localDrivebyRequest = 0;
        g_lastLocalShotAim = {};
        g_hostedSnap.clear();
        g_hostedCarSnap.clear();
        g_pedRpcOps.clear();
        g_localAlpha = 255;
        g_localCollisions = 1;
        ENGINE_LOGI("Remote world reset for reconnect");
    }
    {
        ServerWorld::ActorSpawn actors[kMaxRemotePedBodies + kMaxRemoteVehicleRecords];
        const int count = ServerWorld::CopyActors(actors, kMaxRemotePedBodies + kMaxRemoteVehicleRecords,
                                                   kMaxRemotePedBodies, kMaxRemoteVehicleRecords);
        std::lock_guard<std::mutex> lock(g_remoteMutex);
        for (int i = 0; i < count; ++i) {
            const auto& actor = actors[i];
            if (actor.kind == 14) {
                auto& ped = g_remotePeds[actor.id];
                const bool firstSeen = !ped.serverPed;
                ped.serverPed = true;
                if (firstSeen) ENGINE_LOGI("DIAG ped actor id=%u model=%d hosted=%d pos=%.1f,%.1f,%.1f", actor.id, actor.model, actor.hosted ? 1 : 0, actor.x, actor.y, actor.z);
                ped.hosted = actor.hosted;
                ped.interior = actor.interior;
                if (firstSeen) {
                    if (!ped.observerHasPosition) {
                        ped.x = actor.x;
                        ped.y = actor.y;
                        ped.z = actor.z;
                    }
                    if (!ped.observerHasHeading) ped.heading = actor.heading;
                }
            } else if (actor.kind == 2 && actor.model >= 400) {
                auto& car = g_remoteCars[actor.id];
                car.model = actor.model;
                car.hosted = actor.hosted;
                car.doorLockMode = actor.vehicleDoorLock;
                car.colors = actor.vehicleColors;
                car.nitroUpgrade = actor.nitroUpgrade;
                car.nitroRevision = actor.nitroRevision;
                // Desktop Packet_EntityAdd, vehicle case: SetRotationDegrees and SetHealth. Once per record; a
                // VEHICLE_STARTSYNC seed that arrived first is newer and is kept.
                if (!car.actorSeeded) {
                    car.actorSeeded = true;
                    if (!car.frozen) car.frozen = actor.vehicleFrozen;
                    if (!car.engineOn) car.engineOn = actor.vehicleEngineOn;
                    if (!car.alpha) car.alpha = actor.vehicleAlpha;
                    if (!car.seed.pending) {
                        constexpr float kRadToDeg = 180.f / 3.14159265f;
                        car.seed.hasRotation = true;
                        car.seed.rotX = actor.rotX;
                        car.seed.rotY = actor.rotY;
                        car.seed.rotZ = actor.heading * kRadToDeg;
                        car.seed.hasHealth = actor.health >= 0.f;
                        car.seed.health = actor.health;
                        car.seed.pending = true;
                    }
                }
                if (!car.dirty && !car.shown) {
                    car.x = actor.x;
                    car.y = actor.y;
                    car.z = actor.z;
                    car.heading = actor.heading;
                    car.dirty = true;
                }
            }
        }
        TaskFns& api = Tasks();
        for (uint32_t id : g_forget) {
            // This loop holds g_remoteMutex, so the locked variants are used. A seated ped leaves its seat, and a
            // forgotten car first unseats the peds inside it.
            UnseatLocked(id);
            UnseatPedsInCarLocked(id);
            g_seatRequests.erase(id);
            auto body = g_remoteBodies.find(id);
            if (body != g_remoteBodies.end()) {
                ClearRemoteStick(body->second.ped);
                DropBody(body->second.ped, false);
                g_remoteBodies.erase(body);
            }
            auto car = g_remoteCars.find(id);
            if (car != g_remoteCars.end()) {
                DropCarLocked(id, car->second);
                g_remoteCars.erase(car);
            }
            g_remotePeds.erase(id);
            // ENTITY_REMOVE is deferred to this game pulse. Keep weapon RPCs
            // already received for a new element reusing this ID; discard the
            // old generation and internal restoration commands with its body.
            auto queued = g_pedRpcOps.find(id);
            if (queued != g_pedRpcOps.end()) {
                auto& ops = queued->second;
                std::erase_if(ops, [id](const PedRpcOp& op) {
                    return !op.queryGeneration || !PedQueries::Current(id, op.queryGeneration);
                });
                if (ops.empty()) g_pedRpcOps.erase(queued);
            }
        }
        g_forget.clear();
        (void)api;
    }
    std::vector<uint32_t> unstreamed;
    {
        std::lock_guard lock(g_remoteMutex);
        for (const auto& [id, queue] : g_pedRpcOps)
            if (id != ServerWorld::LocalPlayer() && !g_remoteBodies.count(id)) unstreamed.push_back(id);
    }
    for (uint32_t id : unstreamed) ApplyUnstreamedWeaponRpcs(id);
    std::unordered_map<uint32_t, RemotePed> pending;
    {
        std::lock_guard<std::mutex> lock(g_remoteMutex);
        for (auto& [id, ped] : g_remotePeds) pending.emplace(id, ped);
    }
    for (auto& [id, ped] : pending) {
        // PLAYER_LIST omits the position of a seated player. Stream that player
        // at the vehicle while its GTA seat task is waiting for both bodies.
        {
            std::lock_guard lock(g_remoteMutex);
            auto req = g_seatRequests.find(id);
            if (req != g_seatRequests.end() && req->second.seated) {
                auto car = g_remoteCars.find(req->second.vehicleId);
                if (car != g_remoteCars.end()) {
                    ped.x = car->second.x;
                    ped.y = car->second.y;
                    ped.z = car->second.z;
                }
            }
        }
        if (!PlausiblePosition(ped.x, ped.y, ped.z)) continue;
        RemotePed* live = nullptr;
        {
            std::lock_guard lock(g_remoteMutex);
            auto found = g_remotePeds.find(id);
            if (found == g_remotePeds.end()) continue;
            live = &found->second; // Only this thread erases records.
        }
        if (id == ServerWorld::LocalPlayer()) continue;
        // PLAYER_SPAWN starts a fresh inventory even when the player was alive.
        // Discard the previous body's controls and weapons before restoring this life.
        auto previous = g_remoteBodies.find(id);
        if (previous != g_remoteBodies.end() && previous->second.networkPlayer &&
            !PedQueries::Current(id, previous->second.queryGeneration)) {
            UnseatApplied(id);
            ClearRemoteStick(previous->second.ped);
            DropBody(previous->second.ped, false);
            g_remoteBodies.erase(previous);
            std::lock_guard lock(g_remoteMutex);
            live->shown = false;
            if (auto queued = g_pedRpcOps.find(id); queued != g_pedRpcOps.end()) {
                // Restore commands belong to the discarded body. Network RPCs carry a life token.
                std::erase_if(queued->second, [playerId = id](const PedRpcOp& op) {
                    return op.kind >= PedRpcOp::Kind::GiveWeapon && op.kind <= PedRpcOp::Kind::SetWeaponSlot &&
                           (!op.queryGeneration || !PedQueries::Current(playerId, op.queryGeneration));
                });
            }
        }
        // CClientStreamer streams out a ped whose dimension is not the local player's (CClientGame::SetAllDimensions ->
        // the player streamer). The body is dropped, as on stream-out, and made again when the dimensions match.
        if (ServerWorld::DimensionOf(id) != ServerWorld::LocalDimension()) {
            // A seated ped keeps its body: the seat path owns it (no vehicle streaming here).
            auto out = g_remoteBodies.find(id);
            if (out == g_remoteBodies.end()) continue;
            if (!g_seatedPeds.count(id)) {
                ENGINE_LOGI("DIAG ped %u streamed out: dimension %u, local %u", id,
                    static_cast<unsigned>(ServerWorld::DimensionOf(id)), static_cast<unsigned>(ServerWorld::LocalDimension()));
                ClearRemoteStick(out->second.ped);
                SavePedInventory(id, out->second);
                DropBody(out->second.ped, false);
                g_remoteBodies.erase(out);
                live->shown = false;
                continue;
            }
        }
        int model = ServerWorld::ModelOf(id);
        // Model 0 is CJ. A body with private player data (desktop CPlayerPedSA ctor) is dressed from its own
        // CPedClothesDesc (DressRemoteCj). Without that support it stays skin 7, as before.
        if (model < 0 || model > 312 || (model == 0 && !CanAttachPrivatePlayerData())) model = 7;
        void* body = nullptr;
        auto found = g_remoteBodies.find(id);
        if (found == g_remoteBodies.end()) {
            static int spawnedFrame = -1;
            if (g_gameFrame == spawnedFrame) continue;
            if (!PedModelSettled(model)) {
                static int waits = 0;
                if (waits < 4) {
                    ++waits;
                    ENGINE_LOGI("Server ped %u waiting model %d state %d",
                        id, model, StreamingLoadState(model));
                }
                continue;
            }
            // The actor list is not distance-sorted. A full cap of far peds left
            // none around the local spawn (126,-114 while bodies sat at 757 and 2490).
            // Desktop streams by distance. Drop the farthest body when this ped is closer.
            if (g_remoteBodies.size() >= static_cast<size_t>(kMaxRemotePedBodies)) {
                const float lx = g_poseX.load();
                const float ly = g_poseY.load();
                auto dist2 = [&](float x, float y) {
                    const float dx = x - lx;
                    const float dy = y - ly;
                    return dx * dx + dy * dy;
                };
                const float mine = dist2(ped.x, ped.y);
                uint32_t farId = 0;
                float farD = -1.f;
                for (const auto& entry : g_remoteBodies) {
                    if (g_seatedPeds.count(entry.first)) continue;
                    auto pit = g_remotePeds.find(entry.first);
                    if (pit == g_remotePeds.end()) continue;
                    const float d = dist2(pit->second.x, pit->second.y);
                    if (d > farD) {
                        farD = d;
                        farId = entry.first;
                    }
                }
                if (farId == 0 || mine >= farD) continue;
                auto far = g_remoteBodies.find(farId);
                if (far == g_remoteBodies.end()) continue;
                ClearRemoteStick(far->second.ped);
                SavePedInventory(farId, far->second);
                DropBody(far->second.ped, false);
                g_remoteBodies.erase(far);
            }
            ApplyUnstreamedWeaponRpcs(id);
            spawnedFrame = g_gameFrame;
            // CClientPed::SetPosition (CClientPed.cpp 1064-1098) and Packet_EntityAdd
            // (CPacketHandler.cpp 4054) write the network position. No ground offset.
            const Vec3 pos{ped.x, ped.y, ped.z};
            body = CreateRemotePlayerPed(model, pos.x, pos.y, pos.z, ped.heading);
            if (!body) continue;
            // A new body starts undressed: the clothes are applied on the next frame (one rebuild per frame).
            live->clothesDirty = true;
            RemoteBody stored;
            stored.ped = body;
            stored.spatialGeneration = ServerWorld::ElementGeneration(id);
            stored.model = model;
            // ENTITY_ADD state first: the fence early-outs read frozen (CClientPed.cpp 5917).
            const bool restreamed = live->spawnApplied;
            ApplyPedSpawnRecord(id, *live);
            // _CreateModel restores the last body inventory before any RPCs waiting
            // for this body. Do not replay ENTITY_ADD after later weapon changes.
            PedQueries::Snapshot restore;
            if (PedQueries::CopyRestore(id, restore)) {
                stored.queryGeneration = restore.generation;
                stored.networkPlayer = restore.player;
                std::deque<PedRpcOp> base;
                for (const auto& w : restore.weapons) if (w.type) {
                    PedRpcOp give;
                    give.kind = PedRpcOp::Kind::GiveWeapon;
                    give.weapon = w.type;
                    give.ammo = w.total;
                    base.push_back(give);
                }
                PedRpcOp slot;
                slot.kind = PedRpcOp::Kind::SetWeaponSlot;
                slot.slot = restore.currentSlot;
                base.push_back(slot);
                std::lock_guard lock(g_remoteMutex);
                auto& queue = g_pedRpcOps[id];
                queue.insert(queue.begin(), base.begin(), base.end());
            }

            // _CreateModel -> RunAnimationFromCache: a body made again replays the element's last animation.
            // The cached start time / progress is not restored (the animation starts from the beginning).
            if (restreamed) {
                std::lock_guard<std::mutex> lock(g_remoteMutex);
                if (live->hasAnimCache) {
                    PedRpcOp replay = live->animCache;
                    replay.waits = 0;
                    replay.requested = false;
                    g_pedRpcOps[id].push_front(replay);
                }
            }
            ArmPedCollisionFence(body, *live, stored);
            // A frozen ped skips the fence, so its freeze bit is written here (_CreateModel -> ApplyPhysicalFreezeState).
            if (!stored.collisionFence) ApplyPedFreezeState(body, stored, *live);
            {
                static int spawnLists = 0;
                if (spawnLists < 8) {
                    ++spawnLists;
                    ENGINE_LOGI("DIAG ped spawn id=%u hosted=%d fence=%d static=%d staticWaiting=%d movingList=%d",
                        id, ped.hosted ? 1 : 0, stored.collisionFence ? 1 : 0, IsEntityStatic(body) ? 1 : 0,
                        IsStaticWaitingForCollision(body) ? 1 : 0, OnMovingList(body) ? 1 : 0);
                }
            }
            // Packet_EntityAdd calls SetInterior after the ped exists (CPacketHandler.cpp 4412,
            // CClientPed::SetInterior 1101-1119).
            ApplyPedInterior(body, stored, ped.interior);
            g_remoteBodies.emplace(id, std::move(stored));
            {
                std::lock_guard lock(g_remoteMutex);
                live->shownX = pos.x;
                live->shownY = pos.y;
                live->shownZ = pos.z;
                live->shownH = ped.heading;
                live->shown = true;
            }
            ApplySyncedPlayerFields(id, body);
            // A ped that is dead on the network and gets a new body (model change, far-drop at the cap) is created dead
            // again; otherwise it stands up and the dead branch below leaves it frozen with no sync.
            if (live->dead)
                KillRemoteBody(id, body, live->killWeapon, live->killAnimGroup, live->killAnimId);
            DebugPedPose("H1", "spawn", id, body, ped, true);
            static int created = 0;
            if (created < 6) {
                ++created;
                ENGINE_LOGI("Server ped %u model %d at %.0f %.0f %.0f", id, model, pos.x, pos.y, pos.z);
            }
            continue;
        }
        body = found->second.ped;
        if (!body) continue;
        // CClientPed::SetModel -> _ChangeModel (CClientPed.cpp 2939, 6397-6525) replaces the body when the model changes,
        // for example when PLAYER_LIST reports skin 0 after the body was made with the stand-in. Here the body is
        // dropped and the spawn branch makes a new one (one per frame). A seated ped keeps its body.
        // Only a known model replaces the body: ModelOf is -1 for a moment after ENTITY_REMOVE erased the record, and
        // the stand-in rule turned that into 7, so removed server peds were rebuilt as skin 7 before being dropped.
        if (ServerWorld::ModelOf(id) >= 0 && found->second.model != model && !g_seatedPeds.count(id)) {
            static int changedFrame = -1;
            if (changedFrame != g_gameFrame) {
                changedFrame = g_gameFrame;
                ENGINE_LOGI("DIAG ped %u model %d -> %d: body replaced", id, found->second.model, model);
                ClearRemoteStick(body);
                SavePedInventory(id, found->second);
                DropBody(body, false);
                g_remoteBodies.erase(found);
                live->shown = false;
                continue;
            }
        }
        // ProcessRebuildPlayer: at most one rebuild per frame (CClientPed.cpp 6609-6626).
        if (model == 0 && live->clothesDirty && HasPrivatePlayerData(body)) {
            static int rebuiltFrame = -1;
            if (rebuiltFrame != g_gameFrame) {
                rebuiltFrame = g_gameFrame;
                std::vector<WorldRPG::Entity::PedClothes> clothes;
                {
                    std::lock_guard<std::mutex> lock(g_remoteMutex);
                    clothes = live->clothes;
                }
                if (DressRemoteCj(body, clothes)) {
                    std::lock_guard<std::mutex> lock(g_remoteMutex);
                    live->clothesDirty = false;
                }
            }
        }
        ApplyPedInterior(body, found->second, live->interior);
        // SET_ELEMENT_FROZEN -> CClientPed::SetFrozen -> ApplyPhysicalFreezeState (CClientPed.cpp 3904-3944).
        // Desktop SetFrozen(true) also removes the primary and event-response tasks; that step is not run (DIAG).
        if (found->second.frozenApplied != (live->frozen ? 1 : 0)) {
            found->second.frozenApplied = live->frozen ? 1 : 0;
            ApplyPedFreezeState(body, found->second, *live);
        }
        // SET_ELEMENT_ALPHA / SET_ELEMENT_INTERIOR (UpdateAlphaAndVisibility), SET_ELEMENT_COLLISIONS_ENABLED, then the
        // queued animation and weapon RPCs.
        {
            uint8_t alpha = 255;
            bool collisions = true;
            {
                std::lock_guard<std::mutex> lock(g_remoteMutex);
                alpha = live->alpha;
                collisions = live->collisions;
            }
            ApplyPedAlphaAndVisibility(body, alpha, live->interior, found->second.alphaApplied);
            ApplyPedCollisions(body, collisions, found->second.collisionsApplied);
            RunPedRpcOps(id, body, false, &found->second);
        }
        if (live->killPending) {
            int weapon = 0, group = 0, anim = 0;
            {
                std::lock_guard<std::mutex> lock(g_remoteMutex);
                live->killPending = false;
                weapon = live->killWeapon;
                group = live->killAnimGroup;
                anim = live->killAnimId;
            }
            if (!live->dead) {
                live->dead = true;
                KillRemoteBody(id, body, weapon, group, anim);
            }
        }
        // A dead body is left to its death task: no position, rotation or stick from the phone.
        if (live->dead) {
            ClearRemoteStick(body);
            ApplySyncedPlayerFields(id, body);
            continue;
        }
        // SET_ELEMENT_POSITION: SetPosition, interpolation reset (CElementRPCs.cpp 122-170). SET_PED_ROTATION:
        // SetCurrentRotation (CPedRPCs.cpp 62-90).
        {
            bool teleport = false, headingSnap = false;
            Vec3 position{};
            float heading = 0;
            {
                std::lock_guard<std::mutex> lock(g_remoteMutex);
                teleport = live->teleport;
                headingSnap = live->headingSnap;
                position = live->teleportPosition;
                heading = live->snapHeading;
                live->teleport = false;
                live->headingSnap = false;
            }
            if (teleport) {
                PlaceRemotePlayerPed(body, position.x, position.y, position.z,
                    headingSnap ? heading : live->shownH);
                found->second.interp.finishMs = 0;
                std::lock_guard lock(g_remoteMutex);
                live->shownX = position.x;
                live->shownY = position.y;
                live->shownZ = position.z;
                live->shown = true;
            }
            if (headingSnap) {
                found->second.rotationInterp = {};
                SetRemotePedHeading(body, heading);
                std::lock_guard lock(g_remoteMutex);
                live->shownH = heading;
            }
        }
        // A ped in a seat is placed by the seat task. The stream-in fence returns
        // when the ped occupies a vehicle (CClientPed.cpp 5918).
        if (g_seatedPeds.count(id)) {
            ApplySyncedPlayerFields(id, body);
            if (found->second.collisionFence) {
                SetStaticWaitingForCollision(body, found->second.fencePrevStaticWaiting);
                found->second.collisionFence = false;
                ApplyPedFreezeState(body, found->second, *live);
            }
        } else if (HoldPedCollisionFence(body, found->second, *live)) {
            ApplySyncedPlayerFields(id, body);
            DebugPedPose("H5", "fence", id, body, *live, true);
            continue;
        }
        // StreamedInPulse -> UpdateRemoteReplicaPhysicsFence (CClientPed.cpp 7140-7152), after the stream-in fence.
        UpdateRemoteReplicaPhysicsFence(body, found->second, *live, g_seatedPeds.count(id) != 0);
        const bool weaponReady = ApplyRemotePlayerWeapon(body, found->second, ped);
        const auto nowAim = GameNowMs();
        auto& presentation = found->second;
        if (presentation.aimSlot != ped.syncWeapon.slot) {
            presentation.aimInterpolation = {};
            presentation.aimSlot = ped.syncWeapon.slot;
        }
        if (presentation.aimRevision != ped.aimRevision) {
            presentation.aimInterpolation.update(ped.syncWeapon.aim, nowAim);
            presentation.aimRevision = ped.aimRevision;
        }
        const bool showAim = ped.gangDriveby || PlayerWeaponSync::HasAimOrFire(ped.buttons);
        auto aim = weaponReady && showAim ? presentation.aimInterpolation.sample(nowAim) : PlayerWeaponSync::Aim{};
        if (aim.full && weaponReady) {
            const int type = WeaponType(PedWeaponAt(body, ped.syncWeapon.slot));
            aim.akimbo = PlayerWeaponSync::IsAkimbo(type);
            aim.range = NativeWeaponRange(type);
        }
        if (!ped.hosted) StopLostTrafficTask(found->second);
        // Native seats own the transform. Publish the occupant's live pose for
        // Lua queries without running the on-foot interpolator over it.
        if (auto seated = g_seatedPeds.find(id); seated != g_seatedPeds.end()) {
            const bool driveby = ped.gangDriveby && weaponReady && PlayerWeaponSync::SlotHasAmmo(ped.syncWeapon.slot) &&
                                 RemoteControlHookActive();
            if (driveby) {
                aim.inVehicle = true;
                SetRemoteStick(body, 0, 0, ped.fire, ped.cameraRotation, true, ped.buttons, aim);
            } else {
                ClearRemoteStick(body);
            }
            void* vehicle = nullptr;
            {
                std::lock_guard lock(g_remoteMutex);
                auto car = g_remoteCars.find(seated->second.vehicleId);
                if (car != g_remoteCars.end()) vehicle = car->second.body;
            }
            SetNativeDriveby(body, vehicle, seated->second.seat, driveby);
            float x = 0, y = 0, z = 0, heading = 0;
            if (ReadBodyPos(body, x, y, z, heading)) {
                std::lock_guard lock(g_remoteMutex);
                live->shownX = x;
                live->shownY = y;
                live->shownZ = z;
                live->shownH = heading;
                live->shown = true;
            }
            continue;
        }
        const bool controlled = !ped.serverPed && !ped.hosted && ped.keysync && RemoteControlHookActive();
        if (controlled) {
            // Plan step A: the stick drives CPlayerPed::ProcessControl. The per-tick
            // matrix lerp below would fight it, so only re-anchor when the body
            // drifts more than 8 m from the server position.
            const auto buttons = weaponReady ? ped.buttons :
                static_cast<uint8_t>(ped.buttons & ~(PlayerWeaponSync::kAim | PlayerWeaponSync::kFire));
            SetRemoteStick(body, ped.stickX, ped.stickY, weaponReady && ped.fire,
                           ped.cameraRotation, true, buttons, PlayerWeaponSync::HasAimOrFire(buttons) ? aim : PlayerWeaponSync::Aim{});
            // ReadPlayerPuresync: SetTargetPosition(position, TICK_RATE) then SetTargetRotation(rotation)
            // (CNetAPI.cpp 998-999); Interpolate -> UpdateTargetPosition every frame. Replaces the old rule that
            // only re-placed the body past 8 m, which let the stick physics drift up to 8 m from the real position.
            bool fresh = false;
            {
                std::lock_guard<std::mutex> lock(g_remoteMutex);
                fresh = live->newTarget;
                live->newTarget = false;
            }
            if (fresh) {
                SetTargetPosition(body, found->second, ped.x, ped.y, ped.z, kPureSyncTickMs);
                // CClientPed::SetTargetRotation(float) writes the rotation at once (CClientPed.cpp 5332-5339).
                SetRemotePedHeading(body, ped.heading);
            }
            UpdateTargetPosition(body, found->second);
            live->shown = true;
            // The pad hook only drives the stick. libGTASA reads fire from the touch widget, not from the pad
            // (PlayerPedPort.cpp, RemotePlayerProcessControl), so the punch for a remote fire key comes from
            // PresentOnFoot, as it does without the hook.
            // With the virtual touch the attack comes from ButtonCircle through GTA's own melee code (combos,
            // weapon swings), as on desktop. The scripted CTaskSimpleFight punch is only the fallback.
            PresentOnFoot(body, found->second, ped, weaponReady && ped.syncWeapon.slot <= 1 && !RemoteTouchActive());
            ApplySyncedPlayerFields(id, body);
            DebugPedPose("H4", "stick", id, body, ped, false);
            continue;
        }
        if (!ped.hosted && ped.serverPed) {
            // A 0x30 viewer receives velocity instead of the newer locomotion block.
            // Project onto a zero-heading virtual camera, like CaptureNativeTaskLocomotion.
            // The replica physics fence leaves world movement to packet interpolation.
            const float speed = std::hypot(ped.vx, ped.vy);
            const bool walking = nowAim < ped.observerMotionUntilMs && !ped.frozen && !ped.duck &&
                !ped.inWater && !ped.hasAnimCache && !ped.physicalTakeoverPending &&
                ped.nativeTaskAnimId == 0 && std::isfinite(speed) && speed > 0.009f;
            TraceObserverGait(id, body, speed, walking, ped.observerMotionUntilMs, nowAim);
            if (walking) {
                const int magnitude = speed > 0.27f ? 128 : speed > 0.14f ? 108 : 59;
                SetRemoteStick(body, std::lround(magnitude * ped.vx / speed),
                    std::lround(-magnitude * ped.vy / speed), false, 0.f, true, speed > 0.27f ? 0x08 : 0);
            } else {
                ClearRemoteStick(body);
            }
        } else if (!ped.hosted || !ped.ownedControls) {
            ClearRemoteStick(body);
        }
        const float dx = ped.x - live->shownX;
        const float dy = ped.y - live->shownY;
        const float dz = ped.z - live->shownZ;
        const float dist2 = dx * dx + dy * dy + dz * dz;
        if (ped.hosted) {
            if (ped.ownedControls) {
                const int magnitude = (ped.ownedControls & 0x10) ? 59 : 108;
                const int x = ((ped.ownedControls & 0x08) != 0) - ((ped.ownedControls & 0x04) != 0);
                const int y = ((ped.ownedControls & 0x02) != 0) - ((ped.ownedControls & 0x01) != 0);
                const uint8_t buttons = (ped.ownedControls & 0x20) ? 0x08 : 0;
                SetRemoteStick(body, x * magnitude, y * magnitude, false, -live->shownH, true, buttons);
            }
            ApplySyncedPlayerFields(id, body);
            found->second.interp.finishMs = 0;
            found->second.rotationInterp = {};
            float hx = 0, hy = 0, hz = 0, hh = live->shownH;
            if (ReadBodyPos(body, hx, hy, hz, hh)) {
                auto* speed = reinterpret_cast<float*>(static_cast<unsigned char*>(body) + 0x68);

                std::lock_guard<std::mutex> lock(g_remoteMutex);
                auto snap = g_remotePeds.find(id);
                if (snap != g_remotePeds.end() && snap->second.hosted && !snap->second.teleport) {
                    snap->second.x = hx;
                    snap->second.y = hy;
                    snap->second.z = hz;
                    snap->second.heading = hh;
                    snap->second.vx = speed[0];
                    snap->second.vy = speed[1];
                    snap->second.vz = speed[2];
                    snap->second.shownX = hx;
                    snap->second.shownY = hy;
                    snap->second.shownZ = hz;
                    snap->second.shownH = hh;
                }
            }
            DebugPedPose("H2", "hosted", id, body, ped, false);
            continue;
        }
        if (ped.serverPed) {
            // CPedSync::Packet_PedSync (428-437) uses the same position interpolation
            // as remote players. Rotation uses the delayed CClientPed overload.
            uint64_t positionInterval = 0, headingInterval = 0;
            float x = 0, y = 0, z = 0, heading = 0;
            {
                std::lock_guard lock(g_remoteMutex);
                positionInterval = std::exchange(live->observerPositionInterval, 0);
                headingInterval = std::exchange(live->observerHeadingInterval, 0);
                x = live->x;
                y = live->y;
                z = live->z;
                heading = live->heading;
            }
            if (positionInterval) SetTargetPosition(body, found->second, x, y, z, positionInterval);
            UpdateTargetPosition(body, found->second);
            const auto now = static_cast<uint64_t>(GameNowMs());
            auto& rotation = found->second.rotationInterp;
            float renderedHeading = live->shownH;
            if (headingInterval) {
                Interp::StepRotation(rotation, renderedHeading, now);
                Interp::BeginRotation(rotation, renderedHeading, heading, headingInterval, now);
            }
            if (Interp::StepRotation(rotation, renderedHeading, now))
                SetRemotePedHeading(body, renderedHeading);
            float ignoredHeading = 0;
            if (ReadBodyPos(body, x, y, z, ignoredHeading)) {
                std::lock_guard lock(g_remoteMutex);
                live->shownX = x;
                live->shownY = y;
                live->shownZ = z;
                live->shownH = renderedHeading;
            }
        } else if (!live->shown || dist2 > 20.f * 20.f) {
            live->shownX = ped.x;
            live->shownY = ped.y;
            live->shownZ = ped.z;
            live->shownH = ped.heading;
            live->shown = true;
            PlaceRemotePlayerPed(body, ped.x, ped.y, ped.z, ped.heading);
            ApplySyncedPlayerFields(id, body);
            DebugPedPose("H1", "snap", id, body, ped, false);
        } else {
            // CClientPed::SetTargetPosition / UpdateTargetPosition (CClientPed.cpp 8657-8822)
            // spread the error across one sync interval. The player ped has no m_interp
            // fields, and CClientPed::Interpolate is not called from a ped task, so this
            // step stays the existing 0.35 position blend. A second algorithm is not added.
            static bool loggedInterp = false;
            if (!loggedInterp) {
                loggedInterp = true;
                ENGINE_LOGI("DIAG player position: CClientPed::UpdateTargetPosition "
                            "(CClientPed.cpp 8765-8822) is not wired; keeping the 0.35 position step. "
                            "CPed::SetTargetRotation is SetRemotePedHeading (+0x6C4/+0x6C8).");
            }
            live->shownX += dx * 0.35f;
            live->shownY += dy * 0.35f;
            live->shownZ += dz * 0.35f;
            if (ped.keysync) {
                // CClientPed::SetTargetRotation(float) (CClientPed.cpp 5332-5339) writes
                // the heading at once. ReadPlayerPuresync and ReadKeysync use that overload.
                live->shownH = ped.heading;
            } else {
                float turn = ped.heading - live->shownH;
                constexpr float pi = 3.14159265f;
                while (turn > pi) turn -= 2.f * pi;
                while (turn < -pi) turn += 2.f * pi;
                live->shownH += turn * 0.35f;
            }
            auto* raw = static_cast<unsigned char*>(body);
            auto* matrix = *reinterpret_cast<unsigned char**>(raw + 0x18);
            const auto matrixAddr = reinterpret_cast<uintptr_t>(matrix);
            if (matrix && (matrixAddr & 3u) == 0 && matrixAddr > 0x10000u) {
                *reinterpret_cast<float*>(matrix + 0x30) = live->shownX;
                *reinterpret_cast<float*>(matrix + 0x34) = live->shownY;
                *reinterpret_cast<float*>(matrix + 0x38) = live->shownZ;
            }
            *reinterpret_cast<float*>(raw + 0x08) = live->shownX;
            *reinterpret_cast<float*>(raw + 0x0C) = live->shownY;
            *reinterpret_cast<float*>(raw + 0x10) = live->shownZ;
            DebugPedPose("H3", "lerp", id, body, ped, false);
        }
        // CPlaceable::SetHeading is not exported on libGTASA 2.10. Yaw is
        // CMatrix::SetRotateZOnly plus fCurrentRotation / fTargetRotation.
        if (!ped.serverPed) SetRemotePedHeading(body, live->shownH);
        // The snap branch already consumed the one-shot flags. The lerp branch has not.
        ApplySyncedPlayerFields(id, body);
        if (!ped.serverPed && std::fabs(ped.vx) + std::fabs(ped.vy) + std::fabs(ped.vz) > 0.001f) {
            static SetMoveSpeedFn setMove = nullptr;
            static bool looked = false;
            if (!looked) {
                looked = true;
                void* handle = dlopen("libGTASA.so", RTLD_NOLOAD | RTLD_GLOBAL);
                if (handle) {
                    setMove = reinterpret_cast<SetMoveSpeedFn>(dlsym(handle, "_ZN10CPhysical12SetMoveSpeedERK7CVector"));
                    if (!setMove)
                        setMove = reinterpret_cast<SetMoveSpeedFn>(dlsym(handle, "_ZN10CPhysical12SetMoveSpeedE7CVector"));
                }
            }
            if (setMove) {
                Vec3 speed{ped.vx, ped.vy, ped.vz};
                setMove(body, speed);
            }
        }

        // Native task presentation — matches mta_neon CClientPed::UpdateNativeTaskAnimationPresentation
        // Apply locomotion mode, weapon type, animation, onfire/inwater, and animation kill
        // to the remote ped body on the game thread after spatial sync.
        if (ped.animationKilled && found->second.ped) {
            // ucFlags & 0x80: clear synced anim state so the ped is no longer
            // permanently excluded from native-task locomotion presentation.
            TaskFns& api = Tasks();
            void* intel = PedIntel(found->second.ped);
            if (intel && api.clearTasks) api.clearTasks(intel, true, true);
            live->animationKilled = false;
        }
        // ped.fire is the attack button from NoteRemotePlayerSync / NoteRemoteAim, not
        // CPed::IsOnFire. Copying it into ducked made an attacking ped duck.
        // The on-fire bit is PED_SYNC ucFlags 0x20, stored by PedQueries::OnFire.
        if (ped.inWater) {
            // IsInWater state — tracked for future native call integration.
            // No immediate GTA native available without further dlsym work.
        }
        if (ped.nativeTaskWeaponType != 0 && !ped.hosted) {
            // SNativeTaskWeaponPresentationSync weapon type — future integration
            // point for creating/destroying weapon fire tasks on remote peds.
        }
        if (ped.nativeTaskAnimGroup != 0 || ped.nativeTaskAnimId != 0) {
            // SNativeTaskAnimationPresentationSync — future integration point
            // for playing/blending animations on remote ped clumps via
            // RpAnimBlendClumpGetAssociation equivalent on Android.
        }
        if (ped.physicalTakeoverPending && found->second.ped) {
            // Physical takeover from PED_START_SYNC seeds airborne/climb anchor.
            // Matches mta_neon CClientPed::SetNativeTaskPhysicalTakeoverState.
            // Full implementation requires CTaskSimpleClimb equivalent on Android.
            live->physicalTakeoverPending = false;
        }

        RemotePed animPed = ped;
        if (ped.serverPed && (nowAim >= ped.observerMotionUntilMs || ped.frozen || ped.duck || ped.inWater))
            animPed.vx = animPed.vy = 0.f;
        if (!ped.serverPed && std::fabs(animPed.vx) + std::fabs(animPed.vy) < 0.02f) {
            const float step = std::sqrt(dx * dx + dy * dy);
            if (step > 0.02f && step < 8.f) {
                animPed.vx = dx * 0.35f;
                animPed.vy = dy * 0.35f;
            }
        }
        if (!weaponReady || ped.syncWeapon.slot > 1) animPed.fire = false;
        if (!ped.hosted) PresentOnFoot(body, found->second, animPed);
    }
    std::vector<HostedPed> hosted;
    {
        std::lock_guard<std::mutex> lock(g_remoteMutex);
        for (auto& [id, ped] : g_remotePeds) {
            if (!ped.hosted) continue;
            HostedPed sample;
            sample.id = id;
            sample.x = ped.shown ? ped.shownX : ped.x;
            sample.y = ped.shown ? ped.shownY : ped.y;
            sample.z = ped.shown ? ped.shownZ : ped.z;
            sample.heading = ped.shown ? ped.shownH : ped.heading;
            sample.vx = ped.vx;
            sample.vy = ped.vy;
            sample.vz = ped.vz;
            sample.health = ped.health;
            // WritePedInformation reads the live ped: GetPosition, GetMoveSpeed, GetHealth, GetArmor,
            // and GetCurrentRotation (CPedSA.h 433). That float is CPed +0x6C4 on libGTASA 2.10
            // (CPed::UpdatePosition 0x597b3c). Health and armour stay +0x6AC and +0x6B4.
            auto bodyIt = g_remoteBodies.find(id);
            if (bodyIt == g_remoteBodies.end() || !bodyIt->second.ped) continue;
            void* body = bodyIt->second.ped;
            if (body) {
                float hx = 0, hy = 0, hz = 0, hh = sample.heading;
                if (ReadBodyPos(body, hx, hy, hz, hh)) {
                    sample.x = hx;
                    sample.y = hy;
                    sample.z = hz;
                    sample.heading = hh;
                    const float current = *reinterpret_cast<const float*>(static_cast<unsigned char*>(body) + 0x6C4);
                    if (std::isfinite(current)) sample.heading = current;
                    auto* speed = reinterpret_cast<float*>(static_cast<unsigned char*>(body) + 0x68);
                    sample.vx = speed[0];
                    sample.vy = speed[1];
                    sample.vz = speed[2];
                }
                auto* raw = static_cast<unsigned char*>(body);
                const float health = *reinterpret_cast<float*>(raw + 0x6AC);
                const float armor = *reinterpret_cast<float*>(raw + 0x6B4);
                if (std::isfinite(health) && std::isfinite(armor)) {
                    sample.health = health;
                    sample.armor = armor;
                    sample.bodyVitals = true;
                }
            }
            hosted.push_back(sample);
        }
        g_hostedSnap.swap(hosted);
    }
    TaskFns& api = Tasks();
    struct CarPose {
        uint32_t id;
        RemoteCar* record;
        int model;
        float x, y, z, heading;
        bool hosted;
        bool useUnoccupiedSync;
    };
    std::vector<CarPose> cars;
    {
        std::lock_guard<std::mutex> lock(g_remoteMutex);
        cars.reserve(g_remoteCars.size());
        // Only the game thread erases vehicle records. References survive insertions and rehashes
        // by the packet thread, so use these pointers without looking up the map again unlocked.
        for (auto& [id, car] : g_remoteCars)
            cars.push_back({id, &car, car.model, car.x, car.y, car.z, car.heading, car.hosted, car.useUnoccupiedSync});
    }
    // The car the local ped is in. Its body is moved by the local driver, not by server poses.
    void* localVehicle = api.findPlayerVehicle ? api.findPlayerVehicle(-1, false) : nullptr;
    // Only the driver integrates the car. A passenger follows the network driver.
    if (localVehicle && NativeVehicleSeat(localVehicle, g_sampledLocalPed) != 0) localVehicle = nullptr;
    const float localX = g_poseX.load();
    const float localY = g_poseY.load();
    const auto distance = [&](const CarPose& car) { return std::hypot(car.x - localX, car.y - localY); };
    std::stable_sort(cars.begin(), cars.end(), [&](const CarPose& a, const CarPose& b) {
        if (a.hosted != b.hosted) return a.hosted;
        return distance(a) < distance(b);
    });
    auto bodies = std::count_if(cars.begin(), cars.end(), [](const auto& car) { return car.record->body != nullptr; });
    for (const auto& car : cars) {
        const uint32_t id = car.id;
        if (!PlausiblePosition(car.x, car.y, car.z) || car.model < 400 || car.model > 611) continue;
        RemoteCar& record = *car.record;
        void* body = record.body;
        if (!body) {
            if (bodies >= kMaxRemoteVehicleRecords || !EnsureWorldModel(car.model)) continue;
            if (api.createCarForScript) {
                // Desktop CPoolsSA::AddVehicle: CCarCtrl::CreateCarForScript(model, CVector(), 0) (pool-owned, added to the world).
                body = api.createCarForScript(car.model, Vec3{car.x, car.y, car.z}, 0);
                if (!body) continue;
            } else {
                if (!api.carCtor || !g_worldAdd) continue;
                record.owned = std::make_unique<std::byte[]>(0x2000);
                body = record.owned.get();
                std::memset(body, 0, 0x2000);
                api.carCtor(body, car.model, 2, 0);
                g_worldAdd(body);
            }
            ++bodies;
            const uint64_t generation = ServerWorld::ElementGeneration(id);
            {
                std::lock_guard lock(g_remoteMutex);
                record.body = body;
                g_vehicleBodyIds[body] = id;
                record.spatialGeneration = generation;
                record.shownX = car.x;
                record.shownY = car.y;
                record.shownZ = car.z;
                record.shownH = car.heading;
                record.shown = true;
            }
            record.appliedDoorLockMode = -1;
            record.appliedNitroUpgrade = 0;
            record.appliedNitroRevision = 0;
            ApplyCarDoorLock(record);
            ApplyCarNitro(id, record, api);
            WriteBodyPos(body, car.x, car.y, car.z);
            if (g_setHeading) g_setHeading(body, car.heading);
            // Server rotation and health from ENTITY_ADD or VEHICLE_STARTSYNC. A new body starts upright at the
            // constructor health of 1000, which packets 56 and 33 would otherwise report back to the server.
            ApplyCarSeed(id, body, false);
            ApplyVehiclePresentation(id, record, body);
            if (!car.hosted && car.useUnoccupiedSync) ApplyUnoccupiedVehicleSync(id, car.model, record, body);
            static int created = 0;
            if (created < 6) {
                ++created;
                ENGINE_LOGI("Server vehicle %u model %d at %.0f %.0f %.0f", id, car.model, car.x, car.y, car.z);
            }
            continue;
        }
        ApplyCarDoorLock(record);
        ApplyCarNitro(id, record, api);
        ApplyCarSeed(id, body, body == localVehicle);
        ApplyVehiclePresentation(id, record, body);
        if (body == localVehicle) {
            // The local ped is in this car. The desktop applies remote vehicle state (CNetAPI::ReadVehiclePuresync,
            // Packet_UnoccupiedVehicleSync) only to vehicles other players sync; the local player's own vehicle is
            // driven by the game. Once the server seats the ped it sends VEHICLE_STOPSYNC, so the car is not hosted
            // and the pose lerp below would write the last server pose into the body on every frame. The record
            // follows the live body instead, so the lerp starts from the right place when the ped leaves.
            float lx = 0, ly = 0, lz = 0, lh = 0;
            if (ReadBodyPos(body, lx, ly, lz, lh)) {
                HostedCar pose;
                const bool sampled = SampleHostedCar(body, pose);
                std::lock_guard lock(g_remoteMutex);
                record.x = record.shownX = lx;
                record.y = record.shownY = ly;
                record.z = record.shownZ = lz;
                // Same heading convention as the server's rz (CarPose heading is rz in radians).
                if (sampled) record.heading = record.shownH = pose.rotZ * 3.14159265f / 180.f;
                record.motionPending = false;
                record.observedVehicle.reset();
            }
            record.unoccupiedPosition = {};
            record.unoccupiedRotation = {};
            continue;
        }
        if (car.hosted) {
            record.unoccupiedPosition = {};
            record.unoccupiedRotation = {};
            continue;
        }
        if (car.useUnoccupiedSync) {
            ApplyUnoccupiedVehicleSync(id, car.model, record, body);
            continue;
        }
        record.unoccupiedPosition = {};
        record.unoccupiedRotation = {};
        ApplyCarMotion(id, body);
        Vec3 position;
        {
            std::lock_guard lock(g_remoteMutex);
            const float dx = car.x - record.shownX;
            const float dy = car.y - record.shownY;
            const float dz = car.z - record.shownZ;
            if (dx * dx + dy * dy + dz * dz > 30.f * 30.f) {
                record.shownX = car.x;
                record.shownY = car.y;
                record.shownZ = car.z;
            } else {
                record.shownX += dx * 0.35f;
                record.shownY += dy * 0.35f;
                record.shownZ += dz * 0.35f;
            }
            record.shownH = car.heading;
            position = {record.shownX, record.shownY, record.shownZ};
        }
        WriteBodyPos(body, position.x, position.y, position.z);
        if (g_setHeading) g_setHeading(body, car.heading);
    }

    // Hosted vehicles for packet 56 (SendUnoccupiedVehicleSync). Sampled here on the game thread,
    // then swapped in as one snapshot, the same way g_hostedSnap is built above.
    std::vector<std::pair<uint32_t, uint64_t>> streamed;
    streamed.reserve(g_remoteBodies.size() + cars.size());
    for (auto& [id, remote] : g_remoteBodies) {
        if (!remote.ped || IsPlaceableVtable(remote.ped)) continue;
        if (!remote.spatialGeneration) remote.spatialGeneration = ServerWorld::ElementGeneration(id);
        streamed.emplace_back(id, remote.spatialGeneration);
    }
    std::vector<HostedCar> hostedCars;
    for (const auto& car : cars) {
        void* body = car.record->body;
        HostedCar sample;
        if (!body || IsPlaceableVtable(body)) continue;
        if (!car.record->spatialGeneration) car.record->spatialGeneration = ServerWorld::ElementGeneration(car.id);
        streamed.emplace_back(car.id, car.record->spatialGeneration);
        if (!SampleHostedCar(body, sample)) continue;
        sample.id = car.id;
        std::lock_guard lock(g_remoteMutex);
        car.record->lastBodyPose = sample;
        car.record->shownX = sample.x;
        car.record->shownY = sample.y;
        car.record->shownZ = sample.z;
        car.record->shownH = sample.rotZ * (3.14159265f / 180.f);
        if (car.record->hosted && body != localVehicle) hostedCars.push_back(sample);
    }
    {
        std::lock_guard<std::mutex> lock(g_remoteMutex);
        g_hostedCarSnap.swap(hostedCars);
        if (!g_remoteResetPending.load()) g_streamedActors.swap(streamed);
    }
    // After the car loop, so the car bodies that a seat needs exist for this tick.
    ApplyRemoteSeats();
    WatchLocalVehicle();
}

// Registration wardrobe preview. Lua runs on mta-net; the body, camera, clock and
// clothes run here on the game thread. One new ped body per call (skill cap).
namespace {
constexpr int kScriptPedSlots = 4;
constexpr int kClothesSlots = 4;
constexpr uint32_t kFirstScriptPed = 0x300001u;

struct ScriptPedSlot {
    uint32_t id = 0;
    int model = 0;
    float x = 0, y = 0, z = 0, heading = 0;
    bool used = false;
    bool spawn = false;
    bool drop = false;
    bool pose = false;
    bool clothes = false;
    bool frozen = false;
    void* body = nullptr;
    char texture[kClothesSlots][40]{};
    char cloth[kClothesSlots][40]{};
    bool slotOn[kClothesSlots]{};
};

std::mutex g_scriptMu;
ScriptPedSlot g_scriptPeds[kScriptPedSlots];
uint32_t g_nextScriptPed = kFirstScriptPed;

struct ScriptCam {
    float x = 0, y = 0, z = 0, lx = 0, ly = 0, lz = 0;
    bool on = false;
};
ScriptCam g_scriptCam;
bool g_scriptCamRestore = false;
bool g_scriptTimeOn = false;
int g_scriptHour = 12, g_scriptMinute = 0;
bool g_scriptWeatherOn = false;
int g_scriptWeather = 0;

struct CamFns {
    bool looked = false;
    void (*fixed)(void*, const float*, const float*) = nullptr;
    // libGTASA exports TakeControlNoEntity(CVector const&, short, int), not (int, int).
    void (*take)(void*, const float*, short, int) = nullptr;
    void (*restore)(void*) = nullptr;
    void* camera = nullptr;
};
CamFns& CamApi() {
    static CamFns api;
    if (api.looked) return api;
    api.looked = true;
#ifdef __ANDROID__
    void* handle = dlopen("libGTASA.so", RTLD_NOLOAD | RTLD_GLOBAL);
    if (!handle) return api;
    api.fixed = reinterpret_cast<void (*)(void*, const float*, const float*)>(
        dlsym(handle, "_ZN7CCamera26SetCamPositionForFixedModeERK7CVectorS2_"));
    api.take = reinterpret_cast<void (*)(void*, const float*, short, int)>(
        dlsym(handle, "_ZN7CCamera19TakeControlNoEntityERK7CVectorsi"));
    api.restore = reinterpret_cast<void (*)(void*)>(dlsym(handle, "_ZN7CCamera7RestoreEv"));
    const uintptr_t base = Hook::MemoryPatcher::getLibraryBaseAddress("libGTASA.so");
    if (base) api.camera = reinterpret_cast<void*>(base + 0xBBA8D0);
    if (!api.fixed || !api.take || !api.camera) {
        ENGINE_LOGI("DIAG setCameraMatrix: fixed=%p take=%p camera=%p restore=%p",
            reinterpret_cast<void*>(api.fixed), reinterpret_cast<void*>(api.take),
            api.camera, reinterpret_cast<void*>(api.restore));
    }
#endif
    return api;
}

struct WorldFns {
    bool looked = false;
    // CClock::SetGameClock(hour, minute, unknown). The export takes three bytes.
    void (*clock)(unsigned char, unsigned char, unsigned char) = nullptr;
    void (*weather)(short) = nullptr;
};
WorldFns& WorldApi() {
    static WorldFns api;
    if (api.looked) return api;
    api.looked = true;
#ifdef __ANDROID__
    void* handle = dlopen("libGTASA.so", RTLD_NOLOAD | RTLD_GLOBAL);
    if (!handle) return api;
    api.clock = reinterpret_cast<void (*)(unsigned char, unsigned char, unsigned char)>(
        dlsym(handle, "_ZN6CClock12SetGameClockEhhh"));
    api.weather = reinterpret_cast<void (*)(short)>(dlsym(handle, "_ZN8CWeather12ForceWeatherEs"));
    if (!api.clock)
        ENGINE_LOGI("DIAG setTime: CClock::SetGameClock missing");
    if (!api.weather)
        ENGINE_LOGI("DIAG setWeather: CWeather::ForceWeather missing");
#endif
    return api;
}

bool ApplyScriptClothes(void* ped, const ScriptPedSlot& slot) {
    if (!StreamingIdle()) return false;
    auto& api = Clothes();
    if (!api.rebuild || !api.setTex) {
        ENGINE_LOGI("DIAG addPedClothes: rebuild=%p set=%p",
            reinterpret_cast<void*>(api.rebuild), reinterpret_cast<void*>(api.setTex));
        return false;
    }
    void* data = EnsurePlayerData(ped, true);
    if (data && api.allocData) api.allocData(data);
    void* desc = data ? *reinterpret_cast<void**>(static_cast<unsigned char*>(data) + 8) : nullptr;
    if (!desc) {
        ENGINE_LOGI("DIAG addPedClothes: clothes desc missing");
        return false;
    }
    for (int i = 0; i < kClothesSlots; ++i) {
        // CClientPlayerClothes::RemoveClothes / AddAllToModel (384-452)
        // uses the first clothing in slots 0-3 for bare torso/face/legs/feet.
        // Clearing them to null produces texture key 0: S25 2026-11-16
        // 04:37:25, GetClothesTexture+268 -> strlen(nullptr) in the preview.
        const auto& bare = PlayerClothesTable::kTable[i].items[0];
        if (!slot.slotOn[i]) api.setTex(desc, bare.texture, bare.model, i);
        else api.setTex(desc, slot.texture[i], slot.cloth[i], i);
    }
    api.rebuild(ped, false);
    ENGINE_LOGI("WARDROBE clothes rebuilt id=%u (bare slots preserved)", slot.id);
    return true;
}

void ServiceScriptStage() {
    ScriptPedSlot spawn{};
    bool doSpawn = false;
    void* drop = nullptr;
    ScriptPedSlot dress{};
    bool doDress = false;
    ScriptPedSlot pose{};
    bool doPose = false;
    ScriptCam cam{};
    bool restoreCam = false;
    bool timeOn = false;
    int hour = 0, minute = 0, weather = 0;
    bool weatherOn = false;
    {
        std::lock_guard<std::mutex> lock(g_scriptMu);
        cam = g_scriptCam;
        restoreCam = g_scriptCamRestore && !g_scriptCam.on;
        g_scriptCamRestore = false;
        timeOn = g_scriptTimeOn;
        hour = g_scriptHour;
        minute = g_scriptMinute;
        weatherOn = g_scriptWeatherOn;
        weather = g_scriptWeather;
        g_scriptTimeOn = false;
        g_scriptWeatherOn = false;
        for (auto& slot : g_scriptPeds) {
            if (!slot.used) continue;
            if (slot.drop && slot.body) {
                drop = slot.body;
                slot.body = nullptr;
                slot.drop = false;
                slot.used = false;
                break;
            }
            if (slot.spawn && !slot.body && !doSpawn) {
                spawn = slot;
                slot.spawn = false;
                doSpawn = true;
            }
        }
    }
    if (drop) ReleasePlayerPed(drop);
    if (doSpawn) {
        const float rad = spawn.heading;
        void* body = CreateRemotePlayerPed(spawn.model, spawn.x, spawn.y, spawn.z, rad, true);
        std::lock_guard<std::mutex> lock(g_scriptMu);
        bool kept = false;
        for (auto& slot : g_scriptPeds) {
            if (slot.id != spawn.id || !slot.used || slot.drop) continue;
            slot.body = body;
            kept = true;
            if (body) {
                SetPhysicalFrozen(body, slot.frozen);
                slot.pose = false;
                if (slot.clothes) {
                    dress = slot;
                    slot.clothes = false;
                    doDress = true;
                }
            }
            ENGINE_LOGI("WARDROBE createPed id=%u model=%d body=%p", slot.id, slot.model, body);
            break;
        }
        if (body && !kept) ReleasePlayerPed(body);
    }
    {
        std::lock_guard<std::mutex> lock(g_scriptMu);
        for (auto& slot : g_scriptPeds) {
            if (!slot.used || !slot.body) continue;
            if (slot.pose) {
                pose = slot;
                slot.pose = false;
                doPose = true;
            }
            if (slot.clothes && !doDress) {
                dress = slot;
                slot.clothes = false;
                doDress = true;
            }
        }
    }
    if (doPose && pose.body) {
        PlaceRemotePlayerPed(pose.body, pose.x, pose.y, pose.z, pose.heading);
        SetPhysicalFrozen(pose.body, pose.frozen);
    }
    if (doDress && dress.body && !ApplyScriptClothes(dress.body, dress)) {
        std::lock_guard<std::mutex> lock(g_scriptMu);
        for (auto& slot : g_scriptPeds)
            if (slot.used && !slot.drop && slot.id == dress.id && slot.body == dress.body)
                slot.clothes = true;
    }
    if (timeOn) {
        auto& api = WorldApi();
        if (api.clock) api.clock(static_cast<unsigned char>(hour), static_cast<unsigned char>(minute), 0);
    }
    if (weatherOn) {
        auto& api = WorldApi();
        if (api.weather) api.weather(static_cast<short>(weather));
    }
    if (restoreCam) {
        auto& api = CamApi();
        if (api.camera && api.restore) api.restore(api.camera);
    } else if (cam.on) {
        auto& api = CamApi();
        if (api.camera && api.fixed && api.take) {
            const float pos[3] = {cam.x, cam.y, cam.z};
            const float up[3] = {0.f, 0.f, 1.f};
            const float look[3] = {cam.lx, cam.ly, cam.lz};
            api.fixed(api.camera, pos, up);
            api.take(api.camera, look, static_cast<short>(2), 1);
        }
    }
}

}  // namespace

uint32_t NoteScriptPed(int model, float x, float y, float z, float rotationDegrees) {
    if (model < 0 || model > 312 || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return 0;
    float rad = rotationDegrees * 0.01745329251f;
    constexpr float kPi = 3.14159265f;
    if (std::isfinite(rad)) {
        while (rad < -kPi) rad += kPi * 2.f;
        while (rad > kPi) rad -= kPi * 2.f;
    } else {
        rad = 0.f;
    }
    std::lock_guard<std::mutex> lock(g_scriptMu);
    for (auto& slot : g_scriptPeds) {
        if (slot.used) continue;
        slot = {};
        slot.used = true;
        slot.spawn = true;
        slot.frozen = true;
        slot.id = g_nextScriptPed++;
        if (g_nextScriptPed < kFirstScriptPed) g_nextScriptPed = kFirstScriptPed;
        slot.model = model;
        slot.x = x;
        slot.y = y;
        slot.z = z;
        slot.heading = rad;
        PedQueries::Create(slot.id, false);
        return slot.id;
    }
    return 0;
}

bool ReleaseScriptPed(uint32_t id) {
    std::lock_guard<std::mutex> lock(g_scriptMu);
    for (auto& slot : g_scriptPeds) {
        if (!slot.used || slot.id != id) continue;
        slot.drop = true;
        PedQueries::Forget(id);
        slot.used = slot.body != nullptr;
        if (!slot.body) slot.used = false;
        return true;
    }
    return false;
}

bool NoteScriptPedModel(uint32_t id, int model) {
    if (model < 0 || model > 312 || model == 74 || model == 149 || model == 208) return false;
    std::lock_guard<std::mutex> lock(g_scriptMu);
    for (auto& slot : g_scriptPeds) {
        if (!slot.used || slot.id != id || slot.drop) continue;
        if (slot.model == model) return false;
        slot.model = model;
        return true;
    }
    return false;
}

bool ScriptPedAlive(uint32_t id) {
    std::lock_guard<std::mutex> lock(g_scriptMu);
    for (const auto& slot : g_scriptPeds)
        if (slot.used && slot.id == id && !slot.drop) return true;
    return false;
}

void NoteScriptClothes(uint32_t id, const char* texture, const char* model, int type, bool remove) {
    if (type < 0 || type >= kClothesSlots) return;
    if (!remove) {
        // Desktop CClientPlayerClothes::GetClothing (524-544) ignores
        // unlisted pairs instead of passing unknown names into PLAYER.IMG.
        const auto& choices = PlayerClothesTable::kTable[type];
        bool listed = false;
        for (int i = 0; texture && model && i < choices.count; ++i)
            if (strcasecmp(texture, choices.items[i].texture) == 0 && strcasecmp(model, choices.items[i].model) == 0)
                listed = true;
        if (!listed) return;
    }
    std::lock_guard<std::mutex> lock(g_scriptMu);
    for (auto& slot : g_scriptPeds) {
        if (!slot.used || slot.id != id) continue;
        slot.slotOn[type] = !remove && texture && model && texture[0] && model[0];
        if (slot.slotOn[type]) {
            std::snprintf(slot.texture[type], sizeof slot.texture[type], "%s", texture);
            std::snprintf(slot.cloth[type], sizeof slot.cloth[type], "%s", model);
        }
        slot.clothes = true;
        return;
    }
}

bool CopyScriptClothes(uint32_t id, int type, char* texture, int textureBytes, char* model, int modelBytes) {
    if (!texture || !model || textureBytes <= 0 || modelBytes <= 0 || type < 0 || type >= kClothesSlots) return false;
    texture[0] = 0;
    model[0] = 0;
    std::lock_guard<std::mutex> lock(g_scriptMu);
    for (const auto& slot : g_scriptPeds) {
        if (!slot.used || slot.drop || slot.id != id || !slot.slotOn[type]) continue;
        std::snprintf(texture, static_cast<size_t>(textureBytes), "%s", slot.texture[type]);
        std::snprintf(model, static_cast<size_t>(modelBytes), "%s", slot.cloth[type]);
        return texture[0] != 0 && model[0] != 0;
    }
    return false;
}

void NoteScriptRotation(uint32_t id, float rotationDegrees) {
    float rad = rotationDegrees * 0.01745329251f;
    constexpr float kPi = 3.14159265f;
    if (!std::isfinite(rad)) return;
    while (rad < -kPi) rad += kPi * 2.f;
    while (rad > kPi) rad -= kPi * 2.f;
    std::lock_guard<std::mutex> lock(g_scriptMu);
    for (auto& slot : g_scriptPeds) {
        if (!slot.used || slot.id != id) continue;
        slot.heading = rad;
        slot.pose = true;
        return;
    }
}

void NoteScriptFrozen(uint32_t id, bool frozen) {
    std::lock_guard<std::mutex> lock(g_scriptMu);
    for (auto& slot : g_scriptPeds) {
        if (!slot.used || slot.id != id) continue;
        slot.frozen = frozen;
        slot.pose = true;
        return;
    }
}

void NoteScriptCamera(float x, float y, float z, float lookX, float lookY, float lookZ) {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) ||
        !std::isfinite(lookX) || !std::isfinite(lookY) || !std::isfinite(lookZ))
        return;
    std::lock_guard<std::mutex> lock(g_scriptMu);
    g_scriptCam = {x, y, z, lookX, lookY, lookZ, true};
}

void ClearScriptCamera() {
    std::lock_guard<std::mutex> lock(g_scriptMu);
    if (g_scriptCam.on) g_scriptCamRestore = true;
    g_scriptCam.on = false;
}

bool NoteScriptTime(int hour, int minute) {
    if (hour < 0 || hour > 23 || minute < 0 || minute > 60) return false;
    std::lock_guard<std::mutex> lock(g_scriptMu);
    g_scriptHour = hour;
    g_scriptMinute = minute;
    g_scriptTimeOn = true;
    return true;
}

bool NoteScriptWeather(int weather) {
    if (weather < 0 || weather > 255) return false;
    std::lock_guard<std::mutex> lock(g_scriptMu);
    g_scriptWeather = weather;
    g_scriptWeatherOn = true;
    return true;
}

bool CopyScriptWeather(int& weather) {
    std::lock_guard<std::mutex> lock(g_scriptMu);
    if (!g_scriptWeatherOn) return false;
    weather = g_scriptWeather;
    return true;
}

bool CopySpatialScene(std::vector<Spatial::Element>& elements) {
    return GameTasks::RunQuery([&] {
        auto sample=[](Spatial::Element& e,void* body) {
            if(!body || IsPlaceableVtable(body)) return;
            float x=0,y=0,z=0,h=0;
            if(ReadBodyPos(body,x,y,z,h)) { e.position={x,y,z}; e.bounds.center=e.position; }
            // GetBoundCentre 0x4cba84 loads CEntity model from +0x32.
            e.model=*reinterpret_cast<const int16_t*>(static_cast<const unsigned char*>(body)+0x32);
        };
        for(auto& e:elements) {
            if(e.id==ServerWorld::LocalPlayer()) { sample(e,g_findPlayer?g_findPlayer(0):nullptr); continue; }
            std::lock_guard lock(g_remoteMutex);
            auto ped=g_remoteBodies.find(e.id);
            if(ped!=g_remoteBodies.end() && PedQueries::Current(e.id,ped->second.queryGeneration)) sample(e,ped->second.ped);
            auto car=g_remoteCars.find(e.id);
            if(car!=g_remoteCars.end() && car->second.spatialGeneration==e.generation) sample(e,car->second.body);
        }
        {
            std::lock_guard lock(g_scriptMu);
            for(const auto& ped:g_scriptPeds) if(ped.used&&!ped.drop) {
                Spatial::Element e; e.id=ped.id; e.generation=e.id; e.type="ped"; e.model=ped.model;
                e.position={ped.x,ped.y,ped.z}; e.bounds.center=e.position; sample(e,ped.body);
                elements.push_back(e);
            }
        }
        SpatialNative::ModelBounds(elements);
    });
}

static bool ReadNativePedQuery(uint32_t id, void* body, PedQueries::Snapshot& result) {
    if (IsPlaceableVtable(body) || !ReadPedInventory(body, result)) return false;
    result.health = *reinterpret_cast<const float*>(static_cast<const unsigned char*>(body) + 0x6AC);
    if (!std::isfinite(result.health)) return false;
    bool hl = false, al = false;
    float health = 0, armor = 0;
    CopyPedVitalsLock(id, hl, health, al, armor);
    if (hl) result.health = health;
    // CPedIntelligence +8 is CTaskManager, primary[2] is the
    // event-response-nontemp root (SetTask 0x64e098/0x64e0c0).
    if (void* intel = PedIntel(body)) {
        void* task = reinterpret_cast<void**>(static_cast<unsigned char*>(intel) + 8)[2];
        if (task) {
            using TypeFn = int (*)(void*);
            result.taskType = reinterpret_cast<TypeFn>((*reinterpret_cast<void***>(task))[0x28 / 8])(task);
        }
    }
    return true;
}

static void PublishPedQueries() {
    static PedQueries::Frame frame;
    frame.clear();
    auto sample = [&](uint32_t id, void* body, uint64_t generation) {
        if (!body) return;
        PedQueries::Snapshot state;
        if (!PedQueries::Copy(id, state) || (generation && state.generation != generation)) return;
        if (!ReadNativePedQuery(id, body, state)) state.streamed = false;
        frame.emplace_back(id, state);
    };
    sample(ServerWorld::LocalPlayer(), g_findPlayer ? g_findPlayer(0) : nullptr, 0);
    for (const auto& [id, remote] : g_remoteBodies) {
        if (remote.queryGeneration) sample(id, remote.ped, remote.queryGeneration);
    }
    std::array<std::pair<uint32_t, void*>, kScriptPedSlots> scriptBodies{};
    {
        std::lock_guard lock(g_scriptMu);
        unsigned count = 0;
        for (const auto& ped : g_scriptPeds)
            if (ped.used && !ped.drop && ped.body) scriptBodies[count++] = {ped.id, ped.body};
    }
    for (const auto& [id, body] : scriptBodies) sample(id, body, 0);
    PedQueries::PublishFrame(frame);
}

bool CopyPedQuery(uint32_t id, PedQueries::Snapshot& out) {
    PedQueries::Snapshot result;
    bool copied = false;
    if (!GameTasks::RunQuery([&] {
        if (!PedQueries::Copy(id, result)) return;
        void* body = nullptr;
        const bool local = id == ServerWorld::LocalPlayer();
        if (local) body = g_findPlayer ? g_findPlayer(0) : nullptr;
        else {
            std::lock_guard lock(g_scriptMu);
            for (const auto& ped : g_scriptPeds)
                if (ped.used && !ped.drop && ped.id == id) body = ped.body;
        }
        if (!body && !local) {
            std::lock_guard lock(g_remoteMutex);
            auto found = g_remoteBodies.find(id);
            if (found != g_remoteBodies.end() && found->second.queryGeneration == result.generation)
                body = found->second.ped;
        }
        if (body && !ReadNativePedQuery(id, body, result)) return;
        PedQueries::Snapshot latest;
        copied = PedQueries::Copy(id, latest) && latest.generation == result.generation;
    }) || !copied) return false;
    out = result;
    return true;
}

bool CopyScriptPedFacts(uint32_t id, int& model, float& headingRadians, bool& frozen) {
    std::lock_guard<std::mutex> lock(g_scriptMu);
    for (const auto& slot : g_scriptPeds) {
        if (!slot.used || slot.id != id) continue;
        model = slot.model;
        headingRadians = slot.heading;
        frozen = slot.frozen;
        return true;
    }
    return false;
}

void NoteLocalHealth(float health) {
    // CClientPed::SetHealth clamps to GetMaxHealth. The phone does not read that max,
    // so the stored value stays inside 0..200, which covers the default 100 and the
    // stat-raised ped maximum used by desktop.
    if (!std::isfinite(health)) return;
    if (health < 0.f) health = 0.f;
    if (health > 200.f) health = 200.f;
    g_health = health;
    g_pendingLocalHealth = health;
    PedQueries::Health(ServerWorld::LocalPlayer(), health);
}



static void* ResolveProjectileEntity(uint32_t id) {
    if (id == 0 || id == 0xFFFFFFFFu) return nullptr;
    auto live = [](void* body) -> void* { return body && !IsPlaceableVtable(body) ? body : nullptr; };
    if (id == ServerWorld::LocalPlayer()) return live(g_findPlayer ? g_findPlayer(0) : nullptr);
    {
        std::lock_guard lock(g_remoteMutex);
        auto ped = g_remoteBodies.find(id);
        if (ped != g_remoteBodies.end()) return live(ped->second.ped);
        auto car = g_remoteCars.find(id);
        if (car != g_remoteCars.end()) return live(car->second.body);
    }
    {
        std::lock_guard lock(g_scriptMu);
        for (const auto& ped : g_scriptPeds)
            if (ped.used && ped.id == id && !ped.drop) return live(ped.body);
    }
    return nullptr;
}

bool HasNativeTrafficCapability(const char* name) {
    bool available = false;
    return GameTasks::Run([&] {
        auto& api = Tasks();
        const bool common = api.taskNew && api.addPrimary && api.findTaskByType && api.setPrimary;
        if (std::strcmp(name, "ped-wander") == 0) available = common && api.wanderCtor && api.wanderVtable;
        else if (std::strcmp(name, "drive-wander") == 0) available = common && api.driveWanderCtor && api.isVehicleDriver;
    }) && available;
}

bool CopyTrafficVehicleState(uint32_t id, HostedCar& out) {
    std::lock_guard lock(g_remoteMutex);
    auto found = g_remoteCars.find(id);
    if (found == g_remoteCars.end() || !found->second.lastBodyPose) return false;
    out = *found->second.lastBodyPose;
    return true;
}

bool SetTrafficVehicleVelocity(uint32_t id, float x, float y, float z, bool angular) {
    bool changed = false;
    return GameTasks::Run([&] {
        if (!ServerWorld::ActorHosted(id)) return;
        void* body = ResolveProjectileEntity(id);
        if (!body) return;
        auto* raw = static_cast<unsigned char*>(body);
        if ((*reinterpret_cast<const uint32_t*>(raw + 0x64) & (1u << 13)) != 0) return;
        const float speed[] = {x, y, z};
        std::memcpy(raw + (angular ? 0x74 : 0x68), speed, sizeof speed);
        changed = true;
    }) && changed;
}

bool SetTrafficVehicleRotation(uint32_t id, float rx, float ry, float rz) {
    bool changed = false;
    return GameTasks::Run([&] {
        if (!ServerWorld::ActorHosted(id)) return;
        void* body = ResolveProjectileEntity(id);
        changed = body && WriteBodyRotation(body, rx, ry, rz);
    }) && changed;
}

bool IsTrafficVehicleOnGround(uint32_t id) {
    bool onGround = false;
    return GameTasks::Run([&] {
        void* body = ResolveProjectileEntity(id);
        if (!body) return;
        using ModelIs = bool (*)(int);
        using OffGround = bool (*)(const void*);
        struct GroundFns {
            ModelIs car = nullptr, bike = nullptr, bmx = nullptr, quad = nullptr;
            OffGround carAirborne = nullptr, bikeAirborne = nullptr;
        };
        static const auto ground = [] {
            GroundFns fns;
            if (void* handle = dlopen("libGTASA.so", RTLD_NOLOAD | RTLD_GLOBAL)) {
                fns.car = reinterpret_cast<ModelIs>(dlsym(handle, "_ZN10CModelInfo10IsCarModelEi"));
                fns.bike = reinterpret_cast<ModelIs>(dlsym(handle, "_ZN10CModelInfo11IsBikeModelEi"));
                fns.bmx = reinterpret_cast<ModelIs>(dlsym(handle, "_ZN10CModelInfo10IsBmxModelEi"));
                fns.quad = reinterpret_cast<ModelIs>(dlsym(handle, "_ZN10CModelInfo15IsQuadBikeModelEi"));
                fns.carAirborne = reinterpret_cast<OffGround>(dlsym(handle, "_ZNK11CAutomobile21GetAllWheelsOffGroundEv"));
                fns.bikeAirborne = reinterpret_cast<OffGround>(dlsym(handle, "_ZN5CBike21GetAllWheelsOffGroundEv"));
            }
            return fns;
        }();
        const int model = ServerWorld::ModelOf(id);
        if (model < 400 || model > 611) return;
        if (ground.bikeAirborne && ((ground.bike && ground.bike(model)) || (ground.bmx && ground.bmx(model))))
            onGround = !ground.bikeAirborne(body);
        else if (ground.carAirborne && ((ground.car && ground.car(model)) || (ground.quad && ground.quad(model))))
            onGround = !ground.carAirborne(body);
    }) && onGround;
}

bool SetTrafficVehicleLoadCollision(uint32_t id, bool enabled) {
    std::lock_guard lock(g_remoteMutex);
    g_remoteCars[id].loadCollision = enabled;
    return true;
}

bool SetTrafficVehicleEngine(uint32_t id, bool enabled) {
    std::lock_guard lock(g_remoteMutex);
    auto& car = g_remoteCars[id];
    car.engineOn = enabled;
    if (car.observedVehicle) car.observedVehicle->engineOn = enabled;
    return true;
}

bool IsNativeActorStreamed(uint32_t id) {
    if (!id || id == 0xFFFFFFFFu) return false;
    if (id == ServerWorld::LocalPlayer()) return true;
    if (id < kFirstScriptPed) {
        const auto generation = ServerWorld::ElementGeneration(id);
        if (!generation) return false;
        std::lock_guard lock(g_remoteMutex);
        const std::pair key{id, generation};
        return std::find(g_streamedActors.begin(), g_streamedActors.end(), key) != g_streamedActors.end();
    }
    // Script peds have a separate four-slot pool and client-only IDs.
    bool streamed = false;
    return GameTasks::Run([&] { streamed = ResolveProjectileEntity(id) != nullptr; }) && streamed;
}

bool StartPedKillOnFoot(uint32_t id, uint32_t targetId) {
    if (id == targetId) return false;
    bool accepted = false;
    return GameTasks::Run([&] {
        if (!ServerWorld::ActorHosted(id) && id != ServerWorld::LocalPlayer()) return;
        void* body = ResolveProjectileEntity(id);
        void* target = ResolveProjectileEntity(targetId);
        void* intel = PedIntel(body);
        auto& api = Tasks();
        static void* const lib = dlopen("libGTASA.so", RTLD_NOLOAD | RTLD_GLOBAL);
        using Constructor = void (*)(void*, void*, int, unsigned, unsigned, unsigned, int);
        static const auto construct = lib ? reinterpret_cast<Constructor>(
            dlsym(lib, "_ZN25CTaskComplexKillPedOnFootC1EP4CPedijjji")) : nullptr;
        if (!intel || !target || !construct || !api.taskNew || !api.addPrimary) return;
        // ARM64 Clone at 0x5ee918 allocates 0x50. TaskAttackSA.cpp constructs
        // the desktop script command with an indefinite timeout and task-owned target.
        void* task = api.taskNew(0x50);
        if (!task) return;
        construct(task, target, -1, 0, 0, 0, 1);
        api.addPrimary(intel, task, false);
        if (auto it = g_remoteBodies.find(id); it != g_remoteBodies.end())
            it->second.trafficTask = static_cast<int>(TrafficPedTask::KillOnFoot);
        accepted = true;
    }) && accepted;
}

// A shot already simulated by the authoritative ped. Only touch the model,
// animation and sound here. Calling FireInstantHit would roll damage again.
bool PresentPedWeaponShot(uint32_t id, unsigned weapon, float x, float y, float z) {
    if (weapon < 22 || weapon > 34 || !PlausiblePosition(x, y, z)) return false;
    bool shown = false;
    return GameTasks::Run([&] {
        if (ServerWorld::ActorHosted(id)) return;
        auto it = g_remoteBodies.find(id);
        if (it == g_remoteBodies.end() || it->second.networkPlayer) return;
        void* body = it->second.ped;
        const auto* info = WeaponInfoOf(weapon);
        if (!body || !info) return;
        auto* clump = *reinterpret_cast<void**>(static_cast<unsigned char*>(body) + 0x20);
        if (!clump) return;
        if (!PedWeaponOfType(body, weapon) || CurrentWeaponSlot(body) != WeaponInfoSlot(weapon)) {
            const int model = WeaponInfoModel(weapon);
            if (model <= 0) return;
            if (StreamingLoadState(model) != 1) {
                if (g_requestModel) g_requestModel(model, 0);
                return;
            }
            if (!PedWeaponOfType(body, weapon)) PedGiveWeapon(body, weapon, 1, true, false, nullptr);
            else PedSetCurrentWeaponSlotSA(body, WeaponInfoSlot(weapon), false);
            if (CurrentWeaponSlot(body) != WeaponInfoSlot(weapon) || !PedWeaponOfType(body, weapon)) return;
        }
        static void* const lib = dlopen("libGTASA.so", RTLD_NOLOAD | RTLD_GLOBAL);
        if (!lib) return;
        using Blend = void* (*)(void*, int, int, float);
        using Reset = void (*)(void*, float);
        using Flash = void (*)(void*, int, bool);
        using Audio = void (*)(void*, int, int, void*);
        static const auto blend = reinterpret_cast<Blend>(dlsym(lib, "_ZN12CAnimManager14BlendAnimationEP7RpClump12AssocGroupId11AnimationIdf"));
        static const auto reset = reinterpret_cast<Reset>(dlsym(lib, "_ZN21CAnimBlendAssociation14SetCurrentTimeEf"));
        static const auto flash = reinterpret_cast<Flash>(dlsym(lib, "_ZN4CPed10DoGunFlashEib"));
        static const auto audio = reinterpret_cast<Audio>(dlsym(lib, "_ZN12CAudioEngine17ReportWeaponEventEi11eWeaponTypeP9CPhysical"));
        static void* const sound = dlsym(lib, "AudioEngine");
        if (!blend || !reset || !flash || !audio || !sound) return;
        float px, py, pz, heading;
        if (!ReadBodyPos(body, px, py, pz, heading)) return;
        if (std::hypot(x - px, y - py) > 0.1f)
            SetRemotePedHeading(body, std::atan2(px - x, y - py));
        // CTaskSimpleUseGun::StartAnim at 0x5decec / 0x5dedcc selects FIRE
        // (224) from CWeaponInfo + 0x1c, then blends it at 8.0.
        int group;
        std::memcpy(&group, info + 0x1c, sizeof group);
        static const auto groups = static_cast<const int*>(dlsym(lib, "_ZN12CAnimManager26ms_numAnimAssocDefinitionsE"));
        using BlockName = const char* (*)(int);
        static const auto blockName = reinterpret_cast<BlockName>(dlsym(lib, "_ZN12CAnimManager16GetAnimBlockNameE12AssocGroupId"));
        auto& api = PedRpc();
        if (!groups || group < 0 || group >= *groups || !blockName || !api.getAnimBlock || !api.getAnimBlockIndex) return;
        const char* name = blockName(group);
        const auto* block = name ? static_cast<const unsigned char*>(api.getAnimBlock(name)) : nullptr;
        if (!block) return;
        if (!block[0x10]) {
            const int index = api.getAnimBlockIndex(name);
            if (index >= 0 && g_requestModel) g_requestModel(kIfpStreamingBase + index, 0);
            return;
        }
        void* association = blend(clump, group, 224, 8.f);
        if (!association) return;
        reset(association, 0.f);
        flash(body, 250, false);
        audio(sound, 145, weapon, body); // AE_WEAPON_FIRE
        shown = true;
    }) && shown;
}

bool StartPedWander(uint32_t id, int moveState, int direction, bool sensible) {
    bool accepted = false;
    return GameTasks::Run([&] {
        const bool serverPed = std::strcmp(ServerWorld::ElementKindName(id), "ped") == 0;
        if (serverPed && !ServerWorld::ActorHosted(id)) return;
        accepted = StartWander(ResolveProjectileEntity(id), moveState, direction, sensible);
        if (accepted && serverPed) {
            auto stored = g_remoteBodies.find(id);
            if (stored != g_remoteBodies.end()) stored->second.trafficTask = static_cast<int>(TrafficPedTask::Wander);
        }
    }) && accepted;
}

bool StartPedDriveWander(uint32_t pedId, uint32_t vehicleId, float speed, int drivingStyle) {
    bool accepted = false;
    return GameTasks::Run([&] {
        if (!ServerWorld::ActorHosted(pedId) || !ServerWorld::ActorHosted(vehicleId)) return;
        auto seated = g_seatedPeds.find(pedId);
        if (seated == g_seatedPeds.end() || seated->second.vehicleId != vehicleId || seated->second.seat != 0) return;
        void* ped = ResolveProjectileEntity(pedId);
        void* car = ResolveProjectileEntity(vehicleId);
        auto& api = Tasks();
        void* intel = PedIntel(ped);
        if (!intel || !car || !api.isVehicleDriver || !api.isVehicleDriver(car, ped)) return;
        if (!api.taskNew || !api.driveWanderCtor || !api.addPrimary) {
            static bool reported = false;
            if (!reported) {
                reported = true;
                ENGINE_LOGI("DIAG setPedDriveWander: native driver task dispatch is unavailable");
            }
            return;
        }
        // libGTASA 2.10 C1 at 0x6070e8 initializes members through byte 0x30.
        void* task = api.taskNew(0x38);
        if (!task) return;
        api.driveWanderCtor(task, car, drivingStyle, speed);
        api.addPrimary(intel, task, false);
        auto stored = g_remoteBodies.find(pedId);
        if (stored != g_remoteBodies.end()) stored->second.trafficTask = static_cast<int>(TrafficPedTask::DriveWander);
        accepted = true;
    }) && accepted;
}

// Collision flag order follows CWorldSA.cpp:737-763. The queried ped is
// excluded by the ped flag. GTA owns all collision and task objects on this thread.
bool ScanTrafficObstacle(uint32_t id, bool resuming, float originalHeading, TrafficObstacleScan& out) {
    bool sampled = false;
    return GameTasks::RunQuery([&] {
        using Sight = bool (*)(const Vec3&, const Vec3&, bool, bool, bool, bool, bool, bool, bool);
        static const auto sight = [] {
            void* lib = dlopen("libGTASA.so", RTLD_NOLOAD | RTLD_GLOBAL);
            return lib ? reinterpret_cast<Sight>(dlsym(lib, "_ZN6CWorld21GetIsLineOfSightClearERK7CVectorS2_bbbbbbb")) : nullptr;
        }();
        auto ground = Ground().findGround;
        auto find = Tasks().findTaskByType;
        if (!sight || !ground || !find) {
            static bool reported = false;
            if (!reported) ENGINE_LOGE("DIAG traffic obstacle scan unavailable: sight, ground or task query missing");
            reported = true;
            return;
        }
        if (!ServerWorld::ActorHosted(id)) return;
        void* ped = ResolveProjectileEntity(id);
        void* intel = PedIntel(ped);
        float x, y, z, radians;
        if (!intel || !ReadBodyPos(ped, x, y, z, radians) || !std::isfinite(radians)) return;
        const auto has = [&](int type) { return find(intel, type) != nullptr; };
        // Client/sdk/game/TaskTypes.h; GetTaskType in the ARM64 wander, flee and crossing classes.
        enum { StandStill = 203, ObserveLights = 224, WaitAtLights = 225, CrossRoad = 227,
               Cower = 412, HandsUp = 413, Duck = 415, FleePoint = 908, FleeEntity = 909,
               SmartFleePoint = 910, SmartFleeEntity = 911, Wander = 912, FleeAnyMeans = 927, KillOnFoot = 1000 };
        constexpr float degrees = 57.29577951f;
        out.heading = radians * degrees;
        sampled = true;
        if (has(Cower) || has(HandsUp) || has(Duck) || has(KillOnFoot)) return;
        const bool fleeing = has(FleePoint) || has(FleeEntity) || has(SmartFleePoint) || has(SmartFleeEntity) || has(FleeAnyMeans);
        if (has(CrossRoad) || has(ObserveLights) || has(WaitAtLights)) {
            out.mode = fleeing ? "flee-crossing" : "crossing";
            return;
        }
        if (!fleeing && (!has(Wander) || has(StandStill))) return;
        out.mode = fleeing ? "flee" : "walk";
        if (resuming) radians = originalHeading / degrees;
        bool found = false;
        out.floor = ground(x, y, z + 0.25f, &found, nullptr);
        if (!found || !std::isfinite(out.floor) || z - out.floor > 2.f || out.floor > z + 0.2f) {
            sampled = false;
            return;
        }
        auto clearAt = [&](float ex, float ey, float ez, float height) {
            return sight({x, y, out.floor + height}, {ex, ey, ez + height},
                         true, true, false, true, true, false, false);
        };
        for (float angle : {0.f, -20.f, 20.f, -35.f, 35.f}) {
            float r = radians + angle / degrees;
            float ex = x - std::sin(r) * 1.6f, ey = y + std::cos(r) * 1.6f;
            if (!clearAt(ex, ey, out.floor, .25f) || !clearAt(ex, ey, out.floor, .60f) ||
                !clearAt(ex, ey, out.floor, 1.f)) {
                out.frontBlocked = true;
                break;
            }
        }
        if (resuming || !out.frontBlocked) return;
        for (int dir = 0; dir < 8; ++dir) {
            float r = dir * 45.f / degrees;
            float ex = x - std::sin(r) * 2.8f, ey = y + std::cos(r) * 2.8f;
            found = false;
            float end = ground(ex, ey, z + .25f, &found, nullptr);
            if (!found || !std::isfinite(end) || std::abs(end - out.floor) > 1.2f) continue;
            out.clear[dir] = clearAt(ex, ey, end, .15f) && clearAt(ex, ey, end, .60f) && clearAt(ex, ey, end, 1.f);
            out.ground[dir] = end;
        }
    }) && sampled;
}

bool IsTrafficPedTaskActive(uint32_t id, TrafficPedTask task) {
    bool active = false;
    return GameTasks::Run([&] {
        void* intel = PedIntel(ResolveProjectileEntity(id));
        auto& api = Tasks();
        active = intel && api.findTaskByType && api.findTaskByType(intel, static_cast<int>(task));
    }) && active;
}

bool KillNativePedTask(uint32_t id, bool primary, int slot, bool gracefully) {
    bool stopped = false;
    return GameTasks::Run([&] {
        void* intel = PedIntel(ResolveProjectileEntity(id));
        auto& api = Tasks();
        if (!intel || gracefully) return;
        auto* manager = static_cast<unsigned char*>(intel) + 8;
        if (primary && api.setPrimary) {
            api.setPrimary(manager, nullptr, slot, false);
            stopped = true;
        } else if (!primary && api.setSecondary) {
            api.setSecondary(manager, nullptr, slot);
            stopped = true;
        }
        if (stopped && primary && slot == 2) PedQueries::InvalidateNative(id);
    }) && stopped;
}

extern "C" void WorldRPG_NoStoryScript(void*) {
    ServiceScriptStage();
    static int depth = 0;
    static bool ready = false;
    if (depth > 0) return;
    if (ready) {
        ++depth;
        Projectiles::Pump(ResolveProjectileEntity);
        GameTasks::Service();
        ApplyLocalPedRpcs();
        PlacePlayer();
        SampleLocalPose();
        SampleLocalPadAndCamera();
        ApplyRemotePeds();
        if (!WorldRPG::Debug::Flag("noremote")) ApplyRemoteBullets();
        PublishPedQueries();
        ServerWorld::Tick(g_poseX.load(), g_poseY.load(), g_poseZ.load());
        // Server and client objects into GTA's world (CClientObject streaming), after ServerWorld
        // has published this frame's server objects. Game thread, like the ped and vehicle bodies.
        WorldObjects::Pulse(g_poseX.load(), g_poseY.load(), g_poseZ.load());
        --depth;
        return;
    }
    ++depth;
    Projectiles::Pump(ResolveProjectileEntity);
    GameTasks::Service();
    if (g_requestSpecial && g_loadAll && g_setupPlayer) {
        g_requestSpecial(0, "player", 26);
        g_loadAll(true);
        g_setupPlayer(0);
        ApplyLocalPedRpcs();
        PlacePlayer(true);
        DressLocalPlayer();
        if (g_endPause) g_endPause();
        ENGINE_LOGI("Player created without main.scm");
    }
    ready = true;
    ApplyRemotePeds();
    --depth;
}

// Desktop CMultiplayerSA.cpp:1415-1416 writes a ret at the start of
// CEntryExitManager::Update ("they crash when you enter anyway").
// libGTASA 2.10 arm64: CGame::Process calls Update, which calls
// CEntryExit::TransitionFinished(FindPlayerPed). That virtual-calls
// CPed::Teleport (vtable + 0x78) and the slot is not executable code
// (logcat 2026-11-13 18:31:09, SIGSEGV SEGV_ACCERR, TransitionFinished+1552).
static void DisableEntryExits() {
    constexpr std::uint32_t kOriginalWord = 0xd10283ffu;  // sub sp, sp, #0xa0
    constexpr std::uint32_t kReturnWord = 0xd65f03c0u;    // ret

    void* handle = dlopen("libGTASA.so", RTLD_NOLOAD | RTLD_GLOBAL);
    if (!handle) {
        ENGINE_LOGE("CEntryExitManager::Update: libGTASA.so not loaded");
        return;
    }
    void* fn = dlsym(handle, "_ZN17CEntryExitManager6UpdateEv");
    if (!fn) {
        ENGINE_LOGE("CEntryExitManager::Update symbol missing");
        return;
    }

    auto* site = static_cast<unsigned char*>(fn);
    std::uint32_t word = 0;
    std::memcpy(&word, site, sizeof(word));
    if (word == kReturnWord) return;
    if (word != kOriginalWord) {
        ENGINE_LOGE("CEntryExitManager::Update: unexpected word 0x%08x", word);
        return;
    }

    const long pageSizeLong = sysconf(_SC_PAGESIZE);
    if (pageSizeLong <= 0) {
        ENGINE_LOGE("CEntryExitManager::Update: sysconf failed");
        return;
    }
    const auto pageSize = static_cast<std::uintptr_t>(pageSizeLong);
    const auto addr = reinterpret_cast<std::uintptr_t>(site);
    void* page = reinterpret_cast<void*>(addr & ~(pageSize - 1));
    if (mprotect(page, pageSize, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
        ENGINE_LOGE("CEntryExitManager::Update: mprotect RWX failed");
        return;
    }
    std::memcpy(site, &kReturnWord, sizeof(kReturnWord));
    if (mprotect(page, pageSize, PROT_READ | PROT_EXEC) != 0) {
        ENGINE_LOGE("CEntryExitManager::Update: mprotect RX failed");
    }
    __builtin___clear_cache(reinterpret_cast<char*>(site), reinterpret_cast<char*>(site) + 4);
    ENGINE_LOGI("Disabled CEntryExitManager::Update (desktop CMultiplayerSA.cpp:1416)");
}

// Crash: null m_pWidgets[159], write at +0xb1. CWidgetRegionGang::Update+168
// (0x381434) loads that slot and stores the gang widget enabled byte
// (CWidget+0x59). The slot is CWidgetReplay; null makes the store address 0xb1
// (SIGSEGV on MainThread, Idle -> CTouchInterface::Update+108 at 0x36fae0,
// 2026-11-13 21:47:22 and 23:23:51). Desktop has no touch widgets and returns
// from CReplay::Update (CMultiplayerSA.cpp:1739) without constructing
// CWidgetRegionGang. Do not allocate one.
//
// The store is the only use of slot 159. The rest of Update (SetEnabled,
// CWidgetRegion::Update, ManageAlpha, BuildRects) null-checks its other
// loads. Patch that one strb to a bl. The prologue has already saved x30,
// and the epilogue reloads it before the tail call, so this bl does not
// change the return to CTouchInterface::Update. An entry trampoline is the
// wrong tool here: installHook copies 16 bytes and enters the replacement
// with BR, and a bad copy freezes Idle inside CTouchInterface::Update.
// x8 is the widget (or null), w9 is the byte, x0 is already `this`.
__attribute__((naked)) static void GangReplaySlotStore() {
    __asm__ volatile(
        "cbz x8, 1f\n\t"
        "strb w9, [x8, #0xb1]\n\t"
        "1:\n\t"
        "ret\n\t");
}

static bool WriteExecutable(void* site, const void* bytes, size_t size) {
    const long pageSizeLong = sysconf(_SC_PAGESIZE);
    if (pageSizeLong <= 0) return false;
    const auto pageSize = static_cast<std::uintptr_t>(pageSizeLong);
    const auto first = reinterpret_cast<std::uintptr_t>(site);
    const auto start = first & ~(pageSize - 1);
    const auto end = (first + size + pageSize - 1) & ~(pageSize - 1);
    if (mprotect(reinterpret_cast<void*>(start), end - start,
            PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
        return false;
    }
    std::memcpy(site, bytes, size);
    __builtin___clear_cache(reinterpret_cast<char*>(site),
        reinterpret_cast<char*>(site) + size);
    mprotect(reinterpret_cast<void*>(start), end - start, PROT_READ | PROT_EXEC);
    return true;
}

static void InstallGangRegionUpdateSkip() {
    // Update+0x9c .. +0xa8. The last word is the crashing store.
    constexpr std::uint32_t kLoadSlot = 0xf9427e88u;    // ldr x8, [x20, #0x4f8]
    constexpr std::uint32_t kLoadByte = 0x39416669u;    // ldrb w9, [x19, #0x59]
    constexpr std::uint32_t kMoveThis = 0xaa1303e0u;    // mov x0, x19
    constexpr std::uint32_t kStoreByte = 0x3902c509u;   // strb w9, [x8, #0xb1]
    constexpr std::uintptr_t kStoreOffset = 0xa8;

    void* handle = dlopen("libGTASA.so", RTLD_NOLOAD | RTLD_GLOBAL);
    if (!handle) {
        ENGINE_LOGE("CWidgetRegionGang::Update: libGTASA.so not loaded");
        return;
    }
    void* fn = dlsym(handle, "_ZN17CWidgetRegionGang6UpdateEv");
    if (!fn) {
        ENGINE_LOGE("CWidgetRegionGang::Update: symbol missing");
        return;
    }

    auto* site = static_cast<unsigned char*>(fn) + kStoreOffset;
    std::uint32_t words[4] = {};
    std::memcpy(words, site - 12, sizeof(words));
    if (words[0] != kLoadSlot || words[1] != kLoadByte || words[2] != kMoveThis) {
        ENGINE_LOGE("CWidgetRegionGang::Update: unexpected prefix %08x %08x %08x",
            words[0], words[1], words[2]);
        return;
    }
    if (words[3] != kStoreByte) {
        ENGINE_LOGE("CWidgetRegionGang::Update: store is 0x%08x, left unchanged", words[3]);
        return;
    }

    const auto stub = reinterpret_cast<std::uintptr_t>(&GangReplaySlotStore);
    const auto at = reinterpret_cast<std::uintptr_t>(site);
    const std::int64_t imm = (static_cast<std::int64_t>(stub) - static_cast<std::int64_t>(at)) >> 2;
    constexpr std::int64_t kBlImm = 1LL << 25;
    if (imm < -kBlImm || imm >= kBlImm) {
        ENGINE_LOGE("CWidgetRegionGang::Update: stub out of bl range");
        return;
    }
    const std::uint32_t branch = 0x94000000u | (static_cast<std::uint32_t>(imm) & 0x03ffffffu);
    if (!WriteExecutable(site, &branch, sizeof(branch))) {
        ENGINE_LOGE("CWidgetRegionGang::Update: mprotect failed");
        return;
    }
    ENGINE_LOGI("DIAG skip gang replay store when m_pWidgets[159] is null (strb at +0xb1)");
}

// RenderQueue::Flush (0x267878) on the multithreaded path sets byte +0x410
// and tail-calls OS_SemaphoreWait. GraphicsThread (0x267968) clears that
// byte only after it has drained the ring and run both SetSafe calls, then
// posts the semaphore. Live hang, 2026-11-13 23:39, engine pid 12704:
// MainThread was in sem_wait on that semaphore (value 0xfffffffe, return
// into RQVertexBuffer::Set at +0x120, Idle -> CFont::RenderFontBuffer),
// while GraphicsThread was in usleep(30) at GraphicsThread+0x2b0 and the
// byte was already 0. The post never woke the waiter, so Idle never
// returned. Completion is the byte, not the semaphore.
using RenderQueueFlushFn = void (*)(void*);
static RenderQueueFlushFn g_renderQueueFlush = nullptr;
static std::uintptr_t g_gtasaBase = 0;

static void* RenderQueueFlushSemaphore() {
    if (!g_gtasaBase) return nullptr;
    // RenderQueue::Initialize stores the semaphore through the pointer at
    // this link address (adrp 0x84e000 + 0x608).
    constexpr std::uintptr_t kSemSlot = 0x84e608;
    auto* slot = *reinterpret_cast<void**>(g_gtasaBase + kSemSlot);
    if (!slot) return nullptr;
    return *reinterpret_cast<void**>(slot);
}

static void RenderQueueFlush(void* queue) {
    auto* bytes = static_cast<unsigned char*>(queue);
    // +0x3e8 is the multithreaded flag set by RenderQueue::RenderQueue.
    if (!bytes[0x3e8]) {
        if (g_renderQueueFlush) g_renderQueueFlush(queue);
        return;
    }
    __atomic_store_n(bytes + 0x410, static_cast<unsigned char>(1), __ATOMIC_RELEASE);
    int waits = 0;
    while (__atomic_load_n(bytes + 0x410, __ATOMIC_ACQUIRE)) {
        sched_yield();
        if (!__atomic_load_n(bytes + 0x410, __ATOMIC_ACQUIRE)) break;
        usleep(200);
        if (++waits == 5000)
            ENGINE_LOGI("DIAG RenderQueue::Flush waited 1s for the graphics thread");
    }
    // GraphicsThread posts after clearing the byte. Consume the token when
    // it has arrived. Do not block if the post was the one that never woke
    // the old waiter: the byte is the completion condition.
    if (void* sem = RenderQueueFlushSemaphore()) {
        for (int i = 0; i < 4; ++i) {
            if (sem_trywait(static_cast<sem_t*>(sem)) != 0) break;
        }
    }
}

static void InstallRenderQueueFlush(std::uintptr_t base) {
    constexpr std::uint32_t kOriginalWord = 0xf81e0ff3u;  // str x19, [sp, #-0x20]!
    g_gtasaBase = base;
    void* handle = dlopen("libGTASA.so", RTLD_NOLOAD | RTLD_GLOBAL);
    if (!handle) {
        ENGINE_LOGE("RenderQueue::Flush: libGTASA.so not loaded");
        return;
    }
    void* fn = dlsym(handle, "_ZN11RenderQueue5FlushEv");
    if (!fn) {
        ENGINE_LOGE("RenderQueue::Flush: symbol missing");
        return;
    }
    std::uint32_t word = 0;
    std::memcpy(&word, fn, sizeof(word));
    if (word != kOriginalWord) {
        ENGINE_LOGE("RenderQueue::Flush: unexpected word 0x%08x", word);
        return;
    }
    // First 16 bytes are str/stp/add/mov of x19. None are PC-relative.
    static Hook::MemoryPatcher patcher;
    const auto hooked = patcher.installHook(reinterpret_cast<uintptr_t>(fn),
        reinterpret_cast<uintptr_t>(&RenderQueueFlush));
    if (!hooked.success) {
        ENGINE_LOGE("RenderQueue::Flush hook failed: %s", hooked.errorMessage.c_str());
        return;
    }
    g_renderQueueFlush = reinterpret_cast<RenderQueueFlushFn>(hooked.trampolineAddress);
    ENGINE_LOGI("DIAG RenderQueue::Flush waits on byte +0x410, not the semaphore");
}

// Set once GTA has been in the playing state for 120 frames with no menu up. The
// connection loading screen uses it as desktop CClientGame::m_bGameLoaded.
static std::atomic<bool> leftMenu{false};
bool ServerSessionLive() { return leftMenu; }

static std::atomic<bool> g_serverSessionRequested{false};
static void (*g_originalDoGameState)() = nullptr;
static bool BeginServerSessionOnGameThread();

static void ServerSessionGameState() {
    // Desktop CCore::DoPreFramePulse / DoPostFramePulse (CCore.cpp 2743-2759,
    // 2886-2893) runs game and connect work on the game frame. Android's
    // mta-net thread must not close MobileMenu: Menu_SwitchOffToGame creates
    // touch widgets and submits texture commands to the single-producer RQ.
    // S25, 2026-11-16 04:24:05: TextureListingContainer::CreateRaster ->
    // CTouchInterface::CreateAll -> Menu_SwitchOffToGame -> BeginServerSession
    // ran on mta-net while MainThread also wrote the queue (RenderQueue faults).
    g_originalDoGameState();
    if (g_serverSessionRequested.load(std::memory_order_acquire)) {
        static bool announced = false;
        if (!announced) {
            announced = true;
            ENGINE_LOGI("Server session entering on GTA game thread tid=%d", gettid());
        }
        BeginServerSessionOnGameThread();
    }
}

// Written by the network pulse, sampled only at the GTA ambient generators.
static std::atomic<bool> g_allowAmbientPeds{false};
static std::atomic<bool> g_allowAmbientVehicles{false};
static void (*g_updatePedPopulation)(bool) = nullptr;
static void (*g_managePopulation)() = nullptr;
static void (*g_generateRandomCars)() = nullptr;
static void (*g_processCarGenerators)() = nullptr;

void SetAmbientTrafficEnabled(bool pedestrians, bool vehicles) {
    g_allowAmbientPeds.store(pedestrians, std::memory_order_relaxed);
    g_allowAmbientVehicles.store(vehicles, std::memory_order_relaxed);
}

static void UpdatePedPopulation(bool generate) {
    if (g_allowAmbientPeds.load(std::memory_order_relaxed)) {
        g_updatePedPopulation(generate);
    } else {
        // Desktop CMultiplayerSA.cpp:1735 stops Update after ManagePopulation.
        // Stock Update(false) still spawns the startup crowd. Run only management
        // so existing random peds can despawn and streamed props retain collision.
        g_managePopulation();
    }
}

static void GenerateRandomCars() {
    if (g_allowAmbientVehicles.load(std::memory_order_relaxed)) g_generateRandomCars();
}

static void ProcessCarGenerators() {
    if (g_allowAmbientVehicles.load(std::memory_order_relaxed)) g_processCarGenerators();
}

bool InstallServerSessionHook() {
    if (g_originalDoGameState) return true;
    void* handle = dlopen("libGTASA.so", RTLD_NOLOAD | RTLD_GLOBAL);
    if (!handle) return false;  // not the isolated engine process
    void* fn = dlsym(handle, "_Z11DoGameStatev");
    // libGTASA 2.10 arm64 DoGameState at 0x70a208. Its fourth instruction
    // is PC-relative ADRP, so do not copy this prologue into a trampoline.
    constexpr std::array<uint32_t, 4> expected{0xa9be4ff4, 0xa9017bfd, 0x910043fd, 0xf0000a33};
    if (!fn || std::memcmp(fn, expected.data(), sizeof(expected)) != 0) {
        ENGINE_LOGE("DIAG server session: missing or unverified DoGameState; game-thread dispatch not installed");
        dlclose(handle);
        return false;
    }
    // JUMP_SLOTs in libGTASA 2.10 arm64. CGame::Process enters the population,
    // random traffic and parked car generators through these slots. The original
    // entries remain callable, including GenerateRandomCars' PC-relative prologue.
    std::array<Import, 4> imports{{
        {"_Z11DoGameStatev", 0x847f80, reinterpret_cast<void*>(&ServerSessionGameState)},
        {"_ZN11CPopulation6UpdateEb", 0x842228, reinterpret_cast<void*>(&UpdatePedPopulation)},
        {"_ZN8CCarCtrl18GenerateRandomCarsEv", 0x83efd0, reinterpret_cast<void*>(&GenerateRandomCars)},
        {"_ZN17CTheCarGenerators7ProcessEv", 0x83ff00, reinterpret_cast<void*>(&ProcessCarGenerators)},
    }};
    g_managePopulation = reinterpret_cast<decltype(g_managePopulation)>(
        dlsym(handle, "_ZN11CPopulation16ManagePopulationEv"));
    if (!g_managePopulation || !BindImports(handle, imports)) {
        ENGINE_LOGE("DIAG server session: could not install the game state and ambient traffic hooks");
        dlclose(handle);
        return false;
    }
    g_originalDoGameState = reinterpret_cast<decltype(g_originalDoGameState)>(imports[0].original);
    g_updatePedPopulation = reinterpret_cast<decltype(g_updatePedPopulation)>(imports[1].original);
    g_generateRandomCars = reinterpret_cast<decltype(g_generateRandomCars)>(imports[2].original);
    g_processCarGenerators = reinterpret_cast<decltype(g_processCarGenerators)>(imports[3].original);
    VehiclePaint::Install(handle);
    dlclose(handle);
    ENGINE_LOGI("Server session dispatcher installed before GTA threads (DoGameState)");
    return true;
}

bool BeginServerSession() {
    if (!g_originalDoGameState) return false;
    g_serverSessionRequested.store(true, std::memory_order_release);
    return true;
}

static void CloseMobileMenuOnGameThread(void* menu) {
    // InitForPause (0x3571b8) stores the pending screen at +0x30 before
    // it is on the +0x24 screen stack. RemoveTopScreen (0x354758) returns
    // immediately for an empty stack, leaving that pending screen alive.
    // ProcessPending (0x3569c0-0x356a28) pushes it and clears +0x30. Do
    // that first, then pop normally; never leave the loader waiting forever
    // on a pending pause screen that has zero stacked screens.
    if (*reinterpret_cast<uintptr_t*>(static_cast<unsigned char*>(menu) + 0x30)) {
        if (!g_processMenuPending) {
            ENGINE_LOGE("DIAG server session: MobileMenu::ProcessPending missing");
            return;
        }
        g_processMenuPending(menu);
    }
    for (int i = 0; i < 8 && *reinterpret_cast<int32_t*>(static_cast<unsigned char*>(menu) + 0x24) > 0; ++i)
        g_removeTop(menu, 1, 1);
    if (g_switchOff) g_switchOff();
}

static bool BeginServerSessionOnGameThread() {
    const uintptr_t base = Hook::MemoryPatcher::getLibraryBaseAddress("libGTASA.so");
    if (leftMenu) {
        if (!base) return true;
        constexpr uintptr_t kPendingPause = 0x00D3D664;
        constexpr uintptr_t kGotMobileMenu = 0x008519B0;
        auto* pending = reinterpret_cast<uint8_t*>(base + kPendingPause);
        uintptr_t menu = *reinterpret_cast<uintptr_t*>(base + kGotMobileMenu);
        const bool menuUp = menu && (*reinterpret_cast<int32_t*>(menu + 0x24) != 0 ||
            *reinterpret_cast<uintptr_t*>(menu + 0x30) != 0);
        if (*pending || menuUp) {
            if (*pending) {
                AllowWrite(pending, 1);
                *pending = 0;
            }
            if (menuUp && g_removeTop) {
                g_mobileMenu = reinterpret_cast<void*>(menu);
                CloseMobileMenuOnGameThread(g_mobileMenu);
            }
            ::WorldRPG::JniBridge::PostGameLine(9, "open");
        }
        return true;
    }
    if (!base) return false;
    // libGTASA 2.10 ARM64. DoGameState is the stock switch MobileSA copies.
    constexpr uintptr_t kGameState = 0x00D3D640;
    constexpr uintptr_t kGotMobileMenu = 0x008519B0;
    constexpr uintptr_t kScriptProcess = 0x003f3f18;
    auto* state = reinterpret_cast<int32_t*>(base + kGameState);
    AllowWrite(state, sizeof(int32_t));
    ResolvePlayerFns();
    static bool scriptBlocked = false;
    if (!scriptBlocked) {
        static Hook::MemoryPatcher patcher;
        const auto hooked = patcher.installHook(base + kScriptProcess,
            reinterpret_cast<uintptr_t>(&WorldRPG_NoStoryScript));
        scriptBlocked = hooked.success;
        g_originalScript = hooked.success
            ? reinterpret_cast<ScriptProcessFn>(hooked.trampolineAddress) : nullptr;
        (void)g_originalScript;
        if (!hooked.success) {
            ENGINE_LOGE("Could not block CRunningScript::Process: %s", hooked.errorMessage.c_str());
            return false;
        }
        ENGINE_LOGI("Blocked CRunningScript::Process; main.scm will not run");
        if (!InstallRemoteControlHook())
            ENGINE_LOGE("Remote players keep the velocity fallback (no controller hook)");
        {
            // CEventDamage::AffectsPed (const, 0x444bac; prologue is sub sp / str / stp / stp, no PC-relative words).
            void* handle = dlopen("libGTASA.so", RTLD_NOLOAD | RTLD_GLOBAL);
            if (handle) InstallVehicleEntryGuard(handle);
            void* affects = handle ? dlsym(handle, "_ZNK12CEventDamage10AffectsPedEP4CPed") : nullptr;
            static Hook::MemoryPatcher damagePatcher;
            constexpr uint32_t affectsPrologue[] = {0xd10103ff, 0xf9000bf5, 0xa9024ff4, 0xa9037bfd};
            const auto damage = affects && std::memcmp(affects, affectsPrologue, sizeof affectsPrologue) == 0
                ? damagePatcher.installHook(reinterpret_cast<uintptr_t>(affects), reinterpret_cast<uintptr_t>(&HookAffectsPed))
                : Hook::HookResult{};
            if (affects && damage.success) {
                g_origAffectsPed = reinterpret_cast<AffectsPedFn>(damage.trampolineAddress);
                ENGINE_LOGI("CEventDamage::AffectsPed hook installed (remote damage lock)");
            } else {
                ENGINE_LOGE("DIAG remote damage lock not installed: AffectsPed=%p", affects);
            }
        }
        if (!InstallRenderGuard())
            ENGINE_LOGE("RenderGuard not installed; the sortLODs crash is unguarded");
        InstallWeaponAimHooks();
        if (!InstallLocalBulletFireHook())
            ENGINE_LOGE("Local bullet sync (PLAYER_BULLETSYNC) disabled: CWeapon::Fire hook missing");
        DisableEntryExits();
        InstallGangRegionUpdateSkip();
        InstallRenderQueueFlush(base);
    }
    // Idle opens MobileMenu::InitForPause when this byte is set. The story
    // script is what normally consumes it. Keep it clear until the world is up.
    constexpr uintptr_t kPendingPause = 0x00D3D664;
    auto* pending = reinterpret_cast<uint8_t*>(base + kPendingPause);
    AllowWrite(pending, 1);
    *pending = 0;
    uintptr_t menu = *reinterpret_cast<uintptr_t*>(base + kGotMobileMenu);
    g_mobileMenu = reinterpret_cast<void*>(menu);
    const int32_t current = *state;
    if (current == 7) {
        // +0x6d is New Game and +0x6c is the restart MainGameFunc turns
        // into DoGameRestart. Both stay clear so the story does not start.
        if (menu) {
            auto* flags = reinterpret_cast<uint8_t*>(menu + 0x6c);
            AllowWrite(flags, 2);
            flags[0] = 0;
            flags[1] = 0;
        }
        *state = 8;
        ENGINE_LOGI("Server start: gGameState 7 -> 8 without the New Game flag");
        return true;
    }
    if (current < 9) return true;
    // Idle can open the pause a frame after the state reaches 9. Keep
    // clearing it until that frame has happened and the menu is gone.
    static int seenPlaying = 0;
    ++seenPlaying;
    const int32_t screens = menu ? *reinterpret_cast<int32_t*>(menu + 0x24) : -1;
    const uintptr_t top = menu ? *reinterpret_cast<uintptr_t*>(menu + 0x30) : 0;
    if (seenPlaying == 30) {
        ENGINE_LOGI("After start: screens=%d top=%p pending cleared", screens, reinterpret_cast<void*>(top));
    }
    const bool menuUp = screens != 0 || top != 0;
    // Unload only frees menu textures. The pause screen stays until
    // RemoveTopScreen pops it, which is what the Resume button does.
    if (menuUp && g_removeTop && g_mobileMenu) {
        CloseMobileMenuOnGameThread(g_mobileMenu);
        if (seenPlaying < 4) ENGINE_LOGI("Closed the GTA pause menu");
    }
    if (g_endPause) g_endPause();
    if (seenPlaying > 120 && !menuUp) leftMenu = true;
    return true;
}
void ApplyServerSpawn(float x, float y, float z) {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return;
    QueueLocalPlacement(x, y, z, true);
    {
        std::lock_guard lock(g_remoteMutex);
        g_localShots.clear();
        g_localWeapon = {};
        g_localDrivebyRequest = 0;
        g_lastLocalShotAim = {};
    }
    g_health = 100.f;
    g_armor = 0.f;
    g_pendingLocalHealth = 100.f;
    g_pendingLocalArmor = 0.f;
    g_pendingDamage.clear();
}
void CopySpawn(float& x, float& y, float& z) {
    std::lock_guard lock(g_localPlacementMutex);
    x = g_localPlacement.x;
    y = g_localPlacement.y;
    z = g_localPlacement.z;
}
void CopyPose(float& x, float& y, float& z, float& heading) {
    x = g_poseX.load();
    y = g_poseY.load();
    z = g_poseZ.load();
    heading = g_poseH.load();
}
float CopyRadarHeading() { return g_radarH.load(); }
void CopyBody(float& x, float& y, float& z, float& heading,
    float& vx, float& vy, float& vz, float& health) {
    CopyPose(x, y, z, heading);
    vx = g_velX.load();
    vy = g_velY.load();
    vz = g_velZ.load();
    health = g_health.load();
}
float CopyLocalArmor() { return g_armor.load(); }
// Apply the outstanding damage from a shot replayed during a local network interruption.
void NoteDamage(uint32_t attacker, float amount, uint8_t weapon, uint8_t zone) {
    auto* ped = g_findPlayer ? static_cast<unsigned char*>(g_findPlayer(0)) : nullptr;
    if (!ped) return;
    // Same native vitals as SampleLocalPose (2.10: 0x37d93c and 0x444044 for armor).
    auto& health = *reinterpret_cast<float*>(ped + 0x6AC);
    auto& armor = *reinterpret_cast<float*>(ped + 0x6B4);
    if (!Combat::ApplyBackupDamage(amount, health, armor)) return;
    g_health = health;
    g_armor = armor;
    g_pendingDamage.record(attacker, weapon, zone);
    NoteLocalCombatDeath(ped, nullptr, attacker, weapon, zone);
}
Combat::DamageReport PeekDamage() { return g_pendingDamage.peek(); }
void AcknowledgeDamage(uint64_t serial) { g_pendingDamage.acknowledge(serial); }
// Commit the weapon and its input from the same packet before the game thread sees either.
static void ApplyWeaponControls(RemotePed& ped, uint32_t id, uint8_t buttons,
                                const PlayerWeaponSync::RemoteWeapon* weapon) {
    if (ped.buttons != buttons) {
        // What desktop sends for each move (e.g. a held key that kicks): 0x01 L1, 0x02 R1, 0x04 Square, 0x08 Cross,
        // 0x10 Circle, 0x20 Triangle, 0x40 ShockL, 0x80 PedWalk.
        static int changes = 0;
        if (changes < 80) {
            ++changes;
            ENGINE_LOGI("DIAG remote buttons id=%u 0x%02x -> 0x%02x", id, static_cast<unsigned>(ped.buttons),
                static_cast<unsigned>(buttons));
        }
    }
    ped.buttons = buttons;
    ped.fire = (buttons & PlayerWeaponSync::kFire) != 0;
    if (weapon) {
        ped.syncWeapon = *weapon;
        ++ped.weaponRevision;
        ++ped.aimRevision;
    }
}
void NoteRemoteAim(uint32_t id, float heading, float camera, int stickX, int stickY, uint8_t buttons, bool duck, const PlayerWeaponSync::RemoteWeapon* weapon) {
    if (id == 0 || id == 0xFFFFFFFFu || id == ServerWorld::LocalPlayer()) return;
    const float pi = 3.14159265f;
    while (heading > pi) heading -= 2.f * pi;
    while (heading < -pi) heading += 2.f * pi;
    const float along = -static_cast<float>(stickY) / 128.f;
    const float side = static_cast<float>(stickX) / 128.f;
    const float mag = std::sqrt(along * along + side * side);
    const float scale = mag > 0.7f ? 0.18f : 0.09f;
    std::lock_guard<std::mutex> lock(g_remoteMutex);
    auto& ped = g_remotePeds[id];
    ped.heading = heading;
    // A seated keysync supplies the aim only. Vehicle puresync owns the
    // selected weapon and its total ammo (CNetAPI::ReadKeysync 623-669).
    if (weapon && weapon->aim.inVehicle) {
        ped.syncWeapon.aim = weapon->aim;
        ++ped.aimRevision;
        ApplyWeaponControls(ped, id, buttons, nullptr);
    } else {
        ApplyWeaponControls(ped, id, buttons, weapon);
    }
    ped.duck = duck;
    // CClientPed::SetControllerState replaces the whole controller (CClientPed.cpp 1460-1484).
    // A zero stick overwrites the previous one. ReadKeysync does not call SetMoveSpeed.
    ped.stickX = stickX;
    ped.stickY = stickY;
    ped.cameraRotation = camera;
    ped.keysync = true;
    // With the controller hook the game moves the ped from the stick, so the
    // velocity fallback stays off while the stick is held. A zero stick clears
    // the stored speed PresentOnFoot turns into SetMoveState. ReadKeysync does
    // not call SetMoveSpeed; the body write is the next pure sync's SVelocitySync.
    if (mag > 0.08f) {
        if (!RemoteControlHookActive()) {
            // Inverse of CClientPed: stick Y negative is forward along the camera.
            const float input = std::atan2(-static_cast<float>(stickX), -static_cast<float>(stickY));
            const float desired = input - camera;
            ped.vx = -std::sin(desired) * scale;
            ped.vy = std::cos(desired) * scale;
        }
    } else {
        ped.vx = 0.f;
        ped.vy = 0.f;
    }
    ped.dirty = true;
}
PlayerWeaponSync::LocalWeapon CopyLocalWeapon() {
    std::lock_guard lock(g_remoteMutex);
    return g_localWeapon;
}
void NoteDriveby(uint32_t id, bool enabled) {
    const bool local = id == ServerWorld::LocalPlayer();
    std::lock_guard lock(g_remoteMutex);
    if (local) g_localDrivebyRequest = enabled ? 1 : 0;
    else g_remotePeds[id].gangDriveby = enabled;
}
bool IsDriveby(uint32_t id) {
    const bool local = id == ServerWorld::LocalPlayer();
    std::lock_guard lock(g_remoteMutex);
    if (local) return g_localDrivebyRequest >= 0 ? g_localDrivebyRequest != 0 : g_localWeapon.gangDriveby;
    auto it = g_remotePeds.find(id);
    return it != g_remotePeds.end() && it->second.gangDriveby;
}
void NoteVehicleCombat(uint32_t id, const PlayerWeaponSync::RemoteWeapon& weapon, uint8_t buttons, bool driveby) {
    if (!id || id == ServerWorld::LocalPlayer()) return;
    std::lock_guard lock(g_remoteMutex);
    auto& ped = g_remotePeds[id];
    ped.gangDriveby = driveby;
    ApplyWeaponControls(ped, id, driveby ? buttons : 0, &weapon);
}
void NoteRemotePlayerWeapon(uint32_t id, const PlayerWeaponSync::RemoteWeapon& weapon) {
    if (id == 0 || id == 0xFFFFFFFFu || id == ServerWorld::LocalPlayer()) return;
    std::lock_guard lock(g_remoteMutex);
    auto& ped = g_remotePeds[id];
    ped.syncWeapon = weapon;
    ++ped.weaponRevision;
    ++ped.aimRevision;
}
void QueueRemoteBullet(const Combat::SBulletSync& shot) {
    RemoteBullet bullet;
    bullet.localLife = g_localLife.load();
    bullet.shot = shot;
    bullet.generation = PedQueries::Generation(shot.shooter);
    if (!bullet.generation) return;
    bullet.deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(350);
    std::lock_guard lock(g_remoteMutex);
    if (g_remoteBullets.size() == kMaxRemoteBullets) g_remoteBullets.pop_front();
    g_remoteBullets.push_back(std::move(bullet));
}
bool RemoteFacingLocal(uint32_t id, float facing, float reach) {
    std::lock_guard<std::mutex> lock(g_remoteMutex);
    auto found = g_remotePeds.find(id);
    if (found == g_remotePeds.end()) return false;
    const float dx = g_poseX.load() - found->second.x;
    const float dy = g_poseY.load() - found->second.y;
    if (dx * dx + dy * dy > reach * reach) return false;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.05f) return true;
    const float fx = -std::sin(facing);
    const float fy = std::cos(facing);
    return (fx * dx + fy * dy) / len > 0.35f;
}
// The jump press is cleared only by a packet that carries it (CopyOnFoot reads it, the sender commits).
void ConsumeLocalJump(uint64_t serial) {
    if (serial) g_localJumpLatch.compare_exchange_strong(serial, 0);
}

void ConsumeLocalAttack(uint64_t serial) {
    if (serial) g_localAttackLatch.compare_exchange_strong(serial, 0);
}

void CopyOnFoot(float heading, float vx, float vy, OnFootKeys& keys) {
    keys.attackPress = g_localAttackLatch.load();
    keys.fire = g_localAttackHeld.load() || keys.attackPress;
    keys.duck = g_localDuck.load();
    keys.sprint = g_localSprint.load();
    keys.jumpPress = g_localJumpLatch.load();
    keys.jump = keys.jumpPress != 0;
    keys.padStickZero = false;
    const float pi = 3.14159265f;
    // CNetAPI::WritePlayerPuresync / WriteKeysync: SCameraRotationSync is GetCameraRotation,
    // the float CTaskSimplePlayerOnFoot subtracts from the stick angle (TheCamera+0x154).
    float camera = g_cameraRotValid.load() ? g_cameraRot.load() : 0.f;
    const float turns = pi * 2.f;
    camera = camera - turns * std::floor((camera + pi) / turns);
    keys.camera = camera;
    // CPad::GetPad(0) shorts. A released stick stays 0 even while CPhysical+0x68 is still moving.
    if (g_padStickValid.load()) {
        const int rawX = g_padStickX.load();
        const int rawY = g_padStickY.load();
        if (rawX == 0 && rawY == 0) {
            keys.stickX = 0;
            keys.stickY = 0;
            keys.padStickZero = true;
            return;
        }
        // SFullKeysyncSync stretches -128..128 into a char with *127/128 (SyncStructures.h 998-1001).
        auto wireStick = [](int raw) {
            const int scaled = static_cast<int>(std::lround(static_cast<float>(raw) * 127.f / 128.f));
            return std::clamp(scaled, -127, 127);
        };
        keys.stickX = wireStick(rawX);
        keys.stickY = wireStick(rawY);
        return;
    }
    const float speed = std::sqrt(vx * vx + vy * vy);
    if (speed < 0.02f) {
        keys.stickX = 0;
        keys.stickY = 0;
        return;
    }
    const float desired = std::atan2(-vx, vy);
    const float input = desired + camera;
    const float mag = speed > 0.16f ? 108.f : 59.f;
    keys.stickX = static_cast<int>(std::lround(std::clamp(-std::sin(input) * mag, -127.f, 127.f) * 127.f / 128.f));
    keys.stickY = static_cast<int>(std::lround(std::clamp(-std::cos(input) * mag, -127.f, 127.f) * 127.f / 128.f));
    (void)heading;
}
void NoteRemotePlayerSync(uint32_t id, float x, float y, float z, float heading,
    float vx, float vy, float vz, bool hasVelocity,
    int stickX, int stickY, float camera, uint8_t buttons, bool duck, const PlayerWeaponSync::RemoteWeapon& weapon,
    bool hasHealth, float health, bool hasArmor, float armor) {
    if (id == 0 || id == 0xFFFFFFFFu || id == ServerWorld::LocalPlayer()) return;
    std::lock_guard<std::mutex> lock(g_remoteMutex);
    auto& ped = g_remotePeds[id];
    ped.x = x;
    ped.y = y;
    ped.z = z;
    ped.heading = heading;
    ped.vx = vx;
    ped.vy = vy;
    ped.vz = vz;
    ped.syncMoveSpeed = hasVelocity;
    // SetControllerState replacement. Sticks are already in pad units (-128..128).
    ped.stickX = stickX;
    ped.stickY = stickY;
    ped.cameraRotation = camera;
    ApplyWeaponControls(ped, id, buttons, &weapon);
    ped.duck = duck;
    ped.keysync = true;
    ped.newTarget = true;
    if (hasHealth && std::isfinite(health)) {
        ped.health = health;
        ped.syncHealth = true;
    }
    if (hasArmor && std::isfinite(armor)) {
        ped.armor = armor;
        ped.syncArmor = true;
    }
    ped.dirty = true;
}
void NoteRemotePed(uint32_t id, float x, float y, float z, float heading, float vx, float vy, float vz, bool applyVelocity) {
    if (id == 0 || id == 0xFFFFFFFFu) return;
    std::lock_guard<std::mutex> lock(g_remoteMutex);
    auto& ped = g_remotePeds[id];
    ped.x = x;
    ped.y = y;
    ped.z = z;
    ped.heading = heading;
    // CPedSync::Packet_PedSync sets move speed only when ucFlags & 0x04
    // (CPedSync.cpp 434-437). A position-only packet must not clear the last speed.
    if (applyVelocity) {
        ped.vx = vx;
        ped.vy = vy;
        ped.vz = vz;
        ped.syncMoveSpeed = true;
    }
    ped.dirty = true;
}
void NoteObservedPedSync(uint32_t id, const float* position, const float* heading,
    const float* velocity, uint64_t intervalMs) {
    if (id == 0 || id == 0xFFFFFFFFu || ServerWorld::ModelOf(id) < 0) return;
    std::lock_guard lock(g_remoteMutex);
    RemotePed& ped = g_remotePeds[id];
    if (ped.hosted || (ped.hasAnimCache && !ped.animCache.updatePosition)) return;
    const bool validPosition = position && PlausiblePosition(position[0], position[1], position[2]);
    const bool validVelocity = velocity && std::isfinite(velocity[0]) && std::isfinite(velocity[1]) && std::isfinite(velocity[2]);
    if (validPosition || validVelocity) {
        const auto now = GameNowMs();
        const int64_t observed = ped.lastObserverMotionMs ? now - ped.lastObserverMotionMs : 0;
        const int64_t interval = std::clamp<int64_t>(std::max<int64_t>(intervalMs, observed), 400, 2000);
        ped.lastObserverMotionMs = now;
        ped.observerMotionUntilMs = now + 3 * interval;
    }
    if (validPosition) {
        const auto now = GameNowMs();
        const int64_t elapsed = now - ped.lastObserverPositionMs;
        if (!velocity && ped.lastObserverPositionMs && elapsed >= 50 && elapsed <= 3000) {
            // Desktop MTA velocity is displacement per 20 ms game step. Position-only
            // deltas may present a gait but must never inject speed into the physics body.
            float x = (position[0] - ped.x) * 20.f / elapsed;
            float y = (position[1] - ped.y) * 20.f / elapsed;
            const bool plausible = std::hypot(x, y) <= 0.5f;
            ped.vx = plausible ? x : 0.f;
            ped.vy = plausible ? y : 0.f;
        }
        ped.lastObserverPositionMs = now;
        ped.x = position[0];
        ped.y = position[1];
        ped.z = position[2];
        ped.observerHasPosition = true;
        ped.observerPositionInterval = intervalMs;
    }
    if (heading && std::isfinite(*heading)) {
        ped.heading = *heading;
        ped.observerHasHeading = true;
        ped.observerHeadingInterval = intervalMs;
    }
    if (validVelocity) {
        static const bool trace = Debug::Flag("traceobservergait");
        static int64_t nextSample = 0;
        if (trace && ped.lastObserverMotionMs >= nextSample) {
            nextSample = ped.lastObserverMotionMs + 2000;
            ENGINE_LOGI("Observer packet id=%u velocity=%.4f,%.4f position=%.2f,%.2f", id,
                velocity[0], velocity[1], ped.x, ped.y);
        }
        ped.vx = velocity[0];
        ped.vy = velocity[1];
        ped.vz = velocity[2];
        ped.syncMoveSpeed = true;
    }
}

void SetPedHost(uint32_t id, bool host) {
    if (id == 0 || id == 0xFFFFFFFFu) return;
    std::lock_guard<std::mutex> lock(g_remoteMutex);
    RemotePed& ped = g_remotePeds[id];
    ped.hosted = host;
    if (!host) ped.ownedControls = 0;
    ped.lastObserverPositionMs = 0;
    ped.lastObserverMotionMs = 0;
    ped.observerMotionUntilMs = 0;
    ped.observerPositionInterval = 0;
    ped.observerHeadingInterval = 0;
}
void ForgetRemote(uint32_t id) {
    std::lock_guard<std::mutex> lock(g_remoteMutex);
    g_forget.push_back(id);
}

void RpcSetPosition(uint32_t id, float x, float y, float z) {
    if (id == 0 || id == 0xFFFFFFFFu || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return;
    if (id == ServerWorld::LocalPlayer()) {
        // CClientPlayer::Teleport for the local player (CElementRPCs.cpp 133-150): run on the game thread.
        QueueLocalPlacement(x, y, z);
        return;
    }
    std::lock_guard<std::mutex> lock(g_remoteMutex);
    RemotePed& ped = g_remotePeds[id];
    ped.x = x;
    ped.y = y;
    ped.z = z;
    ped.teleportPosition = {x, y, z};
    ped.lastObserverPositionMs = 0;
    ped.observerMotionUntilMs = 0;
    ped.teleport = true;
    ped.observerHasPosition = true;
    ped.observerPositionInterval = 0;
}

void NoteRemoteWasted(uint32_t id, int weapon, int animGroup, int animId) {
    if (id == 0 || id == 0xFFFFFFFFu || id == ServerWorld::LocalPlayer()) return;
    std::lock_guard<std::mutex> lock(g_remoteMutex);
    RemotePed& ped = g_remotePeds[id];
    ped.killPending = true;
    ped.killWeapon = weapon;
    ped.killAnimGroup = animGroup;
    ped.killAnimId = animId;
}

void NoteLocalWasted(int weapon, int animGroup, int animId) {
    std::lock_guard<std::mutex> lock(g_remoteMutex);
    g_pendingLocalWasted.pending = true;
    g_pendingLocalWasted.life = g_localLife.load();
    g_pendingLocalWasted.weapon = weapon;
    g_pendingLocalWasted.animGroup = animGroup;
    g_pendingLocalWasted.animId = animId;
}

void NoteRemoteSpawn(uint32_t id) {
    if (id == 0 || id == 0xFFFFFFFFu || id == ServerWorld::LocalPlayer()) return;
    {
        std::lock_guard<std::mutex> lock(g_remoteMutex);
        RemotePed& ped = g_remotePeds[id];
        g_seatRequests.erase(id);
        ped.dead = false;
        ped.killPending = false;
        ped.syncWeapon = {};
        ped.gangDriveby = false;
        ++ped.weaponRevision;
        ++ped.aimRevision;
        ped.buttons = 0;
        ped.fire = false;
    }
    // CClientPlayer::Spawn: SetHealth(100), SetArmor(0) (remote players keep their locks at those values).
    LockPedHealth(id, 100.f);
    LockPedArmor(id, 0.f);
}

void RpcSetFrozen(uint32_t id, bool frozen) {
    if (id == 0 || id == 0xFFFFFFFFu || id == ServerWorld::LocalPlayer()) return;
    if (std::strcmp(ServerWorld::ElementKindName(id), "vehicle") == 0) {
        std::lock_guard lock(g_remoteMutex);
        g_remoteCars[id].frozen = frozen;
        return;
    }
    std::lock_guard<std::mutex> lock(g_remoteMutex);
    auto& ped = g_remotePeds[id];
    ped.frozen = frozen;
    ped.frozenChanged = true;
}

void RpcSetRotation(uint32_t id, float radians) {
    if (id == 0 || id == 0xFFFFFFFFu || id == ServerWorld::LocalPlayer() || !std::isfinite(radians)) return;
    std::lock_guard<std::mutex> lock(g_remoteMutex);
    RemotePed& ped = g_remotePeds[id];
    ped.heading = radians;
    ped.snapHeading = radians;
    ped.headingSnap = true;
    ped.observerHasHeading = true;
    ped.observerHeadingInterval = 0;
}

void RpcSetInterior(uint32_t id, uint8_t interior) {
    if (id == 0 || id == 0xFFFFFFFFu) return;
    if (id == ServerWorld::LocalPlayer()) {
        g_pendingLocalInterior = interior;
        return;
    }
    std::lock_guard<std::mutex> lock(g_remoteMutex);
    g_remotePeds[id].interior = interior;
}

// SET_ELEMENT_HEALTH / SET_PED_ARMOR: LockHealth when the ped's health is locked, otherwise SetHealth
// (CElementRPCs.cpp 471-535, CPedRPCs.cpp 42-60).
void RpcSetHealth(uint32_t id, float health) {
    if (id == 0 || id == 0xFFFFFFFFu || !std::isfinite(health)) return;
    if (id == ServerWorld::LocalPlayer()) {
        NoteLocalHealth(health);
        return;
    }
    bool healthLocked = false, armorLocked = false;
    float h = 0.f, a = 0.f;
    CopyPedVitalsLock(id, healthLocked, h, armorLocked, a);
    if (healthLocked) {
        LockPedHealth(id, health);
        return;
    }
    std::lock_guard<std::mutex> lock(g_remoteMutex);
    RemotePed& ped = g_remotePeds[id];
    ped.health = health;
    ped.syncHealth = true;
}

void RpcSetArmor(uint32_t id, float armor) {
    if (id == 0 || id == 0xFFFFFFFFu || !std::isfinite(armor)) return;
    if (id == ServerWorld::LocalPlayer()) {
        g_pendingLocalArmor = std::max(0.f, armor);
        return;
    }
    bool healthLocked = false, armorLocked = false;
    float h = 0.f, a = 0.f;
    CopyPedVitalsLock(id, healthLocked, h, armorLocked, a);
    if (armorLocked) {
        LockPedArmor(id, armor);
        return;
    }
    std::lock_guard<std::mutex> lock(g_remoteMutex);
    RemotePed& ped = g_remotePeds[id];
    ped.armor = armor;
    ped.syncArmor = true;
}

void QueuePedRpc(uint32_t id, const PedRpcOp& op) {
    if (id == 0 || id == 0xFFFFFFFFu) return;
    if (op.queryGeneration && !PedQueries::Current(id, op.queryGeneration)) return;
    std::lock_guard<std::mutex> lock(g_remoteMutex);
    // m_AnimationCache: SetPedAnimation stores the animation, KillAnimation clears it (also for the local player, whose
    // body is never re-created here; the cache is then unused).
    if (id != ServerWorld::LocalPlayer()) {
        if (op.kind == PedRpcOp::Kind::Animation) {
            RemotePed& ped = g_remotePeds[id];
            ped.hasAnimCache = true;
            ped.animCache = op;
        } else if (op.kind == PedRpcOp::Kind::KillAnimation) {
            auto found = g_remotePeds.find(id);
            if (found != g_remotePeds.end()) found->second.hasAnimCache = false;
        }
    }
    auto& queue = g_pedRpcOps[id];
    if (queue.size() >= kMaxPedRpcOps) {
        static int dropped = 0;
        if (dropped < 4) {
            ++dropped;
            ENGINE_LOGI("DIAG ped RPC queue full for id=%u: oldest op dropped (no body to run it)", id);
        }
        queue.pop_front();
    }
    queue.push_back(op);
}

void RpcSetAlpha(uint32_t id, uint8_t alpha) {
    if (id == 0 || id == 0xFFFFFFFFu) return;
    if (id == ServerWorld::LocalPlayer()) {
        g_localAlpha = alpha;
        return;
    }
    if (std::strcmp(ServerWorld::ElementKindName(id), "vehicle") == 0) {
        std::lock_guard lock(g_remoteMutex);
        g_remoteCars[id].alpha = alpha;
        return;
    }
    std::lock_guard<std::mutex> lock(g_remoteMutex);
    auto& ped = g_remotePeds[id];
    ped.alpha = alpha;
    ped.alphaChanged = true;
}

void RpcSetCollisions(uint32_t id, bool enabled) {
    if (id == 0 || id == 0xFFFFFFFFu) return;
    if (id == ServerWorld::LocalPlayer()) {
        g_localCollisions = enabled ? 1 : 0;
        return;
    }
    std::lock_guard<std::mutex> lock(g_remoteMutex);
    auto& ped = g_remotePeds[id];
    ped.collisions = enabled;
    ped.collisionsChanged = true;
}



void NoteRemoteClothes(uint32_t id, const std::vector<WorldRPG::Entity::PedClothes>& clothes) {
    if (id == 0 || id == 0xFFFFFFFFu || id == ServerWorld::LocalPlayer()) return;
    std::lock_guard<std::mutex> lock(g_remoteMutex);
    RemotePed& ped = g_remotePeds[id];
    // AddClothes per item replaces that slot (CClientPlayerClothes::AddClothes); a packet can carry a subset.
    for (const auto& item : clothes) {
        bool replaced = false;
        for (auto& have : ped.clothes) {
            if (have.type == item.type) {
                have = item;
                replaced = true;
            }
        }
        if (!replaced) ped.clothes.push_back(item);
    }
    ped.clothesDirty = true;
}

void SetPedVitals(uint32_t id, float health, float armor) {
    if (id == 0 || id == 0xFFFFFFFFu || id == ServerWorld::LocalPlayer()) return;
    if (!std::isfinite(health) || !std::isfinite(armor)) return;
    std::lock_guard<std::mutex> lock(g_remoteMutex);
    RemotePed& ped = g_remotePeds[id];
    ped.health = health;
    ped.armor = armor;
    ped.syncHealth = true;
    ped.syncArmor = true;
}

// Reconnect (P3.1): the network thread clears the record set; the game thread
// drops the bodies in ApplyRemotePeds on its next tick.
void RequestRemoteReset() {
    ++g_localLife;
    g_pendingDamage.clear();
    g_pendingLocalHealth = g_pendingLocalArmor = -1.f;
    {
        std::lock_guard lock(g_localPlacementMutex);
        g_appliedPlacement = g_requestedPlacement;
        g_pendingLocalInterior = -1;
    }
    {
        std::lock_guard<std::mutex> lock(g_remoteMutex);
        g_remotePeds.clear();
        g_forget.clear();
        g_streamedActors.clear();
        g_pendingLocalWasted = {};
        g_remoteResetPending = true;
    }
}
bool SetOwnedPedControl(uint32_t id, const char* control, bool enabled) {
    constexpr const char* names[] = {"forwards", "backwards", "left", "right", "walk", "sprint"};
    unsigned index = 0;
    while (index < std::size(names) && strcasecmp(control, names[index]) != 0) ++index;
    if (index == std::size(names) || !ServerWorld::ActorHosted(id)) return false;
    std::lock_guard lock(g_remoteMutex);
    auto found = g_remotePeds.find(id);
    if (found == g_remotePeds.end() || !found->second.hosted) return false;
    const uint8_t flag = 1u << index;
    if (enabled) found->second.ownedControls |= flag;
    else found->second.ownedControls &= ~flag;
    return true;
}

void NoteRemoteVehicle(uint32_t id, int model, float x, float y, float z, float heading) {
    if (id == 0 || id == 0xFFFFFFFFu || !PlausiblePosition(x, y, z)) return;
    std::lock_guard<std::mutex> lock(g_remoteMutex);
    auto& car = g_remoteCars[id];
    if (model >= 400) car.model = model;
    car.x = x;
    car.y = y;
    car.z = z;
    car.heading = heading;
    car.dirty = true;
    car.useUnoccupiedSync = false;
    car.observedVehicle.reset();
}
void NoteUnoccupiedVehicleSync(const VehicleSync::UnoccupiedUpdate& u) {
    std::lock_guard lock(g_remoteMutex);
    auto& car = g_remoteCars[u.vehicleId];
    if (car.hosted) return;
    car.useUnoccupiedSync = true;
    if (!car.observedVehicle || car.observedVehicle->timeContext != u.timeContext)
        car.observedVehicle.emplace();
    auto& pending = *car.observedVehicle;
    pending.timeContext = u.timeContext;
    if (u.syncPosition && PlausiblePosition(u.position.x, u.position.y, u.position.z)) {
        pending.syncPosition = true;
        pending.position = u.position;
        car.x = u.position.x;
        car.y = u.position.y;
        car.z = u.position.z;
        car.dirty = true;
    }
    if (u.syncRotation) {
        pending.syncRotation = true;
        pending.rotation = u.rotation;
    }
    if (u.syncVelocity) {
        pending.syncVelocity = true;
        pending.velocity = u.velocity;
    }
    if (u.syncTurnVelocity) {
        pending.syncTurnVelocity = true;
        pending.turnVelocity = u.turnVelocity;
    }
    if (u.syncHealth) {
        pending.syncHealth = true;
        pending.health = u.health;
    }
    pending.engineOn = u.engineOn;
}

void RpcSetVehiclePosition(uint32_t id, float x, float y, float z) {
    if (!PlausiblePosition(x, y, z)) return;
    std::lock_guard lock(g_remoteMutex);
    auto& car = g_remoteCars[id];
    car.x = x;
    car.y = y;
    car.z = z;
    car.dirty = true;
    car.seed.pending = true;
    car.seed.hasPose = true;
    car.seed.x = x;
    car.seed.y = y;
    car.seed.z = z;
    if (car.observedVehicle) car.observedVehicle->syncPosition = false;
}

void RpcSetVehicleHealth(uint32_t id, float health) {
    std::lock_guard lock(g_remoteMutex);
    auto& car = g_remoteCars[id];
    car.seed.pending = true;
    car.seed.hasHealth = true;
    car.seed.health = health;
    if (car.observedVehicle) car.observedVehicle->syncHealth = false;
}

void RpcSetVehicleRotation(uint32_t id, float x, float y, float z) {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return;
    std::lock_guard lock(g_remoteMutex);
    auto& car = g_remoteCars[id];
    car.seed.pending = true;
    car.seed.hasRotation = true;
    car.seed.rotX = x;
    car.seed.rotY = y;
    car.seed.rotZ = z;
    if (car.observedVehicle) car.observedVehicle->syncRotation = false;
}

void RpcSetVehicleVelocity(uint32_t id, float x, float y, float z, bool angular) {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return;
    std::lock_guard lock(g_remoteMutex);
    auto& car = g_remoteCars[id];
    float* speed = angular ? car.seed.turn : car.seed.move;
    speed[0] = x;
    speed[1] = y;
    speed[2] = z;
    car.seed.pending = true;
    if (angular) {
        car.seed.hasTurnSpeed = true;
        if (car.observedVehicle) car.observedVehicle->syncTurnVelocity = false;
    } else {
        car.seed.hasMoveSpeed = true;
        if (car.observedVehicle) car.observedVehicle->syncVelocity = false;
    }
}

// Desktop CUnoccupiedVehicleSync::Packet_UnoccupiedVehicleStartSync (CUnoccupiedVehicleSync.cpp 104-158): target
// position, target rotation, move speed, turn speed and health are set on the vehicle before AddVehicle. The values are
// applied to the body on the game thread (ApplyCarSeed). rot is in degrees.
void NoteVehicleStartSync(uint32_t id, const float pos[3], const float rot[3], const float move[3], const float turn[3],
    float health) {
    if (id == 0 || id == 0xFFFFFFFFu || !pos || !rot || !move || !turn) return;
    if (!PlausiblePosition(pos[0], pos[1], pos[2])) return;
    std::lock_guard<std::mutex> lock(g_remoteMutex);
    auto& car = g_remoteCars[id];
    car.x = pos[0];
    car.y = pos[1];
    car.z = pos[2];
    car.heading = rot[2] * 3.14159265f / 180.f;
    car.dirty = true;
    car.useUnoccupiedSync = false;
    car.observedVehicle.reset();
    CarSeed& seed = car.seed;
    seed.hasPose = true;
    seed.x = pos[0];
    seed.y = pos[1];
    seed.z = pos[2];
    seed.hasRotation = true;
    seed.rotX = rot[0];
    seed.rotY = rot[1];
    seed.rotZ = rot[2];
    seed.hasMoveSpeed = true;
    seed.hasTurnSpeed = true;
    for (int i = 0; i < 3; ++i) {
        seed.move[i] = std::isfinite(move[i]) ? move[i] : 0.f;
        seed.turn[i] = std::isfinite(turn[i]) ? turn[i] : 0.f;
    }
    seed.hasHealth = true;
    seed.health = health;
    seed.pending = true;
}
// Desktop CPacketHandler::Packet_Vehicle_InOut seats or unseats the ped. The request is applied in ApplyRemoteSeats.
void NoteRemoteSeat(uint32_t pedId, uint32_t vehicleId, uint8_t seat) {
    if (pedId == 0 || pedId == 0xFFFFFFFFu || vehicleId == 0 || vehicleId == 0xFFFFFFFFu) return;
    std::lock_guard<std::mutex> lock(g_remoteMutex);
    RemoteSeatRequest& req = g_seatRequests[pedId];
    req.vehicleId = vehicleId;
    req.seat = seat;
    req.seated = true;
    req.serial = ++g_seatSerial;
}
void NoteRemoteUnseat(uint32_t pedId) {
    if (pedId == 0 || pedId == 0xFFFFFFFFu) return;
    std::lock_guard<std::mutex> lock(g_remoteMutex);
    RemoteSeatRequest& req = g_seatRequests[pedId];
    if (auto it = g_remotePeds.find(pedId); it != g_remotePeds.end()) {
        it->second.gangDriveby = false;
        it->second.buttons = 0;
        it->second.fire = false;
        it->second.syncWeapon.aim = {};
        ++it->second.weaponRevision;
        ++it->second.aimRevision;
    }
    req.vehicleId = kNoVehicle;
    req.seat = 0;
    req.seated = false;
    req.serial = ++g_seatSerial;
}
void NoteRemoteVehicleMotion(uint32_t id, float mx, float my, float mz, float tx, float ty, float tz) {
    if (id == 0 || id == 0xFFFFFFFFu) return;
    std::lock_guard<std::mutex> lock(g_remoteMutex);
    // Only a car that already has a pose (NoteRemoteVehicle from the same packet, or VEHICLE_START). Creating one here
    // would let CopyRemotePose report a car at the origin.
    auto found = g_remoteCars.find(id);
    if (found == g_remoteCars.end()) return;
    RemoteCar& car = found->second;
    car.moveX = mx;
    car.moveY = my;
    car.moveZ = mz;
    car.turnX = tx;
    car.turnY = ty;
    car.turnZ = tz;
    car.motionPending = true;
}

bool CopyRemotePose(uint32_t id, float& x, float& y, float& z) {
    std::lock_guard<std::mutex> lock(g_remoteMutex);
    auto found = g_remotePeds.find(id);
    if (found != g_remotePeds.end()) {
        x = found->second.shown ? found->second.shownX : found->second.x;
        y = found->second.shown ? found->second.shownY : found->second.y;
        z = found->second.shown ? found->second.shownZ : found->second.z;
        return true;
    }
    auto car = g_remoteCars.find(id);
    if (car == g_remoteCars.end()) return false;
    x = car->second.shown ? car->second.shownX : car->second.x;
    y = car->second.shown ? car->second.shownY : car->second.y;
    z = car->second.shown ? car->second.shownZ : car->second.z;
    return true;
}
bool CopyRemotePedMotion(uint32_t id, float& heading, float& vx, float& vy, float& vz) {
    std::lock_guard lock(g_remoteMutex);
    auto found = g_remotePeds.find(id);
    if (found == g_remotePeds.end() || !found->second.shown || !found->second.serverPed) return false;
    const auto& ped = found->second;
    heading = ped.shownH;
    vx = ped.vx;
    vy = ped.vy;
    vz = ped.vz;
    return true;
}
int CopyHostedPeds(HostedPed* out, int capacity) {
    if (!out || capacity <= 0) return 0;
    std::lock_guard<std::mutex> lock(g_remoteMutex);
    const int count = std::min(capacity, static_cast<int>(g_hostedSnap.size()));
    for (int i = 0; i < count; ++i) out[i] = g_hostedSnap[static_cast<size_t>(i)];
    return count;
}
int CopyHostedCars(HostedCar* out, int max) {
    if (!out || max <= 0) return 0;
    std::lock_guard<std::mutex> lock(g_remoteMutex);
    const int count = std::min(max, static_cast<int>(g_hostedCarSnap.size()));
    for (int i = 0; i < count; ++i) out[i] = g_hostedCarSnap[static_cast<size_t>(i)];
    return count;
}
// VEHICLE_INOUT source queue (see WatchLocalVehicle). The network thread drains it from Pulse.
bool ConsumeLocalVehicleEvent(LocalVehicleEvent& out) {
    std::lock_guard<std::mutex> lock(g_remoteMutex);
    if (g_localVehicleEvents.empty()) return false;
    out = g_localVehicleEvents.front();
    g_localVehicleEvents.erase(g_localVehicleEvents.begin());
    return true;
}
uint32_t LocalVehicleId() {
    std::lock_guard<std::mutex> lock(g_remoteMutex);
    return g_localVehicleId;
}
bool CopyLocalCar(LocalCarState& out) {
    std::lock_guard<std::mutex> lock(g_remoteMutex);
    if (!g_localCarValid) return false;
    out = g_localCar;
    return true;
}

void AcknowledgeVehicleFire(uint64_t serial) {
    if (!serial) return;
    std::lock_guard lock(g_remoteMutex);
    if (g_localCar.firePress == serial) g_localCar.firePress = 0;
}

std::vector<VehicleTouchRect> CopyVehicleTouchControlBounds() {
    std::lock_guard<std::mutex> lock(g_remoteMutex);
    return g_vehicleTouchBounds;
}
// Desktop has no queue: CClientGame::PostWeaponFire sends at once. The queue only bridges the game thread
// (CWeapon::Fire hook) and the network thread (SendBulletSync). At the cap the oldest shot is dropped.
void NoteLocalShot(int weapon, const float* start, const float* end, const Combat::BulletHit& hit) {
    if (!end) return;
    LocalShot shot;
    shot.hit = hit;
    shot.weapon = weapon;
    if (start) {
        shot.sx = start[0];
        shot.sy = start[1];
        shot.sz = start[2];
    } else {
        float heading = 0.f;
        CopyPose(shot.sx, shot.sy, shot.sz, heading);
    }
    shot.ex = end[0];
    shot.ey = end[1];
    shot.ez = end[2];
    PlayerWeaponSync::Aim ray;
    if (!PlayerWeaponSync::SetAimLine(ray, {shot.sx, shot.sy, shot.sz}, {shot.ex, shot.ey, shot.ez})) return;
    // CClientGame::BulletFire / multiplayer_shotsync.cpp:1585-1593, 1670-1684.
    // FireInstantHit 0x7018b0 reads CWeaponInfo::m_fWeaponRange at +0x08.
    const float range = NativeWeaponRange(weapon);
    if (range > 0.f && std::hypot(shot.ex - shot.sx, shot.ey - shot.sy, shot.ez - shot.sz) > range) {
        shot.ex = shot.sx + ray.direction.x * range;
        shot.ey = shot.sy + ray.direction.y * range;
        shot.ez = shot.sz + ray.direction.z * range;
    }
    std::lock_guard<std::mutex> lock(g_remoteMutex);
    if (g_sampledLife != g_localLife.load()) return;
    g_lastLocalShotAim = ray;
    g_lastLocalShotWeapon = weapon;
    g_lastLocalShotTime = std::chrono::steady_clock::now();
    if (g_localShots.size() >= kMaxLocalShots) g_localShots.erase(g_localShots.begin());
    shot.serial = ++g_localShotSerial;
    g_localShots.push_back(shot);
}
bool ConsumeHostedWasted(HostedWasted& out) {
    std::lock_guard<std::mutex> lock(g_remoteMutex);
    if (g_hostedWasted.empty()) return false;
    out = g_hostedWasted.front();
    g_hostedWasted.erase(g_hostedWasted.begin());
    return true;
}

bool PeekLocalShot(LocalShot& out) {
    std::lock_guard<std::mutex> lock(g_remoteMutex);
    if (g_localShots.empty()) return false;
    out = g_localShots.front();
    return true;
}
void FinishLocalShot(uint64_t serial) {
    std::lock_guard lock(g_remoteMutex);
    if (!g_localShots.empty() && g_localShots.front().serial == serial)
        g_localShots.erase(g_localShots.begin());
}

#else
bool InstallServerSessionHook() { return false; }
bool BeginServerSession() { return false; }
void SetAmbientTrafficEnabled(bool, bool) {}
bool ServerSessionLive() { return false; }
bool LocalPlacementPending() { return false; }
void ApplyServerSpawn(float, float, float) {}
void CopySpawn(float& x, float& y, float& z) { x = y = z = 0.f; }
void CopyPose(float& x, float& y, float& z, float& heading) { x = y = z = heading = 0.f; }
float CopyRadarHeading() { return 0.f; }
void CopyBody(float& x, float& y, float& z, float& heading, float& vx, float& vy, float& vz, float& health) {
    x = y = z = heading = vx = vy = vz = 0.f;
    health = 100.f;
}
float CopyLocalArmor() { return 0.f; }
bool FindGroundZFor3DPosition(float, float, float, float&) { return false; }
bool IsAmbientSphereVisible(float, float, float, float, bool&) { return false; }
void NoteDamage(uint32_t, float, uint8_t, uint8_t) {}
Combat::DamageReport PeekDamage() { return {}; }
void AcknowledgeDamage(uint64_t) {}
void NoteRemoteAim(uint32_t, float, float, int, int, uint8_t, bool, const PlayerWeaponSync::RemoteWeapon*) {}
PlayerWeaponSync::LocalWeapon CopyLocalWeapon() { return {}; }
void NoteDriveby(uint32_t, bool) {}
bool IsDriveby(uint32_t) { return false; }
void NoteVehicleCombat(uint32_t, const PlayerWeaponSync::RemoteWeapon&, uint8_t, bool) {}
void NoteRemotePlayerWeapon(uint32_t, const PlayerWeaponSync::RemoteWeapon&) {}
void QueueRemoteBullet(const Combat::SBulletSync&) {}
bool IsRemoteNetworkPlayer(void*) { return false; }
uint32_t EntityElementId(void*) { return 0xFFFFFFFFu; }
float NativeWeaponRange(int) { return 0.f; }
bool CopyPedQuery(uint32_t id, PedQueries::Snapshot& out) { return PedQueries::Copy(id, out); }
bool CopySpatialScene(std::vector<Spatial::Element>&) { return false; }
bool RemoteFacingLocal(uint32_t, float, float) { return false; }
void CopyOnFoot(float, float, float, OnFootKeys& keys) { keys = {}; }
void ConsumeLocalJump(uint64_t) {}
void ConsumeLocalAttack(uint64_t) {}
void NoteRemotePlayerSync(uint32_t, float, float, float, float, float, float, float, bool, int, int, float, uint8_t, bool, const PlayerWeaponSync::RemoteWeapon&, bool, float, bool, float) {}
void NoteRemotePed(uint32_t, float, float, float, float, float, float, float, bool) {}
void SetPedHost(uint32_t, bool) {}
void NoteObservedPedSync(uint32_t, const float*, const float*, const float*, uint64_t) {}
void ForgetRemote(uint32_t) {}
void RequestRemoteReset() {}
void NoteRemoteVehicle(uint32_t, int, float, float, float, float) {}
void NoteVehicleStartSync(uint32_t, const float*, const float*, const float*, const float*, float) {}
void NoteRemoteSeat(uint32_t, uint32_t, uint8_t) {}
void NoteRemoteUnseat(uint32_t) {}
void NoteRemoteVehicleMotion(uint32_t, float, float, float, float, float, float) {}
void NoteUnoccupiedVehicleSync(const VehicleSync::UnoccupiedUpdate&) {}
void RpcSetVehiclePosition(uint32_t, float, float, float) {}
void RpcSetVehicleHealth(uint32_t, float) {}
void RpcSetVehicleRotation(uint32_t, float, float, float) {}
void RpcSetVehicleVelocity(uint32_t, float, float, float, bool) {}
bool SetOwnedPedControl(uint32_t, const char*, bool) { return false; }
bool StartPedKillOnFoot(uint32_t, uint32_t) { return false; }
bool PresentPedWeaponShot(uint32_t, unsigned, float, float, float) { return false; }
bool StartPedWander(uint32_t, int, int, bool) { return false; }
bool StartPedDriveWander(uint32_t, uint32_t, float, int) { return false; }
bool ScanTrafficObstacle(uint32_t, bool, float, TrafficObstacleScan&) { return false; }
bool IsTrafficPedTaskActive(uint32_t, TrafficPedTask) { return false; }
bool KillNativePedTask(uint32_t, bool, int, bool) { return false; }
bool IsNativeActorStreamed(uint32_t) { return false; }
bool HasNativeTrafficCapability(const char*) { return false; }
bool SetTrafficVehicleVelocity(uint32_t, float, float, float, bool) { return false; }
bool SetTrafficVehicleRotation(uint32_t, float, float, float) { return false; }
bool IsTrafficVehicleOnGround(uint32_t) { return false; }
bool SetTrafficVehicleLoadCollision(uint32_t, bool) { return false; }
bool CopyTrafficVehicleState(uint32_t, HostedCar&) { return false; }
bool SetTrafficVehicleEngine(uint32_t, bool) { return false; }
bool CopyRemotePose(uint32_t, float& x, float& y, float& z) { x = y = z = 0.f; return false; }
bool CopyRemotePedMotion(uint32_t, float&, float&, float&, float&) { return false; }
int CopyHostedPeds(HostedPed*, int) { return 0; }
int CopyHostedCars(HostedCar*, int) { return 0; }
bool ConsumeLocalVehicleEvent(LocalVehicleEvent&) { return false; }
uint32_t LocalVehicleId() { return 0xFFFFFFFFu; }
void AcknowledgeVehicleFire(uint64_t) {}
bool CopyLocalCar(LocalCarState& out) {
    out = LocalCarState{};
    return false;
}
void NoteLocalShot(int, const float*, const float*, const Combat::BulletHit&) {}
void FinishLocalShot(uint64_t) {}
bool PeekLocalShot(LocalShot& out) {
    out = LocalShot{};
    return false;
}
void SetPedNativeTaskLocomotion(uint32_t, unsigned int) {}
void SetPedNativeTaskWeapon(uint32_t, uint8_t) {}
void SetPedNativeTaskAnimation(uint32_t, uint16_t, uint16_t) {}
void SetPedNativeTaskPhysicalTakeover(uint32_t, unsigned int, uint16_t, uint16_t, float) {}
void SetPedCameraRotation(uint32_t, float) {}
void SetPedOnFire(uint32_t, bool) {}
void SetPedInWater(uint32_t, bool) {}
void KillPedAnimation(uint32_t) {}
void LockPedHealth(uint32_t, float) {}
void LockPedArmor(uint32_t, float) {}
void LockPedHealthCurrent(uint32_t) {}
void LockPedArmorCurrent(uint32_t) {}
void ResolvePedVitalsLockCurrent(uint32_t, float, float) {}
void UnlockPedVitals(uint32_t) {}
void ForgetPedNativeTaskState(uint32_t) {}
void SetPedVitals(uint32_t, float, float) {}
void NoteRemoteClothes(uint32_t, const std::vector<WorldRPG::Entity::PedClothes>&) {}
void RpcSetPosition(uint32_t, float, float, float) {}
void RpcSetFrozen(uint32_t, bool) {}
void NoteRemoteWasted(uint32_t, int, int, int) {}
void NoteLocalWasted(int, int, int) {}
bool CopyLocalCombatDeath(uint64_t&, HostedWasted&) { return false; }
bool ConsumeHostedWasted(HostedWasted&) { return false; }
void NoteRemoteSpawn(uint32_t) {}
void RpcSetRotation(uint32_t, float) {}
void RpcSetInterior(uint32_t, uint8_t) {}
void RpcSetHealth(uint32_t, float) {}
void RpcSetArmor(uint32_t, float) {}
void QueuePedRpc(uint32_t, const PedRpcOp&) {}
void RpcSetAlpha(uint32_t, uint8_t) {}
void RpcSetCollisions(uint32_t, bool) {}
bool CopyPedVitalsLock(uint32_t, bool& healthLocked, float& health, bool& armorLocked, float& armor) {
    healthLocked = false;
    armorLocked = false;
    health = 0.f;
    armor = 0.f;
    return false;
}
#endif

// ---------------------------------------------------------------------------
// EngineOffsets factory
// ---------------------------------------------------------------------------

EngineOffsets EngineOffsets::forVersion(const char* version) {
    EngineOffsets offsets{};
    std::memset(&offsets, 0, sizeof(offsets));

    if (!version) return offsets;

    // Source of truth: GTASAOffsets (verified ARM64 v2.10 table extracted from
    // the SA-MP-2.10 source and cross-checked against libGTASA_v210.so).
    //
    // Only entries whose ARM64 call signature is proven to match the
    // corresponding typedef in GameEngine::Impl are mapped here. Entries are
    // deliberately left zero when the verified table documents a DIFFERENT
    // function or an unproven signature, because calling them through the
    // wrong prototype would corrupt the engine state or segfault:
    //  - requestModel: table entry 0x391FF0 is CStreaming::RemoveModel
    //    (the exported CStreaming::RequestModel sits at 0x3949e0 — pending
    //    ABI probe before wiring). Model requests go through ScriptCommand.
    //  - getPlayerPed: table entry 0x671BBC is FindPlayerSpeed, not a
    //    FindPlayerPed accessor.
    //  - ped/vehicle/object create+delete and position setters: table entries
    //    are CWorld::Add/Remove (CEntity* parameter) and PLT hooks with
    //    engine-internal signatures; none matches the handle-based typedefs.
    if (std::strcmp(version, "2.10") == 0) {
        const GTASAOffsets table = GTASAOffsets::forVersion("2.10");

        offsets.loadAllModels  = table.loadAllModels;   // void()            @ 0x396D1C
        offsets.hasModelLoaded = table.hasModelLoaded;  // bool(int)         @ 0x395098

        ENGINE_LOGI("EngineOffsets: v2.10 mapped %d/%d signature-proven offsets "
                    "(%d verified offsets available in GTASAOffsets)",
                    (offsets.loadAllModels != 0) + (offsets.hasModelLoaded != 0), 2,
                    table.countValidOffsets());
    } else {
        ENGINE_LOGE("EngineOffsets: unknown version '%s', all offsets zero", version);
    }

    return offsets;
}

// ---------------------------------------------------------------------------
// GameEngine implementation
// ---------------------------------------------------------------------------

struct GameEngine::Impl {
    bool initialized = false;
    std::atomic<bool> paused{false};
    EngineOffsets offsets{};
    Hook::SymbolResolver resolver;
    uintptr_t gtasaBase = 0;
#ifdef __ANDROID__
    // Handle do dlopen("libGTASA.so"). Mantido aberto durante toda a vida do
    // engine: fechar descarregaria os hooks que apontam para dentro dessa lib.
    void* gtasaHandle = nullptr;
#endif

    // Function pointer typedefs for engine calls
    using CreatePedFn = uint32_t (*)(int modelId, float x, float y, float z, float heading);
    using DeletePedFn = void (*)(uint32_t handle);
    using SetPosFn = void (*)(uint32_t handle, float x, float y, float z);
    using GetPosFn = void (*)(uint32_t handle, float* x, float* y, float* z);
    using SetRotFn = void (*)(uint32_t handle, float rx, float ry, float rz);
    using SetHealthFn = void (*)(uint32_t handle, float health);
    using GetHealthFn = float (*)(uint32_t handle);
    using CreateVehicleFn = uint32_t (*)(int modelId, float x, float y, float z, float heading);
    using DeleteVehicleFn = void (*)(uint32_t handle);
    using SetVelFn = void (*)(uint32_t handle, float vx, float vy, float vz);
    using CreateObjectFn = uint32_t (*)(int modelId, float x, float y, float z);
    using DeleteObjectFn = void (*)(uint32_t handle);
    using RequestModelFn = void (*)(int modelId);
    using LoadModelsFn = void (*)();
    using HasModelLoadedFn = bool (*)(int modelId);
    using GetPlayerPedFn = uint32_t (*)();
    using SetPlayerControlFn = void (*)(bool enable);

    template<typename Fn>
    Fn getFunction(uintptr_t offset) const {
        if (offset == 0 || gtasaBase == 0) return nullptr;
        return reinterpret_cast<Fn>(gtasaBase + offset);
    }
};

GameEngine::GameEngine() : pImpl_(std::make_unique<Impl>()) {}
GameEngine::~GameEngine() = default;

bool GameEngine::initialize(const EngineOffsets& offsets) {
#ifdef __ANDROID__
    // O libGTASA.so e' empacotado em jniLibs mas NAO e' linkado pelo build
    // (nao esta em target_link_libraries). Sem um dlopen explicito, ele nunca
    // entra na memoria do processo e getLibraryBaseAddress devolve 0.
    // RTLD_NOW | RTLD_GLOBAL: GLOBAL e' necessario para que os simbolos do
    // libGTASA fiquem visiveis a dlsym dos nossos hooks.
    pImpl_->gtasaBase = Hook::MemoryPatcher::getLibraryBaseAddress("libGTASA.so");
    if (pImpl_->gtasaBase == 0) {
        GTASA_LOG("libGTASA.so nao carregado; tentando dlopen");
        void* h = dlopen("libGTASA.so", RTLD_NOW | RTLD_GLOBAL);
        if (!h) {
            const char* err = dlerror();
            ENGINE_LOGE("GameEngine::initialize: dlopen(libGTASA.so) falhou: %s",
                        err ? err : "(sem detalhe)");
            return false;
        }
        GTASA_LOG("libGTASA.so carregado em %p", h);
        pImpl_->gtasaHandle = h;
        pImpl_->gtasaBase = Hook::MemoryPatcher::getLibraryBaseAddress("libGTASA.so");
    }
    if (pImpl_->gtasaBase == 0) {
        ENGINE_LOGE("GameEngine::initialize: libGTASA.so not found in memory");
        return false;
    }

    pImpl_->offsets = offsets;
    pImpl_->initialized = true;

    ENGINE_LOGI("GameEngine::initialize: libGTASA.so base=%p, hooks ready",
                reinterpret_cast<void*>(pImpl_->gtasaBase));
    return true;
#else
    (void)offsets;
    ENGINE_LOGE("GameEngine::initialize: non-Android platform, stub only");
    return false;
#endif
}

bool GameEngine::isReady() const {
    return pImpl_->initialized && pImpl_->gtasaBase != 0;
}

void GameEngine::pause() {
    pImpl_->paused.store(true);
    ENGINE_LOGI("GameEngine: paused");
}

void GameEngine::resume() {
    pImpl_->paused.store(false);
    ENGINE_LOGI("GameEngine: resumed");
}

bool GameEngine::isPaused() const {
    return pImpl_->paused.load();
}

// --- Ped operations ---

PedHandle GameEngine::spawnPed(int modelId, float x, float y, float z, float heading) {
    auto fn = pImpl_->getFunction<Impl::CreatePedFn>(pImpl_->offsets.createPed);
    if (!fn) {
        ENGINE_LOGE("spawnPed: function not available");
        return INVALID_PED;
    }
    PedHandle h = fn(modelId, x, y, z, heading);
    ENGINE_LOGI("spawnPed: model=%d pos=(%.1f,%.1f,%.1f) -> handle=%u", modelId, x, y, z, h);
    return h;
}

void GameEngine::deletePed(PedHandle handle) {
    auto fn = pImpl_->getFunction<Impl::DeletePedFn>(pImpl_->offsets.deletePed);
    if (fn) fn(handle);
}

void GameEngine::setPedPosition(PedHandle handle, float x, float y, float z) {
    auto fn = pImpl_->getFunction<Impl::SetPosFn>(pImpl_->offsets.setPedPosition);
    if (fn) fn(handle, x, y, z);
}

void GameEngine::getPedPosition(PedHandle handle, float& x, float& y, float& z) {
    auto fn = pImpl_->getFunction<Impl::GetPosFn>(pImpl_->offsets.getPedPosition);
    if (fn) fn(handle, &x, &y, &z);
    else { x = y = z = 0.0f; }
}

void GameEngine::setPedRotation(PedHandle handle, float rx, float ry, float rz) {
    auto fn = pImpl_->getFunction<Impl::SetRotFn>(pImpl_->offsets.setPedRotation);
    if (fn) fn(handle, rx, ry, rz);
}

void GameEngine::setPedHealth(PedHandle handle, float health) {
    auto fn = pImpl_->getFunction<Impl::SetHealthFn>(pImpl_->offsets.setPedHealth);
    if (fn) fn(handle, health);
}

float GameEngine::getPedHealth(PedHandle handle) {
    auto fn = pImpl_->getFunction<Impl::GetHealthFn>(pImpl_->offsets.getPedHealth);
    return fn ? fn(handle) : 0.0f;
}

// --- Vehicle operations ---

VehicleHandle GameEngine::spawnVehicle(int modelId, float x, float y, float z, float heading) {
    auto fn = pImpl_->getFunction<Impl::CreateVehicleFn>(pImpl_->offsets.createVehicle);
    if (!fn) {
        ENGINE_LOGE("spawnVehicle: function not available");
        return INVALID_VEHICLE;
    }
    VehicleHandle h = fn(modelId, x, y, z, heading);
    ENGINE_LOGI("spawnVehicle: model=%d pos=(%.1f,%.1f,%.1f) -> handle=%u", modelId, x, y, z, h);
    return h;
}

void GameEngine::deleteVehicle(VehicleHandle handle) {
    auto fn = pImpl_->getFunction<Impl::DeleteVehicleFn>(pImpl_->offsets.deleteVehicle);
    if (fn) fn(handle);
}

void GameEngine::setVehiclePosition(VehicleHandle handle, float x, float y, float z) {
    auto fn = pImpl_->getFunction<Impl::SetPosFn>(pImpl_->offsets.setVehiclePosition);
    if (fn) fn(handle, x, y, z);
}

void GameEngine::getVehiclePosition(VehicleHandle handle, float& x, float& y, float& z) {
    auto fn = pImpl_->getFunction<Impl::GetPosFn>(pImpl_->offsets.getVehiclePosition);
    if (fn) fn(handle, &x, &y, &z);
    else { x = y = z = 0.0f; }
}

void GameEngine::setVehicleRotation(VehicleHandle handle, float rx, float ry, float rz) {
    auto fn = pImpl_->getFunction<Impl::SetRotFn>(pImpl_->offsets.setVehicleRotation);
    if (fn) fn(handle, rx, ry, rz);
}

void GameEngine::setVehicleVelocity(VehicleHandle handle, float vx, float vy, float vz) {
    auto fn = pImpl_->getFunction<Impl::SetVelFn>(pImpl_->offsets.setVehicleVelocity);
    if (fn) fn(handle, vx, vy, vz);
}

void GameEngine::setVehicleHealth(VehicleHandle handle, float health) {
    auto fn = pImpl_->getFunction<Impl::SetHealthFn>(pImpl_->offsets.setVehicleHealth);
    if (fn) fn(handle, health);
}

// --- Object operations ---

ObjectHandle GameEngine::spawnObject(int modelId, float x, float y, float z) {
    auto fn = pImpl_->getFunction<Impl::CreateObjectFn>(pImpl_->offsets.createObject);
    if (!fn) return INVALID_OBJECT;
    return fn(modelId, x, y, z);
}

void GameEngine::deleteObject(ObjectHandle handle) {
    auto fn = pImpl_->getFunction<Impl::DeleteObjectFn>(pImpl_->offsets.deleteObject);
    if (fn) fn(handle);
}

void GameEngine::setObjectPosition(ObjectHandle handle, float x, float y, float z) {
    auto fn = pImpl_->getFunction<Impl::SetPosFn>(pImpl_->offsets.setObjectPosition);
    if (fn) fn(handle, x, y, z);
}

void GameEngine::setObjectRotation(ObjectHandle handle, float rx, float ry, float rz) {
    auto fn = pImpl_->getFunction<Impl::SetRotFn>(pImpl_->offsets.setObjectRotation);
    if (fn) fn(handle, rx, ry, rz);
}

// --- Model loading ---

void GameEngine::requestModel(int modelId) {
    auto fn = pImpl_->getFunction<Impl::RequestModelFn>(pImpl_->offsets.requestModel);
    if (fn) fn(modelId);
}

void GameEngine::loadModels() {
    auto fn = pImpl_->getFunction<Impl::LoadModelsFn>(pImpl_->offsets.loadAllModels);
    if (fn) fn();
}

bool GameEngine::hasModelLoaded(int modelId) {
    auto fn = pImpl_->getFunction<Impl::HasModelLoadedFn>(pImpl_->offsets.hasModelLoaded);
    return fn ? fn(modelId) : false;
}

// --- Player ---

PedHandle GameEngine::getPlayerPed() {
    auto fn = pImpl_->getFunction<Impl::GetPlayerPedFn>(pImpl_->offsets.getPlayerPed);
    return fn ? fn() : INVALID_PED;
}

void GameEngine::setPlayerControl(bool enable) {
    auto fn = pImpl_->getFunction<Impl::SetPlayerControlFn>(pImpl_->offsets.setPlayerControl);
    if (fn) fn(enable);
}

} // namespace WorldRPG::Game
