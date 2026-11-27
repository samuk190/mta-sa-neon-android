#include "../shared/ControlToggles.h"
#include "../shared/ConnectionLoading.h"
#include "../shared/VoiceRuntime.h"
#include "../shared/LuaBrowserApi.h"
#include <jni.h>
#include <cstring>
#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <atomic>
#include <sys/stat.h>

#include "../net/raknet/RakNetWrapper.h"
#include "../net/NeonJoinProtocol.h"
#include "../net/HttpDownloader.h"
#include "../net/RemoteCalls.h"
#include "../net/CNet.cpp" // CNetAdapter (included for singleton access)
#include "../gtasa/HardwareMouse.h"
#include "../gtasa/ControlMask.h"
#include "../gtasa/GameEngine.h"
#include "../gtasa/GameHeap.h"
#include "../gtasa/RemoteVehicle.h"
#include "../gtasa/RenderGuard.h"
#include "../gtasa/RenderWareHooks.h"
#include "../gtasa/InputHooks.h"
#include "../gtasa/HUDRenderer.h"
#include "../gtasa/MobileSettings.h"
#include "../shared/ElementManager.h"
#include "../shared/SyncManager.h"
#include "../shared/PacketHandler.h"
#include "../shared/LuaVMStub.h"
#include "../shared/Utf8.h"
#include "../shared/LuaMtaWindowApi.h"
#include "../shared/LuaKeyBindApi.h"
#include "../shared/LuaRocketSoccerApi.h"
#include "../shared/LuaWeaponApi.h"
#include "../shared/ServerWorld.h"
#include "../shared/ResourceLoader.h"
#include "../shared/AssetManager.h"

// Host the player connected to; the HTTP file service lives on the same host.
static std::string g_serverHost;
// "Use PC Network — Compatibility" for the next nativeConnect: the netc.dll the
// emulator runs (libsaonline_netemu.so), its build id and data folder. An
// empty netcPath selects the normal transport.
struct PcNetworkConfig {
    std::string netcPath, build, dataDir, productVersion;
};
static PcNetworkConfig g_pcNetwork;
// Server release for the next join (nativeSetNetcodeRelease) and what
// nativeConnect last asked for, so a release fallback can reopen the same
// transport from nativePulse.
static WorldRPG::Net::NetcodeRelease g_requestedRelease = WorldRPG::Net::NetcodeRelease::Automatic;
static WorldRPG::Net::NetcodeRelease g_releaseHint = WorldRPG::Net::NetcodeRelease::Current;
struct LastConnect {
    std::string address;  // what nativeConnect received (the native transport resolves it)
    uint16_t port = 0;
    bool quic = false;
    std::string nick;
    WorldRPG::Net::JoinProfile profile = WorldRPG::Net::JoinProfile::Standard;
};
static LastConnect g_lastConnect;

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
static jmethodID g_measureTextMethod = nullptr;
static jmethodID g_fontHeightMethod = nullptr;
static jmethodID g_loadPixelsMethod = nullptr;
static jmethodID g_decodePixelsMethod = nullptr;
static jmethodID g_encodePixelsMethod = nullptr;
static jmethodID g_storePixelsMethod = nullptr;
static jmethodID g_renderSvgMethod = nullptr;
static jmethodID g_copyScreenMethod = nullptr;
static jmethodID g_onVoiceConfigMethod = nullptr;
static jmethodID g_onVoicePlaybackMethod = nullptr;

namespace WorldRPG::JniBridge {
void PostGameLine(int kind, const char* text) {
    if (!g_jvm || !g_gameActivityRef || !g_onGameLineMethod || !text) return;
    // NewStringUTF aborts the process on bytes that are not modified UTF-8.
    // Keep only printable ASCII so a mis-parsed packet cannot close the game.
    std::string safe;
    safe.reserve(std::strlen(text));
    for (const unsigned char ch : std::string(text)) {
        if (ch >= 0x20) safe.push_back(static_cast<char>(ch));
    }
    if (text[0] != '\0' && safe.empty()) return;
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
    const auto wide = Utf8::ToUtf16(text);
    jstring jtext = env->NewString(reinterpret_cast<const jchar*>(wide.data()), static_cast<jsize>(wide.size()));
    if (jtext) {
        env->CallVoidMethod(g_gameActivityRef, g_onGameLineMethod, kind, jtext);
        if (env->ExceptionCheck()) env->ExceptionClear();
        env->DeleteLocalRef(jtext);
    }
    if (attached) g_jvm->DetachCurrentThread();
}
// Width of one line of text in the DX overlay font, or in a TTF file (EngineHostActivity.onNativeMeasureText).
// The text goes over as UTF-16, because NewStringUTF aborts on bytes that are not modified UTF-8.
float MeasureText(const std::string& utf8, float pixelSize, const std::string& fontFile, bool bold) {
    if (!g_jvm || !g_gameActivityRef || !g_measureTextMethod) return -1.f;
    const auto wide = WorldRPG::Utf8::ToUtf16(utf8);
    JNIEnv* env = nullptr;
    bool attached = false;
    const jint result = g_jvm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
    if (result == JNI_EDETACHED) {
        if (g_jvm->AttachCurrentThread(&env, nullptr) != JNI_OK) return -1.f;
        attached = true;
    } else if (result != JNI_OK) {
        return -1.f;
    }
    float width = -1.f;
    jstring jtext = env->NewString(reinterpret_cast<const jchar*>(wide.data()), static_cast<jsize>(wide.size()));
    if (jtext) {
        // The versioned font source and cache path are printable ASCII; the overlay resolves them.
        jstring jfont = fontFile.empty() ? nullptr : env->NewStringUTF(fontFile.c_str());
        width = env->CallFloatMethod(g_gameActivityRef, g_measureTextMethod, jtext, static_cast<jfloat>(pixelSize), jfont,
                                     static_cast<jboolean>(bold ? JNI_TRUE : JNI_FALSE));
        if (jfont) env->DeleteLocalRef(jfont);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            width = -1.f;
        }
        env->DeleteLocalRef(jtext);
    } else if (env->ExceptionCheck()) {
        env->ExceptionClear();
    }
    if (attached) g_jvm->DetachCurrentThread();
    return width;
}
namespace {
// A JNIEnv for the calling thread, attached for the scope when it was not.
struct EnvScope {
    JNIEnv* env = nullptr;
    bool attached = false;
    EnvScope() {
        if (!g_jvm) return;
        const jint result = g_jvm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
        if (result == JNI_EDETACHED) {
            if (g_jvm->AttachCurrentThread(&env, nullptr) == JNI_OK) attached = true;
            else env = nullptr;
        } else if (result != JNI_OK) {
            env = nullptr;
        }
    }
    ~EnvScope() {
        if (attached) g_jvm->DetachCurrentThread();
    }
    EnvScope(const EnvScope&) = delete;
    EnvScope& operator=(const EnvScope&) = delete;
};

// The Activity returns {width, height, pixels...}. Null or a short array is a failure.
bool TakePixels(JNIEnv* env, jintArray result, std::vector<uint32_t>& argb, unsigned& width, unsigned& height) {
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }
    if (!result) return false;
    const jsize count = env->GetArrayLength(result);
    jint head[2] = {0, 0};
    bool ok = false;
    if (count >= 2) {
        env->GetIntArrayRegion(result, 0, 2, head);
        const jlong pixels = static_cast<jlong>(head[0]) * static_cast<jlong>(head[1]);
        if (head[0] > 0 && head[1] > 0 && pixels == static_cast<jlong>(count) - 2) {
            argb.resize(static_cast<size_t>(pixels));
            env->GetIntArrayRegion(result, 2, static_cast<jsize>(pixels), reinterpret_cast<jint*>(argb.data()));
            width = static_cast<unsigned>(head[0]);
            height = static_cast<unsigned>(head[1]);
            ok = !env->ExceptionCheck();
            if (!ok) env->ExceptionClear();
        }
    }
    env->DeleteLocalRef(result);
    return ok;
}

jintArray ToIntArray(JNIEnv* env, const std::vector<uint32_t>& argb) {
    jintArray array = env->NewIntArray(static_cast<jsize>(argb.size()));
    if (!array) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        return nullptr;
    }
    env->SetIntArrayRegion(array, 0, static_cast<jsize>(argb.size()), reinterpret_cast<const jint*>(argb.data()));
    return array;
}
}  // namespace

float FontHeight(const std::string& source, float pixelSize, bool bold) {
    EnvScope scope;
    JNIEnv* env = scope.env;
    if (!env || !g_gameActivityRef || !g_fontHeightMethod) return -1.f;
    const auto wide = WorldRPG::Utf8::ToUtf16(source);
    jstring jsource = env->NewString(reinterpret_cast<const jchar*>(wide.data()), static_cast<jsize>(wide.size()));
    if (!jsource) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        return 0.f;
    }
    float height = env->CallFloatMethod(g_gameActivityRef, g_fontHeightMethod, jsource,
                                       static_cast<jfloat>(pixelSize), static_cast<jboolean>(bold));
    if (env->ExceptionCheck()) { env->ExceptionClear(); height = 0.f; }
    env->DeleteLocalRef(jsource);
    return height;
}

// Pixels of an image file or of an overlay bitmap ("tx:<id>", "rt:<id>"). The name is a cache
// path or a bitmap name, both printable ASCII.
bool LoadPixels(const std::string& source, std::vector<uint32_t>& argb, unsigned& width, unsigned& height) {
    if (!g_gameActivityRef || !g_loadPixelsMethod) return false;
    EnvScope scope;
    JNIEnv* env = scope.env;
    if (!env) return false;
    jstring jsource = env->NewStringUTF(source.c_str());
    if (!jsource) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        return false;
    }
    auto result = static_cast<jintArray>(env->CallObjectMethod(g_gameActivityRef, g_loadPixelsMethod, jsource));
    env->DeleteLocalRef(jsource);
    return TakePixels(env, result, argb, width, height);
}

bool DecodePixels(const char* data, size_t size, std::vector<uint32_t>& argb, unsigned& width, unsigned& height) {
    if (!g_gameActivityRef || !g_decodePixelsMethod || !data || size == 0 || size > 0x7FFFFFFFu) return false;
    EnvScope scope;
    JNIEnv* env = scope.env;
    if (!env) return false;
    jbyteArray bytes = env->NewByteArray(static_cast<jsize>(size));
    if (!bytes) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        return false;
    }
    env->SetByteArrayRegion(bytes, 0, static_cast<jsize>(size), reinterpret_cast<const jbyte*>(data));
    auto result = static_cast<jintArray>(env->CallObjectMethod(g_gameActivityRef, g_decodePixelsMethod, bytes));
    env->DeleteLocalRef(bytes);
    return TakePixels(env, result, argb, width, height);
}

bool EncodePixels(const std::vector<uint32_t>& argb, unsigned width, unsigned height, bool jpeg, int quality,
                  std::string& out) {
    if (!g_gameActivityRef || !g_encodePixelsMethod || argb.empty() || argb.size() > 0x7FFFFFFFu) return false;
    EnvScope scope;
    JNIEnv* env = scope.env;
    if (!env) return false;
    jintArray pixels = ToIntArray(env, argb);
    if (!pixels) return false;
    auto result = static_cast<jbyteArray>(
        env->CallObjectMethod(g_gameActivityRef, g_encodePixelsMethod, pixels, static_cast<jint>(width),
                              static_cast<jint>(height), static_cast<jboolean>(jpeg ? JNI_TRUE : JNI_FALSE),
                              static_cast<jint>(quality)));
    env->DeleteLocalRef(pixels);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }
    if (!result) return false;
    const jsize length = env->GetArrayLength(result);
    out.resize(static_cast<size_t>(length));
    if (length > 0) env->GetByteArrayRegion(result, 0, length, reinterpret_cast<jbyte*>(&out[0]));
    env->DeleteLocalRef(result);
    return length > 0;
}

bool StorePixels(const std::string& name, const std::vector<uint32_t>& argb, unsigned width, unsigned height) {
    if (!g_gameActivityRef || !g_storePixelsMethod || argb.empty() || argb.size() > 0x7FFFFFFFu) return false;
    EnvScope scope;
    JNIEnv* env = scope.env;
    if (!env) return false;
    jstring jname = env->NewStringUTF(name.c_str());
    jintArray pixels = jname ? ToIntArray(env, argb) : nullptr;
    bool stored = false;
    if (jname && pixels) {
        stored = env->CallBooleanMethod(g_gameActivityRef, g_storePixelsMethod, jname, pixels, static_cast<jint>(width),
                                        static_cast<jint>(height)) == JNI_TRUE;
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            stored = false;
        }
    } else if (env->ExceptionCheck()) {
        env->ExceptionClear();
    }
    if (pixels) env->DeleteLocalRef(pixels);
    if (jname) env->DeleteLocalRef(jname);
    return stored;
}
// dxUpdateScreenSource: the Activity copies the game surface into the bitmap of this name.
void CopyScreen(const std::string& name, unsigned width, unsigned height) {
    if (!g_gameActivityRef || !g_copyScreenMethod) return;
    EnvScope scope;
    JNIEnv* env = scope.env;
    if (!env) return;
    jstring jname = env->NewStringUTF(name.c_str());
    if (jname) {
        env->CallVoidMethod(g_gameActivityRef, g_copyScreenMethod, jname, static_cast<jint>(width), static_cast<jint>(height));
        env->DeleteLocalRef(jname);
    }
    if (env->ExceptionCheck()) env->ExceptionClear();
}

// The overlay draws the SVG document into the bitmap of this name. The document goes over as
// bytes because it may hold any UTF-8. 1 drawn, 0 not readable as SVG, -1 no Activity.
int RenderSvg(const std::string& name, const std::string& xml, unsigned width, unsigned height) {
    if (!g_gameActivityRef || !g_renderSvgMethod || xml.empty() || xml.size() > 0x7FFFFFFFu) return -1;
    EnvScope scope;
    JNIEnv* env = scope.env;
    if (!env) return -1;
    jstring jname = env->NewStringUTF(name.c_str());
    jbyteArray bytes = jname ? env->NewByteArray(static_cast<jsize>(xml.size())) : nullptr;
    int result = -1;
    if (jname && bytes) {
        env->SetByteArrayRegion(bytes, 0, static_cast<jsize>(xml.size()), reinterpret_cast<const jbyte*>(xml.data()));
        result = env->CallIntMethod(g_gameActivityRef, g_renderSvgMethod, jname, bytes, static_cast<jint>(width),
                                    static_cast<jint>(height));
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            result = -1;
        }
    } else if (env->ExceptionCheck()) {
        env->ExceptionClear();
    }
    if (bytes) env->DeleteLocalRef(bytes);
    if (jname) env->DeleteLocalRef(jname);
    return result;
}

void VoiceConfigChanged(bool enabled, uint32_t sampleRate) {
    if (!g_gameActivityRef || !g_onVoiceConfigMethod) return;
    EnvScope scope;
    JNIEnv* env = scope.env;
    if (!env) return;
    env->CallVoidMethod(g_gameActivityRef, g_onVoiceConfigMethod,
                        static_cast<jboolean>(enabled ? JNI_TRUE : JNI_FALSE),
                        static_cast<jint>(sampleRate));
    if (env->ExceptionCheck()) env->ExceptionClear();
}

void VoicePlaybackPcm(uint32_t playerId, const int16_t* samples, size_t sampleCount, uint32_t sampleRate) {
    if (!g_gameActivityRef || !g_onVoicePlaybackMethod || !samples || sampleCount == 0) return;
    EnvScope scope;
    JNIEnv* env = scope.env;
    if (!env) return;
    const jsize count = static_cast<jsize>(sampleCount);
    jshortArray arr = env->NewShortArray(count);
    if (!arr) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        return;
    }
    env->SetShortArrayRegion(arr, 0, count, reinterpret_cast<const jshort*>(samples));
    env->CallVoidMethod(g_gameActivityRef, g_onVoicePlaybackMethod,
                        static_cast<jint>(playerId), arr, static_cast<jint>(sampleRate));
    if (env->ExceptionCheck()) env->ExceptionClear();
    env->DeleteLocalRef(arr);
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
    JNI_LOGI("JNI_OnLoad: libsaonlineneon.so loaded");
    // Engine process only: libGTASA is already loaded and its engine thread has not started yet.
    WorldRPG::Game::InstallGameHeap();
    WorldRPG::Game::InstallStreamingCapacities();
    WorldRPG::Game::InstallServerSessionHook();
    WorldRPG::Game::InstallHardwareMouse();
    WorldRPG::Game::InstallControlMask();
    return JNI_VERSION_1_6;
}

JNIEXPORT jboolean JNICALL
Java_com_saonline_neonlauncher_game_GameActivity_nativeInit(JNIEnv* env, jobject thiz) {
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
    g_measureTextMethod = env->GetMethodID(activityClass, "onNativeMeasureText", "(Ljava/lang/String;FLjava/lang/String;Z)F");
    if (!g_measureTextMethod && env->ExceptionCheck()) env->ExceptionClear();
    g_fontHeightMethod = env->GetMethodID(activityClass, "onNativeFontHeight", "(Ljava/lang/String;FZ)F");
    if (!g_fontHeightMethod && env->ExceptionCheck()) env->ExceptionClear();
    g_loadPixelsMethod = env->GetMethodID(activityClass, "onNativeLoadPixels", "(Ljava/lang/String;)[I");
    if (!g_loadPixelsMethod && env->ExceptionCheck()) env->ExceptionClear();
    g_decodePixelsMethod = env->GetMethodID(activityClass, "onNativeDecodePixels", "([B)[I");
    if (!g_decodePixelsMethod && env->ExceptionCheck()) env->ExceptionClear();
    g_encodePixelsMethod = env->GetMethodID(activityClass, "onNativeEncodePixels", "([IIIZI)[B");
    if (!g_encodePixelsMethod && env->ExceptionCheck()) env->ExceptionClear();
    g_storePixelsMethod = env->GetMethodID(activityClass, "onNativeStorePixels", "(Ljava/lang/String;[III)Z");
    if (!g_storePixelsMethod && env->ExceptionCheck()) env->ExceptionClear();
    g_renderSvgMethod = env->GetMethodID(activityClass, "onNativeRenderSvg", "(Ljava/lang/String;[BII)I");
    if (!g_renderSvgMethod && env->ExceptionCheck()) env->ExceptionClear();
    g_copyScreenMethod = env->GetMethodID(activityClass, "onNativeCopyScreen", "(Ljava/lang/String;II)V");
    if (!g_copyScreenMethod && env->ExceptionCheck()) env->ExceptionClear();
    g_onVoiceConfigMethod = env->GetMethodID(activityClass, "onNativeVoiceConfig", "(ZI)V");
    if (!g_onVoiceConfigMethod && env->ExceptionCheck()) env->ExceptionClear();
    g_onVoicePlaybackMethod = env->GetMethodID(activityClass, "onNativeVoicePlayback", "(I[SI)V");
    if (!g_onVoicePlaybackMethod && env->ExceptionCheck()) env->ExceptionClear();
    env->DeleteLocalRef(activityClass);
    if (!callback) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        JNI_LOGE("nativeInit: connection callback signature not found");
        return JNI_FALSE;
    }

    activityClass = env->GetObjectClass(thiz);
    jmethodID identityCallback = env->GetMethodID(activityClass, "onNativeIdentityRequest", "(JLjava/lang/String;Ljava/lang/String;I)V");
    if (env->ExceptionCheck()) env->ExceptionClear();
    env->DeleteLocalRef(activityClass);
    if (identityCallback) state->join->SetIdentityCallback([identityCallback](uint64_t requestId, const std::string& serverId,
            const std::string& endpoint, WorldRPG::Net::NeonAuthMode mode) {
        WorldRPG::JniBridge::EnvScope scope;
        if (!scope.env || !g_gameActivityRef) return;
        jstring id = scope.env->NewStringUTF(serverId.c_str());
        jstring address = scope.env->NewStringUTF(endpoint.c_str());
        if (id && address) scope.env->CallVoidMethod(g_gameActivityRef, identityCallback, static_cast<jlong>(requestId), id, address, static_cast<jint>(mode));
        if (id) scope.env->DeleteLocalRef(id);
        if (address) scope.env->DeleteLocalRef(address);
        if (scope.env->ExceptionCheck()) scope.env->ExceptionClear();
    });

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
        if (state != JoinState::Complete) WorldRPG::Game::SetAmbientTrafficEnabled(false, false);
        if (state == JoinState::Complete) {
            PostConnectionState(true, "Neon join complete");
            // Desktop Packet_ServerJoined: client files come from the server's HTTP service.
            if (g_state && g_state->join) {
                WorldRPG::Net::HttpDownloader::SetServerBase(
                    WorldRPG::Net::HttpDownloader::BuildServerBase(g_state->join->Info(), g_serverHost));
            }
        } else if (state == JoinState::Failed) {
            // Desktop ~CClientGame / CCore::RemoveMessageBox path: never leave the screen over an error.
            WorldRPG::ConnectionLoading::End();
            WorldRPG::ConnectionLoading::Flush();
            WorldRPG::Controls::ResetAll();
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
Java_com_saonline_neonlauncher_game_GameActivity_nativeBootEngine(JNIEnv* env, jobject thiz) {
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
Java_com_saonline_neonlauncher_game_GameActivity_nativePause(JNIEnv* env, jobject thiz) {
    const std::lock_guard lock(g_stateMutex);
    if (isOwner(env, thiz) && g_state->gameEngine) g_state->gameEngine->pause();
}

JNIEXPORT void JNICALL
Java_com_saonline_neonlauncher_game_GameActivity_nativeResume(JNIEnv* env, jobject thiz) {
    const std::lock_guard lock(g_stateMutex);
    if (isOwner(env, thiz) && g_state->gameEngine) g_state->gameEngine->resume();
}

// Opens the transport nativeConnect chose (QUIC, PC Network or the native MTA
// one) for g_lastConnect. Caller holds g_stateMutex.
static bool StartTransport(JNIEnv* env, jobject thiz) {
    const LastConnect& c = g_lastConnect;
    if (c.quic) {
#ifdef __ANDROID__
        auto client = WorldRPG::Net::QuicGameClient::Connect(env, thiz, g_serverHost, c.port);
        return g_state->network->StartQuic(g_serverHost, c.port, std::move(client));
#else
        return false;
#endif
    }
    if (!g_pcNetwork.netcPath.empty()) {
        // InitNetRev product version for this server (PcNetworkSettings.productVersionFor).
        return g_state->network->StartPcNetwork(g_serverHost, c.port, g_pcNetwork.netcPath, g_pcNetwork.dataDir,
                                                g_pcNetwork.productVersion);
    }
    return g_state->network->StartNetwork(c.address.c_str(), c.port);
}

JNIEXPORT jboolean JNICALL
Java_com_saonline_neonlauncher_game_GameActivity_nativeConnect(
    JNIEnv* env, jobject thiz, jstring ip, jint port, jstring nickname, jint joinProfile)
{
    const std::lock_guard lock(g_stateMutex);
    if (!isOwner(env, thiz) || !g_state->assetsReady || !ip || !nickname ||
        port < 1 || port > 65535) {
        return JNI_FALSE;
    }

    const char* ipStr = env->GetStringUTFChars(ip, nullptr);
    if (!ipStr) return JNI_FALSE;
    g_serverHost = ipStr;
    const bool useQuic = g_serverHost.compare(0, 7, "quic://") == 0;
    if (useQuic) g_serverHost.erase(0, 7);
    const char* nickStr = env->GetStringUTFChars(nickname, nullptr);
    if (!nickStr) {
        env->ReleaseStringUTFChars(ip, ipStr);
        return JNI_FALSE;
    }

    std::string normalizedNick = WorldRPG::Net::NormalizeAscii(nickStr);
    env->ReleaseStringUTFChars(nickname, nickStr);
    WorldRPG::Game::SetAmbientTrafficEnabled(false, false);
    if (g_sessionOpened) {
        g_state->network->StopNetwork();
        // The element table goes first: ApplyRemotePeds copies its peds and cars back into the remote records
        // every tick, so a table that outlives the session would recreate the old world after the reset.
        // No packet runs here: packets are handled in nativePulse, which takes g_stateMutex like this call.
        WorldRPG::ServerWorld::ResetSession();
        WorldRPG::RemoteVehicle::ResetSession();
        WorldRPG::Game::RequestRemoteReset();
        WorldRPG::Script::ActiveVmResetSession();
    }
    g_lastConnect.address = ipStr;
    env->ReleaseStringUTFChars(ip, ipStr);
    g_lastConnect.port = static_cast<uint16_t>(port);
    g_lastConnect.quic = useQuic;
    g_lastConnect.nick = normalizedNick;
    // Scheduling a join only starts an attempt. It does not verify a session.
    // The Neon join handshake (PLAYER_JOIN -> MOD_NAME -> PLAYER_JOINDATA ->
    // SERVER_JOIN_COMPLETE -> RPCs -> SERVER_JOINEDGAME) is driven by
    // NeonJoinClient from nativePulse once the transport is up.
    g_lastConnect.profile = joinProfile == static_cast<jint>(WorldRPG::Net::JoinProfile::LegacyMobile)
        ? WorldRPG::Net::JoinProfile::LegacyMobile
        : WorldRPG::Net::JoinProfile::Standard;
    // The Server version choice belongs to PC Network. Off it, the join is
    // the stock one: current netcode, no release fallback.
    // StartTransport picks QUIC before PC Network, so a quic:// address never goes through netc.
    const bool pcNetwork = !useQuic && !g_pcNetwork.netcPath.empty();
    const auto release = pcNetwork ? g_requestedRelease : WorldRPG::Net::NetcodeRelease::Current;
    if (g_serverHost.empty() || normalizedNick.empty() || !StartTransport(env, thiz) ||
        !g_state->join->Begin(*g_state->network, normalizedNick, "", g_lastConnect.profile, release, g_releaseHint)) {
        g_state->network->StopNetwork();
        return JNI_FALSE;
    }
    g_enterServer = true;
    g_sessionOpened = true;
    {
        // The phone opens the loading screen at connect: GTA is still loading underneath, and its
        // counter is the first part of the bar. Desktop shows CConnectManager's box here instead.
        char status[160];
        std::snprintf(status, sizeof(status), "Connecting to %s:%d", g_serverHost.c_str(), static_cast<int>(port));
        WorldRPG::ConnectionLoading::End();
        WorldRPG::ConnectionLoading::Begin(status);
    }
    return JNI_TRUE;
}

JNIEXPORT void JNICALL
Java_com_saonline_neonlauncher_game_GameActivity_nativeSurfaceChanged(
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

JNIEXPORT jboolean JNICALL
Java_com_saonline_neonlauncher_game_EngineHostActivity_nativeCancelIdentityRequest(JNIEnv* env, jobject thiz, jlong requestId) {
    const std::lock_guard lock(g_stateMutex);
    if (!isOwner(env, thiz) || !g_state->join->CancelIdentityRequest(static_cast<uint64_t>(requestId))) return JNI_FALSE;
    g_state->network->StopNetwork();
    WorldRPG::ConnectionLoading::End();
    return JNI_TRUE;
}

JNIEXPORT jboolean JNICALL
Java_com_saonline_neonlauncher_game_EngineHostActivity_nativeSetNetworkSerial(JNIEnv* env, jobject thiz, jstring serial) {
    const std::lock_guard lock(g_stateMutex);
    if (!isOwner(env, thiz) || !serial || env->GetStringUTFLength(serial) != 32) return JNI_FALSE;
    const char* value = env->GetStringUTFChars(serial, nullptr);
    if (!value) return JNI_FALSE;
    const bool ok = g_state->network->SetSerial(value);
    env->ReleaseStringUTFChars(serial, value);
    return ok ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_saonline_neonlauncher_game_EngineHostActivity_nativeProvideIdentityTicket(JNIEnv* env, jobject thiz, jlong requestId,
        jstring ticket, jstring error) {
    const std::lock_guard lock(g_stateMutex);
    if (!isOwner(env, thiz) || !ticket || !error) return;
    if (env->GetStringUTFLength(ticket) > 4096 || env->GetStringUTFLength(error) > 512) return;
    const char* ticketText = env->GetStringUTFChars(ticket, nullptr);
    if (!ticketText) return;
    const char* errorText = env->GetStringUTFChars(error, nullptr);
    if (errorText) {
        g_state->join->ProvideIdentityTicket(static_cast<uint64_t>(requestId), ticketText, errorText);
        env->ReleaseStringUTFChars(error, errorText);
    }
    env->ReleaseStringUTFChars(ticket, ticketText);
}

JNIEXPORT void JNICALL
Java_com_saonline_neonlauncher_game_GameActivity_nativeOnTouch(
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

/// Activity window focus. Desktop CCore::IsFocused, sampled by CClientGame::OnWindowFocusChange.
JNIEXPORT void JNICALL
Java_com_saonline_neonlauncher_game_GameActivity_nativeWindowFocus(
    JNIEnv*, jobject, jboolean focused) {
    WorldRPG::Script::MtaWindowSetFocused(focused == JNI_TRUE);
}

// The engine host keeps RakNet and Lua timers running while backgrounded. Only
// the foreground host marshals draw commands and starts game-thread resources.
static void PulseClient(JNIEnv* env, jobject thiz, bool renderFrame) {
    const std::lock_guard lock(g_stateMutex);
    if (!isOwner(env, thiz) || !g_state->network) return;
    if (g_enterServer.load()) WorldRPG::Game::BeginServerSession();
    g_state->network->DoPulse();
    g_state->join->Pulse();
    if (g_state->join->TakeReleaseFallback()) {
        // Automatic server release: the server refused this netcode before the
        // join completed. Reopen the same transport and join once more with the
        // other release; a second refusal fails the join as usual.
        const auto release = g_state->join->FallbackRelease();
        g_state->network->StopNetwork();
        WorldRPG::ServerWorld::ResetSession();
        WorldRPG::RemoteVehicle::ResetSession();
        if (!StartTransport(env, thiz) ||
            !g_state->join->Begin(*g_state->network, g_lastConnect.nick, "", g_lastConnect.profile,
                                  WorldRPG::Net::NetcodeRelease::Automatic, release)) {
            g_state->network->StopNetwork();
            WorldRPG::ConnectionLoading::End();
            PostConnectionState(false, "Could not reconnect with the server's release");
        }
    }
    bool transferring = false;
    {
        // Transfer box (desktop CTransferBox): "downloaded|total" while files are in flight, then one
        // empty line. Sent only when the numbers change.
        static uint64_t lastDone = ~0ull, lastTotal = ~0ull;
        static bool shown = false;
        const auto transfer = WorldRPG::Net::HttpDownloader::GetTransfer();
        transferring = transfer.active && transfer.total > 0;
        if (transferring) {
            // Desktop CResourceFileDownloadManager::AddDownloadSize (245-269) on the first queued file,
            // then DoPulse (198-210) every pulse: loading status with bytes done / total, and the Lua event.
            if (!shown) {
                WorldRPG::ConnectionLoading::Update(WorldRPG::ConnectionLoading::Phase::Download, "Downloading resources", 0.0f);
                WorldRPG::Script::ActiveVmFireTransferVisibility(true);
            }
            WorldRPG::ConnectionLoading::Update(WorldRPG::ConnectionLoading::Phase::Download, "Downloading resources",
                static_cast<float>(static_cast<double>(transfer.done) / static_cast<double>(transfer.total)));
            WorldRPG::Script::ActiveVmFireTransferProgress(static_cast<double>(transfer.done),
                                                           static_cast<double>(transfer.total));
            if (transfer.done != lastDone || transfer.total != lastTotal) {
                lastDone = transfer.done;
                lastTotal = transfer.total;
                char line[64];
                std::snprintf(line, sizeof(line), "%llu|%llu", static_cast<unsigned long long>(transfer.done),
                              static_cast<unsigned long long>(transfer.total));
                WorldRPG::JniBridge::PostGameLine(3, line);
                shown = true;
            }
        } else if (shown) {
            shown = false;
            lastDone = lastTotal = ~0ull;
            WorldRPG::JniBridge::PostGameLine(3, "0|0");
            // Desktop DoPulse when the last group finished (214-232): hide, event, then "Processing resources".
            WorldRPG::Script::ActiveVmFireTransferVisibility(false);
            WorldRPG::ConnectionLoading::Update(WorldRPG::ConnectionLoading::Phase::Process, "Processing resources");
        }
    }
    // GTA's own load counter feeds the engine part of the bar until the session leaves the frontend.
    const bool gameReady = WorldRPG::Game::ServerSessionLive();
    WorldRPG::ConnectionLoading::SetEngine(gameReady ? 1.0f : WorldRPG::ConnectionLoading::ReadEngineLoad());
    // Desktop CClientGame::PulseConnectionLoading (CClientGame.cpp:3657-3672). GTA is loaded once the
    // session left the frontend (desktop m_bGameLoaded).
    WorldRPG::ConnectionLoading::Pulse(g_state->join->State() == WorldRPG::Net::JoinState::Complete, gameReady, transferring);
    WorldRPG::ConnectionLoading::Flush();
    // Initial resource scripts may load models synchronously through the
    // game's task queue. Run them once the game can service that queue.
    if (gameReady && renderFrame) WorldRPG::Script::ActiveVmPulseResources();
    const auto traffic = WorldRPG::Script::ResourceLoader::RunningAmbientTraffic(
        g_state->join->State() == WorldRPG::Net::JoinState::Complete);
    WorldRPG::Game::SetAmbientTrafficEnabled(traffic.pedestrians, traffic.vehicles);
    // CClientWeaponManager::DoPulse for createWeapon and touched server weapons.
    WorldRPG::Script::WeaponsPulse();
    if (!g_onDxFrameMethod || !g_gameActivityRef) return;
    const std::string frame = WorldRPG::Script::ActiveVmPump(renderFrame);
    // Commands that a key bind ran go to the server as a typed command does (COMMAND_Executed).
    for (const std::string& line : WorldRPG::Script::ActiveVmTakeServerCommands()) {
        if (g_state->join) g_state->join->SendChat(line);
    }
    // Keys the server bound (BIND_KEY): CClientGame::ProcessServerKeyBind.
    for (const auto& event : WorldRPG::Script::KeyBindsTakeServerEvents()) {
        WorldRPG::Net::SendKeyBind(event.control, event.down, event.name);
    }
    if (!renderFrame) return;
    // An empty frame is sent once, so the overlay clears what the scripts stopped drawing.
    static bool lastFrameEmpty = true;
    if (frame.empty() && lastFrameEmpty) return;
    lastFrameEmpty = frame.empty();
    const auto wide = WorldRPG::Utf8::ToUtf16(frame);
    jstring jtext = env->NewString(reinterpret_cast<const jchar*>(wide.data()), static_cast<jsize>(wide.size()));
    if (!jtext) return;
    env->CallVoidMethod(g_gameActivityRef, g_onDxFrameMethod, jtext);
    if (env->ExceptionCheck()) env->ExceptionClear();
    env->DeleteLocalRef(jtext);
}

JNIEXPORT void JNICALL
Java_com_saonline_neonlauncher_game_GameActivity_nativePulse(JNIEnv* env, jobject thiz) {
    PulseClient(env, thiz, true);
}

JNIEXPORT jstring JNICALL
Java_com_saonline_neonlauncher_game_EngineHostActivity_nativeMapSnapshot(JNIEnv* env, jobject) {
    float x = 0, y = 0, z = 0, heading = 0;
    WorldRPG::Game::CopyPose(x, y, z, heading);
    heading = WorldRPG::Game::CopyRadarHeading();
    const std::string text = WorldRPG::ServerWorld::MapSnapshot(x, y, heading);
    return env->NewStringUTF(text.c_str());
}

JNIEXPORT void JNICALL
Java_com_saonline_neonlauncher_game_EngineHostActivity_nativeCursorEvent(
    JNIEnv*, jobject, jint action, jfloat x, jfloat y) {
    WorldRPG::Script::ActiveVmQueueCursor(action, x, y);
}

JNIEXPORT void JNICALL
Java_com_saonline_neonlauncher_game_EngineHostActivity_nativeSoundEnded(
    JNIEnv*, jobject, jint id) {
    WorldRPG::Script::ActiveVmNoteSoundEnded(static_cast<uint32_t>(id));
}

JNIEXPORT void JNICALL
Java_com_saonline_neonlauncher_game_EngineHostActivity_nativeSoundLength(
    JNIEnv*, jobject, jint id, jdouble seconds) {
    WorldRPG::Script::ActiveVmNoteSoundLength(static_cast<uint32_t>(id), seconds);
}

JNIEXPORT void JNICALL
Java_com_saonline_neonlauncher_game_EngineHostActivity_nativeSoundStarted(
    JNIEnv* env, jobject, jint id, jstring reason) {
    std::string reasonStr = "play";
    if (reason) {
        const char* chars = env->GetStringUTFChars(reason, nullptr);
        if (chars) {
            reasonStr = chars;
            env->ReleaseStringUTFChars(reason, chars);
        }
    }
    WorldRPG::Script::ActiveVmNoteSoundStarted(static_cast<uint32_t>(id), reasonStr.c_str());
}

JNIEXPORT void JNICALL
Java_com_saonline_neonlauncher_game_EngineHostActivity_nativeSoundStream(
    JNIEnv* env, jobject, jint id, jboolean success, jdouble length, jstring streamName, jstring errorMsg) {
    std::string nameStr;
    if (streamName) {
        const char* chars = env->GetStringUTFChars(streamName, nullptr);
        if (chars) {
            nameStr = chars;
            env->ReleaseStringUTFChars(streamName, chars);
        }
    }
    std::string errStr;
    if (errorMsg) {
        const char* chars = env->GetStringUTFChars(errorMsg, nullptr);
        if (chars) {
            errStr = chars;
            env->ReleaseStringUTFChars(errorMsg, chars);
        }
    }
    WorldRPG::Script::ActiveVmNoteSoundStream(
        static_cast<uint32_t>(id), success != JNI_FALSE, length, nameStr.c_str(), errStr.c_str());
}

JNIEXPORT void JNICALL
Java_com_saonline_neonlauncher_game_EngineHostActivity_nativeSoundAnalysis(
    JNIEnv* env, jobject, jint id, jint leftLevel, jint rightLevel, jfloatArray fft, jfloatArray wave) {
    jfloat* fftPtr = fft ? env->GetFloatArrayElements(fft, nullptr) : nullptr;
    const jsize fftLen = fft ? env->GetArrayLength(fft) : 0;
    jfloat* wavePtr = wave ? env->GetFloatArrayElements(wave, nullptr) : nullptr;
    const jsize waveLen = wave ? env->GetArrayLength(wave) : 0;
    WorldRPG::Script::ActiveVmNoteSoundAnalysis(
        static_cast<uint32_t>(id), static_cast<int>(leftLevel), static_cast<int>(rightLevel),
        fftPtr, static_cast<int>(fftLen), wavePtr, static_cast<int>(waveLen));
    if (fftPtr) env->ReleaseFloatArrayElements(fft, fftPtr, JNI_ABORT);
    if (wavePtr) env->ReleaseFloatArrayElements(wave, wavePtr, JNI_ABORT);
}

JNIEXPORT void JNICALL
Java_com_saonline_neonlauncher_game_EngineHostActivity_nativeScriptInput(
    JNIEnv* env, jobject, jstring text, jint backspaces, jstring key) {
    std::string utf8;
    if (text) {
        const jchar* chars = env->GetStringChars(text, nullptr);
        if (!chars) return;
        const std::u16string wide(chars, chars + env->GetStringLength(text));
        utf8 = WorldRPG::Utf8::FromUtf16(wide);
        env->ReleaseStringChars(text, chars);
    }
    const char* name = key ? env->GetStringUTFChars(key, nullptr) : nullptr;
    if (key && !name) return;
    WorldRPG::Script::ActiveVmQueueScriptInput(utf8.c_str(), backspaces, name ? name : "");
    if (name) env->ReleaseStringUTFChars(key, name);
}

JNIEXPORT void JNICALL
Java_com_saonline_neonlauncher_game_EngineHostActivity_nativeSetScreenSize(
    JNIEnv* env, jobject thiz, jint width, jint height) {
    const std::lock_guard lock(g_stateMutex);
    if (!isOwner(env, thiz) || width <= 0 || height <= 0) return;
    WorldRPG::Script::ActiveVmSetScreen(width, height);
}

JNIEXPORT void JNICALL
Java_com_saonline_neonlauncher_game_GameActivity_nativeDestroy(JNIEnv* env, jobject thiz) {
    const std::lock_guard lock(g_stateMutex);
    if (!isOwner(env, thiz)) return;

    WorldRPG::Game::SetAmbientTrafficEnabled(false, false);
    // Detach callbacks holding raw pointers into this state before teardown.
    g_state->network->SetPacketHandler(nullptr);
    g_state->join->SetStateCallback({});
    g_state->join->Reset();
    g_state->network->SetConnectionStateCallback({});
    g_state->network->Shutdown();
    g_hudRenderer = nullptr;
    g_enterServer = false;
    WorldRPG::ConnectionLoading::End();
    // The Activity and its chat input bar are gone. The window is no longer foreground.
    WorldRPG::Script::MtaWindowSetChatInputActive(false);
    WorldRPG::Script::MtaWindowSetFocused(false);
    g_state.reset();
    env->DeleteGlobalRef(g_gameActivityRef);
    g_gameActivityRef = nullptr;
    g_onConnectionStateChangeMethod = nullptr;
    g_onGameLineMethod = nullptr;
    JNI_LOGI("nativeDestroy: client bridge released");
}

/// Set the private storage path published by the Android asset installer.
JNIEXPORT jboolean JNICALL
Java_com_saonline_neonlauncher_game_GameActivity_nativeSetAssetPath(
    JNIEnv* env, jobject thiz, jstring assetPath)
{
    const std::lock_guard lock(g_stateMutex);
    if (!isOwner(env, thiz) || !assetPath) return JNI_FALSE;

    const char* path = env->GetStringUTFChars(assetPath, nullptr);
    if (!path) return JNI_FALSE;
    struct stat metadata{};
    g_state->assetsReady = path[0] != '\0' && stat(path, &metadata) == 0 &&
        S_ISDIR(metadata.st_mode) && g_state->assetManager->initialize(path);
    if (g_state->assetsReady) {
        // Resource client files are cached next to the game data, in the app's private files dir:
        // <...>/files/game_assets/... -> <...>/files/mta_resource_cache.
        std::string root(path);
        const auto filesPos = root.find("/files/");
        root = filesPos != std::string::npos ? root.substr(0, filesPos + 6) : root;
        root += "/mta_resource_cache";
        mkdir(root.c_str(), 0700);
        WorldRPG::Net::HttpDownloader::SetResourceCacheRoot(root);
        JNI_LOGI("Resource cache: %s", root.c_str());
    }
    env->ReleaseStringUTFChars(assetPath, path);
    if (!g_state->assetsReady) JNI_LOGE("nativeSetAssetPath: installed directory is unavailable");
    return g_state->assetsReady ? JNI_TRUE : JNI_FALSE;
}

// The game screen is EngineHostActivity. These entry points share the
// GameActivity client so the join, download bar and chat run there.
JNIEXPORT jboolean JNICALL
Java_com_saonline_neonlauncher_game_EngineHostActivity_nativeInit(JNIEnv* env, jobject thiz) {
    return Java_com_saonline_neonlauncher_game_GameActivity_nativeInit(env, thiz);
}
JNIEXPORT jboolean JNICALL
Java_com_saonline_neonlauncher_game_EngineHostActivity_nativeSetAssetPath(JNIEnv* env, jobject thiz, jstring assetPath) {
    return Java_com_saonline_neonlauncher_game_GameActivity_nativeSetAssetPath(env, thiz, assetPath);
}
JNIEXPORT jboolean JNICALL
Java_com_saonline_neonlauncher_game_EngineHostActivity_nativeConnect(
    JNIEnv* env, jobject thiz, jstring ip, jint port, jstring nickname, jint joinProfile) {
    return Java_com_saonline_neonlauncher_game_GameActivity_nativeConnect(env, thiz, ip, port, nickname, joinProfile);
}
JNIEXPORT jboolean JNICALL
Java_com_saonline_neonlauncher_game_EngineHostActivity_nativeSetPcNetwork(
    JNIEnv* env, jobject thiz, jstring netcPath, jstring netcBuild, jstring dataDir, jstring productVersion) {
    const std::lock_guard lock(g_stateMutex);
    if (!isOwner(env, thiz) || !netcPath || !netcBuild || !dataDir || !productVersion) return JNI_FALSE;
    auto read = [env](jstring s) {
        const char* c = env->GetStringUTFChars(s, nullptr);
        std::string out = c ? c : "";
        if (c) env->ReleaseStringUTFChars(s, c);
        return out;
    };
    PcNetworkConfig config{read(netcPath), read(netcBuild), read(dataDir), read(productVersion)};
    if (!config.netcPath.empty() && (config.build.empty() || config.dataDir.empty())) return JNI_FALSE;
    g_pcNetwork = std::move(config);
    if (!g_pcNetwork.netcPath.empty())
        JNI_LOGI("PC Network compatibility on: netc %s (%s build)", g_pcNetwork.netcPath.c_str(), g_pcNetwork.build.c_str());
    return JNI_TRUE;
}
// Server release for the next nativeConnect: requested 0 Automatic, 1 Current,
// 2 Release 189; hint is the release Automatic tries first.
JNIEXPORT void JNICALL
Java_com_saonline_neonlauncher_game_EngineHostActivity_nativeSetNetcodeRelease(JNIEnv* env, jobject thiz, jint requested,
                                                                                jint hint) {
    const std::lock_guard lock(g_stateMutex);
    if (!isOwner(env, thiz)) return;
    using WorldRPG::Net::NetcodeRelease;
    auto pick = [](jint v, NetcodeRelease fallback) {
        return v == 1 ? NetcodeRelease::Current : v == 2 ? NetcodeRelease::Release189 : fallback;
    };
    g_requestedRelease = pick(requested, NetcodeRelease::Automatic);
    g_releaseHint = pick(hint, NetcodeRelease::Current);
}
// The release the current join speaks (1 Current, 2 Release 189), so the
// launcher can remember what an Automatic join found.
JNIEXPORT jint JNICALL
Java_com_saonline_neonlauncher_game_EngineHostActivity_nativeGetNetcodeRelease(JNIEnv* env, jobject thiz) {
    const std::lock_guard lock(g_stateMutex);
    if (!isOwner(env, thiz) || !g_state || !g_state->join) return 0;
    return g_state->join->ActiveRelease() == WorldRPG::Net::NetcodeRelease::Release189 ? 2 : 1;
}
JNIEXPORT void JNICALL
Java_com_saonline_neonlauncher_game_EngineHostActivity_nativePulse(JNIEnv* env, jobject thiz, jboolean renderFrame) {
    PulseClient(env, thiz, renderFrame == JNI_TRUE);
}
JNIEXPORT void JNICALL
Java_com_saonline_neonlauncher_game_EngineHostActivity_nativeDestroy(JNIEnv* env, jobject thiz) {
    Java_com_saonline_neonlauncher_game_GameActivity_nativeDestroy(env, thiz);
}

JNIEXPORT jboolean JNICALL
Java_com_saonline_neonlauncher_game_EngineHostActivity_nativeSendChat(JNIEnv* env, jobject thiz, jstring text) {
    const std::lock_guard lock(g_stateMutex);
    if (!isOwner(env, thiz) || !g_state || !g_state->join || !text) return JNI_FALSE;
    const char* raw = env->GetStringUTFChars(text, nullptr);
    if (!raw) return JNI_FALSE;
    const bool sent = g_state->join->SendChat(raw);
    // The client command handlers and onClientConsole see the same line (COMMAND_Executed).
    if (sent) WorldRPG::Script::ActiveVmQueueCommand(raw);
    env->ReleaseStringUTFChars(text, raw);
    return sent ? JNI_TRUE : JNI_FALSE;
}

// A hardware keyboard key in desktop key-bind spelling ("e", "F1", "space"). Unknown names are ignored.
JNIEXPORT void JNICALL
Java_com_saonline_neonlauncher_game_EngineHostActivity_nativeKeyEvent(JNIEnv* env, jobject, jstring key, jboolean down) {
    const char* name = key ? env->GetStringUTFChars(key, nullptr) : nullptr;
    if (!name) return;
    WorldRPG::Script::KeyBindsQueueKey(name, down == JNI_TRUE);
    env->ReleaseStringUTFChars(key, name);
}

JNIEXPORT void JNICALL
Java_com_saonline_neonlauncher_game_EngineHostActivity_nativeGamepadState(
    JNIEnv*, jobject, jboolean connected, jint index, jint buttons,
    jfloat lx, jfloat ly, jfloat rx, jfloat ry, jfloat lt, jfloat rt) {
    WorldRPG::Script::RocketGamepadInput state{};
    state.connected = connected == JNI_TRUE;
    state.index = index >= 0 ? index : 0;
    state.warButtons = static_cast<uint16_t>(buttons & 0xFFFF);
    state.axes[0] = lx;
    state.axes[1] = ly;
    state.axes[2] = rx;
    state.axes[3] = ry;
    state.axes[4] = lt;
    state.axes[5] = rt;
    WorldRPG::Script::RocketSoccerSetGamepad(state);
}

JNIEXPORT void JNICALL
Java_com_saonline_neonlauncher_game_EngineHostActivity_nativeVoiceSetTransmit(
    JNIEnv*, jobject, jboolean active) {
    WorldRPG::Audio::VoiceRuntime::SetTransmitActive(active == JNI_TRUE);
}

JNIEXPORT void JNICALL
Java_com_saonline_neonlauncher_game_EngineHostActivity_nativeVoiceMicPcm(
    JNIEnv* env, jobject, jshortArray samples, jint count) {
    if (!samples || count <= 0) return;
    const jsize arrLen = env->GetArrayLength(samples);
    const jsize toCopy = count < arrLen ? count : arrLen;
    if (toCopy <= 0) return;
    std::vector<int16_t> pcm(static_cast<size_t>(toCopy));
    env->GetShortArrayRegion(samples, 0, toCopy, reinterpret_cast<jshort*>(pcm.data()));
    WorldRPG::Audio::VoiceRuntime::OnMicPcm(pcm.data(), pcm.size());
}

JNIEXPORT void JNICALL
Java_com_saonline_neonlauncher_game_EngineHostActivity_nativeBrowserLoadingStart(
    JNIEnv* env, jobject, jint id, jstring url, jboolean isMainFrame) {
    std::string urlStr;
    if (url) {
        if (const char* raw = env->GetStringUTFChars(url, nullptr)) {
            urlStr = raw;
            env->ReleaseStringUTFChars(url, raw);
        }
    }
    WorldRPG::Script::BrowserNoteLoadingStart(static_cast<uint32_t>(id), urlStr.c_str(), isMainFrame == JNI_TRUE);
}

JNIEXPORT void JNICALL
Java_com_saonline_neonlauncher_game_EngineHostActivity_nativeBrowserDocumentReady(
    JNIEnv* env, jobject, jint id, jstring url, jstring title, jboolean canBack, jboolean canForward) {
    std::string urlStr, titleStr;
    if (url) {
        if (const char* raw = env->GetStringUTFChars(url, nullptr)) {
            urlStr = raw;
            env->ReleaseStringUTFChars(url, raw);
        }
    }
    if (title) {
        if (const char* raw = env->GetStringUTFChars(title, nullptr)) {
            titleStr = raw;
            env->ReleaseStringUTFChars(title, raw);
        }
    }
    WorldRPG::Script::BrowserNoteDocumentReady(
        static_cast<uint32_t>(id), urlStr.c_str(), titleStr.c_str(), canBack == JNI_TRUE, canForward == JNI_TRUE);
}

JNIEXPORT void JNICALL
Java_com_saonline_neonlauncher_game_EngineHostActivity_nativeBrowserLoadingFailed(
    JNIEnv* env, jobject, jint id, jstring url, jint errorCode, jstring description) {
    std::string urlStr, descStr;
    if (url) {
        if (const char* raw = env->GetStringUTFChars(url, nullptr)) {
            urlStr = raw;
            env->ReleaseStringUTFChars(url, raw);
        }
    }
    if (description) {
        if (const char* raw = env->GetStringUTFChars(description, nullptr)) {
            descStr = raw;
            env->ReleaseStringUTFChars(description, raw);
        }
    }
    WorldRPG::Script::BrowserNoteLoadingFailed(
        static_cast<uint32_t>(id), urlStr.c_str(), static_cast<int>(errorCode), descStr.c_str());
}

JNIEXPORT void JNICALL
Java_com_saonline_neonlauncher_game_EngineHostActivity_nativeBrowserSource(
    JNIEnv* env, jobject, jint id, jstring html) {
    std::string htmlStr;
    if (html) {
        if (const char* raw = env->GetStringUTFChars(html, nullptr)) {
            htmlStr = raw;
            env->ReleaseStringUTFChars(html, raw);
        }
    }
    WorldRPG::Script::BrowserNoteSource(static_cast<uint32_t>(id), htmlStr.c_str());
}

JNIEXPORT jboolean JNICALL
Java_com_saonline_neonlauncher_game_EngineHostActivity_nativeHardwareMouseReady(JNIEnv*, jobject) {
    return WorldRPG::Game::HardwareMouseReady() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_saonline_neonlauncher_game_EngineHostActivity_nativeHardwareMouse(
    JNIEnv*, jobject, jfloat x, jfloat y, jint buttons, jfloat wheel, jboolean captured) {
    WorldRPG::Game::QueueHardwareMouse(x, y, static_cast<unsigned>(buttons), wheel, captured == JNI_TRUE);
}

// GTA mobile options (gtasa/MobileSettings.h) for the in-game settings screen.
JNIEXPORT jboolean JNICALL
Java_com_saonline_neonlauncher_game_EngineHostActivity_nativeSetGameSetting(JNIEnv*, jobject, jint index, jint value) {
    return WorldRPG::Game::SetMobileSetting(index, value) ? JNI_TRUE : JNI_FALSE;
}

// {value, min, max}, or null when the table is not available.
JNIEXPORT jintArray JNICALL
Java_com_saonline_neonlauncher_game_EngineHostActivity_nativeGetGameSetting(JNIEnv* env, jobject, jint index) {
    int value = 0, min = 0, max = 0;
    if (!WorldRPG::Game::GetMobileSetting(index, value, min, max)) return nullptr;
    jintArray out = env->NewIntArray(3);
    if (!out) return nullptr;
    const jint data[3] = {value, min, max};
    env->SetIntArrayRegion(out, 0, 3, data);
    return out;
}

// The keys scripts have bound ("e|F1|p"), for the on-screen key list.
JNIEXPORT jstring JNICALL
Java_com_saonline_neonlauncher_game_EngineHostActivity_nativeBoundKeys(JNIEnv* env, jobject) {
    return env->NewStringUTF(WorldRPG::Script::KeyBindsBoundKeys().c_str());
}

// The chat input bar opened or closed (desktop CLocalGUI::IsChatBoxInputEnabled, read by
// isChatBoxInputActive and isMTAWindowActive).
JNIEXPORT void JNICALL
Java_com_saonline_neonlauncher_game_EngineHostActivity_nativeChatInputActive(JNIEnv*, jobject, jboolean active) {
    WorldRPG::Script::MtaWindowSetChatInputActive(active == JNI_TRUE);
}

JNIEXPORT void JNICALL
Java_com_saonline_neonlauncher_game_EngineHostActivity_nativeDropConnection(JNIEnv* env, jobject thiz)
{
    const std::lock_guard lock(g_stateMutex);
    if (!isOwner(env, thiz) || !g_state || !g_state->network) return;
    // Report "Connection lost" through the join callback (P3.1), then stop the transport
    // so a later Retry starts a clean session.
    g_state->join->ForceConnectionLost();
    g_state->network->StopNetwork();
    WorldRPG::Controls::ResetAll();
    JNI_LOGI("nativeDropConnection: transport stopped for /disconnect");
}

} // extern "C"

namespace WorldRPG::Net {
// Lua runs inside nativePulse, which already holds g_stateMutex. Locking again
// would deadlock. StartNetwork writes the host on the activity thread and takes
// that same mutex, so it cannot run during this call.
std::string QueryConnectedServer(bool includePort) {
    if (!g_state || !g_state->network) return {};
    return g_state->network->GetConnectedServer(includePort);
}
}  // namespace WorldRPG::Net
