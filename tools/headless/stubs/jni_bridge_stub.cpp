// Host replacement for WorldRPG::JniBridge (jni/JNI_OnLoad.cpp is Android-only).
// Game lines that the Android client would show in the GameActivity are printed
// to stdout instead, so chat/progress text from the server stays visible.
#include <cstdio>

namespace WorldRPG::JniBridge {
void PostGameLine(int kind, const char* text) {
    if (text && *text) std::printf("[GAMELINE kind=%d] %s\n", kind, text);
}
} // namespace WorldRPG::JniBridge
