// ws2_32 on POSIX sockets.
//
// Host sockets are always O_NONBLOCK so a guest call never stalls the emu
// thread. A socket in Windows blocking mode retries through Block(). DNS
// lookups run on a worker thread for the same reason.
//
// Windows and Linux share the IPv4 sockaddr layout. AF_INET6, SOL_SOCKET,
// the SO_* options, the error codes and the addrinfo field order differ and
// are translated here.

#include "NetEmuLog.h"
#include "Win32Shims.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <mutex>
#include <thread>

namespace NetEmu {

// Per-machine Winsock state (Win32State::winsock): the sockets this netc
// opened, DNS lookups in flight, and the per-process static result buffers.
// Destroying it closes every socket, so an unloaded session leaks nothing.
struct SockInfo {
    bool blocking = true;
    uint32_t recvTimeoutMs = 0;
    uint32_t sendTimeoutMs = 0;
    int type = SOCK_DGRAM;
    bool connecting = false;
};

// Async DNS: one lookup in flight per guest thread.
struct Lookup {
    std::mutex mutex;
    bool done = false;
    int status = 0;
    addrinfo* result = nullptr;
    ~Lookup() {
        if (result) ::freeaddrinfo(result);
    }
};

struct WinsockState {
    std::map<int, SockInfo> sockets;
    std::map<uint32_t, std::shared_ptr<Lookup>> lookups;  // by guest thread id
    uint32_t hostent = 0;     // gethostbyname's static hostent
    uint32_t ntoaBuffer = 0;  // inet_ntoa's static string
    ~WinsockState() {
        for (const auto& [fd, info] : sockets) ::close(fd);
    }
};

namespace {
constexpr char kWs[] = "ws2_32.dll";
constexpr uint32_t kSocketError = 0xFFFFFFFFu;
constexpr uint16_t kWinAfInet6 = 23;

constexpr uint32_t WSAEINTR = 10004, WSAEFAULT = 10014, WSAEINVAL = 10022, WSAEWOULDBLOCK = 10035,
                   WSAEALREADY = 10037, WSAENOTSOCK = 10038, WSAEMSGSIZE = 10040, WSAENOPROTOOPT = 10042,
                   WSAEOPNOTSUPP = 10045, WSAEAFNOSUPPORT = 10047, WSAEADDRINUSE = 10048, WSAEADDRNOTAVAIL = 10049,
                   WSAENETDOWN = 10050, WSAENETUNREACH = 10051, WSAECONNABORTED = 10053, WSAECONNRESET = 10054,
                   WSAENOBUFS = 10055, WSAEISCONN = 10056, WSAENOTCONN = 10057, WSAETIMEDOUT = 10060,
                   WSAECONNREFUSED = 10061, WSAEHOSTUNREACH = 10065, WSAHOST_NOT_FOUND = 11001, WSATRY_AGAIN = 11002,
                   WSANO_DATA = 11004;



uint32_t WsaError(int e) {
    switch (e) {
    case EINTR: return WSAEINTR;
    case EFAULT: return WSAEFAULT;
    case EINVAL: return WSAEINVAL;
    case EAGAIN: return WSAEWOULDBLOCK;
    case EINPROGRESS: return WSAEWOULDBLOCK;
    case EALREADY: return WSAEALREADY;
    case ENOTSOCK:
    case EBADF: return WSAENOTSOCK;
    case EMSGSIZE: return WSAEMSGSIZE;
    case ENOPROTOOPT: return WSAENOPROTOOPT;
    case EOPNOTSUPP: return WSAEOPNOTSUPP;
    case EAFNOSUPPORT: return WSAEAFNOSUPPORT;
    case EADDRINUSE: return WSAEADDRINUSE;
    case EADDRNOTAVAIL: return WSAEADDRNOTAVAIL;
    case ENETDOWN: return WSAENETDOWN;
    case ENETUNREACH: return WSAENETUNREACH;
    case ECONNABORTED: return WSAECONNABORTED;
    case ECONNRESET: return WSAECONNRESET;
    case ENOBUFS: return WSAENOBUFS;
    case EISCONN: return WSAEISCONN;
    case ENOTCONN: return WSAENOTCONN;
    case ETIMEDOUT: return WSAETIMEDOUT;
    case ECONNREFUSED: return WSAECONNREFUSED;
    case EHOSTUNREACH: return WSAEHOSTUNREACH;
    default: return WSAEINVAL;
    }
}

int HostFamily(uint32_t win) { return win == kWinAfInet6 ? AF_INET6 : static_cast<int>(win); }
uint16_t WinFamily(int host) { return host == AF_INET6 ? kWinAfInet6 : static_cast<uint16_t>(host); }

// Guest sockaddr -> host sockaddr_storage.
bool ReadAddr(GuestMemory& mem, uint32_t va, uint32_t len, sockaddr_storage* out, socklen_t* outLen) {
    if (!va || len < 2 || len > sizeof(sockaddr_storage)) return false;
    std::memset(out, 0, sizeof(*out));
    mem.ReadBytes(va, out, len);
    uint16_t fam = 0;
    std::memcpy(&fam, out, 2);
    out->ss_family = static_cast<sa_family_t>(HostFamily(fam));
    *outLen = out->ss_family == AF_INET6 ? sizeof(sockaddr_in6) : sizeof(sockaddr_in);
    return true;
}

// Host sockaddr -> guest buffer with an int* length in/out.
void WriteAddr(GuestMemory& mem, uint32_t va, uint32_t lenPtr, const sockaddr_storage& addr, socklen_t len) {
    if (!va || !lenPtr) return;
    sockaddr_storage tmp = addr;
    const uint16_t fam = WinFamily(addr.ss_family);
    std::memcpy(&tmp, &fam, 2);
    const uint32_t cap = mem.Read32(lenPtr);
    mem.WriteBytes(va, &tmp, std::min<uint32_t>(cap, static_cast<uint32_t>(len)));
    mem.Write32(lenPtr, static_cast<uint32_t>(len));
}

bool MapSockOpt(uint32_t level, uint32_t name, int* hostLevel, int* hostName) {
    if (level == 0xFFFF) {
        *hostLevel = SOL_SOCKET;
        switch (name) {
        case 0x0004: *hostName = SO_REUSEADDR; return true;
        case 0x0008: *hostName = SO_KEEPALIVE; return true;
        case 0x0020: *hostName = SO_BROADCAST; return true;
        case 0x0080: *hostName = SO_LINGER; return true;
        case 0x1001: *hostName = SO_SNDBUF; return true;
        case 0x1002: *hostName = SO_RCVBUF; return true;
        case 0x1005: *hostName = SO_SNDTIMEO; return true;
        case 0x1006: *hostName = SO_RCVTIMEO; return true;
        case 0x1007: *hostName = SO_ERROR; return true;
        case 0x1008: *hostName = SO_TYPE; return true;
        default: return false;
        }
    }
    if (level == 0) {  // IPPROTO_IP
        *hostLevel = IPPROTO_IP;
        switch (name) {
        case 3: *hostName = IP_TOS; return true;
        case 4: *hostName = IP_TTL; return true;
        default: return false;
        }
    }
    if (level == 6) {  // IPPROTO_TCP
        *hostLevel = IPPROTO_TCP;
        if (name == 1) {
            *hostName = TCP_NODELAY;
            return true;
        }
        return false;
    }
    if (level == 41) {  // IPPROTO_IPV6
        *hostLevel = IPPROTO_IPV6;
        if (name == 27) {
            *hostName = IPV6_V6ONLY;
            return true;
        }
        return false;
    }
    return false;
}

short PollEvents(uint32_t mask, const SockInfo* info) {
    short ev = 0;
    if (mask & (0x01 | 0x08 | 0x20)) ev |= POLLIN;              // FD_READ | FD_ACCEPT | FD_CLOSE
    if (mask & 0x10) ev |= POLLOUT;                             // FD_CONNECT
    if ((mask & 0x02) && info && info->type == SOCK_STREAM) ev |= POLLOUT;  // FD_WRITE on TCP only
    return ev;
}



std::shared_ptr<Lookup> StartLookup(const std::string& node, const std::string& service, const addrinfo& hints) {
    auto l = std::make_shared<Lookup>();
    std::thread([l, node, service, hints] {
        addrinfo* res = nullptr;
        const int rc = ::getaddrinfo(node.empty() ? nullptr : node.c_str(), service.empty() ? nullptr : service.c_str(), &hints, &res);
        std::lock_guard lk(l->mutex);
        l->status = rc;
        l->result = res;
        l->done = true;
    }).detach();
    return l;
}

uint32_t GaiError(int rc) {
    switch (rc) {
    case 0: return 0;
    case EAI_AGAIN: return WSATRY_AGAIN;
    case EAI_NONAME: return WSAHOST_NOT_FOUND;
#ifdef EAI_NODATA
    case EAI_NODATA: return WSANO_DATA;
#endif
    case EAI_FAMILY: return WSAEAFNOSUPPORT;
    case EAI_MEMORY: return WSAENOBUFS;
    default: return WSAHOST_NOT_FOUND;
    }
}

// Names Windows answers without asking DNS: the computer's own name resolves
// to its adapter address, "localhost" to loopback.
std::string LocalName(const Win32State& st, const std::string& node) {
    std::string n = node;
    for (char& ch : n) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    std::string host = st.env.computerName;
    for (char& ch : host) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    if (n == host) return st.env.localIp;
    if (n == "localhost") return "127.0.0.1";
    return node;
}

// Run fn(lookup) once the lookup for this thread is done; block until then.
template <class Fn>
void WithLookup(WinsockState& ws, ShimCall& c, const std::string& node, const std::string& service, const addrinfo& hints, Fn fn) {
    GuestThread& t = c.thread;
    if (t.waitPhase == 0) {
        ws.lookups[t.id] = StartLookup(node, service, hints);
        t.waitPhase = 1;
    }
    auto l = ws.lookups[t.id];
    {
        std::lock_guard lk(l->mutex);
        if (!l->done) {
            if (c.WaitElapsedMs() > 15000) {
                ws.lookups.erase(t.id);
                fn(nullptr, EAI_AGAIN);
                return;
            }
            c.Block(5);
            return;
        }
    }
    ws.lookups.erase(t.id);
    fn(l->result, l->status);
}
}  // namespace

bool WinsockEventReady(const Win32State& st, int fd, uint32_t mask) {
    const auto& sockets = st.winsock->sockets;
    auto it = sockets.find(fd);
    pollfd p{fd, PollEvents(mask, it == sockets.end() ? nullptr : const_cast<SockInfo*>(&it->second)), 0};
    if (!p.events) return false;
    return ::poll(&p, 1, 0) > 0 && (p.revents & (p.events | POLLERR | POLLHUP));
}

void InstallWinsock(Win32State& s) {
    Machine& m = s.machine;
    Win32State* st = &s;
    s.winsock = std::make_shared<WinsockState>();
    auto R = [&m](const char* name, int args, ShimFn fn) { m.RegisterShim(kWs, name, args * 4, std::move(fn)); };
    auto O = [&m](uint16_t ord, const char* name) { m.RegisterOrdinal(kWs, ord, name); };

    O(1, "accept");
    O(2, "bind");
    O(3, "closesocket");
    O(4, "connect");
    O(5, "getpeername");
    O(6, "getsockname");
    O(7, "getsockopt");
    O(8, "htonl");
    O(9, "htons");
    O(10, "ioctlsocket");
    O(11, "inet_addr");
    O(12, "inet_ntoa");
    O(13, "listen");
    O(14, "ntohl");
    O(15, "ntohs");
    O(16, "recv");
    O(17, "recvfrom");
    O(18, "select");
    O(19, "send");
    O(20, "sendto");
    O(21, "setsockopt");
    O(22, "shutdown");
    O(23, "socket");
    O(51, "gethostbyaddr");
    O(52, "gethostbyname");
    O(57, "gethostname");
    O(111, "WSAGetLastError");
    O(112, "WSASetLastError");
    O(115, "WSAStartup");
    O(116, "WSACleanup");
    O(151, "__WSAFDIsSet");

    auto fail = [](ShimCall& c, uint32_t err) {
        c.SetLastError(err);
        c.Return(kSocketError);
    };
    auto info = [st](uint32_t s) -> SockInfo* {
        auto it = st->winsock->sockets.find(static_cast<int>(s));
        return it == st->winsock->sockets.end() ? nullptr : &it->second;
    };

    R("WSAStartup", 2, [](ShimCall& c) {
        const uint32_t d = c.Arg(1);
        c.mem.Fill(d, 0, 400);
        c.mem.Write16(d, 0x0202);
        c.mem.Write16(d + 2, 0x0202);
        c.mem.WriteA(d + 4, "WinSock 2.0", 257);
        c.mem.WriteA(d + 261, "Running", 129);
        c.Return(0);
    });
    R("WSACleanup", 0, [](ShimCall& c) { c.Return(0); });
    R("WSAGetLastError", 0, [](ShimCall& c) { c.Return(c.mem.Read32(c.thread.teb + 0x34)); });
    R("WSASetLastError", 1, [](ShimCall& c) {
        c.SetLastError(c.Arg(0));
        c.Return(0);
    });
    R("htons", 1, [](ShimCall& c) { c.Return(htons(static_cast<uint16_t>(c.Arg(0)))); });
    R("ntohs", 1, [](ShimCall& c) { c.Return(ntohs(static_cast<uint16_t>(c.Arg(0)))); });
    R("htonl", 1, [](ShimCall& c) { c.Return(htonl(c.Arg(0))); });
    R("ntohl", 1, [](ShimCall& c) { c.Return(ntohl(c.Arg(0))); });
    R("inet_addr", 1, [](ShimCall& c) {
        in_addr a{};
        c.Return(::inet_aton(c.mem.ReadA(c.Arg(0), 64).c_str(), &a) ? a.s_addr : 0xFFFFFFFFu);
    });
    R("inet_ntoa", 1, [st](ShimCall& c) {
        uint32_t& buf = st->winsock->ntoaBuffer;
        if (!buf) buf = st->machine.Mem().StaticAlloc(32);
        in_addr a{};
        a.s_addr = c.Arg(0);
        c.mem.WriteA(buf, ::inet_ntoa(a), 32);
        c.Return(buf);
    });
    R("inet_pton", 3, [](ShimCall& c) {
        const int fam = HostFamily(c.Arg(0));
        uint8_t out[16];
        const int rc = ::inet_pton(fam, c.mem.ReadA(c.Arg(1), 128).c_str(), out);
        if (rc == 1) c.mem.WriteBytes(c.Arg(2), out, fam == AF_INET6 ? 16 : 4);
        c.Return(static_cast<uint32_t>(rc));
    });
    R("inet_ntop", 4, [](ShimCall& c) {
        const int fam = HostFamily(c.Arg(0));
        uint8_t in[16];
        c.mem.ReadBytes(c.Arg(1), in, fam == AF_INET6 ? 16 : 4);
        char out[64];
        if (!::inet_ntop(fam, in, out, sizeof(out)) || std::strlen(out) + 1 > c.Arg(3)) {
            c.SetLastError(WSAEINVAL);
            c.Return(0);
            return;
        }
        c.mem.WriteA(c.Arg(2), out, c.Arg(3));
        c.Return(c.Arg(2));
    });
    R("gethostname", 2, [st, fail](ShimCall& c) {
        const std::string& name = st->env.computerName;
        if (c.Arg(1) <= name.size()) {
            fail(c, WSAEFAULT);
            return;
        }
        c.mem.WriteA(c.Arg(0), name, c.Arg(1));
        c.Return(0);
    });
    R("gethostbyname", 1, [st](ShimCall& c) {
        // One static hostent per process, like the real per-thread buffer.
        uint32_t& he = st->winsock->hostent;
        if (!he) he = st->machine.Mem().StaticAlloc(0x200);
        addrinfo hints{};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;
        const std::string name = c.mem.ReadA(c.Arg(0), 256);
        WithLookup(*st->winsock, c, LocalName(*st, name), "", hints, [&](addrinfo* res, int rc) {
            if (rc || !res) {
                c.SetLastError(GaiError(rc ? rc : EAI_NONAME));
                c.Return(0);
                return;
            }
            uint32_t addrs = he + 0x100, list = he + 0x80, count = 0;
            for (addrinfo* a = res; a && count < 16; a = a->ai_next) {
                if (a->ai_family != AF_INET) continue;
                const auto* sin = reinterpret_cast<const sockaddr_in*>(a->ai_addr);
                c.mem.WriteBytes(addrs + count * 4, &sin->sin_addr, 4);
                c.mem.Write32(list + count * 4, addrs + count * 4);
                ++count;
            }
            c.mem.Write32(list + count * 4, 0);
            c.mem.WriteA(he + 0x20, name, 0x60);
            c.mem.Write32(he + 0x1C, 0);  // empty alias list
            c.mem.Write32(he + 0x00, he + 0x20);
            c.mem.Write32(he + 0x04, he + 0x1C);
            c.mem.Write16(he + 0x08, 2);
            c.mem.Write16(he + 0x0A, 4);
            c.mem.Write32(he + 0x0C, list);
            c.Return(count ? he : 0);
        });
    });
    R("gethostbyaddr", 3, [](ShimCall& c) {
        c.SetLastError(WSANO_DATA);
        c.Return(0);
    });
    R("getaddrinfo", 4, [st](ShimCall& c) {
        const std::string node = c.Arg(0) ? c.mem.ReadA(c.Arg(0), 256) : "";
        const std::string service = c.Arg(1) ? c.mem.ReadA(c.Arg(1), 64) : "";
        addrinfo hints{};
        if (const uint32_t h = c.Arg(2)) {
            hints.ai_flags = static_cast<int>(c.mem.Read32(h) & 0x7);
            hints.ai_family = HostFamily(c.mem.Read32(h + 4));
            hints.ai_socktype = static_cast<int>(c.mem.Read32(h + 8));
            hints.ai_protocol = static_cast<int>(c.mem.Read32(h + 12));
        }
        const uint32_t resPtr = c.Arg(3);
        WithLookup(*st->winsock, c, node.empty() ? node : LocalName(*st, node), service, hints, [&](addrinfo* res, int rc) {
            if (rc) {
                c.mem.Write32(resPtr, 0);
                const uint32_t e = GaiError(rc);
                c.SetLastError(e);
                c.Return(e);
                return;
            }
            // Windows ADDRINFOA: flags, family, socktype, protocol, addrlen,
            // canonname, addr, next (canonname before addr).
            uint32_t head = 0, prev = 0;
            for (addrinfo* a = res; a; a = a->ai_next) {
                if (a->ai_family != AF_INET && a->ai_family != AF_INET6) continue;
                const uint32_t node32 = c.mem.HeapAlloc(32, true);
                const uint32_t addr = c.mem.HeapAlloc(static_cast<uint32_t>(a->ai_addrlen), true);
                sockaddr_storage tmp{};
                std::memcpy(&tmp, a->ai_addr, a->ai_addrlen);
                const uint16_t fam = WinFamily(a->ai_family);
                std::memcpy(&tmp, &fam, 2);
                c.mem.WriteBytes(addr, &tmp, a->ai_addrlen);
                c.mem.Write32(node32 + 0, static_cast<uint32_t>(a->ai_flags));
                c.mem.Write32(node32 + 4, fam);
                c.mem.Write32(node32 + 8, static_cast<uint32_t>(a->ai_socktype));
                c.mem.Write32(node32 + 12, static_cast<uint32_t>(a->ai_protocol));
                c.mem.Write32(node32 + 16, static_cast<uint32_t>(a->ai_addrlen));
                c.mem.Write32(node32 + 20, a->ai_canonname ? c.mem.NewA(a->ai_canonname) : 0);
                c.mem.Write32(node32 + 24, addr);
                if (prev) c.mem.Write32(prev + 28, node32);
                else head = node32;
                prev = node32;
            }
            c.mem.Write32(resPtr, head);
            c.Return(head ? 0 : WSAHOST_NOT_FOUND);
        });
    });
    R("freeaddrinfo", 1, [](ShimCall& c) {
        for (uint32_t n = c.Arg(0); n;) {
            const uint32_t next = c.mem.Read32(n + 28);
            c.mem.HeapFree(c.mem.Read32(n + 20));
            c.mem.HeapFree(c.mem.Read32(n + 24));
            c.mem.HeapFree(n);
            n = next;
        }
        c.Return(0);
    });

    R("socket", 3, [fail, st](ShimCall& c) {
        const int fam = HostFamily(c.Arg(0));
        const int type = static_cast<int>(c.Arg(1));
        const int fd = ::socket(fam, type | SOCK_NONBLOCK | SOCK_CLOEXEC, static_cast<int>(c.Arg(2)));
        if (fd < 0) {
            fail(c, WsaError(errno));
            return;
        }
        SockInfo si;
        si.type = type;
        st->winsock->sockets[fd] = si;
        NETEMU_LOGI("socket(af=%u, type=%d) -> %d", c.Arg(0), type, fd);
        c.Return(static_cast<uint32_t>(fd));
    });
    R("closesocket", 1, [fail, st](ShimCall& c) {
        const int fd = static_cast<int>(c.Arg(0));
        if (!st->winsock->sockets.erase(fd)) {
            fail(c, WSAENOTSOCK);
            return;
        }
        ::close(fd);
        c.Return(0);
    });
    R("bind", 3, [fail](ShimCall& c) {
        sockaddr_storage a{};
        socklen_t len = 0;
        if (!ReadAddr(c.mem, c.Arg(1), c.Arg(2), &a, &len)) {
            fail(c, WSAEFAULT);
            return;
        }
        if (::bind(static_cast<int>(c.Arg(0)), reinterpret_cast<sockaddr*>(&a), len) != 0) {
            fail(c, WsaError(errno));
            return;
        }
        c.Return(0);
    });
    R("listen", 2, [fail](ShimCall& c) {
        if (::listen(static_cast<int>(c.Arg(0)), static_cast<int>(c.Arg(1))) != 0) fail(c, WsaError(errno));
        else c.Return(0);
    });
    R("accept", 3, [fail, info, st](ShimCall& c) {
        sockaddr_storage a{};
        socklen_t len = sizeof(a);
        const int fd = ::accept4(static_cast<int>(c.Arg(0)), reinterpret_cast<sockaddr*>(&a), &len, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (fd < 0) {
            SockInfo* si = info(c.Arg(0));
            if (errno == EAGAIN && si && si->blocking) {
                c.Block(5);
                return;
            }
            fail(c, WsaError(errno));
            return;
        }
        SockInfo si;
        si.type = SOCK_STREAM;
        st->winsock->sockets[fd] = si;
        WriteAddr(c.mem, c.Arg(1), c.Arg(2), a, len);
        c.Return(static_cast<uint32_t>(fd));
    });
    R("connect", 3, [fail, info](ShimCall& c) {
        const int fd = static_cast<int>(c.Arg(0));
        SockInfo* si = info(c.Arg(0));
        if (!si) {
            fail(c, WSAENOTSOCK);
            return;
        }
        if (c.thread.waitPhase == 0) {
            sockaddr_storage a{};
            socklen_t len = 0;
            if (!ReadAddr(c.mem, c.Arg(1), c.Arg(2), &a, &len)) {
                fail(c, WSAEFAULT);
                return;
            }
            if (::connect(fd, reinterpret_cast<sockaddr*>(&a), len) == 0) {
                c.Return(0);
                return;
            }
            if (errno != EINPROGRESS || !si->blocking) {
                fail(c, WsaError(errno));
                return;
            }
            c.thread.waitPhase = 1;
        }
        pollfd p{fd, POLLOUT, 0};
        if (::poll(&p, 1, 0) <= 0) {
            if (c.WaitElapsedMs() > 21000) fail(c, WSAETIMEDOUT);
            else c.Block(5);
            return;
        }
        int err = 0;
        socklen_t elen = sizeof(err);
        ::getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &elen);
        if (err) fail(c, WsaError(err));
        else c.Return(0);
    });
    R("shutdown", 2, [fail](ShimCall& c) {
        const uint32_t how = c.Arg(1);  // SD_RECEIVE 0, SD_SEND 1, SD_BOTH 2: same as SHUT_*
        if (::shutdown(static_cast<int>(c.Arg(0)), static_cast<int>(how)) != 0) fail(c, WsaError(errno));
        else c.Return(0);
    });
    auto nameQuery = [fail](ShimCall& c, bool peer) {
        sockaddr_storage a{};
        socklen_t len = sizeof(a);
        const int rc = peer ? ::getpeername(static_cast<int>(c.Arg(0)), reinterpret_cast<sockaddr*>(&a), &len)
                            : ::getsockname(static_cast<int>(c.Arg(0)), reinterpret_cast<sockaddr*>(&a), &len);
        if (rc != 0) {
            fail(c, WsaError(errno));
            return;
        }
        WriteAddr(c.mem, c.Arg(1), c.Arg(2), a, len);
        c.Return(0);
    };
    R("getsockname", 3, [nameQuery](ShimCall& c) { nameQuery(c, false); });
    R("getpeername", 3, [nameQuery](ShimCall& c) { nameQuery(c, true); });
    R("setsockopt", 5, [fail, info](ShimCall& c) {
        const uint32_t level = c.Arg(1), name = c.Arg(2), val = c.Arg(3), len = c.Arg(4);
        SockInfo* si = info(c.Arg(0));
        if (!si) {
            fail(c, WSAENOTSOCK);
            return;
        }
        if (level == 0xFFFF && (name == 0x1006 || name == 0x1005)) {  // timeouts are DWORD milliseconds on Windows
            (name == 0x1006 ? si->recvTimeoutMs : si->sendTimeoutMs) = len >= 4 ? c.mem.Read32(val) : 0;
            c.Return(0);
            return;
        }
        if (level == 0xFFFF && (name == 0xFFFFFFFBu || name == 0x0002)) {  // SO_EXCLUSIVEADDRUSE, SO_ACCEPTCONN
            c.Return(0);
            return;
        }
        int hl = 0, hn = 0;
        if (!MapSockOpt(level, name, &hl, &hn)) {
            NETEMU_TRACE("setsockopt level %u name %u ignored", level, name);
            c.Return(0);
            return;
        }
        uint8_t buf[16] = {};
        const uint32_t n = std::min<uint32_t>(len, sizeof(buf));
        c.mem.ReadBytes(val, buf, n);
        if (hn == SO_LINGER) {
            // Windows LINGER is two u_shorts, Linux two ints.
            struct linger l{};
            l.l_onoff = buf[0] | (buf[1] << 8);
            l.l_linger = buf[2] | (buf[3] << 8);
            ::setsockopt(static_cast<int>(c.Arg(0)), hl, hn, &l, sizeof(l));
        } else {
            int v = 0;
            std::memcpy(&v, buf, std::min<uint32_t>(n, 4));
            if (n == 1) v = buf[0];
            if (::setsockopt(static_cast<int>(c.Arg(0)), hl, hn, &v, sizeof(v)) != 0) {
                fail(c, WsaError(errno));
                return;
            }
        }
        c.Return(0);
    });
    R("getsockopt", 5, [fail, info](ShimCall& c) {
        const uint32_t level = c.Arg(1), name = c.Arg(2), val = c.Arg(3), lenPtr = c.Arg(4);
        SockInfo* si = info(c.Arg(0));
        if (!si) {
            fail(c, WSAENOTSOCK);
            return;
        }
        int hl = 0, hn = 0;
        if (level == 0xFFFF && name == 0x2005) {  // SO_MAX_MSG_SIZE
            c.mem.Write32(val, 65507);
            c.mem.Write32(lenPtr, 4);
            c.Return(0);
            return;
        }
        if (!MapSockOpt(level, name, &hl, &hn)) {
            fail(c, WSAENOPROTOOPT);
            return;
        }
        if (hn == SO_RCVTIMEO || hn == SO_SNDTIMEO) {
            c.mem.Write32(val, hn == SO_RCVTIMEO ? si->recvTimeoutMs : si->sendTimeoutMs);
            c.mem.Write32(lenPtr, 4);
            c.Return(0);
            return;
        }
        int v = 0;
        socklen_t l = sizeof(v);
        if (::getsockopt(static_cast<int>(c.Arg(0)), hl, hn, &v, &l) != 0) {
            fail(c, WsaError(errno));
            return;
        }
        if (hn == SO_ERROR && v) v = static_cast<int>(WsaError(v));
        c.mem.Write32(val, static_cast<uint32_t>(v));
        c.mem.Write32(lenPtr, 4);
        c.Return(0);
    });
    R("ioctlsocket", 3, [fail, info](ShimCall& c) {
        SockInfo* si = info(c.Arg(0));
        if (!si) {
            fail(c, WSAENOTSOCK);
            return;
        }
        const uint32_t cmd = c.Arg(1);
        if (cmd == 0x8004667Eu) {  // FIONBIO
            si->blocking = c.mem.Read32(c.Arg(2)) == 0;
            c.Return(0);
        } else if (cmd == 0x4004667Fu) {  // FIONREAD
            int n = 0;
            ::ioctl(static_cast<int>(c.Arg(0)), FIONREAD, &n);
            c.mem.Write32(c.Arg(2), static_cast<uint32_t>(n));
            c.Return(0);
        } else {
            fail(c, WSAEINVAL);
        }
    });
    R("WSAIoctl", 9, [fail](ShimCall& c) {
        const uint32_t code = c.Arg(1);
        if (code == 0x9800000Cu || code == 0x98000004u) {  // SIO_UDP_CONNRESET, SIO_KEEPALIVE_VALS
            if (c.Arg(6)) c.mem.Write32(c.Arg(6), 0);
            c.Return(0);
            return;
        }
        fail(c, WSAEOPNOTSUPP);
    });

    // Send and receive. A blocking-mode socket retries until data moves or its
    // SO_RCVTIMEO/SO_SNDTIMEO expires.
    auto sendTo = [fail, info](ShimCall& c, uint32_t s, uint32_t buf, uint32_t len, uint32_t flags, uint32_t to, uint32_t toLen) {
        SockInfo* si = info(s);
        if (!si) {
            fail(c, WSAENOTSOCK);
            return;
        }
        const uint8_t* data = c.mem.Host(buf, len ? len : 1);
        if (len && !data) {
            fail(c, WSAEFAULT);
            return;
        }
        sockaddr_storage a{};
        socklen_t alen = 0;
        const bool hasTo = to && ReadAddr(c.mem, to, toLen, &a, &alen);
        const int hflags = MSG_NOSIGNAL | ((flags & 1) ? MSG_OOB : 0);
        const ssize_t n = hasTo ? ::sendto(static_cast<int>(s), data, len, hflags, reinterpret_cast<sockaddr*>(&a), alen)
                                : ::send(static_cast<int>(s), data, len, hflags);
        if (n < 0) {
            if (errno == EAGAIN && si->blocking && (!si->sendTimeoutMs || c.WaitElapsedMs() < si->sendTimeoutMs)) {
                c.Block(2);
                return;
            }
            fail(c, errno == EAGAIN && si->blocking ? WSAETIMEDOUT : WsaError(errno));
            return;
        }
        c.Return(static_cast<uint32_t>(n));
    };
    R("send", 4, [sendTo](ShimCall& c) { sendTo(c, c.Arg(0), c.Arg(1), c.Arg(2), c.Arg(3), 0, 0); });
    R("sendto", 6, [sendTo](ShimCall& c) { sendTo(c, c.Arg(0), c.Arg(1), c.Arg(2), c.Arg(3), c.Arg(4), c.Arg(5)); });
    auto recvFrom = [fail, info](ShimCall& c, uint32_t s, uint32_t buf, uint32_t len, uint32_t flags, uint32_t from, uint32_t fromLen) {
        SockInfo* si = info(s);
        if (!si) {
            fail(c, WSAENOTSOCK);
            return;
        }
        uint8_t* data = c.mem.Host(buf, len ? len : 1);
        if (len && !data) {
            fail(c, WSAEFAULT);
            return;
        }
        sockaddr_storage a{};
        socklen_t alen = sizeof(a);
        int hflags = ((flags & 2) ? MSG_PEEK : 0) | ((flags & 1) ? MSG_OOB : 0);
        if (si->type == SOCK_DGRAM) hflags |= MSG_TRUNC;
        const ssize_t n = ::recvfrom(static_cast<int>(s), data, len, hflags, reinterpret_cast<sockaddr*>(&a), &alen);
        if (n < 0) {
            if (errno == EAGAIN && si->blocking && (!si->recvTimeoutMs || c.WaitElapsedMs() < si->recvTimeoutMs)) {
                c.Block(2);
                return;
            }
            fail(c, errno == EAGAIN && si->blocking ? WSAETIMEDOUT : WsaError(errno));
            return;
        }
        if (from) WriteAddr(c.mem, from, fromLen, a, alen);
        if (si->type == SOCK_DGRAM && static_cast<size_t>(n) > len) {
            fail(c, WSAEMSGSIZE);
            return;
        }
        c.Return(static_cast<uint32_t>(n));
    };
    R("recv", 4, [recvFrom](ShimCall& c) { recvFrom(c, c.Arg(0), c.Arg(1), c.Arg(2), c.Arg(3), 0, 0); });
    R("recvfrom", 6, [recvFrom](ShimCall& c) { recvFrom(c, c.Arg(0), c.Arg(1), c.Arg(2), c.Arg(3), c.Arg(4), c.Arg(5)); });

    // fd_set on Windows: { u_int fd_count; SOCKET fd_array[64]; }.
    R("select", 5, [fail](ShimCall& c) {
        const uint32_t sets[3] = {c.Arg(1), c.Arg(2), c.Arg(3)};
        std::vector<pollfd> fds;
        std::vector<std::pair<int, int>> owner;  // (set index, socket)
        for (int k = 0; k < 3; ++k) {
            if (!sets[k]) continue;
            const uint32_t count = std::min<uint32_t>(c.mem.Read32(sets[k]), 64);
            for (uint32_t i = 0; i < count; ++i) {
                const int fd = static_cast<int>(c.mem.Read32(sets[k] + 4 + i * 4));
                fds.push_back({fd, static_cast<short>(k == 0 ? POLLIN : k == 1 ? POLLOUT : POLLPRI), 0});
                owner.push_back({k, fd});
            }
        }
        uint32_t timeoutMs = Win32::kInfinite;
        if (c.Arg(4)) timeoutMs = c.mem.Read32(c.Arg(4)) * 1000u + c.mem.Read32(c.Arg(4) + 4) / 1000u;
        const int rc = fds.empty() ? 0 : ::poll(fds.data(), fds.size(), 0);
        if (rc < 0) {
            fail(c, WsaError(errno));
            return;
        }
        if (rc == 0 && timeoutMs != 0 && (timeoutMs == Win32::kInfinite || c.WaitElapsedMs() < timeoutMs)) {
            c.Block(timeoutMs == Win32::kInfinite ? 2 : std::min<uint32_t>(2, timeoutMs));
            return;
        }
        uint32_t counts[3] = {0, 0, 0};
        std::vector<int> ready[3];
        for (size_t i = 0; i < fds.size(); ++i) {
            const short want = fds[i].events;
            if (fds[i].revents & (want | POLLERR | POLLHUP)) ready[owner[i].first].push_back(owner[i].second);
        }
        uint32_t total = 0;
        for (int k = 0; k < 3; ++k) {
            if (!sets[k]) continue;
            counts[k] = static_cast<uint32_t>(ready[k].size());
            c.mem.Write32(sets[k], counts[k]);
            for (uint32_t i = 0; i < counts[k]; ++i) c.mem.Write32(sets[k] + 4 + i * 4, static_cast<uint32_t>(ready[k][i]));
            total += counts[k];
        }
        c.Return(total);
    });
    R("__WSAFDIsSet", 2, [](ShimCall& c) {
        const uint32_t set = c.Arg(1);
        const uint32_t count = std::min<uint32_t>(c.mem.Read32(set), 64);
        for (uint32_t i = 0; i < count; ++i)
            if (c.mem.Read32(set + 4 + i * 4) == c.Arg(0)) {
                c.Return(1);
                return;
            }
        c.Return(0);
    });

    // Event objects for WSAEventSelect are manual-reset kernel events.
    R("WSACreateEvent", 0, [st](ShimCall& c) {
        auto e = std::make_shared<KEvent>();
        e->manual = true;
        c.Return(st->Add(e));
    });
    R("WSACloseEvent", 1, [st](ShimCall& c) { c.Return(st->Close(c.Arg(0)) ? 1 : 0); });
    R("WSAEventSelect", 3, [st, fail, info](ShimCall& c) {
        SockInfo* si = info(c.Arg(0));
        auto e = st->GetAs<KEvent>(c.Arg(1), KObject::Kind::Event);
        if (!si || !e) {
            fail(c, si ? WSAEINVAL : WSAENOTSOCK);
            return;
        }
        si->blocking = false;  // WSAEventSelect puts the socket in non-blocking mode
        e->socketFd = c.Arg(2) ? static_cast<int>(c.Arg(0)) : -1;
        e->networkMask = c.Arg(2);
        c.Return(0);
    });
    R("WSAWaitForMultipleEvents", 5, [st](ShimCall& c) {
        const uint32_t n = std::min<uint32_t>(c.Arg(0), 64);
        const uint32_t arr = c.Arg(1);
        const bool all = c.Arg(2) != 0;
        const uint32_t ms = c.Arg(3);
        uint32_t first = 0xFFFFFFFFu, signaled = 0;
        for (uint32_t i = 0; i < n; ++i) {
            if (st->IsSignaled(c.mem.Read32(arr + i * 4), c.thread.id)) {
                ++signaled;
                if (first == 0xFFFFFFFFu) first = i;
            }
        }
        if (n && ((all && signaled == n) || (!all && signaled))) {
            if (all) for (uint32_t i = 0; i < n; ++i) st->Acquire(c.mem.Read32(arr + i * 4), c.thread.id);
            else st->Acquire(c.mem.Read32(arr + first * 4), c.thread.id);
            c.Return(all ? 0 : first);
            return;
        }
        if (ms == 0 || (ms != Win32::kInfinite && c.WaitElapsedMs() >= ms)) {
            c.Return(258);  // WSA_WAIT_TIMEOUT
            return;
        }
        c.Block(ms == Win32::kInfinite ? 2 : static_cast<uint32_t>(std::min<uint64_t>(2, ms - c.WaitElapsedMs())));
    });
}

}  // namespace NetEmu
