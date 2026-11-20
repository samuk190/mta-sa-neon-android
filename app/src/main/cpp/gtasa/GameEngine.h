#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "../shared/entity/PedSpawn.h"
#include "../shared/PedQueries.h"
#include "../shared/SpatialWorld.h"

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
// Install before GTA starts its threads; the network pulse only requests entry.
bool InstallServerSessionHook();
bool BeginServerSession();
// True once BeginServerSession left the frontend and the game is playing (read-only).
bool ServerSessionLive();
void ApplyServerSpawn(float x, float y, float z);
bool LocalPlacementPending();
void CopySpawn(float& x, float& y, float& z);
// Live pose sampled on the game thread. Heading is radians, -pi..pi.
void CopyPose(float& x, float& y, float& z, float& heading);
// Local-player facing for the radar arrow and the F11 map. Radians, -pi..pi.
// Same value FindPlayerHeading reads (forward row). Not the pure-sync heading.
float CopyRadarHeading();
void CopyBody(float& x, float& y, float& z, float& heading,
    float& vx, float& vy, float& vz, float& health);
// Local ped armour sampled beside health in SampleLocalPose. 0 until that sample runs.
float CopyLocalArmor();
// CLuaWorldDefs::GetGroundPosition (CLuaWorldDefs.cpp 872-893) via
// CWorldSA::FindGroundZFor3DPosition (CWorldSA.cpp 708-729):
// CWorld::FindGroundZFor3DCoord(x, y, z, nullptr, nullptr). The native runs on
// the game thread. False when the symbol is missing or that thread does not
// run the call; out is then unchanged.
bool FindGroundZFor3DPosition(float x, float y, float z, float& out);

// Client-only ped for createPed (CStaticFunctionDefinitions::CreatePed 2815-2852).
// The id is not a server element. The body is created on the game thread.
// Model 0 stays CJ. 0 means the call was rejected.
uint32_t NoteScriptPed(int model, float x, float y, float z, float rotationDegrees);
// CStaticFunctionDefinitions::SetElementModel for a client-created ped. Writes the stored
// model only. The GTA body is not rebuilt here (CPed::SetModel is not called).
bool NoteScriptPedModel(uint32_t id, int model);
bool ReleaseScriptPed(uint32_t id);
bool ScriptPedAlive(uint32_t id);
// addPedClothes / removePedClothes. type is the clothing slot. remove clears it.
void NoteScriptClothes(uint32_t id, const char* texture, const char* model, int type, bool remove);
// The texture and model NoteScriptClothes stored for this slot. False when the slot is empty.
bool CopyScriptClothes(uint32_t id, int type, char* texture, int textureBytes, char* model, int modelBytes);
// setElementRotation for a script ped: Z degrees, default order.
void NoteScriptRotation(uint32_t id, float rotationDegrees);
void NoteScriptFrozen(uint32_t id, bool frozen);
// setCameraMatrix position and look-at. Applied on the game thread until cleared.
void NoteScriptCamera(float x, float y, float z, float lookX, float lookY, float lookZ);
void ClearScriptCamera();
// setTime / setWeather. False when the arguments are outside the desktop range.
bool NoteScriptTime(int hour, int minute);
bool NoteScriptWeather(int weather);
// The weather from the last successful setWeather. False until one is set.
bool CopyScriptWeather(int& weather);
// createPed bookkeeping: model, heading in radians, frozen. False if that id is not a script ped.
bool CopyScriptPedFacts(uint32_t id, int& model, float& headingRadians, bool& frozen);
// CClientPed::SetHealth for the local player. Clamped and written through the existing health lock.
void NoteLocalHealth(float health);
// Another player shot this ped. Health is reported in the next pure sync.
void NoteDamage(uint32_t attacker, float amount, uint8_t weapon);
bool ConsumeDamage(uint32_t& attacker, uint8_t& weapon);
// Keysync between pure-sync samples. Camera rotation is the left-stick basis:
// stick Y negative is forward along that camera, matching CNetAPI / CClientPed.
void NoteRemoteAim(uint32_t id, float heading, float camera, int stickX, int stickY, bool fire, bool duck);
void NoteRemoteWeapon(uint32_t id, int weapon);
// One game-thread snapshot; false on unavailable native data or a stale element.
bool CopyPedQuery(uint32_t id, PedQueries::Snapshot& out);
// Enrich known elements with game-thread body positions and model bounds; adds script peds.
bool CopySpatialScene(std::vector<Spatial::Element>& elements);
// True when the remote ped is in front of itself and within melee reach of us.
bool RemoteFacingLocal(uint32_t id, float facing, float reach);
// Buttons the local ped is actually using, so outgoing keysync can set them.
struct OnFootKeys {
    bool fire = false;
    bool duck = false;
    // ButtonCross held (CPad::GetSprint) and a ButtonSquare press (jump) to send once.
    bool sprint = false;
    bool jump = false;
    int stickX = 0;
    int stickY = 0;
    // CCameraSA::GetCameraRotation: TheCamera.Orientation (libGTASA 2.10, TheCamera+0x154).
    // 0 until that float has been sampled.
    float camera = 0;
    // CPad::GetPad(0) LeftStickX and LeftStickY were both 0. The sticks above stay 0;
    // move speed must not rebuild them.
    bool padStickZero = false;
};
void CopyOnFoot(float heading, float vx, float vy, OnFootKeys& keys);
// Clears the jump press after an on-foot packet with ButtonSquare was written.
void ConsumeLocalJump();
// Clears the attack tap after an on-foot packet with ButtonCircle was written.
void ConsumeLocalAttack();
// A server ped from PACKET_ID_PED_SYNC or ENTITY_ADD. Applied on the game thread.
void NoteRemotePed(uint32_t id, float x, float y, float z, float heading,
    float vx = 0, float vy = 0, float vz = 0, bool applyVelocity = true);
// CNetAPI::ReadPlayerPuresync (CNetAPI.cpp 830-1021) for the calls the phone can run.
// hasVelocity writes move speed even when it is zero (SVelocitySync with the bit clear).
// Health and armor are applied to CPed +0x6AC / +0x6B4 on the game thread.
void NoteRemotePlayerSync(uint32_t id, float x, float y, float z, float heading,
    float vx, float vy, float vz, bool hasVelocity,
    int stickX, int stickY, float camera, bool fire, bool duck,
    bool hasHealth, float health, bool hasArmor, float armor);
void SetPedHost(uint32_t id, bool host);
void ForgetRemote(uint32_t id);
// P3.1: drop every remote body and car on the next game tick (reconnect).
void RequestRemoteReset();
void NoteRemoteVehicle(uint32_t id, int model, float x, float y, float z, float heading);
// VEHICLE_STARTSYNC (desktop CUnoccupiedVehicleSync::Packet_UnoccupiedVehicleStartSync): position, rotation in
// degrees, move speed, turn speed and health, written to the car body once on the game thread. Each array has 3 floats.
void NoteVehicleStartSync(uint32_t id, const float pos[3], const float rot[3], const float move[3], const float turn[3],
    float health);
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
// Snapshot of the locks Packet_PedSync / ReadPlayerPuresync stored.
// False when this id has neither lock.
bool CopyPedVitalsLock(uint32_t id, bool& healthLocked, float& health, bool& armorLocked, float& armor);
// Game thread: a pending LockPed*Current takes the body's value (CPedSync.cpp 280-281).
void ResolvePedVitalsLockCurrent(uint32_t id, float bodyHealth, float bodyArmor);
// UnlockHealth + UnlockArmor (CPedSync.cpp 256-257).
void UnlockPedVitals(uint32_t id);
// Drop locks and presentation state for an element that no longer exists.
void ForgetPedNativeTaskState(uint32_t id);
// CClientPed::SetHealth / SetArmor once (Packet_EntityAdd 4057-4058, Packet_PedStartSync 260-261). Written to the
// body on the next game tick through the existing one-shot fields (ApplySyncedPlayerFields).
void SetPedVitals(uint32_t id, float health, float armor);
// PLAYER_CLOTHES (Packet_PlayerClothes): the clothes of a remote CJ, applied on the game thread.
void NoteRemoteClothes(uint32_t id, const std::vector<WorldRPG::Entity::PedClothes>& clothes);
// SFullKeysyncSync button flags of a remote player (0x01 L1, 0x02 R1, 0x04 Square, 0x08 Cross, 0x10 Circle,
// 0x20 Triangle, 0x40 ShockL, 0x80 PedWalk; SyncStructures.h 1004-1012).
void NoteRemoteButtons(uint32_t id, uint8_t buttons);
// Element RPCs (packet 77) applied to peds and players. The game thread writes the body (ApplyRemotePeds).
void RpcSetPosition(uint32_t id, float x, float y, float z);
void RpcSetFrozen(uint32_t id, bool frozen);
// CClientGame::SendPedWastedPacket for a ped this phone hosts (queued by the damage hook, sent by NeonJoinProtocol).
struct HostedWasted {
    uint32_t id = 0;
    uint32_t damager = 0xFFFFFFFFu;
    int weapon = 0;
    int bodyPart = 0;
    int animGroup = 0;
    int animId = 15;
    float x = 0, y = 0, z = 0;
};
bool ConsumeHostedWasted(HostedWasted& out);
// PED_WASTED (64) / PLAYER_WASTED (27): CClientPed::Kill on the game thread.
void NoteRemoteWasted(uint32_t id, int weapon, int animGroup, int animId);
// PLAYER_SPAWN of a remote player: alive again (CClientPlayer::Spawn).
void NoteRemoteSpawn(uint32_t id);
void RpcSetRotation(uint32_t id, float radians);
void RpcSetInterior(uint32_t id, uint8_t interior);
void RpcSetHealth(uint32_t id, float health);
void RpcSetArmor(uint32_t id, float armor);
// Phase 6 extras: ped animation (CPedRPCs.cpp 259-375) and weapon RPCs (CWeaponRPCs.cpp 26-260), queued per element in
// arrival order and run on the game thread against the body (the local player included, as on desktop).
struct PedRpcOp {
    enum class Kind : uint8_t {
        Animation,         // CClientPed::RunNamedAnimation
        KillAnimation,     // CClientPed::KillAnimation
        AnimationProgress, // CAnimBlendAssociationSA::SetCurrentProgress
        AnimationSpeed,    // CAnimBlendAssociationSA::SetCurrentSpeed
        GiveWeapon,        // CWeaponRPCs::GiveWeapon body
        TakeWeaponAmmo,    // CWeaponRPCs::TakeWeapon with ammo
        RemoveWeapon,      // CClientPed::RemoveWeapon (TakeWeapon without ammo, local player only)
        TakeAllWeapons,    // CClientPed::RemoveAllWeapons
        SetWeaponAmmo,     // CWeaponRPCs::SetWeaponAmmo body
        SetWeaponSlot,     // CClientPed::SetCurrentWeaponSlot
    };
    uint64_t queryGeneration = 0; // packet cache receipt, not an internal restore op
    Kind kind = Kind::Animation;
    std::string block;
    std::string name;
    int32_t time = -1;
    int32_t blend = 250;
    bool loop = false;
    bool updatePosition = false;
    bool interruptible = false;
    bool freezeLastFrame = false;
    float value = 0.f;      // progress or speed
    int weapon = 0;
    unsigned ammo = 0;
    unsigned ammoInClip = 0;
    bool setCurrent = false;
    int slot = 0;
    // Game thread only: frames spent waiting for the anim block or the weapon model to stream in.
    int waits = 0;
    bool requested = false;
};
void QueuePedRpc(uint32_t id, const PedRpcOp& op);
// SET_ELEMENT_ALPHA (CElementRPCs.cpp 422-454 -> CClientPed::SetAlpha) and SET_ELEMENT_COLLISIONS_ENABLED (658-696 ->
// CClientPed::SetUsesCollision). Stored on the element and written to every body it gets.
void RpcSetAlpha(uint32_t id, uint8_t alpha);
void RpcSetCollisions(uint32_t id, bool enabled);
struct HostedPed {
    uint32_t id = 0;
    float x = 0, y = 0, z = 0, heading = 0;
    float vx = 0, vy = 0, vz = 0, health = 100;
    float armor = 0;
    // True when health and armor were read from the live body (CPed +0x6AC / +0x6B4).
    bool bodyVitals = false;
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
