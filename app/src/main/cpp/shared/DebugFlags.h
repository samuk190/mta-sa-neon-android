#pragma once

#include <cstdio>
#include <cstring>

// Runtime switches for on-device bisection, read once from the app's private files dir:
//   adb shell run-as com.worldrpg.neonlauncher sh -c 'echo noremote > files/neon_debug_flags'
// One word per line. Missing file = all features on.
namespace WorldRPG::Debug {

inline bool Flag(const char* name) {
    static char flags[512] = {};
    static bool loaded = false;
    if (!loaded) {
        loaded = true;
#ifdef __ANDROID__
        if (FILE* f = std::fopen("/data/data/com.worldrpg.neonlauncher/files/neon_debug_flags", "r")) {
            const size_t n = std::fread(flags, 1, sizeof(flags) - 1, f);
            flags[n] = '\0';
            std::fclose(f);
        }
#endif
    }
    return std::strstr(flags, name) != nullptr;
}

}  // namespace WorldRPG::Debug
