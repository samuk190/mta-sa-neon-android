#include "PcNetworkTransport.h"

#include "netemu/NetEmuApi.h"

#include <dlfcn.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <thread>

#ifdef __ANDROID__
#include <android/log.h>
#define PCNET_LOGI(...) __android_log_print(ANDROID_LOG_INFO, "SAOPcNetwork", __VA_ARGS__)
#define PCNET_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "SAOPcNetwork", __VA_ARGS__)
#else
#include <cstdio>
#define PCNET_LOGI(...) (std::fprintf(stderr, __VA_ARGS__), std::fputc('\n', stderr))
#define PCNET_LOGE(...) (std::fprintf(stderr, __VA_ARGS__), std::fputc('\n', stderr))
#endif

namespace WorldRPG::Net {
namespace {

constexpr uint32_t kConnectTimeoutMs = 30000;  // after netc is up; RakNet's own handshake retries inside it
constexpr size_t kMaxPacketBytes = 16u << 20;

uint64_t NowMs() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

// The library stays open for the life of the process: Unicorn keeps global
// state, and reloading it buys nothing.
const NetEmuApi* OpenApi(std::string* error) {
    static std::mutex mutex;
    static const NetEmuApi* api = nullptr;
    std::lock_guard lk(mutex);
    if (api) return api;
    void* lib = ::dlopen("libsaonline_netemu.so", RTLD_NOW | RTLD_LOCAL);
    if (!lib) {
        if (error) *error = std::string("cannot open libsaonline_netemu.so: ") + ::dlerror();
        return nullptr;
    }
    auto get = reinterpret_cast<NetEmuGetApiFn>(::dlsym(lib, "netemu_get_api"));
    const NetEmuApi* a = get ? get() : nullptr;
    if (!a || a->abi_version != NETEMU_ABI_VERSION) {
        if (error) *error = "libsaonline_netemu.so has an incompatible ABI";
        return nullptr;
    }
    api = a;
    return api;
}

// Copies bits [from, to) of src (MSB-first) to dst starting at bit 0.
void CopyBits(const uint8_t* src, size_t from, size_t to, uint8_t* dst) {
    const size_t count = to - from;
    const size_t shift = from % 8;
    const uint8_t* s = src + from / 8;
    const size_t bytes = (count + 7) / 8;
    if (shift == 0) {
        std::memcpy(dst, s, bytes);
    } else {
        const size_t srcBytes = (shift + count + 7) / 8;  // bytes of src the range touches
        for (size_t i = 0; i < bytes; ++i) {
            const uint8_t hi = static_cast<uint8_t>(s[i] << shift);
            const uint8_t lo = i + 1 < srcBytes ? static_cast<uint8_t>(s[i + 1] >> (8 - shift)) : 0;
            dst[i] = hi | lo;
        }
    }
    if (count % 8) dst[bytes - 1] &= static_cast<uint8_t>(0xFF << (8 - count % 8));
}

}  // namespace

// state and cancelled are seq_cst: the loader stores state then reads
// cancelled, the destructor stores cancelled then reads state, and at least
// one of them must see the other's store.
struct PcNetworkTransport::Session {
    const NetEmuApi* api = nullptr;
    NetEmuHandle* handle = nullptr;  // set by the loader before state becomes 1
    // 0 loading, 1 netc up and StartNetwork called, 2 failed (error set).
    std::atomic<int> state{0};
    std::atomic<bool> cancelled{false};
    std::string error;
    std::mutex mutex;  // one netc call at a time
    // Settings made before netc was up (0 = none).
    uint16_t bitStreamVersion = 0;
    uint32_t timeoutMs = 0;
    void ApplyPending() {
        if (bitStreamVersion) api->set_server_bitstream_version(handle, bitStreamVersion);
        if (timeoutMs) api->set_timeout_time(handle, timeoutMs);
    }
};

std::unique_ptr<PcNetworkTransport> PcNetworkTransport::Start(const Options& options, const std::string& host, uint16_t port,
                                                              std::string* error) {
    const NetEmuApi* api = OpenApi(error);
    if (!api) return nullptr;
    std::unique_ptr<PcNetworkTransport> t(new PcNetworkTransport());
    t->session_ = std::make_shared<Session>();
    t->session_->api = api;
    std::thread([session = t->session_, options, host, port] {
        NetEmuConfig cfg{};
        cfg.size = sizeof(cfg);
        cfg.dll_path = options.netcPath.c_str();
        cfg.data_dir = options.dataDir.c_str();
        cfg.product_version = options.productVersion.empty() ? nullptr : options.productVersion.c_str();
        char err[256] = {};
        const uint64_t started = NowMs();
        NetEmuHandle* h = session->api->load(&cfg, err, sizeof(err));
        if (!h) {
            session->error = std::string("PC network module failed to load: ") + err;
            session->state.store(2);
            return;
        }
        PCNET_LOGI("netc loaded in %llu ms", static_cast<unsigned long long>(NowMs() - started));
        if (session->cancelled.load() || !session->api->start_network(h, host.c_str(), port, 0)) {
            if (!session->cancelled.load()) session->error = "PC network module could not start the connection";
            session->api->unload(h);
            session->state.store(2);
            return;
        }
        {
            std::lock_guard lk(session->mutex);
            session->handle = h;
            session->ApplyPending();
        }
        session->state.store(1);
        // Cancelled while StartNetwork ran: the transport is gone, clean up here.
        if (session->cancelled.load()) {
            std::lock_guard lk(session->mutex);
            if (session->handle) {  // the destructor may have got here first
                session->api->stop_network(session->handle);
                session->api->unload(session->handle);
                session->handle = nullptr;
            }
        }
    }).detach();
    PCNET_LOGI("PC network: loading %s for %s:%u", options.netcPath.c_str(), host.c_str(), port);
    return t;
}

PcNetworkTransport::~PcNetworkTransport() {
    Session& s = *session_;
    s.cancelled.store(true);
    if (s.state.load() != 1) return;  // still loading: the loader cleans up
    std::lock_guard lk(s.mutex);
    if (s.handle) {
        s.api->stop_network(s.handle);
        s.api->unload(s.handle);
        s.handle = nullptr;
    }
}

bool PcNetworkTransport::Up() {
    return session_->state.load() == 1 && session_->handle;
}

PcNetworkTransport::Status PcNetworkTransport::Pulse(std::string* error) {
    Session& s = *session_;
    const int state = s.state.load();
    if (state == 0) return Status::Loading;
    if (state == 2) {
        if (error) *error = s.error;
        return Status::Failed;
    }
    std::lock_guard lk(s.mutex);
    if (!s.handle) {
        if (error) *error = "PC network module stopped";
        return Status::Failed;
    }
    if (!connectStartMs_) connectStartMs_ = NowMs();
    s.api->do_pulse(s.handle);
    if (s.api->session_over(s.handle)) {
        if (error) *error = "PC network module stopped";
        return Status::Failed;
    }
    // As the desktop CConnectManager::DoPulse: failure is a connection error
    // code (or the timeout before the first connect), never IsConnected()
    // turning false, which netc also reports while it runs its own join.
    // Disconnects after that arrive as game packets.
    if (const uint8_t code = s.api->get_connection_error(s.handle)) {
        PCNET_LOGE("PC network: connection error %u", code);
        if (error) *error = "Connection failed (PC network error " + std::to_string(code) + ")";
        return Status::Failed;
    }
    if (!connected_) {
        if (s.api->is_connected(s.handle)) {
            connected_ = true;
            PCNET_LOGI("PC network: connected");
            return Status::Connected;
        }
        if (NowMs() - connectStartMs_ > kConnectTimeoutMs) {
            if (error) *error = "Connection timed out";
            return Status::Failed;
        }
        return Status::Connecting;
    }
    return Status::Connected;
}

bool PcNetworkTransport::Pop(std::vector<uint8_t>& packet, size_t& bits) {
    if (!Up()) return false;
    Session& s = *session_;
    std::lock_guard lk(s.mutex);
    uint8_t id = 0;
    uint32_t total = 0, offset = 0;
    if (scratch_.empty()) scratch_.resize(64 * 1024);
    int r = s.api->pop_packet(s.handle, &id, scratch_.data(), scratch_.size(), &total, &offset);
    if (r < 0) {
        const size_t need = (static_cast<size_t>(total) + 7) / 8;
        if (need > kMaxPacketBytes) {
            PCNET_LOGE("PC network: %zu-byte packet dropped", need);
            s.api->drop_packet(s.handle);
            return false;
        }
        scratch_.resize(need);
        r = s.api->pop_packet(s.handle, &id, scratch_.data(), scratch_.size(), &total, &offset);
    }
    if (r != 1) return false;
    // netc already read past what it consumed (the id); the payload starts at
    // its read cursor.
    const size_t from = std::min<size_t>(offset, total);
    const size_t payloadBits = total - from;
    packet.assign(1 + (payloadBits + 7) / 8, 0);
    packet[0] = id;
    if (payloadBits) CopyBits(scratch_.data(), from, total, packet.data() + 1);
    bits = 8 + payloadBits;
    return true;
}

bool PcNetworkTransport::Send(uint8_t id, const uint8_t* payload, size_t bits, int priority, int reliability, int ordering) {
    if (!Up()) return false;
    Session& s = *session_;
    std::lock_guard lk(s.mutex);
    if (s.api->send_packet(s.handle, id, payload, bits, priority, reliability, ordering) == 1) return true;
    PCNET_LOGE("PC network: netc refused packet %u (%zu bits, priority %d, reliability %d, ordering %d)", id, bits,
               priority, reliability, ordering);
    return false;
}

int PcNetworkTransport::Ping() {
    if (!Up()) return 0;
    std::lock_guard lk(session_->mutex);
    return session_->api->get_ping(session_->handle);
}

std::string PcNetworkTransport::Serial() {
    if (!Up()) return {};
    std::lock_guard lk(session_->mutex);
    char buf[64] = {};
    session_->api->get_serial(session_->handle, buf, sizeof(buf));
    return buf;
}

bool PcNetworkTransport::IsConnected() {
    if (!Up()) return false;
    std::lock_guard lk(session_->mutex);
    return session_->api->is_connected(session_->handle) != 0;
}

uint8_t PcNetworkTransport::ConnectionError() {
    if (!Up()) return 0;
    std::lock_guard lk(session_->mutex);
    return session_->api->get_connection_error(session_->handle);
}

void PcNetworkTransport::SetServerBitStreamVersion(uint16_t version) {
    std::lock_guard lk(session_->mutex);
    session_->bitStreamVersion = version;
    if (session_->handle) session_->api->set_server_bitstream_version(session_->handle, version);
}

void PcNetworkTransport::SetTimeoutTime(uint32_t ms) {
    std::lock_guard lk(session_->mutex);
    session_->timeoutMs = ms;
    if (session_->handle) session_->api->set_timeout_time(session_->handle, ms);
}

}  // namespace WorldRPG::Net
