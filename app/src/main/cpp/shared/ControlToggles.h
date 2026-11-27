#pragma once

// Control enable state for toggleControl / toggleAllControls / isControlEnabled and the two server RPCs
// (TOGGLE_CONTROL_ABILITY 105, TOGGLE_ALL_CONTROL_ABILITY 106).
//
// Desktop reference: CKeyBinds.cpp g_bcControls (187-232, GTA controls, each with bEnabled), CKeyBinds::
// IsControlEnabled / SetControlEnabled / SetAllControlsEnabled (1233-1310), CCommands::Get (280-298) and the mod
// commands registered in CClient.cpp (55-90). A control name is looked up case-insensitively, first among the 44
// GTA controls and then among the "mod commands" (chatbox, radar, ...), exactly as desktop does.
//
// The GTA part is also what the phone applies to its own input: CPad getters are masked through MaskGetter
// (gtasa/ControlMask.cpp), which covers the touch widgets, a hardware keyboard, a gamepad and the hardware mouse
// because all of them end in the same CPad getters. Mod commands that the Java layer owns (chatbox, radar) are
// pushed to the activity as game line kind 20.

#include <cstddef>
#include <cstdint>

namespace WorldRPG::Controls {

// Index = position in desktop g_bcControls (CKeyBinds.cpp:187-232).
enum class Gta : uint8_t {
    Fire, NextWeapon, PreviousWeapon, Forwards, Backwards, Left, Right, ZoomIn, ZoomOut, EnterExit,
    ChangeCamera, Jump, Sprint, LookBehind, Crouch, Action, Walk, VehicleFire, VehicleSecondaryFire,
    VehicleLeft, VehicleRight, SteerForward, SteerBack, Accelerate, BrakeReverse, RadioNext, RadioPrevious,
    RadioUserTrackSkip, Horn, SubMission, Handbrake, VehicleLookLeft, VehicleLookRight, VehicleLookBehind,
    VehicleMouseLook, SpecialControlLeft, SpecialControlRight, SpecialControlDown, SpecialControlUp, AimWeapon,
    ConversationYes, ConversationNo, GroupControlForwards, GroupControlBack,
    Count
};
constexpr size_t kGtaControlCount = static_cast<size_t>(Gta::Count);

const char* GtaControlName(Gta control);
// Case-insensitive (desktop stricmp). Returns Gta::Count when the name is not a GTA control.
Gta FindGtaControl(const char* name);

// CKeyBinds::IsControlEnabled: the GTA control, else the mod command, else false.
bool IsControlEnabled(const char* name);
// CStaticFunctionDefinitions::IsControlEnabled (the isControlEnabled Lua function): GTA controls only. Returns
// false, and leaves enabled alone, for any other name (mod commands included).
bool GetScriptControlEnabled(const char* name, bool& enabled);

// CKeyBinds::SetControlEnabled. A GTA control whose state changes returns true. A GTA control that is already in
// that state falls through to a mod command of the same name, and any other name needs a mod command; false
// when there is none.
bool SetControlEnabled(const char* name, bool enabled);
// CKeyBinds::SetAllControlsEnabled.
void SetAllControlsEnabled(bool gtaControls, bool modCommands, bool enabled);
// CClientGame.cpp:586 and ResetMapInfo (6429): SetAllControlsEnabled(true, true, true).
void ResetAll();

// A mod command's enabled flag. False for a name that is not a mod command.
bool IsModCommandEnabled(const char* name);

// Bit n set = Gta(n) disabled. Safe to read from the game thread while the script thread writes.
uint64_t DisabledGtaMask();
bool IsGtaControlDisabled(Gta control);

// The CPad getters that the phone masks. Each row says which GTA control(s) gate it.
enum class Getter : uint8_t {
    PedWalkLeftRight, PedWalkUpDown, SteeringLeftRight, SteeringUpDown,
    Accelerate, AccelerateJustDown, Brake,
    Jump, JumpJustDown, SwimJumpJustDown, MeleeAttack, MeleeAttackJustDown, Duck, DuckJustDown, Sprint,
    SprintJustDown, Weapon, WeaponJustDown,
    ExitVehicle, ExitVehicleJustDown, Horn, HornJustDown, HandBrake,
    LookBehindForPed, LookBehindForCar, LookLeft, LookRight, Target, EnterTargeting,
    CycleWeaponLeftJustDown, CycleWeaponRightJustDown, CycleCameraModeDownJustDown,
    ConversationYesJustDown, ConversationNoJustDown,
    GroupControlForwardJustDown, GroupControlBackJustDown, GroupControlForward, GroupControlBack,
    SniperZoomIn, SniperZoomOut, NextStationJustUp, LastStationJustUp, CarGunJustDown, CarGunFired,
    Count
};
constexpr size_t kGetterCount = static_cast<size_t>(Getter::Count);

// The value a getter returns once the toggles are applied. A button getter returns 0 while its control is
// disabled. An axis getter drops only the disabled direction, as CClientPad::ProcessToggledControl does
// (CClientPad.cpp:803-870: "left" clears LeftStickX < 0, "right" clears LeftStickX > 0, "forwards" clears
// LeftStickY < 0, "backwards" clears LeftStickY > 0, and the vehicle forms act on the same stick).
int32_t MaskGetter(Getter getter, int32_t raw);

}  // namespace WorldRPG::Controls
