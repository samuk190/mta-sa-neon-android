#include "MobileSettings.h"

#include <cstdint>
#include <cstring>

#ifdef __ANDROID__
#include <dlfcn.h>
#endif

namespace WorldRPG::Game {

namespace {

constexpr std::size_t kEntrySize = 0x28;
constexpr std::size_t kValue = 0x10;
constexpr std::size_t kMin = 0x18;
constexpr std::size_t kMax = 0x1c;

std::uint8_t* Table() {
#ifdef __ANDROID__
    static std::uint8_t* table = nullptr;
    if (table) return table;
    void* handle = dlopen("libGTASA.so", RTLD_NOLOAD | RTLD_GLOBAL);
    if (!handle) return nullptr;
    table = static_cast<std::uint8_t*>(dlsym(handle, "_ZN14MobileSettings8settingsE"));
    return table;
#else
    return nullptr;
#endif
}

std::int32_t ReadInt(const std::uint8_t* at) {
    std::int32_t value = 0;
    std::memcpy(&value, at, sizeof(value));
    return value;
}

}  // namespace

bool GetMobileSetting(int index, int& value, int& min, int& max) {
    std::uint8_t* table = Table();
    if (!table || index < 0 || index >= kMobileSettingCount) return false;
    const std::uint8_t* entry = table + static_cast<std::size_t>(index) * kEntrySize;
    value = ReadInt(entry + kValue);
    min = ReadInt(entry + kMin);
    max = ReadInt(entry + kMax);
    return min <= max;
}

bool SetMobileSetting(int index, int value) {
    int current = 0, min = 0, max = 0;
    if (!GetMobileSetting(index, current, min, max)) return false;
    if (value < min) value = min;
    if (value > max) value = max;
    std::uint8_t* entry = Table() + static_cast<std::size_t>(index) * kEntrySize;
    // One aligned 32-bit store; the game thread reads it the next time it uses the option.
    __atomic_store_n(reinterpret_cast<std::int32_t*>(entry + kValue), value, __ATOMIC_RELAXED);
    return true;
}

}  // namespace WorldRPG::Game
