#include "HttpDownloader.h"

#include "Checksums.h"
#include "NeonJoinProtocol.h"

#include <algorithm>
#include <cerrno>
#include <condition_variable>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <string_view>
#include <thread>
#include <unordered_set>

#include <netdb.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#ifdef __ANDROID__
#include <android/log.h>
#define HTTP_LOGI(...) __android_log_print(ANDROID_LOG_INFO, "WorldRPGNeonHttp", __VA_ARGS__)
#else
#define HTTP_LOGI(...) do { std::printf(__VA_ARGS__); std::printf("\n"); } while (0)
#endif

namespace WorldRPG::Net::HttpDownloader {

namespace {

constexpr int kWorkerCount = 4;          // desktop clamps per-client HTTP connections to 4
constexpr int kSocketTimeoutSec = 20;
constexpr size_t kMaxFailureNotes = 20;
constexpr size_t kMaxSamples = 4;
constexpr size_t kMaxHeaderLine = 8192;
constexpr size_t kMaxHeaderLines = 128;

struct HttpTarget {
    std::string host;
    std::string port;
    std::string pathPrefix;  // from the external URL form, "" for the port form
};

struct Request {
    std::string cacheRoot;
    std::string serverBase;
    std::string resource;
    std::string file;
    uint64_t size = 0;
    uint32_t crc = 0;
    bool hasMd5 = false;
    uint8_t md5[16] = {};
};

struct Outcome {
    enum class Kind { Cached, Downloaded, Mismatch, Failed } kind = Kind::Failed;
    uint64_t bytes = 0;
    std::string note;
    Sample sample;
};

struct State {
    std::mutex mutex;
    std::condition_variable wake;  // work queued or stop requested
    std::condition_variable idle;  // queue drained and no transfer running
    std::deque<Request> queue;
    std::unordered_set<std::string> inFlight;
    std::vector<std::thread> workers;
    size_t active = 0;
    bool stopping = false;
    bool rootWarned = false;
    bool baseWarned = false;
    std::string cacheRoot;
    std::string serverBase;
    Stats stats;

    ~State() { StopAndJoin(); }

    void StopAndJoin() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            stopping = true;
            stats.dropped += queue.size();
            queue.clear();
        }
        wake.notify_all();
        for (std::thread& worker : workers) {
            if (worker.joinable()) worker.join();
        }
        workers.clear();
    }
};

State& GetState() {
    static State state;
    return state;
}

std::string TrimTrailingSlashes(std::string value) {
    while (!value.empty() && value.back() == '/') value.pop_back();
    return value;
}

// Creates every directory component of `path` (POSIX mkdir -p).
bool EnsureDirectories(const std::string& path) {
    size_t pos = 0;
    while (pos <= path.size()) {
        size_t next = path.find('/', pos);
        if (next == std::string::npos) next = path.size();
        const std::string current = path.substr(0, next);
        if (!current.empty() && ::mkdir(current.c_str(), 0775) != 0 && errno != EEXIST) return false;
        pos = next + 1;
    }
    return true;
}

bool EnsureParentDirectory(const std::string& filePath) {
    const size_t slash = filePath.rfind('/');
    if (slash == std::string::npos) return true;
    return EnsureDirectories(filePath.substr(0, slash));
}

bool HashExistingFile(const std::string& path, FileHasher& hasher) {
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (!file) return false;
    std::vector<uint8_t> buffer(64 * 1024);
    size_t got = 0;
    while ((got = std::fread(buffer.data(), 1, buffer.size(), file)) > 0) hasher.Update(buffer.data(), got);
    const bool ok = std::ferror(file) == 0;
    std::fclose(file);
    return ok;
}

// Percent-encodes a URL path. '/' is kept so file names with folders survive.
std::string EncodePath(const std::string& value) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(value.size());
    for (unsigned char c : value) {
        const bool unreserved = std::isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~' || c == '/';
        if (unreserved) {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(kHex[c >> 4]);
            out.push_back(kHex[c & 0x0F]);
        }
    }
    return out;
}

// Accepts "http://host[:port][/prefix]". https is not supported (port 22005 is plain HTTP).
bool ParseBaseUrl(const std::string& base, HttpTarget& target, std::string& error) {
    constexpr std::string_view kScheme = "http://";
    if (base.rfind(kScheme, 0) != 0) {
        error = base.rfind("https://", 0) == 0 ? "https not supported" : "base URL is not http://";
        return false;
    }
    const std::string rest = base.substr(kScheme.size());
    const size_t slash = rest.find('/');
    const std::string hostPort = rest.substr(0, slash);
    target.pathPrefix = slash == std::string::npos ? std::string() : TrimTrailingSlashes(rest.substr(slash));
    const size_t colon = hostPort.rfind(':');
    if (colon == std::string::npos) {
        target.host = hostPort;
        target.port = "80";
    } else {
        target.host = hostPort.substr(0, colon);
        target.port = hostPort.substr(colon + 1);
    }
    if (target.host.empty() || target.port.empty()) {
        error = "base URL has no host";
        return false;
    }
    return true;
}

// Buffered reader over one TCP socket. Owns the descriptor.
class HttpConnection {
public:
    explicit HttpConnection(int fd) : fd_(fd) {}
    ~HttpConnection() {
        if (fd_ >= 0) ::close(fd_);
    }
    HttpConnection(const HttpConnection&) = delete;
    HttpConnection& operator=(const HttpConnection&) = delete;

    bool Send(const std::string& data, std::string& error) {
        size_t offset = 0;
        while (offset < data.size()) {
            const ssize_t n = ::send(fd_, data.data() + offset, data.size() - offset, MSG_NOSIGNAL);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) {
                error = std::string("send: ") + std::strerror(errno);
                return false;
            }
            offset += static_cast<size_t>(n);
        }
        return true;
    }

    // Returns bytes copied (>0), 0 at end of stream, -1 on error (error set).
    ssize_t ReadSome(uint8_t* out, size_t max, std::string& error) {
        if (pos_ >= buffer_.size()) {
            buffer_.clear();
            pos_ = 0;
            char chunk[16384];
            ssize_t n = 0;
            do {
                n = ::recv(fd_, chunk, sizeof(chunk), 0);
            } while (n < 0 && errno == EINTR);
            if (n < 0) {
                error = std::string("recv: ") + std::strerror(errno);
                return -1;
            }
            if (n == 0) return 0;
            buffer_.assign(chunk, static_cast<size_t>(n));
        }
        const size_t take = std::min(max, buffer_.size() - pos_);
        std::memcpy(out, buffer_.data() + pos_, take);
        pos_ += take;
        return static_cast<ssize_t>(take);
    }

    // Reads one CRLF (or LF) terminated line without the terminator.
    bool ReadLine(std::string& line, std::string& error) {
        line.clear();
        while (true) {
            const size_t newline = buffer_.find('\n', pos_);
            if (newline != std::string::npos) {
                line.append(buffer_, pos_, newline - pos_);
                pos_ = newline + 1;
                if (!line.empty() && line.back() == '\r') line.pop_back();
                return true;
            }
            line.append(buffer_, pos_, std::string::npos);
            if (line.size() > kMaxHeaderLine) {
                error = "header line too long";
                return false;
            }
            buffer_.clear();
            pos_ = 0;
            char chunk[4096];
            ssize_t n = 0;
            do {
                n = ::recv(fd_, chunk, sizeof(chunk), 0);
            } while (n < 0 && errno == EINTR);
            if (n < 0) {
                error = std::string("recv: ") + std::strerror(errno);
                return false;
            }
            if (n == 0) {
                error = "connection closed inside headers";
                return false;
            }
            buffer_.assign(chunk, static_cast<size_t>(n));
        }
    }

private:
    int fd_ = -1;
    std::string buffer_;
    size_t pos_ = 0;
};

bool ConnectTo(const HttpTarget& target, int& fd, std::string& error) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* results = nullptr;
    const int rc = ::getaddrinfo(target.host.c_str(), target.port.c_str(), &hints, &results);
    if (rc != 0) {
        error = std::string("resolve: ") + ::gai_strerror(rc);
        return false;
    }
    for (addrinfo* address = results; address && fd < 0; address = address->ai_next) {
        const int sock = ::socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (sock < 0) continue;
        timeval timeout{kSocketTimeoutSec, 0};
        ::setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
        ::setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        if (::connect(sock, address->ai_addr, address->ai_addrlen) == 0) {
            fd = sock;
        } else {
            error = std::string("connect: ") + std::strerror(errno);
            ::close(sock);
        }
    }
    ::freeaddrinfo(results);
    return fd >= 0;
}

// HTTP/1.0 GET. Streams the body into `path` (".part") and hashes it.
// Returns false with `error` set; `status` is the HTTP status when headers arrived.
bool FetchToFile(const HttpTarget& target, const std::string& requestPath, const std::string& path, FileHasher& hasher, int& status,
                 std::string& error) {
    status = 0;
    int fd = -1;
    if (!ConnectTo(target, fd, error)) return false;
    HttpConnection conn(fd);

    const std::string request = "GET " + requestPath + " HTTP/1.0\r\nHost: " + target.host + ":" + target.port +
                                "\r\nUser-Agent: WorldRPG-Neon/1.0\r\nAccept: */*\r\nConnection: close\r\n\r\n";
    if (!conn.Send(request, error)) return false;

    std::string line;
    if (!conn.ReadLine(line, error)) return false;
    if (line.rfind("HTTP/", 0) != 0) {
        error = "bad status line";
        return false;
    }
    const size_t space = line.find(' ');
    if (space == std::string::npos) {
        error = "bad status line";
        return false;
    }
    status = std::atoi(line.c_str() + space + 1);

    long long contentLength = -1;
    bool chunked = false;
    std::string location;
    for (size_t lines = 0;; ++lines) {
        if (lines > kMaxHeaderLines || !conn.ReadLine(line, error)) {
            if (error.empty()) error = "too many header lines";
            return false;
        }
        if (line.empty()) break;
        const size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string name = line.substr(0, colon);
        for (char& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        std::string value = line.substr(colon + 1);
        const size_t first = value.find_first_not_of(" \t");
        value = first == std::string::npos ? std::string() : value.substr(first);
        if (name == "content-length") {
            contentLength = std::atoll(value.c_str());
        } else if (name == "transfer-encoding") {
            std::string lower = value;
            for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            chunked = lower.find("chunked") != std::string::npos;
        } else if (name == "location") {
            location = value;
        }
    }
    if (status != 200) {
        error = "HTTP status" + (location.empty() ? std::string() : " (Location: " + location + ")");
        return false;
    }

    std::FILE* out = std::fopen(path.c_str(), "wb");
    if (!out) {
        error = std::string("open ") + std::strerror(errno);
        return false;
    }
    bool ok = true;
    const auto sink = [&](const uint8_t* data, size_t length) -> bool {
        if (std::fwrite(data, 1, length, out) != length) {
            error = "write failed";
            return false;
        }
        hasher.Update(data, length);
        return true;
    };
    const auto copyBytes = [&](unsigned long long count) -> bool {
        uint8_t buffer[16384];
        while (count > 0) {
            const size_t want = static_cast<size_t>(std::min<unsigned long long>(count, sizeof(buffer)));
            const ssize_t got = conn.ReadSome(buffer, want, error);
            if (got < 0) return false;
            if (got == 0) {
                error = "short body";
                return false;
            }
            if (!sink(buffer, static_cast<size_t>(got))) return false;
            count -= static_cast<unsigned long long>(got);
        }
        return true;
    };

    if (chunked) {
        while (ok) {
            if (!conn.ReadLine(line, error)) {
                ok = false;
                break;
            }
            if (line.empty() || !std::isxdigit(static_cast<unsigned char>(line[0]))) {
                error = "bad chunk size";
                ok = false;
                break;
            }
            const unsigned long long size = std::strtoull(line.c_str(), nullptr, 16);
            if (size == 0) {
                // Trailers end with an empty line.
                do {
                    if (!conn.ReadLine(line, error)) {
                        ok = false;
                        break;
                    }
                } while (!line.empty());
                break;
            }
            if (!copyBytes(size)) {
                ok = false;
                break;
            }
            if (!conn.ReadLine(line, error) || !line.empty()) {
                if (error.empty()) error = "bad chunk terminator";
                ok = false;
                break;
            }
        }
    } else if (contentLength >= 0) {
        ok = copyBytes(static_cast<unsigned long long>(contentLength));
    } else {
        // No length: HTTP/1.0 close-delimited body.
        uint8_t buffer[16384];
        while (ok) {
            const ssize_t got = conn.ReadSome(buffer, sizeof(buffer), error);
            if (got < 0) {
                ok = false;
            } else if (got == 0) {
                break;
            } else {
                ok = sink(buffer, static_cast<size_t>(got));
            }
        }
    }
    if (std::fclose(out) != 0 && ok) {
        error = "close failed";
        ok = false;
    }
    return ok;
}

Outcome ProcessRequest(const Request& req) {
    Outcome out;
    const std::string label = req.resource + "/" + req.file;
    const std::string finalPath = req.cacheRoot + "/" + req.resource + "/" + req.file;
    const std::string partPath = finalPath + ".part";

    // Cache: reuse the local copy when size, CRC-32 and (if sent) MD5 all match.
    struct stat st{};
    if (::stat(finalPath.c_str(), &st) == 0 && static_cast<uint64_t>(st.st_size) == req.size) {
        FileHasher local;
        if (HashExistingFile(finalPath, local)) {
            uint8_t digest[16];
            local.Md5Digest(digest);
            if (local.Crc() == req.crc && (!req.hasMd5 || std::memcmp(digest, req.md5, sizeof(digest)) == 0)) {
                out.kind = Outcome::Kind::Cached;
                return out;
            }
        }
    }

    if (!EnsureParentDirectory(finalPath)) {
        out.note = label + ": cannot create cache directory";
        return out;
    }

    HttpTarget target;
    std::string error;
    if (!ParseBaseUrl(req.serverBase, target, error)) {
        out.note = label + ": " + error;
        return out;
    }
    const std::string requestPath = target.pathPrefix + "/" + EncodePath(req.resource) + "/" + EncodePath(req.file);

    FileHasher hasher;
    int status = 0;
    if (!FetchToFile(target, requestPath, partPath, hasher, status, error)) {
        std::remove(partPath.c_str());
        out.note = label + ": " + error;
        return out;  // Kind::Failed (default)
    }

    uint8_t digest[16];
    hasher.Md5Digest(digest);
    std::string reason;
    if (hasher.Size() != req.size) {
        reason = "size " + std::to_string(hasher.Size()) + " != declared " + std::to_string(req.size);
    } else if (hasher.Crc() != req.crc) {
        reason = "CRC-32 mismatch";
    } else if (req.hasMd5 && std::memcmp(digest, req.md5, sizeof(digest)) != 0) {
        reason = "MD5 mismatch";
    }
    if (!reason.empty()) {
        std::remove(partPath.c_str());
        out.kind = Outcome::Kind::Mismatch;
        out.note = label + ": " + reason;
        return out;
    }
    if (std::rename(partPath.c_str(), finalPath.c_str()) != 0) {
        std::remove(partPath.c_str());
        out.note = label + ": rename failed";
        return out;
    }

    out.kind = Outcome::Kind::Downloaded;
    out.bytes = hasher.Size();
    out.sample.path = finalPath;
    out.sample.size = hasher.Size();
    out.sample.crc = hasher.Crc();
    out.sample.md5Hex = ToHex(digest, sizeof(digest));
    out.sample.packetHadMd5 = req.hasMd5;
    out.sample.packetMd5Hex = req.hasMd5 ? ToHex(req.md5, sizeof(req.md5)) : std::string();
    out.sample.packetCrc = req.crc;
    return out;
}

void ApplyOutcome(Stats& stats, Outcome& outcome, const Request& req) {
    switch (outcome.kind) {
        case Outcome::Kind::Cached:
            ++stats.cached;
            HTTP_LOGI("HTTP: cached %s/%s", req.resource.c_str(), req.file.c_str());
            break;
        case Outcome::Kind::Downloaded:
            ++stats.downloaded;
            stats.bytes += outcome.bytes;
            if (req.hasMd5) ++stats.md5Verified;
            else ++stats.crcVerified;
            if (stats.samples.size() < kMaxSamples) stats.samples.push_back(outcome.sample);
            HTTP_LOGI("HTTP: verified %s/%s %llu bytes crc=%08x md5=%s", req.resource.c_str(), req.file.c_str(),
                      static_cast<unsigned long long>(outcome.bytes), outcome.sample.crc, outcome.sample.md5Hex.c_str());
            break;
        case Outcome::Kind::Mismatch:
            ++stats.mismatches;
            HTTP_LOGI("HTTP: discarded %s", outcome.note.c_str());
            if (stats.failures.size() < kMaxFailureNotes) stats.failures.push_back(outcome.note);
            break;
        case Outcome::Kind::Failed:
            ++stats.failed;
            HTTP_LOGI("HTTP: failed %s", outcome.note.c_str());
            if (stats.failures.size() < kMaxFailureNotes) stats.failures.push_back(outcome.note);
            break;
    }
}

void WorkerLoop() {
    State& state = GetState();
    while (true) {
        Request req;
        {
            std::unique_lock<std::mutex> lock(state.mutex);
            state.wake.wait(lock, [&] { return state.stopping || !state.queue.empty(); });
            if (state.stopping) return;
            req = std::move(state.queue.front());
            state.queue.pop_front();
            ++state.active;
        }

        Outcome outcome = ProcessRequest(req);

        std::lock_guard<std::mutex> lock(state.mutex);
        ApplyOutcome(state.stats, outcome, req);
        state.inFlight.erase(req.resource + "/" + req.file);
        --state.active;
        if (state.queue.empty() && state.active == 0) state.idle.notify_all();
    }
}

} // namespace

void SetResourceCacheRoot(const std::string& root) {
    State& state = GetState();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.cacheRoot = TrimTrailingSlashes(root);
    state.rootWarned = false;
}

void SetServerBase(const std::string& baseUrl) {
    State& state = GetState();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.serverBase = TrimTrailingSlashes(baseUrl);
    state.baseWarned = false;
}

std::string BuildServerBase(const NeonJoinInfo& info, const std::string& host) {
    if (info.httpDownloadType == 1 && info.httpPort != 0 && !host.empty()) {
        return "http://" + host + ":" + std::to_string(info.httpPort);
    }
    if (info.httpDownloadType == 2 && !info.httpUrl.empty()) return TrimTrailingSlashes(info.httpUrl);
    return std::string();
}

bool Enqueue(const std::string& resourceName, const ResourceFile& file) {
    State& state = GetState();
    std::lock_guard<std::mutex> lock(state.mutex);
    if (state.stopping) return false;
    if (state.cacheRoot.empty()) {
        if (!state.rootWarned) {
            HTTP_LOGI("HTTP: cache root not set, client files are not downloaded");
            state.rootWarned = true;
        }
        return false;
    }
    if (state.serverBase.empty()) {
        if (!state.baseWarned) {
            HTTP_LOGI("HTTP: no HTTP download service in the join packet, client files are not downloaded");
            state.baseWarned = true;
        }
        return false;
    }
    const std::string key = resourceName + "/" + file.name;
    if (!state.inFlight.insert(key).second) {
        ++state.stats.skippedDuplicate;
        return false;
    }

    Request req;
    req.cacheRoot = state.cacheRoot;
    req.serverBase = state.serverBase;
    req.resource = resourceName;
    req.file = file.name;
    req.size = file.size;
    req.crc = file.crc;
    req.hasMd5 = file.hasMd5;
    std::memcpy(req.md5, file.md5, sizeof(req.md5));
    state.queue.push_back(std::move(req));
    ++state.stats.queued;

    if (state.workers.empty()) {
        for (int i = 0; i < kWorkerCount; ++i) state.workers.emplace_back(WorkerLoop);
    }
    state.wake.notify_one();
    return true;
}

bool WaitIdle(int timeoutMs) {
    State& state = GetState();
    std::unique_lock<std::mutex> lock(state.mutex);
    return state.idle.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                               [&] { return state.queue.empty() && state.active == 0; });
}

Stats GetStats() {
    State& state = GetState();
    std::lock_guard<std::mutex> lock(state.mutex);
    return state.stats;
}

void Shutdown() {
    GetState().StopAndJoin();
}

} // namespace WorldRPG::Net::HttpDownloader
