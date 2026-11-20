#pragma once

#include <cstdint>

namespace WorldRPG::Game {

// One remote CPlayerPed from the ped pool. Null when the pool is full or
// CPlayerPed::CPlayerPed is not exported. Does not call SetupPlayerPed or
// CPed::Teleport.
// keepCj: model 0 stays the CJ player clump (createPed). Remote sync passes false,
// and VisibleModel replaces 0 with skin 7.
void* CreateRemotePlayerPed(int model, float x, float y, float z, float heading, bool keepCj = false);
// CWorld::Remove only, and only when the vtable is still a live ped. Game thread.
void ReleasePlayerPed(void* ped);

// CClientPed::_CreateModel writes the element matrix (CClientPed.cpp 5772) after
// SetCurrentRotation has put a Z-only CMatrix::SetRotation on it (5298-5321,
// CMatrix.h 253-279). Position is the network Z (SetPosition, 1064-1098).
void PlaceRemotePlayerPed(void* ped, float x, float y, float z, float heading);

// Yaw only. CMatrix::SetRotateZOnly rows, plus fCurrentRotation and fTargetRotation.
// Does not move the ped. CPlaceable::SetHeading is not in libGTASA 2.10.
void SetRemotePedHeading(void* ped, float heading);

// Plan step A (controller switch). Hooks CPlayerPed::ProcessControl once. For a
// remote ped with a stick set, CPad::GetPad(0) gets that stick for one call and
// the whole pad is restored afterwards. Returns false when the symbol, CPad::GetPad
// or the hook is missing; the caller then keeps the velocity fallback.
bool InstallRemoteControlHook();
bool RemoteControlHookActive();
// Stick in PC CControllerState units (-128..128). Negative Y is forward.
// fire sets ButtonCircle (CControllerState offset 0x26) to 255, the desktop pressed value.
// camera: the remote player's keysync camera rotation (TheCamera+0x154 for the call), as desktop SwitchContext.
// CWorld::Remove plus CPed::~CPed and CPed::operator delete (desktop CPlayerPedSA::~CPlayerPedSA). Game thread.
bool DestroyRemotePlayerPed(void* ped);
// True when the body has its own CPlayerPedData and CPedClothesDesc (desktop CPlayerPedSA ctor), so model 0 can be dressed.
bool HasPrivatePlayerData(void* ped);
bool CanAttachPrivatePlayerData();
void SetRemoteStick(void* ped, int stickX, int stickY, bool fire, float camera = 0.f, bool hasCamera = false,
                    uint8_t buttons = 0);
// True when remote bodies get their own virtual touch (CTouchInterface hooks installed).
bool RemoteTouchActive();
void ClearRemoteStick(void* ped);

// Hooks CWeapon::Fire once (desktop CWeaponSA::Fire / CMultiplayerSA shotsync). For the local ped with a
// bullet-weapon id (22..34) the shot is queued with NoteLocalShot after the original runs. Returns false when
// the symbol, FindPlayerPed or the hook is missing; no bullet sync is sent then.
bool InstallLocalBulletFireHook();

// Driver seat for a remote ped (VEHICLE_INOUT, packet 58). Desktop CClientPed::InternalWarpIntoVehicle
// (CClientPed.cpp 7441) and InternalRemoveFromVehicle (7464). Game thread only.
// Both return false when a libGTASA symbol is missing or an argument is null, and the caller keeps the request.
bool SeatRemotePedInCar(void* ped, void* car);
bool UnseatRemotePedFromCar(void* ped, void* car);
// Passenger seat for a remote ped (VEHICLE_INOUT seat > 0). gameDoor is a PassengerGameDoor result (8, 9 or 11).
// Desktop CClientPed::InternalWarpIntoVehicle passenger branch (CClientPed.cpp 3441-3462). Game thread only.
// Returns false for a null argument, a door outside 8..11, or a missing libGTASA symbol.
bool SeatRemotePedAsPassenger(void* ped, void* car, int gameDoor);
// Desktop CClientVehicleManager::ConvertIndexToGameSeat (CClientVehicleManager.cpp 580-645): the packet seat index to
// the game door of the vehicle's model. Returns 0xFF when that model has no such seat.
unsigned char PassengerGameDoor(int model, unsigned char seatIndex);

}
