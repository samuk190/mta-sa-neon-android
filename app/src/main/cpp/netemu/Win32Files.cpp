// kernel32 files and directories, advapi32 registry.
//
// Guest paths map to Win32Env::dataDir/drive_<letter>/<lower-case path>.
// The module's own path maps to the real DLL so integrity reads see it.

#include "NetEmuLog.h"
#include "Win32Shims.h"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>

namespace NetEmu {

namespace {
constexpr char kK32[] = "kernel32.dll";
constexpr char kAdv[] = "advapi32.dll";
constexpr uint32_t kAttrDirectory = 0x10;
constexpr uint32_t kAttrArchive = 0x20;
constexpr uint32_t kInvalidAttributes = 0xFFFFFFFFu;

uint32_t ErrnoToWin32(int e) {
    switch (e) {
    case ENOENT: return Win32::kErrorFileNotFound;
    case ENOTDIR: return Win32::kErrorPathNotFound;
    case EACCES:
    case EPERM: return Win32::kErrorAccessDenied;
    case EEXIST: return Win32::kErrorAlreadyExists;
    case ENOMEM: return Win32::kErrorNotEnoughMemory;
    case EBADF: return Win32::kErrorInvalidHandle;
    default: return Win32::kErrorInvalidParameter;
    }
}

void MakeParents(const std::string& path) {
    for (size_t pos = 1; (pos = path.find('/', pos)) != std::string::npos; ++pos) {
        const std::string dir = path.substr(0, pos);
        ::mkdir(dir.c_str(), 0700);
    }
}

uint64_t ToFileTime(const struct timespec& ts) {
    return static_cast<uint64_t>(ts.tv_sec) * 10000000ull + static_cast<uint64_t>(ts.tv_nsec) / 100ull + 116444736000000000ull;
}

bool WildMatch(const std::string& pattern, const std::string& name) {
    // '*' and '?' on lower-case strings; "*.*" matches every name.
    if (pattern == "*.*" || pattern == "*") return true;
    size_t p = 0, n = 0, star = std::string::npos, mark = 0;
    while (n < name.size()) {
        if (p < pattern.size() && (pattern[p] == '?' || pattern[p] == name[n])) {
            ++p;
            ++n;
        } else if (p < pattern.size() && pattern[p] == '*') {
            star = p++;
            mark = n;
        } else if (star != std::string::npos) {
            p = star + 1;
            n = ++mark;
        } else {
            return false;
        }
    }
    while (p < pattern.size() && pattern[p] == '*') ++p;
    return p == pattern.size();
}

std::string NormalizeGuest(const Win32State& s, const std::string& path) {
    std::string p = path;
    std::replace(p.begin(), p.end(), '/', '\\');
    if (p.rfind("\\\\?\\", 0) == 0) p.erase(0, 4);
    if (p.size() < 2 || p[1] != ':') {
        if (!p.empty() && p[0] == '\\') p = s.CurrentDirectory.substr(0, 2) + p;
        else p = s.CurrentDirectory + "\\" + p;
    }
    std::vector<std::string> parts;
    std::string seg;
    for (size_t i = 3; i <= p.size(); ++i) {
        if (i == p.size() || p[i] == '\\') {
            if (seg == "..") {
                if (!parts.empty()) parts.pop_back();
            } else if (!seg.empty() && seg != ".") {
                parts.push_back(seg);
            }
            seg.clear();
        } else {
            seg.push_back(p[i]);
        }
    }
    std::string out = p.substr(0, 2) + "\\";
    for (size_t i = 0; i < parts.size(); ++i) out += (i ? "\\" : "") + parts[i];
    return out;
}

void WriteFindData(GuestMemory& mem, uint32_t out, const std::string& name, const struct stat& stt) {
    mem.Fill(out, 0, 592);
    mem.Write32(out, S_ISDIR(stt.st_mode) ? kAttrDirectory : kAttrArchive);
    mem.Write64(out + 4, ToFileTime(stt.st_ctim));
    mem.Write64(out + 12, ToFileTime(stt.st_atim));
    mem.Write64(out + 20, ToFileTime(stt.st_mtim));
    mem.Write32(out + 28, static_cast<uint32_t>(static_cast<uint64_t>(stt.st_size) >> 32));
    mem.Write32(out + 32, static_cast<uint32_t>(stt.st_size));
    mem.WriteW(out + 44, name, 260);
}
}  // namespace

void InstallFiles(Win32State& s) {
    Machine& m = s.machine;
    Win32State* st = &s;
    auto R = [&m](const char* dll, const char* name, int args, ShimFn fn) { m.RegisterShim(dll, name, args * 4, std::move(fn)); };

    // --- open / close ------------------------------------------------------------
    // Returns a handle or INVALID_HANDLE_VALUE with the last error set.
    auto openFile = [st](ShimCall& c, const std::string& guest, uint32_t access, uint32_t disposition, uint32_t flags) {
        // Devices: one fixed disk. FairplayKD and other drivers are absent.
        if (guest.rfind("\\\\.\\", 0) == 0) {
            if (Lower(guest) == "\\\\.\\physicaldrive0") {
                auto d = std::make_shared<KFile>();
                d->disk = true;
                d->console = true;  // no host fd
                d->name = guest;
                c.SetLastError(0);
                return st->Add(d);
            }
            NETEMU_LOGI("CreateFile %s: no such device", guest.c_str());
            c.SetLastError(Win32::kErrorFileNotFound);
            return Win32::kInvalidHandle;
        }
        const std::string host = st->HostPath(guest);
        int oflags = O_CLOEXEC;
        const bool wantWrite = (access & 0x40000000u) || (access & 0x10000000u) || (access & 0x2u) || (access & 0x4u);
        const bool wantRead = (access & 0x80000000u) || (access & 0x10000000u) || (access & 0x1u) || !wantWrite;
        oflags |= wantWrite ? (wantRead ? O_RDWR : O_WRONLY) : O_RDONLY;
        struct stat stt{};
        const bool exists = ::stat(host.c_str(), &stt) == 0;
        switch (disposition) {
        case 1:  // CREATE_NEW
            if (exists) {
                c.SetLastError(Win32::kErrorFileExists);
                return Win32::kInvalidHandle;
            }
            oflags |= O_CREAT | O_EXCL;
            break;
        case 2: oflags |= O_CREAT | O_TRUNC; break;  // CREATE_ALWAYS
        case 3: break;                               // OPEN_EXISTING
        case 4: oflags |= O_CREAT; break;            // OPEN_ALWAYS
        case 5: oflags |= O_TRUNC; break;            // TRUNCATE_EXISTING
        default:
            c.SetLastError(Win32::kErrorInvalidParameter);
            return Win32::kInvalidHandle;
        }
        if (exists && S_ISDIR(stt.st_mode)) {
            if (!(flags & 0x02000000u)) {  // FILE_FLAG_BACKUP_SEMANTICS
                c.SetLastError(Win32::kErrorAccessDenied);
                return Win32::kInvalidHandle;
            }
            oflags = O_RDONLY | O_DIRECTORY | O_CLOEXEC;
        }
        if (oflags & O_CREAT) MakeParents(host);
        const int fd = ::open(host.c_str(), oflags, 0600);
        if (fd < 0) {
            c.SetLastError(ErrnoToWin32(errno));
            NETEMU_LOGI("CreateFile %s -> errno %d", guest.c_str(), errno);
            return Win32::kInvalidHandle;
        }
        auto f = std::make_shared<KFile>();
        f->fd = fd;
        f->hostPath = host;
        f->name = NormalizeGuest(*st, guest);
        // ERROR_ALREADY_EXISTS reports an existing file for CREATE_ALWAYS / OPEN_ALWAYS.
        c.SetLastError(exists && (disposition == 2 || disposition == 4) ? Win32::kErrorAlreadyExists : 0);
        NETEMU_LOGI("CreateFile %s (%s)", guest.c_str(), exists ? "existing" : "new");
        return st->Add(f);
    };
    R(kK32, "CreateFileW", 7, [openFile](ShimCall& c) { c.Return(openFile(c, c.mem.ReadW(c.Arg(0), 1024), c.Arg(1), c.Arg(4), c.Arg(5))); });
    R(kK32, "CreateFileA", 7, [openFile](ShimCall& c) { c.Return(openFile(c, c.mem.ReadA(c.Arg(0), 1024), c.Arg(1), c.Arg(4), c.Arg(5))); });
    R(kK32, "CreateFile2", 5, [openFile](ShimCall& c) {
        const uint32_t ext = c.Arg(4);
        const uint32_t flags = ext ? c.mem.Read32(ext + 8) : 0;
        c.Return(openFile(c, c.mem.ReadW(c.Arg(0), 1024), c.Arg(1), c.Arg(3), flags));
    });
    auto fileOf = [st](uint32_t h) { return st->GetAs<KFile>(h, KObject::Kind::File); };

    R(kK32, "ReadFile", 5, [fileOf](ShimCall& c) {
        auto f = fileOf(c.Arg(0));
        if (!f) {
            c.SetLastError(Win32::kErrorInvalidHandle);
            c.Return(0);
            return;
        }
        uint32_t got = 0;
        if (!f->console) {
            uint8_t* dst = c.mem.Host(c.Arg(1), c.Arg(2));
            const ssize_t n = dst ? ::read(f->fd, dst, c.Arg(2)) : -1;
            if (n < 0) {
                c.SetLastError(ErrnoToWin32(errno));
                c.Return(0);
                return;
            }
            got = static_cast<uint32_t>(n);
        }
        if (c.Arg(3)) c.mem.Write32(c.Arg(3), got);
        c.Return(1);
    });
    R(kK32, "WriteFile", 5, [fileOf](ShimCall& c) {
        auto f = fileOf(c.Arg(0));
        if (!f) {
            c.SetLastError(Win32::kErrorInvalidHandle);
            c.Return(0);
            return;
        }
        const uint32_t len = c.Arg(2);
        if (f->console) {
            NETEMU_LOGI("guest stdout: %s", c.mem.ReadA(c.Arg(1), len, true).c_str());
        } else {
            const uint8_t* src = c.mem.Host(c.Arg(1), len);
            if (!src || ::write(f->fd, src, len) != static_cast<ssize_t>(len)) {
                c.SetLastError(ErrnoToWin32(errno));
                c.Return(0);
                return;
            }
        }
        if (c.Arg(3)) c.mem.Write32(c.Arg(3), len);
        c.Return(1);
    });
    auto seek = [fileOf](ShimCall& c, uint32_t h, int64_t dist, uint32_t method, int64_t* out) {
        auto f = fileOf(h);
        if (!f || f->console) {
            c.SetLastError(Win32::kErrorInvalidHandle);
            return false;
        }
        const int whence = method == 0 ? SEEK_SET : method == 1 ? SEEK_CUR : SEEK_END;
        const off_t r = ::lseek(f->fd, static_cast<off_t>(dist), whence);
        if (r < 0) {
            c.SetLastError(ErrnoToWin32(errno));
            return false;
        }
        *out = r;
        return true;
    };
    R(kK32, "SetFilePointer", 4, [seek](ShimCall& c) {
        int64_t dist = static_cast<int32_t>(c.Arg(1));
        if (c.Arg(2)) dist = static_cast<int64_t>((static_cast<uint64_t>(c.mem.Read32(c.Arg(2))) << 32) | c.Arg(1));
        int64_t pos = 0;
        if (!seek(c, c.Arg(0), dist, c.Arg(3), &pos)) {
            c.Return(0xFFFFFFFFu);
            return;
        }
        if (c.Arg(2)) c.mem.Write32(c.Arg(2), static_cast<uint32_t>(static_cast<uint64_t>(pos) >> 32));
        c.SetLastError(0);
        c.Return(static_cast<uint32_t>(pos));
    });
    R(kK32, "SetFilePointerEx", 5, [seek](ShimCall& c) {
        int64_t pos = 0;
        if (!seek(c, c.Arg(0), static_cast<int64_t>(c.Arg64(1)), c.Arg(4), &pos)) {
            c.Return(0);
            return;
        }
        if (c.Arg(3)) c.mem.Write64(c.Arg(3), static_cast<uint64_t>(pos));
        c.Return(1);
    });
    R(kK32, "SetEndOfFile", 1, [fileOf](ShimCall& c) {
        auto f = fileOf(c.Arg(0));
        if (!f || f->console) {
            c.Return(0);
            return;
        }
        const off_t pos = ::lseek(f->fd, 0, SEEK_CUR);
        c.Return(::ftruncate(f->fd, pos) == 0 ? 1 : 0);
    });
    R(kK32, "FlushFileBuffers", 1, [fileOf](ShimCall& c) {
        auto f = fileOf(c.Arg(0));
        if (f && !f->console) ::fsync(f->fd);
        c.Return(f ? 1 : 0);
    });
    R(kK32, "GetFileType", 1, [fileOf](ShimCall& c) {
        auto f = fileOf(c.Arg(0));
        if (!f) {
            c.SetLastError(Win32::kErrorInvalidHandle);
            c.Return(0);
            return;
        }
        c.Return(f->console ? 2 : 1);  // FILE_TYPE_CHAR / FILE_TYPE_DISK
    });
    R(kK32, "GetFileSizeEx", 2, [fileOf](ShimCall& c) {
        auto f = fileOf(c.Arg(0));
        struct stat stt{};
        if (!f || f->console || ::fstat(f->fd, &stt) != 0) {
            c.SetLastError(Win32::kErrorInvalidHandle);
            c.Return(0);
            return;
        }
        c.mem.Write64(c.Arg(1), static_cast<uint64_t>(stt.st_size));
        c.Return(1);
    });
    R(kK32, "GetFileInformationByHandle", 2, [fileOf](ShimCall& c) {
        auto f = fileOf(c.Arg(0));
        struct stat stt{};
        if (!f || f->console || ::fstat(f->fd, &stt) != 0) {
            c.SetLastError(Win32::kErrorInvalidHandle);
            c.Return(0);
            return;
        }
        const uint32_t p = c.Arg(1);
        c.mem.Fill(p, 0, 52);
        c.mem.Write32(p, S_ISDIR(stt.st_mode) ? kAttrDirectory : kAttrArchive);
        c.mem.Write64(p + 4, ToFileTime(stt.st_ctim));
        c.mem.Write64(p + 12, ToFileTime(stt.st_atim));
        c.mem.Write64(p + 20, ToFileTime(stt.st_mtim));
        c.mem.Write32(p + 28, 0x5A0E5A0Eu);
        c.mem.Write32(p + 32, static_cast<uint32_t>(static_cast<uint64_t>(stt.st_size) >> 32));
        c.mem.Write32(p + 36, static_cast<uint32_t>(stt.st_size));
        c.mem.Write32(p + 40, static_cast<uint32_t>(stt.st_nlink));
        c.mem.Write32(p + 44, static_cast<uint32_t>(static_cast<uint64_t>(stt.st_ino) >> 32));
        c.mem.Write32(p + 48, static_cast<uint32_t>(stt.st_ino));
        c.Return(1);
    });
    R(kK32, "GetFileInformationByHandleEx", 4, [fileOf](ShimCall& c) {
        auto f = fileOf(c.Arg(0));
        struct stat stt{};
        if (!f || f->console || ::fstat(f->fd, &stt) != 0) {
            c.SetLastError(Win32::kErrorInvalidHandle);
            c.Return(0);
            return;
        }
        const uint32_t cls = c.Arg(1), p = c.Arg(2), size = c.Arg(3);
        if (cls == 0 && size >= 40) {  // FileBasicInfo
            c.mem.Fill(p, 0, 40);
            c.mem.Write64(p, ToFileTime(stt.st_ctim));
            c.mem.Write64(p + 8, ToFileTime(stt.st_atim));
            c.mem.Write64(p + 16, ToFileTime(stt.st_mtim));
            c.mem.Write64(p + 24, ToFileTime(stt.st_mtim));
            c.mem.Write32(p + 32, S_ISDIR(stt.st_mode) ? kAttrDirectory : kAttrArchive);
            c.Return(1);
        } else if (cls == 1 && size >= 24) {  // FileStandardInfo
            c.mem.Fill(p, 0, 24);
            c.mem.Write64(p, static_cast<uint64_t>(stt.st_blocks) * 512ull);
            c.mem.Write64(p + 8, static_cast<uint64_t>(stt.st_size));
            c.mem.Write32(p + 16, static_cast<uint32_t>(stt.st_nlink));
            c.mem.Write8(p + 21, S_ISDIR(stt.st_mode) ? 1 : 0);
            c.Return(1);
        } else {
            c.SetLastError(Win32::kErrorNotSupported);
            c.Return(0);
        }
    });
    R(kK32, "GetFinalPathNameByHandleW", 4, [fileOf](ShimCall& c) {
        auto f = fileOf(c.Arg(0));
        if (!f || f->console) {
            c.SetLastError(Win32::kErrorInvalidHandle);
            c.Return(0);
            return;
        }
        const std::string path = "\\\\?\\" + f->name;
        if (c.Arg(2) <= path.size()) {
            c.Return(static_cast<uint32_t>(path.size() + 1));
            return;
        }
        c.Return(static_cast<uint32_t>(c.mem.WriteW(c.Arg(1), path, c.Arg(2))));
    });

    // --- paths -------------------------------------------------------------------
    auto attributes = [st](const std::string& guest, struct stat* out) {
        const std::string host = st->HostPath(guest);
        return ::stat(host.c_str(), out) == 0;
    };
    R(kK32, "GetFileAttributesExW", 3, [attributes](ShimCall& c) {
        struct stat stt{};
        if (!attributes(c.mem.ReadW(c.Arg(0), 1024), &stt)) {
            c.SetLastError(Win32::kErrorFileNotFound);
            c.Return(0);
            return;
        }
        const uint32_t p = c.Arg(2);
        c.mem.Fill(p, 0, 36);
        c.mem.Write32(p, S_ISDIR(stt.st_mode) ? kAttrDirectory : kAttrArchive);
        c.mem.Write64(p + 4, ToFileTime(stt.st_ctim));
        c.mem.Write64(p + 12, ToFileTime(stt.st_atim));
        c.mem.Write64(p + 20, ToFileTime(stt.st_mtim));
        c.mem.Write32(p + 28, static_cast<uint32_t>(static_cast<uint64_t>(stt.st_size) >> 32));
        c.mem.Write32(p + 32, static_cast<uint32_t>(stt.st_size));
        c.Return(1);
    });
    auto getAttributes = [st](ShimCall& c, const std::string& guest) {
        struct stat stt{};
        if (guest.rfind("\\\\.\\", 0) == 0 || ::stat(st->HostPath(guest).c_str(), &stt) != 0) {
            c.SetLastError(Win32::kErrorFileNotFound);
            c.Return(kInvalidAttributes);
            return;
        }
        c.Return(S_ISDIR(stt.st_mode) ? kAttrDirectory : kAttrArchive);
    };
    R(kK32, "GetFileAttributesW", 1, [getAttributes](ShimCall& c) { getAttributes(c, c.mem.ReadW(c.Arg(0), 1024)); });
    R(kK32, "GetFileAttributesA", 1, [getAttributes](ShimCall& c) { getAttributes(c, c.mem.ReadA(c.Arg(0), 1024)); });
    auto setAttributes = [attributes](ShimCall& c, const std::string& guest) {
        struct stat stt{};
        if (!attributes(guest, &stt)) {
            c.SetLastError(Win32::kErrorFileNotFound);
            c.Return(0);
            return;
        }
        c.Return(1);
    };
    R(kK32, "SetFileAttributesW", 2, [setAttributes](ShimCall& c) { setAttributes(c, c.mem.ReadW(c.Arg(0), 1024)); });
    R(kK32, "SetFileAttributesA", 2, [setAttributes](ShimCall& c) { setAttributes(c, c.mem.ReadA(c.Arg(0), 1024)); });
    R(kK32, "DeleteFileW", 1, [st](ShimCall& c) {
        if (::unlink(st->HostPath(c.mem.ReadW(c.Arg(0), 1024)).c_str()) != 0) {
            c.SetLastError(ErrnoToWin32(errno));
            c.Return(0);
            return;
        }
        c.Return(1);
    });
    R(kK32, "MoveFileExA", 3, [st](ShimCall& c) {
        const std::string from = st->HostPath(c.mem.ReadA(c.Arg(0), 1024));
        if (!c.Arg(1)) {  // delete on reboot / immediate delete
            ::unlink(from.c_str());
            c.Return(1);
            return;
        }
        const std::string to = st->HostPath(c.mem.ReadA(c.Arg(1), 1024));
        MakeParents(to);
        if (::rename(from.c_str(), to.c_str()) != 0) {
            c.SetLastError(ErrnoToWin32(errno));
            c.Return(0);
            return;
        }
        c.Return(1);
    });
    R(kK32, "CreateDirectoryW", 2, [st](ShimCall& c) {
        const std::string host = st->HostPath(c.mem.ReadW(c.Arg(0), 1024));
        MakeParents(host);
        if (::mkdir(host.c_str(), 0700) != 0) {
            c.SetLastError(errno == EEXIST ? Win32::kErrorAlreadyExists : ErrnoToWin32(errno));
            c.Return(0);
            return;
        }
        c.Return(1);
    });
    R(kK32, "GetCurrentDirectoryW", 2, [st](ShimCall& c) {
        const std::string& dir = st->CurrentDirectory;
        if (c.Arg(0) <= dir.size()) {
            c.Return(static_cast<uint32_t>(dir.size() + 1));
            return;
        }
        c.Return(static_cast<uint32_t>(c.mem.WriteW(c.Arg(1), dir, c.Arg(0))));
    });
    R(kK32, "GetFullPathNameW", 4, [st](ShimCall& c) {
        const std::string full = NormalizeGuest(*st, c.mem.ReadW(c.Arg(0), 1024));
        if (c.Arg(1) <= full.size()) {
            c.Return(static_cast<uint32_t>(full.size() + 1));
            return;
        }
        const uint32_t n = static_cast<uint32_t>(c.mem.WriteW(c.Arg(2), full, c.Arg(1)));
        if (c.Arg(3)) {
            const size_t slash = full.find_last_of('\\');
            c.mem.Write32(c.Arg(3), slash == std::string::npos ? 0 : c.Arg(2) + static_cast<uint32_t>(slash + 1) * 2);
        }
        c.Return(n);
    });
    R(kK32, "GetLongPathNameW", 3, [](ShimCall& c) {
        const std::string p = c.mem.ReadW(c.Arg(0), 1024);
        if (c.Arg(2) <= p.size()) {
            c.Return(static_cast<uint32_t>(p.size() + 1));
            return;
        }
        c.Return(static_cast<uint32_t>(c.mem.WriteW(c.Arg(1), p, c.Arg(2))));
    });

    // --- directory search ------------------------------------------------------------
    auto findFirst = [st](ShimCall& c, const std::string& pattern, uint32_t out) {
        const std::string norm = NormalizeGuest(*st, pattern);
        const size_t slash = norm.find_last_of('\\');
        const std::string dirGuest = norm.substr(0, slash);
        const std::string mask = Lower(norm.substr(slash + 1));
        const std::string hostDir = st->HostPath(dirGuest.size() == 2 ? dirGuest + "\\" : dirGuest);
        auto find = std::make_shared<KFind>();
        find->hostDir = hostDir;
        if (DIR* d = ::opendir(hostDir.c_str())) {
            while (dirent* e = ::readdir(d)) {
                const std::string n = e->d_name;
                if (WildMatch(mask, Lower(n))) find->names.push_back(n);
            }
            ::closedir(d);
        }
        if (find->names.empty()) {
            c.SetLastError(Win32::kErrorFileNotFound);
            c.Return(Win32::kInvalidHandle);
            return;
        }
        std::sort(find->names.begin(), find->names.end());
        struct stat stt{};
        ::stat((hostDir + "/" + find->names[0]).c_str(), &stt);
        WriteFindData(c.mem, out, find->names[0], stt);
        find->next = 1;
        c.Return(st->Add(find));
    };
    R(kK32, "FindFirstFileW", 2, [findFirst](ShimCall& c) { findFirst(c, c.mem.ReadW(c.Arg(0), 1024), c.Arg(1)); });
    R(kK32, "FindFirstFileExW", 6, [findFirst](ShimCall& c) { findFirst(c, c.mem.ReadW(c.Arg(0), 1024), c.Arg(2)); });
    R(kK32, "FindNextFileW", 2, [st](ShimCall& c) {
        auto find = st->GetAs<KFind>(c.Arg(0), KObject::Kind::Find);
        if (!find || find->next >= find->names.size()) {
            c.SetLastError(Win32::kErrorNoMoreFiles);
            c.Return(0);
            return;
        }
        const std::string& n = find->names[find->next++];
        struct stat stt{};
        ::stat((find->hostDir + "/" + n).c_str(), &stt);
        WriteFindData(c.mem, c.Arg(1), n, stt);
        c.Return(1);
    });
    R(kK32, "FindClose", 1, [st](ShimCall& c) { c.Return(st->Close(c.Arg(0)) ? 1 : 0); });

    // --- file mapping (anonymous or file-backed copy) ---------------------------------
    R(kK32, "CreateFileMappingA", 6, [st, fileOf](ShimCall& c) {
        const std::string name = c.Arg(5) ? c.mem.ReadA(c.Arg(5), 260) : "";
        if (const uint32_t existing = st->FindNamed(name)) {
            c.SetLastError(Win32::kErrorAlreadyExists);
            c.Return(existing);
            return;
        }
        auto map = std::make_shared<KMapping>();
        map->name = name;
        uint64_t size = (static_cast<uint64_t>(c.Arg(3)) << 32) | c.Arg(4);
        auto f = c.Arg(0) == Win32::kInvalidHandle ? nullptr : fileOf(c.Arg(0));
        struct stat stt{};
        if (f && !size && ::fstat(f->fd, &stt) == 0) size = static_cast<uint64_t>(stt.st_size);
        if (!size || size > 0x10000000ull) {
            c.SetLastError(Win32::kErrorInvalidParameter);
            c.Return(0);
            return;
        }
        map->size = static_cast<uint32_t>(size);
        map->view = c.mem.AllocPages(map->size);
        if (f && map->view) ::pread(f->fd, c.mem.Host(map->view, map->size), map->size, 0);
        c.SetLastError(0);
        c.Return(map->view ? st->Add(map) : 0);
    });
    R(kK32, "MapViewOfFile", 5, [st](ShimCall& c) {
        auto map = st->GetAs<KMapping>(c.Arg(0), KObject::Kind::Mapping);
        const uint32_t offset = c.Arg(3);
        c.Return(map && offset < map->size ? map->view + offset : 0);
    });
    R(kK32, "UnmapViewOfFile", 1, [](ShimCall& c) { c.Return(1); });

    // --- registry ----------------------------------------------------------------------
    auto keyPath = [st](uint32_t hKey, const std::string& sub) -> std::string {
        std::string base;
        switch (hKey) {
        case 0x80000000u: base = "hkcr"; break;
        case 0x80000001u: base = "hkcu"; break;
        case 0x80000002u: base = "hklm"; break;
        case 0x80000003u: base = "hku"; break;
        default: {
            auto it = st->regKeys.find(hKey);
            if (it == st->regKeys.end()) return {};
            base = it->second.path;
        }
        }
        std::string s = Lower(sub);
        std::replace(s.begin(), s.end(), '/', '\\');
        while (!s.empty() && s.back() == '\\') s.pop_back();
        while (!s.empty() && s.front() == '\\') s.erase(0, 1);
        return s.empty() ? base : base + "\\" + s;
    };
    auto newKey = [st](const std::string& path) {
        const uint32_t h = 0x00F00000u + (st->nextRegKey += 4);
        st->regKeys[h] = {path};
        return h;
    };
    // A shim reads strings as ANSI, W as UTF-16; the stored data is the raw
    // value bytes either way (netc stores REG_SZ as the width it wrote).
    auto readStr = [](ShimCall& c, uint32_t va, bool wide) { return wide ? c.mem.ReadW(va, 512) : c.mem.ReadA(va, 512); };
    auto writeStr = [](ShimCall& c, uint32_t va, const std::string& v, uint32_t cap, bool wide) {
        return wide ? c.mem.WriteW(va, v, cap) : c.mem.WriteA(va, v, cap);
    };
    auto subkeyList = [st](const std::string& path) {
        std::vector<std::string> out;
        for (const auto& [k, v] : st->registry) {
            if (k.rfind(path + "\\", 0) != 0) continue;
            const std::string rest = k.substr(path.size() + 1);
            const std::string child = rest.substr(0, rest.find('\\'));
            if (std::find(out.begin(), out.end(), child) == out.end()) out.push_back(child);
        }
        std::sort(out.begin(), out.end());
        return out;
    };

    auto regOpen = [st, keyPath, newKey, readStr](ShimCall& c, bool wide) {
        const std::string path = keyPath(c.Arg(0), c.Arg(1) ? readStr(c, c.Arg(1), wide) : "");
        if (path.empty()) {
            c.Return(Win32::kErrorInvalidHandle);
            return;
        }
        bool exists = st->registry.count(path) > 0;
        for (const auto& [k, v] : st->registry) exists = exists || k.rfind(path + "\\", 0) == 0;
        if (!exists && path.find('\\') != std::string::npos) {
            NETEMU_LOGI("RegOpenKeyEx %s: not found", path.c_str());
            c.Return(Win32::kErrorFileNotFound);
            return;
        }
        if (c.Arg(4)) c.mem.Write32(c.Arg(4), newKey(path));
        c.Return(0);
    };
    R(kAdv, "RegOpenKeyExW", 5, [regOpen](ShimCall& c) { regOpen(c, true); });
    R(kAdv, "RegOpenKeyExA", 5, [regOpen](ShimCall& c) { regOpen(c, false); });
    auto regOpenShort = [st, keyPath, newKey, readStr](ShimCall& c, bool wide) {
        const std::string path = keyPath(c.Arg(0), c.Arg(1) ? readStr(c, c.Arg(1), wide) : "");
        if (c.Arg(2)) c.mem.Write32(c.Arg(2), newKey(path));
        c.Return(st->registry.count(path) || path.find('\\') == std::string::npos ? 0 : Win32::kErrorFileNotFound);
    };
    R(kAdv, "RegOpenKeyW", 3, [regOpenShort](ShimCall& c) { regOpenShort(c, true); });
    R(kAdv, "RegOpenKeyA", 3, [regOpenShort](ShimCall& c) { regOpenShort(c, false); });

    auto regCreate = [st, keyPath, newKey, readStr](ShimCall& c, bool wide) {
        const std::string path = keyPath(c.Arg(0), c.Arg(1) ? readStr(c, c.Arg(1), wide) : "");
        if (path.empty()) {
            c.Return(Win32::kErrorInvalidHandle);
            return;
        }
        const bool existed = st->registry.count(path) > 0;
        st->registry[path];
        if (c.Arg(7)) c.mem.Write32(c.Arg(7), newKey(path));
        if (c.Arg(8)) c.mem.Write32(c.Arg(8), existed ? 2 : 1);
        c.Return(0);
    };
    R(kAdv, "RegCreateKeyExW", 9, [regCreate](ShimCall& c) { regCreate(c, true); });
    R(kAdv, "RegCreateKeyExA", 9, [regCreate](ShimCall& c) { regCreate(c, false); });

    auto regQuery = [st, keyPath, readStr](ShimCall& c, bool wide) {
        const std::string path = keyPath(c.Arg(0), "");
        const std::string name = Lower(c.Arg(1) ? readStr(c, c.Arg(1), wide) : "");
        auto k = st->registry.find(path);
        if (k == st->registry.end() || !k->second.count(name)) {
            NETEMU_LOGI("RegQuery %s\\%s: not found", path.c_str(), name.c_str());
            c.Return(Win32::kErrorFileNotFound);
            return;
        }
        auto [type, data] = k->second.at(name);
        // Convert a stored UTF-16 REG_SZ down to ANSI for an A query.
        if (!wide && type == 1 && data.size() >= 2) {
            std::string ansi;
            for (size_t i = 0; i + 1 < data.size(); i += 2) ansi.push_back(static_cast<char>(data[i]));
            data.assign(ansi.begin(), ansi.end());
        }
        if (c.Arg(3)) c.mem.Write32(c.Arg(3), type);
        const uint32_t pcb = c.Arg(5);
        if (!c.Arg(4)) {
            if (pcb) c.mem.Write32(pcb, static_cast<uint32_t>(data.size()));
            c.Return(0);
            return;
        }
        const uint32_t cap = pcb ? c.mem.Read32(pcb) : 0;
        if (pcb) c.mem.Write32(pcb, static_cast<uint32_t>(data.size()));
        if (cap < data.size()) {
            c.Return(Win32::kErrorMoreData);
            return;
        }
        if (!data.empty()) c.mem.WriteBytes(c.Arg(4), data.data(), data.size());
        c.Return(0);
    };
    R(kAdv, "RegQueryValueExW", 6, [regQuery](ShimCall& c) { regQuery(c, true); });
    R(kAdv, "RegQueryValueExA", 6, [regQuery](ShimCall& c) { regQuery(c, false); });

    auto regSet = [st, keyPath, readStr](ShimCall& c, bool wide) {
        const std::string path = keyPath(c.Arg(0), "");
        if (path.empty()) {
            c.Return(Win32::kErrorInvalidHandle);
            return;
        }
        const std::string name = Lower(c.Arg(1) ? readStr(c, c.Arg(1), wide) : "");
        std::vector<uint8_t> data(c.Arg(5));
        if (!data.empty()) c.mem.ReadBytes(c.Arg(4), data.data(), data.size());
        st->registry[path][name] = {c.Arg(3), data};
        st->SaveRegistry();
        c.Return(0);
    };
    R(kAdv, "RegSetValueExW", 6, [regSet](ShimCall& c) { regSet(c, true); });
    R(kAdv, "RegSetValueExA", 6, [regSet](ShimCall& c) { regSet(c, false); });

    auto regDeleteValue = [st, keyPath, readStr](ShimCall& c, bool wide) {
        const std::string path = keyPath(c.Arg(0), "");
        auto k = st->registry.find(path);
        if (k != st->registry.end()) {
            k->second.erase(Lower(c.Arg(1) ? readStr(c, c.Arg(1), wide) : ""));
            st->SaveRegistry();
        }
        c.Return(0);
    };
    R(kAdv, "RegDeleteValueW", 2, [regDeleteValue](ShimCall& c) { regDeleteValue(c, true); });
    R(kAdv, "RegDeleteValueA", 2, [regDeleteValue](ShimCall& c) { regDeleteValue(c, false); });
    auto regDeleteKey = [st, keyPath, readStr](ShimCall& c, bool wide) {
        const std::string path = keyPath(c.Arg(0), c.Arg(1) ? readStr(c, c.Arg(1), wide) : "");
        for (auto it = st->registry.begin(); it != st->registry.end();)
            it = (it->first == path || it->first.rfind(path + "\\", 0) == 0) ? st->registry.erase(it) : std::next(it);
        st->SaveRegistry();
        c.Return(0);
    };
    R(kAdv, "RegDeleteKeyW", 2, [regDeleteKey](ShimCall& c) { regDeleteKey(c, true); });
    R(kAdv, "RegDeleteKeyA", 2, [regDeleteKey](ShimCall& c) { regDeleteKey(c, false); });

    auto regEnumKey = [keyPath, subkeyList, writeStr](ShimCall& c, bool wide) {
        const std::string path = keyPath(c.Arg(0), "");
        const auto kids = subkeyList(path);
        if (c.Arg(1) >= kids.size()) {
            c.Return(Win32::kErrorNoMoreItems);
            return;
        }
        const std::string& name = kids[c.Arg(1)];
        const uint32_t pcb = c.Arg(3);
        if (pcb && c.mem.Read32(pcb) <= name.size()) {
            c.Return(Win32::kErrorMoreData);
            return;
        }
        writeStr(c, c.Arg(2), name, pcb ? c.mem.Read32(pcb) : static_cast<uint32_t>(name.size() + 1), wide);
        if (pcb) c.mem.Write32(pcb, static_cast<uint32_t>(name.size()));
        c.Return(0);
    };
    R(kAdv, "RegEnumKeyExW", 8, [regEnumKey](ShimCall& c) { regEnumKey(c, true); });
    R(kAdv, "RegEnumKeyExA", 8, [regEnumKey](ShimCall& c) { regEnumKey(c, false); });

    auto regEnumValue = [st, keyPath, writeStr](ShimCall& c, bool wide) {
        const std::string path = keyPath(c.Arg(0), "");
        auto k = st->registry.find(path);
        if (k == st->registry.end() || c.Arg(1) >= k->second.size()) {
            c.Return(Win32::kErrorNoMoreItems);
            return;
        }
        auto it = std::next(k->second.begin(), c.Arg(1));
        const uint32_t pcbName = c.Arg(3);
        if (pcbName && c.mem.Read32(pcbName) <= it->first.size()) {
            c.Return(Win32::kErrorMoreData);
            return;
        }
        writeStr(c, c.Arg(2), it->first, pcbName ? c.mem.Read32(pcbName) : static_cast<uint32_t>(it->first.size() + 1), wide);
        if (pcbName) c.mem.Write32(pcbName, static_cast<uint32_t>(it->first.size()));
        if (c.Arg(5)) c.mem.Write32(c.Arg(5), it->second.first);
        const auto& data = it->second.second;
        if (c.Arg(6) && c.Arg(7)) {
            if (c.mem.Read32(c.Arg(7)) >= data.size() && !data.empty()) c.mem.WriteBytes(c.Arg(6), data.data(), data.size());
            c.mem.Write32(c.Arg(7), static_cast<uint32_t>(data.size()));
        } else if (c.Arg(7)) {
            c.mem.Write32(c.Arg(7), static_cast<uint32_t>(data.size()));
        }
        c.Return(0);
    };
    R(kAdv, "RegEnumValueW", 8, [regEnumValue](ShimCall& c) { regEnumValue(c, true); });
    R(kAdv, "RegEnumValueA", 8, [regEnumValue](ShimCall& c) { regEnumValue(c, false); });

    auto regQueryInfo = [st, keyPath, subkeyList](ShimCall& c) {
        const std::string path = keyPath(c.Arg(0), "");
        const auto kids = subkeyList(path);
        auto k = st->registry.find(path);
        const uint32_t values = k == st->registry.end() ? 0 : static_cast<uint32_t>(k->second.size());
        if (c.Arg(5)) c.mem.Write32(c.Arg(5), static_cast<uint32_t>(kids.size()));
        if (c.Arg(6)) c.mem.Write32(c.Arg(6), 255);
        if (c.Arg(7)) c.mem.Write32(c.Arg(7), 0);
        if (c.Arg(8)) c.mem.Write32(c.Arg(8), values);
        if (c.Arg(9)) c.mem.Write32(c.Arg(9), 255);
        if (c.Arg(10)) c.mem.Write32(c.Arg(10), 1024);
        c.Return(0);
    };
    R(kAdv, "RegQueryInfoKeyW", 12, regQueryInfo);
    R(kAdv, "RegQueryInfoKeyA", 12, regQueryInfo);

    R(kAdv, "RegFlushKey", 1, [st](ShimCall& c) {
        st->SaveRegistry();
        c.Return(0);
    });
    R(kAdv, "RegCloseKey", 1, [st](ShimCall& c) {
        st->regKeys.erase(c.Arg(0));
        c.Return(0);
    });
}

}  // namespace NetEmu
