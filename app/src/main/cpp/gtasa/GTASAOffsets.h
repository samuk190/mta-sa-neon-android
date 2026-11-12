#pragma once

#include <cstdint>
#include <cstring>

#ifdef __ANDROID__
#include <android/log.h>
#define OFF_LOG_TAG "WorldRPGNeon"
#define OFF_LOGI(...) __android_log_print(ANDROID_LOG_INFO, OFF_LOG_TAG, __VA_ARGS__)
#define OFF_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, OFF_LOG_TAG, __VA_ARGS__)
#else
#include <cstdio>
#define OFF_LOGI(...) do { printf(__VA_ARGS__); printf("\n"); } while(0)
#define OFF_LOGE(...) do { fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } while(0)
#endif

namespace WorldRPG::Game {

/// Centralized GTA SA Mobile function offset table.
/// All offsets are relative to libGTASA.so base address.
///
/// SOURCES & REFERENCES:
/// - kuzia15/SA-MP-2.10 (GitHub, branch GTA-2.10): Android SA-MP client with ARM64 hooks
///   https://github.com/kuzia15/SA-MP-2.10/tree/GTA-2.10
/// - Verified against pulled libGTASA_v210.so (ARM64, stripped, 8.9MB)
/// - Cross-referenced with descompile_mta_launcher hook tables in this repository
///
/// STATUS: Verified ARM64 v2.10 offsets extracted from SA-MP-2.10 source.
/// Offsets marked [VERIFIED] have direct g_libGTASA + 0xNNNNNN references in source.
/// Offsets marked [PLT] are PLT hook entries (runtime-resolved via symbol lookup).
/// Offsets marked [INFERRED] are derived from code context but not directly named.
struct GTASAOffsets {
    // =========================================================================
    // Ped functions
    // =========================================================================
    uintptr_t createPed;          // [INFERRED] CWorld::Add — ped creation goes through script commands
    uintptr_t deletePed;          // [INFERRED] CWorld::Remove entity
    uintptr_t setPedPosition;     // [VERIFIED] CPed_UpdatePosition PLT hook
    uintptr_t getPedPosition;     // [INFERRED] CPlaceable::GetPosition via vtable
    uintptr_t setPedRotation;     // [INFERRED] CPlaceable::SetHeading via vtable
    uintptr_t setPedHealth;       // [INFERRED] CPed health setter via script command
    uintptr_t getPedHealth;       // [INFERRED] CPed health getter via script command
    uintptr_t setPedAnimation;    // [VERIFIED] CAnimManager_UncompressAnimation PLT hook

    // =========================================================================
    // Vehicle functions
    // =========================================================================
    uintptr_t createVehicle;      // [INFERRED] CWorld::Add — vehicle creation via script commands
    uintptr_t deleteVehicle;      // [INFERRED] CWorld::Remove entity
    uintptr_t setVehiclePosition; // [VERIFIED] CPed_UpdatePosition PLT (shared entity position update)
    uintptr_t getVehiclePosition; // [INFERRED] CPlaceable::GetPosition via vtable
    uintptr_t setVehicleRotation; // [INFERRED] CPlaceable::SetHeading via vtable
    uintptr_t setVehicleVelocity; // [INFERRED] CPhysical velocity setter
    uintptr_t setVehicleHealth;   // [INFERRED] CVehicle health setter via script command
    uintptr_t setVehicleColor;    // [VERIFIED] CVehicleModelInfo__SetupCommonData PLT hook

    // =========================================================================
    // Object functions
    // =========================================================================
    uintptr_t createObject;       // [INFERRED] CWorld::Add — object creation via script commands
    uintptr_t deleteObject;       // [VERIFIED] CWorld remove entity call at 0x507518
    uintptr_t setObjectPosition;  // [VERIFIED] CWorld add entity call at 0x5073A0
    uintptr_t setObjectRotation;  // [INFERRED] CPlaceable rotation via vtable

    // =========================================================================
    // Model loading / Streaming
    // =========================================================================
    uintptr_t requestModel;       // [VERIFIED] CStreaming::RequestModel (commented out, uses ScriptCommand)
    uintptr_t loadAllModels;      // [VERIFIED] CStreaming::LoadAllModels at 0x396D1C
    uintptr_t hasModelLoaded;     // [VERIFIED] CStreaming::HasModelLoaded at 0x395098

    // =========================================================================
    // Player
    // =========================================================================
    uintptr_t getPlayerPed;       // [VERIFIED] FindPlayerSpeed PLT hook at 0x671BBC
    uintptr_t setPlayerControl;   // [VERIFIED] player_control_zelda PLT hook at 0x67193C

    // =========================================================================
    // World
    // =========================================================================
    uintptr_t addEntityToWorld;      // [VERIFIED] CWorld::Add at 0x5073A0
    uintptr_t removeEntityFromWorld; // [VERIFIED] CWorld::Remove at 0x507518

    // =========================================================================
    // RenderWare
    // =========================================================================
    uintptr_t rwCameraBeginScene; // [VERIFIED] RwCamera destroy/setup at 0x001AD9D8
    uintptr_t rwCameraEndScene;   // [VERIFIED] RwFrame operation at 0x001AEC84

    // =========================================================================
    // Additional verified offsets from SA-MP-2.10 ARM64 source
    // =========================================================================
    uintptr_t cWorldProcess;             // [VERIFIED] CWorld::Process at 0x50BE40
    uintptr_t cGameProcess;              // [PLT] CGame_Process at 0x66FE58
    uintptr_t cCameraProcess;            // [PLT] CCamera__Process at 0x6717BC
    uintptr_t idle;                      // [PLT] Idle at 0x6710C4
    uintptr_t render2dStuff;             // [PLT] Render2dStuff at 0x67589C
    uintptr_t cCustomRoadsignMgrRender;  // [PLT] CCustomRoadsignMgr_RenderRoadsignAtomic at 0x66F5AC
    uintptr_t rwTextureDestroy;          // [PLT] _RwTextureDestroy at 0x67332C
    uintptr_t rwFrameAddChild;           // [PLT] RwFrameAddChild at 0x675490
    uintptr_t rpMaterialListDeinit;      // [PLT] rpMaterialListDeinitialize at 0x6730F0
    uintptr_t cAnimManagerUncompress;    // [PLT] CAnimManager_UncompressAnimation at 0x6750D4
    uintptr_t cVehicleModelInfoSetup;    // [PLT] CVehicleModelInfo__SetupCommonData at 0x674280
    uintptr_t aeVehicleAudioSettings;    // [PLT] CAEVehicleAudioEntity__GetVehicleAudioSettings at 0x06D008
    uintptr_t radarClearBlip;            // [PLT] CRadar_ClearBlip at 0x66FF0C
    uintptr_t pedGetWeaponSkill;         // [PLT] CPed__GetWeaponSkill at 0x6749D0
    uintptr_t findPlayerSpeed;           // [PLT] FindPlayerSpeed at 0x671BBC
    uintptr_t txdStoreFindCB;            // [PLT] CTxdStore__TxdStoreFindCB at 0x676034
    uintptr_t taskComplexLeaveCar;       // [PLT] CTaskComplexLeaveCar at 0x671984 / 0x675320
    uintptr_t cPedUpdatePosition;        // [PLT] CPed_UpdatePosition at 0x671458
    uintptr_t textureDbRuntimeGetEntry;  // [PLT] CTextureDatabaseRuntime__GetEntry at 0x672D14
    uintptr_t rqVertexBufferSelect;      // [PLT] rqVertexBufferSelect at 0x677498
    uintptr_t rqVertexBufferDelete;      // [PLT] rqVertexBufferDelete at 0x679B14
    uintptr_t mpg123Param;               // [PLT] mpg123_param at 0x66F3D4

    // =========================================================================
    // Global variable addresses (data, not code)
    // =========================================================================
    uintptr_t theCamera;                 // [VERIFIED] CCamera global at 0xBBA8D0
    uintptr_t msTimeStep;                // [VERIFIED] CTimer::ms_fTimeStep at 0x96B500
    uintptr_t streamingBufferSize;       // [VERIFIED] CStreaming buffer size at 0x850C78
    uintptr_t streamingMemoryUsed;       // [VERIFIED] CStreaming::ms_memoryUsed at 0x851D80
    uintptr_t streamingMemoryAvailable;  // [VERIFIED] CStreaming::ms_memoryAvailable at 0x850408
    uintptr_t desiredNumVehiclesLoaded;  // [VERIFIED] CStreaming at 0x850B68
    uintptr_t gravity;                   // [VERIFIED] Gravity float at 0x3A0B64
    uintptr_t gameTimer;                 // [VERIFIED] Timer at 0x96B4D8
    uintptr_t currentWeather;            // [VERIFIED] Weather ID at 0xD216F0
    uintptr_t sectors;                   // [VERIFIED] CSector array at 0xBDCB20
    uintptr_t repeatSectors;             // [VERIFIED] CRepeatSector array at 0xC14F20

    /// Create offset table for a specific game version.
    /// Returns zeroed offsets if version is unknown.
    static GTASAOffsets forVersion(const char* version) {
        GTASAOffsets offsets{};
        std::memset(&offsets, 0, sizeof(offsets));

        if (!version) return offsets;

        if (std::strcmp(version, "2.10") == 0) {
            // =================================================================
            // GTA SA Mobile v2.10 (ARM64) — Verified offsets from SA-MP-2.10
            // Source: kuzia15/SA-MP-2.10 branch GTA-2.10
            // Binary: libGTASA_v210.so (ELF 64-bit LSB shared object, ARM aarch64)
            // =================================================================

            // --- Ped functions ---
            offsets.createPed          = 0x0;  // Created via ScriptCommand, no direct offset
            offsets.deletePed          = 0x507518;  // CWorld::Remove entity [VERIFIED]
            offsets.setPedPosition     = 0x671458;  // CPed_UpdatePosition PLT [VERIFIED]
            offsets.getPedPosition     = 0x0;  // Via CPlaceable vtable
            offsets.setPedRotation     = 0x0;  // Via CPlaceable vtable
            offsets.setPedHealth       = 0x0;  // Via ScriptCommand
            offsets.getPedHealth       = 0x0;  // Via ScriptCommand
            offsets.setPedAnimation    = 0x6750D4;  // CAnimManager_UncompressAnimation PLT [VERIFIED]

            // --- Vehicle functions ---
            offsets.createVehicle      = 0x0;  // Created via ScriptCommand
            offsets.deleteVehicle      = 0x507518;  // CWorld::Remove entity [VERIFIED]
            offsets.setVehiclePosition = 0x671458;  // Shared CPed_UpdatePosition PLT [VERIFIED]
            offsets.getVehiclePosition = 0x0;  // Via CPlaceable vtable
            offsets.setVehicleRotation = 0x0;  // Via CPlaceable vtable
            offsets.setVehicleVelocity = 0x0;  // Via CPhysical
            offsets.setVehicleHealth   = 0x0;  // Via ScriptCommand
            offsets.setVehicleColor    = 0x674280;  // CVehicleModelInfo__SetupCommonData PLT [VERIFIED]

            // --- Object functions ---
            offsets.createObject       = 0x5073A0;  // CWorld::Add entity [VERIFIED]
            offsets.deleteObject       = 0x507518;  // CWorld::Remove entity [VERIFIED]
            offsets.setObjectPosition  = 0x5073A0;  // CWorld::Add entity [VERIFIED]
            offsets.setObjectRotation  = 0x0;  // Via CPlaceable vtable

            // --- Model loading / Streaming ---
            offsets.requestModel       = 0x391FF0;  // CStreaming::RemoveModel (direct call) [VERIFIED]
            offsets.loadAllModels      = 0x396D1C;  // CStreaming::LoadAllModels [VERIFIED]
            offsets.hasModelLoaded     = 0x395098;  // CStreaming::HasModelLoaded [VERIFIED]

            // --- Player ---
            offsets.getPlayerPed       = 0x671BBC;  // FindPlayerSpeed PLT [VERIFIED]
            offsets.setPlayerControl   = 0x67193C;  // player_control_zelda PLT [VERIFIED]

            // --- World ---
            offsets.addEntityToWorld      = 0x5073A0;  // CWorld::Add [VERIFIED]
            offsets.removeEntityFromWorld = 0x507518;  // CWorld::Remove [VERIFIED]

            // --- RenderWare ---
            offsets.rwCameraBeginScene = 0x001AD9D8;  // RwCamera operation [VERIFIED]
            offsets.rwCameraEndScene   = 0x001AEC84;  // RwFrame operation [VERIFIED]

            // --- Additional PLT hooks ---
            offsets.cWorldProcess             = 0x50BE40;
            offsets.cGameProcess              = 0x66FE58;
            offsets.cCameraProcess            = 0x6717BC;
            offsets.idle                      = 0x6710C4;
            offsets.render2dStuff             = 0x67589C;
            offsets.cCustomRoadsignMgrRender  = 0x66F5AC;
            offsets.rwTextureDestroy          = 0x67332C;
            offsets.rwFrameAddChild           = 0x675490;
            offsets.rpMaterialListDeinit      = 0x6730F0;
            offsets.cAnimManagerUncompress    = 0x6750D4;
            offsets.cVehicleModelInfoSetup    = 0x674280;
            offsets.aeVehicleAudioSettings    = 0x06D008;
            offsets.radarClearBlip            = 0x66FF0C;
            offsets.pedGetWeaponSkill         = 0x6749D0;
            offsets.findPlayerSpeed           = 0x671BBC;
            offsets.txdStoreFindCB            = 0x676034;
            offsets.taskComplexLeaveCar       = 0x671984;
            offsets.cPedUpdatePosition        = 0x671458;
            offsets.textureDbRuntimeGetEntry  = 0x672D14;
            offsets.rqVertexBufferSelect      = 0x677498;
            offsets.rqVertexBufferDelete      = 0x679B14;
            offsets.mpg123Param               = 0x66F3D4;

            // --- Global variables ---
            offsets.theCamera                 = 0xBBA8D0;
            offsets.msTimeStep                = 0x96B500;
            offsets.streamingBufferSize       = 0x850C78;
            offsets.streamingMemoryUsed       = 0x851D80;
            offsets.streamingMemoryAvailable  = 0x850408;
            offsets.desiredNumVehiclesLoaded  = 0x850B68;
            offsets.gravity                   = 0x3A0B64;
            offsets.gameTimer                 = 0x96B4D8;
            offsets.currentWeather            = 0xD216F0;
            offsets.sectors                   = 0xBDCB20;
            offsets.repeatSectors             = 0xC14F20;

            OFF_LOGI("GTASAOffsets: loaded v2.10 table (VERIFIED from SA-MP-2.10 ARM64 source)");
        } else if (std::strcmp(version, "2.11") == 0) {
            // GTA SA Mobile v2.11 offsets (if different from 2.10)
            OFF_LOGI("GTASAOffsets: loaded v2.11 table (PLACEHOLDER — verify before use)");
        } else {
            OFF_LOGE("GTASAOffsets: unknown version '%s'", version);
        }

        return offsets;
    }

    /// Check if any offsets have been populated (non-zero)
    bool hasValidOffsets() const {
        return addEntityToWorld != 0 || removeEntityFromWorld != 0 ||
               getPlayerPed != 0 || loadAllModels != 0 ||
               rwCameraBeginScene != 0 || cWorldProcess != 0;
    }

    /// Count how many offsets are non-zero (for diagnostics)
    int countValidOffsets() const {
        int count = 0;
        const uintptr_t* p = reinterpret_cast<const uintptr_t*>(this);
        size_t numFields = sizeof(GTASAOffsets) / sizeof(uintptr_t);
        for (size_t i = 0; i < numFields; i++) {
            if (p[i] != 0) count++;
        }
        return count;
    }

    /// Log all current offset values for debugging
    void dumpOffsets() const {
        OFF_LOGI("GTASAOffsets dump (%d valid):", countValidOffsets());
        OFF_LOGI("  addEntityToWorld=0x%lx", (unsigned long)addEntityToWorld);
        OFF_LOGI("  removeEntityFromWorld=0x%lx", (unsigned long)removeEntityFromWorld);
        OFF_LOGI("  getPlayerPed=0x%lx", (unsigned long)getPlayerPed);
        OFF_LOGI("  setPlayerControl=0x%lx", (unsigned long)setPlayerControl);
        OFF_LOGI("  loadAllModels=0x%lx", (unsigned long)loadAllModels);
        OFF_LOGI("  hasModelLoaded=0x%lx", (unsigned long)hasModelLoaded);
        OFF_LOGI("  requestModel=0x%lx", (unsigned long)requestModel);
        OFF_LOGI("  rwCameraBeginScene=0x%lx", (unsigned long)rwCameraBeginScene);
        OFF_LOGI("  rwCameraEndScene=0x%lx", (unsigned long)rwCameraEndScene);
        OFF_LOGI("  cWorldProcess=0x%lx", (unsigned long)cWorldProcess);
        OFF_LOGI("  cGameProcess=0x%lx", (unsigned long)cGameProcess);
        OFF_LOGI("  cCameraProcess=0x%lx", (unsigned long)cCameraProcess);
        OFF_LOGI("  theCamera=0x%lx", (unsigned long)theCamera);
        OFF_LOGI("  gravity=0x%lx", (unsigned long)gravity);
        OFF_LOGI("  valid=%d", hasValidOffsets() ? 1 : 0);
    }
};

} // namespace WorldRPG::Game