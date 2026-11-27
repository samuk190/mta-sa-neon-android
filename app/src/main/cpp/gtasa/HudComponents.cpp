#include "HudComponents.h"
#include "GameEngine.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>

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
constexpr std::uint32_t kRet = 0xD65F03C0u;
constexpr std::uint32_t kCrosshairPrologue = 0xD10283FFu; // sub sp, sp, #0xa0

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
    bool applied = false;
};

Component g_components[] = {
    {"clock", {0x37d3c0, 0}, {0, 0}, true},  {"money", {0x37d528, 0}, {0, 0}, true},
    {"health", {0x37d558, 0x37d5b0}, {0, 0}, true}, {"armour", {0x37d580, 0x37d5d8}, {0, 0}, true},
    {"breath", {0x37d60c, 0}, {0, 0}, true}, {"weapon", {0x37d634, 0x37d668}, {0, 0}, true},
    {"ammo", {0x37d690, 0x37d6c8}, {0, 0}, true}, {"wanted", {0x37d6d0, 0}, {0, 0}, true},
};
bool g_radarVisible = true;
bool g_crosshairVisible = true;
std::mutex g_mutex;

bool WriteWord(std::uintptr_t address, std::uint32_t word) {
    const long pageLong = sysconf(_SC_PAGESIZE);
    const std::uintptr_t page = pageLong > 0 ? static_cast<std::uintptr_t>(pageLong) : 4096u;
    void* start = reinterpret_cast<void*>(address & ~(page - 1));
    if (mprotect(start, page, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) return false;
    __atomic_store_n(reinterpret_cast<std::uint32_t*>(address), word, __ATOMIC_RELEASE);
    mprotect(start, page, PROT_READ | PROT_EXEC);
    __builtin___clear_cache(reinterpret_cast<char*>(address), reinterpret_cast<char*>(address) + 4);
    return true;
}

// Caller holds g_mutex.
bool ApplyLocked(Component& c, bool visible) {
    if (c.applied && c.visible == visible) return true;
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
    c.applied = ok;
    if (ok) c.visible = visible;
    return ok;
}

bool SetCrosshairLocked(bool visible) {
#if defined(__aarch64__)
    if (g_crosshairVisible == visible) return true;
    void* lib = dlopen("libGTASA.so", RTLD_NOW | RTLD_NOLOAD | RTLD_GLOBAL);
    if (!lib) return false;
    auto* draw = static_cast<const std::uint32_t*>(dlsym(lib, "_ZN4CHud14DrawCrossHairsEv"));
    const std::uint32_t expected = visible ? kRet : kCrosshairPrologue;
    // CHud::DrawCrossHairs at 0x51c694 (2.10 ARM64). Ret at the entry
    // leaves the stack intact and can be swapped with a single aligned store.
    const bool applied = draw && draw[0] == expected && draw[1] == 0x6D023BEFu
        && WriteWord(reinterpret_cast<uintptr_t>(draw), visible ? kCrosshairPrologue : kRet);
    dlclose(lib);
    if (applied) g_crosshairVisible = visible;
    return applied;
#else
    return false;
#endif
}

bool SetRadarLocked(bool visible) {
    static unsigned char* flag = nullptr;
    if (!flag) {
        void* handle = dlopen("libGTASA.so", RTLD_NOLOAD | RTLD_NOW);
        if (!handle) return false;
        flag = static_cast<unsigned char*>(dlsym(handle, "_ZN4CHud23bScriptDontDisplayRadarE"));
        dlclose(handle);
        if (!flag) return false;
    }
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
#if defined(__aarch64__)
        ok = SetCrosshairLocked(visible) && ok;
#endif
        for (Component& c : g_components) ok = ApplyLocked(c, visible) && ok;
        return ok;
    }
    if (std::strcmp(component, "radar") == 0) return SetRadarLocked(visible);
    if (std::strcmp(component, "crosshair") == 0) return SetCrosshairLocked(visible);
    for (Component& c : g_components) {
        if (std::strcmp(component, c.name) == 0) return ApplyLocked(c, visible);
    }
    return false;
}

bool IsHudComponentVisible(const char* component) {
    if (!component) return false;
    std::lock_guard<std::mutex> lock(g_mutex);
    if (std::strcmp(component, "radar") == 0) return g_radarVisible;
    if (std::strcmp(component, "crosshair") == 0) return g_crosshairVisible;
    for (const Component& c : g_components) {
        if (std::strcmp(component, c.name) == 0) return c.visible;
    }
    return true;
}

bool GetMobileCrosshairPosition(float& x, float& y) {
    // CHud::DrawCrossHairs multiplies these two values by the render
    // dimensions. Keep them normalized for the Lua logical viewport.
    static const auto position = [] {
        struct Position { const float* x; const float* y; };
        void* lib = dlopen("libGTASA.so", RTLD_NOW | RTLD_NOLOAD | RTLD_GLOBAL);
        if (!lib) return Position{};
        Position pos{
            static_cast<const float*>(dlsym(lib, "_ZN7CCamera22m_f3rdPersonCHairMultXE")),
            static_cast<const float*>(dlsym(lib, "_ZN7CCamera22m_f3rdPersonCHairMultYE")),
        };
        dlclose(lib);
        return pos;
    }();
    if (!position.x || !position.y) return false;
    x = *position.x;
    y = *position.y;
    return std::isfinite(x) && std::isfinite(y) && x > 0.f && x < 1.f && y > 0.f && y < 1.f;
}

void ResetHudComponents() {
    std::lock_guard<std::mutex> lock(g_mutex);
    for (Component& c : g_components) {
        if (!c.visible) ApplyLocked(c, true);
    }
    if (!g_radarVisible) SetRadarLocked(true);
    if (!g_crosshairVisible) SetCrosshairLocked(true);
}

#else
// Host build: remembers what was set so tests can read it back. Every name is accepted.
namespace {
std::map<std::string, bool> g_hostVisible;
}
bool SetHudComponentVisible(const char* component, bool visible) {
    if (!component || !*component) return false;
    g_hostVisible[component] = visible;
    return true;
}
bool IsHudComponentVisible(const char* component) {
    const auto found = component ? g_hostVisible.find(component) : g_hostVisible.end();
    return found == g_hostVisible.end() ? true : found->second;
}
bool GetMobileCrosshairPosition(float&, float&) { return false; }
void ResetHudComponents() { g_hostVisible.clear(); }
#endif

}  // namespace WorldRPG::Game
