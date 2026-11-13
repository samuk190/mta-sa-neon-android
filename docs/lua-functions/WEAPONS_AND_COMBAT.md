# Weapons & Projectiles (CLuaWeaponDefs, CLuaProjectileDefs)

Custom weapons, weapon damage/range/accuracy stats, projectiles (rockets, grenades), bullet traces, and weapon firing flags.

## Parity Status

- **Total Functions:** 31
- **Implemented:** 0 (0.0%)
- **Stubs:** 0
- **Missing:** 31

> **Technical Requirement:** Requires hooking `CWeaponInfo`, `CProjectileInfo`, bullet trace pool (`CBulletTraces`), and bullet sync packet (98).

## Function List

| Lua Function | Status | Desktop Source Ref | Desktop Handler | Conversion Requirement / Implementation Notes |
|---|---|---|---|---|
| `createProjectile` | Missing | `CLuaProjectileDefs.cpp:17` | `CreateProjectile` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `createWeapon` | Missing | `CLuaWeaponDefs.cpp:23` | `CreateWeapon` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `fireWeapon` | Missing | `CLuaWeaponDefs.cpp:27` | `FireWeapon` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getOriginalWeaponProperty` | Missing | `CLuaWeaponDefs.cpp:26` | `ArgumentParserWarn<false, GetOriginalWeaponProperty>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getProjectileCounter` | Missing | `CLuaProjectileDefs.cpp:19` | `GetProjectileCounter` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getProjectileCreator` | Missing | `CLuaProjectileDefs.cpp:18` | `GetProjectileCreator` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getProjectileForce` | Missing | `CLuaProjectileDefs.cpp:18` | `GetProjectileForce` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getProjectileTarget` | Missing | `CLuaProjectileDefs.cpp:17` | `GetProjectileTarget` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getProjectileType` | Missing | `CLuaProjectileDefs.cpp:17` | `GetProjectileType` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getSlotFromWeapon` | Missing | `CLuaWeaponDefs.cpp:22` | `GetSlotFromWeapon` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getWeaponAmmo` | Missing | `CLuaWeaponDefs.cpp:39` | `GetWeaponAmmo` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getWeaponClipAmmo` | Missing | `CLuaWeaponDefs.cpp:40` | `GetWeaponClipAmmo` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getWeaponFiringRate` | Missing | `CLuaWeaponDefs.cpp:37` | `GetWeaponFiringRate` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getWeaponFlags` | Missing | `CLuaWeaponDefs.cpp:35` | `GetWeaponFlags` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getWeaponIDFromName` | Missing | `CLuaWeaponDefs.cpp:21` | `GetWeaponIDFromName` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getWeaponNameFromID` | Missing | `CLuaWeaponDefs.cpp:20` | `GetWeaponNameFromID` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getWeaponOwner` | Missing | `CLuaWeaponDefs.cpp:33` | `GetWeaponOwner` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getWeaponProperty` | Missing | `CLuaWeaponDefs.cpp:25` | `ArgumentParserWarn<false, GetWeaponProperty>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getWeaponState` | Missing | `CLuaWeaponDefs.cpp:29` | `GetWeaponState` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `getWeaponTarget` | Missing | `CLuaWeaponDefs.cpp:31` | `GetWeaponTarget` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `isWeaponRenderEnabled` | Missing | `CLuaWeaponDefs.cpp:44` | `ArgumentParser<IsWeaponRenderEnabled>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `resetWeaponFiringRate` | Missing | `CLuaWeaponDefs.cpp:38` | `ResetWeaponFiringRate` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setProjectileCounter` | Missing | `CLuaProjectileDefs.cpp:18` | `SetProjectileCounter` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setWeaponAmmo` | Missing | `CLuaWeaponDefs.cpp:41` | `SetWeaponAmmo` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setWeaponClipAmmo` | Missing | `CLuaWeaponDefs.cpp:42` | `SetWeaponClipAmmo` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setWeaponFiringRate` | Missing | `CLuaWeaponDefs.cpp:36` | `SetWeaponFiringRate` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setWeaponFlags` | Missing | `CLuaWeaponDefs.cpp:34` | `SetWeaponFlags` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setWeaponProperty` | Missing | `CLuaWeaponDefs.cpp:24` | `SetWeaponProperty` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setWeaponRenderEnabled` | Missing | `CLuaWeaponDefs.cpp:43` | `ArgumentParser<SetWeaponRenderEnabled>` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setWeaponState` | Missing | `CLuaWeaponDefs.cpp:28` | `SetWeaponState` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
| `setWeaponTarget` | Missing | `CLuaWeaponDefs.cpp:30` | `SetWeaponTarget` | Needs desktop C++ logic ported to app/src/main/cpp/shared. |
