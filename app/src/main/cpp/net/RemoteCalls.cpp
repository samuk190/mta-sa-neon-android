#include "RemoteCalls.h"

extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#ifdef __ANDROID__
#include <android/log.h>
#define REMOTE_LOGI(...) __android_log_print(ANDROID_LOG_INFO, "WorldRPGNeonHttp", __VA_ARGS__)
#else
#include <cstdio>
#define REMOTE_LOGI(...) do { std::printf(__VA_ARGS__); std::printf("\n"); } while (0)
#endif

namespace WorldRPG::Net {

// Headless builds do not link jni/JNI_OnLoad.cpp, so this weak definition is
// the one that remains. The phone's strong definition calls GetConnectedServer.
__attribute__((weak)) std::string QueryConnectedServer(bool includePort) {
    (void)includePort;
    return {};
}

namespace RemoteCalls {
namespace {

// Desktop Client/sdk/net/CNet.h:19. Queue names map into this many slots.
constexpr int kMaxQueues = 100;
// Same worker clamp as the resource-file downloader. Slots stay at 100;
// a script cannot open 100 sockets at once.
constexpr int kWorkerCount = 4;
// Phone limits. Desktop keeps the whole body; the callback builds a Lua string.
constexpr size_t kMaxBody = 4u * 1024u * 1024u;
constexpr size_t kMaxHeaders = 64u * 1024u;
constexpr int kMaxAttempts = 100;
// CURLOPT_CONNECTTIMEOUT of 0 uses curl's 300s default. A zero Linux
// SO_RCVTIMEO means "wait forever", so 0 is mapped to that default.
constexpr int kMaxTimeoutMs = 300000;
constexpr const char* kDefaultQueue = "default";

// libcurl codes scripts already test against (MTA's download manager).
constexpr int kErrUnsupportedProtocol = 1;
constexpr int kErrUrlMalformat = 3;
constexpr int kErrCouldntResolve = 6;
constexpr int kErrCouldntConnect = 7;
constexpr int kErrWeirdReply = 8;
constexpr int kErrTimeout = 28;
constexpr int kErrAborted = 42;
constexpr int kErrTooManyRedirects = 47;
constexpr int kErrSendError = 55;
constexpr int kErrRecvError = 56;
constexpr int kErrFileSize = 63;

std::string Lower(std::string value) {
    for (char& c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return value;
}

std::string Upper(std::string value) {
    for (char& c : value) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return value;
}

bool HasCrlf(const std::string& value) {
    return value.find('\r') != std::string::npos || value.find('\n') != std::string::npos;
}

std::string Trim(const std::string& value) {
    size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin]))) ++begin;
    size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1]))) --end;
    return value.substr(begin, end - begin);
}

// Same clock as getTickCount (LuaVMStub.cpp DxNow), so `start` can be compared with it.
uint64_t NowMs() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::steady_clock::now().time_since_epoch())
                                     .count());
}

int EffectiveTimeout(uint32_t requested) {
    if (requested == 0 || requested > static_cast<uint32_t>(kMaxTimeoutMs)) return kMaxTimeoutMs;
    return static_cast<int>(requested);
}

std::string Base64(const std::string& input) {
    static constexpr char kTable[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((input.size() + 2) / 3) * 4);
    unsigned value = 0;
    int bits = -6;
    for (unsigned char c : input) {
        value = (value << 8) + c;
        bits += 8;
        while (bits >= 0) {
            out.push_back(kTable[(value >> bits) & 0x3F]);
            bits -= 6;
        }
    }
    if (bits > -6) out.push_back(kTable[((value << 8) >> (bits + 8)) & 0x3F]);
    while (out.size() % 4) out.push_back('=');
    return out;
}

struct Url {
    std::string host;
    std::string port = "80";
    std::string path = "/";
    std::string user;
    std::string password;
};

// http:// only. https and any other scheme fail before a socket is opened.
// Returns false and sets `code` to the curl-style error.
bool ParseUrl(const std::string& raw, Url& url, int& code) {
    code = kErrUrlMalformat;
    if (raw.empty() || HasCrlf(raw) || raw.size() > 8192) return false;
    const size_t scheme = raw.find("://");
    if (scheme == std::string::npos || scheme == 0) return false;
    const std::string kind = Lower(raw.substr(0, scheme));
    if (kind == "https") {
        code = kErrUnsupportedProtocol;
        return false;
    }
    if (kind != "http") {
        code = kErrUnsupportedProtocol;
        return false;
    }
    std::string rest = raw.substr(scheme + 3);
    const size_t hash = rest.find('#');
    if (hash != std::string::npos) rest.erase(hash);
    const size_t slash = rest.find('/');
    const size_t question = rest.find('?');
    size_t authorityEnd = rest.size();
    if (slash != std::string::npos) authorityEnd = slash;
    if (question != std::string::npos && question < authorityEnd) authorityEnd = question;
    const std::string authority = rest.substr(0, authorityEnd);
    std::string path = rest.substr(authorityEnd);
    if (path.empty()) path = "/";
    else if (path[0] == '?') path.insert(path.begin(), '/');
    if (authority.empty()) return false;

    std::string hostport = authority;
    const size_t at = authority.rfind('@');
    if (at != std::string::npos) {
        const std::string userinfo = authority.substr(0, at);
        hostport = authority.substr(at + 1);
        const size_t colon = userinfo.find(':');
        if (colon == std::string::npos) {
            url.user = userinfo;
        } else {
            url.user = userinfo.substr(0, colon);
            url.password = userinfo.substr(colon + 1);
        }
    }
    if (hostport.empty() || HasCrlf(hostport)) return false;
    if (hostport[0] == '[') {
        const size_t end = hostport.find(']');
        if (end == std::string::npos) return false;
        url.host = hostport.substr(1, end - 1);
        if (end + 1 < hostport.size()) {
            if (hostport[end + 1] != ':') return false;
            url.port = hostport.substr(end + 2);
        }
    } else {
        const size_t colon = hostport.rfind(':');
        if (colon != std::string::npos) {
            if (hostport.find(':') != colon) return false;
            url.host = hostport.substr(0, colon);
            url.port = hostport.substr(colon + 1);
        } else {
            url.host = hostport;
        }
    }
    if (url.host.empty() || url.port.empty()) return false;
    for (char c : url.port) {
        if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    }
    url.path = path;
    code = 0;
    return true;
}

std::string HostHeader(const Url& url) {
    const bool v6 = url.host.find(':') != std::string::npos;
    std::string host = v6 ? "[" + url.host + "]" : url.host;
    if (url.port != "80") host += ":" + url.port;
    return host;
}

// Desktop CRemoteCalls.cpp:217 splits the curl header block on "\\n" then ": ".
void PushResponseHeaders(lua_State* L, const std::string& text) {
    lua_newtable(L);
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(pos, end - pos);
        pos = end < text.size() ? end + 1 : text.size();
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t split = line.find(": ");
        if (split == std::string::npos || split == 0) continue;
        if (line.find('\0') != std::string::npos) continue;
        const std::string key = line.substr(0, split);
        const std::string value = line.substr(split + 2);
        lua_pushlstring(L, value.data(), value.size());
        lua_setfield(L, -2, key.c_str());
    }
}

struct Options {
    bool legacy = false;
    uint32_t attempts = 10;
    uint32_t timeoutMs = 10000;
    uint32_t maxRedirects = 8;
    std::string method;  // empty: desktop info reports POST (CLuaNetworkDefs.cpp:231)
    std::string queue = kDefaultQueue;
    std::string postData;
    bool postBinary = false;
    std::string username;
    std::string password;
    std::map<std::string, std::string> headers;
    std::map<std::string, std::string> formFields;
};

struct Done {
    uint64_t id = 0;
    uint64_t epoch = 0;
    bool legacy = false;
    bool gotStatus = false;
    int statusCode = 0;
    std::string body;
    std::string headers;
};

struct Call {
    Call() = default;
    Call(const Call&) = delete;
    Call& operator=(const Call&) = delete;

    uint64_t id = 0;
    uint64_t epoch = 0;
    uint64_t startMs = 0;
    int slot = -1;
    bool finishing = false;
    Options options;
    std::string url;
    std::string wireMethod;
    std::string wireBody;
    std::string contentType;
    bool sendAuth = false;
    int callbackRef = LUA_NOREF;
    int argsRef = LUA_NOREF;
    std::atomic<bool> cancelled{false};
    std::atomic<uint32_t> bytesReceived{0};
    std::atomic<uint32_t> bytesTotal{0};
    std::atomic<uint32_t> attempt{0};
    std::mutex socketMutex;
    int fd = -1;

    bool Arm(int newFd) {
        std::lock_guard<std::mutex> lock(socketMutex);
        if (cancelled.load()) return false;
        fd = newFd;
        return true;
    }

    void Disarm() {
        std::lock_guard<std::mutex> lock(socketMutex);
        fd = -1;
    }

    void CancelSocket() {
        cancelled.store(true);
        std::lock_guard<std::mutex> lock(socketMutex);
        if (fd >= 0) ::shutdown(fd, SHUT_RDWR);
    }
};

struct Slot {
    bool occupied = false;
    std::string name;
    std::deque<uint64_t> waiting;
    uint64_t running = 0;
};

lua_State* g_L = nullptr;
bool g_delivering = false;

struct State {
    std::mutex mutex;
    std::condition_variable wake;
    std::unordered_map<uint64_t, std::shared_ptr<Call>> calls;
    std::deque<uint64_t> order;
    std::vector<Done> completions;
    std::array<Slot, kMaxQueues> slots{};
    std::vector<std::thread> workers;
    uint64_t nextId = 1;
    uint64_t epoch = 1;
    int cursor = 0;
    bool stopping = false;

    ~State() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            stopping = true;
            ++epoch;
            for (auto& entry : calls) {
                entry.second->cancelled.store(true);
                entry.second->CancelSocket();
            }
            calls.clear();
            order.clear();
            completions.clear();
            for (Slot& slot : slots) slot.waiting.clear();
        }
        wake.notify_all();
        for (std::thread& worker : workers) {
            if (worker.joinable()) worker.join();
        }
    }
};

State& GetState() {
    static State state;
    return state;
}

void Unref(Call& call) {
    if (!g_L) return;
    if (call.callbackRef != LUA_NOREF) {
        luaL_unref(g_L, LUA_REGISTRYINDEX, call.callbackRef);
        call.callbackRef = LUA_NOREF;
    }
    if (call.argsRef != LUA_NOREF) {
        luaL_unref(g_L, LUA_REGISTRYINDEX, call.argsRef);
        call.argsRef = LUA_NOREF;
    }
}

// Desktop CRemoteCalls::GetDownloadModeForQueueName (CRemoteCalls.cpp:70-89):
// the lowest unused index, and index %= MAX_CALL_REMOTE_QUEUES. There is no
// CEF domain list on the phone (CRemoteCall::MakeCall, CRemoteCalls.cpp:149-164),
// so every host is allowed and the restricted/any-host manager pair is one slot.
int GetDownloadModeForQueueName(State& state, const std::string& name) {
    for (int i = 0; i < kMaxQueues; ++i) {
        if (state.slots[i].occupied && state.slots[i].name == name) return i;
    }
    for (int i = 0; i < kMaxQueues; ++i) {
        if (!state.slots[i].occupied) {
            state.slots[i].occupied = true;
            state.slots[i].name = name;
            return i;
        }
    }
    return static_cast<int>(std::hash<std::string>{}(name) % static_cast<size_t>(kMaxQueues));
}

void FreeSlotIfIdle(State& state, const Call& call) {
    if (call.slot < 0 || call.slot >= kMaxQueues) return;
    Slot& slot = state.slots[call.slot];
    if (slot.running == call.id) slot.running = 0;
    if (slot.running == 0 && slot.waiting.empty() && slot.occupied && slot.name == call.options.queue &&
        call.options.queue != kDefaultQueue) {
        slot = Slot{};
    }
}

void RemoveWaiting(State& state, const Call& call) {
    if (call.slot < 0 || call.slot >= kMaxQueues) return;
    Slot& slot = state.slots[call.slot];
    std::deque<uint64_t>& waiting = slot.waiting;
    waiting.erase(std::remove(waiting.begin(), waiting.end(), call.id), waiting.end());
    FreeSlotIfIdle(state, call);
}

std::string WireMethod(const Options& options) {
    if (!options.method.empty()) return Upper(options.method);
    if (!options.formFields.empty() || !options.postData.empty()) return "POST";
    return "GET";
}

std::string InfoMethod(const Options& options) {
    // CLuaNetworkDefs.cpp:231: an empty method string is reported as POST.
    if (options.method.size() >= 1) return Upper(options.method);
    return "POST";
}

bool ValidMethod(const std::string& method) {
    if (method.empty() || method.size() > 16) return false;
    for (unsigned char c : method) {
        if (c < 'A' || c > 'Z') return false;
    }
    return true;
}

std::string FormBody(const Options& options, uint64_t id, std::string& contentType) {
    const std::string boundary = "----MtaFetch" + std::to_string(id);
    std::string body;
    for (const auto& field : options.formFields) {
        if (HasCrlf(field.first) || field.first.find('"') != std::string::npos) return {};
        body += "--" + boundary + "\r\n";
        body += "Content-Disposition: form-data; name=\"" + field.first + "\"\r\n\r\n";
        body += field.second;
        body += "\r\n";
    }
    body += "--" + boundary + "--\r\n";
    contentType = "multipart/form-data; boundary=" + boundary;
    return body;
}

// bPostBinary false truncates at the first NUL and is sent as text/plain
// (SHttpRequestOptions in CNetHTTPDownloadManagerInterface.h).
std::string PostBody(const Options& options, std::string& contentType) {
    contentType.clear();
    if (!options.formFields.empty()) return {};
    std::string body = options.postData;
    if (!options.postBinary) {
        const size_t nul = body.find('\0');
        if (nul != std::string::npos) body.erase(nul);
        if (!body.empty()) contentType = "text/plain";
    } else if (!body.empty()) {
        contentType = "application/octet-stream";
    }
    return body;
}

struct Fd {
    int fd = -1;
    explicit Fd(int value = -1) : fd(value) {}
    ~Fd() {
        if (fd >= 0) ::close(fd);
    }
    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;
};

int WaitWritable(int fd, int timeoutMs, const std::atomic<bool>& cancel) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (!cancel.load()) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
        if (left.count() <= 0) return kErrTimeout;
        const int slice = static_cast<int>(std::min<std::chrono::milliseconds::rep>(left.count(), 200));
        pollfd pfd{};
        pfd.fd = fd;
        pfd.events = POLLOUT;
        const int rc = ::poll(&pfd, 1, slice);
        if (rc < 0 && errno == EINTR) continue;
        if (rc < 0) return kErrCouldntConnect;
        if (rc == 0) continue;
        int so = 0;
        socklen_t len = sizeof(so);
        if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &so, &len) < 0 || so != 0) return kErrCouldntConnect;
        return 0;
    }
    return kErrAborted;
}

int Dial(const Url& url, int timeoutMs, const std::atomic<bool>& cancel, int& code) {
    code = kErrCouldntResolve;
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* results = nullptr;
    if (::getaddrinfo(url.host.c_str(), url.port.c_str(), &hints, &results) != 0) return -1;
    code = kErrCouldntConnect;
    int ready = -1;
    for (addrinfo* address = results; address && ready < 0; address = address->ai_next) {
        if (cancel.load()) {
            code = kErrAborted;
            break;
        }
        const int sock = ::socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (sock < 0) continue;
        const int flags = ::fcntl(sock, F_GETFL, 0);
        if (flags < 0 || ::fcntl(sock, F_SETFL, flags | O_NONBLOCK) < 0) {
            ::close(sock);
            continue;
        }
        const int connected = ::connect(sock, address->ai_addr, address->ai_addrlen);
        if (connected < 0 && errno != EINPROGRESS && errno != EINTR) {
            ::close(sock);
            continue;
        }
        if (connected < 0) {
            const int wait = WaitWritable(sock, timeoutMs, cancel);
            if (wait != 0) {
                code = wait == kErrAborted ? kErrAborted : (wait == kErrTimeout ? kErrTimeout : kErrCouldntConnect);
                ::close(sock);
                continue;
            }
        }
        ::fcntl(sock, F_SETFL, flags);
        timeval tv{};
        tv.tv_sec = timeoutMs / 1000;
        tv.tv_usec = (timeoutMs % 1000) * 1000;
        ::setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        ::setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        ready = sock;
        code = 0;
    }
    ::freeaddrinfo(results);
    return ready;
}

bool SendAll(int fd, const std::string& data, const std::atomic<bool>& cancel, int& code) {
    size_t offset = 0;
    while (offset < data.size()) {
        if (cancel.load()) {
            code = kErrAborted;
            return false;
        }
        const ssize_t n = ::send(fd, data.data() + offset, data.size() - offset, MSG_NOSIGNAL);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == ETIMEDOUT)) {
            code = kErrTimeout;
            return false;
        }
        if (n <= 0) {
            code = kErrSendError;
            return false;
        }
        offset += static_cast<size_t>(n);
    }
    return true;
}

class Stream {
public:
    Stream(int fd, const std::atomic<bool>& cancel) : fd_(fd), cancel_(cancel) {}

    bool ReadLine(std::string& line, int& code) {
        line.clear();
        while (true) {
            const size_t newline = buffer_.find('\n', pos_);
            if (newline != std::string::npos) {
                line.append(buffer_, pos_, newline - pos_);
                pos_ = newline + 1;
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (countHeaders_) {
                    headerBytes_ += line.size() + 2;
                    if (headerBytes_ > kMaxHeaders) {
                        code = kErrFileSize;
                        return false;
                    }
                }
                return true;
            }
            line.append(buffer_, pos_, std::string::npos);
            pos_ = buffer_.size();
            if (countHeaders_ && line.size() > kMaxHeaders) {
                code = kErrFileSize;
                return false;
            }
            if (!ReadMore(code)) return false;
        }
    }

    // Copies up to `max` bytes. Returns 0 on EOF, -1 on error.
    ssize_t ReadSome(char* out, size_t max, int& code) {
        if (pos_ >= buffer_.size()) {
            buffer_.clear();
            pos_ = 0;
            if (!ReadMore(code)) return code == 0 ? 0 : -1;
        }
        if (pos_ >= buffer_.size()) return 0;
        const size_t take = std::min(max, buffer_.size() - pos_);
        std::memcpy(out, buffer_.data() + pos_, take);
        pos_ += take;
        return static_cast<ssize_t>(take);
    }

    void FinishHeaders() { countHeaders_ = false; }

private:
    bool ReadMore(int& code) {
        if (cancel_.load()) {
            code = kErrAborted;
            return false;
        }
        char chunk[8192];
        ssize_t n = 0;
        do {
            n = ::recv(fd_, chunk, sizeof(chunk), 0);
        } while (n < 0 && errno == EINTR);
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == ETIMEDOUT)) {
            code = kErrTimeout;
            return false;
        }
        if (n < 0) {
            code = kErrRecvError;
            return false;
        }
        if (n == 0) {
            code = 0;
            return false;
        }
        if (pos_ > 0 && pos_ == buffer_.size()) {
            buffer_.clear();
            pos_ = 0;
        }
        buffer_.append(chunk, static_cast<size_t>(n));
        return true;
    }

    int fd_ = -1;
    const std::atomic<bool>& cancel_;
    std::string buffer_;
    size_t pos_ = 0;
    size_t headerBytes_ = 0;
    bool countHeaders_ = true;
};

bool ReadBody(Stream& stream, Call& call, unsigned long long count, bool knownLength, std::string& body, int& code) {
    body.clear();
    while (!knownLength || count > 0) {
        if (call.cancelled.load()) {
            code = kErrAborted;
            return false;
        }
        char chunk[8192];
        const size_t want = knownLength ? static_cast<size_t>(std::min<unsigned long long>(count, sizeof(chunk))) : sizeof(chunk);
        const ssize_t got = stream.ReadSome(chunk, want, code);
        if (got < 0) return false;
        if (got == 0) {
            if (knownLength && count > 0) {
                code = kErrRecvError;
                return false;
            }
            return true;
        }
        if (body.size() + static_cast<size_t>(got) > kMaxBody) {
            code = kErrFileSize;
            body.clear();
            return false;
        }
        body.append(chunk, static_cast<size_t>(got));
        call.bytesReceived.store(static_cast<uint32_t>(body.size()));
        if (knownLength) count -= static_cast<unsigned long long>(got);
    }
    return true;
}

bool ReadChunked(Stream& stream, Call& call, std::string& body, int& code) {
    body.clear();
    while (true) {
        std::string line;
        if (!stream.ReadLine(line, code)) {
            if (code == 0) code = kErrWeirdReply;
            return false;
        }
        if (line.empty() || !std::isxdigit(static_cast<unsigned char>(line[0]))) {
            code = kErrWeirdReply;
            return false;
        }
        const unsigned long long size = std::strtoull(line.c_str(), nullptr, 16);
        if (size == 0) {
            do {
                if (!stream.ReadLine(line, code)) return code == 0;
            } while (!line.empty());
            return true;
        }
        if (body.size() + size > kMaxBody) {
            code = kErrFileSize;
            body.clear();
            return false;
        }
        std::string chunk;
        if (!ReadBody(stream, call, size, true, chunk, code)) {
            body.clear();
            return false;
        }
        if (body.size() + chunk.size() > kMaxBody) {
            code = kErrFileSize;
            body.clear();
            return false;
        }
        body += chunk;
        call.bytesReceived.store(static_cast<uint32_t>(body.size()));
        if (!stream.ReadLine(line, code) || !line.empty()) {
            if (code == 0) code = kErrWeirdReply;
            body.clear();
            return false;
        }
    }
}

struct Hop {
    bool transport = true;
    bool retry = false;
    int code = 0;
    int status = 0;
    std::string body;
    std::string headers;
    std::string location;
};

std::string BuildRequest(const std::string& method, const Url& url, const std::string& body, const Options& options,
                         bool allowAuth, const std::string& contentType, int& code) {
    bool hasHost = false;
    bool hasType = false;
    bool hasAuth = false;
    std::string extra;
    for (const auto& header : options.headers) {
        if (HasCrlf(header.first) || HasCrlf(header.second) || header.first.empty()) {
            code = kErrUrlMalformat;
            return {};
        }
        const std::string lower = Lower(header.first);
        if (lower == "content-length" || lower == "connection" || lower == "transfer-encoding") continue;
        if (lower == "host") hasHost = true;
        if (lower == "content-type") hasType = true;
        if (lower == "authorization") hasAuth = true;
        extra += header.first + ": " + header.second + "\r\n";
    }
    const bool needsLength = !body.empty() || method == "POST" || method == "PUT" || method == "PATCH";
    std::string out = method + " " + url.path + " HTTP/1.1\r\n";
    if (!hasHost) out += "Host: " + HostHeader(url) + "\r\n";
    out += "User-Agent: MTA:SA Client\r\nAccept: */*\r\nConnection: close\r\n";
    if (needsLength) out += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    if (!body.empty() && !hasType && !contentType.empty()) out += "Content-Type: " + contentType + "\r\n";
    if (allowAuth && !hasAuth && (!options.username.empty() || !options.password.empty())) {
        out += "Authorization: Basic " + Base64(options.username + ":" + options.password) + "\r\n";
    }
    out += extra;
    out += "\r\n";
    out += body;
    return out;
}

Hop OneHop(Call& call, const Url& url, const std::string& method, const std::string& body, bool allowAuth) {
    Hop hop;
    const int timeoutMs = EffectiveTimeout(call.options.timeoutMs);
    int code = 0;
    Fd sock(Dial(url, timeoutMs, call.cancelled, code));
    if (sock.fd < 0) {
        hop.code = code;
        hop.retry = code == kErrCouldntResolve || code == kErrCouldntConnect || code == kErrTimeout;
        return hop;
    }
    if (!call.Arm(sock.fd)) {
        hop.code = kErrAborted;
        hop.transport = true;
        return hop;
    }
    const std::string request = BuildRequest(method, url, body, call.options, allowAuth, call.contentType, code);
    if (request.empty() || !SendAll(sock.fd, request, call.cancelled, code)) {
        call.Disarm();
        hop.code = request.empty() ? code : code;
        hop.retry = hop.code == kErrTimeout || hop.code == kErrSendError;
        return hop;
    }
    Stream stream(sock.fd, call.cancelled);
    std::string statusLine;
    if (!stream.ReadLine(statusLine, code)) {
        call.Disarm();
        hop.code = code == 0 ? kErrWeirdReply : code;
        hop.retry = hop.code == kErrTimeout || hop.code == kErrRecvError;
        return hop;
    }
    if (statusLine.rfind("HTTP/", 0) != 0) {
        call.Disarm();
        hop.code = kErrWeirdReply;
        return hop;
    }
    const size_t space = statusLine.find(' ');
    hop.status = space == std::string::npos ? 0 : std::atoi(statusLine.c_str() + space + 1);
    if (hop.status < 100 || hop.status > 599) {
        call.Disarm();
        hop.code = kErrWeirdReply;
        return hop;
    }
    long long contentLength = -1;
    bool chunked = false;
    std::string headers;
    for (size_t lines = 0;; ++lines) {
        std::string line;
        if (lines > 128 || !stream.ReadLine(line, code)) {
            call.Disarm();
            hop.code = code == 0 ? kErrFileSize : code;
            hop.retry = hop.code == kErrTimeout || hop.code == kErrRecvError;
            return hop;
        }
        if (line.empty()) break;
        headers += line + "\n";
        const size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        const std::string name = Lower(Trim(line.substr(0, colon)));
        std::string value = colon + 1 < line.size() ? Trim(line.substr(colon + 1)) : std::string();
        if (name == "content-length") contentLength = std::atoll(value.c_str());
        else if (name == "transfer-encoding" && Lower(value).find("chunked") != std::string::npos) chunked = true;
        else if (name == "location") hop.location = value;
    }
    hop.headers = headers;
    hop.transport = false;
    hop.code = hop.status;
    stream.FinishHeaders();
    if (contentLength > static_cast<long long>(kMaxBody)) {
        call.Disarm();
        hop.transport = true;
        hop.code = kErrFileSize;
        hop.body.clear();
        return hop;
    }
    if (contentLength >= 0) call.bytesTotal.store(static_cast<uint32_t>(contentLength));
    const bool redirect = hop.status == 301 || hop.status == 302 || hop.status == 303 || hop.status == 307 || hop.status == 308;
    if (!redirect) {
        call.bytesReceived.store(0);
        bool ok = false;
        if (chunked) ok = ReadChunked(stream, call, hop.body, code);
        else if (contentLength >= 0) ok = ReadBody(stream, call, static_cast<unsigned long long>(contentLength), true, hop.body, code);
        else ok = ReadBody(stream, call, 0, false, hop.body, code);
        if (!ok) {
            call.Disarm();
            hop.transport = true;
            hop.code = code == 0 ? kErrRecvError : code;
            hop.retry = hop.code == kErrTimeout || hop.code == kErrRecvError;
            hop.body.clear();
            return hop;
        }
    }
    call.Disarm();
    return hop;
}

std::string Resolve(const Url& base, const std::string& location, int& code) {
    code = kErrUrlMalformat;
    const std::string loc = Trim(location);
    if (loc.empty() || HasCrlf(loc)) return {};
    const std::string lower = Lower(loc.substr(0, std::min<size_t>(loc.size(), 8)));
    if (lower.rfind("https:", 0) == 0) {
        code = kErrUnsupportedProtocol;
        return {};
    }
    if (lower.rfind("http://", 0) == 0) {
        code = 0;
        return loc;
    }
    if (lower.rfind("//", 0) == 0) {
        code = 0;
        return "http:" + loc;
    }
    std::string path;
    if (!loc.empty() && loc[0] == '/') path = loc;
    else {
        std::string dir = base.path;
        const size_t query = dir.find('?');
        if (query != std::string::npos) dir.erase(query);
        const size_t slash = dir.rfind('/');
        dir = slash == std::string::npos ? "/" : dir.substr(0, slash + 1);
        path = dir + loc;
    }
    code = 0;
    return "http://" + (base.user.empty() && base.password.empty() ? std::string() : base.user + ":" + base.password + "@") +
           HostHeader(base) + path;
}

Done RunCall(const std::shared_ptr<Call>& call) {
    Done done;
    done.id = call->id;
    done.epoch = call->epoch;
    done.legacy = call->options.legacy;
    const uint32_t tries = std::min(call->options.attempts, static_cast<uint32_t>(kMaxAttempts));
    Url url;
    int code = 0;
    if (!ParseUrl(call->url, url, code)) {
        done.statusCode = code;
        call->attempt.store(1);
        return done;
    }
    if (!call->options.username.empty() || !call->options.password.empty()) {
        url.user = call->options.username;
        url.password = call->options.password;
    }
    std::string method = call->wireMethod;
    std::string body = call->wireBody;
    bool allowAuth = call->sendAuth;
    if (tries == 0) {
        done.statusCode = kErrCouldntConnect;
        call->attempt.store(1);
        return done;
    }
    for (uint32_t attempt = 1; attempt <= tries; ++attempt) {
        if (call->cancelled.load()) return done;
        call->attempt.store(attempt);
        call->bytesReceived.store(0);
        call->bytesTotal.store(0);
        Url current = url;
        std::string hopMethod = method;
        std::string hopBody = body;
        bool hopAuth = allowAuth;
        int followed = 0;
        Hop hop;
        while (true) {
            hop = OneHop(*call, current, hopMethod, hopBody, hopAuth);
            if (call->cancelled.load()) return done;
            if (hop.transport || hop.status == 0) break;
            const bool redirect = hop.status == 301 || hop.status == 302 || hop.status == 303 || hop.status == 307 || hop.status == 308;
            if (!redirect || hop.location.empty()) break;
            if (followed >= static_cast<int>(call->options.maxRedirects)) {
                hop.transport = true;
                hop.code = kErrTooManyRedirects;
                hop.retry = false;
                hop.body.clear();
                break;
            }
            int resolveCode = 0;
            const std::string next = Resolve(current, hop.location, resolveCode);
            Url nextUrl;
            if (next.empty() || !ParseUrl(next, nextUrl, resolveCode)) {
                hop.transport = true;
                hop.code = resolveCode == 0 ? kErrUrlMalformat : resolveCode;
                hop.retry = false;
                hop.body.clear();
                break;
            }
            if (nextUrl.host != current.host) hopAuth = false;
            if (hopMethod == "POST" && (hop.status == 301 || hop.status == 302 || hop.status == 303)) {
                hopMethod = "GET";
                hopBody.clear();
            }
            current = nextUrl;
            ++followed;
        }
        if (call->cancelled.load()) return done;
        if (!hop.transport) {
            done.gotStatus = true;
            done.statusCode = hop.status;
            // Legacy FAILONERROR: 400-599 keep no body (CNetHTTPDownloadManagerInterface.h).
            if (!(done.legacy && hop.status >= 400)) done.body = std::move(hop.body);
            done.headers = std::move(hop.headers);
            call->bytesReceived.store(static_cast<uint32_t>(std::min<size_t>(done.body.size(), kMaxBody)));
            return done;
        }
        done.statusCode = hop.code;
        if (!hop.retry || attempt == tries) return done;
    }
    return done;
}

void Worker() {
    State& state = GetState();
    while (true) {
        std::shared_ptr<Call> call;
        {
            std::unique_lock<std::mutex> lock(state.mutex);
            state.wake.wait(lock, [&] {
                if (state.stopping) return true;
                for (const Slot& slot : state.slots) {
                    if (!slot.waiting.empty() && slot.running == 0) return true;
                }
                return false;
            });
            if (state.stopping) return;
            int picked = -1;
            for (int n = 0; n < kMaxQueues; ++n) {
                const int index = (state.cursor + n) % kMaxQueues;
                Slot& slot = state.slots[index];
                if (!slot.waiting.empty() && slot.running == 0) {
                    picked = index;
                    state.cursor = (index + 1) % kMaxQueues;
                    break;
                }
            }
            if (picked < 0) continue;
            Slot& slot = state.slots[picked];
            const uint64_t id = slot.waiting.front();
            slot.waiting.pop_front();
            slot.running = id;
            auto found = state.calls.find(id);
            if (found == state.calls.end()) {
                slot.running = 0;
                continue;
            }
            call = found->second;
        }
        Done done;
        if (!call->cancelled.load()) done = RunCall(call);
        {
            std::lock_guard<std::mutex> lock(state.mutex);
            FreeSlotIfIdle(state, *call);
            if (!call->cancelled.load() && done.epoch == state.epoch && done.id != 0) state.completions.push_back(std::move(done));
            state.wake.notify_all();
        }
    }
}

void EnsureWorkers(State& state) {
    if (!state.workers.empty() || state.stopping) return;
    for (int i = 0; i < kWorkerCount; ++i) state.workers.emplace_back(Worker);
}

void PushRequest(lua_State* L, uint64_t id) {
    lua_newtable(L);
    lua_pushinteger(L, static_cast<lua_Integer>(id));
    lua_setfield(L, -2, "id");
    lua_pushboolean(L, 1);
    lua_setfield(L, -2, "remoteRequest");
}

bool ReadRequestId(lua_State* L, int index, uint64_t& id) {
    if (!lua_istable(L, index)) return false;
    lua_getfield(L, index, "remoteRequest");
    const bool mark = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    if (!mark) return false;
    lua_getfield(L, index, "id");
    const bool ok = lua_isinteger(L, -1) && lua_tointeger(L, -1) > 0;
    if (ok) id = static_cast<uint64_t>(lua_tointeger(L, -1));
    lua_pop(L, 1);
    return ok;
}

bool ReadLuaString(lua_State* L, int index, std::string& out) {
    if (!lua_isstring(L, index)) return false;
    size_t length = 0;
    const char* text = lua_tolstring(L, index, &length);
    if (!text) return false;
    out.assign(text, length);
    return true;
}

void ReadStringMap(lua_State* L, int index, std::map<std::string, std::string>& out) {
    const int table = lua_absindex(L, index);
    lua_pushnil(L);
    while (lua_next(L, table) != 0) {
        const int keyType = lua_type(L, -2);
        const int valueType = lua_type(L, -1);
        if (keyType == LUA_TSTRING && (valueType == LUA_TSTRING || valueType == LUA_TNUMBER || valueType == LUA_TBOOLEAN)) {
            size_t keyLen = 0;
            const char* key = lua_tolstring(L, -2, &keyLen);
            std::string value;
            if (valueType == LUA_TBOOLEAN) value = lua_toboolean(L, -1) ? "1" : "0";
            else {
                size_t valueLen = 0;
                const char* text = lua_tolstring(L, -1, &valueLen);
                if (text) value.assign(text, valueLen);
            }
            if (key) out[std::string(key, keyLen)] = std::move(value);
        }
        lua_pop(L, 1);
    }
}

// Desktop CStringMap::ReadNumber keeps the default when the field is missing
// or not a number. A negative value is ignored the same way.
void ReadMapNumber(lua_State* L, int index, const char* key, uint32_t& value) {
    lua_getfield(L, index, key);
    const int type = lua_type(L, -1);
    if (type == LUA_TNUMBER || (type == LUA_TSTRING && lua_isnumber(L, -1))) {
        const lua_Number number = lua_tonumber(L, -1);
        if (number >= 0 && number < 4294967296.0) value = static_cast<uint32_t>(number);
    } else if (type == LUA_TBOOLEAN) {
        value = lua_toboolean(L, -1) ? 1u : 0u;
    }
    lua_pop(L, 1);
}

int CaptureArgs(lua_State* L, int first, int last) {
    if (first > last) return LUA_NOREF;
    lua_newtable(L);
    int count = 0;
    for (int i = first; i <= last; ++i) {
        lua_pushvalue(L, i);
        lua_rawseti(L, -2, ++count);
    }
    if (count == 0) {
        lua_pop(L, 1);
        return LUA_NOREF;
    }
    // "n" keeps nil arguments. lua_rawseti(nil) deletes the slot, and rawlen would stop there.
    lua_pushinteger(L, count);
    lua_setfield(L, -2, "n");
    return luaL_ref(L, LUA_REGISTRYINDEX);
}

int CaptureArgTable(lua_State* L, int index) {
    lua_newtable(L);
    const int destination = lua_gettop(L);
    const int source = lua_absindex(L, index);
    int count = 0;
    lua_pushnil(L);
    while (lua_next(L, source) != 0) {
        lua_pushvalue(L, -1);
        lua_rawseti(L, destination, ++count);
        lua_pop(L, 1);
    }
    if (count == 0) {
        lua_pop(L, 1);
        return LUA_NOREF;
    }
    lua_pushinteger(L, count);
    lua_setfield(L, -2, "n");
    return luaL_ref(L, LUA_REGISTRYINDEX);
}

void PrepareWire(Call& call) {
    call.wireMethod = WireMethod(call.options);
    if (!call.options.formFields.empty()) {
        call.wireBody = FormBody(call.options, call.id, call.contentType);
    } else {
        call.wireBody = PostBody(call.options, call.contentType);
    }
    call.sendAuth = !call.options.username.empty() || !call.options.password.empty();
    if (call.options.username.empty() && call.options.password.empty()) {
        Url parsed;
        int code = 0;
        if (ParseUrl(call.url, parsed, code)) {
            call.options.username = parsed.user;
            call.options.password = parsed.password;
            call.sendAuth = !parsed.user.empty() || !parsed.password.empty();
        }
    }
}

void QueueCall(State& state, const std::shared_ptr<Call>& call, bool immediate, int immediateCode) {
    state.calls[call->id] = call;
    state.order.push_back(call->id);
    if (immediate) {
        Done done;
        done.id = call->id;
        done.epoch = call->epoch;
        done.legacy = call->options.legacy;
        done.statusCode = immediateCode;
        call->attempt.store(1);
        state.completions.push_back(std::move(done));
        return;
    }
    const int slot = GetDownloadModeForQueueName(state, call->options.queue);
    call->slot = slot;
    state.slots[slot].waiting.push_back(call->id);
    EnsureWorkers(state);
    state.wake.notify_all();
}

void ForgetCalls(State& state, std::vector<std::shared_ptr<Call>>& unref) {
    ++state.epoch;
    for (auto& entry : state.calls) {
        entry.second->cancelled.store(true);
        entry.second->CancelSocket();
        if (!entry.second->finishing) unref.push_back(entry.second);
    }
    state.calls.clear();
    state.order.clear();
    state.completions.clear();
    for (Slot& slot : state.slots) slot.waiting.clear();
}

void PushExtras(lua_State* L, int argsRef, int& nargs) {
    if (argsRef == LUA_NOREF) return;
    lua_rawgeti(L, LUA_REGISTRYINDEX, argsRef);
    const int table = lua_gettop(L);
    lua_getfield(L, table, "n");
    const int count = static_cast<int>(lua_tointeger(L, -1));
    lua_pop(L, 1);
    if (count <= 0 || !lua_checkstack(L, count + 2)) {
        lua_settop(L, table - 1);
        return;
    }
    for (int i = 1; i <= count; ++i) lua_rawgeti(L, table, i);
    lua_remove(L, table);
    nargs += count;
}

void Invoke(lua_State* L, const Done& done, const std::shared_ptr<Call>& call) {
    const int base = lua_gettop(L);
    lua_rawgeti(L, LUA_REGISTRYINDEX, call->callbackRef);
    if (!lua_isfunction(L, -1)) {
        lua_settop(L, base);
        return;
    }
    int nargs = 2;
    if (done.legacy) {
        // CRemoteCalls.cpp:184-200. Success is the body plus 0. Failure is "ERROR" plus the code.
        // 400-599 are failures for the legacy form and the body is empty.
        if (done.gotStatus && done.statusCode < 400) {
            lua_pushlstring(L, done.body.data(), done.body.size());
            lua_pushinteger(L, 0);
        } else {
            lua_pushliteral(L, "ERROR");
            lua_pushinteger(L, done.statusCode);
        }
    } else {
        // CRemoteCalls.cpp:203-230. statusCode is the HTTP status; 200-299 is success.
        // A transport failure keeps success false and uses the transport code, not 200.
        lua_pushlstring(L, done.body.data(), done.body.size());
        lua_newtable(L);
        const bool success = done.gotStatus && done.statusCode >= 200 && done.statusCode <= 299;
        lua_pushboolean(L, success ? 1 : 0);
        lua_setfield(L, -2, "success");
        lua_pushinteger(L, done.statusCode);
        lua_setfield(L, -2, "statusCode");
        PushResponseHeaders(L, done.headers);
        lua_setfield(L, -2, "headers");
    }
    PushExtras(L, call->argsRef, nargs);
    if (lua_pcall(L, nargs, 0, 0) != LUA_OK) {
        const char* message = lua_tostring(L, -1);
        REMOTE_LOGI("fetchRemote callback failed: %s", message ? message : "");
        lua_pop(L, 1);
    }
    lua_settop(L, base);
}

}  // namespace

void BindState(lua_State* L) {
    if (g_L == L) return;
    if (g_L) {
        State& state = GetState();
        std::vector<std::shared_ptr<Call>> ignored;
        {
            std::lock_guard<std::mutex> lock(state.mutex);
            ForgetCalls(state, ignored);
        }
        // The previous registry belongs to a state that is gone or about to
        // be destroyed. Reset() unrefs while that state is still alive.
    }
    g_L = L;
}

void AbandonAll() {
    State& state = GetState();
    std::vector<std::shared_ptr<Call>> drop;
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        ForgetCalls(state, drop);
    }
    for (const std::shared_ptr<Call>& call : drop) Unref(*call);
}

void DeliverCompletions() {
    if (!g_L || g_delivering) return;
    g_delivering = true;
    State& state = GetState();
    std::vector<Done> batch;
    uint64_t epoch = 0;
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        batch.swap(state.completions);
        epoch = state.epoch;
    }
    for (Done& done : batch) {
        std::shared_ptr<Call> call;
        {
            std::lock_guard<std::mutex> lock(state.mutex);
            if (state.epoch != epoch) break;
            auto found = state.calls.find(done.id);
            if (found == state.calls.end()) continue;
            found->second->finishing = true;
            call = found->second;
        }
        Invoke(g_L, done, call);
        {
            std::lock_guard<std::mutex> lock(state.mutex);
            auto found = state.calls.find(done.id);
            if (found != state.calls.end()) {
                state.order.erase(std::remove(state.order.begin(), state.order.end(), done.id), state.order.end());
                state.calls.erase(found);
            }
        }
        Unref(*call);
    }
    g_delivering = false;
}

int LuaFetchRemote(lua_State* L) {
    // CLuaNetworkDefs.cpp:39-126. Legacy form, then the options-table form.
    const int top = lua_gettop(L);
    int index = 1;
    if (index > top) {
        REMOTE_LOGI("fetchRemote: expected URL");
        lua_pushboolean(L, 0);
        return 1;
    }
    std::string url;
    if (!ReadLuaString(L, index, url)) {
        REMOTE_LOGI("fetchRemote: expected URL");
        lua_pushboolean(L, 0);
        return 1;
    }
    ++index;
    Options options;
    int callbackIndex = 0;
    int argsRef = LUA_NOREF;
    if (index > top || lua_type(L, index) != LUA_TTABLE) {
        options.legacy = true;
        if (index <= top && lua_type(L, index) == LUA_TSTRING) {
            ReadLuaString(L, index, options.queue);
            ++index;
        }
        if (index <= top && lua_type(L, index) == LUA_TNUMBER) {
            const lua_Number attempts = lua_tonumber(L, index);
            if (!(attempts >= 0) || attempts != attempts) {
                REMOTE_LOGI("fetchRemote: expected positive connectionAttempts");
                lua_pushboolean(L, 0);
                return 1;
            }
            options.attempts = static_cast<uint32_t>(std::min(attempts, static_cast<lua_Number>(kMaxAttempts)));
            ++index;
        }
        if (index <= top && lua_type(L, index) == LUA_TNUMBER) {
            const lua_Number timeout = lua_tonumber(L, index);
            if (!(timeout >= 0) || timeout != timeout) {
                REMOTE_LOGI("fetchRemote: expected positive connectTimeout");
                lua_pushboolean(L, 0);
                return 1;
            }
            options.timeoutMs = timeout > 4294967295.0 ? 4294967295u : static_cast<uint32_t>(timeout);
            ++index;
        }
        if (index > top || lua_type(L, index) != LUA_TFUNCTION) {
            REMOTE_LOGI("fetchRemote: expected function");
            lua_pushboolean(L, 0);
            return 1;
        }
        callbackIndex = index++;
        if (index <= top) {
            if (lua_isnoneornil(L, index)) {
                ++index;
            } else if (!ReadLuaString(L, index, options.postData)) {
                REMOTE_LOGI("fetchRemote: expected post data string");
                lua_pushboolean(L, 0);
                return 1;
            } else {
                ++index;
            }
        }
        if (index <= top) {
            if (lua_isnoneornil(L, index)) {
                ++index;
            } else if (lua_type(L, index) != LUA_TBOOLEAN) {
                REMOTE_LOGI("fetchRemote: expected postIsBinary bool");
                lua_pushboolean(L, 0);
                return 1;
            } else {
                options.postBinary = lua_toboolean(L, index) != 0;
                ++index;
            }
        }
        argsRef = CaptureArgs(L, index, top);
    } else {
        const int table = index++;
        lua_getfield(L, table, "queueName");
        if (lua_isstring(L, -1)) ReadLuaString(L, -1, options.queue);
        lua_pop(L, 1);
        lua_getfield(L, table, "method");
        if (lua_isstring(L, -1)) ReadLuaString(L, -1, options.method);
        lua_pop(L, 1);
        lua_getfield(L, table, "postData");
        if (lua_isstring(L, -1)) ReadLuaString(L, -1, options.postData);
        lua_pop(L, 1);
        lua_getfield(L, table, "postIsBinary");
        if (lua_isboolean(L, -1)) options.postBinary = lua_toboolean(L, -1) != 0;
        lua_pop(L, 1);
        lua_getfield(L, table, "username");
        if (lua_isstring(L, -1)) ReadLuaString(L, -1, options.username);
        lua_pop(L, 1);
        lua_getfield(L, table, "password");
        if (lua_isstring(L, -1)) ReadLuaString(L, -1, options.password);
        lua_pop(L, 1);
        ReadMapNumber(L, table, "connectionAttempts", options.attempts);
        ReadMapNumber(L, table, "connectTimeout", options.timeoutMs);
        ReadMapNumber(L, table, "maxRedirects", options.maxRedirects);
        options.attempts = std::min(options.attempts, static_cast<uint32_t>(kMaxAttempts));
        if (options.maxRedirects > 20) options.maxRedirects = 20;
        lua_getfield(L, table, "headers");
        if (lua_istable(L, -1)) ReadStringMap(L, -1, options.headers);
        lua_pop(L, 1);
        lua_getfield(L, table, "formFields");
        if (lua_istable(L, -1)) ReadStringMap(L, -1, options.formFields);
        lua_pop(L, 1);
        if (index > top || lua_type(L, index) != LUA_TFUNCTION) {
            REMOTE_LOGI("fetchRemote: expected function");
            lua_pushboolean(L, 0);
            return 1;
        }
        callbackIndex = index++;
        if (index <= top && lua_type(L, index) == LUA_TTABLE) argsRef = CaptureArgTable(L, index);
    }

    lua_pushvalue(L, callbackIndex);
    const int callbackRef = luaL_ref(L, LUA_REGISTRYINDEX);
    auto call = std::make_shared<Call>();
    call->options = std::move(options);
    call->url = std::move(url);
    call->callbackRef = callbackRef;
    call->argsRef = argsRef;
    call->startMs = NowMs();
    call->wireMethod = WireMethod(call->options);
    if (!ValidMethod(call->wireMethod)) {
        Unref(*call);
        REMOTE_LOGI("fetchRemote: bad method");
        lua_pushboolean(L, 0);
        return 1;
    }

    State& state = GetState();
    bool stopped = false;
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        if (state.stopping) stopped = true;
        else {
            call->id = state.nextId++;
            call->epoch = state.epoch;
        }
    }
    if (stopped) {
        Unref(*call);
        lua_pushboolean(L, 0);
        return 1;
    }
    // id is assigned first so the multipart boundary is unique.
    PrepareWire(*call);

    int immediate = 0;
    bool failNow = false;
    Url parsed;
    int parseCode = 0;
    if (!ParseUrl(call->url, parsed, parseCode)) {
        failNow = true;
        immediate = parseCode;
    }
    if (!failNow && !call->options.formFields.empty() && call->contentType.empty()) {
        // FormBody rejected a field name. The callback reports it; no socket is opened.
        failNow = true;
        immediate = kErrUrlMalformat;
    }
    size_t headerBytes = 0;
    for (const auto& header : call->options.headers) {
        headerBytes += header.first.size() + header.second.size();
        if (!failNow && (HasCrlf(header.first) || HasCrlf(header.second))) {
            failNow = true;
            immediate = kErrUrlMalformat;
        }
    }
    if (!failNow && (call->wireBody.size() > kMaxBody || headerBytes > kMaxHeaders)) {
        failNow = true;
        immediate = kErrFileSize;
    }
    stopped = false;
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        if (state.stopping) stopped = true;
        else QueueCall(state, call, failNow, immediate);
    }
    if (stopped) {
        Unref(*call);
        lua_pushboolean(L, 0);
        return 1;
    }
    PushRequest(L, call->id);
    return 1;
}

int LuaGetRemoteRequests(lua_State* L) {
    // CLuaNetworkDefs.cpp:129-154. The phone has one VM and no resource element,
    // so a resource filter would match nothing. Every live call is returned.
    (void)L;
    State& state = GetState();
    std::vector<uint64_t> ids;
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        ids.assign(state.order.begin(), state.order.end());
    }
    lua_newtable(L);
    int index = 0;
    for (uint64_t id : ids) {
        lua_pushinteger(L, ++index);
        PushRequest(L, id);
        lua_settable(L, -3);
    }
    return 1;
}

int LuaGetRemoteRequestInfo(lua_State* L) {
    // CLuaNetworkDefs.cpp:157-257. `resource` is false: there is no resource element.
    // Extended postData / headers are included because this single VM owns the call,
    // which is the desktop case pResource == pThisResource.
    uint64_t id = 0;
    if (!ReadRequestId(L, 1, id)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    int postLength = 0;
    if (lua_gettop(L) >= 2 && !lua_isnoneornil(L, 2)) {
        if (!lua_isnumber(L, 2)) {
            lua_pushboolean(L, 0);
            return 1;
        }
        postLength = static_cast<int>(lua_tointeger(L, 2));
    }
    bool includeHeaders = false;
    if (lua_gettop(L) >= 3 && !lua_isnoneornil(L, 3)) {
        if (!lua_isboolean(L, 3)) {
            lua_pushboolean(L, 0);
            return 1;
        }
        includeHeaders = lua_toboolean(L, 3) != 0;
    }
    std::shared_ptr<Call> call;
    {
        std::lock_guard<std::mutex> lock(GetState().mutex);
        auto found = GetState().calls.find(id);
        if (found == GetState().calls.end()) {
            lua_pushboolean(L, 0);
            return 1;
        }
        call = found->second;
    }
    const Options& options = call->options;
    lua_newtable(L);
    lua_pushstring(L, "fetch");
    lua_setfield(L, -2, "type");
    lua_pushlstring(L, call->url.data(), call->url.size());
    lua_setfield(L, -2, "url");
    lua_pushlstring(L, options.queue.data(), options.queue.size());
    lua_setfield(L, -2, "queue");
    lua_pushboolean(L, 0);
    lua_setfield(L, -2, "resource");
    lua_pushinteger(L, static_cast<lua_Integer>(call->startMs));
    lua_setfield(L, -2, "start");
    if (postLength == -1 || postLength > 0) {
        std::string post = options.postData;
        if (postLength > 0 && static_cast<size_t>(postLength) < post.size()) post.resize(static_cast<size_t>(postLength));
        lua_pushlstring(L, post.data(), post.size());
        lua_setfield(L, -2, "postData");
    }
    if (includeHeaders) {
        lua_newtable(L);
        for (const auto& header : options.headers) {
            lua_pushlstring(L, header.second.data(), header.second.size());
            lua_setfield(L, -2, header.first.c_str());
        }
        lua_setfield(L, -2, "headers");
    }
    const std::string method = InfoMethod(options);
    lua_pushlstring(L, method.data(), method.size());
    lua_setfield(L, -2, "method");
    lua_pushinteger(L, options.attempts);
    lua_setfield(L, -2, "connectionAttempts");
    lua_pushinteger(L, options.timeoutMs);
    lua_setfield(L, -2, "connectionTimeout");
    lua_pushinteger(L, call->bytesReceived.load());
    lua_setfield(L, -2, "bytesReceived");
    lua_pushinteger(L, call->bytesTotal.load());
    lua_setfield(L, -2, "bytesTotal");
    lua_pushinteger(L, call->attempt.load());
    lua_setfield(L, -2, "currentAttempt");
    return 1;
}

int LuaAbortRemoteRequest(lua_State* L) {
    // CLuaNetworkDefs.cpp:260-279 and CRemoteCall::CancelDownload (CRemoteCalls.cpp:244-251).
    uint64_t id = 0;
    if (!ReadRequestId(L, 1, id)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    State& state = GetState();
    std::shared_ptr<Call> drop;
    bool active = false;
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        auto found = state.calls.find(id);
        if (found == state.calls.end()) {
            lua_pushboolean(L, 0);
            return 1;
        }
        drop = found->second;
        active = !drop->finishing;
        drop->CancelSocket();
        RemoveWaiting(state, *drop);
        state.order.erase(std::remove(state.order.begin(), state.order.end(), id), state.order.end());
        state.calls.erase(found);
    }
    if (!drop->finishing) Unref(*drop);
    lua_pushboolean(L, active ? 1 : 0);
    return 1;
}

int LuaGetServerIp(lua_State* L) {
    // CLuaNetworkDefs.cpp:18-21. includePort defaults to false.
    bool includePort = false;
    if (lua_gettop(L) >= 1 && !lua_isnoneornil(L, 1)) {
        if (!lua_isboolean(L, 1)) {
            REMOTE_LOGI("getServerIp: expected boolean");
            lua_pushboolean(L, 0);
            return 1;
        }
        includePort = lua_toboolean(L, 1) != 0;
    }
    const std::string address = QueryConnectedServer(includePort);
    lua_pushlstring(L, address.data(), address.size());
    return 1;
}

}  // namespace RemoteCalls
}  // namespace WorldRPG::Net
