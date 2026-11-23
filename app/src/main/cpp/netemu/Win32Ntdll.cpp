// ntdll exports that netc resolves through GetProcAddress (its anti-tamper
// and path code). Each answers the way a clean, non-debugged Windows 10
// process would: no debugger, no kernel debugger, no foreign drivers, its own
// image file readable and mappable.

#include "NetEmuLog.h"
#include "PeImage.h"
#include "Win32Shims.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <fstream>
#include <iterator>
#include <optional>

namespace NetEmu {

namespace {
constexpr char kNt[] = "ntdll.dll";
constexpr uint32_t STATUS_SUCCESS = 0;
constexpr uint32_t STATUS_NOT_IMPLEMENTED = 0xC0000002u;
constexpr uint32_t STATUS_INVALID_INFO_CLASS = 0xC0000003u;
constexpr uint32_t STATUS_INFO_LENGTH_MISMATCH = 0xC0000004u;
constexpr uint32_t STATUS_INVALID_HANDLE = 0xC0000008u;
constexpr uint32_t STATUS_INVALID_PARAMETER = 0xC000000Du;
constexpr uint32_t STATUS_END_OF_FILE = 0xC0000011u;
constexpr uint32_t STATUS_NO_MEMORY = 0xC0000017u;
constexpr uint32_t STATUS_ACCESS_DENIED = 0xC0000022u;
constexpr uint32_t STATUS_BUFFER_TOO_SMALL = 0xC0000023u;
constexpr uint32_t STATUS_OBJECT_NAME_NOT_FOUND = 0xC0000034u;
constexpr uint32_t STATUS_PORT_NOT_SET = 0xC0000353u;

// UNICODE_STRING { USHORT Length; USHORT MaximumLength; PWSTR Buffer; }
std::string ReadUnicodeString(GuestMemory& mem, uint32_t us) {
    if (!us) return {};
    const uint16_t len = mem.Read16(us);
    return mem.ReadWCount(mem.Read32(us + 4), len / 2);
}

void WriteUnicodeString(GuestMemory& mem, uint32_t us, const std::string& s) {
    const uint32_t buf = mem.NewW(s);
    const uint16_t chars = static_cast<uint16_t>(mem.StrLenW(buf));
    mem.Write16(us, static_cast<uint16_t>(chars * 2));
    mem.Write16(us + 2, static_cast<uint16_t>(chars * 2 + 2));
    mem.Write32(us + 4, buf);
}

// "\??\C:\x", "\DosDevices\C:\x" or "\Device\HarddiskVolume1\x" to "C:\x".
std::string NtToDosPath(const std::string& nt) {
    std::string p = nt;
    for (const char* prefix : {"\\??\\", "\\\\?\\", "\\DosDevices\\", "\\GLOBAL??\\"})
        if (p.rfind(prefix, 0) == 0) return p.substr(std::strlen(prefix));
    const std::string volume = "\\Device\\HarddiskVolume1";
    if (Lower(p).rfind(Lower(volume), 0) == 0) return "C:" + p.substr(volume.size());
    return p;
}

bool ReadHostFile(const std::string& path, std::vector<uint8_t>* out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    out->assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}
}  // namespace

void InstallNtdll(Win32State& s) {
    Machine& m = s.machine;
    Win32State* st = &s;
    auto R = [&m](const char* name, int args, ShimFn fn) { m.RegisterShim(kNt, name, args * 4, std::move(fn)); };
    auto K = [&m](const char* name, int args, ShimFn fn) { m.RegisterShim("kernel32.dll", name, args * 4, std::move(fn)); };

    // --- kernel32 extras resolved at run time ---------------------------------------
    K("IsWow64Process", 2, [](ShimCall& c) {
        c.mem.Write32(c.Arg(1), 0);
        c.Return(1);
    });
    K("GetCurrentProcessorNumber", 0, [](ShimCall& c) { c.Return(0); });
    auto addVeh = [st](ShimCall& c) {
        if (c.Arg(0)) st->vectoredHandlers.insert(st->vectoredHandlers.begin(), c.Arg(1));
        else st->vectoredHandlers.push_back(c.Arg(1));
        c.Return(c.Arg(1));  // the handle is the handler itself
    };
    auto removeVeh = [st](ShimCall& c) {
        auto& v = st->vectoredHandlers;
        auto it = std::find(v.begin(), v.end(), c.Arg(0));
        if (it == v.end()) {
            c.Return(0);
            return;
        }
        v.erase(it);
        c.Return(1);
    };
    K("AddVectoredExceptionHandler", 2, addVeh);
    K("RemoveVectoredExceptionHandler", 1, removeVeh);
    R("RtlAddVectoredExceptionHandler", 2, addVeh);
    R("RtlRemoveVectoredExceptionHandler", 1, removeVeh);

    // --- devices and machine identity ---------------------------------------------------
    K("DeviceIoControl", 8, [st](ShimCall& c) {
        const uint32_t code = c.Arg(1), in = c.Arg(2), out = c.Arg(4), outSize = c.Arg(5), returned = c.Arg(6);
        auto f = st->GetAs<KFile>(c.Arg(0), KObject::Kind::File);
        if (f && f->disk && code == 0x002D1400u && in && c.mem.Read32(in) == 0 && c.mem.Read32(in + 4) == 0) {
            // IOCTL_STORAGE_QUERY_PROPERTY / StorageDeviceProperty: an NVMe disk
            // whose serial comes from the per-install seed.
            char serial[32];
            std::snprintf(serial, sizeof(serial), "SAO%016llX", static_cast<unsigned long long>(st->machineSeed));
            const std::string vendor = "", product = "SAONLINE VIRTUAL DISK", rev = "1.0";
            const uint32_t vendorOff = 40, productOff = vendorOff + 1, revOff = productOff + static_cast<uint32_t>(product.size() + 1),
                           serialOff = revOff + static_cast<uint32_t>(rev.size() + 1);
            const uint32_t total = serialOff + static_cast<uint32_t>(std::strlen(serial) + 1);
            if (outSize < 8) {
                c.SetLastError(Win32::kErrorInsufficientBuffer);
                c.Return(0);
                return;
            }
            c.mem.Fill(out, 0, std::min(outSize, total));
            c.mem.Write32(out, total);  // Version
            c.mem.Write32(out + 4, total);
            if (outSize >= total) {
                c.mem.Write8(out + 8, 0);  // DeviceType: direct access
                c.mem.Write32(out + 12, vendorOff);
                c.mem.Write32(out + 16, productOff);
                c.mem.Write32(out + 20, revOff);
                c.mem.Write32(out + 24, serialOff);
                c.mem.Write32(out + 28, 17);  // BusTypeNvme
                c.mem.WriteA(out + productOff, product, product.size() + 1);
                c.mem.WriteA(out + revOff, rev, rev.size() + 1);
                c.mem.WriteA(out + serialOff, serial, sizeof(serial));
            }
            if (returned) c.mem.Write32(returned, std::min(outSize, total));
            c.Return(1);
            return;
        }
        char label[64];
        std::snprintf(label, sizeof(label), "DeviceIoControl 0x%08X", code);
        st->NoteStub(label);
        if (returned) c.mem.Write32(returned, 0);
        c.SetLastError(1);  // ERROR_INVALID_FUNCTION
        c.Return(0);
    });
    auto volumeInfo = [st](ShimCall& c, bool wide) {
        if (c.Arg(1) && c.Arg(2)) wide ? c.mem.WriteW(c.Arg(1), "System", c.Arg(2)) : c.mem.WriteA(c.Arg(1), "System", c.Arg(2));
        if (c.Arg(3)) c.mem.Write32(c.Arg(3), static_cast<uint32_t>(st->machineSeed));
        if (c.Arg(4)) c.mem.Write32(c.Arg(4), 255);
        if (c.Arg(5)) c.mem.Write32(c.Arg(5), 0x03E700FFu);
        if (c.Arg(6) && c.Arg(7)) wide ? c.mem.WriteW(c.Arg(6), "NTFS", c.Arg(7)) : c.mem.WriteA(c.Arg(6), "NTFS", c.Arg(7));
        c.Return(1);
    };
    K("GetVolumeInformationW", 8, [volumeInfo](ShimCall& c) { volumeInfo(c, true); });
    K("GetVolumeInformationA", 8, [volumeInfo](ShimCall& c) { volumeInfo(c, false); });
    auto putName = [](ShimCall& c, const std::string& v, bool wide) {
        const uint32_t cap = c.mem.Read32(c.Arg(1));
        if (cap <= v.size()) {
            c.mem.Write32(c.Arg(1), static_cast<uint32_t>(v.size() + 1));
            c.SetLastError(Win32::kErrorInsufficientBuffer);
            c.Return(0);
            return false;
        }
        wide ? c.mem.WriteW(c.Arg(0), v, cap) : c.mem.WriteA(c.Arg(0), v, cap);
        return true;
    };
    K("GetComputerNameW", 2, [st, putName](ShimCall& c) {
        if (putName(c, st->env.computerName, true)) {
            c.mem.Write32(c.Arg(1), static_cast<uint32_t>(st->env.computerName.size()));
            c.Return(1);
        }
    });
    K("GetComputerNameA", 2, [st, putName](ShimCall& c) {
        if (putName(c, st->env.computerName, false)) {
            c.mem.Write32(c.Arg(1), static_cast<uint32_t>(st->env.computerName.size()));
            c.Return(1);
        }
    });
    m.RegisterShim("advapi32.dll", "GetUserNameW", 8, [st, putName](ShimCall& c) {
        if (putName(c, st->env.userName, true)) {
            c.mem.Write32(c.Arg(1), static_cast<uint32_t>(st->env.userName.size() + 1));
            c.Return(1);
        }
    });
    m.RegisterShim("advapi32.dll", "GetUserNameA", 8, [st, putName](ShimCall& c) {
        if (putName(c, st->env.userName, false)) {
            c.mem.Write32(c.Arg(1), static_cast<uint32_t>(st->env.userName.size() + 1));
            c.Return(1);
        }
    });
    // Volume enumeration for hardware fingerprinting: one fixed NTFS volume on
    // C:, its GUID name derived from the per-install seed.
    auto volumeGuid = [st]() {
        char g[64];
        const uint64_t s = st->machineSeed;
        std::snprintf(g, sizeof(g), "\\\\?\\Volume{%08x-%04x-%04x-%04x-%012llx}\\", static_cast<uint32_t>(s),
                      static_cast<uint16_t>(s >> 32), static_cast<uint16_t>(s >> 48), static_cast<uint16_t>(s >> 16),
                      static_cast<unsigned long long>(s & 0xFFFFFFFFFFFFull));
        return std::string(g);
    };
    K("FindFirstVolumeW", 2, [st, volumeGuid](ShimCall& c) {
        c.mem.WriteW(c.Arg(0), volumeGuid(), c.Arg(1));
        auto h = std::make_shared<KGeneric>();
        h->name = "<volume-enum>";
        c.Return(st->Add(h));
    });
    K("FindNextVolumeW", 3, [](ShimCall& c) {
        c.SetLastError(Win32::kErrorNoMoreFiles);
        c.Return(0);
    });
    K("FindVolumeClose", 1, [st](ShimCall& c) { c.Return(st->Close(c.Arg(0)) ? 1 : 0); });
    K("GetVolumeNameForVolumeMountPointW", 3, [volumeGuid](ShimCall& c) {
        c.mem.WriteW(c.Arg(1), volumeGuid(), c.Arg(2));
        c.Return(1);
    });
    K("GetVolumePathNamesForVolumeNameW", 4, [](ShimCall& c) {
        // Multi-sz: "C:\" then an extra terminator.
        c.mem.WriteW(c.Arg(1), "C:\\", c.Arg(2));
        c.mem.Write16(c.Arg(1) + 8, 0);
        if (c.Arg(3)) c.mem.Write32(c.Arg(3), 5);
        c.Return(1);
    });
    K("QueryDosDeviceW", 3, [](ShimCall& c) {
        const std::string target = "\\Device\\HarddiskVolume1";
        if (c.Arg(2) < target.size() + 2) {
            c.SetLastError(Win32::kErrorInsufficientBuffer);
            c.Return(0);
            return;
        }
        c.mem.WriteW(c.Arg(1), target, c.Arg(2));
        c.mem.Write16(c.Arg(1) + static_cast<uint32_t>(target.size() + 1) * 2, 0);
        c.Return(static_cast<uint32_t>(target.size() + 2));
    });
    K("GetLogicalDriveStringsW", 2, [](ShimCall& c) {
        if (c.Arg(0) < 5) {
            c.Return(5);
            return;
        }
        c.mem.WriteW(c.Arg(1), "C:\\", c.Arg(0));
        c.mem.Write16(c.Arg(1) + 8, 0);
        c.Return(4);
    });
    K("GetDiskFreeSpaceExW", 4, [](ShimCall& c) {
        for (int i = 1; i <= 3; ++i)
            if (c.Arg(i)) c.mem.Write64(c.Arg(i), 64ull << 30);
        c.Return(1);
    });

    // --- toolhelp snapshots -------------------------------------------------------------
    // A clean process tree: explorer.exe started gta_sa.exe, which has the
    // game executable, netc.dll and the system DLLs loaded, and its threads.
    struct Snapshot : KObject {
        Snapshot() : KObject(Kind::Generic) {}
        struct Proc {
            uint32_t pid, parent, threads;
            std::string exe;
        };
        struct Mod {
            uint32_t base, size;
            std::string name, path;
        };
        std::vector<Proc> procs;
        std::vector<Mod> mods;
        std::vector<uint32_t> tids;
        size_t procAt = 0, modAt = 0, threadAt = 0;
    };
    K("CreateToolhelp32Snapshot", 2, [st](ShimCall& c) {
        auto snap = std::make_shared<Snapshot>();
        snap->name = "<toolhelp>";
        uint32_t threads = 0;
        for (const auto& t : st->machine.Threads())
            if (t->state != GuestThread::State::Exited) {
                snap->tids.push_back(t->id);
                ++threads;
            }
        snap->procs.push_back({0, 0, 1, "[System Process]"});
        snap->procs.push_back({4, 0, 1, "System"});
        snap->procs.push_back({0x0FA0, 0x0F00, 1, "explorer.exe"});
        snap->procs.push_back({0x1000, 0x0FA0, threads, "gta_sa.exe"});
        GuestMemory& mem = st->machine.Mem();
        uint32_t b = 0, size = 0;
        snap->mods.push_back({st->exeBase, 0x10000, "gta_sa.exe", st->env.gtaDir + "\\gta_sa.exe"});
        if (mem.QueryPages(st->machine.MainModule(), &b, &size))
            snap->mods.push_back({st->machine.MainModule(), size, "netc.dll", st->env.moduleGuestPath});
        for (const auto& [name, base] : st->modules)
            if (name.rfind("api-ms-", 0) != 0) snap->mods.push_back({base, 0x1000, name, "C:\\Windows\\SysWOW64\\" + name});
        c.Return(st->Add(snap));
    });
    auto snapOf = [st](uint32_t h) {
        auto o = st->Get(h);
        return o && o->name == "<toolhelp>" ? std::static_pointer_cast<Snapshot>(o) : nullptr;
    };
    // PROCESSENTRY32(W): size, usage, pid, heap, module, threads, parent, priority, flags, exe[260].
    auto procEntry = [snapOf](ShimCall& c, bool first, bool wide) {
        auto snap = snapOf(c.Arg(0));
        if (!snap) {
            c.SetLastError(Win32::kErrorInvalidHandle);
            c.Return(0);
            return;
        }
        if (first) snap->procAt = 0;
        if (snap->procAt >= snap->procs.size()) {
            c.SetLastError(Win32::kErrorNoMoreFiles);
            c.Return(0);
            return;
        }
        const auto& p = snap->procs[snap->procAt++];
        const uint32_t e = c.Arg(1);
        c.mem.Fill(e + 4, 0, (wide ? 556 : 296) - 4);
        c.mem.Write32(e + 8, p.pid);
        c.mem.Write32(e + 20, p.threads);
        c.mem.Write32(e + 24, p.parent);
        c.mem.Write32(e + 28, 8);
        if (wide) c.mem.WriteW(e + 36, p.exe, 260);
        else c.mem.WriteA(e + 36, p.exe, 260);
        c.Return(1);
    };
    K("Process32FirstW", 2, [procEntry](ShimCall& c) { procEntry(c, true, true); });
    K("Process32NextW", 2, [procEntry](ShimCall& c) { procEntry(c, false, true); });
    K("Process32First", 2, [procEntry](ShimCall& c) { procEntry(c, true, false); });
    K("Process32Next", 2, [procEntry](ShimCall& c) { procEntry(c, false, false); });
    // MODULEENTRY32(W): size, module id, pid, usage x2, base, size, hModule, name[256], path[260].
    auto modEntry = [snapOf](ShimCall& c, bool first, bool wide) {
        auto snap = snapOf(c.Arg(0));
        if (!snap) {
            c.SetLastError(Win32::kErrorInvalidHandle);
            c.Return(0);
            return;
        }
        if (first) snap->modAt = 0;
        if (snap->modAt >= snap->mods.size()) {
            c.SetLastError(Win32::kErrorNoMoreFiles);
            c.Return(0);
            return;
        }
        const auto& md = snap->mods[snap->modAt++];
        const uint32_t e = c.Arg(1);
        c.mem.Fill(e + 4, 0, (wide ? 1064 : 548) - 4);
        c.mem.Write32(e + 4, 1);
        c.mem.Write32(e + 8, 0x1000);
        c.mem.Write32(e + 12, 0xFFFF);
        c.mem.Write32(e + 16, 0xFFFF);
        c.mem.Write32(e + 20, md.base);
        c.mem.Write32(e + 24, md.size);
        c.mem.Write32(e + 28, md.base);
        if (wide) {
            c.mem.WriteW(e + 32, md.name, 256);
            c.mem.WriteW(e + 32 + 512, md.path, 260);
        } else {
            c.mem.WriteA(e + 32, md.name, 256);
            c.mem.WriteA(e + 32 + 256, md.path, 260);
        }
        c.Return(1);
    };
    K("Module32FirstW", 2, [modEntry](ShimCall& c) { modEntry(c, true, true); });
    K("Module32NextW", 2, [modEntry](ShimCall& c) { modEntry(c, false, true); });
    K("Module32First", 2, [modEntry](ShimCall& c) { modEntry(c, true, false); });
    K("Module32Next", 2, [modEntry](ShimCall& c) { modEntry(c, false, false); });
    // THREADENTRY32: size, usage, tid, owner pid, base priority, delta, flags.
    auto threadEntry = [snapOf](ShimCall& c, bool first) {
        auto snap = snapOf(c.Arg(0));
        if (!snap) {
            c.SetLastError(Win32::kErrorInvalidHandle);
            c.Return(0);
            return;
        }
        if (first) snap->threadAt = 0;
        if (snap->threadAt >= snap->tids.size()) {
            c.SetLastError(Win32::kErrorNoMoreFiles);
            c.Return(0);
            return;
        }
        const uint32_t e = c.Arg(1);
        c.mem.Fill(e + 4, 0, 24);
        c.mem.Write32(e + 8, snap->tids[snap->threadAt++]);
        c.mem.Write32(e + 12, 0x1000);
        c.mem.Write32(e + 16, 8);
        c.Return(1);
    };
    K("Thread32First", 2, [threadEntry](ShimCall& c) { threadEntry(c, true); });
    K("Thread32Next", 2, [threadEntry](ShimCall& c) { threadEntry(c, false); });

    // --- psapi (kernel32 K32* and psapi.dll) ----------------------------------------------
    struct ModInfo {
        uint32_t base, size, entry;
        std::string name, path;
    };
    auto moduleList = [st]() {
        std::vector<ModInfo> out;
        GuestMemory& mem = st->machine.Mem();
        uint32_t b = 0, size = 0;
        out.push_back({st->exeBase, 0x10000, 0, "gta_sa.exe", st->env.gtaDir + "\\gta_sa.exe"});
        const uint32_t netc = st->machine.MainModule();
        if (netc && mem.QueryPages(netc, &b, &size)) {
            const uint32_t pe = netc + mem.Read32(netc + 0x3C);
            out.push_back({netc, size, netc + mem.Read32(pe + 0x28), "netc.dll", st->env.moduleGuestPath});
        }
        for (const auto& [name, base] : st->modules)
            if (name.rfind("api-ms-", 0) != 0) out.push_back({base, 0x1000, 0, name, "C:\\Windows\\SysWOW64\\" + name});
        return out;
    };
    auto findModule = [moduleList](uint32_t h) -> std::optional<ModInfo> {
        for (const auto& mi : moduleList())
            if (mi.base == h || (!h && mi.name == "gta_sa.exe")) return mi;
        return std::nullopt;
    };
    auto P2 = [&m](const char* name, int args, ShimFn fn) {
        m.RegisterShim("kernel32.dll", std::string("K32") + name, args * 4, fn);
        m.RegisterShim("psapi.dll", name, args * 4, std::move(fn));
    };
    auto enumModules = [moduleList](ShimCall& c) {
        const auto mods = moduleList();
        const uint32_t arr = c.Arg(1), cb = c.Arg(2);
        for (uint32_t i = 0; i < mods.size() && (i + 1) * 4 <= cb; ++i) c.mem.Write32(arr + i * 4, mods[i].base);
        c.mem.Write32(c.Arg(3), static_cast<uint32_t>(mods.size() * 4));
        c.Return(1);
    };
    P2("EnumProcessModules", 4, enumModules);
    P2("EnumProcessModulesEx", 5, enumModules);
    P2("GetModuleInformation", 4, [findModule](ShimCall& c) {
        auto mi = findModule(c.Arg(1));
        if (!mi || c.Arg(3) < 12) {
            c.SetLastError(Win32::kErrorInvalidHandle);
            c.Return(0);
            return;
        }
        c.mem.Write32(c.Arg(2), mi->base);
        c.mem.Write32(c.Arg(2) + 4, mi->size);
        c.mem.Write32(c.Arg(2) + 8, mi->entry);
        c.Return(1);
    });
    auto moduleString = [findModule](ShimCall& c, bool path, bool wide) {
        auto mi = findModule(c.Arg(1));
        if (!mi) {
            c.SetLastError(Win32::kErrorInvalidHandle);
            c.Return(0);
            return;
        }
        const std::string& v = path ? mi->path : mi->name;
        c.Return(static_cast<uint32_t>(wide ? c.mem.WriteW(c.Arg(2), v, c.Arg(3)) : c.mem.WriteA(c.Arg(2), v, c.Arg(3))));
    };
    P2("GetModuleFileNameExW", 4, [moduleString](ShimCall& c) { moduleString(c, true, true); });
    P2("GetModuleFileNameExA", 4, [moduleString](ShimCall& c) { moduleString(c, true, false); });
    P2("GetModuleBaseNameW", 4, [moduleString](ShimCall& c) { moduleString(c, false, true); });
    P2("GetModuleBaseNameA", 4, [moduleString](ShimCall& c) { moduleString(c, false, false); });
    P2("GetMappedFileNameW", 4, [moduleList](ShimCall& c) {
        for (const auto& mi : moduleList())
            if (c.Arg(1) >= mi.base && c.Arg(1) - mi.base < mi.size) {
                const std::string dev = "\\Device\\HarddiskVolume1" + mi.path.substr(2);
                c.Return(static_cast<uint32_t>(c.mem.WriteW(c.Arg(2), dev, c.Arg(3))));
                return;
            }
        c.SetLastError(Win32::kErrorInvalidParameter);
        c.Return(0);
    });
    P2("EnumProcesses", 3, [](ShimCall& c) {
        const uint32_t pids[] = {0, 4, 0x0FA0, 0x1000};
        uint32_t n = 0;
        for (uint32_t pid : pids)
            if ((n + 1) * 4 <= c.Arg(1)) c.mem.Write32(c.Arg(0) + 4 * n++, pid);
        c.mem.Write32(c.Arg(2), n * 4);
        c.Return(1);
    });
    P2("GetProcessImageFileNameW", 3, [st](ShimCall& c) {
        const std::string dev = "\\Device\\HarddiskVolume1" + st->env.gtaDir.substr(2) + "\\gta_sa.exe";
        c.Return(static_cast<uint32_t>(c.mem.WriteW(c.Arg(1), dev, c.Arg(2))));
    });
    P2("GetProcessMemoryInfo", 3, [](ShimCall& c) {
        c.mem.Fill(c.Arg(1) + 4, 0, c.Arg(2) > 4 ? c.Arg(2) - 4 : 0);
        c.mem.Write32(c.Arg(1), c.Arg(2));
        c.Return(1);
    });
    K("QueryFullProcessImageNameW", 4, [st](ShimCall& c) {
        const std::string exe = st->env.gtaDir + "\\gta_sa.exe";
        const uint32_t cap = c.mem.Read32(c.Arg(3));
        if (cap <= exe.size()) {
            c.SetLastError(Win32::kErrorInsufficientBuffer);
            c.Return(0);
            return;
        }
        c.mem.Write32(c.Arg(3), static_cast<uint32_t>(c.mem.WriteW(c.Arg(2), exe, cap)));
        c.Return(1);
    });

    // --- strings and versions --------------------------------------------------------
    R("RtlInitUnicodeString", 2, [](ShimCall& c) {
        const uint32_t us = c.Arg(0), src = c.Arg(1);
        const uint32_t chars = src ? static_cast<uint32_t>(c.mem.StrLenW(src)) : 0;
        c.mem.Write16(us, static_cast<uint16_t>(chars * 2));
        c.mem.Write16(us + 2, static_cast<uint16_t>(src ? chars * 2 + 2 : 0));
        c.mem.Write32(us + 4, src);
        c.Return(0);
    });
    R("RtlInitAnsiString", 2, [](ShimCall& c) {
        const uint32_t as = c.Arg(0), src = c.Arg(1);
        const uint32_t len = src ? static_cast<uint32_t>(c.mem.ReadA(src, 65534).size()) : 0;
        c.mem.Write16(as, static_cast<uint16_t>(len));
        c.mem.Write16(as + 2, static_cast<uint16_t>(src ? len + 1 : 0));
        c.mem.Write32(as + 4, src);
        c.Return(0);
    });
    R("RtlAnsiStringToUnicodeString", 3, [](ShimCall& c) {
        const uint32_t dst = c.Arg(0), src = c.Arg(1);
        const std::string a = c.mem.ReadA(c.mem.Read32(src + 4), c.mem.Read16(src), true);
        if (c.Arg(2)) {
            WriteUnicodeString(c.mem, dst, a);
        } else {
            const uint16_t cap = c.mem.Read16(dst + 2);
            if (cap < a.size() * 2) {
                c.Return(0x80000005u);  // STATUS_BUFFER_OVERFLOW
                return;
            }
            c.mem.WriteW(c.mem.Read32(dst + 4), a, cap / 2);
            c.mem.Write16(dst, static_cast<uint16_t>(a.size() * 2));
        }
        c.Return(STATUS_SUCCESS);
    });
    R("RtlFreeUnicodeString", 1, [](ShimCall& c) {
        c.mem.HeapFree(c.mem.Read32(c.Arg(0) + 4));
        c.mem.Fill(c.Arg(0), 0, 8);
        c.Return(0);
    });
    R("RtlDosPathNameToNtPathName_U", 4, [st](ShimCall& c) {
        std::string dos = c.mem.ReadW(c.Arg(0), 1024);
        std::replace(dos.begin(), dos.end(), '/', '\\');
        if (dos.size() < 2 || dos[1] != ':') dos = st->CurrentDirectory + "\\" + dos;
        WriteUnicodeString(c.mem, c.Arg(1), "\\??\\" + dos);
        if (c.Arg(2)) {
            const uint32_t buf = c.mem.Read32(c.Arg(1) + 4);
            const size_t slash = dos.find_last_of('\\');
            c.mem.Write32(c.Arg(2), slash == std::string::npos ? 0 : buf + static_cast<uint32_t>(4 + slash + 1) * 2);
        }
        if (c.Arg(3)) c.mem.Fill(c.Arg(3), 0, 16);
        c.Return(1);
    });
    R("RtlCompareMemory", 3, [](ShimCall& c) {
        const uint8_t* a = c.mem.Host(c.Arg(0), c.Arg(2) ? c.Arg(2) : 1);
        const uint8_t* b = c.mem.Host(c.Arg(1), c.Arg(2) ? c.Arg(2) : 1);
        uint32_t n = 0;
        if (a && b)
            while (n < c.Arg(2) && a[n] == b[n]) ++n;
        c.Return(n);
    });
    R("RtlGetVersion", 1, [](ShimCall& c) {
        const uint32_t p = c.Arg(0);
        const uint32_t size = c.mem.Read32(p);
        c.mem.Write32(p + 4, 10);
        c.mem.Write32(p + 8, 0);
        c.mem.Write32(p + 12, 19045);
        c.mem.Write32(p + 16, 2);
        c.mem.Fill(p + 20, 0, 256);
        if (size >= 284) {  // OSVERSIONINFOEXW
            c.mem.Write16(p + 276, 0);
            c.mem.Write16(p + 278, 0);
            c.mem.Write16(p + 280, 0x100);  // VER_SUITE_SINGLEUSERTS
            c.mem.Write8(p + 282, 1);       // VER_NT_WORKSTATION
        }
        c.Return(STATUS_SUCCESS);
    });
    R("RtlVerifyVersionInfo", 4, [](ShimCall& c) { c.Return(STATUS_SUCCESS); });
    for (const char* n : {"RtlEncodePointer", "RtlDecodePointer", "RtlEncodeSystemPointer", "RtlDecodeSystemPointer"})
        R(n, 1, [](ShimCall& c) { c.Return(c.Arg(0)); });
    R("RtlQueryModuleInformation", 3, [](ShimCall& c) { c.Return(STATUS_NOT_IMPLEMENTED); });
    auto statusToDos = [](uint32_t status) -> uint32_t {
        switch (status) {
        case STATUS_SUCCESS: return 0;
        case STATUS_NOT_IMPLEMENTED:
        case STATUS_INVALID_INFO_CLASS: return Win32::kErrorNotSupported;
        case STATUS_INFO_LENGTH_MISMATCH:
        case STATUS_BUFFER_TOO_SMALL: return Win32::kErrorInsufficientBuffer;
        case STATUS_INVALID_HANDLE: return Win32::kErrorInvalidHandle;
        case STATUS_INVALID_PARAMETER: return Win32::kErrorInvalidParameter;
        case STATUS_NO_MEMORY: return Win32::kErrorNotEnoughMemory;
        case STATUS_ACCESS_DENIED: return Win32::kErrorAccessDenied;
        case STATUS_OBJECT_NAME_NOT_FOUND: return Win32::kErrorFileNotFound;
        case STATUS_END_OF_FILE: return 38;  // ERROR_HANDLE_EOF
        case STATUS_PORT_NOT_SET: return Win32::kErrorNotSupported;
        default: return (status & 0xC0000000u) == 0xC0000000u ? Win32::kErrorInvalidParameter : 0;
        }
    };
    R("RtlNtStatusToDosError", 1, [statusToDos](ShimCall& c) { c.Return(statusToDos(c.Arg(0))); });
    m.RegisterShim("kernel32.dll", "BaseSetLastNTError", 4, [statusToDos](ShimCall& c) {
        c.SetLastError(statusToDos(c.Arg(0)));
        c.Return(0);
    });
    // Present so netc can take their addresses; the guest never calls them.
    for (const char* n : {"RtlUserThreadStart", "KiUserExceptionDispatcher", "LdrLoadDll", "LdrGetProcedureAddress"})
        R(n, 0, [n](ShimCall& c) {
            NETEMU_LOGE("netc called ntdll!%s directly", n);
            c.Exit(0xC0000005u);
        });

    // --- process and thread queries --------------------------------------------------------
    R("NtQueryInformationProcess", 5, [st](ShimCall& c) {
        const uint32_t cls = c.Arg(1), buf = c.Arg(2), len = c.Arg(3), ret = c.Arg(4);
        auto put = [&](uint32_t need) {
            if (ret) c.mem.Write32(ret, need);
            return len >= need;
        };
        switch (cls) {
        case 0:  // ProcessBasicInformation
            if (!put(24)) break;
            c.mem.Fill(buf, 0, 24);
            c.mem.Write32(buf, 259);
            c.mem.Write32(buf + 4, st->machine.Peb());
            c.mem.Write32(buf + 8, 0xF);
            c.mem.Write32(buf + 12, 8);
            c.mem.Write32(buf + 16, 0x1000);
            c.mem.Write32(buf + 20, 0x0FA0);
            c.Return(STATUS_SUCCESS);
            return;
        case 7:  // ProcessDebugPort
            if (!put(4)) break;
            c.mem.Write32(buf, 0);
            c.Return(STATUS_SUCCESS);
            return;
        case 0x1E:  // ProcessDebugObjectHandle
            if (len >= 4) c.mem.Write32(buf, 0);
            c.Return(STATUS_PORT_NOT_SET);
            return;
        case 0x1F:  // ProcessDebugFlags: 1 = NoDebugInherit, not debugged
            if (!put(4)) break;
            c.mem.Write32(buf, 1);
            c.Return(STATUS_SUCCESS);
            return;
        case 26:  // ProcessWow64Information
            if (!put(4)) break;
            c.mem.Write32(buf, 0);
            c.Return(STATUS_SUCCESS);
            return;
        case 27:    // ProcessImageFileName
        case 43: {  // ProcessImageFileNameWin32
            const std::string exe = cls == 27 ? "\\Device\\HarddiskVolume1" + st->env.gtaDir.substr(2) + "\\gta_sa.exe"
                                              : st->env.gtaDir + "\\gta_sa.exe";
            const uint32_t need = 8 + static_cast<uint32_t>(exe.size() + 1) * 2;
            if (!put(need)) break;
            c.mem.Write16(buf, static_cast<uint16_t>(exe.size() * 2));
            c.mem.Write16(buf + 2, static_cast<uint16_t>(exe.size() * 2 + 2));
            c.mem.Write32(buf + 4, buf + 8);
            c.mem.WriteW(buf + 8, exe, exe.size() + 1);
            c.Return(STATUS_SUCCESS);
            return;
        }
        default:
            st->NoteStub("NtQueryInformationProcess class " + std::to_string(cls));
            c.Return(STATUS_INVALID_INFO_CLASS);
            return;
        }
        c.Return(STATUS_INFO_LENGTH_MISMATCH);
    });
    // Low-level IOCTLs netc tries for SMART/disk identity: none are supported,
    // so it uses the volume serial and MAC it already collected.
    R("NtDeviceIoControlFile", 10, [st](ShimCall& c) {
        if (c.Arg(4)) {
            c.mem.Write32(c.Arg(4), STATUS_NOT_IMPLEMENTED);
            c.mem.Write32(c.Arg(4) + 4, 0);
        }
        c.Return(STATUS_NOT_IMPLEMENTED);
    });
    R("NtFsControlFile", 10, [](ShimCall& c) { c.Return(STATUS_NOT_IMPLEMENTED); });
    R("NtSetInformationProcess", 4, [](ShimCall& c) { c.Return(STATUS_SUCCESS); });
    R("NtSetInformationThread", 4, [](ShimCall& c) { c.Return(STATUS_SUCCESS); });
    auto threadFor = [st](ShimCall& c, uint32_t h) -> GuestThread* {
        if (h == Win32::kCurrentThread) return &c.thread;
        auto k = st->GetAs<KThread>(h, KObject::Kind::Thread);
        return k ? st->machine.ThreadById(k->tid) : nullptr;
    };
    R("NtQueryInformationThread", 5, [st, threadFor](ShimCall& c) {
        GuestThread* t = threadFor(c, c.Arg(0));
        const uint32_t cls = c.Arg(1), buf = c.Arg(2), len = c.Arg(3), ret = c.Arg(4);
        if (!t) {
            c.Return(STATUS_INVALID_HANDLE);
            return;
        }
        if (cls == 0 && len >= 28) {  // ThreadBasicInformation
            c.mem.Fill(buf, 0, 28);
            c.mem.Write32(buf, t->state == GuestThread::State::Exited ? t->exitCode : 259);
            c.mem.Write32(buf + 4, t->teb);
            c.mem.Write32(buf + 8, 0x1000);
            c.mem.Write32(buf + 12, t->id);
            c.mem.Write32(buf + 16, 0xF);
            c.mem.Write32(buf + 20, 8);
            c.mem.Write32(buf + 24, 8);
            if (ret) c.mem.Write32(ret, 28);
            c.Return(STATUS_SUCCESS);
        } else if (cls == 9 && len >= 4) {  // ThreadQuerySetWin32StartAddress
            c.mem.Write32(buf, st->machine.MainModule());
            if (ret) c.mem.Write32(ret, 4);
            c.Return(STATUS_SUCCESS);
        } else if (cls == 0x11 && len >= 1) {  // ThreadHideFromDebugger
            c.mem.Write8(buf, 0);
            if (ret) c.mem.Write32(ret, 1);
            c.Return(STATUS_SUCCESS);
        } else {
            st->NoteStub("NtQueryInformationThread class " + std::to_string(cls));
            c.Return(STATUS_INVALID_INFO_CLASS);
        }
    });
    R("NtOpenThread", 4, [st](ShimCall& c) {
        const uint32_t tid = c.Arg(3) ? c.mem.Read32(c.Arg(3) + 4) : 0;
        GuestThread* t = st->machine.ThreadById(tid);
        if (!t) {
            c.Return(STATUS_INVALID_PARAMETER);
            return;
        }
        auto k = std::make_shared<KThread>();
        k->tid = tid;
        c.mem.Write32(c.Arg(0), st->Add(k));
        c.Return(STATUS_SUCCESS);
    });
    R("NtGetContextThread", 2, [](ShimCall& c) {
        for (uint32_t off = 4; off <= 0x18; off += 4) c.mem.Write32(c.Arg(1) + off, 0);
        c.Return(STATUS_SUCCESS);
    });
    R("NtSetContextThread", 2, [](ShimCall& c) { c.Return(STATUS_SUCCESS); });
    R("NtClose", 1, [st](ShimCall& c) { c.Return(st->Close(c.Arg(0)) ? STATUS_SUCCESS : STATUS_INVALID_HANDLE); });
    R("NtTerminateProcess", 2, [st](ShimCall& c) {
        if (c.Arg(0) == 0 || c.Arg(0) == Win32::kCurrentProcess) {
            NETEMU_LOGE("guest called NtTerminateProcess(self, 0x%X) from 0x%08X", c.Arg(1), c.ReturnAddress());
            st->processExitRequested = true;
            st->processExitCode = c.Arg(1);
            c.Exit(c.Arg(1));
            return;
        }
        c.Return(STATUS_SUCCESS);
    });

    // --- virtual memory ------------------------------------------------------------------------
    R("NtAllocateVirtualMemory", 6, [](ShimCall& c) {
        const uint32_t pBase = c.Arg(1), pSize = c.Arg(3);
        const uint32_t want = c.mem.Read32(pBase), size = c.mem.Read32(pSize);
        uint32_t base = 0, len = 0;
        uint32_t p = 0;
        if (want && c.mem.QueryPages(want, &base, &len)) p = want & ~(Map::kPage - 1);
        else p = want ? c.mem.AllocPagesAt(want & ~0xFFFFu, size + (want & 0xFFFFu)) : c.mem.AllocPages(size);
        if (!p) {
            c.Return(STATUS_NO_MEMORY);
            return;
        }
        c.mem.Write32(pBase, want ? (want & ~(Map::kPage - 1)) : p);
        c.mem.Write32(pSize, (size + Map::kPage - 1) & ~(Map::kPage - 1));
        c.Return(STATUS_SUCCESS);
    });
    R("NtFreeVirtualMemory", 4, [](ShimCall& c) {
        if (c.Arg(3) & 0x8000) c.mem.FreePages(c.mem.Read32(c.Arg(1)));
        c.Return(STATUS_SUCCESS);
    });
    R("NtProtectVirtualMemory", 5, [st](ShimCall& c) {
        if (c.Arg(4)) c.mem.Write32(c.Arg(4), 0x40);
        const uint32_t base = c.mem.Read32(c.Arg(1));
        st->machine.FlushCode(base, base + c.mem.Read32(c.Arg(2)));
        c.Return(STATUS_SUCCESS);
    });
    R("NtQueryVirtualMemory", 6, [st](ShimCall& c) {
        const uint32_t addr = c.Arg(1), cls = c.Arg(2), buf = c.Arg(3), len = c.Arg(4), ret = c.Arg(5);
        uint32_t base = 0, size = 0;
        const bool used = c.mem.QueryPages(addr, &base, &size);
        if (cls == 0) {  // MemoryBasicInformation
            if (len < 28) {
                c.Return(STATUS_INFO_LENGTH_MISMATCH);
                return;
            }
            const uint32_t page = addr & ~(Map::kPage - 1);
            c.mem.Fill(buf, 0, 28);
            c.mem.Write32(buf, page);
            if (used) {
                c.mem.Write32(buf + 4, base);
                c.mem.Write32(buf + 8, 0x40);
                c.mem.Write32(buf + 12, base + size - page);
                c.mem.Write32(buf + 16, 0x1000);
                c.mem.Write32(buf + 20, 0x40);
                c.mem.Write32(buf + 24, base == st->machine.MainModule() ? 0x1000000u : 0x20000u);
            } else {
                c.mem.Write32(buf + 12, Map::kPage);
                c.mem.Write32(buf + 16, 0x10000);
                c.mem.Write32(buf + 20, 1);
            }
            if (ret) c.mem.Write32(ret, 28);
            c.Return(STATUS_SUCCESS);
            return;
        }
        if (cls == 2) {  // MemoryMappedFilenameInformation
            if (!used || base != st->machine.MainModule()) {
                c.Return(STATUS_INVALID_PARAMETER);
                return;
            }
            const std::string name = "\\Device\\HarddiskVolume1" + st->env.moduleGuestPath.substr(2);
            const uint32_t need = 8 + static_cast<uint32_t>(name.size() + 1) * 2;
            if (ret) c.mem.Write32(ret, need);
            if (len < need) {
                c.Return(0x80000005u);
                return;
            }
            c.mem.Write16(buf, static_cast<uint16_t>(name.size() * 2));
            c.mem.Write16(buf + 2, static_cast<uint16_t>(name.size() * 2 + 2));
            c.mem.Write32(buf + 4, buf + 8);
            c.mem.WriteW(buf + 8, name, name.size() + 1);
            c.Return(STATUS_SUCCESS);
            return;
        }
        st->NoteStub("NtQueryVirtualMemory class " + std::to_string(cls));
        c.Return(STATUS_INVALID_INFO_CLASS);
    });

    // --- native files ------------------------------------------------------------------------------
    // OBJECT_ATTRIBUTES { Length; RootDirectory; ObjectName*; Attributes; ... }.
    auto ntOpen = [st](ShimCall& c, uint32_t outHandle, uint32_t oa, uint32_t iosb, bool create, uint32_t disposition) {
        const std::string dos = NtToDosPath(ReadUnicodeString(c.mem, c.mem.Read32(oa + 8)));
        const std::string host = st->HostPath(dos);
        int flags = O_RDONLY | O_CLOEXEC;
        if (create && disposition != 1) flags = O_RDWR | O_CREAT | O_CLOEXEC;  // anything but FILE_OPEN
        int fd = ::open(host.c_str(), flags, 0600);
        if (fd < 0 && errno == EISDIR) fd = ::open(host.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        NETEMU_LOGI("NtOpenFile %s -> %s", dos.c_str(), fd >= 0 ? "ok" : "not found");
        if (fd < 0) {
            c.Return(errno == ENOENT ? STATUS_OBJECT_NAME_NOT_FOUND : STATUS_ACCESS_DENIED);
            return;
        }
        auto f = std::make_shared<KFile>();
        f->fd = fd;
        f->hostPath = host;
        f->name = dos;
        c.mem.Write32(outHandle, st->Add(f));
        if (iosb) {
            c.mem.Write32(iosb, 0);
            c.mem.Write32(iosb + 4, 1);  // FILE_OPENED
        }
        c.Return(STATUS_SUCCESS);
    };
    R("NtOpenFile", 6, [ntOpen](ShimCall& c) { ntOpen(c, c.Arg(0), c.Arg(2), c.Arg(3), false, 1); });
    R("NtCreateFile", 11, [ntOpen](ShimCall& c) { ntOpen(c, c.Arg(0), c.Arg(2), c.Arg(3), true, c.Arg(7)); });
    R("NtReadFile", 9, [st](ShimCall& c) {
        auto f = st->GetAs<KFile>(c.Arg(0), KObject::Kind::File);
        if (!f || f->console) {
            c.Return(STATUS_INVALID_HANDLE);
            return;
        }
        const uint32_t iosb = c.Arg(4), buf = c.Arg(5), len = c.Arg(6), off = c.Arg(7);
        uint8_t* dst = c.mem.Host(buf, len ? len : 1);
        if (!dst) {
            c.Return(STATUS_ACCESS_DENIED);
            return;
        }
        const ssize_t n = off ? ::pread(f->fd, dst, len, static_cast<off_t>(c.mem.Read64(off))) : ::read(f->fd, dst, len);
        if (iosb) {
            c.mem.Write32(iosb, n > 0 ? 0 : STATUS_END_OF_FILE);
            c.mem.Write32(iosb + 4, n > 0 ? static_cast<uint32_t>(n) : 0);
        }
        c.Return(n > 0 ? STATUS_SUCCESS : n == 0 ? STATUS_END_OF_FILE : STATUS_ACCESS_DENIED);
    });
    R("NtQueryInformationFile", 5, [st](ShimCall& c) {
        auto f = st->GetAs<KFile>(c.Arg(0), KObject::Kind::File);
        struct stat stt{};
        if (!f || f->console || ::fstat(f->fd, &stt) != 0) {
            c.Return(STATUS_INVALID_HANDLE);
            return;
        }
        const uint32_t iosb = c.Arg(1), buf = c.Arg(2), len = c.Arg(3), cls = c.Arg(4);
        auto done = [&](uint32_t n) {
            if (iosb) {
                c.mem.Write32(iosb, 0);
                c.mem.Write32(iosb + 4, n);
            }
            c.Return(STATUS_SUCCESS);
        };
        if (cls == 5 && len >= 24) {  // FileStandardInformation
            c.mem.Fill(buf, 0, 24);
            c.mem.Write64(buf, static_cast<uint64_t>(stt.st_blocks) * 512ull);
            c.mem.Write64(buf + 8, static_cast<uint64_t>(stt.st_size));
            c.mem.Write32(buf + 16, 1);
            c.mem.Write8(buf + 21, S_ISDIR(stt.st_mode) ? 1 : 0);
            done(24);
        } else if (cls == 4 && len >= 40) {  // FileBasicInformation
            c.mem.Fill(buf, 0, 40);
            c.mem.Write32(buf + 32, S_ISDIR(stt.st_mode) ? 0x10 : 0x20);
            done(40);
        } else if (cls == 14 && len >= 8) {  // FilePositionInformation
            c.mem.Write64(buf, static_cast<uint64_t>(::lseek(f->fd, 0, SEEK_CUR)));
            done(8);
        } else {
            st->NoteStub("NtQueryInformationFile class " + std::to_string(cls));
            c.Return(STATUS_INVALID_INFO_CLASS);
        }
    });

    // --- sections --------------------------------------------------------------------------------
    R("NtCreateSection", 7, [st](ShimCall& c) {
        auto f = st->GetAs<KFile>(c.Arg(6), KObject::Kind::File);
        if (!f) {
            st->NoteStub("NtCreateSection without a file");
            c.Return(STATUS_INVALID_HANDLE);
            return;
        }
        auto sec = std::make_shared<KSection>();
        sec->hostPath = f->hostPath;
        sec->image = (c.Arg(5) & 0x1000000u) != 0;  // SEC_IMAGE
        c.mem.Write32(c.Arg(0), st->Add(sec));
        c.Return(STATUS_SUCCESS);
    });
    R("NtMapViewOfSection", 10, [st](ShimCall& c) {
        auto obj = st->Get(c.Arg(0));
        auto sec = obj ? std::dynamic_pointer_cast<KSection>(obj) : nullptr;
        if (!sec) {
            c.Return(STATUS_INVALID_HANDLE);
            return;
        }
        std::vector<uint8_t> file;
        if (!ReadHostFile(sec->hostPath, &file) || file.empty()) {
            c.Return(STATUS_ACCESS_DENIED);
            return;
        }
        uint32_t size = 0, base = 0;
        if (sec->image) {
            base = PeImage::MapView(c.mem, file, &size);
        } else {
            size = static_cast<uint32_t>(file.size());
            base = c.mem.AllocPages(size);
            if (base) c.mem.WriteBytes(base, file.data(), file.size());
        }
        if (!base) {
            c.Return(STATUS_NO_MEMORY);
            return;
        }
        NETEMU_LOGI("NtMapViewOfSection %s%s at 0x%08X", sec->hostPath.c_str(), sec->image ? " (image)" : "", base);
        c.mem.Write32(c.Arg(2), base);
        if (c.Arg(6)) c.mem.Write32(c.Arg(6), (size + Map::kPage - 1) & ~(Map::kPage - 1));
        c.Return(sec->image && base != 0x10000000u ? 0x40000003u : STATUS_SUCCESS);  // STATUS_IMAGE_NOT_AT_BASE
    });
    R("NtUnmapViewOfSection", 2, [](ShimCall& c) {
        uint32_t base = 0;
        if (c.mem.QueryPages(c.Arg(1), &base, nullptr)) c.mem.FreePages(base);
        c.Return(STATUS_SUCCESS);
    });

    // --- object manager -------------------------------------------------------------------------
    R("ZwOpenSymbolicLinkObject", 3, [st](ShimCall& c) {
        auto h = std::make_shared<KGeneric>();
        h->name = "<symlink>" + ReadUnicodeString(c.mem, c.mem.Read32(c.Arg(2) + 8));
        c.mem.Write32(c.Arg(0), st->Add(h));
        c.Return(STATUS_SUCCESS);
    });
    R("ZwQuerySymbolicLinkObject", 3, [st](ShimCall& c) {
        auto h = st->Get(c.Arg(0));
        if (!h) {
            c.Return(STATUS_INVALID_HANDLE);
            return;
        }
        // Every drive letter is the one volume.
        const std::string target = "\\Device\\HarddiskVolume1";
        const uint32_t us = c.Arg(1);
        const uint16_t cap = c.mem.Read16(us + 2);
        if (c.Arg(2)) c.mem.Write32(c.Arg(2), static_cast<uint32_t>(target.size() * 2));
        if (cap < target.size() * 2) {
            c.Return(STATUS_BUFFER_TOO_SMALL);
            return;
        }
        c.mem.WriteW(c.mem.Read32(us + 4), target, cap / 2);
        c.mem.Write16(us, static_cast<uint16_t>(target.size() * 2));
        c.Return(STATUS_SUCCESS);
    });
    auto querySystem = [st](ShimCall& c) {
        const uint32_t cls = c.Arg(0), buf = c.Arg(1), len = c.Arg(2), ret = c.Arg(3);
        auto need = [&](uint32_t n) {
            if (ret) c.mem.Write32(ret, n);
            if (len < n) {
                c.Return(STATUS_INFO_LENGTH_MISMATCH);
                return false;
            }
            c.mem.Fill(buf, 0, n);
            return true;
        };
        switch (cls) {
        case 0:  // SystemBasicInformation
            if (!need(44)) return;
            c.mem.Write32(buf + 8, 0x1000);
            c.mem.Write32(buf + 12, 0x100000);
            c.mem.Write32(buf + 24, 0x10000);
            c.mem.Write32(buf + 28, 0x10000);
            c.mem.Write32(buf + 32, 0x7FFEFFFF);
            c.mem.Write32(buf + 36, 0xF);
            c.mem.Write8(buf + 40, 4);
            break;
        case 5: {  // SystemProcessInformation: only this process
            const std::string name = "gta_sa.exe";
            const uint32_t entry = 0xB8 + 64;
            if (!need(entry)) return;
            c.mem.Write32(buf + 0x04, 1);                        // NumberOfThreads
            c.mem.Write16(buf + 0x38, static_cast<uint16_t>(name.size() * 2));
            c.mem.Write16(buf + 0x3A, static_cast<uint16_t>(name.size() * 2 + 2));
            c.mem.Write32(buf + 0x3C, buf + 0xB8);
            c.mem.Write32(buf + 0x44, 0x1000);                   // UniqueProcessId
            c.mem.WriteW(buf + 0xB8, name, 32);
            break;
        }
        case 11:  // SystemModuleInformation: no modules listed
        case 16:  // SystemHandleInformation: no handles listed
            if (!need(8)) return;
            break;
        case 0x23:  // SystemKernelDebuggerInformation
            if (!need(2)) return;
            c.mem.Write8(buf, 0);
            c.mem.Write8(buf + 1, 1);
            break;
        case 0x67:  // SystemCodeIntegrityInformation
            if (!need(8)) return;
            c.mem.Write32(buf, 8);
            c.mem.Write32(buf + 4, 1);  // CODEINTEGRITY_OPTION_ENABLED
            break;
        default:
            st->NoteStub("ZwQuerySystemInformation class " + std::to_string(cls));
            c.Return(STATUS_INVALID_INFO_CLASS);
            return;
        }
        c.Return(STATUS_SUCCESS);
    };
    R("ZwQuerySystemInformation", 4, querySystem);
    R("NtQuerySystemInformation", 4, querySystem);
}

}  // namespace NetEmu
