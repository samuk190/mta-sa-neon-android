#include "VehiclePaint.h"

#ifdef __ANDROID__
#include <android/log.h>
#include <array>
#include <cstring>
#include <dlfcn.h>
#include <mutex>
#include <sys/mman.h>
#include <unordered_map>
#include <unistd.h>

namespace WorldRPG::Game::VehiclePaint {
namespace {
using Rgba = std::array<uint8_t, 4>;
using Palette = std::array<Rgba, 4>;
struct Paint {
    uint32_t id;
    Palette colors;
    bool firstDraw = true;
};
std::mutex mutex;
std::unordered_map<void*, Paint> paints;
void (*original)(void*) = nullptr;
uint8_t* indices = nullptr;
Rgba* palette = nullptr;

Rgba MaterialColor(VehicleRgb rgb) {
    // Desktop CVehicleSA::SetColor avoids the editable material marker colors.
    const uint32_t value = uint32_t(rgb[0]) << 16 | uint32_t(rgb[1]) << 8 | rgb[2];
    switch (value) {
        case 0xff00ff: case 0x00ffff: case 0xff00af: case 0xffaf00:
        case 0xb9ff00: case 0x00ffc8: case 0xff3c00: case 0x3cff00:
            for (auto& channel : rgb) channel |= 1;
    }
    return {rgb[0], rgb[1], rgb[2], 255};
}

void PaintMaterials(void* clump) {
    Palette draw;
    bool found = false;
    {
        std::lock_guard lock(mutex);
        auto it = paints.find(clump);
        if (it != paints.end()) {
            found = true;
            draw = it->second.colors;
            static unsigned samples = 0;
            if (it->second.firstDraw && samples < 16) {
                ++samples;
                it->second.firstDraw = false;
                __android_log_print(ANDROID_LOG_INFO, "WorldRPGNeon",
                    "Vehicle material id=%u primary=%u,%u,%u secondary=%u,%u,%u", it->second.id,
                    draw[0][0], draw[0][1], draw[0][2], draw[1][0], draw[1][1], draw[1][2]);
            }
        }
    }
    if (!found) return original(clump);

    // The GTA render thread copies palette entries into the materials during this call.
    // Desktop HOOK_VehCol/HOOK_VehColCB similarly gives each vehicle four RGB slots.
    Palette saved;
    std::array<uint8_t, 4> oldIndices;
    std::memcpy(saved.data(), palette, sizeof saved);
    std::memcpy(oldIndices.data(), indices, oldIndices.size());
    for (unsigned i = 0; i < draw.size(); ++i) {
        indices[i] = static_cast<uint8_t>(i);
        palette[i] = draw[i];
    }
    original(clump);
    std::memcpy(palette, saved.data(), sizeof saved);
    std::memcpy(indices, oldIndices.data(), oldIndices.size());
}
} // namespace

void Install(void* handle) {
    if (!handle || original) return;
    void* target = dlsym(handle, "_ZN17CVehicleModelInfo20SetEditableMaterialsEP7RpClump");
    indices = static_cast<uint8_t*>(dlsym(handle, "_ZN17CVehicleModelInfo13ms_currentColE"));
    palette = static_cast<Rgba*>(dlsym(handle, "_ZN17CVehicleModelInfo21ms_vehicleColourTableE"));
    Dl_info info{};
    if (!target || !indices || !palette || !dladdr(target, &info)) return;
    const auto base = reinterpret_cast<uintptr_t>(info.dli_fbase);
    // libGTASA 2.10 ARM64 SetupRender 0x6a5f1c loads the clump then jumps via PLT.
    // readelf: R_AARCH64_JUMP_SLOT SetEditableMaterials at 0x83dee0, inside GNU_RELRO.
    // The function uses ADRP in its prologue, so call its original entry directly.
    if (reinterpret_cast<uintptr_t>(target) != base + 0x4603b0) return;
    auto** slot = reinterpret_cast<void**>(base + 0x83dee0);
    const long size = sysconf(_SC_PAGESIZE);
    if (*slot != target || size <= 0) return;
    void* page = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(slot) & ~(uintptr_t(size) - 1));
    if (mprotect(page, size, PROT_READ | PROT_WRITE) != 0) return;
    original = reinterpret_cast<void (*)(void*)>(target);
    __atomic_store_n(slot, reinterpret_cast<void*>(&PaintMaterials), __ATOMIC_RELEASE);
    if (mprotect(page, size, PROT_READ) != 0)
        __android_log_print(ANDROID_LOG_ERROR, "WorldRPGNeon", "Vehicle paint GOT protection restore failed");
    __android_log_print(ANDROID_LOG_INFO, "WorldRPGNeon", "Vehicle material colors installed");
}

void Track(void* clump, uint32_t id, const VehicleColors& colors) {
    if (!clump || !original) return;
    Paint paint{id, {}};
    for (unsigned i = 0; i < paint.colors.size(); ++i) paint.colors[i] = MaterialColor(colors.rgb[i]);
    std::lock_guard lock(mutex);
    paints.insert_or_assign(clump, paint);
}

void Forget(void* clump) {
    if (!clump) return;
    std::lock_guard lock(mutex);
    paints.erase(clump);
}
} // namespace WorldRPG::Game::VehiclePaint
#endif
