#include "GameEngine.h"
#include "PlayerPedPort.h"
#include "GTASAOffsets.h"
#include "../shared/ServerWorld.h"
#include "../shared/entity/PedSyncState.h"

namespace WorldRPG::JniBridge { void PostGameLine(int kind, const char* text); }
#include "../hook/SymbolResolver.h"
#include "../hook/MemoryPatcher.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <array>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <memory>

#ifdef __ANDROID__
#include <dlfcn.h>
#include <sys/mman.h>
#include <unistd.h>
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
using CivilianCtorFn = void (*)(void*, int, unsigned);
static CivilianCtorFn g_civilianCtor = nullptr;
using AddPedFn = void* (*)(int, unsigned, const Vec3*, bool);
static AddPedFn g_addPed = nullptr;
using WorldAddFn = void (*)(void*);
static WorldAddFn g_worldAdd = nullptr;
using SetHeadingFn = void (*)(void*, float);
static SetHeadingFn g_setHeading = nullptr;
static float* g_pedDensity = nullptr;
static void* g_managePopulation = nullptr;
static std::atomic<float> g_poseX{1754.f};
static std::atomic<float> g_poseY{-1894.f};
static std::atomic<float> g_poseZ{14.f};
static std::atomic<float> g_poseH{0.f};
static std::atomic<float> g_velX{0.f};
static std::atomic<float> g_velY{0.f};
static std::atomic<float> g_velZ{0.f};
static std::atomic<float> g_health{100.f};
static std::atomic<bool> g_healthOwned{false};
static std::atomic<bool> g_damagePending{false};
static std::atomic<uint32_t> g_damager{0xFFFFFFFFu};
static std::atomic<uint8_t> g_damageWeapon{0};
static std::atomic<int64_t> g_lastDamageMs{0};
static std::atomic<int64_t> g_lastMeleeMs{0};
static std::atomic<bool> g_localFire{false};
static std::atomic<bool> g_localDuck{false};
static TeleportFn g_teleport = nullptr;
static EndUserPauseFn g_endPause = nullptr;
static RemoveTopFn g_removeTop = nullptr;
static SwitchOffFn g_switchOff = nullptr;
static void* g_mobileMenu = nullptr;
using ScriptProcessFn = void (*)(void*);
static ScriptProcessFn g_originalScript = nullptr;
static std::atomic<float> g_spawnX{1754.f};
static std::atomic<float> g_spawnY{-1894.f};
static std::atomic<float> g_spawnZ{14.f};

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
    g_civilianCtor = reinterpret_cast<CivilianCtorFn>(dlsym(handle, "_ZN12CCivilianPedC1E8ePedTypej"));
    g_addPed = reinterpret_cast<AddPedFn>(dlsym(handle, "_ZN11CPopulation6AddPedE8ePedTypejRK7CVectorb"));
    g_worldAdd = reinterpret_cast<WorldAddFn>(dlsym(handle, "_ZN6CWorld3AddEP7CEntity"));
    g_setHeading = reinterpret_cast<SetHeadingFn>(dlsym(handle, "_ZN10CPlaceable10SetHeadingEf"));
    g_pedDensity = reinterpret_cast<float*>(dlsym(handle, "_ZN11CPopulation20PedDensityMultiplierE"));
    g_managePopulation = dlsym(handle, "_ZN11CPopulation16ManagePopulationEv");
    g_teleport = reinterpret_cast<TeleportFn>(dlsym(handle, "_ZN4CPed8TeleportE7CVectorh"));
    g_endPause = reinterpret_cast<EndUserPauseFn>(dlsym(handle, "_ZN6CTimer12EndUserPauseEv"));
    g_removeTop = reinterpret_cast<RemoveTopFn>(dlsym(handle, "_ZN10MobileMenu15RemoveTopScreenEbb"));
    g_switchOff = reinterpret_cast<SwitchOffFn>(dlsym(handle, "_Z20Menu_SwitchOffToGamev"));
}

static void PlacePlayer() {
    if (!g_findPlayer || !g_teleport) return;
    void* ped = g_findPlayer(0);
    if (!ped) return;
    Vec3 pos{g_spawnX.load(), g_spawnY.load(), g_spawnZ.load()};
    g_teleport(ped, pos, 0);
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
static void SampleLocalPose() {
    if (!g_findPlayer) return;
    auto* ped = static_cast<unsigned char*>(g_findPlayer(0));
    if (!ped) return;
    float x = 0, y = 0, z = 0, heading = g_poseH.load();
    auto* matrix = *reinterpret_cast<unsigned char**>(ped + 0x18);
    const auto matrixAddr = reinterpret_cast<uintptr_t>(matrix);
    if (matrix && (matrixAddr & 3u) == 0 && matrixAddr > 0x10000u) {
        x = *reinterpret_cast<float*>(matrix + 0x30);
        y = *reinterpret_cast<float*>(matrix + 0x34);
        z = *reinterpret_cast<float*>(matrix + 0x38);
        // `at` is the facing axis. The right vector is 90 degrees off, which
        // made every remote ped walk sideways.
        heading = std::atan2(-*reinterpret_cast<float*>(matrix + 0x20),
                              *reinterpret_cast<float*>(matrix + 0x24));
    } else {
        x = *reinterpret_cast<float*>(ped + 0x08);
        y = *reinterpret_cast<float*>(ped + 0x0C);
        z = *reinterpret_cast<float*>(ped + 0x10);
        heading = *reinterpret_cast<float*>(ped + 0x14);
    }
    if (!PlausiblePosition(x, y, z)) return;
    auto* health = reinterpret_cast<float*>(ped + 0x6AC);
    auto* speed = reinterpret_cast<float*>(ped + 0x68);
    if (g_healthOwned.load()) {
        const float owned = g_health.load();
        if (*health + 0.5f < owned) g_health = *health;
        else *health = owned;
    } else if (*health > 0.f && *health < 1000.f) {
        g_health = *health;
    }
    g_velX = speed[0];
    g_velY = speed[1];
    g_velZ = speed[2];
    const float dx = x - g_poseX.load();
    const float dy = y - g_poseY.load();
    g_poseX = x;
    g_poseY = y;
    g_poseZ = z;
    g_poseH = heading;
    SampleLocalTasks(ped);
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
    int weapon = 0;
    int wanderDir = 0;
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
    bool wander = false;
    bool animationKilled = false;
    bool healthLocked = false;
    bool armorLocked = false;
    // Last keysync stick (PC units, -128..128). Drives CPlayerPed::ProcessControl
    // once the controller hook is installed. keysync is false for script peds.
    int stickX = 0;
    int stickY = 0;
    bool keysync = false;
    // Server ped from the ENTITY_ADD actor list (kind 14).
    bool serverPed = false;
};
static std::mutex g_remoteMutex;
static std::unordered_map<uint32_t, RemotePed> g_remotePeds;
struct RemoteBody {
    void* ped = nullptr;
    std::unique_ptr<std::byte[]> owned;
    int weapon = -1;
    int wanderDir = -1;
    bool ducked = false;
    int64_t lastFight = 0;
};
struct RemoteCar {
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
};
static std::unordered_map<uint32_t, RemoteCar> g_remoteCars;
static std::vector<uint32_t> g_forget;
static std::atomic<bool> g_remoteResetPending{false};
static std::vector<HostedPed> g_hostedSnap;
static std::vector<HostedCar> g_hostedCarSnap;
static std::unordered_map<uint32_t, RemoteBody> g_remoteBodies;
// Local player vehicle state and the queue for VEHICLE_INOUT. Both guarded by g_remoteMutex.
static constexpr uint32_t kNoVehicle = 0xFFFFFFFFu;
static uint32_t g_localVehicleId = kNoVehicle;
// Live sample of that car for packet 33 (CopyLocalCar). Written in WatchLocalVehicle, under g_remoteMutex.
static LocalCarState g_localCar;
static bool g_localCarValid = false;
static std::vector<LocalVehicleEvent> g_localVehicleEvents;
static constexpr size_t kMaxLocalVehicleEvents = 16;
// Local bullet-weapon shots (NoteLocalShot / ConsumeLocalShot). Guarded by g_remoteMutex.
static std::vector<LocalShot> g_localShots;
static constexpr size_t kMaxLocalShots = 32;

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

static void DressLocalPlayer() {
    if (!g_findPlayer) return;
    DressCj(g_findPlayer(0), false);
}

static void ArmRemote(void* body, RemoteBody& stored, int weapon) {
    if (!body || weapon <= 0 || stored.weapon == weapon) return;
    using GiveWeaponFn = void (*)(void*, int, unsigned, bool);
    static GiveWeaponFn giveWeapon = nullptr;
    static bool looked = false;
    if (!looked) {
        looked = true;
        void* handle = dlopen("libGTASA.so", RTLD_NOLOAD | RTLD_GLOBAL);
        if (handle)
            giveWeapon = reinterpret_cast<GiveWeaponFn>(dlsym(handle, "_ZN4CPed10GiveWeaponE11eWeaponTypejb"));
    }
    if (!giveWeapon) return;
    giveWeapon(body, weapon, 60u, true);
    stored.weapon = weapon;
}

struct TaskFns {
    bool ready = false;
    void* (*taskNew)(unsigned long) = nullptr;
    void (*wanderCtor)(void*, int, unsigned char, bool, float) = nullptr;
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
    api.wanderVtable = dlsym(handle, "_ZTV26CTaskComplexWanderStandard");
    return api;
}

static void* PedIntel(void* ped) {
    if (!ped) return nullptr;
    auto* raw = static_cast<unsigned char*>(ped);
    void* intel = *reinterpret_cast<void**>(raw + 0x538);
    const auto addr = reinterpret_cast<uintptr_t>(intel);
    if (!intel || (addr & 3u) != 0 || addr < 0x10000u) return nullptr;
    return intel;
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
}

static void StartWander(void* body, int direction) {
    TaskFns& api = Tasks();
    void* intel = PedIntel(body);
    if (!intel || !api.taskNew || !api.wanderCtor || !api.addPrimary) return;
    void* task = api.taskNew(0x40);
    if (!task) return;
    std::memset(task, 0, 0x40);
    const unsigned char dir = static_cast<unsigned char>(std::clamp(direction, 0, 7));
    api.wanderCtor(task, 4, dir, true, 0.f);
    if (api.wanderVtable)
        *reinterpret_cast<void**>(task) = static_cast<unsigned char*>(api.wanderVtable) + 16;
    api.addPrimary(intel, task, false);
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

static void PresentOnFoot(void* body, RemoteBody& stored, const RemotePed& ped) {
    TaskFns& api = Tasks();
    const float speed = std::sqrt(ped.vx * ped.vx + ped.vy * ped.vy);
    if (api.setMoveState && api.setMoveAnim) {
        const int state = speed > 0.14f ? 6 : (speed > 0.03f ? 4 : 1);
        api.setMoveState(body, state);
        api.setMoveAnim(body);
    }
    void* intel = PedIntel(body);
    if (!intel) return;
    if (ped.duck && !stored.ducked && api.duck) api.duck(intel, 0);
    if (!ped.duck && stored.ducked && api.clearDuck) api.clearDuck(intel);
    stored.ducked = ped.duck;
    if (!ped.fire) return;
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
    heading = std::atan2(-*reinterpret_cast<float*>(matrix + 0x20), *reinterpret_cast<float*>(matrix + 0x24));
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

// Live values for packet 56, read on the game thread like the ped snapshot. Desktop
// CUnoccupiedVehicleSync::WriteVehicleInformation uses CClientVehicle::GetPosition, GetRotationDegrees,
// GetMoveSpeed and GetTurnSpeed. GetRotationDegrees (mta_neon_source/Client/mods/deathmatch/logic/
// CClientVehicle.cpp 464) is 2*PI - ConvertMatrixToEulerAngles, wrapped to [0, 360). That equals the
// negated x and y of CMatrix::GetRotation (Shared/sdk/CMatrix.h 236, "0 - ConvertToEulerAngles") modulo
// 360. As in GetRotationMatrix (CMatrix.h 200), each matrix row is divided by its length, so scale is
// ignored. Health is not read (the Android CVehicle health offset is not verified).
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

    const float values[] = {out.x, out.y, out.z, out.rotX, out.rotY, out.rotZ, out.vx, out.vy, out.vz, out.tx, out.ty, out.tz};
    for (float v : values) {
        if (!std::isfinite(v)) return false;
    }
    return true;
}

static void DropBody(void* body, bool car, bool pooled = false) {
    TaskFns& api = Tasks();
    if (api.worldRemove && body) api.worldRemove(body);
    if (!car || !body) return;
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
static float GroundedZ(float x, float y, float z) {
    auto& api = Ground();
    if (!api.findGround) return z;
    bool found = false;
    const float ground = api.findGround(x, y, z + 2.f, &found, nullptr);
    if (!found || ground <= -200.f || ground >= 4000.f) return z;
    if (z > ground + 1.2f || z < ground) return ground + 1.f;
    return z;
}

// VEHICLE_INOUT seat requests for other players (NoteRemoteSeat / NoteRemoteUnseat). The network thread writes them under
// g_remoteMutex. ApplyRemoteSeats runs on the game thread and removes a request once it is applied.
struct RemoteSeatRequest {
    uint32_t vehicleId = 0xFFFFFFFFu;
    uint8_t seat = 0;
    bool seated = false;
    uint32_t serial = 0;
    int ticks = 0;
};
static std::unordered_map<uint32_t, RemoteSeatRequest> g_seatRequests;
static uint32_t g_seatSerial = 0;
// About 10 s at 60 fps. A request whose ped or car body never appears is dropped after this.
constexpr int kSeatRequestTicks = 600;
// Game thread only: the car each remote ped is seated in by ApplyRemoteSeat.
struct SeatedPed {
    uint32_t vehicleId = 0xFFFFFFFFu;
    uint8_t seat = 0;
};
static std::unordered_map<uint32_t, SeatedPed> g_seatedPeds;

// Ends a seat this thread applied earlier. The caller holds g_remoteMutex. The car body must still exist; without it
// the record is only forgotten. Reads g_remoteBodies and g_remoteCars directly.
static void UnseatLocked(uint32_t pedId) {
    auto seated = g_seatedPeds.find(pedId);
    if (seated == g_seatedPeds.end()) return;
    const uint32_t vehicleId = seated->second.vehicleId;
    g_seatedPeds.erase(seated);
    auto b = g_remoteBodies.find(pedId);
    auto c = g_remoteCars.find(vehicleId);
    if (b == g_remoteBodies.end() || !b->second.ped) return;
    if (c == g_remoteCars.end() || !c->second.body) return;
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

// One request on the game thread. True when it is finished (applied, or nothing to do). False keeps it for the next tick.
static bool ApplyRemoteSeat(uint32_t pedId, const RemoteSeatRequest& req) {
    if (!req.seated) {
        UnseatApplied(pedId);
        return true;
    }
    // Passenger seats are not placed (desktop CClientVehicleManager::ConvertIndexToGameSeat is not ported).
    if (req.seat != 0) return true;
    auto seated = g_seatedPeds.find(pedId);
    if (seated != g_seatedPeds.end() && seated->second.vehicleId == req.vehicleId && seated->second.seat == req.seat) {
        return true;
    }
    void* body = nullptr;
    void* car = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_remoteMutex);
        auto b = g_remoteBodies.find(pedId);
        if (b != g_remoteBodies.end()) body = b->second.ped;
        auto c = g_remoteCars.find(req.vehicleId);
        if (c != g_remoteCars.end()) car = c->second.body;
    }
    if (!body || !car) return false;
    // Desktop WarpIntoVehicle (CClientPed.cpp 3383) calls RemoveFromVehicle before it seats the ped.
    if (seated != g_seatedPeds.end()) UnseatApplied(pedId);
    if (!SeatRemotePedInCar(body, car)) return false;
    // The seated skip in ApplyRemotePeds returns before its ClearRemoteStick, so a stick set before the seat is cleared here.
    ClearRemoteStick(body);
    g_seatedPeds[pedId] = SeatedPed{req.vehicleId, req.seat};
    ENGINE_LOGI("VehInOut: ped %u seated in vehicle %u", pedId, req.vehicleId);
    return true;
}

static void ApplyRemoteSeats() {
    std::vector<std::pair<uint32_t, RemoteSeatRequest>> work;
    {
        std::lock_guard<std::mutex> lock(g_remoteMutex);
        for (const auto& [pedId, req] : g_seatRequests) work.emplace_back(pedId, req);
    }
    for (const auto& [pedId, req] : work) {
        const bool done = ApplyRemoteSeat(pedId, req);
        std::lock_guard<std::mutex> lock(g_remoteMutex);
        auto it = g_seatRequests.find(pedId);
        if (it == g_seatRequests.end() || it->second.serial != req.serial) continue;  // replaced while applying
        if (done) {
            g_seatRequests.erase(it);
        } else if (++it->second.ticks >= kSeatRequestTicks) {
            ENGINE_LOGI("VehInOut: seat request for ped %u dropped, body not ready", pedId);
            g_seatRequests.erase(it);
        }
    }
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
    HostedCar sample;
    const bool sampled = current != kNoVehicle && model >= 400 && model <= 611 && SampleHostedCar(vehicle, sample);
    g_localCarValid = sampled;
    if (sampled) {
        LocalCarState& c = g_localCar;
        c = LocalCarState{};
        c.vehicleId = current;
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
    }
    if (current == g_localVehicleId) return;
    if (g_localVehicleId != kNoVehicle) {
        g_localVehicleEvents.push_back(LocalVehicleEvent{g_localVehicleId, false});
        ENGINE_LOGI("VehInOut: local ped left vehicle %u", g_localVehicleId);
    }
    if (current != kNoVehicle) {
        g_localVehicleEvents.push_back(LocalVehicleEvent{current, true});
        ENGINE_LOGI("VehInOut: local ped entered vehicle %u", current);
    }
    if (g_localVehicleEvents.size() > kMaxLocalVehicleEvents) {
        ENGINE_LOGI("VehInOut: event queue full, dropping oldest");
        g_localVehicleEvents.erase(g_localVehicleEvents.begin());
    }
    g_localVehicleId = current;
}

static void ApplyRemotePeds() {
    NoteGameFrame();
    if (g_remoteResetPending.exchange(false)) {
        // Leave every seat while the car bodies still exist, then drop the requests.
        std::vector<uint32_t> seated;
        for (const auto& entry : g_seatedPeds) seated.push_back(entry.first);
        for (uint32_t pedId : seated) UnseatApplied(pedId);
        {
            std::lock_guard<std::mutex> lock(g_remoteMutex);
            g_seatRequests.clear();
        }
        for (auto& entry : g_remoteBodies) {
            ClearRemoteStick(entry.second.ped);
            DropBody(entry.second.ped, false);
        }
        g_remoteBodies.clear();
        for (auto& entry : g_remoteCars) DropBody(entry.second.body, true, !entry.second.owned);
        g_remoteCars.clear();
        {
            // A new session starts on foot: drop the stale vehicle and any queued VEHICLE_INOUT events.
            std::lock_guard<std::mutex> lock(g_remoteMutex);
            g_localVehicleId = kNoVehicle;
            g_localVehicleEvents.clear();
        }
        ENGINE_LOGI("Remote world reset for reconnect");
    }
    {
        ServerWorld::ActorSpawn actors[96];
        const int count = ServerWorld::CopyActors(actors, 96);
        // Observer interpolation (PedSyncState). Runs before g_remoteMutex is taken, because
        // SamplePose pushes the pose through NoteRemotePed, which locks the same mutex.
        for (int i = 0; i < count; ++i) {
            if (actors[i].kind == 14) WorldRPG::Sync::PedSyncState::SamplePose(actors[i].id);
        }
        std::lock_guard<std::mutex> lock(g_remoteMutex);
        for (int i = 0; i < count; ++i) {
            const auto& actor = actors[i];
            if (actor.kind == 14) {
                const bool firstSeen = g_remotePeds.find(actor.id) == g_remotePeds.end();
                auto& ped = g_remotePeds[actor.id];
                ped.serverPed = true;
                if (firstSeen) ENGINE_LOGI("DIAG ped actor id=%u model=%d hosted=%d pos=%.1f,%.1f,%.1f", actor.id, actor.model, actor.hosted ? 1 : 0, actor.x, actor.y, actor.z);
                ped.hosted = actor.hosted;
                if (!ped.dirty && !ped.shown && actor.model >= 0) {
                    ped.x = actor.x;
                    ped.y = actor.y;
                    ped.z = actor.z;
                    ped.heading = actor.heading;
                    ped.dirty = true;
                }
            } else if (actor.kind == 2 && actor.model >= 400) {
                auto& car = g_remoteCars[actor.id];
                car.model = actor.model;
                car.hosted = actor.hosted;
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
                DropBody(car->second.body, true, !car->second.owned);
                g_remoteCars.erase(car);
            }
            g_remotePeds.erase(id);
        }
        g_forget.clear();
        (void)api;
    }
    std::unordered_map<uint32_t, RemotePed> pending;
    {
        std::lock_guard<std::mutex> lock(g_remoteMutex);
        for (auto& [id, ped] : g_remotePeds) pending.emplace(id, ped);
    }
    for (auto& [id, ped] : pending) {
        if (!PlausiblePosition(ped.x, ped.y, ped.z)) continue;
        auto live = g_remotePeds.find(id);
        if (live == g_remotePeds.end()) continue;
        if (id == ServerWorld::LocalPlayer()) continue;
        int model = ServerWorld::ModelOf(id);
        // Model 0 is the shared CJ clump. Rebuilding it on a second ped
        // replaces the local player. A complete body keeps the other player visible.
        if (model <= 0 || model > 312) model = 7;
        void* body = nullptr;
        auto found = g_remoteBodies.find(id);
        if (found == g_remoteBodies.end()) {
            if (g_remoteBodies.size() >= 48) continue;
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
            spawnedFrame = g_gameFrame;
            const Vec3 pos{ped.x, ped.y, GroundedZ(ped.x, ped.y, ped.z)};
            body = CreateRemotePlayerPed(model, pos.x, pos.y, pos.z, ped.heading);
            if (!body) continue;
            RemoteBody stored;
            ArmRemote(body, stored, ped.weapon);
            stored.ped = body;
            g_remoteBodies.emplace(id, std::move(stored));
            live->second.shownX = pos.x;
            live->second.shownY = pos.y;
            live->second.shownZ = pos.z;
            live->second.shownH = ped.heading;
            live->second.shown = true;
            static int created = 0;
            if (created < 6) {
                ++created;
                ENGINE_LOGI("Server ped %u model %d at %.0f %.0f %.0f", id, model, pos.x, pos.y, pos.z);
            }
            continue;
        }
        body = found->second.ped;
        if (!body) continue;
        ArmRemote(body, found->second, ped.weapon);
        // A seated ped is placed by the seat task (ApplyRemoteSeat), so the on-foot pose below must not move it.
        if (g_seatedPeds.count(id)) continue;
        if (ped.hosted && ped.wander && found->second.wanderDir != ped.wanderDir) {
            StartWander(body, ped.wanderDir);
            found->second.wanderDir = ped.wanderDir;
        }
        if (!ped.hosted && found->second.wanderDir >= 0) {
            TaskFns& api = Tasks();
            void* intel = PedIntel(body);
            if (intel && api.clearTasks) api.clearTasks(intel, true, true);
            found->second.wanderDir = -1;
        }
        const bool controlled = !ped.hosted && ped.keysync && RemoteControlHookActive();
        if (controlled) {
            // Plan step A: the stick drives CPlayerPed::ProcessControl. The per-tick
            // matrix lerp below would fight it, so only re-anchor when the body
            // drifts more than 8 m from the server position.
            SetRemoteStick(body, ped.stickX, ped.stickY, ped.fire);
            float bx = 0, by = 0, bz = 0, bh = 0;
            if (ReadBodyPos(body, bx, by, bz, bh)) {
                const float ex = ped.x - bx;
                const float ey = ped.y - by;
                const float ez = ped.z - bz;
                if (ex * ex + ey * ey + ez * ez > 8.f * 8.f)
                    PlaceRemotePlayerPed(body, ped.x, ped.y, ped.z, ped.heading);
            }
            live->second.shown = true;
            // The pad now presses ButtonCircle, so the game starts the fight task.
            // Skip the separate punch from PresentOnFoot to avoid a second punch.
            RemotePed quiet = ped;
            quiet.fire = false;
            PresentOnFoot(body, found->second, quiet);
            continue;
        }
        ClearRemoteStick(body);
        const float dx = ped.x - live->second.shownX;
        const float dy = ped.y - live->second.shownY;
        const float dz = ped.z - live->second.shownZ;
        const float dist2 = dx * dx + dy * dy + dz * dz;
        if (ped.hosted && live->second.shown && dist2 < 64.f) {
            float hx = 0, hy = 0, hz = 0, hh = live->second.shownH;
            if (ReadBodyPos(body, hx, hy, hz, hh)) {
                auto* speed = reinterpret_cast<float*>(static_cast<unsigned char*>(body) + 0x68);
                live->second.shownX = hx;
                live->second.shownY = hy;
                live->second.shownZ = hz;
                live->second.shownH = hh;
                std::lock_guard<std::mutex> lock(g_remoteMutex);
                auto snap = g_remotePeds.find(id);
                if (snap != g_remotePeds.end()) {
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
            continue;
        }
        if (!live->second.shown || dist2 > 20.f * 20.f) {
            live->second.shownX = ped.x;
            live->second.shownY = ped.y;
            live->second.shownZ = ped.z;
            live->second.shownH = ped.heading;
            live->second.shown = true;
            PlaceRemotePlayerPed(body, ped.x, ped.y, ped.z, ped.heading);
        } else {
            live->second.shownX += dx * 0.35f;
            live->second.shownY += dy * 0.35f;
            live->second.shownZ += dz * 0.35f;
            float turn = ped.heading - live->second.shownH;
            constexpr float pi = 3.14159265f;
            while (turn > pi) turn -= 2.f * pi;
            while (turn < -pi) turn += 2.f * pi;
            live->second.shownH += turn * 0.35f;
            auto* raw = static_cast<unsigned char*>(body);
            auto* matrix = *reinterpret_cast<unsigned char**>(raw + 0x18);
            const auto matrixAddr = reinterpret_cast<uintptr_t>(matrix);
            if (matrix && (matrixAddr & 3u) == 0 && matrixAddr > 0x10000u) {
                *reinterpret_cast<float*>(matrix + 0x30) = live->second.shownX;
                *reinterpret_cast<float*>(matrix + 0x34) = live->second.shownY;
                *reinterpret_cast<float*>(matrix + 0x38) = live->second.shownZ;
            }
            *reinterpret_cast<float*>(raw + 0x08) = live->second.shownX;
            *reinterpret_cast<float*>(raw + 0x0C) = live->second.shownY;
            *reinterpret_cast<float*>(raw + 0x10) = live->second.shownZ;
        }
        if (g_setHeading) g_setHeading(body, live->second.shownH);
        if (std::fabs(ped.vx) + std::fabs(ped.vy) + std::fabs(ped.vz) > 0.001f) {
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
        if (g_setHeading) g_setHeading(body, live->second.shownH);

        // Native task presentation — matches mta_neon CClientPed::UpdateNativeTaskAnimationPresentation
        // Apply locomotion mode, weapon type, animation, onfire/inwater, and animation kill
        // to the remote ped body on the game thread after spatial sync.
        if (ped.animationKilled && found->second.ped) {
            // ucFlags & 0x80: clear synced anim state so the ped is no longer
            // permanently excluded from native-task locomotion presentation.
            TaskFns& api = Tasks();
            void* intel = PedIntel(found->second.ped);
            if (intel && api.clearTasks) api.clearTasks(intel, true, true);
            live->second.animationKilled = false;
        }
        if (ped.fire != found->second.ducked) {
            // OnFire state — GTA uses CPed::SetOnFire(bool). For now we track
            // it in RemoteBody for future native call integration.
            found->second.ducked = ped.fire;
        }
        if (ped.inWater) {
            // IsInWater state — tracked for future native call integration.
            // No immediate GTA native available without further dlsym work.
        }
        if (ped.nativeTaskLocomotionMode != 0 && !ped.hosted) {
            // SNativeTaskLocomotionSync::eMode applied via controller state stick inputs.
            // WALK/RUN/SPRINT modes map to left-stick magnitude thresholds matching
            // mta_neon CClientPed::ApplyNativeTaskLocomotionPresentation.
            // Full implementation requires CTaskManagerSA equivalent on Android.
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
            live->second.physicalTakeoverPending = false;
        }

        if (!ped.hosted) PresentOnFoot(body, found->second, ped);
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
            hosted.push_back(sample);
        }
        g_hostedSnap.swap(hosted);
    }
    TaskFns& api = Tasks();
    struct CarPose { uint32_t id; int model; float x, y, z, heading; bool hosted; };
    std::vector<CarPose> cars;
    {
        std::lock_guard<std::mutex> lock(g_remoteMutex);
        for (const auto& [id, car] : g_remoteCars)
            cars.push_back({id, car.model, car.x, car.y, car.z, car.heading, car.hosted});
    }
    for (const auto& car : cars) {
        const uint32_t id = car.id;
        if (!PlausiblePosition(car.x, car.y, car.z) || car.model < 400 || car.model > 611) continue;
        auto found = g_remoteCars.find(id);
        if (found == g_remoteCars.end()) continue;
        void* body = found->second.body;
        if (!body) {
            if (!EnsureWorldModel(car.model) || g_remoteCars.size() > 48) continue;
            if (api.createCarForScript) {
                // Desktop CPoolsSA::AddVehicle: CCarCtrl::CreateCarForScript(model, CVector(), 0) (pool-owned, added to the world).
                body = api.createCarForScript(car.model, Vec3{car.x, car.y, car.z}, 0);
                if (!body) continue;
            } else {
                if (!api.carCtor || !g_worldAdd) continue;
                found->second.owned = std::make_unique<std::byte[]>(0x2000);
                body = found->second.owned.get();
                std::memset(body, 0, 0x2000);
                api.carCtor(body, car.model, 2, 0);
                g_worldAdd(body);
            }
            found->second.body = body;
            found->second.shownX = car.x;
            found->second.shownY = car.y;
            found->second.shownZ = car.z;
            found->second.shownH = car.heading;
            found->second.shown = true;
            WriteBodyPos(body, car.x, car.y, car.z);
            if (g_setHeading) g_setHeading(body, car.heading);
            static int created = 0;
            if (created < 6) {
                ++created;
                ENGINE_LOGI("Server vehicle %u model %d at %.0f %.0f %.0f", id, car.model, car.x, car.y, car.z);
            }
            continue;
        }
        if (!car.hosted) ApplyCarMotion(id, body);
        if (car.hosted) continue;
        const float dx = car.x - found->second.shownX;
        const float dy = car.y - found->second.shownY;
        const float dz = car.z - found->second.shownZ;
        if (dx * dx + dy * dy + dz * dz > 30.f * 30.f) {
            found->second.shownX = car.x;
            found->second.shownY = car.y;
            found->second.shownZ = car.z;
        } else {
            found->second.shownX += dx * 0.35f;
            found->second.shownY += dy * 0.35f;
            found->second.shownZ += dz * 0.35f;
        }
        found->second.shownH = car.heading;
        WriteBodyPos(body, found->second.shownX, found->second.shownY, found->second.shownZ);
        if (g_setHeading) g_setHeading(body, car.heading);
    }

    // Hosted vehicles for packet 56 (SendUnoccupiedVehicleSync). Sampled here on the game thread,
    // then swapped in as one snapshot, the same way g_hostedSnap is built above.
    std::vector<HostedCar> hostedCars;
    for (const auto& car : cars) {
        if (!car.hosted) continue;
        void* body = nullptr;
        {
            std::lock_guard<std::mutex> lock(g_remoteMutex);
            auto found = g_remoteCars.find(car.id);
            if (found != g_remoteCars.end() && found->second.hosted) body = found->second.body;
        }
        HostedCar sample;
        if (!body || !SampleHostedCar(body, sample)) continue;
        sample.id = car.id;
        hostedCars.push_back(sample);
    }
    {
        std::lock_guard<std::mutex> lock(g_remoteMutex);
        g_hostedCarSnap.swap(hostedCars);
    }
    // After the car loop, so the car bodies that a seat needs exist for this tick.
    ApplyRemoteSeats();
    WatchLocalVehicle();
}

extern "C" void WorldRPG_NoAmbientPeds() {}

extern "C" void WorldRPG_NoStoryScript(void*) {
    static int depth = 0;
    static bool ready = false;
    if (depth > 0) return;
    if (ready) {
        ++depth;
        if (g_pedDensity) *g_pedDensity = 0.f;
        SampleLocalPose();
        ApplyRemotePeds();
        ServerWorld::Tick(g_poseX.load(), g_poseY.load(), g_poseZ.load());
        --depth;
        return;
    }
    ++depth;
    if (g_requestSpecial && g_loadAll && g_setupPlayer) {
        g_requestSpecial(0, "player", 26);
        g_loadAll(true);
        g_setupPlayer(0);
        PlacePlayer();
        DressLocalPlayer();
        if (g_endPause) g_endPause();
        ENGINE_LOGI("Player created without main.scm");
    }
    ready = true;
    ApplyRemotePeds();
    --depth;
}

bool BeginServerSession() {
    static bool leftMenu = false;
    const uintptr_t base = Hook::MemoryPatcher::getLibraryBaseAddress("libGTASA.so");
    if (leftMenu) {
        if (!base) return true;
        constexpr uintptr_t kPendingPause = 0x00D3D664;
        constexpr uintptr_t kGotMobileMenu = 0x008519B0;
        auto* pending = reinterpret_cast<uint8_t*>(base + kPendingPause);
        uintptr_t menu = *reinterpret_cast<uintptr_t*>(base + kGotMobileMenu);
        AllowWrite(pending, 1);
        const bool menuUp = menu && (*reinterpret_cast<int32_t*>(menu + 0x24) != 0 ||
            *reinterpret_cast<uintptr_t*>(menu + 0x30) != 0);
        if (*pending || menuUp) {
            *pending = 0;
            if (menuUp && g_removeTop) {
                g_mobileMenu = reinterpret_cast<void*>(menu);
                for (int i = 0; i < 8 && *reinterpret_cast<int32_t*>(menu + 0x24) > 0; ++i)
                    g_removeTop(g_mobileMenu, 1, 1);
                if (g_switchOff) g_switchOff();
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
        if (!InstallLocalBulletFireHook())
            ENGINE_LOGE("Local bullet sync (PLAYER_BULLETSYNC) disabled: CWeapon::Fire hook missing");
        ENGINE_LOGI("Ped symbols add=%p ctor=%p heading=%p density=%p manage=%p",
            reinterpret_cast<void*>(g_addPed), reinterpret_cast<void*>(g_civilianCtor),
            reinterpret_cast<void*>(g_setHeading), static_cast<void*>(g_pedDensity), g_managePopulation);
        if (g_managePopulation) {
            const auto population = patcher.installHook(reinterpret_cast<uintptr_t>(g_managePopulation),
                reinterpret_cast<uintptr_t>(&WorldRPG_NoAmbientPeds));
            if (population.success) ENGINE_LOGI("Stopped local ped population");
            else ENGINE_LOGE("Could not stop local ped population: %s", population.errorMessage.c_str());
        } else {
            ENGINE_LOGE("CPopulation::ManagePopulation was not exported");
        }
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
        for (int i = 0; i < 8 && *reinterpret_cast<int32_t*>(menu + 0x24) > 0; ++i)
            g_removeTop(g_mobileMenu, 1, 1);
        if (g_switchOff) g_switchOff();
        if (seenPlaying < 4) ENGINE_LOGI("Closed the GTA pause menu");
    }
    if (g_endPause) g_endPause();
    if (seenPlaying > 120 && !menuUp) leftMenu = true;
    return true;
}
void ApplyServerSpawn(float x, float y, float z) {
    g_spawnX = x;
    g_spawnY = y;
    g_spawnZ = z;
    g_poseX = x;
    g_poseY = y;
    g_poseZ = z;
    g_health = 100.f;
    g_healthOwned = false;
    g_damagePending = false;
    PlacePlayer();
}
void CopySpawn(float& x, float& y, float& z) {
    x = g_spawnX.load();
    y = g_spawnY.load();
    z = g_spawnZ.load();
}
void CopyPose(float& x, float& y, float& z, float& heading) {
    x = g_poseX.load();
    y = g_poseY.load();
    z = g_poseZ.load();
    heading = g_poseH.load();
}
void CopyBody(float& x, float& y, float& z, float& heading,
    float& vx, float& vy, float& vz, float& health) {
    CopyPose(x, y, z, heading);
    vx = g_velX.load();
    vy = g_velY.load();
    vz = g_velZ.load();
    health = g_health.load();
}
void NoteDamage(uint32_t attacker, float amount, uint8_t weapon) {
    if (attacker == 0 || attacker == 0xFFFFFFFFu || amount <= 0.f) return;
    const int64_t now = static_cast<int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
    std::atomic<int64_t>& stamp = weapon < 16 ? g_lastMeleeMs : g_lastDamageMs;
    const int64_t gate = weapon < 16 ? 650 : 150;
    int64_t previous = stamp.load();
    if (now - previous < gate) return;
    if (!stamp.compare_exchange_strong(previous, now)) return;
    float health = g_health.load();
    health = std::max(0.f, health - amount);
    g_health = health;
    g_healthOwned = true;
    g_damager = attacker;
    g_damageWeapon = weapon;
    g_damagePending = true;
}
bool ConsumeDamage(uint32_t& attacker, uint8_t& weapon) {
    if (!g_damagePending.exchange(false)) return false;
    attacker = g_damager.load();
    weapon = g_damageWeapon.load();
    return attacker != 0 && attacker != 0xFFFFFFFFu;
}
void NoteRemoteAim(uint32_t id, float heading, float camera, int stickX, int stickY, bool fire, bool duck) {
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
    ped.fire = fire;
    ped.duck = duck;
    ped.stickX = stickX;
    ped.stickY = stickY;
    ped.keysync = true;
    // With the controller hook the game moves the ped from the stick, so the
    // velocity fallback stays off.
    if (!RemoteControlHookActive() && mag > 0.08f) {
        // Inverse of CClientPed: stick Y negative is forward along the camera.
        const float input = std::atan2(-static_cast<float>(stickX), -static_cast<float>(stickY));
        const float desired = input - camera;
        ped.vx = -std::sin(desired) * scale;
        ped.vy = std::cos(desired) * scale;
    }
    ped.dirty = true;
}
void NoteRemoteWeapon(uint32_t id, int weapon) {
    if (id == 0 || id == 0xFFFFFFFFu || weapon <= 0 || weapon > 46) return;
    std::lock_guard<std::mutex> lock(g_remoteMutex);
    g_remotePeds[id].weapon = weapon;
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
void CopyOnFoot(float heading, float vx, float vy, OnFootKeys& keys) {
    keys.fire = g_localFire.load();
    keys.duck = g_localDuck.load();
    const float pi = 3.14159265f;
    float camera = -heading;
    while (camera > pi) camera -= 2.f * pi;
    while (camera < -pi) camera += 2.f * pi;
    keys.camera = camera;
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
}
void NoteRemotePed(uint32_t id, float x, float y, float z, float heading, float vx, float vy, float vz) {
    if (id == 0 || id == 0xFFFFFFFFu) return;
    std::lock_guard<std::mutex> lock(g_remoteMutex);
    auto& ped = g_remotePeds[id];
    ped.x = x;
    ped.y = y;
    ped.z = z;
    ped.heading = heading;
    ped.vx = vx;
    ped.vy = vy;
    ped.vz = vz;
    ped.dirty = true;
}
void SetPedHost(uint32_t id, bool host) {
    if (id == 0 || id == 0xFFFFFFFFu) return;
    std::lock_guard<std::mutex> lock(g_remoteMutex);
    g_remotePeds[id].hosted = host;
    if (!host) g_remotePeds[id].wander = false;
}
void ForgetRemote(uint32_t id) {
    std::lock_guard<std::mutex> lock(g_remoteMutex);
    g_forget.push_back(id);
}

// Reconnect (P3.1): the network thread clears the record set; the game thread
// drops the bodies in ApplyRemotePeds on its next tick.
void RequestRemoteReset() {
    {
        std::lock_guard<std::mutex> lock(g_remoteMutex);
        g_remotePeds.clear();
        g_forget.clear();
    }
    g_remoteResetPending = true;
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
    req.ticks = 0;
}
void NoteRemoteUnseat(uint32_t pedId) {
    if (pedId == 0 || pedId == 0xFFFFFFFFu) return;
    std::lock_guard<std::mutex> lock(g_remoteMutex);
    RemoteSeatRequest& req = g_seatRequests[pedId];
    req.vehicleId = kNoVehicle;
    req.seat = 0;
    req.seated = false;
    req.serial = ++g_seatSerial;
    req.ticks = 0;
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
void RequestWander(uint32_t id, int direction) {
    if (id == 0 || id == 0xFFFFFFFFu) return;
    std::lock_guard<std::mutex> lock(g_remoteMutex);
    auto& ped = g_remotePeds[id];
    ped.wander = true;
    ped.wanderDir = direction < 0 ? 0 : direction;
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
// Desktop has no queue: CClientGame::PostWeaponFire sends at once. The queue only bridges the game thread
// (CWeapon::Fire hook) and the network thread (SendBulletSync). At the cap the oldest shot is dropped.
void NoteLocalShot(int weapon, const float* start, const float* end) {
    if (!end) return;
    LocalShot shot;
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
    std::lock_guard<std::mutex> lock(g_remoteMutex);
    if (g_localShots.size() >= kMaxLocalShots) g_localShots.erase(g_localShots.begin());
    g_localShots.push_back(shot);
}
bool ConsumeLocalShot(LocalShot& out) {
    std::lock_guard<std::mutex> lock(g_remoteMutex);
    if (g_localShots.empty()) return false;
    out = g_localShots.front();
    g_localShots.erase(g_localShots.begin());
    return true;
}

#else
bool BeginServerSession() { return false; }
void ApplyServerSpawn(float, float, float) {}
void CopySpawn(float& x, float& y, float& z) { x = y = z = 0.f; }
void CopyPose(float& x, float& y, float& z, float& heading) { x = y = z = heading = 0.f; }
void CopyBody(float& x, float& y, float& z, float& heading, float& vx, float& vy, float& vz, float& health) {
    x = y = z = heading = vx = vy = vz = 0.f;
    health = 100.f;
}
void NoteDamage(uint32_t, float, uint8_t) {}
bool ConsumeDamage(uint32_t&, uint8_t&) { return false; }
void NoteRemoteAim(uint32_t, float, float, int, int, bool, bool) {}
void NoteRemoteWeapon(uint32_t, int) {}
bool RemoteFacingLocal(uint32_t, float, float) { return false; }
void CopyOnFoot(float, float, float, OnFootKeys& keys) { keys = {}; }
void NoteRemotePed(uint32_t, float, float, float, float, float, float, float) {}
void SetPedHost(uint32_t, bool) {}
void ForgetRemote(uint32_t) {}
void RequestRemoteReset() {}
void NoteRemoteVehicle(uint32_t, int, float, float, float, float) {}
void NoteRemoteSeat(uint32_t, uint32_t, uint8_t) {}
void NoteRemoteUnseat(uint32_t) {}
void NoteRemoteVehicleMotion(uint32_t, float, float, float, float, float, float) {}
void RequestWander(uint32_t, int) {}
bool CopyRemotePose(uint32_t, float& x, float& y, float& z) { x = y = z = 0.f; return false; }
int CopyHostedPeds(HostedPed*, int) { return 0; }
int CopyHostedCars(HostedCar*, int) { return 0; }
bool ConsumeLocalVehicleEvent(LocalVehicleEvent&) { return false; }
uint32_t LocalVehicleId() { return 0xFFFFFFFFu; }
bool CopyLocalCar(LocalCarState& out) {
    out = LocalCarState{};
    return false;
}
void NoteLocalShot(int, const float*, const float*) {}
bool ConsumeLocalShot(LocalShot& out) {
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