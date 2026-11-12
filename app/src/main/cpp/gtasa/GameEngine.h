#pragma once

#include <cstdint>
#include <memory>
#include <string>

#ifdef __ANDROID__
#include <android/log.h>
#define ENGINE_LOG_TAG "WorldRPGNeon"
#define ENGINE_LOGI(...) __android_log_print(ANDROID_LOG_INFO, ENGINE_LOG_TAG, __VA_ARGS__)
#define ENGINE_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, ENGINE_LOG_TAG, __VA_ARGS__)
#else
#include <cstdio>
#define ENGINE_LOGI(...) do { printf(__VA_ARGS__); printf("\n"); } while(0)
#define ENGINE_LOGE(...) do { fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } while(0)
#endif

namespace WorldRPG::Game {

using PedHandle = uint32_t;
using VehicleHandle = uint32_t;
using ObjectHandle = uint32_t;

constexpr PedHandle INVALID_PED = 0xFFFFFFFF;
constexpr VehicleHandle INVALID_VEHICLE = 0xFFFFFFFF;
constexpr ObjectHandle INVALID_OBJECT = 0xFFFFFFFF;

/// Version-specific function offsets for libGTASA.so
/// These must be updated for each GTA SA Mobile version
struct EngineOffsets {
    // Ped functions
    uintptr_t createPed;
    uintptr_t deletePed;
    uintptr_t setPedPosition;
    uintptr_t getPedPosition;
    uintptr_t setPedRotation;
    uintptr_t setPedHealth;
    uintptr_t getPedHealth;

    // Vehicle functions
    uintptr_t createVehicle;
    uintptr_t deleteVehicle;
    uintptr_t setVehiclePosition;
    uintptr_t getVehiclePosition;
    uintptr_t setVehicleRotation;
    uintptr_t setVehicleVelocity;
    uintptr_t setVehicleHealth;

    // Object functions
    uintptr_t createObject;
    uintptr_t deleteObject;
    uintptr_t setObjectPosition;
    uintptr_t setObjectRotation;

    // Model loading
    uintptr_t requestModel;
    uintptr_t loadAllModels;
    uintptr_t hasModelLoaded;

    // Player
    uintptr_t getPlayerPed;
    uintptr_t setPlayerControl;

    // World
    uintptr_t addEntityToWorld;
    uintptr_t removeEntityFromWorld;

    /// Initialize with default offsets for a specific game version
    static EngineOffsets forVersion(const char* version);
};

/// Abstract interface wrapping GTA SA Mobile engine functions.
/// All methods delegate to hooked engine functions via SymbolResolver.
class GameEngine {
public:
    GameEngine();
    ~GameEngine();

    GameEngine(const GameEngine&) = delete;
    GameEngine& operator=(const GameEngine&) = delete;

    /// Initialize engine hooks. Must be called after libGTASA.so is loaded.
    bool initialize(const EngineOffsets& offsets);

    /// Check if engine is initialized and hooks are active
    bool isReady() const;

    /// Lifecycle gating driven by the Android Activity pause/resume.
    /// While paused, engine frame work must not run.
    void pause();
    void resume();
    bool isPaused() const;

    // --- Ped operations ---
    PedHandle spawnPed(int modelId, float x, float y, float z, float heading);
    void deletePed(PedHandle handle);
    void setPedPosition(PedHandle handle, float x, float y, float z);
    void getPedPosition(PedHandle handle, float& x, float& y, float& z);
    void setPedRotation(PedHandle handle, float rx, float ry, float rz);
    void setPedHealth(PedHandle handle, float health);
    float getPedHealth(PedHandle handle);

    // --- Vehicle operations ---
    VehicleHandle spawnVehicle(int modelId, float x, float y, float z, float heading);
    void deleteVehicle(VehicleHandle handle);
    void setVehiclePosition(VehicleHandle handle, float x, float y, float z);
    void getVehiclePosition(VehicleHandle handle, float& x, float& y, float& z);
    void setVehicleRotation(VehicleHandle handle, float rx, float ry, float rz);
    void setVehicleVelocity(VehicleHandle handle, float vx, float vy, float vz);
    void setVehicleHealth(VehicleHandle handle, float health);

    // --- Object operations ---
    ObjectHandle spawnObject(int modelId, float x, float y, float z);
    void deleteObject(ObjectHandle handle);
    void setObjectPosition(ObjectHandle handle, float x, float y, float z);
    void setObjectRotation(ObjectHandle handle, float rx, float ry, float rz);

    // --- Model loading ---
    void requestModel(int modelId);
    void loadModels();
    bool hasModelLoaded(int modelId);

    // --- Player ---
    PedHandle getPlayerPed();
    void setPlayerControl(bool enable);

private:
    struct Impl;
    std::unique_ptr<Impl> pImpl_;
};

// Leaves the GTA frontend and blocks main.scm. MTA's PC client replaces
// CRunningScript::Process and never runs the story; MobileSA does the same
// through that function. The menu flag only starts a new game, which is the
// single-player path.
bool BeginServerSession();
void ApplyServerSpawn(float x, float y, float z);
void CopySpawn(float& x, float& y, float& z);
// Live pose sampled on the game thread. Heading is radians, -pi..pi.
void CopyPose(float& x, float& y, float& z, float& heading);
void CopyBody(float& x, float& y, float& z, float& heading,
    float& vx, float& vy, float& vz, float& health);
// Another player shot this ped. Health is reported in the next pure sync.
void NoteDamage(uint32_t attacker, float amount, uint8_t weapon);
bool ConsumeDamage(uint32_t& attacker, uint8_t& weapon);
// Keysync between pure-sync samples. Camera rotation is the left-stick basis:
// stick Y negative is forward along that camera, matching CNetAPI / CClientPed.
void NoteRemoteAim(uint32_t id, float heading, float camera, int stickX, int stickY, bool fire, bool duck);
void NoteRemoteWeapon(uint32_t id, int weapon);
// True when the remote ped is in front of itself and within melee reach of us.
bool RemoteFacingLocal(uint32_t id, float facing, float reach);
// Buttons the local ped is actually using, so outgoing keysync can set them.
struct OnFootKeys {
    bool fire = false;
    bool duck = false;
    int stickX = 0;
    int stickY = 0;
    float camera = 0;
};
void CopyOnFoot(float heading, float vx, float vy, OnFootKeys& keys);
// A server ped from PACKET_ID_PED_SYNC or ENTITY_ADD. Applied on the game thread.
void NoteRemotePed(uint32_t id, float x, float y, float z, float heading,
    float vx = 0, float vy = 0, float vz = 0);
void SetPedHost(uint32_t id, bool host);
void ForgetRemote(uint32_t id);
// P3.1: drop every remote body and car on the next game tick (reconnect).
void RequestRemoteReset();
void NoteRemoteVehicle(uint32_t id, int model, float x, float y, float z, float heading);
// VEHICLE_INOUT for another player (packet 58). Queued here and applied on the game thread in ApplyRemotePeds. A seated
// ped is not moved by its on-foot pose until it is unseated. Seat 0 is the driver. Other seats use the passenger task
// through the desktop seat table (PassengerGameDoor).
void NoteRemoteSeat(uint32_t pedId, uint32_t vehicleId, uint8_t seat);
void NoteRemoteUnseat(uint32_t pedId);
// Driver block of PLAYER_VEHICLE_PURESYNC (packet 33): move speed and turn speed of a remote car. Applied once per packet
// on the game thread (desktop CNetAPI::ReadVehiclePuresync, CClientVehicle::SetMoveSpeed / SetTurnSpeed).
void NoteRemoteVehicleMotion(uint32_t id, float mx, float my, float mz, float tx, float ty, float tz);
void RequestWander(uint32_t id, int direction);
bool CopyRemotePose(uint32_t id, float& x, float& y, float& z);
// Stage S4 (defined in shared/entity/PedSyncState.cpp). SetPedWander starts the
// hosted ped's wander task (RequestWander); ClearPedWander releases hosting,
// which clears the task in ApplyRemotePeds (SetPedHost(false)).
void SetPedWander(uint32_t id, int direction);
void ClearPedWander(uint32_t id);
// Stage S4: one observer pose (already interpolated) for the ped. Same
// effect as NoteRemotePed; called by PedSyncState::SamplePose.
void ApplyPedTarget(uint32_t id, float x, float y, float z, float heading,
    float vx, float vy, float vz);

// Native task presentation — matches mta_neon CClientPed interface.
// Locomotion mode: SNativeTaskLocomotionSync::eMode (NONE/WALK/RUN/SPRINT).
void SetPedNativeTaskLocomotion(uint32_t id, unsigned int mode);
// Weapon presentation: weapon type byte from SNativeTaskWeaponPresentationSync.
void SetPedNativeTaskWeapon(uint32_t id, uint8_t weaponType);
// Animation presentation: anim group + id from SNativeTaskAnimationPresentationSync.
void SetPedNativeTaskAnimation(uint32_t id, uint16_t animGroup, uint16_t animId);
// Physical takeover from PED_START_SYNC — seeds airborne/climb anchor state.
void SetPedNativeTaskPhysicalTakeover(uint32_t id, unsigned int mode,
    uint16_t animGroup, uint16_t animId, float progress);
// Camera rotation from keysync/ped sync packets.
void SetPedCameraRotation(uint32_t id, float rotation);
// OnFire / IsInWater state bits from ucFlags & 0x20 / 0x40.
void SetPedOnFire(uint32_t id, bool onFire);
void SetPedInWater(uint32_t id, bool inWater);
// Animation kill flag from ucFlags & 0x80 — clears synced anim state.
void KillPedAnimation(uint32_t id);
// Health/Armor lock semantics from CPedSync::Packet_PedStopSync.
// LockPedHealthCurrent/LockPedArmorCurrent freeze at the ped's current value.
void LockPedHealth(uint32_t id, float health);
void LockPedArmor(uint32_t id, float armor);
void LockPedHealthCurrent(uint32_t id);
void LockPedArmorCurrent(uint32_t id);
struct HostedPed {
    uint32_t id = 0;
    float x = 0, y = 0, z = 0, heading = 0;
    float vx = 0, vy = 0, vz = 0, health = 100;
};
int CopyHostedPeds(HostedPed* out, int capacity);
// Hosted unoccupied vehicles (packet 56 source). Rotation is in degrees, wrapped to [0, 360).
struct HostedCar {
    uint32_t id = 0;
    float x = 0, y = 0, z = 0;
    float rotX = 0, rotY = 0, rotZ = 0;
    float vx = 0, vy = 0, vz = 0;
    float tx = 0, ty = 0, tz = 0;
    // CVehicle::m_fHealth (+0x634 on libGTASA 2.10 arm64).
    float health = 1000.f;
};
int CopyHostedCars(HostedCar* out, int max);
// Local player (CJ) enter/exit of a server vehicle, for VEHICLE_INOUT (packet 58). The game thread
// detects the change in ApplyRemotePeds; the network thread drains it in NeonJoinClient::Pulse.
// vehicleId is the server ElementID from g_remoteCars. entered=true is a board, false is a leave.
struct LocalVehicleEvent {
    uint32_t vehicleId = 0xFFFFFFFFu;
    bool entered = false;
};
// Pops the oldest queued event. Returns false when the queue is empty.
bool ConsumeLocalVehicleEvent(LocalVehicleEvent& out);
// Server ElementID of the vehicle the local player is in, or 0xFFFFFFFF when on foot or in an unmapped car.
uint32_t LocalVehicleId();
// The local player's own car, for PLAYER_VEHICLE_PURESYNC (packet 33, driver). Sampled on the game thread in
// WatchLocalVehicle with SampleHostedCar, so the units match HostedCar: rotation in degrees [0, 360), move and
// turn speed per frame from the body. health is CVehicle::m_fHealth (+0x634).
struct LocalCarState {
    uint32_t vehicleId = 0xFFFFFFFFu;
    int model = 0;
    float x = 0, y = 0, z = 0;
    float rotX = 0, rotY = 0, rotZ = 0;
    float vx = 0, vy = 0, vz = 0;
    float tx = 0, ty = 0, tz = 0;
    float health = 1000.f;
    // Driver controls from CPad (game thread, WatchLocalVehicle). Zero when a getter is missing.
    // steering: CPad::GetSteeringLeftRight, -128..128 (desktop LeftStickX).
    // accelerate / brake: CPad::GetAccelerate / GetBrake, 0..255 (desktop ButtonCross / ButtonSquare).
    // handBrake: CPad::GetHandBrake != 0 (desktop RightShoulder1). horn: CPad::GetHorn (desktop ShockButtonL).
    // lookLeft / lookRight: CPad::GetLookLeft(true) / GetLookRight(true) (desktop LeftShoulder2 / RightShoulder2).
    int16_t steering = 0;
    uint8_t accelerate = 0;
    uint8_t brake = 0;
    bool handBrake = false;
    bool horn = false;
    bool lookLeft = false;
    bool lookRight = false;
};
// False when the local ped is not in a mapped vehicle (always false off Android).
bool CopyLocalCar(LocalCarState& out);

// Local bullet-weapon shot from CWeapon::Fire (PlayerPedPort.cpp LocalBulletFireHook). Desktop source:
// CClientGame::BulletFire (CClientGame.cpp 6242-6257) sets the start/end vectors that PostWeaponFire (6024-6026)
// sends as PLAYER_BULLETSYNC (98). weapon is the eWeaponType id, start/end are world coordinates.
struct LocalShot {
    int weapon = 0;
    float sx = 0, sy = 0, sz = 0;
    float ex = 0, ey = 0, ez = 0;
};
// Queues one shot. start null means the live ped position (CopyPose). end null is ignored. Game thread.
void NoteLocalShot(int weapon, const float* start, const float* end);
// Pops the oldest queued shot. Returns false when the queue is empty. Network thread.
bool ConsumeLocalShot(LocalShot& out);

} // namespace WorldRPG::Game