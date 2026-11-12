#include <jni.h>
#include <cstring>
#include <string>
#include <memory>
#include <mutex>
#include <atomic>
#include <sys/stat.h>

#include "../net/raknet/RakNetWrapper.h"
#include "../net/NeonJoinProtocol.h"
#include "../net/CNet.cpp" // CNetAdapter (included for singleton access)
#include "../gtasa/GameEngine.h"
#include "../gtasa/RenderWareHooks.h"
#include "../gtasa/InputHooks.h"
#include "../gtasa/HUDRenderer.h"
#include "../shared/ElementManager.h"
#include "../shared/SyncManager.h"
#include "../shared/PacketHandler.h"
#include "../shared/LuaVMStub.h"
#include "../shared/ServerWorld.h"
#include "../shared/AssetManager.h"

#ifdef __ANDROID__
#include <android/log.h>
#define JNI_LOG_TAG "WorldRPGNeon"
#define JNI_LOGI(...) __android_log_print(ANDROID_LOG_INFO, JNI_LOG_TAG, __VA_ARGS__)
#define JNI_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, JNI_LOG_TAG, __VA_ARGS__)
#else
#include <cstdio>
#define JNI_LOGI(...) do { printf(__VA_ARGS__); printf("\n"); } while(0)
#define JNI_LOGE(...) do { fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } while(0)
#endif

// Cached JVM and GameActivity class/method references for callbacks from native threads
static JavaVM* g_jvm = nullptr;
static jobject g_gameActivityRef = nullptr;
static jmethodID g_onConnectionStateChangeMethod = nullptr;
static jmethodID g_onGameLineMethod = nullptr;
static jmethodID g_onDxFrameMethod = nullptr;

namespace WorldRPG::JniBridge {
void PostGameLine(int kind, const char* text) {
    if (!g_jvm || !g_gameActivityRef || !g_onGameLineMethod || !text) return;
    // NewStringUTF aborts the process on bytes that are not modified UTF-8.
    // Keep only printable ASCII so a mis-parsed packet cannot close the game.
    std::string safe;
    safe.reserve(std::strlen(text));
    for (const unsigned char ch : std::string(text)) {
        if (ch >= 0x20 && ch <= 0x7E) safe.push_back(static_cast<char>(ch));
    }
    if (safe.empty()) return;
    text = safe.c_str();
    JNIEnv* env = nullptr;
    bool attached = false;
    jint result = g_jvm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
    if (result == JNI_EDETACHED) {
        if (g_jvm->AttachCurrentThread(&env, nullptr) != JNI_OK) return;
        attached = true;
    } else if (result != JNI_OK) {
        return;
    }
    jstring jtext = env->NewStringUTF(text);
    if (jtext) {
        env->CallVoidMethod(g_gameActivityRef, g_onGameLineMethod, kind, jtext);
        if (env->ExceptionCheck()) env->ExceptionClear();
        env->DeleteLocalRef(jtext);
    }
    if (attached) g_jvm->DetachCurrentThread();
}
} // namespace WorldRPG::JniBridge

/// Posts a connection-state event to the owning GameActivity. Safe to call
/// from the GL thread or from transport callbacks (attaches when needed).
static void PostConnectionState(bool connected, const char* message) {
    if (!g_jvm || !g_gameActivityRef || !g_onConnectionStateChangeMethod) return;
    JNIEnv* env = nullptr;
    bool attached = false;
    jint result = g_jvm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
    if (result == JNI_EDETACHED) {
        if (g_jvm->AttachCurrentThread(&env, nullptr) != JNI_OK) return;
        attached = true;
    } else if (result != JNI_OK) {
        return;
    }
    jstring jmsg = env->NewStringUTF(message ? message : "");
    if (jmsg) {
        env->CallVoidMethod(g_gameActivityRef, g_onConnectionStateChangeMethod,
                            static_cast<jboolean>(connected), jmsg);
        env->DeleteLocalRef(jmsg);
    }
    if (attached) g_jvm->DetachCurrentThread();
}

// ---------------------------------------------------------------------------
// Native client state belongs to the Activity that initialized it.
// ---------------------------------------------------------------------------

namespace {

struct NeonEngineState {
    std::unique_ptr<WorldRPG::Net::RakNetWrapper> network;
    std::unique_ptr<WorldRPG::Net::NeonJoinClient> join;
    std::unique_ptr<WorldRPG::Game::GameEngine> gameEngine;
    std::unique_ptr<WorldRPG::Game::RenderWareHooks> renderer;
    std::unique_ptr<WorldRPG::Game::InputHooks> input;
    std::unique_ptr<WorldRPG::Game::HUDRenderer> hud;
    std::unique_ptr<WorldRPG::Sync::ElementManager> elements;
    std::unique_ptr<WorldRPG::Sync::SyncManager> syncManager;
    std::unique_ptr<WorldRPG::Sync::PacketHandler> packetHandler;
    std::unique_ptr<WorldRPG::Script::LuaVMStub> luaVM;
    std::unique_ptr<WorldRPG::Sync::AssetManager> assetManager;
    bool assetsReady = false;
    bool inputReady = false;
};

// JNI lifecycle calls can originate on the Activity thread while the view
// has queued work. Transport callbacks run synchronously inside DoPulse().
std::mutex g_stateMutex;
std::unique_ptr<NeonEngineState> g_state;
std::atomic<bool> g_enterServer{false};
// True after the first nativeConnect. A later call is a reconnect (P3.1): stop
// the old RakNet session and reset the remote world before joining again.
bool g_sessionOpened = false;

bool isOwner(JNIEnv* env, jobject activity) {
    return g_state && g_gameActivityRef && env->IsSameObject(activity, g_gameActivityRef);
}

} // anonymous namespace

// Global HUD renderer pointer exposed for JNI_HUD.cpp (must be outside anonymous namespace for external linkage)
WorldRPG::Game::HUDRenderer* g_hudRenderer = nullptr;

// ---------------------------------------------------------------------------
// JNI native method implementations matching GameActivity.kt declarations
// ---------------------------------------------------------------------------

extern "C" {

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* /*reserved*/) {
    JNIEnv* env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        JNI_LOGE("JNI_OnLoad: GetEnv failed");
        return JNI_ERR;
    }
    g_jvm = vm;
    JNI_LOGI("JNI_OnLoad: WorldRPGNeon native library loaded");
    return JNI_VERSION_1_6;
}

JNIEXPORT jboolean JNICALL
Java_com_worldrpg_neonlauncher_game_GameActivity_nativeInit(JNIEnv* env, jobject thiz) {
    const std::lock_guard lock(g_stateMutex);
    if (g_state) return isOwner(env, thiz) ? JNI_TRUE : JNI_FALSE;

    auto state = std::make_unique<NeonEngineState>();
    state->network = std::make_unique<WorldRPG::Net::RakNetWrapper>();
    state->join = std::make_unique<WorldRPG::Net::NeonJoinClient>();
    state->gameEngine = std::make_unique<WorldRPG::Game::GameEngine>();
    state->renderer = std::make_unique<WorldRPG::Game::RenderWareHooks>();
    state->input = std::make_unique<WorldRPG::Game::InputHooks>();
    state->hud = std::make_unique<WorldRPG::Game::HUDRenderer>();
    state->elements = std::make_unique<WorldRPG::Sync::ElementManager>();
    state->syncManager = std::make_unique<WorldRPG::Sync::SyncManager>();
    state->packetHandler = std::make_unique<WorldRPG::Sync::PacketHandler>();
    state->luaVM = std::make_unique<WorldRPG::Script::LuaVMStub>();
    state->assetManager = std::make_unique<WorldRPG::Sync::AssetManager>();

    if (!state->syncManager->initialize(*state->elements) ||
        !state->packetHandler->initialize(*state->syncManager) ||
        !state->luaVM->initialize()) {
        JNI_LOGE("nativeInit: client subsystem initialization failed");
        return JNI_FALSE;
    }
    state->packetHandler->registerAll(*state->network);

    jclass activityClass = env->GetObjectClass(thiz);
    if (!activityClass) return JNI_FALSE;
    jmethodID callback = env->GetMethodID(
        activityClass, "onNativeConnectionStateChange", "(ZLjava/lang/String;)V");
    g_onGameLineMethod = env->GetMethodID(activityClass, "onNativeGameLine", "(ILjava/lang/String;)V");
    if (!g_onGameLineMethod && env->ExceptionCheck()) env->ExceptionClear();
    g_onDxFrameMethod = env->GetMethodID(activityClass, "onNativeDxFrame", "(Ljava/lang/String;)V");
    if (!g_onDxFrameMethod && env->ExceptionCheck()) env->ExceptionClear();
    env->DeleteLocalRef(activityClass);
    if (!callback) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        JNI_LOGE("nativeInit: connection callback signature not found");
        return JNI_FALSE;
    }

    jobject activityRef = env->NewGlobalRef(thiz);
    if (!activityRef) return JNI_FALSE;
    g_gameActivityRef = activityRef;
    g_onConnectionStateChangeMethod = callback;

    WorldRPG::Net::NeonJoinClient* joinRaw = state->join.get();
    // A transport connection is progress, not a verified Neon session. Route
    // failures through the join so its timeout cannot report a second cause.
    state->network->SetConnectionStateCallback([joinRaw](bool connected, const char* message) {
        if (connected) {
            JNI_LOGI("Transport ready; Neon verification still pending");
            return;
        }
        joinRaw->OnTransportFailure(message ? message : "Transport connection failed");
    });

    // Only terminal join transitions surface to Kotlin: the UI treats
    // connected=false as an error, so intermediate progress states stay in
    // logcat until the session is validated (Plano 22 acceptance rule).
    state->join->SetStateCallback([](WorldRPG::Net::JoinState state, const char* detail) {
        using WorldRPG::Net::JoinState;
        if (state == JoinState::Complete) {
            PostConnectionState(true, "Neon join complete");
        } else if (state == JoinState::Failed) {
            PostConnectionState(false, detail);
        }
    });

    // Composite application-packet routing: the join state machine consumes
    // handshake packets first; everything else falls through to the game
    // PacketHandler (which strips the same [99][gameId] envelope itself).
    WorldRPG::Sync::PacketHandler* packetRaw = state->packetHandler.get();
    state->network->SetPacketHandler(
        [joinRaw, packetRaw](uint8_t packetId, const uint8_t* data, size_t length, size_t bitLength) -> bool {
            if (packetId != static_cast<uint8_t>(WorldRPG::Net::PacketID::USER_PACKET_ENUM) ||
                length < 1) {
                return false;
            }
            if (joinRaw->ProcessGamePacket(data[0], data + 1, length - 1, bitLength >= 8 ? bitLength - 8 : 0)) return true;
            if (joinRaw->State() != WorldRPG::Net::JoinState::Complete) return false;
            return packetRaw->handlePacket(packetId, data, length, bitLength);
        });

    g_hudRenderer = state->hud.get();
    g_state = std::move(state);
    JNI_LOGI("nativeInit: client bridge ready");
    return JNI_TRUE;
}

/// Loads libGTASA.so, resolves its base address and installs the verified
/// v2.10 offset table. Must be called on the GL thread after
/// nativeSetAssetPath succeeded. Returning true means the engine library is
/// resident and the hook surface is ready; it does NOT mean the game world is
/// running (full boot is P1A.4).
JNIEXPORT jboolean JNICALL
Java_com_worldrpg_neonlauncher_game_GameActivity_nativeBootEngine(JNIEnv* env, jobject thiz) {
    const std::lock_guard lock(g_stateMutex);
    if (!isOwner(env, thiz) || !g_state->assetsReady) return JNI_FALSE;
    if (g_state->gameEngine->isReady()) return JNI_TRUE;

    const auto offsets = WorldRPG::Game::EngineOffsets::forVersion("2.10");
    if (!g_state->gameEngine->initialize(offsets)) {
        JNI_LOGE("nativeBootEngine: GameEngine::initialize failed");
        return JNI_FALSE;
    }
    JNI_LOGI("nativeBootEngine: libGTASA resident, engine hook surface ready");
    return JNI_TRUE;
}

JNIEXPORT void JNICALL
Java_com_worldrpg_neonlauncher_game_GameActivity_nativePause(JNIEnv* env, jobject thiz) {
    const std::lock_guard lock(g_stateMutex);
    if (isOwner(env, thiz) && g_state->gameEngine) g_state->gameEngine->pause();
}

JNIEXPORT void JNICALL
Java_com_worldrpg_neonlauncher_game_GameActivity_nativeResume(JNIEnv* env, jobject thiz) {
    const std::lock_guard lock(g_stateMutex);
    if (isOwner(env, thiz) && g_state->gameEngine) g_state->gameEngine->resume();
}

JNIEXPORT jboolean JNICALL
Java_com_worldrpg_neonlauncher_game_GameActivity_nativeConnect(
    JNIEnv* env, jobject thiz, jstring ip, jint port, jstring nickname)
{
    const std::lock_guard lock(g_stateMutex);
    if (!isOwner(env, thiz) || !g_state->assetsReady || !ip || !nickname ||
        port < 1 || port > 65535) {
        return JNI_FALSE;
    }

    const char* ipStr = env->GetStringUTFChars(ip, nullptr);
    if (!ipStr) return JNI_FALSE;
    const char* nickStr = env->GetStringUTFChars(nickname, nullptr);
    if (!nickStr) {
        env->ReleaseStringUTFChars(ip, ipStr);
        return JNI_FALSE;
    }

    std::string normalizedNick = WorldRPG::Net::NormalizeAscii(nickStr);
    env->ReleaseStringUTFChars(nickname, nickStr);
    if (g_sessionOpened) {
        g_state->network->StopNetwork();
        WorldRPG::Game::RequestRemoteReset();
    }
    const bool started = ipStr[0] != '\0' && !normalizedNick.empty() &&
        g_state->network->StartNetwork(ipStr, static_cast<uint16_t>(port));
    env->ReleaseStringUTFChars(ip, ipStr);
    // Scheduling a join only starts an attempt. It does not verify a session.
    // The Neon join handshake (PLAYER_JOIN -> MOD_NAME -> PLAYER_JOINDATA ->
    // SERVER_JOIN_COMPLETE -> RPCs -> SERVER_JOINEDGAME) is driven by
    // NeonJoinClient from nativePulse once the transport is up.
    if (!started || !g_state->join->Begin(*g_state->network, normalizedNick, "")) {
        g_state->network->StopNetwork();
        return JNI_FALSE;
    }
    g_enterServer = true;
    g_sessionOpened = true;
    return JNI_TRUE;
}

JNIEXPORT void JNICALL
Java_com_worldrpg_neonlauncher_game_GameActivity_nativeSurfaceChanged(
    JNIEnv* env, jobject thiz, jint width, jint height)
{
    const std::lock_guard lock(g_stateMutex);
    if (!isOwner(env, thiz) || width <= 0 || height <= 0) return;
    WorldRPG::Script::ActiveVmSetScreen(width, height);
    if (g_state->inputReady) {
        g_state->input->setScreenSize(width, height);
    } else {
        g_state->inputReady = g_state->input->initialize(width, height);
    }
}

JNIEXPORT void JNICALL
Java_com_worldrpg_neonlauncher_game_GameActivity_nativeOnTouch(
    JNIEnv* env, jobject thiz, jfloat x, jfloat y, jint action, jint pointerId)
{
    const std::lock_guard lock(g_stateMutex);
    if (!isOwner(env, thiz) || !g_state->inputReady ||
        action < 0 || action > 3 || pointerId < 0) return;

    WorldRPG::Game::TouchEvent event{};
    event.x = x;
    event.y = y;
    event.pressure = 1.0f;
    event.pointerId = pointerId;
    event.action = static_cast<WorldRPG::Game::TouchAction>(action);

    g_state->input->onTouchEvent(event);
}

/// Process incoming network packets on the view's GL thread.
/// Suppressed while the Activity is paused (no frames are drawn then, but a
/// queued event could still arrive; the transport must not advance).
JNIEXPORT void JNICALL
Java_com_worldrpg_neonlauncher_game_GameActivity_nativePulse(JNIEnv* env, jobject thiz) {
    const std::lock_guard lock(g_stateMutex);
    if (!isOwner(env, thiz) || !g_state->network) return;
    if (g_enterServer.load()) WorldRPG::Game::BeginServerSession();
    g_state->network->DoPulse();
    g_state->join->Pulse();
    if (!g_onDxFrameMethod || !g_gameActivityRef) return;
    const std::string frame = WorldRPG::Script::ActiveVmPump();
    if (frame.empty()) return;
    jstring jtext = env->NewStringUTF(frame.c_str());
    if (!jtext) return;
    env->CallVoidMethod(g_gameActivityRef, g_onDxFrameMethod, jtext);
    if (env->ExceptionCheck()) env->ExceptionClear();
    env->DeleteLocalRef(jtext);
}

JNIEXPORT jstring JNICALL
Java_com_worldrpg_neonlauncher_game_EngineHostActivity_nativeMapSnapshot(JNIEnv* env, jobject) {
    float x = 0, y = 0, z = 0, heading = 0;
    WorldRPG::Game::CopyPose(x, y, z, heading);
    const std::string text = WorldRPG::ServerWorld::MapSnapshot(x, y, heading);
    return env->NewStringUTF(text.c_str());
}

JNIEXPORT void JNICALL
Java_com_worldrpg_neonlauncher_game_EngineHostActivity_nativeSetScreenSize(
    JNIEnv*, jobject, jint width, jint height) {
    WorldRPG::Script::ActiveVmSetScreen(width, height);
}

JNIEXPORT void JNICALL
Java_com_worldrpg_neonlauncher_game_GameActivity_nativeDestroy(JNIEnv* env, jobject thiz) {
    const std::lock_guard lock(g_stateMutex);
    if (!isOwner(env, thiz)) return;

    // Detach callbacks holding raw pointers into this state before teardown.
    g_state->network->SetPacketHandler(nullptr);
    g_state->join->SetStateCallback({});
    g_state->join->Reset();
    g_state->network->SetConnectionStateCallback({});
    g_state->network->Shutdown();
    g_hudRenderer = nullptr;
    g_enterServer = false;
    g_state.reset();
    env->DeleteGlobalRef(g_gameActivityRef);
    g_gameActivityRef = nullptr;
    g_onConnectionStateChangeMethod = nullptr;
    g_onGameLineMethod = nullptr;
    JNI_LOGI("nativeDestroy: client bridge released");
}

/// Set the private storage path published by the Android asset installer.
JNIEXPORT jboolean JNICALL
Java_com_worldrpg_neonlauncher_game_GameActivity_nativeSetAssetPath(
    JNIEnv* env, jobject thiz, jstring assetPath)
{
    const std::lock_guard lock(g_stateMutex);
    if (!isOwner(env, thiz) || !assetPath) return JNI_FALSE;

    const char* path = env->GetStringUTFChars(assetPath, nullptr);
    if (!path) return JNI_FALSE;
    struct stat metadata{};
    g_state->assetsReady = path[0] != '\0' && stat(path, &metadata) == 0 &&
        S_ISDIR(metadata.st_mode) && g_state->assetManager->initialize(path);
    env->ReleaseStringUTFChars(assetPath, path);
    if (!g_state->assetsReady) JNI_LOGE("nativeSetAssetPath: installed directory is unavailable");
    return g_state->assetsReady ? JNI_TRUE : JNI_FALSE;
}

// The game screen is EngineHostActivity. These entry points share the
// GameActivity client so the join, download bar and chat run there.
JNIEXPORT jboolean JNICALL
Java_com_worldrpg_neonlauncher_game_EngineHostActivity_nativeInit(JNIEnv* env, jobject thiz) {
    return Java_com_worldrpg_neonlauncher_game_GameActivity_nativeInit(env, thiz);
}
JNIEXPORT jboolean JNICALL
Java_com_worldrpg_neonlauncher_game_EngineHostActivity_nativeSetAssetPath(JNIEnv* env, jobject thiz, jstring assetPath) {
    return Java_com_worldrpg_neonlauncher_game_GameActivity_nativeSetAssetPath(env, thiz, assetPath);
}
JNIEXPORT jboolean JNICALL
Java_com_worldrpg_neonlauncher_game_EngineHostActivity_nativeConnect(JNIEnv* env, jobject thiz, jstring ip, jint port, jstring nickname) {
    return Java_com_worldrpg_neonlauncher_game_GameActivity_nativeConnect(env, thiz, ip, port, nickname);
}
JNIEXPORT void JNICALL
Java_com_worldrpg_neonlauncher_game_EngineHostActivity_nativePulse(JNIEnv* env, jobject thiz) {
    Java_com_worldrpg_neonlauncher_game_GameActivity_nativePulse(env, thiz);
}
JNIEXPORT void JNICALL
Java_com_worldrpg_neonlauncher_game_EngineHostActivity_nativeDestroy(JNIEnv* env, jobject thiz) {
    Java_com_worldrpg_neonlauncher_game_GameActivity_nativeDestroy(env, thiz);
}

JNIEXPORT jboolean JNICALL
Java_com_worldrpg_neonlauncher_game_EngineHostActivity_nativeSendChat(JNIEnv* env, jobject thiz, jstring text) {
    const std::lock_guard lock(g_stateMutex);
    if (!isOwner(env, thiz) || !g_state || !g_state->join || !text) return JNI_FALSE;
    const char* raw = env->GetStringUTFChars(text, nullptr);
    if (!raw) return JNI_FALSE;
    const bool sent = g_state->join->SendChat(raw);
    env->ReleaseStringUTFChars(text, raw);
    return sent ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_worldrpg_neonlauncher_game_EngineHostActivity_nativeDropConnection(JNIEnv* env, jobject thiz)
{
    const std::lock_guard lock(g_stateMutex);
    if (!isOwner(env, thiz) || !g_state || !g_state->network) return;
    // Report "Connection lost" through the join callback (P3.1), then stop the transport
    // so a later Retry starts a clean session.
    g_state->join->ForceConnectionLost();
    g_state->network->StopNetwork();
    JNI_LOGI("nativeDropConnection: transport stopped for /disconnect");
}

} // extern "C"
