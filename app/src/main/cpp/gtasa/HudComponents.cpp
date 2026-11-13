#include "HudComponents.h"
#include "GameEngine.h"

#include <cstdint>
#include <cstring>

#ifdef __ANDROID__
#include <dlfcn.h>
#include <sys/mman.h>
#include <unistd.h>
#include <mutex>
#include "../hook/MemoryPatcher.h"
#endif

namespace WorldRPG::Game {

#ifdef __ANDROID__
namespace {

constexpr std::uint32_t kNop = 0xD503201Fu;

// Call sites inside CWidgetPlayerInfo::Draw. Each is a BL whose result is not used:
//   clock   0x37d3c0  CFont::PrintString (the "%02d:%02d" string)
//   money   0x37d528  CFont::PrintString (the "$%08d" string)
//   health  0x37d558 / 0x37d5b0  RenderHealthBar (player 1 / player 2)
//   armour  0x37d580 / 0x37d5d8  RenderArmorBar
//   breath  0x37d60c  RenderBreathBar
//   weapon  0x37d634 / 0x37d668  DrawWeaponIcon
//   ammo    0x37d690 / 0x37d6c8  DrawAmmo
//   wanted  0x37d6d0  DrawWanted
struct Component {
    const char* name;
    std::uintptr_t sites[2];
    std::uint32_t saved[2];
    bool visible;
};

Component g_components[] = {
    {"clock", {0x37d3c0, 0}, {0, 0}, true},  {"money", {0x37d528, 0}, {0, 0}, true},
    {"health", {0x37d558, 0x37d5b0}, {0, 0}, true}, {"armour", {0x37d580, 0x37d5d8}, {0, 0}, true},
    {"breath", {0x37d60c, 0}, {0, 0}, true}, {"weapon", {0x37d634, 0x37d668}, {0, 0}, true},
    {"ammo", {0x37d690, 0x37d6c8}, {0, 0}, true}, {"wanted", {0x37d6d0, 0}, {0, 0}, true},
};
bool g_radarVisible = true;
std::mutex g_mutex;

bool WriteWord(std::uintptr_t address, std::uint32_t word) {
    const long pageLong = sysconf(_SC_PAGESIZE);
    const std::uintptr_t page = pageLong > 0 ? static_cast<std::uintptr_t>(pageLong) : 4096u;
    void* start = reinterpret_cast<void*>(address & ~(page - 1));
    if (mprotect(start, page, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) return false;
    std::memcpy(reinterpret_cast<void*>(address), &word, sizeof(word));
    mprotect(start, page, PROT_READ | PROT_EXEC);
    __builtin___clear_cache(reinterpret_cast<char*>(address), reinterpret_cast<char*>(address) + 4);
    return true;
}

// Caller holds g_mutex.
bool ApplyLocked(Component& c, bool visible) {
    const std::uintptr_t base = Hook::MemoryPatcher::getLibraryBaseAddress("libGTASA.so");
    if (!base) return false;
    bool ok = true;
    for (int i = 0; i < 2; ++i) {
        if (!c.sites[i]) continue;
        const std::uintptr_t address = base + c.sites[i];
        std::uint32_t current = 0;
        std::memcpy(&current, reinterpret_cast<const void*>(address), sizeof(current));
        if (!visible) {
            if (current == kNop) continue;
            // Only a BL is replaced; anything else means this is not the library these offsets are for.
            if ((current & 0xFC000000u) != 0x94000000u) {
                ENGINE_LOGE("HUD component %s: unexpected word 0x%08x at 0x%lx", c.name, current,
                            static_cast<unsigned long>(c.sites[i]));
                ok = false;
                continue;
            }
            c.saved[i] = current;
            ok = WriteWord(address, kNop) && ok;
        } else if (current == kNop && c.saved[i]) {
            ok = WriteWord(address, c.saved[i]) && ok;
        }
    }
    if (ok) c.visible = visible;
    return ok;
}

bool SetRadarLocked(bool visible) {
    void* handle = dlopen("libGTASA.so", RTLD_NOLOAD | RTLD_GLOBAL);
    auto* flag = handle ? static_cast<unsigned char*>(dlsym(handle, "_ZN4CHud23bScriptDontDisplayRadarE")) : nullptr;
    if (!flag) return false;
    *flag = visible ? 0 : 1;
    g_radarVisible = visible;
    return true;
}

}  // namespace

bool SetHudComponentVisible(const char* component, bool visible) {
    if (!component) return false;
    std::lock_guard<std::mutex> lock(g_mutex);
    if (std::strcmp(component, "all") == 0) {
        bool ok = SetRadarLocked(visible);
        for (Component& c : g_components) ok = ApplyLocked(c, visible) && ok;
        return ok;
    }
    if (std::strcmp(component, "radar") == 0) return SetRadarLocked(visible);
    for (Component& c : g_components) {
        if (std::strcmp(component, c.name) == 0) return ApplyLocked(c, visible);
    }
    return false;
}

bool IsHudComponentVisible(const char* component) {
    if (!component) return false;
    std::lock_guard<std::mutex> lock(g_mutex);
    if (std::strcmp(component, "radar") == 0) return g_radarVisible;
    for (const Component& c : g_components) {
        if (std::strcmp(component, c.name) == 0) return c.visible;
    }
    return true;
}

void ResetHudComponents() {
    std::lock_guard<std::mutex> lock(g_mutex);
    for (Component& c : g_components) {
        if (!c.visible) ApplyLocked(c, true);
    }
    if (!g_radarVisible) SetRadarLocked(true);
}

#else
bool SetHudComponentVisible(const char*, bool) { return true; }
bool IsHudComponentVisible(const char*) { return true; }
void ResetHudComponents() {}
#endif

}  // namespace WorldRPG::Game
