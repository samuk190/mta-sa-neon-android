#include "WorldNativeHooks.h"

#include "GameEngine.h"
#include "GameTasks.h"
#include "WorldObjects.h"
#include "../shared/EngineWorld.h"
#include "../shared/ServerWorld.h"
#include "../shared/WorldNativeBridge.h"

#include <android/log.h>
#include <dlfcn.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <vector>

#define WORLD_HOOK_LOGI(...) __android_log_print(ANDROID_LOG_INFO, "WorldRPG_WorldHooks", __VA_ARGS__)
#define WORLD_HOOK_LOGW(...) __android_log_print(ANDROID_LOG_WARN, "WorldRPG_WorldHooks", __VA_ARGS__)

namespace WorldRPG::Gta {
namespace {

constexpr float kRadToDeg = 57.29577951308232f;

struct CVector {
    float x, y, z;
};

struct RwRGBA {
    uint8_t r, g, b, a;
};

// Verified ARM64 CPool header layout (matches RenderGuard.cpp:265-270 and
// _ZN9CBuildingnwEm / _ZN6CDummynwEm in libGTASA.so 2.10 arm64):
// +0x00: void* m_pObjects
// +0x08: uint8_t* m_byteMap (bit 7 = 0x80 => free slot)
// +0x10: int32_t m_nSize
// +0x14: int32_t m_nFirstFree
struct NativePoolHeader {
    void* objects;
    uint8_t* byteMap;
    int32_t size;
    int32_t firstFree;
};

// Verified libGTASA.so 2.10 arm64 CEntity / CPlaceable offsets:
//   +0x08: CSimpleTransform { float x, y, z, heading } (verified _ZNK7CEntity14GetBoundCentreER7CVector)
//   +0x18: CMatrix* m_matrix (vRight +0x00, vUp +0x10, vAt +0x20, vPos +0x30)
//   +0x32: int16_t m_nModelIndex (verified _ZN7CEntity14DeleteRwObjectEv)
//   +0x4B: uint8_t m_nAreaCode
//   +0x5A: uint8_t m_nType (low 3 bits: 1=building, 2=vehicle, 3=ped, 4=object, 5=dummy)
//   CBuilding & CDummy pool element stride: 0x60 (verified _ZN9CBuildingnwEm & _ZN6CDummynwEm)
constexpr size_t kBuildingOrDummyStride = 0x60;
constexpr size_t kEntityTransformOff = 0x08;
constexpr size_t kEntityMatrixPtrOff = 0x18;
constexpr size_t kEntityModelIdOff = 0x32;
constexpr size_t kEntityAreaCodeOff = 0x4B;
constexpr size_t kEntityTypeOff = 0x5A;

// Verified libGTASA.so 2.10 arm64 CGarages::aGarages layout:
//   stride = 0xF4 (verified _ZN8CGarages12IsGarageOpenEs)
//   +0x00: float x, y, z; +0x1C: Height, Width, Depth; +0x28: Left, Right, Front, Back
//   (verified _ZN7CGarage19IsPointInsideGarageE7CVector & desktop CGarageSA.h)
constexpr size_t kGarageStride = 0xF4;
constexpr int kMaxGarages = 50;

// Pool symbol table aligned 1:1 with EngineWorld.cpp kPools[0..19].
const char* kPoolSymbols[Models::kPoolCount] = {
    "_ZN6CPools16ms_pBuildingPoolE",
    "_ZN6CPools11ms_pPedPoolE",
    "_ZN6CPools14ms_pObjectPoolE",
    "_ZN6CPools13ms_pDummyPoolE",
    "_ZN6CPools15ms_pVehiclePoolE",
    "_ZN6CPools16ms_pColModelPoolE",
    "_ZN6CPools12ms_pTaskPoolE",
    "_ZN6CPools13ms_pEventPoolE",
    "_ZN6CPools21ms_pTaskAllocatorPoolE",
    "_ZN6CPools23ms_pPedIntelligencePoolE",
    "_ZN6CPools20ms_pPedAttractorPoolE",
    "_ZN6CPools21ms_pEntryInfoNodePoolE",
    "_ZN6CPools17ms_pNodeRoutePoolE",
    "_ZN6CPools19ms_pPatrolRoutePoolE",
    "_ZN6CPools18ms_pPointRoutePoolE",
    "_ZN6CPools25ms_pPtrNodeDoubleLinkPoolE",
    "_ZN6CPools25ms_pPtrNodeSingleLinkPoolE",
    "_ZN24CCustomCarEnvMapPipeline24m_gEnvMapPipeMatDataPoolE",
    "_ZN24CCustomCarEnvMapPipeline27m_gEnvMapPipeAtmDataPoolE",
    "_ZN24CCustomCarEnvMapPipeline25m_gSpecMapPipeMatDataPoolE",
};

void* g_lib = nullptr;
NativePoolHeader** g_pools[Models::kPoolCount]{};

// Verified ARM64 function pointer signatures (cross-checked with nm -D + c++filt on libGTASA.so 2.10).
using AddExplosionFn = bool (*)(void* victim, void* creator, int type, CVector pos, uint32_t time, uint8_t makeSound, float camShake, uint8_t isVisible);
using StartFireFn = void* (*)(void* mgr, CVector pos, float size, uint8_t unknown, void* creator, uint32_t time, int8_t numGenerations, uint8_t arg);
using StartEntityFireFn = void* (*)(void* mgr, void* target, void* creator, float size, uint8_t unknown, uint32_t time, int8_t numGenerations);
using ExtinguishPointFn = void (*)(void* mgr, CVector pos, float radius);
using AddPointLightFn = void (*)(uint8_t type, CVector pos, CVector dir, float radius, float r, float g, float b, uint8_t fogType, bool extraShadows, void* entity);
using LoadSceneFn = void (*)(const CVector& pos);
using CreateSwatRopeFn = void (*)(const CVector& pos);
using TestSphereFn = void* (*)(CVector centre, float radius, void* ignoredEntity, bool checkBuildings, bool checkVehicles, bool checkPeds, bool checkObjects, bool checkDummies, bool cameraIgnore);
using WorldEntityFn = void (*)(void* entity);

// Verified player/ped/entity/vehicle libGTASA.so 2.10 arm64 function signatures.
using PedGetBonePositionFn = void (*)(void* ped, CVector& out, uint32_t boneId, bool dynamic);
using EntityUpdateRpHAnimFn = void (*)(void* entity);
using GetAnimHierarchyFromSkinClumpFn = void* (*)(void* clump);
using RpHAnimIDGetIndexFn = int32_t (*)(void* hierarchy, int32_t boneId);
using RpHAnimHierarchyGetMatrixArrayFn = void* (*)(void* hierarchy);
using RpHAnimHierarchyUpdateMatricesFn = int32_t (*)(void* hierarchy);
using StatsGetStatValueFn = float (*)(uint16_t stat);
using StatsSetStatValueFn = void (*)(uint16_t stat, float value);
using PedSetMoveAnimFn = void (*)(void* ped);
using PedRemoveBodyPartFn = void (*)(void* ped, int32_t bodyPart, int8_t dir);
using PedSayFn = int16_t (*)(void* ped, uint16_t phrase, uint32_t startTimeDelay, float probability, uint8_t overrideSilence, uint8_t isForceAudible, uint8_t isFrontEnd);
using EntityGetComDistFn = float (*)(void* entity);
using EntityGetIsOnScreenFn = bool (*)(void* entity);
using VehicleFixFn = void (*)(void* vehicle);
using VehicleNumContactWheelsFn = uint8_t (*)(void* vehicle);

using FxBloodFn = void (*)(void* fx, CVector& pos, CVector& dir, int count, float brightness);
using FxBulletImpactFn = void (*)(void* fx, CVector& pos, CVector& dir, int smokeSize, int sparkCount, float smokeIntensity);
using FxSparksFn = void (*)(void* fx, CVector& pos, CVector& dir, float force, int count, CVector across, uint8_t blur, float spread, float life);
using FxTyreBurstFn = void (*)(void* fx, CVector& pos, CVector& dir);
using FxPunchImpactFn = void (*)(void* fx, CVector& pos, CVector& dir, int material);
using FxDebrisFn = void (*)(void* fx, CVector& pos, RwRGBA& color, float scale, int count);

AddExplosionFn g_addExplosion = nullptr;
void* g_fireManager = nullptr;
StartFireFn g_startFire = nullptr;
StartEntityFireFn g_startEntityFire = nullptr;
ExtinguishPointFn g_extinguishPoint = nullptr;
AddPointLightFn g_addPointLight = nullptr;
LoadSceneFn g_loadScene = nullptr;
LoadSceneFn g_loadSceneCollision = nullptr;
CreateSwatRopeFn g_createSwatRope = nullptr;
TestSphereFn g_testSphere = nullptr;
WorldEntityFn g_worldRemove = nullptr;
WorldEntityFn g_worldAdd = nullptr;
uint8_t* g_worldPlayers = nullptr;  // CWorld::Players[0] (CPlayerInfo: +0xF0 m_nMoney, +0xF4 m_nDisplayMoney)
PedGetBonePositionFn g_pedGetBonePosition = nullptr;
EntityUpdateRpHAnimFn g_entityUpdateRpHAnim = nullptr;
GetAnimHierarchyFromSkinClumpFn g_getAnimHierarchy = nullptr;
RpHAnimIDGetIndexFn g_rpHAnimIdGetIndex = nullptr;
RpHAnimHierarchyGetMatrixArrayFn g_rpHAnimGetMatrixArray = nullptr;
RpHAnimHierarchyUpdateMatricesFn g_rpHAnimUpdateMatrices = nullptr;
StatsGetStatValueFn g_statsGetStatValue = nullptr;
StatsSetStatValueFn g_statsSetStatValue = nullptr;
PedSetMoveAnimFn g_pedSetMoveAnim = nullptr;
PedRemoveBodyPartFn g_pedRemoveBodyPart = nullptr;
PedSayFn g_pedSay = nullptr;
void** g_modelInfoPtrs = nullptr;  // CModelInfo::ms_modelInfoPtrs[20000]
bool g_modelColOffsetVerified = false;
EntityGetComDistFn g_entityGetComDist = nullptr;
EntityGetIsOnScreenFn g_entityGetIsOnScreen = nullptr;
bool g_vehicleVtableFixVerified = false;
bool g_vehicleVtableWheelsVerified = false;
uint8_t* g_garages = nullptr;
int32_t* g_numGarages = nullptr;
using GarageMethodFn = void (*)(void* garage);
using IsGarageOpenFn = bool (*)(int16_t garageId);
GarageMethodFn g_garageOpen = nullptr;
GarageMethodFn g_garageClose = nullptr;
IsGarageOpenFn g_isGarageOpen = nullptr;

// Verified libGTASA.so 2.10 arm64 world/weather/clock/postfx/area globals (exact sizes verified via nm -D --print-size).
float* g_timeScale = nullptr;                   // CTimer::ms_fTimeScale (4B)
uint32_t* g_msPerGameMinute = nullptr;          // CClock::ms_nMillisecondsPerGameMinute (4B)
int16_t* g_newWeatherType = nullptr;            // CWeather::NewWeatherType (2B)
int16_t* g_forcedWeatherType = nullptr;         // CWeather::ForcedWeatherType (2B)
float* g_weatherInterp = nullptr;               // CWeather::InterpolationValue (4B)
float* g_weatherRain = nullptr;                 // CWeather::Rain (4B)
bool* g_scriptsForceRain = nullptr;             // CWeather::bScriptsForceRain (1B)
float* g_weatherWind = nullptr;                 // CWeather::Wind (4B)
CVector* g_weatherWindDir = nullptr;            // CWeather::WindDir (12B)
float* g_weatherWavyness = nullptr;             // CWeather::Wavyness (4B)
float* g_weatherHeatHaze = nullptr;             // CWeather::HeatHaze (4B)
int32_t* g_moonSize = nullptr;                  // CCoronas::MoonSize (4B)
int32_t* g_currArea = nullptr;                  // CGame::currArea (4B)
bool* g_speedFx = nullptr;                      // CPostEffects::m_bSpeedFX (1B)
int32_t* g_speedFxAlpha = nullptr;              // CPostEffects::m_SpeedFXAlpha (4B)
bool* g_grainEnable = nullptr;                  // CPostEffects::m_bGrainEnable (1B)
int32_t* g_grainStrength = nullptr;             // CPostEffects::m_grainStrength (4B)
bool* g_heatHazeEnable = nullptr;               // CPostEffects::m_bHeatHazeFX (1B)
int32_t* g_heatHazeIntensity = nullptr;         // CPostEffects::m_HeatHazeFXIntensity (4B)
int32_t* g_heatHazeRandomShift = nullptr;       // CPostEffects::m_HeatHazeFXRandomShift (4B)
int32_t* g_heatHazeSpeedMin = nullptr;          // CPostEffects::m_HeatHazeFXSpeedMin (4B)
int32_t* g_heatHazeSpeedMax = nullptr;          // CPostEffects::m_HeatHazeFXSpeedMax (4B)
int32_t* g_heatHazeScanX = nullptr;             // CPostEffects::m_HeatHazeFXScanSizeX (4B)
int32_t* g_heatHazeScanY = nullptr;             // CPostEffects::m_HeatHazeFXScanSizeY (4B)
int32_t* g_heatHazeRenderX = nullptr;           // CPostEffects::m_HeatHazeFXRenderSizeX (4B)
int32_t* g_heatHazeRenderY = nullptr;           // CPostEffects::m_HeatHazeFXRenderSizeY (4B)
int32_t* g_numOccludersOnMap = nullptr;         // COcclusion::NumOccludersOnMap (4B)
bool g_savedOccludersValid = false;
int32_t g_savedOccludersCount = 0;

// CTimeCycle 23-weather x 8-hour tables (0xB8 = 184 uint8_t, 0x170 = 184 int16_t).
constexpr size_t kTimeCycleEntries = 23 * 8;
int16_t* g_tcFarClip = nullptr;
int16_t* g_tcFogStart = nullptr;
uint8_t* g_tcSunSize = nullptr;
uint8_t* g_tcSkyTop[3] = {};
uint8_t* g_tcSkyBottom[3] = {};
uint8_t* g_tcSunCore[3] = {};
uint8_t* g_tcSunCorona[3] = {};
uint8_t* g_tcWater[4] = {};
uint8_t* g_tcCloudAlpha = nullptr;

bool g_savedFarClip = false;
std::array<int16_t, kTimeCycleEntries> g_origFarClip{};
bool g_savedFogStart = false;
std::array<int16_t, kTimeCycleEntries> g_origFogStart{};
bool g_savedSunSize = false;
std::array<uint8_t, kTimeCycleEntries> g_origSunSize{};
bool g_savedSky = false;
std::array<uint8_t, kTimeCycleEntries> g_origSkyTop[3]{};
std::array<uint8_t, kTimeCycleEntries> g_origSkyBottom[3]{};
bool g_savedSunColor = false;
std::array<uint8_t, kTimeCycleEntries> g_origSunCore[3]{};
std::array<uint8_t, kTimeCycleEntries> g_origSunCorona[3]{};
bool g_savedWaterColor = false;
std::array<uint8_t, kTimeCycleEntries> g_origWater[4]{};
bool g_savedCloudAlpha = false;
std::array<uint8_t, kTimeCycleEntries> g_origCloudAlpha{};

// Verified libGTASA.so 2.10 arm64 Camera, PostFX color filter, Blur, and Water symbols/offsets.
using CameraSetFadeColourFn = void (*)(void* cam, uint8_t r, uint8_t g, uint8_t b);
using CameraFadeFn = void (*)(void* cam, float duration, int16_t inOut);
using CameraCamShakeFn = void (*)(void* cam, float strength, float x, float y, float z);
using CameraSetNearClipScriptFn = void (*)(void* cam, float dist);
using PostFxVisionSwitchFn = void (*)(bool enable);
using MBlurSetDrunkFn = void (*)(float intensity);
using MBlurClearDrunkFn = void (*)();
using WaterGetLevelFn = bool (*)(float x, float y, float z, float* outHeight, bool touchWaves, CVector* outNormal);
using WaterGetLevelNoWavesFn = bool (*)(float x, float y, float z, float* outHeight, float* f1, float* f2);
using WaterTestLineFn = bool (*)(CVector start, CVector end, CVector* outHit);

// Verified from _ZN7CCamera17SetNearClipScriptEf (0x4ba948): strb w8, [x0, #0x37].
constexpr size_t kCameraUseNearClipScriptOff = 0x37;
// Verified from _ZN11CWaterLevel18AddWaterLevelVertexEiiifffh (0x6b99fc): stride 0x14, +0x04 float z, max 1021 verts.
constexpr size_t kWaterVertexStride = 0x14;
constexpr size_t kWaterVertexZOff = 0x04;
constexpr int32_t kMaxWaterVertices = 1021;

uint8_t* g_theCamera = nullptr;
CameraSetFadeColourFn g_camSetFadeColour = nullptr;
CameraFadeFn g_camFade = nullptr;
CameraCamShakeFn g_camShake = nullptr;
CameraSetNearClipScriptFn g_camSetNearClipScript = nullptr;
PostFxVisionSwitchFn g_nightVisionSwitch = nullptr;
PostFxVisionSwitchFn g_infraredVisionSwitch = nullptr;
MBlurSetDrunkFn g_setDrunkBlur = nullptr;
MBlurClearDrunkFn g_clearDrunkBlur = nullptr;

bool* g_colorFilterEnable = nullptr;
uint8_t* g_tcPostFx1[4] = {};
uint8_t* g_tcPostFx2[4] = {};
bool g_savedPostFx = false;
std::array<uint8_t, kTimeCycleEntries> g_origPostFx1[4]{};
std::array<uint8_t, kTimeCycleEntries> g_origPostFx2[4]{};

WaterGetLevelFn g_waterGetLevel = nullptr;
WaterGetLevelNoWavesFn g_waterGetLevelNoWaves = nullptr;
WaterTestLineFn g_waterTestLine = nullptr;
uint8_t* g_waterVertices = nullptr;
int32_t* g_numWaterVertices = nullptr;
bool g_savedWaterVerts = false;
std::vector<float> g_origWaterVertsZ;

void* g_fx = nullptr;
FxBloodFn g_fxBlood = nullptr;
FxBloodFn g_fxWood = nullptr;
FxBulletImpactFn g_fxBulletImpact = nullptr;
FxSparksFn g_fxSparks = nullptr;
FxTyreBurstFn g_fxTyreBurst = nullptr;
FxPunchImpactFn g_fxPunchImpact = nullptr;
FxDebrisFn g_fxDebris = nullptr;
FxDebrisFn g_fxGlass = nullptr;

// Verified libGTASA.so 2.10 arm64 Batch E native symbols & offsets.
using CameraSetZoomFn = void (*)(void* cam, int16_t mode);
using CameraTakeControlFn = void (*)(void* cam, void* target, int16_t mode, int16_t switchStyle, int32_t whoIsInControl);
using AutoSetTaxiLightFn = void (*)(void* veh, bool on);
using VehicleSetRemapFn = void (*)(void* veh, int32_t remap);
using VehicleUpgradeFn = void (*)(void* veh, int32_t upgrade);
using RequestUpgradeFn = void (*)(int32_t model, int32_t flags);
using HasUpgradeLoadedFn = bool (*)(int32_t model);
using TrainSetSpeedFn = void (*)(void* train, float speed);
using VehicleOpenDoorFn = void (*)(void* veh, void* ped, int32_t compId, int32_t doorId, float ratio, bool playNoise);
using VehicleBlowUpCarFn = void (*)(void* veh, void* damager, uint8_t hideExplosion);
using VehicleBurstTyreFn = void (*)(void* veh, uint8_t wheelId, bool bUsedByCop);

uint8_t* g_cheatsActive = nullptr;
CameraSetZoomFn g_camSetZoomVeh = nullptr;
CameraSetZoomFn g_camSetZoomPed = nullptr;
CameraTakeControlFn g_camTakeControl = nullptr;
AutoSetTaxiLightFn g_autoSetTaxiLight = nullptr;
VehicleSetRemapFn g_vehSetRemap = nullptr;
VehicleUpgradeFn g_vehAddUpgrade = nullptr;
VehicleUpgradeFn g_vehRemoveUpgrade = nullptr;
RequestUpgradeFn g_reqVehUpgrade = nullptr;
HasUpgradeLoadedFn g_hasVehUpgradeLoaded = nullptr;
TrainSetSpeedFn g_trainSetSpeed = nullptr;
bool g_vehicleVtableOpenDoorVerified = false;
bool g_vehicleVtableBlowUpVerified = false;
bool g_vehicleVtableBurstTyreVerified = false;

constexpr size_t kCheatsActiveSize = 0x6F;
constexpr size_t kVehEngineFlagsOff = 0x568;       // bit 4 (0x10) = bEngineOn, bit 6 (0x40) = bLightsOn, bit 31 (0x80000000) = bTyresDontBurst
constexpr size_t kVehSirenFlagByteOff = 0x56D;     // bit 7 (0x80) = bSirenOrAlarm (CVehicle::ProcessSirenAndHorn)
constexpr size_t kVehColorOff = 0x574;             // 4 colour palette bytes (CVehicle::SetupRender)
constexpr size_t kVehNitroBoostsOff = 0x5EE;       // int8_t m_nNitroBoosts (CAutomobile::NitrousControl)
constexpr size_t kVehOverrideLightsOff = 0x61A;    // bits 3..4 (0x18): 0=auto, 1=off, 2=on (CVehicle::DoVehicleLights)
constexpr size_t kVehDirtLevelOff = 0x624;         // float m_fDirtLevel (0..15, CVehicle::MakeDirty)
constexpr size_t kVehDoorLockOff = 0x684;          // int32_t m_eDoorLock (CVehicle::CanPedOpenLocks)
constexpr size_t kVehClassOff = 0x734;             // uint32_t m_dwVehicleClass (0=auto, 6=train)
constexpr size_t kAutoHasNitroFlagsOff = 0x4B8;    // bit 19 (0x80000) = bHasNitro (CAutomobile::NitrousControl)
constexpr size_t kAutoNitroValueOff = 0xAC0;       // float m_fNitroValue (CAutomobile::NitrousControl)
constexpr size_t kPhysicalProofsByteOff = 0x66;    // bits: 0x04 bullet, 0x08 fire, 0x10 collision, 0x20 melee, 0x80 explosion
constexpr size_t kVehicleVtableOpenDoorSlot = 28;  // +0xE0: CVehicle::OpenDoor
constexpr size_t kVehicleVtableBlowUpSlot = 42;    // +0x150: CVehicle::BlowUpCar
constexpr size_t kVehicleVtableBurstTyreSlot = 45; // +0x168: CVehicle::BurstTyre

// Verified libGTASA.so 2.10 arm64 Batch F native symbols & offsets.
using DamageSetDoorFn = void (*)(void* dmg, int32_t door, uint32_t status);
using DamageSetLightFn = void (*)(void* dmg, int32_t light, uint32_t status);
using DamageSetPanelFn = void (*)(void* dmg, int32_t panel, uint32_t status);
using DamageSetWheelFn = void (*)(void* dmg, int32_t wheel, uint32_t status);
using SetClumpAlphaFn = void (*)(void* clump, int32_t alpha);
using CameraFindFovFn = float (*)(void* cam);
using CameraLerpFovFn = void (*)(void* cam, float fromFov, float toFov, float timeMs, bool ease);
using PedIntelGetJetPackFn = void* (*)(void* intel);
using TaskMgrGetSimplestActiveFn = void* (*)(void* taskMgr);
using TaskMgrGetSecondaryFn = void* (*)(void* taskMgr, int32_t slot);
using TaskGetSubTaskFn = void* (*)(void* task);
using TaskGetIdFn = int32_t (*)(void* task);
using PedSetAimFlagFn = void (*)(void* ped, float heading);
using PedSetLookFlagFn = void (*)(void* ped, float heading, bool likeUnused, bool turnBody);
using PedClearLookFlagFn = void (*)(void* ped);
using TextLoadMissionFn = void (*)(void* text, char* blockName);
using TextGetFn = uint16_t* (*)(void* text, const char* key);
using MessagesAddJumpQFn = void (*)(const char* key, uint16_t* text, uint32_t timeMs, uint16_t flags, bool addToBrief);
using MessagesAddBigFn = void (*)(uint16_t* text, uint32_t timeMs, uint16_t style);
using MessagesClearFn = void (*)(uint8_t flag);
using HudSetHelpMessageFn = void (*)(const char* key, uint16_t* text, bool quick, bool permanent, bool addToBrief, uint32_t menuId);
using WeaponInfoGetFn = uint8_t* (*)(int32_t weaponType, int8_t skill);

DamageSetDoorFn g_dmgSetDoor = nullptr;
DamageSetLightFn g_dmgSetLight = nullptr;
DamageSetPanelFn g_dmgSetPanel = nullptr;
DamageSetWheelFn g_dmgSetWheel = nullptr;
SetClumpAlphaFn g_setClumpAlpha = nullptr;
CameraFindFovFn g_camFindFov = nullptr;
CameraLerpFovFn g_camLerpFov = nullptr;
PedIntelGetJetPackFn g_intelGetJetPack = nullptr;
TaskMgrGetSimplestActiveFn g_taskMgrSimplestActive = nullptr;
TaskMgrGetSecondaryFn g_taskMgrSecondary = nullptr;
PedSetAimFlagFn g_pedSetAimFlag = nullptr;
PedSetLookFlagFn g_pedSetLookFlag = nullptr;
PedClearLookFlagFn g_pedClearLookFlag = nullptr;
void* g_theText = nullptr;
TextLoadMissionFn g_textLoadMission = nullptr;
TextGetFn g_textGet = nullptr;
MessagesAddJumpQFn g_msgAddJumpQ = nullptr;
MessagesAddBigFn g_msgAddBig = nullptr;
MessagesClearFn g_msgClear = nullptr;
HudSetHelpMessageFn g_hudSetHelp = nullptr;
WeaponInfoGetFn g_weaponInfoGet = nullptr;

// Verified libGTASA.so 2.10 arm64 offsets (disassembled):
constexpr size_t kAutoDamageManagerOff = 0x758;  // CAutomobile::Fix (0x67df24)
constexpr size_t kEntityColFlagsOff = 0x28;      // bit 0 = bUsesCollision (CPhysical::ApplyGravity 0x4e1ea0)
constexpr size_t kCameraActiveCamOff = 0x5F;     // CCamera::FindCamFOV (0x4b9ac4)
constexpr size_t kCameraCamStride = 0x228;       // CCamera::FindCamFOV (0x4b9ac4)
constexpr size_t kCameraCamFovOff = 0x204;       // CCamera::FindCamFOV (0x4b9ac4)
constexpr size_t kPedIntelPtrOff = 0x538;        // CPed::CPed (0x595128) & CPed::ProcessControl (0x598798)
constexpr size_t kPedCreatedByOff = 0x548;       // CPed::CPed (0x594e20): 1=game, 2=mission
constexpr size_t kPedWeaponsOff = 0x730;         // CPed::CPed (0x59503c) & ProcessControl (0x598774), stride 0x20
constexpr size_t kPedWeaponStride = 0x20;        // CPed::ProcessControl (0x598770: lsl #5)
constexpr size_t kWeaponStateOff = 0x04;         // CWeapon::Reload (0x700a2c): 2 == WEAPONSTATE_RELOADING
constexpr size_t kPedActiveWeaponSlotOff = 0x8DC;  // CPed::ProcessControl (0x59876c)
constexpr size_t kPedShootRateOff = 0x8DD;       // CPed::CPed (0x594f04, init 40)
constexpr size_t kPedWeaponAccuracyOff = 0x8DE;  // CPed::CPed (0x5950c0, init 60)
constexpr size_t kIntelTaskMgrOff = 0x08;        // CPedIntelligence::GetTaskJetPack (0x5b5cb0)
constexpr size_t kTaskVtableSubTaskSlot = 3;     // +0x18: CTask::GetSubTask (CTaskManager::GetSimplestTask 0x64e250)
constexpr size_t kTaskVtableGetIdSlot = 5;       // +0x28: CTask::GetId (CPedIntelligence::GetTaskJetPack 0x5b5cc8)

struct RemovedNativeEntity {
    void* entity = nullptr;
    uint16_t model = 0;
    float x = 0.f;
    float y = 0.f;
    float z = 0.f;
    uint8_t areaCode = 0;
};

std::mutex g_removedMutex;
std::vector<RemovedNativeEntity> g_removedEntities;

template <typename T>
T ResolveAny(void* handle, std::initializer_list<const char*> names) {
    if (!handle) return nullptr;
    for (const char* name : names) {
        if (void* sym = dlsym(handle, name)) return reinterpret_cast<T>(sym);
    }
    return nullptr;
}

bool ReadEntityPosition(const uint8_t* ent, float& x, float& y, float& z) {
    if (!ent) return false;
    const uint8_t* mat = nullptr;
    std::memcpy(&mat, ent + kEntityMatrixPtrOff, sizeof(mat));
    if (mat) {
        std::memcpy(&x, mat + 0x30, sizeof(float));
        std::memcpy(&y, mat + 0x34, sizeof(float));
        std::memcpy(&z, mat + 0x38, sizeof(float));
    } else {
        std::memcpy(&x, ent + kEntityTransformOff + 0x0, sizeof(float));
        std::memcpy(&y, ent + kEntityTransformOff + 0x4, sizeof(float));
        std::memcpy(&z, ent + kEntityTransformOff + 0x8, sizeof(float));
    }
    return std::isfinite(x) && std::isfinite(y) && std::isfinite(z);
}

void ReadEntityRotationDeg(const uint8_t* ent, float& rxDeg, float& ryDeg, float& rzDeg) {
    rxDeg = ryDeg = rzDeg = 0.f;
    if (!ent) return;
    const uint8_t* mat = nullptr;
    std::memcpy(&mat, ent + kEntityMatrixPtrOff, sizeof(mat));
    if (mat) {
        // Matches desktop CWorldSA::TestSphereAgainstWorld + ConvertMatrixToEulerAngles (negated).
        // CMatrix layout: right (+0x00), up (+0x10), at (+0x20).
        float right[3], up[3], at[3];
        std::memcpy(right, mat + 0x00, sizeof(right));
        std::memcpy(up, mat + 0x10, sizeof(up));
        std::memcpy(at, mat + 0x20, sizeof(at));
        const float rx = std::asin(std::clamp(up[2], -1.f, 1.f));
        float ry = 0.f, rz = 0.f;
        if (std::fabs(up[2]) < 0.99999f) {
            ry = std::atan2(-right[2], at[2]);
            rz = std::atan2(-up[0], up[1]);
        } else {
            rz = std::atan2(right[1], right[0]);
        }
        rxDeg = -rx * kRadToDeg;
        ryDeg = -ry * kRadToDeg;
        rzDeg = -rz * kRadToDeg;
    } else {
        float heading = 0.f;
        std::memcpy(&heading, ent + kEntityTransformOff + 0xC, sizeof(float));
        if (std::isfinite(heading)) rzDeg = -heading * kRadToDeg;
    }
}

bool MatchesRemovalSphere(float ex, float ey, float ez, float x, float y, float z, float radius) {
    const float dx = ex - x;
    const float dy = ey - y;
    const float dz = ez - z;
    return (dx * dx + dy * dy + dz * dz) <= (radius * radius);
}

int NativeGetPoolCapacity(int pool) {
    if (pool < 0 || pool >= Models::kPoolCount || !g_pools[pool]) return -1;
    NativePoolHeader* header = *g_pools[pool];
    if (!header || header->size < 0 || header->size > 2000000) return -1;
    return header->size;
}

int NativeGetPoolUsedCapacity(int pool) {
    if (pool < 0 || pool >= Models::kPoolCount || !g_pools[pool]) return -1;
    NativePoolHeader* header = *g_pools[pool];
    if (!header || !header->byteMap || header->size <= 0 || header->size > 2000000) return -1;
    int used = 0;
    for (int32_t i = 0; i < header->size; ++i) {
        if ((header->byteMap[i] & 0x80u) == 0) ++used;
    }
    return used;
}

bool NativeCreateExplosion(float x, float y, float z, int type, bool makeSound, float camShake, bool damaging) {
    (void)damaging;
    if (!g_addExplosion) return false;
    GameTasks::Defer([x, y, z, type, makeSound, camShake]() {
        if (g_addExplosion) {
            g_addExplosion(nullptr, nullptr, type, CVector{x, y, z}, 0, makeSound ? 1 : 0, camShake, 1);
        }
        return true;
    });
    return true;
}

bool NativeCreateFire(float x, float y, float z, float size) {
    if (!g_fireManager || !g_startFire) return false;
    GameTasks::Defer([x, y, z, size]() {
        if (g_fireManager && g_startFire) {
            g_startFire(g_fireManager, CVector{x, y, z}, size, 1, nullptr, 15000, 1, 1);
        }
        return true;
    });
    return true;
}

bool NativeExtinguishFire(float x, float y, float z, float radius) {
    if (!g_fireManager || !g_extinguishPoint) return false;
    GameTasks::Defer([x, y, z, radius]() {
        if (g_fireManager && g_extinguishPoint) {
            g_extinguishPoint(g_fireManager, CVector{x, y, z}, radius);
        }
        return true;
    });
    return true;
}

bool NativeAddPointLight(int type, float x, float y, float z, float dx, float dy, float dz,
                         float radius, float r, float g, float b, bool fog, bool shadow) {
    if (!g_addPointLight) return false;
    GameTasks::Defer([type, x, y, z, dx, dy, dz, radius, r, g, b, fog, shadow]() {
        if (g_addPointLight) {
            g_addPointLight(static_cast<uint8_t>(type), CVector{x, y, z}, CVector{dx, dy, dz},
                            radius, r, g, b, fog ? 1 : 0, shadow, nullptr);
        }
        return true;
    });
    return true;
}

bool NativePreloadWorldArea(float x, float y, float z, int mode) {
    const bool wantCol = (mode == 0 || mode == 2) && g_loadSceneCollision != nullptr;
    const bool wantMod = (mode == 0 || mode == 1) && g_loadScene != nullptr;
    if (!wantCol && !wantMod) return false;
    GameTasks::Defer([x, y, z, wantCol, wantMod]() {
        const CVector pos{x, y, z};
        if (wantCol && g_loadSceneCollision) g_loadSceneCollision(pos);
        if (wantMod && g_loadScene) g_loadScene(pos);
        return true;
    });
    return true;
}

bool NativeCreateSwatRope(float x, float y, float z, uint32_t durationMs) {
    (void)durationMs;
    if (!g_createSwatRope) return false;
    GameTasks::Defer([x, y, z]() {
        if (g_createSwatRope) {
            const CVector pos{x, y, z};
            g_createSwatRope(pos);
        }
        return true;
    });
    return true;
}

bool NativeTestSphereAgainstWorld(float x, float y, float z, float radius,
                                  bool checkBuildings, bool checkVehicles, bool checkPeds,
                                  bool checkObjects, bool checkDummies, bool cameraIgnore,
                                  WorldNative::SphereTestResult& out) {
    out = {};
    if (!g_testSphere) return false;
    bool ok = GameTasks::RunQuery([&]() {
        void* hit = g_testSphere(CVector{x, y, z}, radius, nullptr,
                                 checkBuildings, checkVehicles, checkPeds,
                                 checkObjects, checkDummies, cameraIgnore);
        if (!hit) return;
        const auto* ent = reinterpret_cast<const uint8_t*>(hit);
        out.hit = true;
        int16_t modelId = 0;
        std::memcpy(&modelId, ent + kEntityModelIdOff, sizeof(modelId));
        out.model = modelId >= 0 ? static_cast<uint16_t>(modelId) : 0;
        ReadEntityPosition(ent, out.x, out.y, out.z);
        ReadEntityRotationDeg(ent, out.rxDeg, out.ryDeg, out.rzDeg);
        out.entityType = static_cast<uint8_t>(ent[kEntityTypeOff] & 0x7u);
    });
    return ok;
}

bool NativeGetGarageGeometry(int garageId, float pos[3], float sizeWDH[3], float bbox[4]) {
    if (!g_garages || garageId < 0 || garageId >= kMaxGarages) return false;
    if (g_numGarages && *g_numGarages > 0 && garageId >= *g_numGarages) return false;
    const uint8_t* base = g_garages + static_cast<size_t>(garageId) * kGarageStride;
    float height = 0.f, width = 0.f, depth = 0.f;
    std::memcpy(pos, base + 0x00, sizeof(float) * 3);
    std::memcpy(&height, base + 0x1C, sizeof(float));
    std::memcpy(&width, base + 0x20, sizeof(float));
    std::memcpy(&depth, base + 0x24, sizeof(float));
    sizeWDH[0] = width;
    sizeWDH[1] = depth;
    sizeWDH[2] = height;
    std::memcpy(bbox, base + 0x28, sizeof(float) * 4);
    return std::isfinite(pos[0]) && std::isfinite(pos[1]) && std::isfinite(pos[2]);
}

int NativeRemoveWorldModel(uint16_t model, float radius, float x, float y, float z, int8_t interior) {
    if (!g_worldRemove || (!g_pools[0] && !g_pools[3])) return -1;
    if (radius < 0.f || !std::isfinite(radius)) return 0;
    int removed = 0;
    const bool ran = GameTasks::Run([&]() {
        const int poolIndices[2] = {0, 3};  // buildings, dummies
        std::lock_guard<std::mutex> lock(g_removedMutex);
        for (int poolIdx : poolIndices) {
            if (!g_pools[poolIdx]) continue;
            NativePoolHeader* pool = *g_pools[poolIdx];
            if (!pool || !pool->objects || !pool->byteMap || pool->size <= 0 || pool->size > 200000) continue;
            auto* base = reinterpret_cast<uint8_t*>(pool->objects);
            for (int32_t i = 0; i < pool->size; ++i) {
                if ((pool->byteMap[i] & 0x80u) != 0) continue;
                uint8_t* ent = base + static_cast<size_t>(i) * kBuildingOrDummyStride;
                int16_t entModel = 0;
                std::memcpy(&entModel, ent + kEntityModelIdOff, sizeof(entModel));
                if (entModel < 0 || static_cast<uint16_t>(entModel) != model) continue;
                const uint8_t areaCode = ent[kEntityAreaCodeOff];
                if (interior >= 0 && static_cast<int>(areaCode) != static_cast<int>(interior)) continue;
                float ex = 0.f, ey = 0.f, ez = 0.f;
                if (!ReadEntityPosition(ent, ex, ey, ez)) continue;
                if (!MatchesRemovalSphere(ex, ey, ez, x, y, z, radius)) continue;
                const bool alreadyRemoved = std::any_of(
                    g_removedEntities.begin(), g_removedEntities.end(),
                    [ent](const RemovedNativeEntity& e) { return e.entity == ent; });
                if (alreadyRemoved) continue;
                g_worldRemove(ent);
                g_removedEntities.push_back(RemovedNativeEntity{ent, model, ex, ey, ez, areaCode});
                ++removed;
            }
        }
    });
    return ran ? removed : 0;
}

int NativeRestoreWorldModel(uint16_t model, float radius, float x, float y, float z, int8_t interior) {
    if (!g_worldAdd) return -1;
    if (radius < 0.f || !std::isfinite(radius)) return 0;
    int restored = 0;
    const bool ran = GameTasks::Run([&]() {
        std::lock_guard<std::mutex> lock(g_removedMutex);
        for (auto it = g_removedEntities.begin(); it != g_removedEntities.end();) {
            const bool modelMatch = (it->model == model);
            const bool intMatch = (interior < 0 || static_cast<int>(it->areaCode) == static_cast<int>(interior));
            if (modelMatch && intMatch && MatchesRemovalSphere(it->x, it->y, it->z, x, y, z, radius)) {
                if (it->entity) g_worldAdd(it->entity);
                it = g_removedEntities.erase(it);
                ++restored;
            } else {
                ++it;
            }
        }
    });
    return ran ? restored : 0;
}

int NativeRestoreAllWorldModels() {
    if (!g_worldAdd) return -1;
    int restored = 0;
    const bool ran = GameTasks::Run([&]() {
        std::lock_guard<std::mutex> lock(g_removedMutex);
        for (const auto& entry : g_removedEntities) {
            if (entry.entity) {
                g_worldAdd(entry.entity);
                ++restored;
            }
        }
        g_removedEntities.clear();
    });
    return ran ? restored : 0;
}

bool NativeSetGameSpeed(float speed) {
    if (!g_timeScale) return false;
    GameTasks::Defer([speed]() {
        if (g_timeScale) *g_timeScale = speed;
        return true;
    });
    return true;
}

bool NativeSetMinuteDuration(uint32_t ms) {
    if (!g_msPerGameMinute) return false;
    GameTasks::Defer([ms]() {
        if (g_msPerGameMinute) *g_msPerGameMinute = ms;
        return true;
    });
    return true;
}

bool NativeSetWeatherBlended(int weather) {
    if (!g_newWeatherType) return false;
    GameTasks::Defer([weather]() {
        if (g_newWeatherType) *g_newWeatherType = static_cast<int16_t>(weather);
        if (g_forcedWeatherType) *g_forcedWeatherType = -1;
        if (g_weatherInterp) *g_weatherInterp = 0.f;
        return true;
    });
    return true;
}

bool NativeSetRainLevel(float level, bool forced) {
    if (!g_weatherRain) return false;
    GameTasks::Defer([level, forced]() {
        if (g_weatherRain) *g_weatherRain = level;
        if (g_scriptsForceRain) *g_scriptsForceRain = forced;
        return true;
    });
    return true;
}

bool NativeSetWindVelocity(float x, float y, float z, bool reset) {
    if (!g_weatherWind && !g_weatherWindDir) return false;
    GameTasks::Defer([x, y, z, reset]() {
        if (reset) {
            if (g_weatherWind) *g_weatherWind = 0.f;
            if (g_weatherWindDir) *g_weatherWindDir = CVector{0.f, 0.f, 0.f};
            return true;
        }
        const float mag = std::sqrt(x * x + y * y + z * z);
        if (g_weatherWind) *g_weatherWind = mag;
        if (g_weatherWindDir) {
            if (mag > 1e-6f) {
                *g_weatherWindDir = CVector{x / mag, y / mag, z / mag};
            } else {
                *g_weatherWindDir = CVector{0.f, 0.f, 0.f};
            }
        }
        return true;
    });
    return true;
}

bool NativeSetWaveHeight(float height) {
    if (!g_weatherWavyness) return false;
    GameTasks::Defer([height]() {
        if (g_weatherWavyness) *g_weatherWavyness = height;
        return true;
    });
    return true;
}

bool NativeSetMoonSize(int size) {
    if (!g_moonSize) return false;
    GameTasks::Defer([size]() {
        if (g_moonSize) *g_moonSize = size;
        return true;
    });
    return true;
}

bool NativeSetBlurLevel(int level) {
    if (!g_speedFxAlpha && !g_speedFx) return false;
    GameTasks::Defer([level]() {
        if (g_speedFxAlpha) *g_speedFxAlpha = std::clamp(level, 0, 255);
        if (g_speedFx) *g_speedFx = (level > 0);
        return true;
    });
    return true;
}

bool NativeSetGrainLevel(int level) {
    if (!g_grainStrength && !g_grainEnable) return false;
    GameTasks::Defer([level]() {
        if (g_grainStrength) *g_grainStrength = std::clamp(level, 0, 255);
        if (g_grainEnable) *g_grainEnable = (level > 0);
        return true;
    });
    return true;
}

bool NativeSetHeatHaze(int intensity, int randomShift, int speedMin, int speedMax,
                       int scanX, int scanY, int renderX, int renderY, bool showInside, bool reset) {
    (void)showInside;
    if (!g_heatHazeEnable && !g_heatHazeIntensity) return false;
    GameTasks::Defer([=]() {
        if (reset) {
            if (g_heatHazeEnable) *g_heatHazeEnable = false;
            if (g_heatHazeIntensity) *g_heatHazeIntensity = 0;
            if (g_weatherHeatHaze) *g_weatherHeatHaze = 0.f;
            return true;
        }
        if (g_heatHazeEnable) *g_heatHazeEnable = (intensity > 0);
        if (g_heatHazeIntensity) *g_heatHazeIntensity = std::clamp(intensity, 0, 255);
        if (g_heatHazeRandomShift) *g_heatHazeRandomShift = randomShift;
        if (g_heatHazeSpeedMin) *g_heatHazeSpeedMin = speedMin;
        if (g_heatHazeSpeedMax) *g_heatHazeSpeedMax = speedMax;
        if (g_heatHazeScanX) *g_heatHazeScanX = scanX;
        if (g_heatHazeScanY) *g_heatHazeScanY = scanY;
        if (g_heatHazeRenderX) *g_heatHazeRenderX = renderX;
        if (g_heatHazeRenderY) *g_heatHazeRenderY = renderY;
        if (g_weatherHeatHaze) *g_weatherHeatHaze = std::clamp(static_cast<float>(intensity) / 255.f, 0.f, 1.f);
        return true;
    });
    return true;
}

bool NativeSetFarClipDistance(float distance, bool reset) {
    if (!g_tcFarClip) return false;
    GameTasks::Defer([distance, reset]() {
        if (!g_tcFarClip) return true;
        if (!g_savedFarClip) {
            std::memcpy(g_origFarClip.data(), g_tcFarClip, sizeof(g_origFarClip));
            g_savedFarClip = true;
        }
        if (reset) {
            std::memcpy(g_tcFarClip, g_origFarClip.data(), sizeof(g_origFarClip));
        } else {
            const int16_t val = static_cast<int16_t>(std::clamp(std::lround(distance), -32768L, 32767L));
            std::fill_n(g_tcFarClip, kTimeCycleEntries, val);
        }
        return true;
    });
    return true;
}

bool NativeSetFogDistance(float distance, bool reset) {
    if (!g_tcFogStart) return false;
    GameTasks::Defer([distance, reset]() {
        if (!g_tcFogStart) return true;
        if (!g_savedFogStart) {
            std::memcpy(g_origFogStart.data(), g_tcFogStart, sizeof(g_origFogStart));
            g_savedFogStart = true;
        }
        if (reset) {
            std::memcpy(g_tcFogStart, g_origFogStart.data(), sizeof(g_origFogStart));
        } else {
            const int16_t val = static_cast<int16_t>(std::clamp(std::lround(distance), -32768L, 32767L));
            std::fill_n(g_tcFogStart, kTimeCycleEntries, val);
        }
        return true;
    });
    return true;
}

bool NativeSetSunSize(float size, bool reset) {
    if (!g_tcSunSize) return false;
    GameTasks::Defer([size, reset]() {
        if (!g_tcSunSize) return true;
        if (!g_savedSunSize) {
            std::memcpy(g_origSunSize.data(), g_tcSunSize, sizeof(g_origSunSize));
            g_savedSunSize = true;
        }
        if (reset) {
            std::memcpy(g_tcSunSize, g_origSunSize.data(), sizeof(g_origSunSize));
        } else {
            const uint8_t val = static_cast<uint8_t>(std::clamp(std::lround(size * 10.f), 0L, 255L));
            std::fill_n(g_tcSunSize, kTimeCycleEntries, val);
        }
        return true;
    });
    return true;
}

bool NativeSetSunColor(int coreR, int coreG, int coreB, int coronaR, int coronaG, int coronaB, bool reset) {
    if (!g_tcSunCore[0] || !g_tcSunCorona[0]) return false;
    GameTasks::Defer([=]() {
        if (!g_savedSunColor) {
            for (int c = 0; c < 3; ++c) {
                if (g_tcSunCore[c]) std::memcpy(g_origSunCore[c].data(), g_tcSunCore[c], kTimeCycleEntries);
                if (g_tcSunCorona[c]) std::memcpy(g_origSunCorona[c].data(), g_tcSunCorona[c], kTimeCycleEntries);
            }
            g_savedSunColor = true;
        }
        const int core[3] = {coreR, coreG, coreB};
        const int corona[3] = {coronaR, coronaG, coronaB};
        for (int c = 0; c < 3; ++c) {
            if (reset) {
                if (g_tcSunCore[c]) std::memcpy(g_tcSunCore[c], g_origSunCore[c].data(), kTimeCycleEntries);
                if (g_tcSunCorona[c]) std::memcpy(g_tcSunCorona[c], g_origSunCorona[c].data(), kTimeCycleEntries);
            } else {
                if (g_tcSunCore[c]) std::fill_n(g_tcSunCore[c], kTimeCycleEntries, static_cast<uint8_t>(std::clamp(core[c], 0, 255)));
                if (g_tcSunCorona[c]) std::fill_n(g_tcSunCorona[c], kTimeCycleEntries, static_cast<uint8_t>(std::clamp(corona[c], 0, 255)));
            }
        }
        return true;
    });
    return true;
}

bool NativeSetSkyGradient(int topR, int topG, int topB, int botR, int botG, int botB, bool reset) {
    if (!g_tcSkyTop[0] || !g_tcSkyBottom[0]) return false;
    GameTasks::Defer([=]() {
        if (!g_savedSky) {
            for (int c = 0; c < 3; ++c) {
                if (g_tcSkyTop[c]) std::memcpy(g_origSkyTop[c].data(), g_tcSkyTop[c], kTimeCycleEntries);
                if (g_tcSkyBottom[c]) std::memcpy(g_origSkyBottom[c].data(), g_tcSkyBottom[c], kTimeCycleEntries);
            }
            g_savedSky = true;
        }
        const int top[3] = {topR, topG, topB};
        const int bot[3] = {botR, botG, botB};
        for (int c = 0; c < 3; ++c) {
            if (reset) {
                if (g_tcSkyTop[c]) std::memcpy(g_tcSkyTop[c], g_origSkyTop[c].data(), kTimeCycleEntries);
                if (g_tcSkyBottom[c]) std::memcpy(g_tcSkyBottom[c], g_origSkyBottom[c].data(), kTimeCycleEntries);
            } else {
                if (g_tcSkyTop[c]) std::fill_n(g_tcSkyTop[c], kTimeCycleEntries, static_cast<uint8_t>(std::clamp(top[c], 0, 255)));
                if (g_tcSkyBottom[c]) std::fill_n(g_tcSkyBottom[c], kTimeCycleEntries, static_cast<uint8_t>(std::clamp(bot[c], 0, 255)));
            }
        }
        return true;
    });
    return true;
}

bool NativeSetWaterColor(int r, int g, int b, int a, bool reset) {
    if (!g_tcWater[0]) return false;
    GameTasks::Defer([=]() {
        if (!g_savedWaterColor) {
            for (int c = 0; c < 4; ++c) {
                if (g_tcWater[c]) std::memcpy(g_origWater[c].data(), g_tcWater[c], kTimeCycleEntries);
            }
            g_savedWaterColor = true;
        }
        const int col[4] = {r, g, b, a};
        for (int c = 0; c < 4; ++c) {
            if (!g_tcWater[c]) continue;
            if (reset) {
                std::memcpy(g_tcWater[c], g_origWater[c].data(), kTimeCycleEntries);
            } else {
                std::fill_n(g_tcWater[c], kTimeCycleEntries, static_cast<uint8_t>(std::clamp(col[c], 0, 255)));
            }
        }
        return true;
    });
    return true;
}

bool NativeSetCloudsEnabled(bool enabled) {
    if (!g_tcCloudAlpha) return false;
    GameTasks::Defer([enabled]() {
        if (!g_tcCloudAlpha) return true;
        if (!g_savedCloudAlpha) {
            std::memcpy(g_origCloudAlpha.data(), g_tcCloudAlpha, kTimeCycleEntries);
            g_savedCloudAlpha = true;
        }
        if (enabled) {
            std::memcpy(g_tcCloudAlpha, g_origCloudAlpha.data(), kTimeCycleEntries);
        } else {
            std::fill_n(g_tcCloudAlpha, kTimeCycleEntries, static_cast<uint8_t>(0));
        }
        return true;
    });
    return true;
}

bool NativeSetOcclusionsEnabled(bool enabled) {
    if (!g_numOccludersOnMap) return false;
    GameTasks::Defer([enabled]() {
        if (!g_numOccludersOnMap) return true;
        if (!g_savedOccludersValid) {
            g_savedOccludersCount = *g_numOccludersOnMap;
            g_savedOccludersValid = true;
        }
        *g_numOccludersOnMap = enabled ? g_savedOccludersCount : 0;
        return true;
    });
    return true;
}

bool NativeSetGarageOpen(int garageId, bool open) {
    if (!g_garages || garageId < 0 || garageId >= kMaxGarages) return false;
    if (open && !g_garageOpen) return false;
    if (!open && !g_garageClose) return false;
    GameTasks::Defer([garageId, open]() {
        if (!g_garages) return true;
        if (g_numGarages && *g_numGarages > 0 && garageId >= *g_numGarages) return true;
        void* garage = g_garages + static_cast<size_t>(garageId) * kGarageStride;
        if (open && g_garageOpen) g_garageOpen(garage);
        else if (!open && g_garageClose) g_garageClose(garage);
        return true;
    });
    return true;
}

bool NativeIsGarageOpen(int garageId, bool& outOpen) {
    if (!g_isGarageOpen || garageId < 0 || garageId >= kMaxGarages) return false;
    if (g_numGarages && *g_numGarages > 0 && garageId >= *g_numGarages) return false;
    bool open = false;
    const bool ok = GameTasks::RunQuery([&]() {
        open = g_isGarageOpen(static_cast<int16_t>(garageId));
    });
    if (!ok) return false;
    outOpen = open;
    return true;
}

bool NativeSetCurrentArea(int area) {
    if (!g_currArea) return false;
    GameTasks::Defer([area]() {
        if (g_currArea) *g_currArea = area;
        return true;
    });
    return true;
}

bool NativeFadeCamera(bool fadeIn, float duration, uint8_t r, uint8_t g, uint8_t b) {
    if (!g_theCamera || !g_camFade) return false;
    GameTasks::Defer([fadeIn, duration, r, g, b]() {
        if (!g_theCamera || !g_camFade) return true;
        if (!fadeIn && g_camSetFadeColour) {
            g_camSetFadeColour(g_theCamera, r, g, b);
        }
        g_camFade(g_theCamera, duration, fadeIn ? static_cast<int16_t>(1) : static_cast<int16_t>(0));
        return true;
    });
    return true;
}

bool NativeShakeCamera(float strength, float x, float y, float z) {
    if (!g_theCamera || !g_camShake) return false;
    GameTasks::Defer([strength, x, y, z]() {
        if (g_theCamera && g_camShake) {
            g_camShake(g_theCamera, strength, x, y, z);
        }
        return true;
    });
    return true;
}

bool NativeSetNearClipDistance(float distance, bool reset) {
    if (!g_theCamera || !g_camSetNearClipScript) return false;
    GameTasks::Defer([distance, reset]() {
        if (!g_theCamera || !g_camSetNearClipScript) return true;
        if (reset) {
            g_theCamera[kCameraUseNearClipScriptOff] = 0;
        } else {
            g_camSetNearClipScript(g_theCamera, distance);
        }
        return true;
    });
    return true;
}

bool NativeSetCameraGoggleEffect(int mode) {
    if (!g_nightVisionSwitch && !g_infraredVisionSwitch) return false;
    GameTasks::Defer([mode]() {
        if (mode == 1) {
            if (g_infraredVisionSwitch) g_infraredVisionSwitch(false);
            if (g_nightVisionSwitch) g_nightVisionSwitch(true);
        } else if (mode == 2) {
            if (g_nightVisionSwitch) g_nightVisionSwitch(false);
            if (g_infraredVisionSwitch) g_infraredVisionSwitch(true);
        } else {
            if (g_nightVisionSwitch) g_nightVisionSwitch(false);
            if (g_infraredVisionSwitch) g_infraredVisionSwitch(false);
        }
        return true;
    });
    return true;
}

bool NativeSetCameraDrunkLevel(int level) {
    if (!g_setDrunkBlur && !g_clearDrunkBlur) return false;
    GameTasks::Defer([level]() {
        if (level <= 0) {
            if (g_clearDrunkBlur) g_clearDrunkBlur();
            else if (g_setDrunkBlur) g_setDrunkBlur(0.f);
        } else if (g_setDrunkBlur) {
            g_setDrunkBlur(std::clamp(static_cast<float>(level) / 255.f, 0.f, 1.f));
        }
        return true;
    });
    return true;
}

bool NativeSetColorFilter(int r1, int g1, int b1, int a1, int r2, int g2, int b2, int a2, bool reset) {
    if (!g_tcPostFx1[0] && !g_colorFilterEnable) return false;
    GameTasks::Defer([=]() {
        if (!g_savedPostFx && g_tcPostFx1[0]) {
            for (int c = 0; c < 4; ++c) {
                if (g_tcPostFx1[c]) std::memcpy(g_origPostFx1[c].data(), g_tcPostFx1[c], kTimeCycleEntries);
                if (g_tcPostFx2[c]) std::memcpy(g_origPostFx2[c].data(), g_tcPostFx2[c], kTimeCycleEntries);
            }
            g_savedPostFx = true;
        }
        const int c1[4] = {r1, g1, b1, a1};
        const int c2[4] = {r2, g2, b2, a2};
        for (int c = 0; c < 4; ++c) {
            if (reset) {
                if (g_tcPostFx1[c]) std::memcpy(g_tcPostFx1[c], g_origPostFx1[c].data(), kTimeCycleEntries);
                if (g_tcPostFx2[c]) std::memcpy(g_tcPostFx2[c], g_origPostFx2[c].data(), kTimeCycleEntries);
            } else {
                if (g_tcPostFx1[c]) std::fill_n(g_tcPostFx1[c], kTimeCycleEntries, static_cast<uint8_t>(std::clamp(c1[c], 0, 255)));
                if (g_tcPostFx2[c]) std::fill_n(g_tcPostFx2[c], kTimeCycleEntries, static_cast<uint8_t>(std::clamp(c2[c], 0, 255)));
            }
        }
        if (g_colorFilterEnable) {
            *g_colorFilterEnable = !reset && (a1 > 0 || a2 > 0);
        }
        return true;
    });
    return true;
}

bool NativeGetWaterLevel(float x, float y, float z, bool ignoreWaves, float& outHeight) {
    if (!g_waterGetLevel && !g_waterGetLevelNoWaves) return false;
    bool hit = false;
    float height = 0.f;
    const bool ok = GameTasks::RunQuery([&]() {
        if (ignoreWaves && g_waterGetLevelNoWaves) {
            float f1 = 0.f, f2 = 0.f;
            hit = g_waterGetLevelNoWaves(x, y, z, &height, &f1, &f2);
        } else if (g_waterGetLevel) {
            hit = g_waterGetLevel(x, y, z, &height, !ignoreWaves, nullptr);
        }
    });
    if (!ok || !hit) return false;
    outHeight = height;
    return true;
}

bool NativeTestLineAgainstWater(float x1, float y1, float z1, float x2, float y2, float z2,
                                float& ox, float& oy, float& oz) {
    if (!g_waterTestLine) return false;
    bool hit = false;
    CVector out{0.f, 0.f, 0.f};
    const bool ok = GameTasks::RunQuery([&]() {
        hit = g_waterTestLine(CVector{x1, y1, z1}, CVector{x2, y2, z2}, &out);
    });
    if (!ok || !hit) return false;
    ox = out.x;
    oy = out.y;
    oz = out.z;
    return true;
}

bool NativeSetWorldWaterLevel(float height, bool reset) {
    if (!g_waterVertices || !g_numWaterVertices) return false;
    GameTasks::Defer([height, reset]() {
        if (!g_waterVertices || !g_numWaterVertices) return true;
        const int32_t count = std::clamp(*g_numWaterVertices, 0, kMaxWaterVertices);
        if (count <= 0) return true;
        if (!g_savedWaterVerts) {
            g_origWaterVertsZ.resize(static_cast<size_t>(count));
            for (int32_t i = 0; i < count; ++i) {
                float z = 0.f;
                std::memcpy(&z, g_waterVertices + static_cast<size_t>(i) * kWaterVertexStride + kWaterVertexZOff, sizeof(float));
                g_origWaterVertsZ[static_cast<size_t>(i)] = z;
            }
            g_savedWaterVerts = true;
        }
        const int32_t limit = reset ? std::min(count, static_cast<int32_t>(g_origWaterVertsZ.size())) : count;
        for (int32_t i = 0; i < limit; ++i) {
            const float z = reset ? g_origWaterVertsZ[static_cast<size_t>(i)] : height;
            std::memcpy(g_waterVertices + static_cast<size_t>(i) * kWaterVertexStride + kWaterVertexZOff, &z, sizeof(float));
        }
        return true;
    });
    return true;
}

bool NativeBreakObject(uint32_t id) {
    GameTasks::Defer([id]() {
        Game::WorldObjects::BreakObject(id);
        return true;
    });
    return true;
}

bool NativeFxAddBlood(float x, float y, float z, float dx, float dy, float dz, int count, float brightness) {
    if (!g_fx || !g_fxBlood) return false;
    GameTasks::Defer([x, y, z, dx, dy, dz, count, brightness]() {
        if (g_fx && g_fxBlood) {
            CVector p{x, y, z}, d{dx, dy, dz};
            g_fxBlood(g_fx, p, d, count, brightness);
        }
        return true;
    });
    return true;
}

bool NativeFxAddBulletImpact(float x, float y, float z, float dx, float dy, float dz, int smokeSize, int sparkCount, float smokeIntensity) {
    if (!g_fx || !g_fxBulletImpact) return false;
    GameTasks::Defer([x, y, z, dx, dy, dz, smokeSize, sparkCount, smokeIntensity]() {
        if (g_fx && g_fxBulletImpact) {
            CVector p{x, y, z}, d{dx, dy, dz};
            g_fxBulletImpact(g_fx, p, d, smokeSize, sparkCount, smokeIntensity);
        }
        return true;
    });
    return true;
}

bool NativeFxAddSparks(float x, float y, float z, float dx, float dy, float dz, float force, int count,
                       float acrossX, float acrossY, float acrossZ, bool blur, float spread, float life) {
    if (!g_fx || !g_fxSparks) return false;
    GameTasks::Defer([x, y, z, dx, dy, dz, force, count, acrossX, acrossY, acrossZ, blur, spread, life]() {
        if (g_fx && g_fxSparks) {
            CVector p{x, y, z}, d{dx, dy, dz}, across{acrossX, acrossY, acrossZ};
            g_fxSparks(g_fx, p, d, force, count, across, blur ? 1 : 0, spread, life);
        }
        return true;
    });
    return true;
}

bool NativeFxAddTyreBurst(float x, float y, float z, float dx, float dy, float dz) {
    if (!g_fx || !g_fxTyreBurst) return false;
    GameTasks::Defer([x, y, z, dx, dy, dz]() {
        if (g_fx && g_fxTyreBurst) {
            CVector p{x, y, z}, d{dx, dy, dz};
            g_fxTyreBurst(g_fx, p, d);
        }
        return true;
    });
    return true;
}

bool NativeFxAddWood(float x, float y, float z, float dx, float dy, float dz, int count, float brightness) {
    if (!g_fx || !g_fxWood) return false;
    GameTasks::Defer([x, y, z, dx, dy, dz, count, brightness]() {
        if (g_fx && g_fxWood) {
            CVector p{x, y, z}, d{dx, dy, dz};
            g_fxWood(g_fx, p, d, count, brightness);
        }
        return true;
    });
    return true;
}

bool NativeFxAddPunchImpact(float x, float y, float z, float dx, float dy, float dz) {
    if (!g_fx || !g_fxPunchImpact) return false;
    GameTasks::Defer([x, y, z, dx, dy, dz]() {
        if (g_fx && g_fxPunchImpact) {
            CVector p{x, y, z}, d{dx, dy, dz};
            g_fxPunchImpact(g_fx, p, d, 0);
        }
        return true;
    });
    return true;
}

bool NativeFxAddDebris(float x, float y, float z, uint8_t r, uint8_t g, uint8_t b, uint8_t a, float scale, int count) {
    if (!g_fx || !g_fxDebris) return false;
    GameTasks::Defer([x, y, z, r, g, b, a, scale, count]() {
        if (g_fx && g_fxDebris) {
            CVector p{x, y, z};
            RwRGBA col{r, g, b, a};
            g_fxDebris(g_fx, p, col, scale, count);
        }
        return true;
    });
    return true;
}

bool NativeFxAddGlass(float x, float y, float z, uint8_t r, uint8_t g, uint8_t b, uint8_t a, float scale, int count) {
    if (!g_fx || !g_fxGlass) return false;
    GameTasks::Defer([x, y, z, r, g, b, a, scale, count]() {
        if (g_fx && g_fxGlass) {
            CVector p{x, y, z};
            RwRGBA col{r, g, b, a};
            g_fxGlass(g_fx, p, col, scale, count);
        }
        return true;
    });
    return true;
}

// Verified libGTASA.so 2.10 arm64: CPlayerInfo::Process (0x4ed9b8 ldp w9,w8,[x19,#0xf0],
// 0x4edad0 str w10,[x19,#0xf0], 0x4edaf0 str w8,[x19,#0xf4]) and CPlayerInfoSaveStructure
// Construct/Extract (0x576d38/0x576dd0).
constexpr size_t kPlayerInfoMoneyOff = 0xF0;
constexpr size_t kPlayerInfoDisplayMoneyOff = 0xF4;

bool NativeSetPlayerMoney(int32_t amount, bool instant) {
    if (!g_worldPlayers) return false;
    GameTasks::Defer([amount, instant]() {
        if (!g_worldPlayers) return true;
        *reinterpret_cast<int32_t*>(g_worldPlayers + kPlayerInfoMoneyOff) = amount;
        if (instant) {
            *reinterpret_cast<int32_t*>(g_worldPlayers + kPlayerInfoDisplayMoneyOff) = amount;
        }
        return true;
    });
    return true;
}

// Verified libGTASA.so 2.10 arm64: _ZN4CPed15GetBonePositionER5RwV3djb (0x59aee4),
// CEntity::m_pRwObject at +0x20 (verified 0x59af30 & 0x4cbb10).
constexpr size_t kEntityRwObjectOff = 0x20;

bool NativeGetPedBonePosition(uint32_t id, uint32_t boneId, float outPos[3]) {
    if (!g_pedGetBonePosition || !outPos) return false;
    bool ok = false;
    float pos[3] = {0.f, 0.f, 0.f};
    if (!GameTasks::RunQuery([id, boneId, &ok, &pos]() {
            void* ped = Game::ElementEntity(id);
            if (!ped) return;
            auto* bytes = static_cast<uint8_t*>(ped);
            if ((bytes[kEntityTypeOff] & 0x07u) != 3u) return;
            if (!*reinterpret_cast<void**>(bytes + kEntityRwObjectOff)) return;
            CVector out{0.f, 0.f, 0.f};
            g_pedGetBonePosition(ped, out, boneId, true);
            if (std::isfinite(out.x) && std::isfinite(out.y) && std::isfinite(out.z)) {
                pos[0] = out.x;
                pos[1] = out.y;
                pos[2] = out.z;
                ok = true;
            }
        })) {
        return false;
    }
    if (!ok) return false;
    outPos[0] = pos[0];
    outPos[1] = pos[1];
    outPos[2] = pos[2];
    return true;
}

bool NativeGetElementBoneMatrix(uint32_t id, uint32_t boneId, float outMat[16]) {
    if (!g_getAnimHierarchy || !g_rpHAnimIdGetIndex || !g_rpHAnimGetMatrixArray || !outMat) return false;
    bool ok = false;
    float mat[16] = {};
    if (!GameTasks::RunQuery([id, boneId, &ok, &mat]() {
            void* entity = Game::ElementEntity(id);
            if (!entity) return;
            auto* bytes = static_cast<uint8_t*>(entity);
            void* clump = *reinterpret_cast<void**>(bytes + kEntityRwObjectOff);
            if (!clump) return;
            void* hierarchy = g_getAnimHierarchy(clump);
            if (!hierarchy) return;
            const int32_t idx = g_rpHAnimIdGetIndex(hierarchy, static_cast<int32_t>(boneId));
            if (idx < 0) return;
            auto* arr = static_cast<const uint8_t*>(g_rpHAnimGetMatrixArray(hierarchy));
            if (!arr) return;
            // RwMatrix stride is 64 bytes (4 x 16B: right, up, at, pos; verified 0x59af60 lsl #6).
            const float* m = reinterpret_cast<const float*>(arr + static_cast<size_t>(idx) * 64u);
            for (int r = 0; r < 4; ++r) {
                mat[r * 4 + 0] = m[r * 4 + 0];
                mat[r * 4 + 1] = m[r * 4 + 1];
                mat[r * 4 + 2] = m[r * 4 + 2];
                mat[r * 4 + 3] = (r == 3) ? 1.f : 0.f;
            }
            ok = true;
        })) {
        return false;
    }
    if (!ok) return false;
    std::memcpy(outMat, mat, sizeof(mat));
    return true;
}

bool NativeSetElementBoneMatrix(uint32_t id, uint32_t boneId, const float inMat[16]) {
    if (!g_getAnimHierarchy || !g_rpHAnimIdGetIndex || !g_rpHAnimGetMatrixArray || !inMat) return false;
    std::array<float, 16> mat{};
    std::memcpy(mat.data(), inMat, sizeof(mat));
    bool ok = false;
    if (!GameTasks::RunQuery([id, boneId, mat, &ok]() {
            void* entity = Game::ElementEntity(id);
            if (!entity) return;
            auto* bytes = static_cast<uint8_t*>(entity);
            void* clump = *reinterpret_cast<void**>(bytes + kEntityRwObjectOff);
            if (!clump) return;
            void* hierarchy = g_getAnimHierarchy(clump);
            if (!hierarchy) return;
            const int32_t idx = g_rpHAnimIdGetIndex(hierarchy, static_cast<int32_t>(boneId));
            if (idx < 0) return;
            auto* arr = static_cast<uint8_t*>(g_rpHAnimGetMatrixArray(hierarchy));
            if (!arr) return;
            float* m = reinterpret_cast<float*>(arr + static_cast<size_t>(idx) * 64u);
            for (int r = 0; r < 4; ++r) {
                m[r * 4 + 0] = mat[r * 4 + 0];
                m[r * 4 + 1] = mat[r * 4 + 1];
                m[r * 4 + 2] = mat[r * 4 + 2];
            }
            ok = true;
        })) {
        return false;
    }
    return ok;
}

bool NativeUpdateElementRpHAnim(uint32_t id) {
    if (!g_entityUpdateRpHAnim && !g_rpHAnimUpdateMatrices) return false;
    bool ok = false;
    if (!GameTasks::RunQuery([id, &ok]() {
            void* entity = Game::ElementEntity(id);
            if (!entity) return;
            if (g_entityUpdateRpHAnim) {
                g_entityUpdateRpHAnim(entity);
                ok = true;
                return;
            }
            auto* bytes = static_cast<uint8_t*>(entity);
            void* clump = *reinterpret_cast<void**>(bytes + kEntityRwObjectOff);
            if (!clump || !g_getAnimHierarchy) return;
            void* hierarchy = g_getAnimHierarchy(clump);
            if (!hierarchy) return;
            g_rpHAnimUpdateMatrices(hierarchy);
            ok = true;
        })) {
        return false;
    }
    return ok;
}

bool NativeGetStatValue(uint16_t stat, float& outValue) {
    if (!g_statsGetStatValue) return false;
    float value = 0.f;
    bool ok = false;
    if (!GameTasks::RunQuery([stat, &value, &ok]() {
            if (!g_statsGetStatValue) return;
            value = g_statsGetStatValue(stat);
            ok = std::isfinite(value);
        })) {
        return false;
    }
    if (!ok) return false;
    outValue = value;
    return true;
}

bool NativeSetStatValue(uint16_t stat, float value) {
    if (!g_statsSetStatValue) return false;
    GameTasks::Defer([stat, value]() {
        if (g_statsSetStatValue) g_statsSetStatValue(stat, value);
        return true;
    });
    return true;
}

// Verified libGTASA.so 2.10 arm64: CPlayerPed::m_pPlayerData at +0x540, CPlayerPedData::m_fBreath at +0x58
// (LOAD: CWidgetPlayerInfo::RenderBreathBar 0x37db5c-0x37db60; STORE: CPlayerPed::ResetPlayerBreath 0x5c8974-0x5c897c).
constexpr size_t kPlayerPedDataPtrOff = 0x540;
constexpr size_t kPlayerPedDataBreathOff = 0x58;

bool NativeGetPedOxygenLevel(uint32_t id, float& outOxygen) {
    if (id == 0 || id != ServerWorld::LocalPlayer()) return false;
    float oxygen = 0.f;
    bool ok = false;
    if (!GameTasks::RunQuery([id, &oxygen, &ok]() {
            void* ped = Game::ElementEntity(id);
            if (!ped) return;
            auto* data = *reinterpret_cast<uint8_t**>(static_cast<uint8_t*>(ped) + kPlayerPedDataPtrOff);
            if (!data) return;
            const float breath = *reinterpret_cast<float*>(data + kPlayerPedDataBreathOff);
            if (std::isfinite(breath)) {
                oxygen = breath;
                ok = true;
            }
        })) {
        return false;
    }
    if (!ok) return false;
    outOxygen = oxygen;
    return true;
}

bool NativeSetPedOxygenLevel(uint32_t id, float oxygen) {
    if (id == 0 || id != ServerWorld::LocalPlayer()) return false;
    GameTasks::Defer([id, oxygen]() {
        void* ped = Game::ElementEntity(id);
        if (!ped) return true;
        auto* data = *reinterpret_cast<uint8_t**>(static_cast<uint8_t*>(ped) + kPlayerPedDataPtrOff);
        if (data) *reinterpret_cast<float*>(data + kPlayerPedDataBreathOff) = oxygen;
        return true;
    });
    return true;
}

// Verified libGTASA.so 2.10 arm64: CPed::m_nFightingStyle at +0x8FD, m_nAllowedAttackMoves at +0x8FE
// (LOAD: CTaskSimpleFight::GetAvailableComboSet 0x5da6b8; STORE: CRunningScript 0x4016d4-0x4016d8 & CPedSaveStructure::Extract 0x5769e0).
constexpr size_t kPedFightingStyleOff = 0x8FD;
constexpr size_t kPedFightingMovesOff = 0x8FE;

bool NativeSetPedFightingStyle(uint32_t id, uint8_t style) {
    GameTasks::Defer([id, style]() {
        void* ped = Game::ElementEntity(id);
        if (!ped) return true;
        auto* bytes = static_cast<uint8_t*>(ped);
        if ((bytes[kEntityTypeOff] & 0x07u) != 3u) return true;
        bytes[kPedFightingStyleOff] = style;
        bytes[kPedFightingMovesOff] = 0u;
        return true;
    });
    return true;
}

// Verified libGTASA.so 2.10 arm64: CPed::m_nAnimGroup at +0x630
// (LOAD: CPed::SetMoveAnim 0x596db8; STORE: CPed::SetModelIndex 0x595af8 & CRunningScript 0x40a0f8).
constexpr size_t kPedAnimGroupOff = 0x630;

bool NativeSetPedWalkingStyle(uint32_t id, int32_t animGroup) {
    if (!g_pedSetMoveAnim) return false;
    GameTasks::Defer([id, animGroup]() {
        void* ped = Game::ElementEntity(id);
        if (!ped || !g_pedSetMoveAnim) return true;
        auto* bytes = static_cast<uint8_t*>(ped);
        if ((bytes[kEntityTypeOff] & 0x07u) != 3u) return true;
        *reinterpret_cast<int32_t*>(bytes + kPedAnimGroupOff) = animGroup;
        g_pedSetMoveAnim(ped);
        return true;
    });
    return true;
}

// Verified libGTASA.so 2.10 arm64: CPed::RemoveBodyPart (0x5a5fe4, 0x5a6014..0x5a6024)
// sets bit 15 (0x8000 = bRemoveHead) in uint64_t at CPed+0x584.
constexpr size_t kPedFlagsOff = 0x584;
constexpr uint64_t kPedFlagRemoveHead = 0x8000ull;

bool NativeSetPedHeadless(uint32_t id, bool headless) {
    if (!g_pedRemoveBodyPart) return false;
    GameTasks::Defer([id, headless]() {
        void* ped = Game::ElementEntity(id);
        if (!ped) return true;
        auto* bytes = static_cast<uint8_t*>(ped);
        if ((bytes[kEntityTypeOff] & 0x07u) != 3u) return true;
        if (headless) {
            if (g_pedRemoveBodyPart) g_pedRemoveBodyPart(ped, 3, 0);
        } else {
            uint64_t flags = 0;
            std::memcpy(&flags, bytes + kPedFlagsOff, sizeof(flags));
            flags &= ~kPedFlagRemoveHead;
            std::memcpy(bytes + kPedFlagsOff, &flags, sizeof(flags));
        }
        return true;
    });
    return true;
}

bool NativePlayPedVoiceLine(uint32_t id, uint16_t speechId, float probability) {
    if (!g_pedSay) return false;
    GameTasks::Defer([id, speechId, probability]() {
        void* ped = Game::ElementEntity(id);
        if (!ped || !g_pedSay) return true;
        auto* bytes = static_cast<uint8_t*>(ped);
        if ((bytes[kEntityTypeOff] & 0x07u) != 3u) return true;
        g_pedSay(ped, speechId, 0u, probability, 1u, 0u, 0u);
        return true;
    });
    return true;
}

bool NativeSetPedAnimation(uint32_t id, const char* block, const char* name, int timeMs,
                           bool loop, bool updatePos, bool interruptible, bool freezeLastFrame) {
    Game::PedRpcOp op;
    if (!block || !block[0] || !name || !name[0]) {
        op.kind = Game::PedRpcOp::Kind::KillAnimation;
    } else {
        op.kind = Game::PedRpcOp::Kind::Animation;
        op.block = block;
        op.name = name;
        op.time = timeMs;
        op.loop = loop;
        op.updatePosition = updatePos;
        op.interruptible = interruptible;
        op.freezeLastFrame = freezeLastFrame;
    }
    Game::QueuePedRpc(id, op);
    return true;
}

bool NativeSetPedAnimationProgress(uint32_t id, const char* name, float progress) {
    if (!name || !name[0]) return false;
    Game::PedRpcOp op;
    op.kind = Game::PedRpcOp::Kind::AnimationProgress;
    op.name = name;
    op.value = progress;
    Game::QueuePedRpc(id, op);
    return true;
}

bool NativeSetPedAnimationSpeed(uint32_t id, const char* name, float speed) {
    if (!name || !name[0]) return false;
    Game::PedRpcOp op;
    op.kind = Game::PedRpcOp::Kind::AnimationSpeed;
    op.name = name;
    op.value = speed;
    Game::QueuePedRpc(id, op);
    return true;
}

bool NativeSetEntityOnFire(uint32_t id, bool onFire) {
    if (!g_fireManager) return false;
    if (onFire && !g_startEntityFire) return false;
    if (!onFire && !g_extinguishPoint) return false;
    GameTasks::Defer([id, onFire]() {
        void* entity = Game::ElementEntity(id);
        if (!entity || !g_fireManager) return true;
        if (onFire) {
            if (g_startEntityFire) g_startEntityFire(g_fireManager, entity, nullptr, 0.8f, 1u, 7000u, 2);
        } else if (g_extinguishPoint) {
            float px = 0.f, py = 0.f, pz = 0.f;
            if (ReadEntityPosition(static_cast<const uint8_t*>(entity), px, py, pz)) {
                g_extinguishPoint(g_fireManager, CVector{px, py, pz}, 2.5f);
            }
        }
        return true;
    });
    return true;
}

// Verified libGTASA.so 2.10 arm64:
//   CPhysical::m_vecMoveSpeed at +0x68 (verified _ZN9CPhysical14ApplyMoveSpeedEv 0x4e1104)
//   CPhysical::m_vecTurnSpeed at +0x74 (verified _ZN9CPhysical14ApplyTurnSpeedEv 0x4e116c)
constexpr size_t kPhysicalMoveSpeedOff = 0x68;
constexpr size_t kPhysicalTurnSpeedOff = 0x74;

bool NativeSetEntityVelocity(uint32_t id, float x, float y, float z, bool angular) {
    GameTasks::Defer([id, x, y, z, angular]() {
        void* entity = Game::ElementEntity(id);
        if (!entity) return true;
        auto* bytes = static_cast<uint8_t*>(entity);
        const uint8_t etype = bytes[kEntityTypeOff] & 0x07u;
        // Only CPhysical subclasses (2=vehicle, 3=ped, 4=object) have move/turn speed vectors.
        if (etype < 2u || etype > 4u) return true;
        float* vec = reinterpret_cast<float*>(bytes + (angular ? kPhysicalTurnSpeedOff : kPhysicalMoveSpeedOff));
        vec[0] = x;
        vec[1] = y;
        vec[2] = z;
        return true;
    });
    return true;
}

// Verified libGTASA.so 2.10 arm64: CBaseModelInfo::m_pColModel at +0x30
// (verified _ZN15CClumpModelInfo14GetBoundingBoxEv 0x45bf64: ldr x0, [x0, #0x30] -> 0xF9401800),
// CColModel layout: +0x00 min[3], +0x0C max[3], +0x18 sphereCenter[3], +0x24 radius.
constexpr size_t kModelInfoColModelOff = 0x30;
constexpr uint16_t kMaxModelId = 20000;

bool NativeGetModelBoundingBox(uint16_t model, float minOut[3], float maxOut[3], float& radiusOut) {
    if (!g_modelInfoPtrs || !g_modelColOffsetVerified || model >= kMaxModelId) return false;
    bool ok = false;
    float mn[3] = {}, mx[3] = {};
    float radius = 0.f;
    if (!GameTasks::RunQuery([model, &ok, &mn, &mx, &radius]() {
            if (!g_modelInfoPtrs) return;
            auto* mi = static_cast<uint8_t*>(g_modelInfoPtrs[model]);
            if (!mi) return;
            auto* col = *reinterpret_cast<const float**>(mi + kModelInfoColModelOff);
            if (!col) return;
            mn[0] = col[0]; mn[1] = col[1]; mn[2] = col[2];
            mx[0] = col[3]; mx[1] = col[4]; mx[2] = col[5];
            radius = col[9];  // +0x24
            ok = std::isfinite(mn[0]) && std::isfinite(mx[0]) && std::isfinite(radius);
        })) {
        return false;
    }
    if (!ok) return false;
    if (minOut) { minOut[0] = mn[0]; minOut[1] = mn[1]; minOut[2] = mn[2]; }
    if (maxOut) { maxOut[0] = mx[0]; maxOut[1] = mx[1]; maxOut[2] = mx[2]; }
    radiusOut = radius;
    return true;
}

bool NativeGetEntityComDistance(uint32_t id, float& outDist) {
    if (!g_entityGetComDist) return false;
    bool ok = false;
    float dist = 0.f;
    if (!GameTasks::RunQuery([id, &ok, &dist]() {
            void* entity = Game::ElementEntity(id);
            if (!entity || !g_entityGetComDist) return;
            dist = g_entityGetComDist(entity);
            ok = std::isfinite(dist);
        })) {
        return false;
    }
    if (!ok) return false;
    outDist = dist;
    return true;
}

bool NativeIsEntityOnScreen(uint32_t id, bool& outOnScreen) {
    if (!g_entityGetIsOnScreen) return false;
    bool ok = false;
    bool visible = false;
    if (!GameTasks::RunQuery([id, &ok, &visible]() {
            void* entity = Game::ElementEntity(id);
            if (!entity || !g_entityGetIsOnScreen) return;
            visible = g_entityGetIsOnScreen(entity);
            ok = true;
        })) {
        return false;
    }
    if (!ok) return false;
    outOnScreen = visible;
    return true;
}

// Verified libGTASA.so 2.10 arm64 CVehicle vtable slots (checked at install time against _ZTV8CVehicle):
//   slot 51 (+0x198): CVehicle::Fix (0x83d418 - 0x83d280)
//   slot 56 (+0x1C0): CVehicle::GetNumContactWheels (0x83d440 - 0x83d280)
constexpr size_t kVehicleVtableFixSlot = 51;
constexpr size_t kVehicleVtableContactWheelsSlot = 56;

bool NativeGetVehicleContactWheels(uint32_t id, uint8_t& outWheels) {
    if (!g_vehicleVtableWheelsVerified) return false;
    bool ok = false;
    uint8_t wheels = 0;
    if (!GameTasks::RunQuery([id, &ok, &wheels]() {
            void* entity = Game::ElementEntity(id);
            if (!entity) return;
            auto* bytes = static_cast<uint8_t*>(entity);
            if ((bytes[kEntityTypeOff] & 0x07u) != 2u) return;
            auto** vtable = *reinterpret_cast<void***>(entity);
            if (!vtable || !vtable[kVehicleVtableContactWheelsSlot]) return;
            auto fn = reinterpret_cast<VehicleNumContactWheelsFn>(vtable[kVehicleVtableContactWheelsSlot]);
            wheels = fn(entity);
            ok = true;
        })) {
        return false;
    }
    if (!ok) return false;
    outWheels = wheels;
    return true;
}

bool NativeFixVehicle(uint32_t id) {
    if (!g_vehicleVtableFixVerified) return false;
    GameTasks::Defer([id]() {
        void* entity = Game::ElementEntity(id);
        if (!entity) return true;
        auto* bytes = static_cast<uint8_t*>(entity);
        if ((bytes[kEntityTypeOff] & 0x07u) != 2u) return true;
        auto** vtable = *reinterpret_cast<void***>(entity);
        if (vtable && vtable[kVehicleVtableFixSlot]) {
            auto fn = reinterpret_cast<VehicleFixFn>(vtable[kVehicleVtableFixSlot]);
            fn(entity);
        }
        return true;
    });
    return true;
}

bool NativeSetWorldSpecialProperty(const char* name, bool enabled) {
    if (!name || !g_cheatsActive) return false;
    // Verified indices from CCheat::CheatLabelE (0x85ec00) in libGTASA.so 2.10 arm64:
    //   [41] BACKTOTHEFUTURE (hovercars), [56] FLYINGCARS (aircars),
    //   [57] CJPHONEHOME (extrabunny), [68] HIGHJUMP (extrajump).
    int idx = -1;
    if (std::strcmp(name, "hovercars") == 0) idx = 41;
    else if (std::strcmp(name, "aircars") == 0) idx = 56;
    else if (std::strcmp(name, "extrabunny") == 0) idx = 57;
    else if (std::strcmp(name, "extrajump") == 0) idx = 68;
    else return false;
    if (idx < 0 || static_cast<size_t>(idx) >= kCheatsActiveSize) return false;
    GameTasks::Defer([idx, enabled]() {
        if (g_cheatsActive) g_cheatsActive[idx] = enabled ? 1u : 0u;
        return true;
    });
    return true;
}

bool NativeSetCameraViewMode(int vehicleMode, int pedMode) {
    if (!g_theCamera || (!g_camSetZoomVeh && !g_camSetZoomPed)) return false;
    GameTasks::Defer([vehicleMode, pedMode]() {
        if (!g_theCamera) return true;
        if (g_camSetZoomVeh && vehicleMode >= 0 && vehicleMode <= 5) {
            g_camSetZoomVeh(g_theCamera, static_cast<int16_t>(std::clamp(vehicleMode, 0, 2)));
        }
        if (g_camSetZoomPed && pedMode >= 0 && pedMode <= 3) {
            g_camSetZoomPed(g_theCamera, static_cast<int16_t>(std::clamp(pedMode, 0, 2)));
        }
        return true;
    });
    return true;
}

bool NativeSetCameraTargetEntity(uint32_t id) {
    if (!g_theCamera || !g_camTakeControl) return false;
    GameTasks::Defer([id]() {
        if (!g_theCamera || !g_camTakeControl) return true;
        void* entity = Game::ElementEntity(id);
        if (!entity) return true;
        auto* bytes = static_cast<const uint8_t*>(entity);
        // MODE_CAM_ON_A_STRING (18) for vehicles, MODE_FOLLOWPED (4) for peds/other, switchStyle=1 (JUMP_CUT), who=1.
        const int16_t mode = ((bytes[kEntityTypeOff] & 0x07u) == 2u) ? 18 : 4;
        g_camTakeControl(g_theCamera, entity, mode, 1, 1);
        return true;
    });
    return true;
}

bool NativeSetVehicleEngineState(uint32_t id, bool on) {
    GameTasks::Defer([id, on]() {
        void* entity = Game::ElementEntity(id);
        if (!entity) return true;
        auto* bytes = static_cast<uint8_t*>(entity);
        if ((bytes[kEntityTypeOff] & 0x07u) != 2u) return true;
        uint32_t flags = 0;
        std::memcpy(&flags, bytes + kVehEngineFlagsOff, sizeof(flags));
        if (on) flags |= 0x10u;
        else flags &= ~0x10u;
        std::memcpy(bytes + kVehEngineFlagsOff, &flags, sizeof(flags));
        return true;
    });
    return true;
}

bool NativeSetVehicleOverrideLights(uint32_t id, uint8_t mode) {
    GameTasks::Defer([id, mode]() {
        void* entity = Game::ElementEntity(id);
        if (!entity) return true;
        auto* bytes = static_cast<uint8_t*>(entity);
        if ((bytes[kEntityTypeOff] & 0x07u) != 2u) return true;
        const uint8_t clamped = mode <= 2u ? mode : 0u;
        bytes[kVehOverrideLightsOff] = static_cast<uint8_t>((bytes[kVehOverrideLightsOff] & ~0x18u) | (clamped << 3));
        if (clamped == 1u || clamped == 2u) {
            uint32_t flags = 0;
            std::memcpy(&flags, bytes + kVehEngineFlagsOff, sizeof(flags));
            if (clamped == 2u) flags |= 0x40u;
            else flags &= ~0x40u;
            std::memcpy(bytes + kVehEngineFlagsOff, &flags, sizeof(flags));
        }
        return true;
    });
    return true;
}

bool NativeSetVehicleDoorLockMode(uint32_t id, int mode) {
    GameTasks::Defer([id, mode]() {
        void* entity = Game::ElementEntity(id);
        if (!entity) return true;
        auto* bytes = static_cast<uint8_t*>(entity);
        if ((bytes[kEntityTypeOff] & 0x07u) != 2u) return true;
        const int32_t v = static_cast<int32_t>(mode);
        std::memcpy(bytes + kVehDoorLockOff, &v, sizeof(v));
        return true;
    });
    return true;
}

bool NativeSetVehicleSirensOn(uint32_t id, bool on) {
    GameTasks::Defer([id, on]() {
        void* entity = Game::ElementEntity(id);
        if (!entity) return true;
        auto* bytes = static_cast<uint8_t*>(entity);
        if ((bytes[kEntityTypeOff] & 0x07u) != 2u) return true;
        if (on) bytes[kVehSirenFlagByteOff] |= 0x80u;
        else bytes[kVehSirenFlagByteOff] &= ~0x80u;
        return true;
    });
    return true;
}

bool NativeSetVehicleTaxiLightOn(uint32_t id, bool on) {
    if (!g_autoSetTaxiLight) return false;
    GameTasks::Defer([id, on]() {
        if (!g_autoSetTaxiLight) return true;
        void* entity = Game::ElementEntity(id);
        if (!entity) return true;
        auto* bytes = static_cast<uint8_t*>(entity);
        if ((bytes[kEntityTypeOff] & 0x07u) != 2u) return true;
        uint32_t vehClass = 0;
        std::memcpy(&vehClass, bytes + kVehClassOff, sizeof(vehClass));
        if (vehClass != 0u) return true;  // CAutomobile only
        g_autoSetTaxiLight(entity, on);
        return true;
    });
    return true;
}

bool NativeSetVehiclePaintjob(uint32_t id, int paintjob) {
    if (!g_vehSetRemap) return false;
    GameTasks::Defer([id, paintjob]() {
        if (!g_vehSetRemap) return true;
        void* entity = Game::ElementEntity(id);
        if (!entity) return true;
        auto* bytes = static_cast<uint8_t*>(entity);
        if ((bytes[kEntityTypeOff] & 0x07u) != 2u) return true;
        // MTA paintjob 3 means default/no remap (-1 in GTA CVehicle::SetRemap).
        g_vehSetRemap(entity, paintjob == 3 ? -1 : paintjob);
        return true;
    });
    return true;
}

bool NativeSetVehicleColor(uint32_t id, uint8_t c1, uint8_t c2, uint8_t c3, uint8_t c4) {
    GameTasks::Defer([id, c1, c2, c3, c4]() {
        void* entity = Game::ElementEntity(id);
        if (!entity) return true;
        auto* bytes = static_cast<uint8_t*>(entity);
        if ((bytes[kEntityTypeOff] & 0x07u) != 2u) return true;
        bytes[kVehColorOff + 0] = c1;
        bytes[kVehColorOff + 1] = c2;
        bytes[kVehColorOff + 2] = c3;
        bytes[kVehColorOff + 3] = c4;
        return true;
    });
    return true;
}

bool NativeSetVehiclePhysicalProofs(uint32_t id, bool bullet, bool fire, bool explosion,
                                    bool collision, bool melee) {
    GameTasks::Defer([id, bullet, fire, explosion, collision, melee]() {
        void* entity = Game::ElementEntity(id);
        if (!entity) return true;
        auto* bytes = static_cast<uint8_t*>(entity);
        if ((bytes[kEntityTypeOff] & 0x07u) != 2u) return true;
        uint8_t proofs = bytes[kPhysicalProofsByteOff];
        proofs = static_cast<uint8_t>((proofs & ~0xBCu) |
                                      (bullet ? 0x04u : 0u) |
                                      (fire ? 0x08u : 0u) |
                                      (collision ? 0x10u : 0u) |
                                      (melee ? 0x20u : 0u) |
                                      (explosion ? 0x80u : 0u));
        bytes[kPhysicalProofsByteOff] = proofs;
        return true;
    });
    return true;
}

bool NativeSetVehicleTyresCanBurst(uint32_t id, bool canBurst) {
    GameTasks::Defer([id, canBurst]() {
        void* entity = Game::ElementEntity(id);
        if (!entity) return true;
        auto* bytes = static_cast<uint8_t*>(entity);
        if ((bytes[kEntityTypeOff] & 0x07u) != 2u) return true;
        uint32_t flags = 0;
        std::memcpy(&flags, bytes + kVehEngineFlagsOff, sizeof(flags));
        // Bit 31 (0x80000000) is bTyresDontBurst.
        if (canBurst) flags &= ~0x80000000u;
        else flags |= 0x80000000u;
        std::memcpy(bytes + kVehEngineFlagsOff, &flags, sizeof(flags));
        return true;
    });
    return true;
}

bool NativeSetVehicleDirtLevel(uint32_t id, float level) {
    const float clamped = std::clamp(level, 0.f, 15.f);
    GameTasks::Defer([id, clamped]() {
        void* entity = Game::ElementEntity(id);
        if (!entity) return true;
        auto* bytes = static_cast<uint8_t*>(entity);
        if ((bytes[kEntityTypeOff] & 0x07u) != 2u) return true;
        std::memcpy(bytes + kVehDirtLevelOff, &clamped, sizeof(clamped));
        return true;
    });
    return true;
}

bool NativeSetVehicleDoorOpenRatio(uint32_t id, uint8_t door, float ratio) {
    if (!g_vehicleVtableOpenDoorVerified || door > 5u) return false;
    const float clamped = std::clamp(ratio, 0.f, 1.f);
    GameTasks::Defer([id, door, clamped]() {
        void* entity = Game::ElementEntity(id);
        if (!entity) return true;
        auto* bytes = static_cast<uint8_t*>(entity);
        if ((bytes[kEntityTypeOff] & 0x07u) != 2u) return true;
        auto** vtable = *reinterpret_cast<void***>(entity);
        if (vtable && vtable[kVehicleVtableOpenDoorSlot]) {
            // GTA eCarNodes door component IDs match MTA door 0..5 -> nodes 16,17,10,8,11,9 (CAR_BONNET..CAR_DOOR_RR).
            static constexpr int32_t kDoorNodeIds[6] = {16, 17, 10, 8, 11, 9};
            auto fn = reinterpret_cast<VehicleOpenDoorFn>(vtable[kVehicleVtableOpenDoorSlot]);
            fn(entity, nullptr, kDoorNodeIds[door], static_cast<int32_t>(door), clamped, false);
        }
        return true;
    });
    return true;
}

bool NativeBlowVehicle(uint32_t id, bool withExplosion) {
    if (!g_vehicleVtableBlowUpVerified) return false;
    GameTasks::Defer([id, withExplosion]() {
        void* entity = Game::ElementEntity(id);
        if (!entity) return true;
        auto* bytes = static_cast<uint8_t*>(entity);
        if ((bytes[kEntityTypeOff] & 0x07u) != 2u) return true;
        auto** vtable = *reinterpret_cast<void***>(entity);
        if (vtable && vtable[kVehicleVtableBlowUpSlot]) {
            auto fn = reinterpret_cast<VehicleBlowUpCarFn>(vtable[kVehicleVtableBlowUpSlot]);
            fn(entity, nullptr, withExplosion ? 0u : 1u);
        }
        return true;
    });
    return true;
}

bool NativeBurstVehicleTyre(uint32_t id, uint8_t wheelId) {
    if (!g_vehicleVtableBurstTyreVerified || wheelId > 3u) return false;
    GameTasks::Defer([id, wheelId]() {
        void* entity = Game::ElementEntity(id);
        if (!entity) return true;
        auto* bytes = static_cast<uint8_t*>(entity);
        if ((bytes[kEntityTypeOff] & 0x07u) != 2u) return true;
        auto** vtable = *reinterpret_cast<void***>(entity);
        if (vtable && vtable[kVehicleVtableBurstTyreSlot]) {
            auto fn = reinterpret_cast<VehicleBurstTyreFn>(vtable[kVehicleVtableBurstTyreSlot]);
            fn(entity, wheelId, false);
        }
        return true;
    });
    return true;
}

bool NativeSetVehicleUpgrade(uint32_t id, uint16_t upgrade, bool add) {
    if (add && (!g_vehAddUpgrade || !g_reqVehUpgrade || !g_hasVehUpgradeLoaded)) return false;
    if (!add && !g_vehRemoveUpgrade) return false;
    GameTasks::Defer([id, upgrade, add, retries = 0]() mutable -> bool {
        void* entity = Game::ElementEntity(id);
        if (!entity) return true;
        auto* bytes = static_cast<uint8_t*>(entity);
        if ((bytes[kEntityTypeOff] & 0x07u) != 2u) return true;
        if (!add) {
            g_vehRemoveUpgrade(entity, static_cast<int32_t>(upgrade));
            return true;
        }
        if (!g_hasVehUpgradeLoaded(static_cast<int32_t>(upgrade))) {
            if (retries == 0) g_reqVehUpgrade(static_cast<int32_t>(upgrade), 0);
            return ++retries >= 120;  // wait up to ~2s for upgrade model streaming
        }
        g_vehAddUpgrade(entity, static_cast<int32_t>(upgrade));
        return true;
    });
    return true;
}

bool NativeSetVehicleNitroState(uint32_t id, int count, float level) {
    GameTasks::Defer([id, count, level]() {
        void* entity = Game::ElementEntity(id);
        if (!entity) return true;
        auto* bytes = static_cast<uint8_t*>(entity);
        if ((bytes[kEntityTypeOff] & 0x07u) != 2u) return true;
        uint32_t vehClass = 0;
        std::memcpy(&vehClass, bytes + kVehClassOff, sizeof(vehClass));
        if (vehClass != 0u) return true;  // CAutomobile only
        if (count >= 0) {
            const int8_t c = static_cast<int8_t>(std::clamp(count, 0, 100));
            bytes[kVehNitroBoostsOff] = static_cast<uint8_t>(c);
            uint32_t autoFlags = 0;
            std::memcpy(&autoFlags, bytes + kAutoHasNitroFlagsOff, sizeof(autoFlags));
            if (c > 0) autoFlags |= 0x80000u;
            else autoFlags &= ~0x80000u;
            std::memcpy(bytes + kAutoHasNitroFlagsOff, &autoFlags, sizeof(autoFlags));
        }
        if (std::isfinite(level)) {
            const float l = std::clamp(level, -1.f, 1.f);
            std::memcpy(bytes + kAutoNitroValueOff, &l, sizeof(l));
        }
        return true;
    });
    return true;
}

bool NativeSetTrainSpeed(uint32_t id, float speed) {
    if (!g_trainSetSpeed) return false;
    GameTasks::Defer([id, speed]() {
        if (!g_trainSetSpeed) return true;
        void* entity = Game::ElementEntity(id);
        if (!entity) return true;
        auto* bytes = static_cast<uint8_t*>(entity);
        if ((bytes[kEntityTypeOff] & 0x07u) != 2u) return true;
        uint32_t vehClass = 0;
        std::memcpy(&vehClass, bytes + kVehClassOff, sizeof(vehClass));
        if (vehClass != 6u) return true;  // CTrain only
        g_trainSetSpeed(entity, speed);
        return true;
    });
    return true;
}

bool NativeSetVehicleDoorState(uint32_t id, uint8_t door, uint8_t state) {
    if (!g_dmgSetDoor || door >= 6u) return false;
    GameTasks::Defer([id, door, state]() {
        if (!g_dmgSetDoor) return true;
        void* entity = Game::ElementEntity(id);
        if (!entity) return true;
        auto* bytes = static_cast<uint8_t*>(entity);
        if ((bytes[kEntityTypeOff] & 0x07u) != 2u) return true;
        uint32_t vehClass = 0;
        std::memcpy(&vehClass, bytes + kVehClassOff, sizeof(vehClass));
        if (vehClass != 0u) return true;  // CAutomobile only
        g_dmgSetDoor(bytes + kAutoDamageManagerOff, static_cast<int32_t>(door), static_cast<uint32_t>(state));
        return true;
    });
    return true;
}

bool NativeSetVehicleLightState(uint32_t id, uint8_t light, uint8_t state) {
    if (!g_dmgSetLight || light >= 4u) return false;
    GameTasks::Defer([id, light, state]() {
        if (!g_dmgSetLight) return true;
        void* entity = Game::ElementEntity(id);
        if (!entity) return true;
        auto* bytes = static_cast<uint8_t*>(entity);
        if ((bytes[kEntityTypeOff] & 0x07u) != 2u) return true;
        uint32_t vehClass = 0;
        std::memcpy(&vehClass, bytes + kVehClassOff, sizeof(vehClass));
        if (vehClass != 0u) return true;
        g_dmgSetLight(bytes + kAutoDamageManagerOff, static_cast<int32_t>(light), static_cast<uint32_t>(state));
        return true;
    });
    return true;
}

bool NativeSetVehiclePanelState(uint32_t id, uint8_t panel, uint8_t state) {
    if (!g_dmgSetPanel || panel >= 7u) return false;
    GameTasks::Defer([id, panel, state]() {
        if (!g_dmgSetPanel) return true;
        void* entity = Game::ElementEntity(id);
        if (!entity) return true;
        auto* bytes = static_cast<uint8_t*>(entity);
        if ((bytes[kEntityTypeOff] & 0x07u) != 2u) return true;
        uint32_t vehClass = 0;
        std::memcpy(&vehClass, bytes + kVehClassOff, sizeof(vehClass));
        if (vehClass != 0u) return true;
        g_dmgSetPanel(bytes + kAutoDamageManagerOff, static_cast<int32_t>(panel), static_cast<uint32_t>(state));
        return true;
    });
    return true;
}

bool NativeSetVehicleWheelState(uint32_t id, uint8_t wheel, uint8_t state) {
    if (!g_dmgSetWheel || wheel >= 4u) return false;
    GameTasks::Defer([id, wheel, state]() {
        if (!g_dmgSetWheel) return true;
        void* entity = Game::ElementEntity(id);
        if (!entity) return true;
        auto* bytes = static_cast<uint8_t*>(entity);
        if ((bytes[kEntityTypeOff] & 0x07u) != 2u) return true;
        uint32_t vehClass = 0;
        std::memcpy(&vehClass, bytes + kVehClassOff, sizeof(vehClass));
        if (vehClass != 0u) return true;
        g_dmgSetWheel(bytes + kAutoDamageManagerOff, static_cast<int32_t>(wheel), static_cast<uint32_t>(state));
        return true;
    });
    return true;
}

bool NativeSetEntityAlpha(uint32_t id, uint8_t alpha) {
    if (!g_setClumpAlpha) return false;
    GameTasks::Defer([id, alpha]() {
        if (!g_setClumpAlpha) return true;
        void* entity = Game::ElementEntity(id);
        if (!entity) return true;
        auto* bytes = static_cast<uint8_t*>(entity);
        void* rwObject = *reinterpret_cast<void**>(bytes + kEntityRwObjectOff);
        if (!rwObject) return true;
        // RwObject::type == 2 is rpCLUMP (CVisibilityPlugins::SetClumpAlpha expects RpClump*).
        if (static_cast<const uint8_t*>(rwObject)[0] != 2u) return true;
        g_setClumpAlpha(rwObject, static_cast<int32_t>(alpha));
        return true;
    });
    return true;
}

bool NativeSetEntityCollisionsEnabled(uint32_t id, bool enabled) {
    GameTasks::Defer([id, enabled]() {
        void* entity = Game::ElementEntity(id);
        if (!entity) return true;
        auto* bytes = static_cast<uint8_t*>(entity);
        if (enabled) bytes[kEntityColFlagsOff] |= 0x01u;
        else bytes[kEntityColFlagsOff] &= ~0x01u;
        return true;
    });
    return true;
}

bool NativeSetEntityPhysicalProofs(uint32_t id, bool bullet, bool fire, bool explosion,
                                   bool collision, bool melee) {
    GameTasks::Defer([id, bullet, fire, explosion, collision, melee]() {
        void* entity = Game::ElementEntity(id);
        if (!entity) return true;
        auto* bytes = static_cast<uint8_t*>(entity);
        const uint8_t etype = bytes[kEntityTypeOff] & 0x07u;
        if (etype < 2u || etype > 4u) return true;  // CVehicle, CPed, CObject
        uint8_t proofs = bytes[kPhysicalProofsByteOff];
        proofs = static_cast<uint8_t>((proofs & ~0xBCu) |
                                      (bullet ? 0x04u : 0u) |
                                      (fire ? 0x08u : 0u) |
                                      (collision ? 0x10u : 0u) |
                                      (melee ? 0x20u : 0u) |
                                      (explosion ? 0x80u : 0u));
        bytes[kPhysicalProofsByteOff] = proofs;
        return true;
    });
    return true;
}

bool NativeSetCameraFov(float fov) {
    if (!g_theCamera) return false;
    const float clamped = std::clamp(fov, 1.f, 179.f);
    GameTasks::Defer([clamped]() {
        if (!g_theCamera) return true;
        if (g_camLerpFov) g_camLerpFov(g_theCamera, clamped, clamped, 0.f, false);
        const uint8_t active = g_theCamera[kCameraActiveCamOff];
        if (active < 3u) {
            std::memcpy(g_theCamera + static_cast<size_t>(active) * kCameraCamStride + kCameraCamFovOff,
                        &clamped, sizeof(clamped));
        }
        return true;
    });
    return true;
}

bool NativeGetCameraFov(float& outFov) {
    if (!g_theCamera || !g_camFindFov) return false;
    float fov = 0.f;
    bool ok = false;
    if (!GameTasks::RunQuery([&fov, &ok]() {
            if (!g_theCamera || !g_camFindFov) return;
            const float value = g_camFindFov(g_theCamera);
            if (std::isfinite(value) && value > 0.f) {
                fov = value;
                ok = true;
            }
        })) {
        return false;
    }
    if (!ok) return false;
    outFov = fov;
    return true;
}

bool ReadPedFlagBit(uint32_t id, size_t wordIndex, uint32_t mask, bool& outValue) {
    bool ok = false;
    bool val = false;
    if (!GameTasks::RunQuery([id, wordIndex, mask, &ok, &val]() {
            void* ped = Game::ElementEntity(id);
            if (!ped) return;
            auto* bytes = static_cast<const uint8_t*>(ped);
            if ((bytes[kEntityTypeOff] & 0x07u) != 3u) return;
            uint32_t word = 0;
            std::memcpy(&word, bytes + kPedFlagsOff + wordIndex * sizeof(uint32_t), sizeof(word));
            val = (word & mask) != 0u;
            ok = true;
        })) {
        return false;
    }
    if (!ok) return false;
    outValue = val;
    return true;
}

bool WritePedFlagBits(uint32_t id, size_t wordIndex, uint32_t clearMask, uint32_t setBits) {
    GameTasks::Defer([id, wordIndex, clearMask, setBits]() {
        void* ped = Game::ElementEntity(id);
        if (!ped) return true;
        auto* bytes = static_cast<uint8_t*>(ped);
        if ((bytes[kEntityTypeOff] & 0x07u) != 3u) return true;
        uint32_t word = 0;
        std::memcpy(&word, bytes + kPedFlagsOff + wordIndex * sizeof(uint32_t), sizeof(word));
        word = (word & ~clearMask) | setBits;
        std::memcpy(bytes + kPedFlagsOff + wordIndex * sizeof(uint32_t), &word, sizeof(word));
        return true;
    });
    return true;
}

bool NativeIsPedStanding(uint32_t id, bool& outStanding) {
    return ReadPedFlagBit(id, 0, 1u << 0, outStanding);
}

bool NativeIsPedDucking(uint32_t id, bool& outDucking) {
    return ReadPedFlagBit(id, 0, 1u << 26, outDucking);
}

bool NativeIsPedFootBloodEnabled(uint32_t id, bool& outEnabled) {
    return ReadPedFlagBit(id, 0, 1u << 28, outEnabled);
}

bool NativeSetPedFootBloodEnabled(uint32_t id, bool enabled) {
    return WritePedFlagBits(id, 0, 1u << 28, enabled ? (1u << 28) : 0u);
}

bool NativeSetPedBleeding(uint32_t id, bool bleeding) {
    return WritePedFlagBits(id, 0, 1u << 18, bleeding ? (1u << 18) : 0u);
}

bool NativeSetPedCanBeKnockedOffBike(uint32_t id, bool canBeKnocked) {
    // CPedFlags word 2 bits 27..28 (CantBeKnockedOffBike): 1=never, 2=always.
    return WritePedFlagBits(id, 2, 3u << 27, (canBeKnocked ? 2u : 1u) << 27);
}

bool NativeSetPedMissionActor(uint32_t id, bool mission) {
    GameTasks::Defer([id, mission]() {
        void* ped = Game::ElementEntity(id);
        if (!ped) return true;
        auto* bytes = static_cast<uint8_t*>(ped);
        if ((bytes[kEntityTypeOff] & 0x07u) != 3u) return true;
        bytes[kPedCreatedByOff] = mission ? 2u : 1u;
        return true;
    });
    return true;
}

bool NativeSetPedSuffersCriticalHits(uint32_t id, bool suffers) {
    // CPedFlags word 1 bit 12: bNoCriticalHits.
    return WritePedFlagBits(id, 1, 1u << 12, suffers ? 0u : (1u << 12));
}

bool NativeSetPedCanBeDraggedOut(uint32_t id, bool canDrag) {
    // CPedFlags word 0 bit 29: bDontDragMeOutCar.
    return WritePedFlagBits(id, 0, 1u << 29, canDrag ? 0u : (1u << 29));
}

bool NativeSetPedStayInSamePlace(uint32_t id, bool stay) {
    // CPedFlags word 0 bit 21: bStayInSamePlace.
    return WritePedFlagBits(id, 0, 1u << 21, stay ? (1u << 21) : 0u);
}

bool NativeSetPedNeverTargeted(uint32_t id, bool never) {
    // CPedFlags word 1 bit 28: bNeverEverTargetThisPed.
    return WritePedFlagBits(id, 1, 1u << 28, never ? (1u << 28) : 0u);
}

bool NativeGetPedWeaponAccuracy(uint32_t id, int& outAccuracy) {
    bool ok = false;
    int value = 0;
    if (!GameTasks::RunQuery([id, &ok, &value]() {
            void* ped = Game::ElementEntity(id);
            if (!ped) return;
            auto* bytes = static_cast<const uint8_t*>(ped);
            if ((bytes[kEntityTypeOff] & 0x07u) != 3u) return;
            value = static_cast<int>(bytes[kPedWeaponAccuracyOff]);
            ok = true;
        })) {
        return false;
    }
    if (!ok) return false;
    outAccuracy = value;
    return true;
}

bool NativeSetPedWeaponAccuracy(uint32_t id, int accuracy) {
    const uint8_t clamped = static_cast<uint8_t>(std::clamp(accuracy, 0, 255));
    GameTasks::Defer([id, clamped]() {
        void* ped = Game::ElementEntity(id);
        if (!ped) return true;
        auto* bytes = static_cast<uint8_t*>(ped);
        if ((bytes[kEntityTypeOff] & 0x07u) != 3u) return true;
        bytes[kPedWeaponAccuracyOff] = clamped;
        return true;
    });
    return true;
}

bool NativeGetPedWeaponShootingRate(uint32_t id, int& outRate) {
    bool ok = false;
    int value = 0;
    if (!GameTasks::RunQuery([id, &ok, &value]() {
            void* ped = Game::ElementEntity(id);
            if (!ped) return;
            auto* bytes = static_cast<const uint8_t*>(ped);
            if ((bytes[kEntityTypeOff] & 0x07u) != 3u) return;
            value = static_cast<int>(bytes[kPedShootRateOff]);
            ok = true;
        })) {
        return false;
    }
    if (!ok) return false;
    outRate = value;
    return true;
}

bool NativeSetPedWeaponShootingRate(uint32_t id, int rate) {
    const uint8_t clamped = static_cast<uint8_t>(std::clamp(rate, 0, 255));
    GameTasks::Defer([id, clamped]() {
        void* ped = Game::ElementEntity(id);
        if (!ped) return true;
        auto* bytes = static_cast<uint8_t*>(ped);
        if ((bytes[kEntityTypeOff] & 0x07u) != 3u) return true;
        bytes[kPedShootRateOff] = clamped;
        return true;
    });
    return true;
}

bool NativeIsPedReloadingWeapon(uint32_t id, bool& outReloading) {
    bool ok = false;
    bool reloading = false;
    if (!GameTasks::RunQuery([id, &ok, &reloading]() {
            void* ped = Game::ElementEntity(id);
            if (!ped) return;
            auto* bytes = static_cast<const uint8_t*>(ped);
            if ((bytes[kEntityTypeOff] & 0x07u) != 3u) return;
            const int8_t slot = static_cast<int8_t>(bytes[kPedActiveWeaponSlotOff]);
            if (slot >= 0 && slot < 13) {
                uint32_t state = 0;
                std::memcpy(&state, bytes + kPedWeaponsOff + static_cast<size_t>(slot) * kPedWeaponStride + kWeaponStateOff,
                            sizeof(state));
                reloading = (state == 2u);
            }
            ok = true;
        })) {
        return false;
    }
    if (!ok) return false;
    outReloading = reloading;
    return true;
}

bool NativeDoesPedHaveJetPack(uint32_t id, bool& outHasJetPack) {
    if (!g_intelGetJetPack) return false;
    bool ok = false;
    bool has = false;
    if (!GameTasks::RunQuery([id, &ok, &has]() {
            void* ped = Game::ElementEntity(id);
            if (!ped || !g_intelGetJetPack) return;
            auto* bytes = static_cast<const uint8_t*>(ped);
            if ((bytes[kEntityTypeOff] & 0x07u) != 3u) return;
            void* intel = nullptr;
            std::memcpy(&intel, bytes + kPedIntelPtrOff, sizeof(intel));
            if (!intel) return;
            has = (g_intelGetJetPack(intel) != nullptr);
            ok = true;
        })) {
        return false;
    }
    if (!ok) return false;
    outHasJetPack = has;
    return true;
}

bool NativeGetPedTaskChain(uint32_t id, bool secondary, int slot, int outTaskIds[4], int& outCount) {
    if (!outTaskIds) return false;
    if (secondary && (slot < 0 || slot >= 6 || !g_taskMgrSecondary)) return false;
    if (!secondary && (slot < 0 || slot >= 5)) return false;
    bool ok = false;
    int ids[4] = {-1, -1, -1, -1};
    int count = 0;
    if (!GameTasks::RunQuery([id, secondary, slot, &ok, &ids, &count]() {
            void* ped = Game::ElementEntity(id);
            if (!ped) return;
            auto* bytes = static_cast<const uint8_t*>(ped);
            if ((bytes[kEntityTypeOff] & 0x07u) != 3u) return;
            uint8_t* intel = nullptr;
            std::memcpy(&intel, bytes + kPedIntelPtrOff, sizeof(intel));
            if (!intel) return;
            uint8_t* taskMgr = intel + kIntelTaskMgrOff;
            void* task = nullptr;
            if (secondary) {
                task = g_taskMgrSecondary ? g_taskMgrSecondary(taskMgr, slot) : nullptr;
            } else {
                std::memcpy(&task, taskMgr + static_cast<size_t>(slot) * sizeof(void*), sizeof(task));
            }
            while (task && count < 4) {
                auto** vt = *reinterpret_cast<void***>(task);
                if (!vt || !vt[kTaskVtableGetIdSlot]) break;
                auto getId = reinterpret_cast<TaskGetIdFn>(vt[kTaskVtableGetIdSlot]);
                ids[count++] = getId(task);
                if (!vt[kTaskVtableSubTaskSlot]) break;
                auto getSub = reinterpret_cast<TaskGetSubTaskFn>(vt[kTaskVtableSubTaskSlot]);
                task = getSub(task);
            }
            ok = true;
        })) {
        return false;
    }
    if (!ok) return false;
    for (int i = 0; i < 4; ++i) outTaskIds[i] = ids[i];
    outCount = count;
    return true;
}

bool NativeGetPedSimplestTaskId(uint32_t id, int& outTaskId) {
    if (!g_taskMgrSimplestActive) return false;
    bool ok = false;
    int taskId = -1;
    if (!GameTasks::RunQuery([id, &ok, &taskId]() {
            void* ped = Game::ElementEntity(id);
            if (!ped || !g_taskMgrSimplestActive) return;
            auto* bytes = static_cast<const uint8_t*>(ped);
            if ((bytes[kEntityTypeOff] & 0x07u) != 3u) return;
            uint8_t* intel = nullptr;
            std::memcpy(&intel, bytes + kPedIntelPtrOff, sizeof(intel));
            if (!intel) return;
            void* task = g_taskMgrSimplestActive(intel + kIntelTaskMgrOff);
            if (task) {
                auto** vt = *reinterpret_cast<void***>(task);
                if (vt && vt[kTaskVtableGetIdSlot]) {
                    auto getId = reinterpret_cast<TaskGetIdFn>(vt[kTaskVtableGetIdSlot]);
                    taskId = getId(task);
                }
            }
            ok = true;
        })) {
        return false;
    }
    if (!ok) return false;
    outTaskId = taskId;
    return true;
}

bool NativeSetPedAimTarget(uint32_t id, float x, float y, float z) {
    if (!g_pedSetAimFlag) return false;
    (void)z;
    GameTasks::Defer([id, x, y]() {
        if (!g_pedSetAimFlag) return true;
        void* ped = Game::ElementEntity(id);
        if (!ped) return true;
        auto* bytes = static_cast<const uint8_t*>(ped);
        if ((bytes[kEntityTypeOff] & 0x07u) != 3u) return true;
        float px = 0.f, py = 0.f, pz = 0.f;
        if (!ReadEntityPosition(bytes, px, py, pz)) return true;
        const float heading = std::atan2(-(x - px), y - py);
        if (std::isfinite(heading)) g_pedSetAimFlag(ped, heading);
        return true;
    });
    return true;
}

bool NativeSetPedLookAt(uint32_t id, float x, float y, float z, int timeMs) {
    if (!g_pedSetLookFlag || !g_pedClearLookFlag) return false;
    (void)z;
    GameTasks::Defer([id, x, y, timeMs]() {
        void* ped = Game::ElementEntity(id);
        if (!ped) return true;
        auto* bytes = static_cast<const uint8_t*>(ped);
        if ((bytes[kEntityTypeOff] & 0x07u) != 3u) return true;
        if (timeMs == 0) {
            if (g_pedClearLookFlag) g_pedClearLookFlag(ped);
            return true;
        }
        float px = 0.f, py = 0.f, pz = 0.f;
        if (!ReadEntityPosition(bytes, px, py, pz)) return true;
        const float heading = std::atan2(-(x - px), y - py);
        if (std::isfinite(heading) && g_pedSetLookFlag) g_pedSetLookFlag(ped, heading, false, false);
        return true;
    });
    return true;
}

bool NativeLoadMissionTextBlock(const char* blockName) {
    if (!g_theText || !g_textLoadMission || !blockName || !blockName[0]) return false;
    std::string block(blockName);
    GameTasks::Defer([block = std::move(block)]() mutable {
        if (!g_theText || !g_textLoadMission) return true;
        std::vector<char> buf(block.begin(), block.end());
        buf.push_back('\0');
        g_textLoadMission(g_theText, buf.data());
        return true;
    });
    return true;
}

bool NativeShowMissionText(const char* gxtKey, uint32_t timeMs, uint16_t flags) {
    if (!g_theText || !g_textGet || !g_msgAddJumpQ || !gxtKey || !gxtKey[0]) return false;
    std::string key(gxtKey);
    GameTasks::Defer([key = std::move(key), timeMs, flags]() {
        if (!g_theText || !g_textGet || !g_msgAddJumpQ) return true;
        uint16_t* text = g_textGet(g_theText, key.c_str());
        if (text) g_msgAddJumpQ(key.c_str(), text, timeMs, flags, false);
        return true;
    });
    return true;
}

bool NativeShowMissionHelp(const char* gxtKey, bool permanent) {
    if (!g_theText || !g_textGet || !g_hudSetHelp || !gxtKey || !gxtKey[0]) return false;
    std::string key(gxtKey);
    GameTasks::Defer([key = std::move(key), permanent]() {
        if (!g_theText || !g_textGet || !g_hudSetHelp) return true;
        uint16_t* text = g_textGet(g_theText, key.c_str());
        if (text) g_hudSetHelp(key.c_str(), text, false, permanent, false, 0u);
        return true;
    });
    return true;
}

bool NativeShowMissionBigText(const char* gxtKey, uint32_t timeMs, uint16_t style) {
    if (!g_theText || !g_textGet || !g_msgAddBig || !gxtKey || !gxtKey[0]) return false;
    std::string key(gxtKey);
    GameTasks::Defer([key = std::move(key), timeMs, style]() {
        if (!g_theText || !g_textGet || !g_msgAddBig) return true;
        uint16_t* text = g_textGet(g_theText, key.c_str());
        if (text) g_msgAddBig(text, timeMs, style);
        return true;
    });
    return true;
}

bool NativeClearMissionTexts() {
    if (!g_msgClear) return false;
    GameTasks::Defer([]() {
        if (g_msgClear) g_msgClear(1u);
        return true;
    });
    return true;
}

bool NativeClearMissionHelp() {
    if (!g_hudSetHelp) return false;
    GameTasks::Defer([]() {
        if (g_hudSetHelp) g_hudSetHelp(nullptr, nullptr, false, false, false, 0u);
        return true;
    });
    return true;
}

bool NativeSetWeaponProperty(int weaponId, int skill, const char* property, double a, double b, double c) {
    if (!g_weaponInfoGet || !property || weaponId < 0 || weaponId > 46 || skill < 0 || skill > 2) return false;
    std::string prop(property);
    GameTasks::Defer([weaponId, skill, prop = std::move(prop), a, b, c]() {
        if (!g_weaponInfoGet) return true;
        uint8_t* info = g_weaponInfoGet(static_cast<int32_t>(weaponId), static_cast<int8_t>(skill));
        if (!info) return true;
        // All offsets byte-for-byte verified against _ZN11CWeaponInfo14LoadWeaponDataEv (0x560ea8) in libGTASA.so 2.10 arm64.
        auto writeF32 = [&](size_t off, double v) {
            const float f = static_cast<float>(v);
            std::memcpy(info + off, &f, sizeof(f));
        };
        if (prop == "target_range") writeF32(0x04, a);
        else if (prop == "weapon_range") writeF32(0x08, a);
        else if (prop == "flags") {
            const uint32_t v = static_cast<uint32_t>(static_cast<int64_t>(a));
            std::memcpy(info + 0x18, &v, sizeof(v));
        } else if (prop == "maximum_clip_ammo") {
            const int16_t v = static_cast<int16_t>(std::clamp(static_cast<int>(a), 0, 32767));
            std::memcpy(info + 0x20, &v, sizeof(v));
        } else if (prop == "damage") {
            const int16_t v = static_cast<int16_t>(std::clamp(static_cast<int>(a), 0, 32767));
            std::memcpy(info + 0x22, &v, sizeof(v));
        } else if (prop == "fire_offset") {
            writeF32(0x24, a);
            writeF32(0x28, b);
            writeF32(0x2C, c);
        } else if (prop == "req_stat_level") {
            const int32_t v = static_cast<int32_t>(a);
            std::memcpy(info + 0x34, &v, sizeof(v));
        } else if (prop == "accuracy") writeF32(0x38, a);
        else if (prop == "move_speed") writeF32(0x3C, a);
        else if (prop == "anim_loop_start") writeF32(0x40, a);
        else if (prop == "anim_loop_stop") writeF32(0x44, a);
        else if (prop == "anim_loop_bullet_fire") writeF32(0x48, a);
        else if (prop == "anim2_loop_start") writeF32(0x4C, a);
        else if (prop == "anim2_loop_stop") writeF32(0x50, a);
        else if (prop == "anim2_loop_bullet_fire") writeF32(0x54, a);
        else if (prop == "anim_breakout_time") writeF32(0x58, a);
        else if (prop == "speed") writeF32(0x5C, a);
        else if (prop == "radius") writeF32(0x60, a);
        else if (prop == "life_span") writeF32(0x64, a);
        else if (prop == "spread") writeF32(0x68, a);
        return true;
    });
    return true;
}

}  // namespace

void InstallWorldNativeHooks(void* gtasaHandle) {
    g_lib = gtasaHandle;
    if (!g_lib) return;

    int resolvedPools = 0;
    for (int i = 0; i < Models::kPoolCount; ++i) {
        g_pools[i] = reinterpret_cast<NativePoolHeader**>(dlsym(g_lib, kPoolSymbols[i]));
        if (g_pools[i]) ++resolvedPools;
    }

    g_addExplosion = ResolveAny<AddExplosionFn>(g_lib, {
        "_ZN10CExplosion12AddExplosionEP7CEntityS1_14eExplosionType7CVectorjhfh",
    });
    g_fireManager = dlsym(g_lib, "gFireManager");
    g_startFire = ResolveAny<StartFireFn>(g_lib, {
        "_ZN12CFireManager9StartFireE7CVectorfhP7CEntityjah",
    });
    g_startEntityFire = ResolveAny<StartEntityFireFn>(g_lib, {
        "_ZN12CFireManager9StartFireEP7CEntityS1_fhja",
    });
    g_extinguishPoint = ResolveAny<ExtinguishPointFn>(g_lib, {
        "_ZN12CFireManager15ExtinguishPointE7CVectorf",
    });
    g_addPointLight = ResolveAny<AddPointLightFn>(g_lib, {
        "_ZN12CPointLights8AddLightEh7CVectorS0_ffffhbP7CEntity",
    });
    g_loadScene = ResolveAny<LoadSceneFn>(g_lib, {
        "_ZN10CStreaming9LoadSceneERK7CVector",
    });
    g_loadSceneCollision = ResolveAny<LoadSceneFn>(g_lib, {
        "_ZN10CStreaming18LoadSceneCollisionERK7CVector",
    });
    g_createSwatRope = ResolveAny<CreateSwatRopeFn>(g_lib, {
        "_ZN6CRopes20CreateRopeForSwatPedERK7CVector",
    });
    g_testSphere = ResolveAny<TestSphereFn>(g_lib, {
        "_ZN6CWorld22TestSphereAgainstWorldE7CVectorfP7CEntitybbbbbb",
    });
    g_worldRemove = ResolveAny<WorldEntityFn>(g_lib, {"_ZN6CWorld6RemoveEP7CEntity"});
    g_worldAdd = ResolveAny<WorldEntityFn>(g_lib, {"_ZN6CWorld3AddEP7CEntity"});
    g_garages = reinterpret_cast<uint8_t*>(dlsym(g_lib, "_ZN8CGarages8aGaragesE"));
    g_numGarages = reinterpret_cast<int32_t*>(dlsym(g_lib, "_ZN8CGarages10NumGaragesE"));
    g_garageOpen = ResolveAny<GarageMethodFn>(g_lib, {"_ZN7CGarage15OpenThisGarageEv"});
    g_garageClose = ResolveAny<GarageMethodFn>(g_lib, {"_ZN7CGarage16CloseThisGarageEv"});
    g_isGarageOpen = ResolveAny<IsGarageOpenFn>(g_lib, {"_ZN8CGarages12IsGarageOpenEs"});

    g_timeScale = reinterpret_cast<float*>(dlsym(g_lib, "_ZN6CTimer13ms_fTimeScaleE"));
    g_msPerGameMinute = reinterpret_cast<uint32_t*>(dlsym(g_lib, "_ZN6CClock29ms_nMillisecondsPerGameMinuteE"));
    g_newWeatherType = reinterpret_cast<int16_t*>(dlsym(g_lib, "_ZN8CWeather14NewWeatherTypeE"));
    g_forcedWeatherType = reinterpret_cast<int16_t*>(dlsym(g_lib, "_ZN8CWeather17ForcedWeatherTypeE"));
    g_weatherInterp = reinterpret_cast<float*>(dlsym(g_lib, "_ZN8CWeather18InterpolationValueE"));
    g_weatherRain = reinterpret_cast<float*>(dlsym(g_lib, "_ZN8CWeather4RainE"));
    g_scriptsForceRain = reinterpret_cast<bool*>(dlsym(g_lib, "_ZN8CWeather17bScriptsForceRainE"));
    g_weatherWind = reinterpret_cast<float*>(dlsym(g_lib, "_ZN8CWeather4WindE"));
    g_weatherWindDir = reinterpret_cast<CVector*>(dlsym(g_lib, "_ZN8CWeather7WindDirE"));
    g_weatherWavyness = reinterpret_cast<float*>(dlsym(g_lib, "_ZN8CWeather8WavynessE"));
    g_weatherHeatHaze = reinterpret_cast<float*>(dlsym(g_lib, "_ZN8CWeather8HeatHazeE"));
    g_moonSize = reinterpret_cast<int32_t*>(dlsym(g_lib, "_ZN8CCoronas8MoonSizeE"));
    g_currArea = reinterpret_cast<int32_t*>(dlsym(g_lib, "_ZN5CGame8currAreaE"));
    g_speedFx = reinterpret_cast<bool*>(dlsym(g_lib, "_ZN12CPostEffects10m_bSpeedFXE"));
    g_speedFxAlpha = reinterpret_cast<int32_t*>(dlsym(g_lib, "_ZN12CPostEffects14m_SpeedFXAlphaE"));
    g_grainEnable = reinterpret_cast<bool*>(dlsym(g_lib, "_ZN12CPostEffects14m_bGrainEnableE"));
    g_grainStrength = reinterpret_cast<int32_t*>(dlsym(g_lib, "_ZN12CPostEffects15m_grainStrengthE"));
    g_heatHazeEnable = reinterpret_cast<bool*>(dlsym(g_lib, "_ZN12CPostEffects13m_bHeatHazeFXE"));
    g_heatHazeIntensity = reinterpret_cast<int32_t*>(dlsym(g_lib, "_ZN12CPostEffects21m_HeatHazeFXIntensityE"));
    g_heatHazeRandomShift = reinterpret_cast<int32_t*>(dlsym(g_lib, "_ZN12CPostEffects23m_HeatHazeFXRandomShiftE"));
    g_heatHazeSpeedMin = reinterpret_cast<int32_t*>(dlsym(g_lib, "_ZN12CPostEffects20m_HeatHazeFXSpeedMinE"));
    g_heatHazeSpeedMax = reinterpret_cast<int32_t*>(dlsym(g_lib, "_ZN12CPostEffects20m_HeatHazeFXSpeedMaxE"));
    g_heatHazeScanX = reinterpret_cast<int32_t*>(dlsym(g_lib, "_ZN12CPostEffects21m_HeatHazeFXScanSizeXE"));
    g_heatHazeScanY = reinterpret_cast<int32_t*>(dlsym(g_lib, "_ZN12CPostEffects21m_HeatHazeFXScanSizeYE"));
    g_heatHazeRenderX = reinterpret_cast<int32_t*>(dlsym(g_lib, "_ZN12CPostEffects23m_HeatHazeFXRenderSizeXE"));
    g_heatHazeRenderY = reinterpret_cast<int32_t*>(dlsym(g_lib, "_ZN12CPostEffects23m_HeatHazeFXRenderSizeYE"));
    g_numOccludersOnMap = reinterpret_cast<int32_t*>(dlsym(g_lib, "_ZN10COcclusion17NumOccludersOnMapE"));

    g_tcFarClip = reinterpret_cast<int16_t*>(dlsym(g_lib, "_ZN10CTimeCycle10m_fFarClipE"));
    g_tcFogStart = reinterpret_cast<int16_t*>(dlsym(g_lib, "_ZN10CTimeCycle11m_fFogStartE"));
    g_tcSunSize = reinterpret_cast<uint8_t*>(dlsym(g_lib, "_ZN10CTimeCycle10m_fSunSizeE"));
    g_tcSkyTop[0] = reinterpret_cast<uint8_t*>(dlsym(g_lib, "_ZN10CTimeCycle12m_nSkyTopRedE"));
    g_tcSkyTop[1] = reinterpret_cast<uint8_t*>(dlsym(g_lib, "_ZN10CTimeCycle14m_nSkyTopGreenE"));
    g_tcSkyTop[2] = reinterpret_cast<uint8_t*>(dlsym(g_lib, "_ZN10CTimeCycle13m_nSkyTopBlueE"));
    g_tcSkyBottom[0] = reinterpret_cast<uint8_t*>(dlsym(g_lib, "_ZN10CTimeCycle15m_nSkyBottomRedE"));
    g_tcSkyBottom[1] = reinterpret_cast<uint8_t*>(dlsym(g_lib, "_ZN10CTimeCycle17m_nSkyBottomGreenE"));
    g_tcSkyBottom[2] = reinterpret_cast<uint8_t*>(dlsym(g_lib, "_ZN10CTimeCycle16m_nSkyBottomBlueE"));
    g_tcSunCore[0] = reinterpret_cast<uint8_t*>(dlsym(g_lib, "_ZN10CTimeCycle13m_nSunCoreRedE"));
    g_tcSunCore[1] = reinterpret_cast<uint8_t*>(dlsym(g_lib, "_ZN10CTimeCycle15m_nSunCoreGreenE"));
    g_tcSunCore[2] = reinterpret_cast<uint8_t*>(dlsym(g_lib, "_ZN10CTimeCycle14m_nSunCoreBlueE"));
    g_tcSunCorona[0] = reinterpret_cast<uint8_t*>(dlsym(g_lib, "_ZN10CTimeCycle15m_nSunCoronaRedE"));
    g_tcSunCorona[1] = reinterpret_cast<uint8_t*>(dlsym(g_lib, "_ZN10CTimeCycle17m_nSunCoronaGreenE"));
    g_tcSunCorona[2] = reinterpret_cast<uint8_t*>(dlsym(g_lib, "_ZN10CTimeCycle16m_nSunCoronaBlueE"));
    g_tcWater[0] = reinterpret_cast<uint8_t*>(dlsym(g_lib, "_ZN10CTimeCycle11m_fWaterRedE"));
    g_tcWater[1] = reinterpret_cast<uint8_t*>(dlsym(g_lib, "_ZN10CTimeCycle13m_fWaterGreenE"));
    g_tcWater[2] = reinterpret_cast<uint8_t*>(dlsym(g_lib, "_ZN10CTimeCycle12m_fWaterBlueE"));
    g_tcWater[3] = reinterpret_cast<uint8_t*>(dlsym(g_lib, "_ZN10CTimeCycle13m_fWaterAlphaE"));
    g_tcCloudAlpha = reinterpret_cast<uint8_t*>(dlsym(g_lib, "_ZN10CTimeCycle13m_fCloudAlphaE"));

    g_theCamera = reinterpret_cast<uint8_t*>(dlsym(g_lib, "TheCamera"));
    g_camSetFadeColour = ResolveAny<CameraSetFadeColourFn>(g_lib, {"_ZN7CCamera13SetFadeColourEhhh"});
    g_camFade = ResolveAny<CameraFadeFn>(g_lib, {"_ZN7CCamera4FadeEfs"});
    g_camShake = ResolveAny<CameraCamShakeFn>(g_lib, {"_ZN7CCamera8CamShakeEffff"});
    g_camSetNearClipScript = ResolveAny<CameraSetNearClipScriptFn>(g_lib, {"_ZN7CCamera17SetNearClipScriptEf"});
    g_nightVisionSwitch = ResolveAny<PostFxVisionSwitchFn>(g_lib, {"_ZN12CPostEffects23ScriptNightVisionSwitchEb"});
    g_infraredVisionSwitch = ResolveAny<PostFxVisionSwitchFn>(g_lib, {"_ZN12CPostEffects26ScriptInfraredVisionSwitchEb"});
    g_setDrunkBlur = ResolveAny<MBlurSetDrunkFn>(g_lib, {"_ZN6CMBlur12SetDrunkBlurEf"});
    g_clearDrunkBlur = ResolveAny<MBlurClearDrunkFn>(g_lib, {"_ZN6CMBlur14ClearDrunkBlurEv"});
    g_colorFilterEnable = reinterpret_cast<bool*>(dlsym(g_lib, "_ZN12CPostEffects14m_bColorEnableE"));
    g_tcPostFx1[0] = reinterpret_cast<uint8_t*>(dlsym(g_lib, "_ZN10CTimeCycle13m_fPostFx1RedE"));
    g_tcPostFx1[1] = reinterpret_cast<uint8_t*>(dlsym(g_lib, "_ZN10CTimeCycle15m_fPostFx1GreenE"));
    g_tcPostFx1[2] = reinterpret_cast<uint8_t*>(dlsym(g_lib, "_ZN10CTimeCycle14m_fPostFx1BlueE"));
    g_tcPostFx1[3] = reinterpret_cast<uint8_t*>(dlsym(g_lib, "_ZN10CTimeCycle15m_fPostFx1AlphaE"));
    g_tcPostFx2[0] = reinterpret_cast<uint8_t*>(dlsym(g_lib, "_ZN10CTimeCycle13m_fPostFx2RedE"));
    g_tcPostFx2[1] = reinterpret_cast<uint8_t*>(dlsym(g_lib, "_ZN10CTimeCycle15m_fPostFx2GreenE"));
    g_tcPostFx2[2] = reinterpret_cast<uint8_t*>(dlsym(g_lib, "_ZN10CTimeCycle14m_fPostFx2BlueE"));
    g_tcPostFx2[3] = reinterpret_cast<uint8_t*>(dlsym(g_lib, "_ZN10CTimeCycle15m_fPostFx2AlphaE"));
    g_waterGetLevel = ResolveAny<WaterGetLevelFn>(g_lib, {"_ZN11CWaterLevel13GetWaterLevelEfffPfbP7CVector"});
    g_waterGetLevelNoWaves = ResolveAny<WaterGetLevelNoWavesFn>(g_lib, {"_ZN11CWaterLevel20GetWaterLevelNoWavesEfffPfS0_S0_"});
    g_waterTestLine = ResolveAny<WaterTestLineFn>(g_lib, {"_ZN11CWaterLevel20TestLineAgainstWaterE7CVectorS0_PS0_"});
    g_waterVertices = reinterpret_cast<uint8_t*>(dlsym(g_lib, "_ZN11CWaterLevel11m_aVerticesE"));
    g_numWaterVertices = reinterpret_cast<int32_t*>(dlsym(g_lib, "_ZN11CWaterLevel21m_nNumOfWaterVerticesE"));

    g_fx = dlsym(g_lib, "g_fx");
    g_fxBlood = ResolveAny<FxBloodFn>(g_lib, {"_ZN4Fx_c8AddBloodER7CVectorS1_if"});
    g_fxWood = ResolveAny<FxBloodFn>(g_lib, {"_ZN4Fx_c7AddWoodER7CVectorS1_if"});
    g_fxBulletImpact = ResolveAny<FxBulletImpactFn>(g_lib, {"_ZN4Fx_c15AddBulletImpactER7CVectorS1_iif"});
    g_fxSparks = ResolveAny<FxSparksFn>(g_lib, {"_ZN4Fx_c9AddSparksER7CVectorS1_fiS0_hff"});
    g_fxTyreBurst = ResolveAny<FxTyreBurstFn>(g_lib, {"_ZN4Fx_c12AddTyreBurstER7CVectorS1_"});
    g_fxPunchImpact = ResolveAny<FxPunchImpactFn>(g_lib, {"_ZN4Fx_c14AddPunchImpactER7CVectorS1_i"});
    g_fxDebris = ResolveAny<FxDebrisFn>(g_lib, {"_ZN4Fx_c9AddDebrisER7CVectorR6RwRGBAfi"});
    g_fxGlass = ResolveAny<FxDebrisFn>(g_lib, {"_ZN4Fx_c8AddGlassER7CVectorR6RwRGBAfi"});

    g_worldPlayers = reinterpret_cast<uint8_t*>(dlsym(g_lib, "_ZN6CWorld7PlayersE"));
    g_pedGetBonePosition = ResolveAny<PedGetBonePositionFn>(g_lib, {"_ZN4CPed15GetBonePositionER5RwV3djb"});
    g_entityUpdateRpHAnim = ResolveAny<EntityUpdateRpHAnimFn>(g_lib, {"_ZN7CEntity13UpdateRpHAnimEv"});
    g_getAnimHierarchy = ResolveAny<GetAnimHierarchyFromSkinClumpFn>(g_lib, {"_Z29GetAnimHierarchyFromSkinClumpP7RpClump"});
    g_rpHAnimIdGetIndex = ResolveAny<RpHAnimIDGetIndexFn>(g_lib, {"_Z17RpHAnimIDGetIndexP16RpHAnimHierarchyi"});
    g_rpHAnimGetMatrixArray = ResolveAny<RpHAnimHierarchyGetMatrixArrayFn>(g_lib, {"_Z30RpHAnimHierarchyGetMatrixArrayP16RpHAnimHierarchy"});
    g_rpHAnimUpdateMatrices = ResolveAny<RpHAnimHierarchyUpdateMatricesFn>(g_lib, {"_Z30RpHAnimHierarchyUpdateMatricesP16RpHAnimHierarchy"});
    g_statsGetStatValue = ResolveAny<StatsGetStatValueFn>(g_lib, {"_ZN6CStats12GetStatValueEt"});
    g_statsSetStatValue = ResolveAny<StatsSetStatValueFn>(g_lib, {"_ZN6CStats12SetStatValueEtf"});
    g_pedSetMoveAnim = ResolveAny<PedSetMoveAnimFn>(g_lib, {"_ZN4CPed11SetMoveAnimEv"});
    g_pedRemoveBodyPart = ResolveAny<PedRemoveBodyPartFn>(g_lib, {"_ZN4CPed14RemoveBodyPartEia"});
    g_pedSay = ResolveAny<PedSayFn>(g_lib, {"_ZN4CPed3SayEtjfhhh"});
    g_modelInfoPtrs = reinterpret_cast<void**>(dlsym(g_lib, "_ZN10CModelInfo16ms_modelInfoPtrsE"));
    // Verify CBaseModelInfo::m_pColModel offset == +0x30 by checking the first instruction of
    // _ZN15CClumpModelInfo14GetBoundingBoxEv is `ldr x0, [x0, #0x30]` (0xF9401800).
    if (void* getBbox = dlsym(g_lib, "_ZN15CClumpModelInfo14GetBoundingBoxEv")) {
        uint32_t insn = 0;
        std::memcpy(&insn, getBbox, sizeof(insn));
        g_modelColOffsetVerified = (insn == 0xF9401800u);
    }
    g_entityGetComDist = ResolveAny<EntityGetComDistFn>(g_lib, {"_ZN7CEntity40GetDistanceFromCentreOfMassToBaseOfModelEv"});
    g_entityGetIsOnScreen = ResolveAny<EntityGetIsOnScreenFn>(g_lib, {"_ZN7CEntity13GetIsOnScreenEv"});
    // Verify CVehicle vtable slots against live _ZTV8CVehicle:
    //   28 (OpenDoor), 42 (BlowUpCar), 45 (BurstTyre), 51 (Fix), 56 (GetNumContactWheels).
    if (auto** vehZtv = reinterpret_cast<void**>(dlsym(g_lib, "_ZTV8CVehicle"))) {
        void** vptr = vehZtv + 2;  // Itanium ABI vtable address point is +0x10 past _ZTV
        void* symOpenDoor = dlsym(g_lib, "_ZN8CVehicle8OpenDoorEP4CPedi6eDoorsfb");
        void* symBlowUp = dlsym(g_lib, "_ZN8CVehicle9BlowUpCarEP7CEntityh");
        void* symBurstTyre = dlsym(g_lib, "_ZN8CVehicle9BurstTyreEhb");
        void* symFix = dlsym(g_lib, "_ZN8CVehicle3FixEv");
        void* symWheels = dlsym(g_lib, "_ZN8CVehicle19GetNumContactWheelsEv");
        g_vehicleVtableOpenDoorVerified = (symOpenDoor && vptr[kVehicleVtableOpenDoorSlot] == symOpenDoor);
        g_vehicleVtableBlowUpVerified = (symBlowUp && vptr[kVehicleVtableBlowUpSlot] == symBlowUp);
        g_vehicleVtableBurstTyreVerified = (symBurstTyre && vptr[kVehicleVtableBurstTyreSlot] == symBurstTyre);
        g_vehicleVtableFixVerified = (symFix && vptr[kVehicleVtableFixSlot] == symFix);
        g_vehicleVtableWheelsVerified = (symWheels && vptr[kVehicleVtableContactWheelsSlot] == symWheels);
    }

    g_cheatsActive = reinterpret_cast<uint8_t*>(dlsym(g_lib, "_ZN6CCheat15m_aCheatsActiveE"));
    g_camSetZoomVeh = ResolveAny<CameraSetZoomFn>(g_lib, {"_ZN7CCamera27SetZoomValueCamStringScriptEs"});
    g_camSetZoomPed = ResolveAny<CameraSetZoomFn>(g_lib, {"_ZN7CCamera27SetZoomValueFollowPedScriptEs"});
    g_camTakeControl = ResolveAny<CameraTakeControlFn>(g_lib, {"_ZN7CCamera11TakeControlEP7CEntityssi"});
    g_autoSetTaxiLight = ResolveAny<AutoSetTaxiLightFn>(g_lib, {"_ZN11CAutomobile12SetTaxiLightEb"});
    g_vehSetRemap = ResolveAny<VehicleSetRemapFn>(g_lib, {"_ZN8CVehicle8SetRemapEi"});
    g_vehAddUpgrade = ResolveAny<VehicleUpgradeFn>(g_lib, {"_ZN8CVehicle17AddVehicleUpgradeEi"});
    g_vehRemoveUpgrade = ResolveAny<VehicleUpgradeFn>(g_lib, {"_ZN8CVehicle20RemoveVehicleUpgradeEi"});
    g_reqVehUpgrade = ResolveAny<RequestUpgradeFn>(g_lib, {"_ZN10CStreaming21RequestVehicleUpgradeEii"});
    g_hasVehUpgradeLoaded = ResolveAny<HasUpgradeLoadedFn>(g_lib, {"_ZN10CStreaming23HasVehicleUpgradeLoadedEi"});
    g_trainSetSpeed = ResolveAny<TrainSetSpeedFn>(g_lib, {"_ZN6CTrain13SetTrainSpeedEPS_f"});

    // Batch F symbol resolutions (all verified via nm -D + llvm-objdump on libGTASA.so 2.10 arm64).
    g_dmgSetDoor = ResolveAny<DamageSetDoorFn>(g_lib, {"_ZN14CDamageManager13SetDoorStatusE6eDoorsj"});
    g_dmgSetLight = ResolveAny<DamageSetLightFn>(g_lib, {"_ZN14CDamageManager14SetLightStatusE7eLightsj"});
    g_dmgSetPanel = ResolveAny<DamageSetPanelFn>(g_lib, {"_ZN14CDamageManager14SetPanelStatusEij"});
    g_dmgSetWheel = ResolveAny<DamageSetWheelFn>(g_lib, {"_ZN14CDamageManager14SetWheelStatusEij"});
    g_setClumpAlpha = ResolveAny<SetClumpAlphaFn>(g_lib, {"_ZN18CVisibilityPlugins13SetClumpAlphaEP7RpClumpi"});
    g_camFindFov = ResolveAny<CameraFindFovFn>(g_lib, {"_ZN7CCamera10FindCamFOVEv"});
    g_camLerpFov = ResolveAny<CameraLerpFovFn>(g_lib, {"_ZN7CCamera7LerpFOVEfffb"});
    g_intelGetJetPack = ResolveAny<PedIntelGetJetPackFn>(g_lib, {"_ZNK16CPedIntelligence14GetTaskJetPackEv"});
    g_taskMgrSimplestActive = ResolveAny<TaskMgrGetSimplestActiveFn>(g_lib, {"_ZNK12CTaskManager21GetSimplestActiveTaskEv"});
    g_taskMgrSecondary = ResolveAny<TaskMgrGetSecondaryFn>(g_lib, {"_ZNK12CTaskManager16GetTaskSecondaryEi"});
    g_pedSetAimFlag = ResolveAny<PedSetAimFlagFn>(g_lib, {"_ZN4CPed10SetAimFlagEf"});
    g_pedSetLookFlag = ResolveAny<PedSetLookFlagFn>(g_lib, {"_ZN4CPed11SetLookFlagEfbb"});
    g_pedClearLookFlag = ResolveAny<PedClearLookFlagFn>(g_lib, {"_ZN4CPed13ClearLookFlagEv"});
    g_theText = dlsym(g_lib, "TheText");
    g_textLoadMission = ResolveAny<TextLoadMissionFn>(g_lib, {"_ZN5CText15LoadMissionTextEPc"});
    g_textGet = ResolveAny<TextGetFn>(g_lib, {"_ZN5CText3GetEPKc"});
    g_msgAddJumpQ = ResolveAny<MessagesAddJumpQFn>(g_lib, {"_ZN9CMessages15AddMessageJumpQEPKcPtjtb"});
    g_msgAddBig = ResolveAny<MessagesAddBigFn>(g_lib, {"_ZN9CMessages13AddBigMessageEPtjt"});
    g_msgClear = ResolveAny<MessagesClearFn>(g_lib, {"_ZN9CMessages13ClearMessagesEh"});
    g_hudSetHelp = ResolveAny<HudSetHelpMessageFn>(g_lib, {"_ZN4CHud14SetHelpMessageEPKcPtbbbj"});
    g_weaponInfoGet = ResolveAny<WeaponInfoGetFn>(g_lib, {"_ZN11CWeaponInfo13GetWeaponInfoE11eWeaponTypea"});

    WorldNative::Hooks hooks;
    hooks.getPoolCapacity = &NativeGetPoolCapacity;
    hooks.getPoolUsedCapacity = &NativeGetPoolUsedCapacity;
    hooks.createExplosion = &NativeCreateExplosion;
    hooks.createFire = &NativeCreateFire;
    hooks.extinguishFire = &NativeExtinguishFire;
    hooks.addPointLight = &NativeAddPointLight;
    hooks.preloadWorldArea = &NativePreloadWorldArea;
    hooks.createSwatRope = &NativeCreateSwatRope;
    hooks.testSphereAgainstWorld = &NativeTestSphereAgainstWorld;
    hooks.getGarageGeometry = &NativeGetGarageGeometry;
    hooks.removeWorldModel = &NativeRemoveWorldModel;
    hooks.restoreWorldModel = &NativeRestoreWorldModel;
    hooks.restoreAllWorldModels = &NativeRestoreAllWorldModels;
    hooks.setGameSpeed = &NativeSetGameSpeed;
    hooks.setMinuteDuration = &NativeSetMinuteDuration;
    hooks.setWeatherBlended = &NativeSetWeatherBlended;
    hooks.setRainLevel = &NativeSetRainLevel;
    hooks.setWindVelocity = &NativeSetWindVelocity;
    hooks.setWaveHeight = &NativeSetWaveHeight;
    hooks.setMoonSize = &NativeSetMoonSize;
    hooks.setBlurLevel = &NativeSetBlurLevel;
    hooks.setGrainLevel = &NativeSetGrainLevel;
    hooks.setHeatHaze = &NativeSetHeatHaze;
    hooks.setFarClipDistance = &NativeSetFarClipDistance;
    hooks.setFogDistance = &NativeSetFogDistance;
    hooks.setSunSize = &NativeSetSunSize;
    hooks.setSunColor = &NativeSetSunColor;
    hooks.setSkyGradient = &NativeSetSkyGradient;
    hooks.setWaterColor = &NativeSetWaterColor;
    hooks.setCloudsEnabled = &NativeSetCloudsEnabled;
    hooks.setOcclusionsEnabled = &NativeSetOcclusionsEnabled;
    hooks.setGarageOpen = &NativeSetGarageOpen;
    hooks.isGarageOpen = &NativeIsGarageOpen;
    hooks.setCurrentArea = &NativeSetCurrentArea;
    hooks.fadeCamera = &NativeFadeCamera;
    hooks.shakeCamera = &NativeShakeCamera;
    hooks.setNearClipDistance = &NativeSetNearClipDistance;
    hooks.setCameraGoggleEffect = &NativeSetCameraGoggleEffect;
    hooks.setCameraDrunkLevel = &NativeSetCameraDrunkLevel;
    hooks.setColorFilter = &NativeSetColorFilter;
    hooks.getWaterLevel = &NativeGetWaterLevel;
    hooks.testLineAgainstWater = &NativeTestLineAgainstWater;
    hooks.setWorldWaterLevel = &NativeSetWorldWaterLevel;
    hooks.breakObject = &NativeBreakObject;
    hooks.setPlayerMoney = &NativeSetPlayerMoney;
    hooks.getPedBonePosition = &NativeGetPedBonePosition;
    hooks.getElementBoneMatrix = &NativeGetElementBoneMatrix;
    hooks.setElementBoneMatrix = &NativeSetElementBoneMatrix;
    hooks.updateElementRpHAnim = &NativeUpdateElementRpHAnim;
    hooks.getStatValue = &NativeGetStatValue;
    hooks.setStatValue = &NativeSetStatValue;
    hooks.getPedOxygenLevel = &NativeGetPedOxygenLevel;
    hooks.setPedOxygenLevel = &NativeSetPedOxygenLevel;
    hooks.setPedFightingStyle = &NativeSetPedFightingStyle;
    hooks.setPedWalkingStyle = &NativeSetPedWalkingStyle;
    hooks.setPedHeadless = &NativeSetPedHeadless;
    hooks.playPedVoiceLine = &NativePlayPedVoiceLine;
    hooks.setPedAnimation = &NativeSetPedAnimation;
    hooks.setPedAnimationProgress = &NativeSetPedAnimationProgress;
    hooks.setPedAnimationSpeed = &NativeSetPedAnimationSpeed;
    hooks.setEntityOnFire = &NativeSetEntityOnFire;
    hooks.setEntityVelocity = &NativeSetEntityVelocity;
    hooks.getModelBoundingBox = &NativeGetModelBoundingBox;
    hooks.getEntityComDistance = &NativeGetEntityComDistance;
    hooks.isEntityOnScreen = &NativeIsEntityOnScreen;
    hooks.getVehicleContactWheels = &NativeGetVehicleContactWheels;
    hooks.fixVehicle = &NativeFixVehicle;
    hooks.setWorldSpecialProperty = &NativeSetWorldSpecialProperty;
    hooks.setCameraViewMode = &NativeSetCameraViewMode;
    hooks.setCameraTargetEntity = &NativeSetCameraTargetEntity;
    hooks.setVehicleEngineState = &NativeSetVehicleEngineState;
    hooks.setVehicleOverrideLights = &NativeSetVehicleOverrideLights;
    hooks.setVehicleDoorLockMode = &NativeSetVehicleDoorLockMode;
    hooks.setVehicleSirensOn = &NativeSetVehicleSirensOn;
    hooks.setVehicleTaxiLightOn = &NativeSetVehicleTaxiLightOn;
    hooks.setVehiclePaintjob = &NativeSetVehiclePaintjob;
    hooks.setVehicleColor = &NativeSetVehicleColor;
    hooks.setVehiclePhysicalProofs = &NativeSetVehiclePhysicalProofs;
    hooks.setVehicleTyresCanBurst = &NativeSetVehicleTyresCanBurst;
    hooks.setVehicleDirtLevel = &NativeSetVehicleDirtLevel;
    hooks.setVehicleDoorOpenRatio = &NativeSetVehicleDoorOpenRatio;
    hooks.blowVehicle = &NativeBlowVehicle;
    hooks.burstVehicleTyre = &NativeBurstVehicleTyre;
    hooks.setVehicleUpgrade = &NativeSetVehicleUpgrade;
    hooks.setVehicleNitroState = &NativeSetVehicleNitroState;
    hooks.setTrainSpeed = &NativeSetTrainSpeed;
    hooks.setVehicleDoorState = &NativeSetVehicleDoorState;
    hooks.setVehicleLightState = &NativeSetVehicleLightState;
    hooks.setVehiclePanelState = &NativeSetVehiclePanelState;
    hooks.setVehicleWheelState = &NativeSetVehicleWheelState;
    hooks.setEntityAlpha = &NativeSetEntityAlpha;
    hooks.setEntityCollisionsEnabled = &NativeSetEntityCollisionsEnabled;
    hooks.setEntityPhysicalProofs = &NativeSetEntityPhysicalProofs;
    hooks.setCameraFov = &NativeSetCameraFov;
    hooks.getCameraFov = &NativeGetCameraFov;
    hooks.isPedStanding = &NativeIsPedStanding;
    hooks.isPedDucking = &NativeIsPedDucking;
    hooks.isPedFootBloodEnabled = &NativeIsPedFootBloodEnabled;
    hooks.setPedFootBloodEnabled = &NativeSetPedFootBloodEnabled;
    hooks.setPedBleeding = &NativeSetPedBleeding;
    hooks.setPedCanBeKnockedOffBike = &NativeSetPedCanBeKnockedOffBike;
    hooks.setPedMissionActor = &NativeSetPedMissionActor;
    hooks.setPedSuffersCriticalHits = &NativeSetPedSuffersCriticalHits;
    hooks.setPedCanBeDraggedOut = &NativeSetPedCanBeDraggedOut;
    hooks.setPedStayInSamePlace = &NativeSetPedStayInSamePlace;
    hooks.setPedNeverTargeted = &NativeSetPedNeverTargeted;
    hooks.getPedWeaponAccuracy = &NativeGetPedWeaponAccuracy;
    hooks.setPedWeaponAccuracy = &NativeSetPedWeaponAccuracy;
    hooks.getPedWeaponShootingRate = &NativeGetPedWeaponShootingRate;
    hooks.setPedWeaponShootingRate = &NativeSetPedWeaponShootingRate;
    hooks.isPedReloadingWeapon = &NativeIsPedReloadingWeapon;
    hooks.doesPedHaveJetPack = &NativeDoesPedHaveJetPack;
    hooks.getPedTaskChain = &NativeGetPedTaskChain;
    hooks.getPedSimplestTaskId = &NativeGetPedSimplestTaskId;
    hooks.setPedAimTarget = &NativeSetPedAimTarget;
    hooks.setPedLookAt = &NativeSetPedLookAt;
    hooks.loadMissionTextBlock = &NativeLoadMissionTextBlock;
    hooks.showMissionText = &NativeShowMissionText;
    hooks.showMissionHelp = &NativeShowMissionHelp;
    hooks.showMissionBigText = &NativeShowMissionBigText;
    hooks.clearMissionTexts = &NativeClearMissionTexts;
    hooks.clearMissionHelp = &NativeClearMissionHelp;
    hooks.setWeaponProperty = &NativeSetWeaponProperty;
    hooks.fxAddBlood = &NativeFxAddBlood;
    hooks.fxAddBulletImpact = &NativeFxAddBulletImpact;
    hooks.fxAddSparks = &NativeFxAddSparks;
    hooks.fxAddTyreBurst = &NativeFxAddTyreBurst;
    hooks.fxAddWood = &NativeFxAddWood;
    hooks.fxAddPunchImpact = &NativeFxAddPunchImpact;
    hooks.fxAddDebris = &NativeFxAddDebris;
    hooks.fxAddGlass = &NativeFxAddGlass;
    WorldNative::RegisterHooks(hooks);

    const int fxCount = (g_fxBlood ? 1 : 0) + (g_fxWood ? 1 : 0) + (g_fxBulletImpact ? 1 : 0) +
                        (g_fxSparks ? 1 : 0) + (g_fxTyreBurst ? 1 : 0) + (g_fxPunchImpact ? 1 : 0) +
                        (g_fxDebris ? 1 : 0) + (g_fxGlass ? 1 : 0);
    WORLD_HOOK_LOGI(
        "World native hooks installed: pools=%d/%d explosion=%d fire=%d light=%d loadScene=%d "
        "rope=%d sphere=%d worldRemove=%d garages=%d fx=%d/8",
        resolvedPools, Models::kPoolCount,
        g_addExplosion ? 1 : 0, (g_fireManager && g_startFire) ? 1 : 0,
        g_addPointLight ? 1 : 0, (g_loadScene || g_loadSceneCollision) ? 1 : 0,
        g_createSwatRope ? 1 : 0, g_testSphere ? 1 : 0,
        (g_worldRemove && g_worldAdd) ? 1 : 0, g_garages ? 1 : 0,
        g_fx ? fxCount : 0);
}

}  // namespace WorldRPG::Gta
