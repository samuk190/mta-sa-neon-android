#include "ControlToggles.h"

#include <array>
#include <atomic>
#include <mutex>
#include <strings.h>

namespace WorldRPG::JniBridge {
void PostGameLine(int kind, const char* text);
}

namespace WorldRPG::Controls {

namespace {

// CKeyBinds.cpp:187-232 g_bcControls, in the same order as enum Gta.
constexpr std::array<const char*, kGtaControlCount> kGtaNames = {
    "fire", "next_weapon", "previous_weapon", "forwards", "backwards", "left", "right", "zoom_in", "zoom_out",
    "enter_exit", "change_camera", "jump", "sprint", "look_behind", "crouch", "action", "walk", "vehicle_fire",
    "vehicle_secondary_fire", "vehicle_left", "vehicle_right", "steer_forward", "steer_back", "accelerate",
    "brake_reverse", "radio_next", "radio_previous", "radio_user_track_skip", "horn", "sub_mission", "handbrake",
    "vehicle_look_left", "vehicle_look_right", "vehicle_look_behind", "vehicle_mouse_look", "special_control_left",
    "special_control_right", "special_control_down", "special_control_up", "aim_weapon", "conversation_yes",
    "conversation_no", "group_control_forwards", "group_control_back",
};

// CClient.cpp:66-87: the commands registered with bModCommand = true.
enum ModCommand : size_t {
    kChatbox, kVoicePtt, kEnterPassenger, kRadioNext, kRadioPrevious, kRadar, kRadarZoomIn, kRadarZoomOut,
    kRadarMoveNorth, kRadarMoveSouth, kRadarMoveEast, kRadarMoveWest, kRadarAttach, kRadarOpacityDown,
    kRadarOpacityUp, kRadarHelp, kMsgTarget, kVehicleNextWeapon, kVehiclePreviousWeapon, kSinfo, kTextscale,
    kModCommandCount
};
constexpr std::array<const char*, kModCommandCount> kModNames = {
    "chatbox", "voiceptt", "enter_passenger", "radio_next", "radio_previous", "radar", "radar_zoom_in",
    "radar_zoom_out", "radar_move_north", "radar_move_south", "radar_move_east", "radar_move_west", "radar_attach",
    "radar_opacity_down", "radar_opacity_up", "radar_help", "msg_target", "vehicle_next_weapon",
    "vehicle_previous_weapon", "sinfo", "textscale",
};

std::mutex g_mutex;
std::array<bool, kModCommandCount> g_modEnabled = [] {
    std::array<bool, kModCommandCount> all{};
    all.fill(true);
    return all;
}();
std::atomic<uint64_t> g_gtaDisabled{0};

constexpr uint64_t Bit(Gta control) { return uint64_t{1} << static_cast<unsigned>(control); }

int FindMod(const char* name) {
    if (!name) return -1;
    for (size_t i = 0; i < kModNames.size(); ++i)
        if (strcasecmp(kModNames[i], name) == 0) return static_cast<int>(i);
    return -1;
}

// The activity owns the chat input and the F11 player map, so it is told after every change (game line kind 20,
// "name|0/1"). Every other mod command has no phone consumer yet and is state only.
void PublishToActivity() {
    bool chat, radar;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        chat = g_modEnabled[kChatbox];
        radar = g_modEnabled[kRadar];
    }
    WorldRPG::JniBridge::PostGameLine(20, radar ? "radar|1" : "radar|0");
    WorldRPG::JniBridge::PostGameLine(20, chat ? "chatbox|1" : "chatbox|0");
}

struct Policy {
    Gta negative;  // control that owns the negative half of an axis (or the whole button)
    Gta positive;  // control that owns the positive half; equal to negative for a button
    bool axis;
};

constexpr Policy Button(Gta control) { return {control, control, false}; }
constexpr Policy Axis(Gta negative, Gta positive) { return {negative, positive, true}; }

// Sign convention is the GTA pad's (CClientPad.cpp:803-870): LeftStickX < 0 is left, LeftStickY < 0 is forwards.
constexpr std::array<Policy, kGetterCount> kPolicy = [] {
    std::array<Policy, kGetterCount> p{};
    auto set = [&p](Getter g, Policy v) { p[static_cast<size_t>(g)] = v; };
    set(Getter::PedWalkLeftRight, Axis(Gta::Left, Gta::Right));
    set(Getter::PedWalkUpDown, Axis(Gta::Forwards, Gta::Backwards));
    set(Getter::SteeringLeftRight, Axis(Gta::VehicleLeft, Gta::VehicleRight));
    set(Getter::SteeringUpDown, Axis(Gta::SteerForward, Gta::SteerBack));
    set(Getter::Accelerate, Button(Gta::Accelerate));
    set(Getter::AccelerateJustDown, Button(Gta::Accelerate));
    set(Getter::Brake, Button(Gta::BrakeReverse));
    set(Getter::Jump, Button(Gta::Jump));
    set(Getter::JumpJustDown, Button(Gta::Jump));
    set(Getter::SwimJumpJustDown, Button(Gta::Jump));
    set(Getter::MeleeAttack, Button(Gta::Fire));
    set(Getter::MeleeAttackJustDown, Button(Gta::Fire));
    set(Getter::Duck, Button(Gta::Crouch));
    set(Getter::DuckJustDown, Button(Gta::Crouch));
    set(Getter::Sprint, Button(Gta::Sprint));
    set(Getter::SprintJustDown, Button(Gta::Sprint));
    set(Getter::Weapon, Button(Gta::Fire));
    set(Getter::WeaponJustDown, Button(Gta::Fire));
    set(Getter::ExitVehicle, Button(Gta::EnterExit));
    set(Getter::ExitVehicleJustDown, Button(Gta::EnterExit));
    set(Getter::Horn, Button(Gta::Horn));
    set(Getter::HornJustDown, Button(Gta::Horn));
    set(Getter::HandBrake, Button(Gta::Handbrake));
    set(Getter::LookBehindForPed, Button(Gta::LookBehind));
    set(Getter::LookBehindForCar, Button(Gta::VehicleLookBehind));
    set(Getter::LookLeft, Button(Gta::VehicleLookLeft));
    set(Getter::LookRight, Button(Gta::VehicleLookRight));
    set(Getter::Target, Button(Gta::AimWeapon));
    set(Getter::EnterTargeting, Button(Gta::AimWeapon));
    set(Getter::CycleWeaponLeftJustDown, Button(Gta::PreviousWeapon));
    set(Getter::CycleWeaponRightJustDown, Button(Gta::NextWeapon));
    set(Getter::CycleCameraModeDownJustDown, Button(Gta::ChangeCamera));
    set(Getter::ConversationYesJustDown, Button(Gta::ConversationYes));
    set(Getter::ConversationNoJustDown, Button(Gta::ConversationNo));
    set(Getter::GroupControlForwardJustDown, Button(Gta::GroupControlForwards));
    set(Getter::GroupControlBackJustDown, Button(Gta::GroupControlBack));
    set(Getter::GroupControlForward, Button(Gta::GroupControlForwards));
    set(Getter::GroupControlBack, Button(Gta::GroupControlBack));
    set(Getter::SniperZoomIn, Button(Gta::ZoomIn));
    set(Getter::SniperZoomOut, Button(Gta::ZoomOut));
    set(Getter::NextStationJustUp, Button(Gta::RadioNext));
    set(Getter::LastStationJustUp, Button(Gta::RadioPrevious));
    set(Getter::CarGunJustDown, Button(Gta::VehicleFire));
    set(Getter::CarGunFired, Button(Gta::VehicleFire));
    return p;
}();

}  // namespace

const char* GtaControlName(Gta control) {
    const size_t i = static_cast<size_t>(control);
    return i < kGtaControlCount ? kGtaNames[i] : nullptr;
}

Gta FindGtaControl(const char* name) {
    if (!name) return Gta::Count;
    for (size_t i = 0; i < kGtaControlCount; ++i)
        if (strcasecmp(kGtaNames[i], name) == 0) return static_cast<Gta>(i);
    return Gta::Count;
}

bool IsControlEnabled(const char* name) {
    const Gta gta = FindGtaControl(name);
    if (gta != Gta::Count) return !IsGtaControlDisabled(gta);
    return IsModCommandEnabled(name);
}

bool GetScriptControlEnabled(const char* name, bool& enabled) {
    const Gta gta = FindGtaControl(name);
    if (gta == Gta::Count) return false;
    enabled = !IsGtaControlDisabled(gta);
    return true;
}

bool IsModCommandEnabled(const char* name) {
    const int mod = FindMod(name);
    if (mod < 0) return false;
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_modEnabled[static_cast<size_t>(mod)];
}

bool SetControlEnabled(const char* name, bool enabled) {
    const Gta gta = FindGtaControl(name);
    if (gta != Gta::Count && IsGtaControlDisabled(gta) == enabled) {
        if (enabled) g_gtaDisabled.fetch_and(~Bit(gta));
        else g_gtaDisabled.fetch_or(Bit(gta));
        return true;
    }
    const int mod = FindMod(name);
    if (mod < 0) return false;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_modEnabled[static_cast<size_t>(mod)] = enabled;
    }
    PublishToActivity();
    return true;
}

void SetAllControlsEnabled(bool gtaControls, bool modCommands, bool enabled) {
    if (gtaControls) g_gtaDisabled.store(enabled ? 0 : (Bit(Gta::Count) - 1));
    if (modCommands) {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_modEnabled.fill(enabled);
    }
    PublishToActivity();
}

void ResetAll() { SetAllControlsEnabled(true, true, true); }

uint64_t DisabledGtaMask() { return g_gtaDisabled.load(std::memory_order_relaxed); }

bool IsGtaControlDisabled(Gta control) { return (DisabledGtaMask() & Bit(control)) != 0; }

int32_t MaskGetter(Getter getter, int32_t raw) {
    const uint64_t disabled = DisabledGtaMask();
    if (disabled == 0) return raw;
    const size_t index = static_cast<size_t>(getter);
    if (index >= kGetterCount) return raw;
    const Policy& policy = kPolicy[index];
    if (!policy.axis) return (disabled & Bit(policy.negative)) ? 0 : raw;
    // The stick getters return a short in w0; its upper half is not defined, so only the low 16 bits are read.
    const int16_t value = static_cast<int16_t>(raw);
    if (value < 0 && (disabled & Bit(policy.negative))) return 0;
    if (value > 0 && (disabled & Bit(policy.positive))) return 0;
    return raw;
}

}  // namespace WorldRPG::Controls
