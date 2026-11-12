#include <jni.h>
#include <string>

#include "../gtasa/HUDRenderer.h"

#ifdef __ANDROID__
#include <android/log.h>
#define JNI_HUD_TAG "WorldRPGNeon"
#define JNI_HUD_LOGI(...) __android_log_print(ANDROID_LOG_INFO, JNI_HUD_TAG, __VA_ARGS__)
#define JNI_HUD_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, JNI_HUD_TAG, __VA_ARGS__)
#else
#include <cstdio>
#define JNI_HUD_LOGI(...) do { printf(__VA_ARGS__); printf("\n"); } while(0)
#define JNI_HUD_LOGE(...) do { fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } while(0)
#endif

// Forward declaration of global engine state defined in JNI_OnLoad.cpp
extern WorldRPG::Game::HUDRenderer* g_hudRenderer;

extern "C" {

/// Set HUD element parameters from Kotlin admin command
/// Matches: /admin module hud <element> <x> <y> <width> <height>
JNIEXPORT void JNICALL
Java_com_worldrpg_neonlauncher_game_GameActivity_nativeSetHUDElementParams(
    JNIEnv* env, jobject /*thiz*/, jstring elementName,
    jfloat x, jfloat y, jfloat width, jfloat height, jboolean visible)
{
    if (!g_hudRenderer) {
        JNI_HUD_LOGE("nativeSetHUDElementParams: HUD renderer not initialized");
        return;
    }

    const char* nameStr = env->GetStringUTFChars(elementName, nullptr);
    if (!nameStr) return;

    WorldRPG::Game::HUDElementParams params;
    params.x = x;
    params.y = y;
    params.width = width;
    params.height = height;
    params.visible = static_cast<bool>(visible);

    g_hudRenderer->setElementParams(nameStr, params);
    JNI_HUD_LOGI("nativeSetHUDElementParams: '%s' x=%.3f y=%.3f w=%.3f h=%.3f vis=%d",
                 nameStr, x, y, width, height, params.visible);

    env->ReleaseStringUTFChars(elementName, nameStr);
}

/// Get current HUD element parameters as formatted string (ASCII-only)
JNIEXPORT jstring JNICALL
Java_com_worldrpg_neonlauncher_game_GameActivity_nativeGetHUDElementParams(
    JNIEnv* env, jobject /*thiz*/, jstring elementName)
{
    if (!g_hudRenderer) {
        return env->NewStringUTF("ERROR: HUD not initialized");
    }

    const char* nameStr = env->GetStringUTFChars(elementName, nullptr);
    if (!nameStr) return env->NewStringUTF("ERROR: null element name");

    auto params = g_hudRenderer->getElementParams(nameStr);
    env->ReleaseStringUTFChars(elementName, nameStr);

    char buffer[256];
    std::snprintf(buffer, sizeof(buffer),
                  "x=%.4f y=%.4f w=%.4f h=%.4f vis=%d",
                  params.x, params.y, params.width, params.height,
                  params.visible ? 1 : 0);

    return env->NewStringUTF(buffer);
}

/// List all configurable HUD element names
JNIEXPORT jstring JNICALL
Java_com_worldrpg_neonlauncher_game_GameActivity_nativeListHUDElements(
    JNIEnv* env, jobject /*thiz*/)
{
    if (!g_hudRenderer) {
        return env->NewStringUTF("ERROR: HUD not initialized");
    }

    std::string result;
    g_hudRenderer->listElements([&result](const char* name, const WorldRPG::Game::HUDElementParams& p) {
        if (!result.empty()) result += "\n";
        char line[256];
        std::snprintf(line, sizeof(line), "%s: x=%.3f y=%.3f w=%.3f h=%.3f vis=%d",
                      name, p.x, p.y, p.width, p.height, p.visible ? 1 : 0);
        result += line;
    });

    if (result.empty()) result = "No HUD elements registered";
    return env->NewStringUTF(result.c_str());
}

/// Add a chat message to the HUD overlay (ASCII-normalized)
JNIEXPORT void JNICALL
Java_com_worldrpg_neonlauncher_game_GameActivity_nativeAddChatMessage(
    JNIEnv* env, jobject /*thiz*/, jstring message, jint color)
{
    if (!g_hudRenderer) return;

    const char* msgStr = env->GetStringUTFChars(message, nullptr);
    if (!msgStr) return;

    // ASCII normalization is handled inside HUDRenderer::addChatMessage
    g_hudRenderer->addChatMessage(msgStr, static_cast<uint8_t>(color));
    env->ReleaseStringUTFChars(message, msgStr);
}

} // extern "C"