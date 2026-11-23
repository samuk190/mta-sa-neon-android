#include "QuicGameClient.h"
#ifdef __ANDROID__
#include "libneon_gameplay.h"
#include <array>
#include <atomic>
#include <thread>
#include <vector>

namespace WorldRPG::Net {
namespace {
struct InstallKey {
    JavaVM* vm;
    jclass type;
    jmethodID sign;
};

int SignDigest(uintptr_t key, const uint8_t* digest, uint8_t* der, int capacity) {
    auto& signer = *reinterpret_cast<InstallKey*>(key);
    JNIEnv* env = nullptr;
    auto status = signer.vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
    bool attach = status == JNI_EDETACHED;
    if (attach && signer.vm->AttachCurrentThread(&env, nullptr) != JNI_OK) return 0;
    if (!env) return 0;
    int length = 0;
    if (env->PushLocalFrame(4) == JNI_OK) {
        auto data = env->NewByteArray(32);
        if (data) env->SetByteArrayRegion(data, 0, 32, reinterpret_cast<const jbyte*>(digest));
        if (!env->ExceptionCheck() && data) {
            auto sig = static_cast<jbyteArray>(env->CallStaticObjectMethod(signer.type, signer.sign, data));
            if (!env->ExceptionCheck() && sig && env->GetArrayLength(sig) <= capacity) {
                length = env->GetArrayLength(sig);
                env->GetByteArrayRegion(sig, 0, length, reinterpret_cast<jbyte*>(der));
            }
        }
        env->PopLocalFrame(nullptr);
    }
    if (env->ExceptionCheck()) { env->ExceptionClear(); length = 0; }
    if (attach) signer.vm->DetachCurrentThread();
    return length;
}
}

struct QuicGameClient::Connection {
    uint64_t handle = 0;
    std::string error;
    std::atomic<int> status{0};
    ~Connection() { if (handle) NeonGameClose(handle); }
};

QuicGameClient::QuicGameClient(std::shared_ptr<Connection> conn) : connection(std::move(conn)) {}
QuicGameClient::~QuicGameClient() = default;

std::unique_ptr<QuicGameClient> QuicGameClient::Connect(JNIEnv* env, jobject activity,
                                                         const std::string& host, uint16_t port) {
    auto local = env->FindClass("com/saonline/neonlauncher/network/AndroidInstallCredential");
    if (!local) { env->ExceptionClear(); return nullptr; }
    auto cert = env->GetStaticMethodID(local, "gameplayCertificate", "(Landroid/content/Context;)[B");
    auto sign = env->GetStaticMethodID(local, "signGameplayDigest", "([B)[B");
    auto roots = env->GetStaticMethodID(local, "gameplayServerRoots", "(Landroid/content/Context;)Ljava/lang/String;");
    JavaVM* vm = nullptr;
    if (!cert || !sign || !roots || env->GetJavaVM(&vm) != JNI_OK) { env->ExceptionClear(); return nullptr; }
    auto type = static_cast<jclass>(env->NewGlobalRef(local));
    auto context = env->NewGlobalRef(activity);
    if (!type || !context) {
        env->ExceptionClear();
        if (type) env->DeleteGlobalRef(type);
        if (context) env->DeleteGlobalRef(context);
        return nullptr;
    }
    auto conn = std::make_shared<Connection>();
    std::thread([=] {
        JNIEnv* worker = nullptr;
        if (vm->AttachCurrentThread(&worker, nullptr) != JNI_OK) {
            conn->error = "Could not attach Android credential worker";
            conn->status.store(-1);
            return;
        }
        auto encoded = static_cast<jbyteArray>(worker->CallStaticObjectMethod(type, cert, context));
        std::vector<uint8_t> der;
        std::string rootFile;
        if (!worker->ExceptionCheck() && encoded) {
            auto size = worker->GetArrayLength(encoded);
            if (size > 0 && size <= 65536) {
                der.resize(size);
                worker->GetByteArrayRegion(encoded, 0, size, reinterpret_cast<jbyte*>(der.data()));
            }
        }
        if (!worker->ExceptionCheck()) {
            auto file = static_cast<jstring>(worker->CallStaticObjectMethod(type, roots, context));
            if (!worker->ExceptionCheck() && file) {
                if (auto path = worker->GetStringUTFChars(file, nullptr)) {
                    rootFile = path;
                    worker->ReleaseStringUTFChars(file, path);
                }
            }
        }
        if (worker->ExceptionCheck()) { worker->ExceptionClear(); der.clear(); }
        if (der.empty()) {
            conn->error = "Android install credential unavailable";
        } else {
            auto address = host + ":" + std::to_string(port);
            auto name = host;
            InstallKey signer{vm, type, sign};
            std::array<char, 512> error{};
            conn->handle = NeonGameDial(address.data(), name.data(), rootFile.data(), der.data(), der.size(),
                                        reinterpret_cast<uintptr_t>(&signer), SignDigest, error.data(), error.size());
            if (!conn->handle) conn->error = error.data();
        }
        worker->DeleteGlobalRef(type);
        worker->DeleteGlobalRef(context);
        vm->DetachCurrentThread();
        conn->status.store(conn->handle ? 1 : -1);
    }).detach();
    return std::unique_ptr<QuicGameClient>(new QuicGameClient(std::move(conn)));
}

int QuicGameClient::Status(std::string& error) const {
    int result = connection->status.load();
    if (result < 0) error = connection->error;
    return result;
}
int QuicGameClient::Poll(uint8_t* kind, uint32_t* bits, uint8_t* payload, int capacity) {
    return NeonGamePoll(connection->handle, kind, bits, payload, capacity);
}
bool QuicGameClient::Send(uint8_t kind, uint32_t bits, const uint8_t* payload, uint8_t mode, uint8_t channel) {
    if (connection->status.load() != 1) return false;
    return NeonGameSend(connection->handle, kind, bits, const_cast<uint8_t*>(payload), mode, channel) == 1;
}
}
#endif
