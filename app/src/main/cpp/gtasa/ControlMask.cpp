#include "ControlMask.h"
#include "GameEngine.h"
#include "GameImports.h"
#include "PlayerPedPort.h"
#include "../shared/ControlToggles.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace WorldRPG::Game {

#if defined(__ANDROID__) && defined(__aarch64__)
namespace {

using Controls::Getter;
using Original = int32_t (*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);

struct Row {
    Getter getter;
    const char* symbol;
    uintptr_t slot;  // R_AARCH64_JUMP_SLOT offset in libGTASA 2.10 arm64
};

// Every row is a CPad member that returns an integer or bool in w0 and takes no float argument, so one wrapper
// shape serves them all: the six integer argument registers are passed through untouched, the original runs, and
// only w0 is replaced. The slots come from `readelf -r libGTASA.so`; BindImports compares each with dlsym.
constexpr std::array<Row, 43> kRows = {{
    {Getter::PedWalkLeftRight, "_ZN4CPad19GetPedWalkLeftRightEv", 0x841dd0},
    {Getter::PedWalkUpDown, "_ZN4CPad16GetPedWalkUpDownEv", 0x840ec0},
    {Getter::SteeringLeftRight, "_ZN4CPad20GetSteeringLeftRightEv", 0x8467f8},
    {Getter::SteeringUpDown, "_ZN4CPad17GetSteeringUpDownEv", 0x844b98},
    {Getter::Accelerate, "_ZN4CPad13GetAccelerateEv", 0x8479a0},
    {Getter::AccelerateJustDown, "_ZN4CPad21GetAccelerateJustDownEv", 0x8460d0},
    {Getter::Brake, "_ZN4CPad8GetBrakeEv", 0x83e358},
    {Getter::Jump, "_ZN4CPad7GetJumpEv", 0x83fb98},
    {Getter::JumpJustDown, "_ZN4CPad12JumpJustDownEv", 0x840808},
    {Getter::SwimJumpJustDown, "_ZN4CPad16SwimJumpJustDownEv", 0x846c68},
    {Getter::MeleeAttack, "_ZN4CPad14GetMeleeAttackEv", 0x842648},
    {Getter::MeleeAttackJustDown, "_ZN4CPad19MeleeAttackJustDownEv", 0x842190},
    {Getter::Duck, "_ZN4CPad7GetDuckEv", 0x83ef98},
    {Getter::DuckJustDown, "_ZN4CPad12DuckJustDownEP4CPed", 0x844488},
    {Getter::Sprint, "_ZN4CPad9GetSprintEi", 0x8418b8},
    {Getter::SprintJustDown, "_ZN4CPad14SprintJustDownEv", 0x841170},
    {Getter::Weapon, "_ZN4CPad9GetWeaponEP4CPedb", 0x844f50},
    {Getter::WeaponJustDown, "_ZN4CPad14WeaponJustDownEP4CPedbb", 0x848150},
    {Getter::ExitVehicle, "_ZN4CPad14GetExitVehicleEv", 0x843ed8},
    {Getter::ExitVehicleJustDown, "_ZN4CPad19ExitVehicleJustDownEbP8CVehiclebRK7CVector", 0x83e2d8},
    {Getter::Horn, "_ZN4CPad7GetHornEb", 0x8451c0},
    {Getter::HornJustDown, "_ZN4CPad12HornJustDownEv", 0x844d68},
    {Getter::HandBrake, "_ZN4CPad12GetHandBrakeEv", 0x840c28},
    {Getter::LookBehindForPed, "_ZN4CPad19GetLookBehindForPedEv", 0x840df0},
    {Getter::LookBehindForCar, "_ZN4CPad19GetLookBehindForCarEv", 0x83ea78},
    {Getter::LookLeft, "_ZN4CPad11GetLookLeftEb", 0x845588},
    {Getter::LookRight, "_ZN4CPad12GetLookRightEb", 0x843890},
    {Getter::Target, "_ZN4CPad9GetTargetEb", 0x83dda0},
    {Getter::CycleWeaponLeftJustDown, "_ZN4CPad23CycleWeaponLeftJustDownEv", 0x83ef50},
    {Getter::CycleWeaponRightJustDown, "_ZN4CPad24CycleWeaponRightJustDownEv", 0x83fa40},
    {Getter::CycleCameraModeDownJustDown, "_ZN4CPad27CycleCameraModeDownJustDownEv", 0x840920},
    {Getter::ConversationYesJustDown, "_ZN4CPad23ConversationYesJustDownEv", 0x845c88},
    {Getter::ConversationNoJustDown, "_ZN4CPad22ConversationNoJustDownEv", 0x847870},
    {Getter::GroupControlForwardJustDown, "_ZN4CPad27GroupControlForwardJustDownEb", 0x841f98},
    {Getter::GroupControlBackJustDown, "_ZN4CPad24GroupControlBackJustDownEb", 0x83f3a0},
    {Getter::GroupControlForward, "_ZN4CPad22GetGroupControlForwardEbb", 0x8466c0},
    {Getter::GroupControlBack, "_ZN4CPad19GetGroupControlBackEbb", 0x842d70},
    {Getter::SniperZoomIn, "_ZN4CPad12SniperZoomInEv", 0x83e2f8},
    {Getter::SniperZoomOut, "_ZN4CPad13SniperZoomOutEv", 0x83e810},
    {Getter::NextStationJustUp, "_ZN4CPad17NextStationJustUpEv", 0x843578},
    {Getter::LastStationJustUp, "_ZN4CPad17LastStationJustUpEv", 0x83dfe0},
    {Getter::CarGunJustDown, "_ZN4CPad14CarGunJustDownEv", 0x842758},
    {Getter::CarGunFired, "_ZN4CPad14GetCarGunFiredEbb", 0x849750},
}};

std::array<Original, kRows.size()> g_original{};
std::atomic<bool> g_ready{false};

template <size_t I>
int32_t Gate(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    int32_t raw = 0;
    if constexpr (kRows[I].getter == Getter::Weapon) {
        ScopedWeaponGateCaller caller(reinterpret_cast<uintptr_t>(__builtin_return_address(0)));
        raw = g_original[I](a0, a1, a2, a3, a4, a5);
    } else {
        raw = g_original[I](a0, a1, a2, a3, a4, a5);
    }
    // A remote player's body runs CPlayerPed::ProcessControl on a swapped pad (PlayerPedPort.cpp
    // RemotePlayerProcessControl). That pad belongs to the remote player, so the local toggles do not apply to it.
    if (InRemoteControlContext()) return raw;
    return Controls::MaskGetter(kRows[I].getter, raw);
}

template <size_t... I>
std::array<Import, sizeof...(I)> MakeImports(std::index_sequence<I...>) {
    return {{Import{kRows[I].symbol, kRows[I].slot, reinterpret_cast<void*>(&Gate<I>)}...}};
}

}  // namespace

bool InstallControlMask() {
    if (ControlMaskReady()) return true;
    void* lib = dlopen("libGTASA.so", RTLD_NOW | RTLD_NOLOAD | RTLD_GLOBAL);
    if (!lib) return false;
    // The wrappers read g_original the moment a slot is stored, so it is filled from dlsym first. BindImports
    // refuses to patch any slot whose current value is not that same address.
    bool symbols = true;
    for (size_t i = 0; i < kRows.size(); ++i) {
        g_original[i] = reinterpret_cast<Original>(dlsym(lib, kRows[i].symbol));
        symbols = symbols && g_original[i] != nullptr;
    }
    auto imports = MakeImports(std::make_index_sequence<kRows.size()>{});
    // JNI_OnLoad runs before the GTA game thread can reach the patched entries.
    const bool installed = symbols && BindImports(lib, imports);
    if (installed) g_ready.store(true, std::memory_order_release);
    else ENGINE_LOGE("Control mask unavailable: a CPad getter slot did not match GTA 2.10");
    dlclose(lib);
    return installed;
}

bool ControlMaskReady() { return g_ready.load(std::memory_order_acquire); }

#else

bool InstallControlMask() { return false; }
bool ControlMaskReady() { return false; }

#endif

}  // namespace WorldRPG::Game
