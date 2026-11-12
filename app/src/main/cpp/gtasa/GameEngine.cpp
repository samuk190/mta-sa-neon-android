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
    // Server ped from the ENTITY_ADD actor list (kind 14). Its body is not created yet.
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
};
static std::unordered_map<uint32_t, RemoteCar> g_remoteCars;
static std::vector<uint32_t> g_forget;
static std::atomic<bool> g_remoteResetPending{false};
static std::vector<HostedPed> g_hostedSnap;
static std::unordered_map<uint32_t, RemoteBody> g_remoteBodies;

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

static void DropBody(void* body, bool car) {
    TaskFns& api = Tasks();
    if (api.worldRemove && body) api.worldRemove(body);
    if (car && api.carDtor && body) api.carDtor(body);
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

static void ApplyRemotePeds() {
    NoteGameFrame();
    if (g_remoteResetPending.exchange(false)) {
        for (auto& entry : g_remoteBodies) {
            ClearRemoteStick(entry.second.ped);
            DropBody(entry.second.ped, false);
        }
        g_remoteBodies.clear();
        for (auto& entry : g_remoteCars) DropBody(entry.second.body, true);
        g_remoteCars.clear();
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
            auto body = g_remoteBodies.find(id);
            if (body != g_remoteBodies.end()) {
                ClearRemoteStick(body->second.ped);
                DropBody(body->second.ped, false);
                g_remoteBodies.erase(body);
            }
            auto car = g_remoteCars.find(id);
            if (car != g_remoteCars.end()) {
                DropBody(car->second.body, true);
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
        // Server peds get no body yet: creating one crashed the engine in CPedGroup (SIGSEGV 0x378).
        if (ped.serverPed && found == g_remoteBodies.end()) continue;
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
            if (!EnsureWorldModel(car.model) || !api.carCtor || !g_worldAdd || g_remoteCars.size() > 48) continue;
            found->second.owned = std::make_unique<std::byte[]>(0x2000);
            body = found->second.owned.get();
            std::memset(body, 0, 0x2000);
            api.carCtor(body, car.model, 2, 0);
            g_worldAdd(body);
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
void RequestWander(uint32_t, int) {}
bool CopyRemotePose(uint32_t, float& x, float& y, float& z) { x = y = z = 0.f; return false; }
int CopyHostedPeds(HostedPed*, int) { return 0; }
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