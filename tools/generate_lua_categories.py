#!/usr/bin/env python3
"""
Generates detailed markdown files per category for ALL 1,581 desktop MTA Lua functions,
documenting their implementation status, desktop reference, and conversion requirements.
"""

import pathlib
import sys
from collections import OrderedDict

# Import parity_matrix extraction logic
sys.path.insert(0, str(pathlib.Path(__file__).parent))
import parity_matrix

client_root = pathlib.Path(parity_matrix.DEFAULT_DESKTOP)
lua_rows, lua_classes, lua_order, lua_unreached = parity_matrix.desktop_lua_functions(client_root)
helpers, fn_bodies = parity_matrix.port_stub_helpers(parity_matrix.PORT_SHARED)
eff = parity_matrix.port_lua_bindings(parity_matrix.PORT_SHARED, helpers, fn_bodies)

CATEGORIES = OrderedDict([
    ("VEHICLES", {
        "title": "Vehicles (CLuaVehicleDefs)",
        "file": "VEHICLES.md",
        "description": "Vehicle physics, handling, sirens, doors, colors, lights, engine state, trailers, nitro, gears, and occupant seats.",
        "groups": ["CLuaVehicleDefs"],
        "subsystem_notes": "Requires hooking libGTASA.so `CVehicle` vtable / offsets (`0x3A0000+` in ARM64), handling structures (`tHandlingData`), and vehicle damage packets (33, 56, 58)."
    }),
    ("PEDS_AND_PLAYERS", {
        "title": "Peds & Players (CLuaPedDefs, CLuaPlayerDefs)",
        "file": "PEDS_AND_PLAYERS.md",
        "description": "Ped animations, tasks, fighting styles, bone transforms, clothes, stats, voice, wanted levels, nametags, and player targeting.",
        "groups": ["CLuaPedDefs", "CLuaPlayerDefs"],
        "subsystem_notes": "Requires hooking libGTASA.so `CPed` / `CPlayerPed` (`_ZN10CPlayerPed...`), `CPedIK`, `CTask` hierarchy (wander, fight, duck), and clothing pools."
    }),
    ("WORLD_AND_OBJECTS", {
        "title": "World, Objects & Environment",
        "file": "WORLD_AND_OBJECTS.md",
        "description": "3D objects, world weather, game time, water levels, gravity, minute duration, heat haze, explosions, fires, garage doors, searchlights, and foliage.",
        "groups": ["CLuaWorldDefs", "CLuaObjectDefs", "CLuaBuildingDefs", "CLuaWaterDefs", "CLuaFoliageDefs", "CLuaFireDefs", "CLuaEffectDefs"],
        "subsystem_notes": "Weather, clock, and clip distances are stored. Objects, water, fire, effects, zones, buildings, and sight queries follow the desktop argument checks. Ambient population calls return the desktop failure while zone lookup, popcycle, model info, and the camera are not hooked. isWorldRPGMobilePopulation keeps the phone polyfills until those hooks succeed."
    }),
    ("ELEMENTS", {
        "title": "Element Tree & Data (CLuaElementDefs)",
        "file": "ELEMENTS.md",
        "description": "Core element hierarchy (parent/child), element data (synced/local), collisions, dimensions, alpha, positions, velocities, matrix, and spatial streaming.",
        "groups": ["CLuaElementDefs"],
        "subsystem_notes": "Implemented via `LuaElementApi.cpp`, `LuaElementTree.cpp` and `ServerWorld.cpp`. Attachment queries read ENTITY_ADD metadata. Marker collisions are created with the marker. Velocity, angular velocity, matrix, attachment offsets, low-LOD links and streaming-lease tokens are stored. Model bounding boxes, on-screen tests, ground-wait flags, native fire and the streamer force-in are not read from the game. Physical attachment and attachment RPCs remain pending."
    }),
    ("GUI_AND_DX", {
        "title": "GUI & 2D/3D Drawing (DX & CEGUI)",
        "file": "GUI_AND_DX.md",
        "description": "2D screen drawing, fonts, text, rectangles, lines, textures, render targets, GUI windows, buttons, edits, labels, gridlists, and 3D line drawing.",
        "groups": ["CLuaGUIDefs", "CLuaDrawingDefs", "CLuaVectorGraphicDefs", "CLua2DFXDefs"],
        "subsystem_notes": "Desktop uses CEGUI and Direct3D9. Mobile uses `MtaDxView.java` (Android Canvas) + `LuaGuiApi.cpp`. Resource GUI fonts load through Android Typeface, are distinct from DX fonts, and detach from widgets on destruction/resource stop. GUI labels honor alignment, wrapping, clipping and logical lines; label metrics use the assigned font. Built-in GUI font names use Android system faces, so their appearance and CEGUI skin geometry still differ. Edit/memo input and caret count Unicode characters; JNI converts UTF-16/UTF-8. Memo Enter inserts a newline; logical lines render separately. The eight 2D draw calls honor postGUI, capture blend/font state and retain render-target order. DX text supports alignment, clipping, wrapping, independent numeric scales and rotation with a pivot; Canvas font metrics still differ from Direct3D. Primitive lines interpolate vertex colors. All 199 names now have nonconstant handlers; this is binding coverage, not full compatibility. Native 3D is limited to postfx, model 2DFX mutation to light/sun-glare, and shaders to a supported single-pass DX image subset using isolated EGL/GLES and bitmap readback. Grid sorting, selection, memo wrapping/automatic caret scrolling and the documented native/shader gaps remain pending. Host tests and Android compilation do not verify device rendering."
    }),
    ("ENGINE_AND_MODELS", {
        "title": "Engine & Model Replacement (DFF, TXD, COL)",
        "file": "ENGINE_AND_MODELS.md",
        "description": "Custom DFF mesh loading, TXD texture dictionaries, COL collision models, model handling overrides, texture filtering, and breakable objects.",
        "groups": ["CLuaEngineDefs", "CLuaPostfxDefs", "CLuaBreakEffectDefs"],
        "subsystem_notes": "DFF and TXD files use the Android RenderWare readers. Applying and restoring models or textures require a streamer integration that handles model types and asset lifetimes."
    }),
    ("AUDIO_AND_VOICE", {
        "title": "Audio & Voice System (CLuaAudioDefs)",
        "file": "AUDIO_AND_VOICE.md",
        "description": "2D audio playback, 3D spatial sounds, internet radio streams, sound effects, volume/pitch/pan, sound tags, and desktop Speex voice chat.",
        "groups": ["CLuaAudioDefs"],
        "subsystem_notes": "Desktop uses BASS for sound and Speex for voice. Android plays resource files and HTTP(S) streams with MediaPlayer. Spatial mixing, GTA sound banks, mission audio, and voice capture/playback require additional engine integration."
    }),
    ("CAMERA", {
        "title": "Camera & Viewport (CLuaCameraDefs)",
        "file": "CAMERA.md",
        "description": "Camera matrix (pos/lookAt), camera target, field of view (FOV), screen shake, fade transitions, clipping planes, and interior viewing.",
        "groups": ["CLuaCameraDefs"],
        "subsystem_notes": "Requires hooking libGTASA.so `TheCamera` (`CCamera`) at `0x71...` in ARM64, and matrix interpolation routines."
    }),
    ("WEAPONS_AND_COMBAT", {
        "title": "Weapons & Projectiles (CLuaWeaponDefs, CLuaProjectileDefs)",
        "file": "WEAPONS_AND_COMBAT.md",
        "description": "Custom weapons, weapon damage/range/accuracy stats, projectiles (rockets, grenades), bullet traces, and weapon firing flags.",
        "groups": ["CLuaWeaponDefs", "CLuaProjectileDefs"],
        "subsystem_notes": "Requires hooking `CWeaponInfo`, `CProjectileInfo`, bullet trace pool (`CBulletTraces`), and bullet sync packet (98)."
    }),
    ("CORE_AND_UTILITY", {
        "title": "Core System, Networking & Utilities",
        "file": "CORE_AND_UTILITY.md",
        "description": "Timers, event handling, network latent transfers, file I/O, XML parsing, JSON serialization, UTF strings, and cryptography.",
        "groups": [
            "CLuaManager (LoadCFunctions table)", "CLuaUtilDefs", "CLuaMain (lua_register)",
            "CLuaTimerDefs", "CLuaResourceDefs", "CLuaNetworkDefs", "CLuaFileDefs",
            "CLuaXMLDefs", "CLuaBitDefs", "CLuaCryptDefs", "CLuaUTFDefs", "CLuaPathDefs",
            "CLuaClientDefs"
        ],
        "subsystem_notes": "Pure CPU/platform logic. Mostly ported or needs standard library wrappers (e.g. tinyxml2, sha256, zlib)."
    }),
    ("MARKERS_AND_SPATIAL", {
        "title": "Markers, Blips, ColShapes & Spatial",
        "file": "MARKERS_AND_SPATIAL.md",
        "description": "Checkpoints, radar map blips, collision shapes (spheres, cubes, polygons), radar areas, pickups, and 3D searchlights.",
        "groups": [
            "CLuaColShapeDefs", "CLuaMarkerDefs", "CLuaBlipDefs", "CLuaRadarAreaDefs",
            "CLuaPickupDefs", "CLuaSearchLightDefs", "CLuaPointLightDefs", "CLuaRopeDefs",
            "CLuaBirdDefs", "CLuaNativeUIDefs", "CLuaTeamDefs"
        ],
        "subsystem_notes": "Requires spatial overlap checks (AABB / SAT collision algorithms) and radar texture rendering hooks."
    }),
    ("BROWSER_AND_COMPATIBILITY", {
        "title": "Web Browser & Compatibility (CEF / Discord)",
        "file": "BROWSER_AND_COMPATIBILITY.md",
        "description": "Embedded Chromium web pages, browser mouse/keyboard input, Discord Rich Presence, and backwards compatibility helpers.",
        "groups": ["CLuaBrowserDefs", "CLuaCompatibilityDefs", "CLuaDiscordDefs"],
        "subsystem_notes": "Desktop bundles CEF. Android has no web core: createBrowser and guiCreateBrowser still enforce the size errors, getBrowserSettings returns the three flags as false, and page calls return false. Discord checks lengths then returns false. Compatibility names share the modern handlers. Weapon slot, knock-off-bike and control state are stored. Ground height uses FindGroundZFor3DPosition when it returns. Task, target and line-of-sight queries return false because those engine calls are not hooked. setPedRotation applies to a script ped. attachElements and detachElements return false and do not change the server attachment snapshot."
    }),
])

out_dir = pathlib.Path("docs/lua-functions")
out_dir.mkdir(parents=True, exist_ok=True)

# Process all functions
func_data = {}
for group, name, file, line, handler in lua_rows:
    status, detail = parity_matrix.classify_lua(name, eff, helpers)
    func_data[name] = {
        "group": group,
        "name": name,
        "status": status,
        "detail": detail,
        "file": file,
        "line": line,
        "handler": handler,
    }

def format_status(status):
    if status == "implemented":
        return "**Implemented**"
    elif status == "stub":
        return "*Stub*"
    else:
        return "Missing"

def guess_conversion_notes(name, group, status, detail):
    if detail == "l_unsafe":
        return "Disabled in the client. Returns false."
    if name in {"load", "loadstring"}:
        return "Compiles Lua source text. Binary chunks are rejected."
    if name == "call":
        return "Resolves the function in the target resource environment, requires a declared export, and runs it with the target owner and caller source fields."
    if detail.startswith("AudioBackendUnavailable"):
        return "Returns false. Requires integration with GTA sound banks or mission audio."
    if name == "getSFXStatus":
        return "Returns false for recognized banks and nil for unknown banks. GTA sound-bank playback is unavailable."
    if name == "playSound":
        return "Plays HTTP(S) streams and resource files through MediaPlayer. Completion releases the sound and fires onClientSoundStopped."
    if name == "playSound3D":
        return "Uses MediaPlayer for 2D playback and stores the requested 3D coordinates. Listener attenuation and spatial mixing are pending."
    if group == "CLuaAudioDefs" and name in {
        "getSoundBPM", "getSoundLevelData", "getSoundFFTData", "getSoundWaveData", "getSoundMetaTags",
        "getSoundBufferLength", "getSoundProperties", "setSoundProperties",
        "isShowSoundEnabled", "showSound", "getSoundEffects", "setSoundEffectEnabled",
        "getSoundEffectParameters", "setSoundEffectParameter", "setSoundSpeed", "getSoundSpeed",
        "setSoundMinDistance", "getSoundMinDistance", "setSoundMaxDistance", "getSoundMaxDistance",
        "setSoundPanningEnabled", "isSoundPanningEnabled",
    }:
        return "Uses client-side state or placeholder values. MediaPlayer does not apply these settings or provide these measurements."
    if name == "playSoundFrontEnd":
        return "Valid IDs are accepted for script compatibility. GTA frontend sound playback is pending."
    if group == "CLuaAudioDefs" and ("WorldSound" in name or "AmbientSound" in name or name in {"setRadioChannel", "getRadioChannel"}):
        return "Uses client-side state. The GTA audio engine is not updated."
    if name in {"engineLoadDFF", "engineLoadTXD"}:
        return "Reads a resource-relative file through Android RenderWare and releases the native asset when Lua collects its handle. Raw data and device verification remain pending."
    if name in {"engineReplaceModel", "engineImportTXD"}:
        return "Returns false until the Android streamer supports applying and restoring the asset for the target model type."
    if status == "implemented" and name in {
        "isVehicleLocked", "setVehicleLocked", "getVehicleDoorLockMode", "setVehicleDoorLockMode",
        "getVehicleEngineState", "setVehicleEngineState", "getVehicleOverrideLights", "setVehicleOverrideLights",
        "getVehiclePaintjob", "setVehiclePaintjob", "getVehiclePlateText", "setVehiclePlateText",
        "isVehicleDamageProof", "setVehicleDamageProof", "isVehicleFuelTankExplodable", "setVehicleFuelTankExplodable",
        "isVehicleFrozen", "setVehicleFrozen", "getVehicleLandingGearDown", "setVehicleLandingGearDown",
        "getVehicleSirensOn", "setVehicleSirensOn", "isVehicleTaxiLightOn", "setVehicleTaxiLightOn",
        "getVehicleTyresCanBurst", "setVehicleTyresCanBurst", "setVehicleDirtLevel",
        "getVehicleDoorState", "setVehicleDoorState", "getVehicleLightState", "setVehicleLightState",
        "getVehiclePanelState", "setVehiclePanelState", "getVehicleWheelStates", "setVehicleWheelStates",
        "isVehicleWheelOnGround", "getVehicleWheelFrictionState", "isVehicleOnGround", "isVehicleOnAllWheels",
        "fixVehicle", "isVehicleBlown", "blowVehicle", "areVehicleLightsOn", "getVehicleCurrentGear",
        "getVehicleTurnVelocity", "setVehicleTurnVelocity", "getVehicleGravity", "setVehicleGravity",
        "getVehicleHeadLightColor", "setVehicleHeadLightColor", "getHelicopterRotorSpeed",
        "isTrainDerailed", "setTrainDerailed", "isTrainDerailable", "setTrainDerailable",
        "getTrainDirection", "setTrainDirection", "isTrainChainEngine", "getTrainSpeed", "setTrainSpeed",
        "getTrainPosition", "setTrainPosition", "getVehicleColor", "setVehicleColor",
        "getVehicleVariant", "setVehicleVariant", "getVehicleWheelScale", "setVehicleWheelScale",
        "isVehicleSmokeTrailEnabled", "setVehicleSmokeTrailEnabled",
        "isVehicleNitroRecharging", "isVehicleNitroActivated", "getVehicleNitroCount", "getVehicleNitroLevel",
        "setVehicleNitroActivated", "setVehicleNitroCount", "setVehicleNitroLevel",
        "getVehicleAdjustableProperty", "setVehicleAdjustableProperty", "getVehicleTurretPosition", "setVehicleTurretPosition",
        "getVehicleDoorOpenRatio", "setVehicleDoorOpenRatio", "getHeliBladeCollisionsEnabled", "setHeliBladeCollisionsEnabled",
        "setHelicopterRotorSpeed", "getVehicleRotorSpeed", "setVehicleRotorSpeed", "isVehicleWindowOpen", "setVehicleWindowOpen",
        "isVehicleParkingBrakeEnabled", "setVehicleParkingBrake", "setVehicleDoorsUndamageable", "setVehicleLoadCollisionFlag",
        "setVehiclePhysicalProofs", "setVehicleWheelsRotation", "getVehicleStraightLineDistance", "setVehicleStraightLineDistance",
        "getVehicleTowedByVehicle", "getVehicleTowingVehicle", "attachTrailerToVehicle", "detachTrailerFromVehicle",
        "getVehicleUpgradeOnSlot", "getVehicleUpgrades", "addVehicleUpgrade", "removeVehicleUpgrade",
        "getVehicleComponents", "getVehicleComponentPosition", "setVehicleComponentPosition",
        "getVehicleComponentRotation", "setVehicleComponentRotation", "getVehicleComponentScale", "setVehicleComponentScale",
        "getVehicleComponentVisible", "setVehicleComponentVisible", "resetVehicleComponentPosition",
        "resetVehicleComponentRotation", "resetVehicleComponentScale", "createVehicle",
        "requestVehicleRecording", "loadVehicleRecording", "isVehicleRecordingLoaded", "isVehiclePlaybackActive",
        "startVehiclePlayback", "stopVehiclePlayback", "setVehiclePlaybackSpeed",
        "addVehicleSirens", "removeVehicleSirens", "getVehicleSirenParams", "getVehicleSirens", "setVehicleSirens",
        "getVehicleRotorState", "setVehicleRotorState", "getOriginalHandling", "getVehicleHandling", "setVehicleHandling",
        "getVehicleModelAudioSettings", "getVehicleAudioSettings", "setVehicleModelAudioSetting", "setVehicleAudioSetting",
        "resetVehicleAudioSettings", "resetVehicleModelAudioSettings", "getVehicleModelDummyPosition", "setVehicleModelDummyPosition",
        "getVehicleModelDummyDefaultPosition", "getVehicleDummyPosition", "setVehicleDummyPosition", "resetVehicleDummyPositions",
        "getVehicleModelExhaustFumesPosition", "setVehicleModelExhaustFumesPosition", "getVehicleModelWheelSize", "setVehicleModelWheelSize",
        "getVehicleEntryPoints", "getVehicleCompatibleUpgrades", "getVehicleNativeAutoPilotDiagnostic", "spawnVehicleFlyingComponent",
    }:
        return "Stored on the client vehicle element with the desktop defaults. Door, train, gear, rotor, and nitro checks follow the desktop model rules. Random variant selection and the GTA CVehicle body are not hooked. Not device-verified."
    if status == "implemented" and name in {"isPedDead", "getPedWeapon", "getPedWeaponSlot", "getPedTotalAmmo", "getPedAmmoInClip"}:
        return "Game-thread GTA ped snapshot plus per-element client cache seeded by ENTITY_ADD/PLAYER_LIST and weapon RPCs. Queries preserve death tasks/network revival, optional weapon slot and uint16 ammo semantics; clip ammo requires a streamed body. Stream-out saves inventory and offscreen RPCs update the cache before body restoration; generation guards reject removed/reused IDs. ARM64 layout is instruction-guarded. Native reload, sync-driven weapons and streaming still need device validation; this is handler coverage, not full gameplay parity."
    if status == "implemented" and name in {"toggleControl", "isControlEnabled"}:
        return "Stores per-control enabled state; only the radar control currently changes Android behavior. Other GTA control actions are not wired by this handler. Registration coverage does not mean complete native control compatibility."
    if status == "implemented" and group == "CLua2DFXDefs":
        return "Partial native 2DFX port: per-resource property/removal stacks, custom-index compaction, transactional commits and deferred cleanup. Verified ARM64 getter/count bridge mutates LIGHT and SUN_GLARE only. Particle/escalator data can be queried; roadsign payload is unavailable. Particle/roadsign/escalator mutations need persistent-body cleanup/restream integration and are rejected. Host state/Lua checks pass; native execution not device-verified."
    if status == "implemented" and name in {"dxDrawLine3D", "dxDrawMaterialLine3D", "dxDrawMaterialSectionLine3D", "dxDrawPrimitive3D", "dxDrawMaterialPrimitive3D", "dxDrawWiredSphere", "dxDrawModel3D"}:
        return "Partial native RenderWare port: immutable resource-owned batches at the main camera end callback, scene depth test, colored/textured primitives, billboard lines and octahedron wire spheres. Loaded atomic/clump models use native instances. Only postfx is accepted; prefx/postgui and nonzero model lighting are rejected. Shader materials and desktop DRAW_LAST sorting are unavailable. Host geometry/queue checks and ARM64 build pass; native visual/runtime validation remains pending."
    if status == "implemented" and name in {"dxCreateShader", "dxSetShaderValue", "dxSetShaderTessellation", "dxSetShaderTransform"}:
        return "Partial DX image shader port: pinned MIT HLSL parser translates a supported single-pass FX subset to GLSL ES 100; an isolated EGL/GLES context compiles, links and renders pixels for dxDrawImage/Section. Numeric/bool/texture uniforms, a CPU tessellated quad (1..64 per axis), rotation/perspective matrices and ordered bitmap release are wired. Unsupported includes/conditional preprocessing, pass/sampler states, multipass and world/3D shader application are rejected. Image shader draws reject overwrite until fragment coverage masking is available. Integer precision matches across stages; common matrices follow custom parameters. Texture values are pixel snapshots; readback/copy performance and device rendering remain unverified. Host Mesa pixel tests pass."
    if status == "implemented" and name == "guiCreateFont":
        return "Creates a resource-owned gui-font from a resolved TTF/OTF/TTC file, including cross-resource paths. Android validates/preloads the Typeface; host checks only the sfnt signature. Size follows unsigned conversion and the desktop 5-150 point clamp at 96 DPI. Live GUI faces remain retained beyond LRU pressure; an ordered D command releases them after earlier draws on destruction/stop/reset. Host/Java lifecycle tests pass; not device-verified."
    if status == "implemented" and name in {"guiSetFont", "guiGetFont"}:
        return "Accepts a live GUI font element or a present GUI font name; DX fonts and unknown names are rejected without changing the widget. guiGetFont returns name and the explicitly attached font element (nil for a named assignment). Destruction/resource stop restores default-normal for users in other resources too. Built-in GUI names use Android system faces; CEGUI appearance/layout is not identical. Host/Java checks pass; not device-verified."
    if status == "implemented" and name == "guiLabelGetFontHeight":
        return "Returns assigned-font Android line spacing in pixels as an approximation. CEGUI returns cached glyph height, which can differ from baseline spacing; exact height parity remains pending. Host builds estimate metrics without Android. Host/Java checks pass; not device-verified."
    if status == "implemented" and name == "guiLabelGetTextExtent":
        return "Measures the longest logical line in pixels using the same Typeface source/size as the view. Host builds estimate metrics without Android. Host/Java checks pass; real glyph metrics require device validation."
    if status == "implemented" and name in {"guiLabelSetHorizontalAlign", "guiLabelSetVerticalAlign", "guiCreateLabel"}:
        return "GUI label layout honors horizontal/vertical alignment, optional word wrapping and clipping. Escaped literal text preserves newlines, backslashes and Unicode; labels have no three-line limit. The assigned GUI font is captured/reset around each widget. Canvas line breaking differs from CEGUI. Host/Java checks pass; not device-verified."
    if status == "implemented" and name in {"dxGetFontHeight", "dxGetTextWidth", "dxGetTextSize"}:
        return "DX metrics resolve the versioned native DX font source; a live GUI font is rejected instead of silently measuring the default. Height/width return false for that wrong type; dxGetTextSize raises its typed argument error before allocating C++ strings, even for empty text. Unknown/destroyed font fallback remains as before. Host type/lifecycle tests pass; real glyph metrics still need device validation."
    if status == "implemented" and name == "dxCreateFont":
        return "Custom fonts use a native instance identity in both the Typeface cache and measured-width cache; recreating a font at the same path reloads it. Java Typeface cache has an LRU bound. Host lifecycle checks pass; Android compiles; glyph metrics still need device validation."
    if status == "stub" and name in {"attachElements", "detachElements"}:
        return "Returns false until native physical attachment and its lifecycle/events are ported. Server attachment queries are metadata-only; these calls do not mutate that snapshot."
    if status == "implemented" and name in {"isElementAttached", "getElementAttachedTo", "getAttachedElements", "getElementAttachedOffsets"}:
        return "Reads server ENTITY_ADD attachment metadata, including forward references in the batch and offsets in degrees. Removal clears references before ID reuse. Physical attachment, local attach/detach and later attachment RPCs remain pending. Host-tested; not device-verified."
    if status == "implemented" and name in {"guiEditSetCaretIndex", "guiEditGetCaretIndex", "guiEditSetMaxLength", "guiEditGetMaxLength", "guiSetText"}:
        return "Edit/memo caret uses Unicode characters; insertion/backspace respect the caret and read-only flag. An explicit edit maximum of 0 accepts no text. Memo text preserves CEGUI's terminal newline. Selection editing remains pending. Host-tested; not device-verified."
    if status == "implemented" and name in {"guiCreateMemo", "guiMemoSetCaretIndex", "guiMemoGetCaretIndex", "guiMemoSetReadOnly", "guiMemoIsReadOnly"}:
        return "Memo accepts Unicode text, backspace and Enter without onClientGUIAccepted; emits onClientGUIChanged, participates in focus and requests multiline Android input. Terminal newline/caret follow CEGUI. Selection, wrapping and automatic caret scrolling remain pending. Host-tested; Android compiles; not device-verified."
    if status == "implemented" and name == "dxDrawText":
        return "Canvas text honors alignment, clip/wordBreak, two numeric scales, subpixel position, rotation/pivot and positive custom line height for color-coded text. Coded block height counts every logical line and each baseline advances by lineHeight, including the final line's occupied height. Escaped payload preserves line breaks and backslashes. Color-coded text ignores clip/wordBreak as desktop does. Font/blend state stays with queued text. Canvas metrics/wrapping differ from Direct3D; nonpositive scales draw nothing; layout width is capped at 32768 pixels. Host/Java regression checks pass; not device-verified."
    if status == "implemented" and name in {"dxDrawImage", "dxDrawImageSection"}:
        return "Canvas 2D calls honor postGUI and retain target/blend order. Accepts texture elements, resource paths and the supported EGL image-shader subset. Shader output is baked at draw time with captured parameter values and the active viewport; temporary bitmaps are released in order. Shader overwrite is rejected to avoid clearing uncovered target pixels. Full-image GPU readback is a transitional path with device performance unverified. Host Mesa shader pixels pass; Android view rendering remains unverified."
    if status == "implemented" and name == "dxDrawRectangle":
        return "Canvas 2D calls return true/false, honor postGUI and capture blend state. Render-target draws keep their original order. Existing texture elements work outside a resource owner; file paths still require one. Finite numeric arguments required. Host-tested; not device-verified."
    if status == "implemented" and name in {"dxDrawLine", "dxDrawCircle", "dxDrawPrimitive", "dxDrawMaterialPrimitive"}:
        return "2D Canvas bridge honors postGUI on screen and retains render-target order; queued calls capture blend mode. Primitive lines interpolate endpoint colors. Nonfinite/out-of-float-range input is rejected. Host-tested; Android compiles; device rendering not verified."
    if status == "implemented" and name in {"createProjectile", "getProjectileType", "getProjectileCreator", "getProjectileTarget", "getProjectileForce", "getProjectileCounter", "setProjectileCounter"}:
        return "Native local-projectile bridge for verified GTA 2.10 arm64. Rotation/model overrides, satchel attachment, creation event and packet sync remain unported. Host guards tested; Android compiles; physical runtime not device-tested."
    if status == "implemented" and name in {"getWeaponAmmo", "getWeaponClipAmmo", "getWeaponState", "getWeaponOwner", "getWeaponTarget"}:
        return f"Queries the last server ENTITY_ADD weapon snapshot; native firing and later weapon RPC updates are not ported. Host-tested, not device-verified. ({detail})"
    if status == "implemented" and name == "getSoundLength":
        return "2D MediaPlayer duration in seconds, reported asynchronously; 0 while unknown. Player voice overload is not ported. Host-tested bridge; not device-verified."
    if status == "implemented" and name == "getRadioChannelName":
        return "Desktop station-name lookup (IDs 0..12); does not tune the GTA radio. Host-tested."
    if status == "implemented" and name in {"setSoundPan", "getSoundPan"}:
        return "2D sound pan is applied to MediaPlayer left/right gains. Player voice overload is not ported. Android compiles; not device-verified."
    if status == "implemented" and ("BreakEffect" in name or name.endswith("BreakProfile") or name.endswith("BreakHealth")):
        return "The break-effect cache starts empty. Calls that need an object or break-effect element return false, matching a missing element. Fracturing a streamed mesh is not implemented. Not device-verified."
    if status == "implemented" and name in {"engineLoadVehicleAudioConfig", "engineReloadVehicleAudioConfig", "engineUnloadVehicleAudioConfig", "enginePlayVehicleAudioBackfire"}:
        return "An empty vehicle-audio path returns false. A non-empty path fails closed because CVehicleSoundManager is not hooked. A successful load would also return ready. Backfire needs a vehicle element. Not device-verified."
    if status == "implemented" and name in {"engineGetCullZones", "engineCreateCullZone", "engineSetCullZone", "engineSetCullZoneEnabled", "engineRemoveCullZone", "engineRestoreCullZone"}:
        return "Script cull zones follow CCullZonesSA. A missing resource returns false. Non-positive size and mirror normals outside -1..1 are rejected. The GTA CULL IPL catalog is not loaded. Not device-verified."
    if status == "implemented" and name in {"engineGetRendererStats", "engineResetRendererStats", "engineSetDistantLightsEnabled", "engineSetDistantLightsDrawDistance", "engineSetDistantLightsAutomaticDrawDistance", "engineRebuildDistantLights", "engineGetDistantLightStats", "engineSetRadarMapTile", "engineResetRadarMapTile", "engineGetRadarMapStats"}:
        return "Distant-light distance is kept from 300 through 5000 and starts at 2000. Renderer capacities are 8192, 8192, and 30000. Radar tiles outside 0..39 return false, and the tile registry is absent. Not device-verified."
    if status == "implemented" and name in {"getPostFXValue", "getPostFXMode", "isPostFXEnabled", "engineGetObjectStreamingLimits", "engineSetObjectStreamingLimits"}:
        return "Postfx reads the borderless cvar defaults. Contrast enabled follows the desktop read of the brightness flag. Object streaming starts at 500 and 500 with a combined cap of 1000. Not device-verified."
    if status == "implemented" and name in {"engineGetPoolCapacity", "engineGetPoolDefaultCapacity", "engineGetPoolUsedCapacity", "engineSetPoolCapacity", "engineSetAsynchronousLoading", "enginePreloadWorldArea", "enginePreloadWorldAreaInDirection", "engineRestream", "engineRestreamModel", "engineRestreamWorld"}:
        return "Default pool sizes are the modded CPoolsSA table, including 32000 buildings and 1200 objects. A size below that default returns false. Live pool counts, scene preload, and entity restream fail closed. Asynchronous loading stores the enabled and forced flags. Not device-verified."
    if status == "implemented" and name.startswith("engineStreaming"):
        return "Memory size follows CCore: zero is rejected and the unset size is 50 MB. Cache limits clear on nil. Load state is the byte at ms_aInfoForModel+id*20+0x10, for ids below 26316. Buffer resize and the resource model streamer fail closed on the host. Not device-verified."
    if status == "implemented" and name in {"engineGetSurfaceProperties", "engineSetSurfaceProperties", "engineResetSurfaceProperties"}:
        return "Surface records are 12 bytes at g_surfaceInfos+0x90. GetBulletFx reads the flag word at record+2. Bad arguments return nil. stepdust has no case and returns false. Surface 179 is outside the 179-record table and returns false. The host has no surface table. Not device-verified."
    if status == "implemented" and name in {"engineGetObjectGroupPhysicalProperty", "engineSetObjectGroupPhysicalProperty", "engineRestoreObjectGroupPhysicalProperties", "engineGetModelPhysicalPropertiesGroup", "engineSetModelPhysicalPropertiesGroup", "engineRestoreModelPhysicalPropertiesGroup"}:
        return "Object groups are 0..159 records of 88 bytes. ObjectDamage reads smash at +0x38 and break velocity at +0x3c; Explode reads the fx blueprint at +0x30. The model group is the signed halfword at CBaseModelInfo+0x2a; 0xFFFF is -1. fx_system cannot be read, and a blueprint name fails closed. The host has no object table, so a valid id fails closed. Not device-verified."
    if status == "implemented" and name in {"engineGetModelVisibleTime", "engineSetModelVisibleTime"}:
        return "Hours are a signed byte and must be 0..24. A non-time model reports 0, 24 and rejects set. Bytes come from the GetTimeInfo virtual (vptr+0x30): time models at +0x48, lod-time models at +0x4c. The first set keeps the previous hours. The host has no model table. Not device-verified."
    if status == "implemented" and name in {"engineSetModelFlags", "engineSetModelFlag", "engineResetModelFlags", "engineSetModelTXDID", "engineResetModelTXDID", "engineGetTXDIDFromName", "engineGetModelRuntimeID", "engineGetModelServerID", "engineGetModelParent", "engineGetModelType"}:
        return "Flag and TXD writes follow CModelInfoSA on the verified arm64 field layout. Raw flags keep the collision-loaded bit. A single IDE flag does not record an original. TXD changes transfer CTxdStore refs, plus one when a RenderWare object exists. TXD names must be 1..24 characters. Runtime, server, parent, and type ids return false until a server model is registered. The host has no model table. Not device-verified."
    if status == "implemented" and name in {"engineSetModelLODDistance", "engineResetModelLODDistance"}:
        return "Writes the verified +0x38 LOD float. Distances of 0 or below fail. Without extendedLod, values above 325 are stored as 325 and the first previous distance is kept for reset. The set error uses range [0-19999]; reset raises on a bad argument and returns false when that model was not changed. No Project2DFX lease or extended-LOD preference is active. The host has no model table. Not device-verified."
    if status == "implemented" and name in {"engineGetModelLODDistance", "engineGetModelFlags", "engineGetModelFlag", "engineGetModelTXDID"}:
        return "Stock model lookup, then libGTASA 2.10 arm64 CBaseModelInfo fields verified from Init and SetTexDictionary (+0x38 LOD, +0x2c flags, +0x24 TXD). is_damagable calls vptr+0x18 with the model. A number passed to engineGetModelLODDistance is an empty string. Flags and the TXD id raise ArgumentParser errors. The host has no model table, so a resolved stock id fails closed. Not device-verified."
    if status == "implemented" and name in {
        "engineLoadCOL", "engineLoadDFF", "engineLoadTXD", "engineLoadIFP", "engineLoadIMG",
        "engineAddImage", "engineRemoveImage", "engineImageGetFile", "engineImageGetFiles", "engineImageGetFilesCount",
        "engineImageLinkDFF", "engineImageLinkTXD", "engineRestoreDFFImage", "engineRestoreTXDImage",
        "engineReplaceCOL", "engineRestoreCOL", "engineImportTXD", "engineReplaceModel", "engineRestoreModel",
        "engineRequestModel", "engineFreeModel", "engineRequestTXD", "engineFreeTXD",
        "engineAddClothingModel", "engineAddClothingTXD", "engineReplaceEncryptedModel",
        "engineReplaceAnimation", "engineRestoreAnimation",
        "engineApplyShaderToWorldTexture", "engineRemoveShaderFromWorldTexture",
        "engineGetModelTextureNames", "engineGetVisibleTextureNames", "engineGetModelTextures",
    }:
        return "Load and replace follow the desktop argument checks. Raw COL, DFF, TXD, and IFP headers are recognized. No resource file, archive, clothing slot, shader, or texture is decoded, so a parsed call fails closed. Image and clothing calls require img, txd, or dff userdata. engineRequestModel does not allocate an id. Not device-verified."
    if status == "implemented" and name == "engineParseSAMPMap":
        return "Parses the literal Texture Studio map text from CSampMapParser.cpp and returns the desktop result table. It does not create elements or remap models. A non-string argument raises. Not device-verified."
    if status == "implemented" and name in {"engineGetModelNameFromID", "engineGetModelIDFromName"}:
        return "Stock IDE names, then vehicle display names for models 400..610 (CModelNames::InitializeMaps). Model 611 keeps no vehicle name. Lookup is case-insensitive. Server-model definitions are checked first and stay empty until engineRequestModel allocates them. A number passed to engineGetModelIDFromName is an empty string, matching lua_objlen. Host-tested; not device-verified."
    if status == "implemented" and name == "getValidPedModels":
        return "Stock models only (0..312 excluding 74, 149, 208); custom allocation is not ported. Host-tested; not device-verified. (GetValidPedModels)"
    if status == "implemented" and name in {"clearDebugBox", "isMTAWindowFocused"}:
        return "clearDebugBox follows CLuaClientDefs.cpp:72-76 and returns true. There is no CDebugView, so nothing is cleared. isMTAWindowFocused reads m_bFocused. The activity reports foreground focus; the VM copies it on the next pump and fires onClientMTAFocusChange. Host-tested. Not device-verified."
    if status == "implemented" and name == "debugSleep":
        return "Follows CLuaUtilDefs.cpp:611-648. A bad number returns false. Outside development mode it logs an error and returns false. In development mode it delivers finished fetchRemote calls, then sleeps. Host-tested."
    if status == "implemented" and name in {"setWindowFlashing", "createTrayNotification", "isTrayNotificationEnabled"}:
        return "setWindowFlashing follows CStaticFunctionDefinitions.cpp:308-323. A focused window or a disabled server_can_flash_window cvar returns false. FlashWindowEx is not hooked, so an allowed call returns true and does not flash. createTrayNotification follows CStaticFunctionDefinitions.cpp:325-333 and CTrayIcon.cpp:101-137. allow_tray_notifications defaults to true. Shell_NotifyIcon is not hooked, so the call returns false. Host-tested. Not device-verified."
    if status == "implemented" and name == "getBoundKeys":
        return "Queries command bindings across resources; no physical GTA-control keyboard mappings on mobile yet. Host-tested; not device-verified. (GetBoundKeys)"
    if status == "implemented" and name in {
        "createObject", "isObjectStatic", "setObjectStatic", "getObjectScale", "setObjectScale",
        "isObjectBreakable", "setObjectBreakable", "getObjectMass", "setObjectMass", "getObjectProperty",
        "setObjectProperty", "isObjectMoving", "isObjectRespawnable", "moveObject", "stopObject",
        "breakObject", "respawnObject", "toggleObjectRespawn", "setObjectMaterial", "removeObjectMaterial",
        "setObjectGangTagAlpha", "acquireObjectGangTag", "setObjectGangTagProgress", "getObjectGangTagProgress",
        "releaseObjectGangTag",
    }:
        return "Follows CLuaObjectDefs.cpp and the object writes in CStaticFunctionDefinitions.cpp. createObject needs a resource and a stock object model; peds, vehicles, clothes, and the cutscene range are rejected. The element stores scale, mass, properties, frozen, respawn, movement, and materials. CObject is not created, so breakObject and setObjectGangTagAlpha return false and a texture replace cannot resolve. The host has no model table. Not device-verified."
    if status == "implemented" and name in {
        "isPedHeadless", "setPedHeadless", "isPedFrozen", "setPedFrozen",
        "isPedFootBloodEnabled", "setPedFootBloodEnabled", "isPedBleeding", "setPedBleeding",
        "getPedOxygenLevel", "setPedOxygenLevel", "getPedArmor", "getPlayerArmor", "setPedArmor",
        "getPedFightingStyle", "setPedFightingStyle", "getPedWalkingStyle", "setPedWalkingStyle",
        "isPedUsingNativeWalkingStyle", "setPedUseNativeWalkingStyle", "isPedWearingJetpack",
        "getPedClothes", "isPedDoingGangDriveby", "setPedDoingGangDriveby",
        "getPedCameraRotation", "setPedCameraRotation", "isPedMissionActor", "setPedMissionActor",
        "isPedStoryProtected", "setPedStoryProtected", "getPedSuffersCriticalHits", "setPedSuffersCriticalHits",
        "canPedBeDraggedOut", "setPedCanBeDraggedOut", "isPedOnlyDamagedByPlayer", "setPedOnlyDamagedByPlayer",
        "getPedStayInSamePlace", "setPedStayInSamePlace", "isPedNeverTargeted", "setPedNeverTargeted",
        "getPedPhysicalProofs", "setPedPhysicalProofs", "getPedWeaponAccuracy", "setPedWeaponAccuracy",
        "getPedWeaponShootingRate", "setPedWeaponShootingRate",
    }:
        return "Follows CLuaPedDefs.cpp and the CClientPed fields those calls store. Headless, frozen, bleeding, armor, fighting style, walking style, gang driveby, camera rotation, mission policy and physical proofs are kept on the client element. ENTITY_ADD headless, frozen, armor and walking style are the unset values. Fighting style starts at 15 and only a client-created ped accepts styles 4..16. Script clothes are read back for slots 0-3. Oxygen and foot blood return the unstreamed result. Weapon accuracy and shooting rate return -1 until the ped is streamed, then the stored byte. CPed body bits are not written. Host-tested. Not device-verified."
    if status == "implemented" and name in {
        "setPedAnimation", "getPedAnimation", "getPedAnimationProgress", "getPedAnimationSpeed", "getPedAnimationLength",
        "setPedAnimationProgress", "setPedAnimationSpeed", "isPedReloadingWeapon",
        "forcePlayerMap", "isPlayerMapForced", "isPlayerMapVisible", "getPlayerMapBoundingBox", "getPlayerMapOpacity",
        "getPlayerScriptDebugLevel", "isPlayerCrosshairVisible", "getPlayerUserName",
    }:
        return "setPedAnimation follows CLuaPedDefs.cpp 2602-2664. An empty block or name clears the animation. A named block returns false because the animation block is not loaded. Progress, speed and length return -1 because task 401 is not running. isPedReloadingWeapon is false because the weapon state is not read. forcePlayerMap stores the flag. The map image is absent, so the map stays hidden and the bounding box is false. Opacity is the desktop default 155. Script debug level starts at 0. The crosshair is false because the camera mode and weapon are not read. getPlayerUserName returns false, as the desktop function does. Host-tested. Not device-verified."
    if status == "implemented" and name in {"getPlayerHudComponentProperty", "setPlayerHudComponentProperty", "resetPlayerHudComponentProperty"}:
        return "Follows CLuaPlayerDefs.cpp 857-1272. Component and property names, bar and text limits, and the read-only text size match the desktop. A set value is returned by the getter. Unset placement is 0,0 because CHud is not read. Radar layout returns false. Host-tested. Not device-verified."
    if status == "implemented" and name in {
        "acquireMissionText", "showMissionText", "showMissionHelp", "showMissionBigText",
        "clearMissionTexts", "clearMissionHelp", "releaseMissionText",
        "getElementBonePosition", "getElementBoneRotation", "getElementBoneQuaternion", "getElementBoneMatrix",
        "setElementBonePosition", "setElementBoneRotation", "setElementBoneQuaternion", "setElementBoneMatrix",
        "updateElementRpHAnim", "getPedBonePosition", "setPedStat", "killPed", "setPedAimTarget",
        "detonateSatchels", "removePedFromVehicle",
    }:
        return "Mission text follows CLuaPlayerDefs.cpp 159-258. A GXT name is 1 to 7 letters, digits, or underscores. The block is not loaded, so a new lease fails. Bone calls follow CLuaPedDefs.cpp 1349-1520 and return false without a streamed CEntity. killPed marks a client-created ped dead. setPedStat stores ids 0..342 and rejects fat and muscle unless the model is CJ. detonateSatchels is false because no satchels are counted. removePedFromVehicle is false when the ped is not in a vehicle. Host-tested. Not device-verified."
    if status == "implemented" and name in {
        "getPedVoice", "setPedVoice", "resetPedVoice", "playPedVoiceLine",
        "getPedAnalogControlState", "setPedAnalogControlState", "givePedWeapon", "warpPedIntoVehicle",
        "setPedLookAt", "killPedTask", "getPedCarriedObject", "setPedCarryObject", "pickUpPedObject",
        "putDownPedObject", "cancelPedCarryObject", "getPedNoiseLevel", "setPedDriveTo", "setPedDriveMission",
        "setPedDriveBy", "setPedTaskSequence", "acquirePedNativeEventProfile", "releasePedNativeEventProfile",
        "isPedNativeEventProfileActive", "acquirePedNativeGroup", "releasePedNativeGroup", "isPedNativeGroupActive",
        "getPedNativeGroupDiagnostic", "validatePedNativeCouple", "acquirePedNativeCouple", "releasePedNativeCouple",
        "isPedNativeCoupleActive", "getPedNativeCoupleDiagnostic", "acquirePedNativeCouplePresentation",
        "updatePedNativeCouplePresentation", "releasePedNativeCouplePresentation", "isPedNativeCouplePresentationActive",
        "acquirePedNativePointArm", "updatePedNativePointArm", "releasePedNativePointArm", "isPedNativePointArmActive",
        "addPedNativeGunAimedAtEvent", "addPedNativeDamageResponseEvent", "getPedNativeVehicleResponse",
        "restorePedNativeVehicleResponse", "addPedNativeDamageEvent", "addPedNativeBikeJackTask",
    }:
        return "Voice, analog, weapons, warp, look-at, task kill, carry, noise, drive, sequence, and native leases follow CLuaPedDefs.cpp 601-682, 1582-1766, 2022-2054, 2319-2353, 3286-3676, 3743-4510, and 4586-4712. A player analog read is 0 because the controller is not sampled. A script ped stores the 14 analog controls and clears the opposite axis. givePedWeapon updates the client weapon cache and does not call CPed::GiveWeapon. warp stays false because WarpIntoVehicle is not called. setPedLookAt returns true and does not call LookAt. killPedTask rejects the default slot and otherwise returns false. Carry state is released. Drive and sequence calls return false because the ped is not driving and CTask is not created. Native profile, group, couple, point-arm, and event calls return false because no resource lease is stored. Host-tested. Not device-verified."
    if status == "implemented" and name in {
        "isPedNativeTaskReady", "setPedGoTo", "setPedNavigateTo", "setPedChatWith", "setPedStandStill", "setPedHandsUp",
        "setPedTurnToFace", "setPedGoToOffset", "setPedKillOnFoot", "setPedAchieveHeading", "setPedAimAt", "setPedShootAt",
        "setPedJump", "setPedScriptedSpeechMuted", "setPedFacialTalk", "stopPedFacialTalk",
        "isPedNativeAmbientCopWanderTask", "getPedTaskSequenceProgress", "setPedDriveWander", "setPedEnterVehicle",
        "setPedExitVehicle",
    }:
        return "Follows CLuaPedDefs.cpp 2997-3283 and 4624-4641. An unstreamed, dead, or unowned ped returns false before a task is built. A call that would create a task returns false because CTask is not created. Enter and exit return false because EnterVehicle and ExitVehicle are not called. Sequence progress is -1. Host-tested. Not device-verified."
    if status == "implemented" and name in {"getPlayerMoney", "setPlayerMoney", "givePlayerMoney", "takePlayerMoney", "getPlayerWantedLevel"}:
        return "Money follows CLuaPlayerDefs.cpp 501-511 and 582-648. set, give and take keep the balance getPlayerMoney returns. The HUD is not updated. getPlayerWantedLevel returns 0 because CWanted::GetWantedLevel is not read. Host-tested. Not device-verified."
    if status == "implemented":
        return f"Operational on Android. ({detail})"
    if status == "stub":
        return f"Currently stubbed: `{detail}`. Needs native engine wiring."
    # Missing: provide technical requirements based on prefixes and groups
    if name.startswith("dxCreateShader") or "Shader" in name:
        return "Needs GLSL ES 3.0 shader pipeline (translating desktop D3DX9 HLSL)."
    if name.startswith("dx"):
        return "Needs Android Canvas / OpenGL ES draw line routing."
    if name.startswith("guiCreateGridList") or name.startswith("guiGridList"):
        return "Needs native Android ListView / RecyclerView or custom CEGUI widget port."
    if name.startswith("gui"):
        return "Needs Android widget state and input handler in LuaGuiApi."
    if name.startswith("sound") or name.startswith("getSound") or name.startswith("setSound") or name.startswith("play"):
        return "Needs AudioTrack / OpenSL ES audio playback mixer."
    if name.startswith("engineLoad") or name.startswith("engineReplace"):
        return "Needs hooking WarDrum RenderWare streaming clump/texture loader."
    if name.startswith("getVehicle") or name.startswith("setVehicle"):
        return "Needs reading/writing CVehicle struct fields in libGTASA.so."
    if name.startswith("getPed") or name.startswith("setPed"):
        return "Needs reading/writing CPed / CPlayerPed struct fields in libGTASA.so."
    if name.startswith("getCamera") or name.startswith("setCamera"):
        return "Needs reading/writing CCamera singleton in libGTASA.so."
    if name.startswith("xml"):
        return "Needs TinyXML2 client-side parser integration."
    if name.startswith("file"):
        return "Needs local sandbox file I/O operations."
    if "Browser" in group:
        return "Needs Android offscreen WebView render-to-texture bridge."
    return "Needs desktop C++ logic ported to app/src/main/cpp/shared."

# Generate category files
for cat_id, cat_info in CATEGORIES.items():
    cat_funcs = []
    for g in cat_info["groups"]:
        for name, data in func_data.items():
            if data["group"] == g:
                cat_funcs.append(data)
    
    # Sort functions by name
    cat_funcs.sort(key=lambda x: x["name"])
    
    total = len(cat_funcs)
    impl_cnt = sum(1 for x in cat_funcs if x["status"] == "implemented")
    stub_cnt = sum(1 for x in cat_funcs if x["status"] == "stub")
    miss_cnt = sum(1 for x in cat_funcs if x["status"] == "missing")
    pct = (impl_cnt / total * 100) if total > 0 else 0.0

    filepath = out_dir / cat_info["file"]
    with open(filepath, "w", encoding="utf-8") as f:
        f.write(f"# {cat_info['title']}\n\n")
        f.write(f"{cat_info['description']}\n\n")
        f.write("## Binding Coverage\n\n")
        f.write(f"- **Total Functions:** {total}\n")
        f.write(f"- **Nonconstant handlers:** {impl_cnt} ({pct:.1f}%)\n")
        f.write(f"- **Stubs:** {stub_cnt}\n")
        f.write(f"- **Missing:** {miss_cnt}\n\n")
        f.write(f"> **Technical Requirement:** {cat_info['subsystem_notes']}\n\n")
        f.write("## Function List\n\n")
        f.write("| Lua Function | Status | Desktop Source Ref | Desktop Handler | Conversion Requirement / Implementation Notes |\n")
        f.write("|---|---|---|---|---|\n")
        for x in cat_funcs:
            req = guess_conversion_notes(x["name"], x["group"], x["status"], x["detail"])
            ref = f"`{x['file'].split('/')[-1]}:{x['line']}`"
            handler = f"`{x['handler']}`" if x['handler'] else "—"
            f.write(f"| `{x['name']}` | {format_status(x['status'])} | {ref} | {handler} | {req} |\n")
    print(f"Generated {filepath} ({total} functions)")

# Generate README index
with open(out_dir / "README.md", "w", encoding="utf-8") as f:
    f.write("# MTA Neon Client Lua API: Complete Conversion Catalog\n\n")
    f.write("This directory contains a complete, exhaustive breakdown of **all 1,581 desktop MTA client Lua functions**, organized into 12 functional categories.\n\n")
    f.write("The counts measure Lua binding coverage. `Implemented` identifies a nonconstant Android handler. Behavior, engine support, and device verification are described in the function notes.\n\n")
    f.write("## Summary by Category\n\n")
    f.write("| Category | Documentation File | Total | Nonconstant handlers | Stub | Missing | Nonconstant % |\n")
    f.write("|---|---|---|---|---|---|---|\n")
    for cat_id, cat_info in CATEGORIES.items():
        cat_funcs = []
        for g in cat_info["groups"]:
            for name, data in func_data.items():
                if data["group"] == g:
                    cat_funcs.append(data)
        total = len(cat_funcs)
        impl_cnt = sum(1 for x in cat_funcs if x["status"] == "implemented")
        stub_cnt = sum(1 for x in cat_funcs if x["status"] == "stub")
        miss_cnt = sum(1 for x in cat_funcs if x["status"] == "missing")
        pct = (impl_cnt / total * 100) if total > 0 else 0.0
        f.write(f"| **{cat_info['title']}** | [{cat_info['file']}]({cat_info['file']}) | {total} | {impl_cnt} | {stub_cnt} | {miss_cnt} | {pct:.1f}% |\n")
    
    total_all = len(func_data)
    total_impl = sum(1 for x in func_data.values() if x["status"] == "implemented")
    total_stub = sum(1 for x in func_data.values() if x["status"] == "stub")
    total_miss = sum(1 for x in func_data.values() if x["status"] == "missing")
    total_pct = (total_impl / total_all * 100) if total_all > 0 else 0.0
    f.write(f"| **TOTAL** | **All Categories** | **{total_all}** | **{total_impl}** | **{total_stub}** | **{total_miss}** | **{total_pct:.1f}%** |\n\n")
    f.write("## Implementation Priority Tiers\n\n")
    f.write("1. **Tier 1 (Core Gameplay & UI - Needed Now)**: `ELEMENTS.md`, `GUI_AND_DX.md`, `PEDS_AND_PLAYERS.md`, `VEHICLES.md`.\n")
    f.write("2. **Tier 2 (World & Audio - Needed for Ambience)**: `WORLD_AND_OBJECTS.md`, `AUDIO_AND_VOICE.md`, `CAMERA.md`, `WEAPONS_AND_COMBAT.md`.\n")
    f.write("3. **Tier 3 (Modding & Advanced UI - Long Term)**: `ENGINE_AND_MODELS.md`, `MARKERS_AND_SPATIAL.md`, `BROWSER_AND_COMPATIBILITY.md`.\n")

print("Generated docs/lua-functions/README.md")
