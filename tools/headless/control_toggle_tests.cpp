// Host checks for toggleControl / toggleAllControls / isControlEnabled, the two server RPCs, and the CPad getter
// masking policy. Desktop reference: CKeyBinds.cpp 1233-1310, CClientPad.cpp 781-870, CInputRPCs.cpp 180-206.
#include "shared/ControlToggles.h"
#include "shared/LuaVMStub.h"
#include "shared/MtaIds.h"
#include "shared/PacketHandler.h"
#include "shared/ServerWorld.h"
#include "net/raknet/RakNetWrapper.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace WorldRPG::JniBridge {
std::vector<std::string> TakeControlLines();
}

using namespace WorldRPG;
namespace C = WorldRPG::Controls;

namespace {
unsigned checks = 0;
void Check(bool yes, const char* label) {
    ++checks;
    if (!yes) {
        std::fprintf(stderr, "FAIL: %s\n", label);
        std::exit(1);
    }
}

void Lua(Script::LuaVMStub& vm, const std::string& script) {
    auto r = vm.executeBuffer(script.data(), script.size(), "control-test");
    if (!r.success) std::fprintf(stderr, "%s\n", r.errorMessage.c_str());
    Check(r.success, "Lua control behavior");
}

bool ToggleRpc(Sync::PacketHandler& h, const std::string& name, uint8_t enabled) {
    Net::BitStreamWriter w;
    w.Write(MtaIds::Packet::Lua);
    w.Write(MtaIds::Rpc::ToggleControlAbility);
    w.Write(uint8_t(name.size()));
    if (!name.empty()) w.WriteBits(reinterpret_cast<const uint8_t*>(name.data()), unsigned(name.size() * 8));
    w.Write(enabled);
    return h.handlePacket(99, w.GetData(), w.GetByteLength(), w.GetBitLength());
}

bool ToggleAllRpc(Sync::PacketHandler& h, uint8_t gta, uint8_t mta, uint8_t enabled) {
    Net::BitStreamWriter w;
    w.Write(MtaIds::Packet::Lua);
    w.Write(MtaIds::Rpc::ToggleAllControlAbility);
    w.Write(gta);
    w.Write(mta);
    w.Write(enabled);
    return h.handlePacket(99, w.GetData(), w.GetByteLength(), w.GetBitLength());
}

void Tables() {
    Check(C::kGtaControlCount == 44, "44 GTA controls as in CKeyBinds g_bcControls");
    Check(std::string(C::GtaControlName(C::Gta::Fire)) == "fire", "first control is fire");
    Check(std::string(C::GtaControlName(C::Gta::GroupControlBack)) == "group_control_back", "last control");
    Check(C::FindGtaControl("FIRE") == C::Gta::Fire, "control names are case-insensitive");
    Check(C::FindGtaControl("radar") == C::Gta::Count, "radar is a mod command, not a GTA control");
    Check(C::FindGtaControl(nullptr) == C::Gta::Count, "null name");
    for (size_t i = 0; i < C::kGtaControlCount; ++i)
        Check(C::FindGtaControl(C::GtaControlName(static_cast<C::Gta>(i))) == static_cast<C::Gta>(i), "name round trip");
}

void State() {
    C::ResetAll();
    JniBridge::TakeControlLines();
    Check(C::IsControlEnabled("fire") && C::IsControlEnabled("radar") && C::IsControlEnabled("chatbox"), "all start enabled");
    Check(!C::IsControlEnabled("not_a_control"), "an unknown name reads false (CKeyBinds::IsControlEnabled)");

    Check(C::SetControlEnabled("fire", false), "disable a GTA control");
    Check(!C::IsControlEnabled("fire") && C::IsGtaControlDisabled(C::Gta::Fire), "state and mask agree");
    Check(C::DisabledGtaMask() == 1u, "only bit 0 set");
    Check(!C::SetControlEnabled("fire", false), "repeating the same state returns false (CKeyBinds.cpp:1254-1263)");
    Check(C::SetControlEnabled("FIRE", true) && C::IsControlEnabled("fire"), "enable again, any case");
    Check(!C::SetControlEnabled("not_a_control", false), "an unknown control returns false");

    bool enabled = false;
    Check(C::GetScriptControlEnabled("jump", enabled) && enabled, "script getter, GTA control");
    Check(!C::GetScriptControlEnabled("radar", enabled), "script getter does not see mod commands");

    JniBridge::TakeControlLines();
    Check(C::SetControlEnabled("radar", false) && !C::IsControlEnabled("radar"), "radar is a mod command");
    auto lines = JniBridge::TakeControlLines();
    Check(lines.size() == 2 && lines[0] == "radar|0" && lines[1] == "chatbox|1", "activity told about radar");
    Check(C::SetControlEnabled("chatbox", false), "chatbox is a mod command");
    lines = JniBridge::TakeControlLines();
    Check(lines.size() == 2 && lines[1] == "chatbox|0", "activity told about chatbox");
    Check(C::IsModCommandEnabled("radar_zoom_in") && !C::IsModCommandEnabled("fire"), "mod command lookup");
    Check(C::SetControlEnabled("radar_zoom_in", false) && !C::IsControlEnabled("radar_zoom_in"), "other mod commands keep state");

    // radio_next is both a GTA control and a mod command: the GTA one changes first, the repeat falls through.
    C::ResetAll();
    Check(C::SetControlEnabled("radio_next", false) && C::IsGtaControlDisabled(C::Gta::RadioNext), "GTA radio_next first");
    Check(C::IsModCommandEnabled("radio_next"), "mod radio_next untouched");
    Check(C::SetControlEnabled("radio_next", false) && !C::IsModCommandEnabled("radio_next"), "repeat reaches the mod command");

    C::SetAllControlsEnabled(true, false, false);
    Check(C::DisabledGtaMask() == (uint64_t{1} << C::kGtaControlCount) - 1, "all 44 GTA controls disabled");
    Check(C::IsModCommandEnabled("voiceptt"), "gta only leaves mod commands alone");
    C::SetAllControlsEnabled(false, true, false);
    Check(!C::IsModCommandEnabled("voiceptt") && C::DisabledGtaMask() != 0, "mta only touches mod commands");
    C::SetAllControlsEnabled(true, true, true);
    Check(C::DisabledGtaMask() == 0 && C::IsModCommandEnabled("voiceptt") && C::IsModCommandEnabled("chatbox"), "enable all");
    C::SetAllControlsEnabled(true, true, false);
    C::ResetAll();
    Check(C::DisabledGtaMask() == 0 && C::IsControlEnabled("radar"), "ResetAll restores everything");
    lines = JniBridge::TakeControlLines();
    Check(!lines.empty() && lines.back() == "chatbox|1", "reset publishes the final state");
}

void Masking() {
    C::ResetAll();
    Check(C::MaskGetter(C::Getter::Weapon, 1) == 1 && C::MaskGetter(C::Getter::PedWalkLeftRight, -90) == -90,
        "nothing disabled: values pass through");

    C::SetControlEnabled("fire", false);
    Check(C::MaskGetter(C::Getter::Weapon, 1) == 0, "fire disables GetWeapon");
    Check(C::MaskGetter(C::Getter::WeaponJustDown, 1) == 0, "fire disables WeaponJustDown");
    Check(C::MaskGetter(C::Getter::MeleeAttack, 1) == 0 && C::MaskGetter(C::Getter::MeleeAttackJustDown, 1) == 0,
        "fire disables melee");
    Check(C::MaskGetter(C::Getter::Jump, 1) == 1, "other buttons pass through");
    C::SetControlEnabled("fire", true);

    // Axis: left is the negative half of the on-foot X axis, right the positive half.
    C::SetControlEnabled("left", false);
    Check(C::MaskGetter(C::Getter::PedWalkLeftRight, -128) == 0, "left disabled clears a left stick");
    Check(C::MaskGetter(C::Getter::PedWalkLeftRight, 77) == 77, "left disabled keeps a right stick");
    C::SetControlEnabled("left", true);
    C::SetControlEnabled("right", false);
    Check(C::MaskGetter(C::Getter::PedWalkLeftRight, 77) == 0 && C::MaskGetter(C::Getter::PedWalkLeftRight, -77) == -77,
        "right disabled clears only the positive half");
    C::SetControlEnabled("right", true);
    C::SetControlEnabled("forwards", false);
    Check(C::MaskGetter(C::Getter::PedWalkUpDown, -128) == 0 && C::MaskGetter(C::Getter::PedWalkUpDown, 128) == 128,
        "forwards is stick Y < 0");
    C::SetControlEnabled("forwards", true);
    C::SetControlEnabled("backwards", false);
    Check(C::MaskGetter(C::Getter::PedWalkUpDown, 128) == 0 && C::MaskGetter(C::Getter::PedWalkUpDown, -128) == -128,
        "backwards is stick Y > 0");
    C::SetControlEnabled("backwards", true);

    C::SetControlEnabled("vehicle_left", false);
    Check(C::MaskGetter(C::Getter::SteeringLeftRight, -100) == 0 && C::MaskGetter(C::Getter::SteeringLeftRight, 100) == 100,
        "vehicle_left");
    C::SetControlEnabled("vehicle_left", true);
    C::SetControlEnabled("steer_back", false);
    Check(C::MaskGetter(C::Getter::SteeringUpDown, 50) == 0 && C::MaskGetter(C::Getter::SteeringUpDown, -50) == -50,
        "steer_back");
    C::SetControlEnabled("steer_back", true);

    // On-foot walking is not affected by the vehicle controls and the other way round.
    C::SetControlEnabled("vehicle_left", false);
    Check(C::MaskGetter(C::Getter::PedWalkLeftRight, -100) == -100, "vehicle_left leaves the walk stick");
    C::SetControlEnabled("vehicle_left", true);

    C::SetControlEnabled("accelerate", false);
    Check(C::MaskGetter(C::Getter::Accelerate, 255) == 0 && C::MaskGetter(C::Getter::AccelerateJustDown, 1) == 0, "accelerate");
    C::SetControlEnabled("accelerate", true);
    C::SetControlEnabled("brake_reverse", false);
    Check(C::MaskGetter(C::Getter::Brake, 255) == 0, "brake_reverse");
    C::SetControlEnabled("brake_reverse", true);
    C::SetControlEnabled("enter_exit", false);
    Check(C::MaskGetter(C::Getter::ExitVehicle, 1) == 0 && C::MaskGetter(C::Getter::ExitVehicleJustDown, 1) == 0, "enter_exit");
    C::SetControlEnabled("enter_exit", true);
    C::SetControlEnabled("crouch", false);
    Check(C::MaskGetter(C::Getter::Duck, 1) == 0 && C::MaskGetter(C::Getter::DuckJustDown, 1) == 0, "crouch");
    C::SetControlEnabled("crouch", true);
    C::SetControlEnabled("aim_weapon", false);
    Check(C::MaskGetter(C::Getter::Target, 1) == 0, "aim_weapon");
    C::SetControlEnabled("aim_weapon", true);

    C::SetAllControlsEnabled(true, true, false);
    for (size_t i = 0; i < C::kGetterCount; ++i) {
        Check(C::MaskGetter(static_cast<C::Getter>(i), 1) == 0, "every masked getter is silent with all controls off");
        Check(C::MaskGetter(static_cast<C::Getter>(i), -1) == 0, "negative values too");
    }
    C::ResetAll();
}

void LuaApi() {
    C::ResetAll();
    Script::LuaVMStub vm;
    Check(vm.initialize(), "VM initializes");
    Lua(vm, R"LUA(
        assert(isControlEnabled('fire') == true)
        assert(toggleControl('fire', false) == true)
        assert(isControlEnabled('fire') == false)
        assert(isControlEnabled('FIRE') == false)
        assert(toggleControl('fire', false) == false, 'same state again is false')
        assert(toggleControl('nope', false) == false, 'unknown control is false')
        assert(isControlEnabled('nope') == false)
        assert(toggleControl('fire') == false, 'missing bool is an argument error')
        assert(toggleControl(false, true) == false, 'bad name type')
        assert(toggleControl('fire', true) == true)
        assert(isControlEnabled('radar') == false, 'isControlEnabled only reports GTA controls')
        assert(toggleControl('radar', false) == true)
        assert(isControlEnabled('radar') == false)
        assert(toggleControl('radar', true) == true)
        assert(toggleAllControls(false) == true)
        assert(isControlEnabled('jump') == false and isControlEnabled('walk') == false)
        assert(toggleAllControls(true, false, false) == true, 'mta/gta both false changes nothing')
        assert(isControlEnabled('jump') == false)
        assert(toggleAllControls(true, true, false) == true)
        assert(isControlEnabled('jump') == true)
        assert(toggleAllControls() == false, 'missing bool')
        assert(toggleAllControls(true, 'x') == false, 'bad gta flag')
        assert(toggleAllControls(true, nil, nil) == true, 'nil takes the defaults')
    )LUA");
    Check(C::DisabledGtaMask() == 0, "script left every GTA control enabled");

    Lua(vm, "assert(toggleAllControls(false, true, true)); assert(toggleControl('chatbox', false))");
    Check(C::DisabledGtaMask() != 0 && !C::IsControlEnabled("chatbox"), "script drives the shared state");
    C::ResetAll();
    vm.shutdown();
}

void Rpcs() {
    C::ResetAll();
    Sync::PacketHandler handler;
    Check(ToggleRpc(handler, "jump", 0), "TOGGLE_CONTROL_ABILITY is handled");
    Check(!C::IsControlEnabled("jump"), "jump disabled by the server");
    Check(ToggleRpc(handler, "jump", 1) && C::IsControlEnabled("jump"), "enabled by the server");
    Check(ToggleRpc(handler, "radar", 0) && !C::IsControlEnabled("radar"), "the server can disable radar");
    Check(ToggleRpc(handler, "", 0), "an empty name is read and ignored");
    Check(ToggleRpc(handler, "jump", 2) && !C::IsControlEnabled("jump"), "enabled counts only when exactly 1");
    Check(ToggleRpc(handler, "no_such_control", 0), "unknown control is consumed");

    Check(ToggleAllRpc(handler, 1, 1, 0), "TOGGLE_ALL_CONTROL_ABILITY is handled");
    Check(C::DisabledGtaMask() != 0 && !C::IsModCommandEnabled("radar"), "everything off");
    Check(ToggleAllRpc(handler, 1, 0, 1), "gta only");
    Check(C::DisabledGtaMask() == 0 && !C::IsModCommandEnabled("radar"), "mta side untouched");
    Check(ToggleAllRpc(handler, 0, 1, 1) && C::IsModCommandEnabled("radar"), "mta only");

    // A truncated packet is rejected and changes nothing.
    {
        C::ResetAll();
        Net::BitStreamWriter w;
        w.Write(MtaIds::Packet::Lua);
        w.Write(MtaIds::Rpc::ToggleAllControlAbility);
        w.Write(uint8_t(1));
        Check(!handler.handlePacket(99, w.GetData(), w.GetByteLength(), w.GetBitLength()), "truncated toggle-all is rejected");
        Check(C::DisabledGtaMask() == 0, "and applies nothing");
    }
    C::ResetAll();
}

void SessionReset() {
    C::SetAllControlsEnabled(true, true, false);
    ServerWorld::ResetSession();
    Check(C::DisabledGtaMask() == 0 && C::IsControlEnabled("radar") && C::IsControlEnabled("chatbox"),
        "a new session enables every control");
}
}  // namespace

int main() {
    Tables();
    State();
    Masking();
    LuaApi();
    Rpcs();
    SessionReset();
    std::printf("control toggle tests: %u checks passed\n", checks);
    return 0;
}
