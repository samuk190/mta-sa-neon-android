// Desktop-only imports: windows, clipboard, COM, certificates, ACLs, URLs.
// Each one fails or succeeds the way a headless Windows process would, and
// the first call is logged so new code paths show up in logcat.

#include "NetEmuLog.h"
#include "Win32Shims.h"

#include <fcntl.h>
#include <unistd.h>

#include <random>

namespace NetEmu {

namespace {
// Minimal printf for wsprintfA: %[-0][width][l|h]{s,S,d,i,u,x,X,c,p,%}.
std::string FormatWs(GuestMemory& mem, const std::string& fmt, uint32_t argVa) {
    std::string out;
    auto next = [&]() {
        const uint32_t v = mem.Read32(argVa);
        argVa += 4;
        return v;
    };
    for (size_t i = 0; i < fmt.size(); ++i) {
        if (fmt[i] != '%') {
            out.push_back(fmt[i]);
            continue;
        }
        if (++i >= fmt.size()) break;
        if (fmt[i] == '%') {
            out.push_back('%');
            continue;
        }
        bool left = false, zero = false;
        while (i < fmt.size() && (fmt[i] == '-' || fmt[i] == '0' || fmt[i] == '#')) {
            left = left || fmt[i] == '-';
            zero = zero || fmt[i] == '0';
            ++i;
        }
        size_t width = 0;
        while (i < fmt.size() && std::isdigit(static_cast<unsigned char>(fmt[i]))) width = width * 10 + (fmt[i++] - '0');
        while (i < fmt.size() && (fmt[i] == 'l' || fmt[i] == 'h')) ++i;
        if (i >= fmt.size()) break;
        std::string piece;
        char buf[32];
        switch (fmt[i]) {
        case 's': piece = mem.ReadA(next(), 4096); break;
        case 'S': piece = mem.ReadW(next(), 4096); break;
        case 'c': piece.push_back(static_cast<char>(next())); break;
        case 'd':
        case 'i': std::snprintf(buf, sizeof(buf), "%d", static_cast<int32_t>(next())); piece = buf; break;
        case 'u': std::snprintf(buf, sizeof(buf), "%u", next()); piece = buf; break;
        case 'x': std::snprintf(buf, sizeof(buf), "%x", next()); piece = buf; break;
        case 'X': std::snprintf(buf, sizeof(buf), "%X", next()); piece = buf; break;
        case 'p': std::snprintf(buf, sizeof(buf), "%08X", next()); piece = buf; break;
        default: piece.push_back(fmt[i]); break;
        }
        if (piece.size() < width) {
            const std::string pad(width - piece.size(), zero && !left ? '0' : ' ');
            piece = left ? piece + std::string(width - piece.size(), ' ') : pad + piece;
        }
        out += piece;
    }
    return out;
}

void FillRandom(uint8_t* p, size_t n) {
    const int fd = ::open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    size_t got = 0;
    if (fd >= 0) {
        while (got < n) {
            const ssize_t r = ::read(fd, p + got, n - got);
            if (r <= 0) break;
            got += static_cast<size_t>(r);
        }
        ::close(fd);
    }
    if (got < n) {
        std::random_device rd;
        for (; got < n; ++got) p[got] = static_cast<uint8_t>(rd());
    }
}
}  // namespace

void InstallStubs(Win32State& s) {
    Machine& m = s.machine;
    Win32State* st = &s;
    auto R = [&m](const char* dll, const char* name, int args, ShimFn fn) { m.RegisterShim(dll, name, args < 0 ? -1 : args * 4, std::move(fn)); };
    // A stub that logs its first use and returns a fixed value.
    auto V = [&m, st](const char* dll, const char* name, int args, uint32_t value, uint32_t lastError = 0) {
        const std::string label = std::string(dll) + "!" + name;
        m.RegisterShim(dll, name, args * 4, [st, label, value, lastError](ShimCall& c) {
            st->NoteStub(label);
            if (lastError) c.SetLastError(lastError);
            c.Return(value);
        });
    };

    // --- user32 --------------------------------------------------------------------
    constexpr char kU[] = "user32.dll";
    R(kU, "GetSystemMetrics", 1, [](ShimCall& c) {
        const uint32_t i = c.Arg(0);
        c.Return(i == 0 ? 1920 : i == 1 ? 1080 : 0);
    });
    auto box = [](ShimCall& c, bool wide) {
        const std::string text = wide ? c.mem.ReadW(c.Arg(1), 1024) : c.mem.ReadA(c.Arg(1), 1024);
        const std::string title = wide ? c.mem.ReadW(c.Arg(2), 256) : c.mem.ReadA(c.Arg(2), 256);
        NETEMU_LOGW("guest MessageBox [%s]: %s", title.c_str(), text.c_str());
        c.Return(1);  // IDOK
    };
    R(kU, "MessageBoxA", 4, [box](ShimCall& c) { box(c, false); });
    R(kU, "MessageBoxW", 4, [box](ShimCall& c) { box(c, true); });
    // A message pump has nothing to receive: the thread idles in GetMessage.
    R(kU, "GetMessageA", 4, [](ShimCall& c) { c.Block(1000); });
    V(kU, "PeekMessageA", 5, 0);
    V(kU, "PostMessageA", 4, 1);
    V(kU, "PostThreadMessageA", 4, 1);
    V(kU, "PostQuitMessage", 1, 0);
    V(kU, "TranslateMessage", 1, 0);
    V(kU, "DispatchMessageA", 1, 0);
    V(kU, "DefWindowProcW", 4, 0);
    V(kU, "CallWindowProcW", 5, 0);
    V(kU, "CallNextHookEx", 4, 0);
    V(kU, "SetWindowLongA", 3, 0);
    V(kU, "SetWindowLongW", 3, 0);
    V(kU, "GetWindowLongA", 2, 0);
    V(kU, "FindWindowA", 2, 0);
    V(kU, "EnumWindows", 2, 1);
    V(kU, "GetClassNameW", 3, 0);
    V(kU, "GetWindowTextW", 3, 0);
    V(kU, "GetWindowThreadProcessId", 2, 0);
    V(kU, "GetAsyncKeyState", 1, 0);
    V(kU, "OpenClipboard", 1, 0);
    V(kU, "CloseClipboard", 0, 0);
    V(kU, "EmptyClipboard", 0, 0);
    V(kU, "SetClipboardData", 2, 0);
    R(kU, "wsprintfA", -1, [](ShimCall& c) {
        const std::string out = FormatWs(c.mem, c.mem.ReadA(c.Arg(1), 4096), c.esp + 12);
        c.Return(static_cast<uint32_t>(c.mem.WriteA(c.Arg(0), out, 1025)));
    });

    // --- advapi32 security and legacy crypto ----------------------------------------
    constexpr char kA[] = "advapi32.dll";
    R(kA, "AllocateAndInitializeSid", 11, [](ShimCall& c) {
        const uint32_t sid = c.mem.HeapAlloc(8 + 8 * 4, true);
        const uint32_t count = std::min<uint32_t>(c.Arg(1) & 0xFF, 8);
        c.mem.Write8(sid, 1);
        c.mem.Write8(sid + 1, static_cast<uint8_t>(count));
        uint8_t auth[6] = {};
        c.mem.ReadBytes(c.Arg(0), auth, 6);
        c.mem.WriteBytes(sid + 2, auth, 6);
        for (uint32_t i = 0; i < count; ++i) c.mem.Write32(sid + 8 + i * 4, c.Arg(2 + static_cast<int>(i)));
        c.mem.Write32(c.Arg(10), sid);
        c.Return(1);
    });
    R(kA, "FreeSid", 1, [](ShimCall& c) {
        c.mem.HeapFree(c.Arg(0));
        c.Return(0);
    });
    R(kA, "CheckTokenMembership", 3, [](ShimCall& c) {
        c.mem.Write32(c.Arg(2), 0);  // not an administrator
        c.Return(1);
    });
    V(kA, "ConvertStringSidToSidA", 2, 0, 1337);  // ERROR_INVALID_SID
    V(kA, "InitializeSecurityDescriptor", 2, 1);
    V(kA, "SetSecurityDescriptorDacl", 4, 1);
    V(kA, "GetSecurityDescriptorDacl", 4, 0, Win32::kErrorInvalidParameter);
    R(kA, "BuildExplicitAccessWithNameA", 5, [](ShimCall& c) {
        c.mem.Fill(c.Arg(0), 0, 32);
        c.Return(0);
    });
    R(kA, "SetEntriesInAclA", 4, [](ShimCall& c) {
        c.mem.Write32(c.Arg(3), 0);
        c.Return(0);
    });
    V(kA, "QueryServiceObjectSecurity", 5, 0, Win32::kErrorAccessDenied);
    V(kA, "SetServiceObjectSecurity", 3, 0, Win32::kErrorAccessDenied);
    V(kA, "CryptAcquireContextA", 5, 0, 0x80090006u);  // NTE_BAD_SIGNATURE: no legacy CSP
    V(kA, "CryptReleaseContext", 2, 1);
    V(kA, "CryptCreateHash", 5, 0, 0x80090008u);  // NTE_BAD_ALGID
    V(kA, "CryptHashData", 4, 0, Win32::kErrorInvalidHandle);
    V(kA, "CryptGetHashParam", 5, 0, Win32::kErrorInvalidHandle);
    V(kA, "CryptDestroyHash", 1, 1);

    // Service Control Manager (resolved through GetProcAddress). Every service
    // netc asks for exists and reports SERVICE_RUNNING; the names are logged.
    auto scmOpen = [st](ShimCall& c) {
        auto h = std::make_shared<KGeneric>();
        h->name = "<scm>";
        c.Return(st->Add(h));
    };
    R(kA, "OpenSCManagerA", 3, scmOpen);
    R(kA, "OpenSCManagerW", 3, scmOpen);
    // netc looks up cheat-tool drivers (Process Hacker, Cheat Engine) by
    // name: none of them is installed here.
    auto openService = [](ShimCall& c, const std::string& name) {
        NETEMU_TRACE("OpenService %s: not installed", name.c_str());
        c.SetLastError(1060);  // ERROR_SERVICE_DOES_NOT_EXIST
        c.Return(0);
    };
    auto createService = [st](ShimCall& c, const std::string& name) {
        NETEMU_LOGI("CreateService %s", name.c_str());
        auto h = std::make_shared<KGeneric>();
        h->name = "<service>" + name;
        c.Return(st->Add(h));
    };
    R(kA, "OpenServiceA", 3, [openService](ShimCall& c) { openService(c, c.mem.ReadA(c.Arg(1), 256)); });
    R(kA, "OpenServiceW", 3, [openService](ShimCall& c) { openService(c, c.mem.ReadW(c.Arg(1), 256)); });
    auto writeStatus = [](GuestMemory& mem, uint32_t p) {
        // SERVICE_STATUS: type, state, accepted, exit, service exit, checkpoint, wait hint.
        mem.Fill(p, 0, 28);
        mem.Write32(p, 0x10);  // SERVICE_WIN32_OWN_PROCESS
        mem.Write32(p + 4, 4); // SERVICE_RUNNING
        mem.Write32(p + 8, 1); // SERVICE_ACCEPT_STOP
    };
    R(kA, "QueryServiceStatus", 2, [writeStatus](ShimCall& c) {
        writeStatus(c.mem, c.Arg(1));
        c.Return(1);
    });
    R(kA, "QueryServiceStatusEx", 5, [writeStatus](ShimCall& c) {
        if (c.Arg(4)) c.mem.Write32(c.Arg(4), 36);
        if (c.Arg(3) < 36) {
            c.SetLastError(Win32::kErrorInsufficientBuffer);
            c.Return(0);
            return;
        }
        writeStatus(c.mem, c.Arg(2));
        c.mem.Write32(c.Arg(2) + 28, 0x1004);  // dwProcessId
        c.mem.Write32(c.Arg(2) + 32, 0);
        c.Return(1);
    });
    R(kA, "StartServiceA", 3, [](ShimCall& c) {
        c.SetLastError(1056);  // ERROR_SERVICE_ALREADY_RUNNING
        c.Return(0);
    });
    R(kA, "StartServiceW", 3, [](ShimCall& c) {
        c.SetLastError(1056);
        c.Return(0);
    });
    R(kA, "ControlService", 3, [writeStatus](ShimCall& c) {
        if (c.Arg(2)) writeStatus(c.mem, c.Arg(2));
        c.Return(1);
    });
    R(kA, "CloseServiceHandle", 1, [st](ShimCall& c) { c.Return(st->Close(c.Arg(0)) ? 1 : 0); });
    R(kA, "CreateServiceA", 13, [createService](ShimCall& c) { createService(c, c.mem.ReadA(c.Arg(1), 256)); });
    R(kA, "CreateServiceW", 13, [createService](ShimCall& c) { createService(c, c.mem.ReadW(c.Arg(1), 256)); });
    V(kA, "DeleteService", 1, 1);
    V(kA, "ChangeServiceConfigA", 11, 1);
    V(kA, "ChangeServiceConfigW", 11, 1);
    R(kA, "QueryServiceConfigA", 4, [](ShimCall& c) {
        if (c.Arg(3)) c.mem.Write32(c.Arg(3), 36);
        if (c.Arg(2) < 36) {
            c.SetLastError(Win32::kErrorInsufficientBuffer);
            c.Return(0);
            return;
        }
        c.mem.Fill(c.Arg(1), 0, 36);
        c.mem.Write32(c.Arg(1), 0x10);
        c.mem.Write32(c.Arg(1) + 4, 2);  // SERVICE_AUTO_START
        c.Return(1);
    });

    // RtlGenRandom, used by the CRT's rand_s.
    R(kA, "SystemFunction036", 2, [](ShimCall& c) {
        uint8_t* p = c.mem.Host(c.Arg(0), c.Arg(1) ? c.Arg(1) : 1);
        if (!p) {
            c.Return(0);
            return;
        }
        FillRandom(p, c.Arg(1));
        c.Return(1);
    });

    // --- version.dll (resolved through GetProcAddress) ---------------------------------
    // System DLLs report Windows 10 22H2 (10.0.19045.1). Other files have no
    // version resource. The block is a VS_VERSIONINFO with only the fixed part.
    constexpr char kV[] = "version.dll";
    auto isSystemFile = [](const std::string& path) {
        const std::string p = Lower(path);
        return p.find("\\windows\\") != std::string::npos || p.find("\\system32\\") != std::string::npos ||
               p.find("\\syswow64\\") != std::string::npos || p.find('\\') == std::string::npos;
    };
    constexpr uint32_t kVersionBlock = 40 + 52;  // header and key, then VS_FIXEDFILEINFO
    R(kV, "GetFileVersionInfoSizeW", 2, [isSystemFile](ShimCall& c) {
        const std::string path = c.mem.ReadW(c.Arg(0), 1024);
        NETEMU_LOGI("GetFileVersionInfoSizeW %s", path.c_str());
        if (c.Arg(1)) c.mem.Write32(c.Arg(1), 0);
        if (!isSystemFile(path)) {
            c.SetLastError(1813);  // ERROR_RESOURCE_TYPE_NOT_FOUND
            c.Return(0);
            return;
        }
        c.Return(kVersionBlock);
    });
    R(kV, "GetFileVersionInfoW", 4, [isSystemFile](ShimCall& c) {
        const std::string path = c.mem.ReadW(c.Arg(0), 1024);
        const uint32_t out = c.Arg(3);
        if (!isSystemFile(path) || c.Arg(2) < kVersionBlock) {
            c.SetLastError(1813);
            c.Return(0);
            return;
        }
        c.mem.Fill(out, 0, kVersionBlock);
        c.mem.Write16(out, kVersionBlock);
        c.mem.Write16(out + 2, 52);
        c.mem.Write16(out + 4, 0);
        c.mem.WriteW(out + 6, "VS_VERSION_INFO", 16);
        const uint32_t ffi = out + 40;  // key "VS_VERSION_INFO" ends at 38, aligned to 40
        c.mem.Write32(ffi + 0, 0xFEEF04BDu);
        c.mem.Write32(ffi + 4, 0x00010000u);
        c.mem.Write32(ffi + 8, (10u << 16) | 0u);      // FileVersionMS 10.0
        c.mem.Write32(ffi + 12, (19045u << 16) | 1u);  // FileVersionLS 19045.1
        c.mem.Write32(ffi + 16, (10u << 16) | 0u);
        c.mem.Write32(ffi + 20, (19045u << 16) | 1u);
        c.mem.Write32(ffi + 24, 0x3F);
        c.mem.Write32(ffi + 32, 0x00040004u);  // VOS_NT_WINDOWS32
        c.mem.Write32(ffi + 36, 2);            // VFT_DLL
        c.Return(1);
    });
    auto verQuery = [](ShimCall& c, const std::string& sub) {
        if (sub == "\\" && c.mem.Read32(c.Arg(0) + 40) == 0xFEEF04BDu) {
            c.mem.Write32(c.Arg(2), c.Arg(0) + 40);
            c.mem.Write32(c.Arg(3), 52);
            c.Return(1);
            return;
        }
        NETEMU_LOGI("VerQueryValue %s: not present", sub.c_str());
        c.Return(0);
    };
    R(kV, "VerQueryValueW", 4, [verQuery](ShimCall& c) { verQuery(c, c.mem.ReadW(c.Arg(1), 256)); });
    R(kV, "VerQueryValueA", 4, [verQuery](ShimCall& c) { verQuery(c, c.mem.ReadA(c.Arg(1), 256)); });

    // --- shell32 ------------------------------------------------------------------------
    constexpr char kS[] = "shell32.dll";
    R(kS, "SHGetFolderPathW", 5, [st](ShimCall& c) {
        const std::string user = "C:\\Users\\" + st->env.userName;
        std::string p;
        switch (c.Arg(1) & 0xFF) {
        case 0x05: p = user + "\\Documents"; break;
        case 0x1A: p = user + "\\AppData\\Roaming"; break;
        case 0x1C: p = user + "\\AppData\\Local"; break;
        case 0x23: p = "C:\\ProgramData"; break;
        case 0x24: p = "C:\\Windows"; break;
        case 0x25: p = "C:\\Windows\\system32"; break;
        case 0x26: p = "C:\\Program Files"; break;
        case 0x2A: p = "C:\\Program Files (x86)"; break;
        case 0x28: p = user; break;
        default: break;
        }
        if (p.empty()) {
            st->NoteStub("SHGetFolderPathW csidl " + std::to_string(c.Arg(1)));
            c.Return(0x80070057u);  // E_INVALIDARG
            return;
        }
        c.mem.WriteW(c.Arg(4), p, 260);
        c.Return(0);
    });
    V(kS, "ShellExecuteA", 6, 2);
    V(kS, "ShellExecuteExW", 1, 0, Win32::kErrorAccessDenied);

    // --- COM: no WMI in this process --------------------------------------------------
    constexpr char kO[] = "ole32.dll";
    V(kO, "CoInitializeEx", 2, 0);
    V(kO, "CoInitializeSecurity", 9, 0);
    R(kO, "CoCreateInstance", 5, [st](ShimCall& c) {
        st->NoteStub("ole32!CoCreateInstance");
        if (c.Arg(4)) c.mem.Write32(c.Arg(4), 0);
        c.Return(0x80040154u);  // REGDB_E_CLASSNOTREG
    });
    V(kO, "CoSetProxyBlanket", 8, 0x80004001u);  // E_NOTIMPL

    constexpr char kOa[] = "oleaut32.dll";
    m.RegisterOrdinal(kOa, 2, "SysAllocString");
    m.RegisterOrdinal(kOa, 6, "SysFreeString");
    m.RegisterOrdinal(kOa, 7, "SysStringLen");
    m.RegisterOrdinal(kOa, 9, "VariantClear");
    m.RegisterOrdinal(kOa, 12, "VariantChangeType");
    m.RegisterOrdinal(kOa, 150, "SysAllocStringByteLen");
    auto bstr = [](ShimCall& c, uint32_t src, uint32_t bytes) {
        const uint32_t p = c.mem.HeapAlloc(bytes + 6, true);
        if (!p) {
            c.Return(0);
            return;
        }
        c.mem.Write32(p, bytes);
        if (src) {
            std::vector<uint8_t> tmp(bytes);
            c.mem.ReadBytes(src, tmp.data(), bytes);
            c.mem.WriteBytes(p + 4, tmp.data(), bytes);
        }
        c.Return(p + 4);
    };
    R(kOa, "SysAllocString", 1, [bstr](ShimCall& c) {
        if (!c.Arg(0)) {
            c.Return(0);
            return;
        }
        bstr(c, c.Arg(0), static_cast<uint32_t>(c.mem.StrLenW(c.Arg(0)) * 2));
    });
    R(kOa, "SysAllocStringByteLen", 2, [bstr](ShimCall& c) { bstr(c, c.Arg(0), c.Arg(1)); });
    R(kOa, "SysFreeString", 1, [](ShimCall& c) {
        if (c.Arg(0)) c.mem.HeapFree(c.Arg(0) - 4);
        c.Return(0);
    });
    R(kOa, "SysStringLen", 1, [](ShimCall& c) { c.Return(c.Arg(0) ? c.mem.Read32(c.Arg(0) - 4) / 2 : 0); });
    R(kOa, "VariantClear", 1, [](ShimCall& c) {
        c.mem.Fill(c.Arg(0), 0, 16);
        c.Return(0);
    });
    V(kOa, "VariantChangeType", 4, 0x80020005u);  // DISP_E_TYPEMISMATCH

    // --- crypt32 / wintrust: no certificate store ----------------------------------------
    constexpr char kC[] = "crypt32.dll";
    V(kC, "CertOpenStore", 5, 0, Win32::kErrorFileNotFound);
    V(kC, "CertCloseStore", 2, 1);
    V(kC, "CertEnumCertificatesInStore", 2, 0, 0x80092004u);  // CRYPT_E_NOT_FOUND
    V(kC, "CertFindCertificateInStore", 6, 0, 0x80092004u);
    V(kC, "CertFreeCertificateContext", 1, 1);
    V(kC, "CertFreeCRLContext", 1, 1);
    V(kC, "CertFreeCTLContext", 1, 1);
    V(kC, "CertFreeCertificateChain", 1, 0);
    V(kC, "CertFreeCertificateChainEngine", 1, 0);
    V(kC, "CertCreateCertificateChainEngine", 2, 0, Win32::kErrorNotSupported);
    V(kC, "CertGetCertificateChain", 8, 0, Win32::kErrorNotSupported);
    V(kC, "CertAddCertificateContextToStore", 4, 0, Win32::kErrorNotSupported);
    V(kC, "CertFindExtension", 3, 0);
    R(kC, "CertGetNameStringA", 6, [](ShimCall& c) {
        if (c.Arg(4) && c.Arg(5)) c.mem.Write8(c.Arg(4), 0);
        c.Return(1);
    });
    R(kC, "CertGetNameStringW", 6, [](ShimCall& c) {
        if (c.Arg(4) && c.Arg(5)) c.mem.Write16(c.Arg(4), 0);
        c.Return(1);
    });
    V(kC, "CryptDecodeObjectEx", 8, 0, 0x80093102u);  // CRYPT_E_ASN1_BADTAG
    V(kC, "CryptQueryObject", 11, 0, 0x80092009u);    // CRYPT_E_NO_MATCH
    V(kC, "CryptStringToBinaryA", 7, 0, Win32::kErrorInvalidParameter);
    V(kC, "PFXImportCertStore", 3, 0, Win32::kErrorInvalidParameter);
    // DPAPI: netc caches its serial as a protected blob. A reversible XOR over
    // the per-install seed keeps the cache self-consistent on this device
    // without a real key store. DATA_BLOB = { DWORD cb; BYTE* pb }.
    auto dpapi = [st](ShimCall& c, bool protect) {
        const uint32_t inBlob = protect ? c.Arg(0) : c.Arg(0);
        const uint32_t outBlob = protect ? c.Arg(6) : c.Arg(6);
        const uint32_t cb = c.mem.Read32(inBlob);
        const uint32_t pb = c.mem.Read32(inBlob + 4);
        if (cb > 0x1000000u) {
            c.SetLastError(Win32::kErrorInvalidParameter);
            c.Return(0);
            return;
        }
        std::vector<uint8_t> data(cb);
        if (cb) c.mem.ReadBytes(pb, data.data(), cb);
        const uint64_t seed = st->machineSeed ^ 0xA5A5A5A5A5A5A5A5ull;
        for (uint32_t i = 0; i < cb; ++i) data[i] ^= static_cast<uint8_t>(seed >> ((i & 7) * 8));
        const uint32_t out = c.mem.HeapAlloc(cb ? cb : 1, false);
        if (cb) c.mem.WriteBytes(out, data.data(), cb);
        c.mem.Write32(outBlob, cb);
        c.mem.Write32(outBlob + 4, out);
        c.Return(1);
    };
    R(kC, "CryptProtectData", 7, [dpapi](ShimCall& c) { dpapi(c, true); });
    R(kC, "CryptUnprotectData", 7, [dpapi](ShimCall& c) { dpapi(c, false); });
    V(kC, "CryptProtectMemory", 3, 1);
    V(kC, "CryptUnprotectMemory", 3, 1);
    constexpr char kW[] = "wintrust.dll";
    V(kW, "WinVerifyTrust", 3, 0x800B0100u);  // TRUST_E_NOSIGNATURE
    V(kW, "WTHelperProvDataFromStateData", 1, 0);
    V(kW, "WTHelperGetProvSignerFromChain", 4, 0);
    V(kW, "WTHelperGetProvCertFromChain", 2, 0);

    // --- small libraries ---------------------------------------------------------------------
    R("bcrypt.dll", "BCryptGenRandom", 4, [](ShimCall& c) {
        uint8_t* p = c.mem.Host(c.Arg(1), c.Arg(2) ? c.Arg(2) : 1);
        if (!p) {
            c.Return(0xC000000Du);  // STATUS_INVALID_PARAMETER
            return;
        }
        FillRandom(p, c.Arg(2));
        c.Return(0);
    });
    R("winmm.dll", "timeGetTime", 0, [st](ShimCall& c) { c.Return(static_cast<uint32_t>(st->machine.NowMs() + 3600000ull)); });
    // Optional fingerprint sources netc probes through GetProcAddress. Each
    // reports "nothing here" so netc falls back to the sources we do provide.
    V(kA, "CredReadW", 5, 0, 1168);   // ERROR_NOT_FOUND
    V(kA, "CredReadA", 5, 0, 1168);
    V(kA, "CredFree", 1, 0);
    V(kA, "CredWriteW", 2, 0, Win32::kErrorAccessDenied);
    constexpr char kK2[] = "kernel32.dll";
    V(kK2, "GetSystemFirmwareTable", 4, 0);  // 0 bytes: no SMBIOS table
    V(kK2, "EnumSystemFirmwareTables", 3, 0);
    V(kK2, "AddDllDirectory", 1, 0);
    V(kK2, "SetDefaultDllDirectories", 1, 1);
    R(kK2, "GetNativeSystemInfo", 1, [](ShimCall& c) {
        const uint32_t p = c.Arg(0);
        c.mem.Fill(p, 0, 36);
        c.mem.Write16(p, 0);        // PROCESSOR_ARCHITECTURE_INTEL
        c.mem.Write32(p + 4, 0x1000);
        c.mem.Write32(p + 8, 0x10000);
        c.mem.Write32(p + 12, 0x7FFEFFFF);
        c.mem.Write32(p + 16, 0xF);
        c.mem.Write32(p + 20, 4);
        c.mem.Write16(p + 32, 6);
        c.Return(0);
    });
    V("iphlpapi.dll", "GetIfTable", 3, Win32::kErrorNotSupported);
    V("iphlpapi.dll", "GetNumberOfInterfaces", 1, 0);
    V("secur32.dll", "GetUserNameExW", 3, 0);
    V("secur32.dll", "GetComputerObjectNameW", 3, 0);
    V("secur32.dll", "InitSecurityInterfaceA", 0, 0);
    auto idn = [](ShimCall& c) {
        // ASCII host names pass through unchanged.
        const int32_t n = static_cast<int32_t>(c.Arg(2));
        const uint32_t len = n < 0 ? static_cast<uint32_t>(c.mem.StrLenW(c.Arg(1)) + 1) : static_cast<uint32_t>(n);
        if (!c.Arg(4)) {
            c.Return(len);
            return;
        }
        if (c.Arg(4) < len) {
            c.SetLastError(Win32::kErrorInsufficientBuffer);
            c.Return(0);
            return;
        }
        std::vector<uint8_t> tmp(len * 2);
        c.mem.ReadBytes(c.Arg(1), tmp.data(), tmp.size());
        c.mem.WriteBytes(c.Arg(3), tmp.data(), tmp.size());
        c.Return(len);
    };
    R("normaliz.dll", "IdnToAscii", 5, idn);
    R("normaliz.dll", "IdnToUnicode", 5, idn);
    // One Ethernet adapter whose MAC derives from the per-install seed: the
    // MTA serial is a hash of this MAC and the volume serial.
    R("iphlpapi.dll", "GetAdaptersInfo", 2, [st](ShimCall& c) {
        constexpr uint32_t kSize = 640;
        if (!c.Arg(1)) {
            c.SetLastError(Win32::kErrorInvalidParameter);
            c.Return(Win32::kErrorInvalidParameter);
            return;
        }
        if (!c.Arg(0) || c.mem.Read32(c.Arg(1)) < kSize) {
            c.mem.Write32(c.Arg(1), kSize);
            c.Return(111);  // ERROR_BUFFER_OVERFLOW
            return;
        }
        const uint32_t a = c.Arg(0);
        c.mem.Fill(a, 0, kSize);
        c.mem.Write32(a + 0, 0);   // Next
        c.mem.Write32(a + 4, 1);   // ComboIndex
        c.mem.WriteA(a + 8, "{SAONLINE-NIC-0001}", 260);
        c.mem.WriteA(a + 268, "SAONLINE Virtual Ethernet", 132);
        c.mem.Write32(a + 400, 6);  // AddressLength
        const uint64_t seed = st->machineSeed;
        uint8_t mac[6] = {0x02, static_cast<uint8_t>(seed), static_cast<uint8_t>(seed >> 8), static_cast<uint8_t>(seed >> 16),
                          static_cast<uint8_t>(seed >> 24), static_cast<uint8_t>(seed >> 32)};
        c.mem.WriteBytes(a + 404, mac, 6);
        c.mem.Write32(a + 412, 1);   // Index
        c.mem.Write32(a + 416, 6);   // MIB_IF_TYPE_ETHERNET
        c.mem.WriteA(a + 432, "192.168.1.100", 16);  // IpAddressList.IpAddress
        c.mem.WriteA(a + 448, "255.255.255.0", 16);  // IpAddressList.IpMask
        c.Return(0);
    });
    R("iphlpapi.dll", "GetAdaptersAddresses", 5, [](ShimCall& c) {
        // Report the buffer is too small once, then that there is nothing more.
        if (c.Arg(4)) {
            const uint32_t need = c.mem.Read32(c.Arg(4));
            if (need < 1024) {
                c.mem.Write32(c.Arg(4), 1024);
                c.Return(111);
                return;
            }
        }
        c.Return(232);  // ERROR_NO_DATA: no addresses of the requested family
    });
    V("iphlpapi.dll", "GetIpNetTable2", 2, Win32::kErrorNotSupported);
    V("iphlpapi.dll", "FreeMibTable", 1, 0);
    V("iphlpapi.dll", "SendARP", 4, Win32::kErrorNotSupported);
    V("wininet.dll", "InternetCrackUrlA", 4, 0, 12006);  // ERROR_INTERNET_UNRECOGNIZED_SCHEME
    V("wininet.dll", "InternetCreateUrlA", 4, 0, 12006);
}

}  // namespace NetEmu
