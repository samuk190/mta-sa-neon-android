# Weapons & Projectiles (CLuaWeaponDefs, CLuaProjectileDefs)

Custom weapons, weapon damage/range/accuracy stats, projectiles (rockets, grenades), bullet traces, and weapon firing flags.

## Binding Coverage

- **Total Functions:** 31
- **Nonconstant handlers:** 31 (100.0%)
- **Stubs:** 0
- **Missing:** 0

> **Technical Requirement:** Requires hooking `CWeaponInfo`, `CProjectileInfo`, bullet trace pool (`CBulletTraces`), and bullet sync packet (98).

## Function List

| Lua Function | Status | Desktop Source Ref | Desktop Handler | Conversion Requirement / Implementation Notes |
|---|---|---|---|---|
| `createProjectile` | **Implemented** | `CLuaProjectileDefs.cpp:17` | `CreateProjectile` | Native local-projectile bridge for verified GTA 2.10 arm64. Rotation/model overrides, satchel attachment, creation event and packet sync remain unported. Host guards tested; Android compiles; physical runtime not device-tested. |
| `createWeapon` | **Implemented** | `CLuaWeaponDefs.cpp:23` | `CreateWeapon` | Operational on Android. (CreateWeapon) |
| `fireWeapon` | **Implemented** | `CLuaWeaponDefs.cpp:27` | `FireWeapon` | Operational on Android. (FireWeapon) |
| `getOriginalWeaponProperty` | **Implemented** | `CLuaWeaponDefs.cpp:26` | `ArgumentParserWarn<false, GetOriginalWeaponProperty>` | Operational on Android. (l_getOriginalWeaponProperty) |
| `getProjectileCounter` | **Implemented** | `CLuaProjectileDefs.cpp:19` | `GetProjectileCounter` | Native local-projectile bridge for verified GTA 2.10 arm64. Rotation/model overrides, satchel attachment, creation event and packet sync remain unported. Host guards tested; Android compiles; physical runtime not device-tested. |
| `getProjectileCreator` | **Implemented** | `CLuaProjectileDefs.cpp:18` | `GetProjectileCreator` | Native local-projectile bridge for verified GTA 2.10 arm64. Rotation/model overrides, satchel attachment, creation event and packet sync remain unported. Host guards tested; Android compiles; physical runtime not device-tested. |
| `getProjectileForce` | **Implemented** | `CLuaProjectileDefs.cpp:18` | `GetProjectileForce` | Native local-projectile bridge for verified GTA 2.10 arm64. Rotation/model overrides, satchel attachment, creation event and packet sync remain unported. Host guards tested; Android compiles; physical runtime not device-tested. |
| `getProjectileTarget` | **Implemented** | `CLuaProjectileDefs.cpp:17` | `GetProjectileTarget` | Native local-projectile bridge for verified GTA 2.10 arm64. Rotation/model overrides, satchel attachment, creation event and packet sync remain unported. Host guards tested; Android compiles; physical runtime not device-tested. |
| `getProjectileType` | **Implemented** | `CLuaProjectileDefs.cpp:17` | `GetProjectileType` | Native local-projectile bridge for verified GTA 2.10 arm64. Rotation/model overrides, satchel attachment, creation event and packet sync remain unported. Host guards tested; Android compiles; physical runtime not device-tested. |
| `getSlotFromWeapon` | **Implemented** | `CLuaWeaponDefs.cpp:22` | `GetSlotFromWeapon` | Operational on Android. (l_getSlotFromWeapon) |
| `getWeaponAmmo` | **Implemented** | `CLuaWeaponDefs.cpp:39` | `GetWeaponAmmo` | Queries the last server ENTITY_ADD weapon snapshot; native firing and later weapon RPC updates are not ported. Host-tested, not device-verified. (GetWeaponAmmo) |
| `getWeaponClipAmmo` | **Implemented** | `CLuaWeaponDefs.cpp:40` | `GetWeaponClipAmmo` | Queries the last server ENTITY_ADD weapon snapshot; native firing and later weapon RPC updates are not ported. Host-tested, not device-verified. (GetWeaponClipAmmo) |
| `getWeaponFiringRate` | **Implemented** | `CLuaWeaponDefs.cpp:37` | `GetWeaponFiringRate` | Operational on Android. (GetWeaponFiringRate) |
| `getWeaponFlags` | **Implemented** | `CLuaWeaponDefs.cpp:35` | `GetWeaponFlags` | Operational on Android. (GetWeaponFlags) |
| `getWeaponIDFromName` | **Implemented** | `CLuaWeaponDefs.cpp:21` | `GetWeaponIDFromName` | Operational on Android. (l_getWeaponIDFromName) |
| `getWeaponNameFromID` | **Implemented** | `CLuaWeaponDefs.cpp:20` | `GetWeaponNameFromID` | Operational on Android. (l_getWeaponNameFromID) |
| `getWeaponOwner` | **Implemented** | `CLuaWeaponDefs.cpp:33` | `GetWeaponOwner` | Queries the last server ENTITY_ADD weapon snapshot; native firing and later weapon RPC updates are not ported. Host-tested, not device-verified. (GetWeaponOwner) |
| `getWeaponProperty` | **Implemented** | `CLuaWeaponDefs.cpp:25` | `ArgumentParserWarn<false, GetWeaponProperty>` | Operational on Android. (GetWeaponPropertyAny) |
| `getWeaponState` | **Implemented** | `CLuaWeaponDefs.cpp:29` | `GetWeaponState` | Queries the last server ENTITY_ADD weapon snapshot; native firing and later weapon RPC updates are not ported. Host-tested, not device-verified. (GetWeaponState) |
| `getWeaponTarget` | **Implemented** | `CLuaWeaponDefs.cpp:31` | `GetWeaponTarget` | Queries the last server ENTITY_ADD weapon snapshot; native firing and later weapon RPC updates are not ported. Host-tested, not device-verified. (GetWeaponTarget) |
| `isWeaponRenderEnabled` | **Implemented** | `CLuaWeaponDefs.cpp:44` | `ArgumentParser<IsWeaponRenderEnabled>` | Operational on Android. (l_isWeaponRenderEnabled) |
| `resetWeaponFiringRate` | **Implemented** | `CLuaWeaponDefs.cpp:38` | `ResetWeaponFiringRate` | Operational on Android. (ResetWeaponFiringRate) |
| `setProjectileCounter` | **Implemented** | `CLuaProjectileDefs.cpp:18` | `SetProjectileCounter` | Native local-projectile bridge for verified GTA 2.10 arm64. Rotation/model overrides, satchel attachment, creation event and packet sync remain unported. Host guards tested; Android compiles; physical runtime not device-tested. |
| `setWeaponAmmo` | **Implemented** | `CLuaWeaponDefs.cpp:41` | `SetWeaponAmmo` | Operational on Android. (SetWeaponAmmo) |
| `setWeaponClipAmmo` | **Implemented** | `CLuaWeaponDefs.cpp:42` | `SetWeaponClipAmmo` | Operational on Android. (SetWeaponClipAmmo) |
| `setWeaponFiringRate` | **Implemented** | `CLuaWeaponDefs.cpp:36` | `SetWeaponFiringRate` | Operational on Android. (SetWeaponFiringRate) |
| `setWeaponFlags` | **Implemented** | `CLuaWeaponDefs.cpp:34` | `SetWeaponFlags` | Operational on Android. (SetWeaponFlags) |
| `setWeaponProperty` | **Implemented** | `CLuaWeaponDefs.cpp:24` | `SetWeaponProperty` | Operational on Android. (SetWeaponPropertyAny) |
| `setWeaponRenderEnabled` | **Implemented** | `CLuaWeaponDefs.cpp:43` | `ArgumentParser<SetWeaponRenderEnabled>` | Operational on Android. (l_setWeaponRenderEnabled) |
| `setWeaponState` | **Implemented** | `CLuaWeaponDefs.cpp:28` | `SetWeaponState` | Operational on Android. (SetWeaponState) |
| `setWeaponTarget` | **Implemented** | `CLuaWeaponDefs.cpp:30` | `SetWeaponTarget` | Operational on Android. (SetWeaponTarget) |
