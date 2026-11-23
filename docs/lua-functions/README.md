# MTA Neon Client Lua API: Complete Conversion Catalog

This directory contains a complete, exhaustive breakdown of **all 1,581 desktop MTA client Lua functions**, organized into 12 functional categories.

The counts measure Lua binding coverage. `Implemented` identifies a nonconstant Android handler. Behavior, engine support, and device verification are described in the function notes.

## Summary by Category

| Category | Documentation File | Total | Nonconstant handlers | Stub | Missing | Nonconstant % |
|---|---|---|---|---|---|---|
| **Vehicles (CLuaVehicleDefs)** | [VEHICLES.md](VEHICLES.md) | 165 | 165 | 0 | 0 | 100.0% |
| **Peds & Players (CLuaPedDefs, CLuaPlayerDefs)** | [PEDS_AND_PLAYERS.md](PEDS_AND_PLAYERS.md) | 206 | 206 | 0 | 0 | 100.0% |
| **World, Objects & Environment** | [WORLD_AND_OBJECTS.md](WORLD_AND_OBJECTS.md) | 227 | 227 | 0 | 0 | 100.0% |
| **Element Tree & Data (CLuaElementDefs)** | [ELEMENTS.md](ELEMENTS.md) | 81 | 81 | 0 | 0 | 100.0% |
| **GUI & 2D/3D Drawing (DX & CEGUI)** | [GUI_AND_DX.md](GUI_AND_DX.md) | 199 | 199 | 0 | 0 | 100.0% |
| **Engine & Model Replacement (DFF, TXD, COL)** | [ENGINE_AND_MODELS.md](ENGINE_AND_MODELS.md) | 125 | 125 | 0 | 0 | 100.0% |
| **Audio & Voice System (CLuaAudioDefs)** | [AUDIO_AND_VOICE.md](AUDIO_AND_VOICE.md) | 57 | 47 | 10 | 0 | 82.5% |
| **Camera & Viewport (CLuaCameraDefs)** | [CAMERA.md](CAMERA.md) | 47 | 47 | 0 | 0 | 100.0% |
| **Weapons & Projectiles (CLuaWeaponDefs, CLuaProjectileDefs)** | [WEAPONS_AND_COMBAT.md](WEAPONS_AND_COMBAT.md) | 31 | 31 | 0 | 0 | 100.0% |
| **Core System, Networking & Utilities** | [CORE_AND_UTILITY.md](CORE_AND_UTILITY.md) | 195 | 195 | 0 | 0 | 100.0% |
| **Markers, Blips, ColShapes & Spatial** | [MARKERS_AND_SPATIAL.md](MARKERS_AND_SPATIAL.md) | 145 | 145 | 0 | 0 | 100.0% |
| **Web Browser & Compatibility (CEF / Discord)** | [BROWSER_AND_COMPATIBILITY.md](BROWSER_AND_COMPATIBILITY.md) | 103 | 103 | 0 | 0 | 100.0% |
| **TOTAL** | **All Categories** | **1581** | **1571** | **10** | **0** | **99.4%** |

## Implementation Priority Tiers

1. **Tier 1 (Core Gameplay & UI - Needed Now)**: `ELEMENTS.md`, `GUI_AND_DX.md`, `PEDS_AND_PLAYERS.md`, `VEHICLES.md`.
2. **Tier 2 (World & Audio - Needed for Ambience)**: `WORLD_AND_OBJECTS.md`, `AUDIO_AND_VOICE.md`, `CAMERA.md`, `WEAPONS_AND_COMBAT.md`.
3. **Tier 3 (Modding & Advanced UI - Long Term)**: `ENGINE_AND_MODELS.md`, `MARKERS_AND_SPATIAL.md`, `BROWSER_AND_COMPATIBILITY.md`.
