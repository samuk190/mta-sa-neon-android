#include "HardwareMouse.h"
#include "GameEngine.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>

#include <dlfcn.h>
#include <sys/mman.h>
#include <unistd.h>

namespace WorldRPG::Game {

#if defined(__ANDROID__) && defined(__aarch64__)
namespace {

struct PendingMouse {
    float x = 0, y = 0, wheel = 0;
    unsigned buttons = 0, pressed = 0;
    bool captured = false;
};

std::mutex g_mutex;
PendingMouse g_pending;
struct GameMouse {
    float x = 0, y = 0;
    unsigned buttons = 0;  // Game thread only.
    bool captured = false;
} g_frame;
std::atomic<unsigned> g_buttonStates{0};
std::atomic<float> g_wheel{0};
std::atomic<bool> g_captured{false};
std::atomic<bool> g_ready{false};

void (*g_updatePads)() = nullptr;
int (*g_pointerType)(int) = nullptr;
int (*g_pointerButton)(int, int) = nullptr;
float (*g_pointerWheel)(int) = nullptr;
int (*g_inputType)() = nullptr;
std::byte* g_mouseState = nullptr;
float* g_mouseDelta = nullptr;

void SampleMouse() {
    PendingMouse sample;
    {
        std::lock_guard lock(g_mutex);
        sample = g_pending;
        g_pending.x = g_pending.y = g_pending.wheel = 0;
        g_pending.pressed = 0;
    }
    // The game expects 2 for a new press, 3 for held, 0 for a release
    // and 1 for idle (CHIDKeyboard::InternalIsPressed/InternalIsReleased).
    unsigned states = 0;
    const unsigned buttons = sample.buttons | sample.pressed;
    for (unsigned i = 0; i < 3; ++i) {
        const bool wasDown = (g_frame.buttons & (1u << i)) != 0;
        const bool isDown = (buttons & (1u << i)) != 0;
        states |= (isDown ? (wasDown ? 3u : 2u) : (wasDown ? 0u : 1u)) << (2 * i);
    }
    g_buttonStates.store(states, std::memory_order_relaxed);
    g_wheel.store(sample.wheel, std::memory_order_relaxed);
    g_captured.store(sample.captured, std::memory_order_release);
    g_frame = {sample.x, sample.y, buttons, sample.captured};
}

int PointerType(int pointer) {
    if (pointer == 0 && g_captured.load(std::memory_order_acquire)) return 1; // mouse
    return g_pointerType(pointer);
}

int PointerButton(int pointer, int button) {
    if (pointer == 0 && button >= 0 && button < 3 && g_captured.load(std::memory_order_acquire))
        return (g_buttonStates.load(std::memory_order_relaxed) >> (2 * button)) & 3u;
    return g_pointerButton(pointer, button);
}

float PointerWheel(int pointer) {
    if (pointer == 0 && g_captured.load(std::memory_order_acquire))
        return g_wheel.load(std::memory_order_relaxed);
    return g_pointerWheel(pointer);
}

void UpdatePads() {
    SampleMouse();
    g_updatePads();
    if (g_inputType() != 2) return; // Preserve touch and gamepad camera input.
    // GTA's PC keyboard branch subtracts the screen center from an absolute
    // touch slot. With no real mouse in that slot it can spin the camera.
    // CPad::UpdateMouse copies vecMouseDelta to NewMouseControllerState +0xc/+0x10.
    const float x = g_frame.captured ? g_frame.x : 0.f;
    const float y = g_frame.captured ? g_frame.y : 0.f;
    g_mouseDelta[0] = x;
    g_mouseDelta[1] = y;
    const float motion[] = {x, y};
    std::memcpy(g_mouseState + 0xc, motion, sizeof(motion));
}

struct Import {
    const char* symbol;
    uintptr_t offset;
    void* replacement;
    void* original = nullptr;
    void** slot = nullptr;
};

// libGTASA 2.10 ARM64 R_AARCH64_JUMP_SLOT entries. Compare every entry with
// dlsym before changing the RELRO pages, as in the server-session dispatcher.
bool BindImports(void* library, std::array<Import, 4>& imports) {
    const long pageSize = sysconf(_SC_PAGESIZE);
    if (pageSize <= 0) return false;
    std::array<void*, 4> pages{};
    size_t numPages = 0;
    for (auto& import : imports) {
        import.original = dlsym(library, import.symbol);
        Dl_info info{};
        if (!import.original || !dladdr(import.original, &info)) return false;
        const auto addr = reinterpret_cast<uintptr_t>(info.dli_fbase) + import.offset;
        import.slot = reinterpret_cast<void**>(addr);
        if (*import.slot != import.original) return false;
        auto* page = reinterpret_cast<void*>(addr & ~(static_cast<uintptr_t>(pageSize) - 1));
        if (std::find(pages.begin(), pages.begin() + numPages, page) == pages.begin() + numPages)
            pages[numPages++] = page;
    }

    size_t writable = 0;
    for (; writable < numPages; ++writable) {
        if (mprotect(pages[writable], pageSize, PROT_READ | PROT_WRITE) != 0) break;
    }
    const bool ok = writable == numPages;
    if (ok) {
        for (const auto& import : imports) __atomic_store_n(import.slot, import.replacement, __ATOMIC_RELEASE);
    }
    for (size_t i = 0; i < writable; ++i) {
        if (mprotect(pages[i], pageSize, PROT_READ) != 0)
            ENGINE_LOGE("Hardware mouse: could not restore GOT protection");
    }
    return ok;
}

}  // namespace

bool InstallHardwareMouse() {
    if (HardwareMouseReady()) return true;
    void* lib = dlopen("libGTASA.so", RTLD_NOW | RTLD_NOLOAD | RTLD_GLOBAL);
    if (!lib) return false;
    std::array<Import, 4> imports{{
        // CGame::Process calls this via PLT at 0x4d62f0, once per gameplay update.
        {"_ZN4CPad10UpdatePadsEv", 0x841480, reinterpret_cast<void*>(&UpdatePads)},
        {"_Z17OS_PointerGetTypei", 0x845750, reinterpret_cast<void*>(&PointerType)},
        {"_Z20LIB_PointerGetButtonii", 0x842fd8, reinterpret_cast<void*>(&PointerButton)},
        {"_Z19LIB_PointerGetWheeli", 0x83dcd0, reinterpret_cast<void*>(&PointerWheel)},
    }};
    g_inputType = reinterpret_cast<decltype(g_inputType)>(dlsym(lib, "_ZN4CHID12GetInputTypeEv"));
    g_mouseState = static_cast<std::byte*>(dlsym(lib, "_ZN4CPad23NewMouseControllerStateE"));
    g_mouseDelta = static_cast<float*>(dlsym(lib, "_ZN4CPad13vecMouseDeltaE"));
    const auto* updateMouse = static_cast<const uint32_t*>(dlsym(lib, "_ZN4CPad11UpdateMouseEv"));
    const bool verified = g_inputType && g_mouseState && g_mouseDelta && updateMouse
        && updateMouse[0x38 / 4] == 0x2901a289 // stp w9, w8, [x20, #0xc]
        && updateMouse[0x114 / 4] == 0xb900110b; // str w11, [x8, #0x10]
    // JNI_OnLoad runs before the GTA game thread can reach the patched entries.
    const bool installed = verified && BindImports(lib, imports);
    if (installed) {
        g_updatePads = reinterpret_cast<decltype(g_updatePads)>(imports[0].original);
        g_pointerType = reinterpret_cast<decltype(g_pointerType)>(imports[1].original);
        g_pointerButton = reinterpret_cast<decltype(g_pointerButton)>(imports[2].original);
        g_pointerWheel = reinterpret_cast<decltype(g_pointerWheel)>(imports[3].original);
        g_ready.store(true, std::memory_order_release);
    } else {
        ENGINE_LOGE("Hardware mouse unavailable: GTA 2.10 input ABI did not match");
    }
    dlclose(lib);
    return installed;
}

bool HardwareMouseReady() {
    return g_ready.load(std::memory_order_acquire);
}

void QueueHardwareMouse(float x, float y, unsigned buttons, float wheel, bool captured) {
    if (!HardwareMouseReady()) return;
    std::lock_guard lock(g_mutex);
    if (!captured) {
        g_pending = {};
        return;
    }
    g_pending.captured = true;
    if (std::isfinite(x)) g_pending.x = std::clamp(g_pending.x + x, -300.f, 300.f);
    if (std::isfinite(y)) g_pending.y = std::clamp(g_pending.y + y, -300.f, 300.f);
    if (std::isfinite(wheel)) g_pending.wheel = std::clamp(g_pending.wheel + wheel, -4.f, 4.f);
    buttons &= 7u;
    g_pending.pressed |= buttons & ~g_pending.buttons;
    g_pending.buttons = buttons;
}

#else
bool InstallHardwareMouse() { return false; }
bool HardwareMouseReady() { return false; }
void QueueHardwareMouse(float, float, unsigned, float, bool) {}
#endif

}  // namespace WorldRPG::Game
