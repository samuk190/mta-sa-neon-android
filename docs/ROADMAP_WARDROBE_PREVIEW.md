# Wardrobe preview port (registration)

The register screen in `worldrpg_core/client/auth_screen_dx.lua` (`startClothesPreview`, `applySelectedClothesToPed`, the `register_char` render branch) shows a CJ on the pier and a DX menu beside him. The menu already draws. The eight calls below are bound. The login cinematic that moves the camera with `setCameraMatrix` from `onClientPreRender` is confirmed on a phone. Opening music uses `playSound`, `stopSound` and `setSoundVolume` (7.0% of the audio group; `playSound3D` and `playSoundFrontEnd` stay stubs). The clothes preview acceptance at the bottom is not recorded yet.

Port them in this order. Each row stays not done until its desktop function was read and compared line by line. Do not add behavior the desktop function does not have. A step that cannot run yet keeps the call and logs `DIAG` with the missing hook named.

The screen only needs slots 0-3 (torso, hair, legs, shoes), model 0, a fixed camera with the script's position and look-at (roll 0, FOV 70), clock 14:00, and weather 0.

## Order

| Step | Lua function | Desktop | Android today | Done when |
|---|---|---|---|---|
| 1 | `createPed` | `CLuaPedDefs::CreatePed` 2870-2906; `CStaticFunctionDefinitions::CreatePed` 2815-2852; `CClientPed::SetCurrentRotationNew` 1256-1258 | Bound as `l_createPed`. Model 0 stays CJ. Not recorded on a device. | `createPed(0, x, y, z, rot)` returns an element. The body is CJ at that pose, not skin 7. `destroyElement` removes it. It is not given a server sync id. |
| 2 | `setTime` | `CLuaWorldDefs::SetTime` 1452-1465; `CStaticFunctionDefinitions::SetTime` 6651-6655; `CClockSA::Set` 18-25 | Bound as `l_setTime` through `CClock::SetGameClock` when the symbol resolves. Not recorded on a device. | Hour and minute bytes are written on the game thread. `setTime(14, 0)` returns true. |
| 3 | `setWeather` | `CStaticFunctionDefinitions::SetWeather` 7076-7087; `CBlendedWeather::SetWeather` 71-78 | Bound as `l_setWeather` through `CWeather::ForceWeather` when the symbol resolves. Not recorded on a device. | Ids above `MAX_VALID_WEATHER` return false. Weather 0 sets primary and secondary and clears the blend. `SetWeatherBlended` is not part of this step. |
| 4 | `setCameraMatrix` | `CLuaCameraDefs::SetCameraMatrix` 241-289; `CStaticFunctionDefinitions::SetCameraMatrix` 5183-5213 | Bound as `l_setCameraMatrix`. The login tour that calls it from `onClientPreRender` is confirmed on the S25. Wardrobe framing uses the same call and has not been recorded. | Fixed camera at the script position, looking at the script target, roll 0, FOV 70. The login tour does not overwrite it. |
| 5 | `setCameraTarget` | `CStaticFunctionDefinitions::SetCameraTarget` 5216-5236, local player branch `SetFocusToLocalPlayer` | Bound as `l_setCameraTarget` through `CCamera::Restore` when the symbol resolves. Not recorded on a device. | `setCameraTarget(localPlayer)` leaves fixed mode and follows the local ped. |
| 6 | `addPedClothes` | `CLuaPedDefs::AddPedClothes` 2200-2214; `CStaticFunctionDefinitions::AddPedClothes` 2466-2482; `CClientPlayerClothes::AddClothes` 344-360; `CClientPed::RebuildModel` 6584-6622 | Bound as `l_addPedClothes`. Slots 0-3, one `RebuildPlayer` per frame. Not recorded on a device. | A ped element stores the slot. Texture and model are resolved for that type. Unknown names return false. `RebuildPlayer` runs at most once per frame, and only for model 0. |
| 7 | `removePedClothes` | `CLuaPedDefs::RemovePedClothes` 2227-2238; `CStaticFunctionDefinitions::RemovePedClothes` 2485-2501; `CClientPlayerClothes::RemoveClothes` 384-417 | Bound as `l_removePedClothes`. Clears the slot, then rebuilds. Not recorded on a device. | Types outside `PLAYER_CLOTHING_SLOTS` return false. Slots 0 and 3 go back to the empty piece (bare chest, bare feet), not the previous item. |
| 8 | `setElementRotation` | `CLuaElementDefs::SetElementRotation` 1913-1929; ped branch `CStaticFunctionDefinitions::SetElementRotation` 1084-1109 | Bound as `l_setElementRotation` for the script ped, Z only. Not recorded on a device. | `setElementRotation(ped, 0, 0, degrees)` writes the Z-only heading already used by `SetRemotePedHeading` (`PlayerPedPort.cpp` 174-189). Vehicle and object orders are not part of this step. |

## After the preview is visible

These return success and do not change the engine. The screen calls them. They are not why the CJ is missing.

| Lua function | Desktop behavior the preview expects |
|---|---|
| `setElementAlpha` | Local player invisible during the preview, visible again when it closes. |
| `setElementDimension` | Local player and the preview ped in the private dimension the server passed. |
| `setElementInterior` | Interior 0. The area byte write already exists for streamed peds (`CClientPed::SetInterior`). |
| `setElementFrozen` | Preview ped frozen. Local player unfrozen when the preview ends. |
| `setElementCollisionsEnabled` | Preview ped collisions off. |
| `fadeCamera` | Fade in. This screen does not use the color or duration overload. |
| `playSoundFrontEnd` | Front-end sounds 40, 41 and 42. Currently returns false. |

`getGroundPosition` is already real. A false result leaves the script's pier Z in place. `dxDrawImage` is already the DX image path.

## Acceptance

On a phone, against the live server, a new account reaches the wardrobe: CJ stands on the pier, the camera frames the selected body part, each card changes only its slot, the bare options show chest and feet, and the turn buttons and drag change his facing. Leaving the preview gives the camera back to the player. Not done until that run is recorded.
