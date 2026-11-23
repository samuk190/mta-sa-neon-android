// kernel32: process, modules, memory, time, strings, locale, environment.
// Synchronization and threads live in Win32Sync.cpp, files in Win32Files.cpp.

#include "NetEmuLog.h"
#include "Win32Shims.h"

#include <cstdlib>

#include <cctype>

namespace NetEmu {

namespace {
constexpr char kK32[] = "kernel32.dll";

// UTF-16 code units <-> code page. 65001 is UTF-8; every other code page is
// treated as Windows-1252 restricted to Latin-1, which covers what netc sends.
std::u16string ReadUnits(GuestMemory& mem, uint32_t va, int32_t count) {
    std::u16string out;
    if (count < 0) {
        for (uint32_t i = 0;; ++i) {
            const uint16_t ch = mem.Read16(va + i * 2);
            out.push_back(static_cast<char16_t>(ch));
            if (!ch || !mem.Valid(va + i * 2 + 2, 2)) break;
        }
    } else {
        for (int32_t i = 0; i < count; ++i) out.push_back(static_cast<char16_t>(mem.Read16(va + static_cast<uint32_t>(i) * 2)));
    }
    return out;
}

std::u16string DecodeBytes(const std::string& in, uint32_t cp) {
    std::u16string out;
    if (cp != 65001) {
        for (unsigned char c : in) out.push_back(static_cast<char16_t>(c));
        return out;
    }
    for (size_t i = 0; i < in.size();) {
        const unsigned char c = static_cast<unsigned char>(in[i]);
        uint32_t code = 0xFFFD;
        size_t len = 1;
        if (c < 0x80) code = c;
        else if ((c & 0xE0) == 0xC0 && i + 1 < in.size()) code = ((c & 0x1Fu) << 6) | (in[i + 1] & 0x3F), len = 2;
        else if ((c & 0xF0) == 0xE0 && i + 2 < in.size())
            code = ((c & 0x0Fu) << 12) | ((in[i + 1] & 0x3F) << 6) | (in[i + 2] & 0x3F), len = 3;
        else if ((c & 0xF8) == 0xF0 && i + 3 < in.size())
            code = ((c & 0x07u) << 18) | ((in[i + 1] & 0x3F) << 12) | ((in[i + 2] & 0x3F) << 6) | (in[i + 3] & 0x3F), len = 4;
        i += len;
        if (code >= 0x10000) {
            code -= 0x10000;
            out.push_back(static_cast<char16_t>(0xD800 + (code >> 10)));
            out.push_back(static_cast<char16_t>(0xDC00 + (code & 0x3FF)));
        } else {
            out.push_back(static_cast<char16_t>(code));
        }
    }
    return out;
}

std::string EncodeUnits(const std::u16string& in, uint32_t cp, bool* usedDefault) {
    std::string out;
    for (size_t i = 0; i < in.size(); ++i) {
        uint32_t code = in[i];
        if (code >= 0xD800 && code <= 0xDBFF && i + 1 < in.size() && in[i + 1] >= 0xDC00 && in[i + 1] <= 0xDFFF) {
            code = 0x10000 + ((code - 0xD800) << 10) + (in[i + 1] - 0xDC00);
            ++i;
        }
        if (cp != 65001) {
            if (code < 0x100) out.push_back(static_cast<char>(code));
            else {
                out.push_back('?');
                if (usedDefault) *usedDefault = true;
            }
            continue;
        }
        if (code < 0x80) out.push_back(static_cast<char>(code));
        else if (code < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (code >> 6)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        } else if (code < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (code >> 12)));
            out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (code >> 18)));
            out.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        }
    }
    return out;
}

char16_t FoldCase(char16_t c, bool upper) {
    if (c < 0x80) return static_cast<char16_t>(upper ? std::toupper(c) : std::tolower(c));
    if (c >= 0xC0 && c <= 0xDE && c != 0xD7 && !upper) return static_cast<char16_t>(c + 0x20);
    if (c >= 0xE0 && c <= 0xFE && c != 0xF7 && upper) return static_cast<char16_t>(c - 0x20);
    return c;
}

// Writes a result string the Win32 way: returns chars written without the
// terminator, or the required size including the terminator when it does not fit.
uint32_t PutStringW(ShimCall& c, uint32_t buf, uint32_t capChars, const std::string& value) {
    const size_t need = value.size() + 1;  // ASCII/Latin values only
    if (!buf || capChars < need) {
        c.SetLastError(Win32::kErrorInsufficientBuffer);
        return static_cast<uint32_t>(need);
    }
    return static_cast<uint32_t>(c.mem.WriteW(buf, value, capChars));
}

uint32_t PutStringA(ShimCall& c, uint32_t buf, uint32_t cap, const std::string& value) {
    const size_t need = value.size() + 1;
    if (!buf || cap < need) {
        c.SetLastError(Win32::kErrorInsufficientBuffer);
        return static_cast<uint32_t>(need);
    }
    return static_cast<uint32_t>(c.mem.WriteA(buf, value, cap));
}

std::string LocaleValue(uint32_t type) {
    switch (type & 0x0FFFFFFFu) {
    case 0x0002: return "English (United States)";
    case 0x0003: return "ENU";
    case 0x0004: return "English";
    case 0x0007: return "United States";
    case 0x000B: return "437";
    case 0x000E: return ".";
    case 0x000F: return ",";
    case 0x0010: return "3;0";
    case 0x0014: return "$";
    case 0x001D: return "/";
    case 0x001E: return ":";
    case 0x001F: return "M/d/yyyy";
    case 0x0020: return "dddd, MMMM d, yyyy";
    case 0x1001: return "English";
    case 0x1002: return "United States";
    case 0x1003: return "h:mm:ss tt";
    case 0x1004: return "1252";
    case 0x0059: return "en";
    case 0x005A: return "US";
    case 0x005C: return "en-US";
    case 0x0001: return "0409";
    default: return {};
    }
}
}  // namespace

// Resource lookup in a mapped PE image, as FindResourceEx does: type, then
// name, then language (the requested one, else neutral, else the first).
// Returns the IMAGE_RESOURCE_DATA_ENTRY address (the HRSRC), 0 when absent.
uint32_t FindImageResource(GuestMemory& mem, uint32_t module, uint32_t typeArg, uint32_t nameArg, uint16_t lang, bool wide) {
    if (!module || mem.Read16(module) != 0x5A4D) return 0;
    const uint32_t pe = module + mem.Read32(module + 0x3C);
    if (mem.Read32(pe) != 0x4550) return 0;
    const uint32_t rva = mem.Read32(pe + 24 + 96 + 2 * 8);  // DataDirectory[IMAGE_DIRECTORY_ENTRY_RESOURCE]
    if (!rva) return 0;
    const uint32_t root = module + rva;
    // An argument is an integer id when its high word is zero, "#123" too.
    auto key = [&](uint32_t arg, std::string* name) -> int64_t {
        if (arg < 0x10000) return arg;
        *name = wide ? mem.ReadW(arg, 256) : mem.ReadA(arg, 256);
        if (name->size() > 1 && (*name)[0] == '#') return std::strtol(name->c_str() + 1, nullptr, 10);
        for (char& ch : *name) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        return -1;
    };
    auto find = [&](uint32_t dir, uint32_t arg, bool any) -> uint32_t {
        std::string name;
        const int64_t id = any ? -2 : key(arg, &name);
        const uint32_t named = mem.Read16(dir + 12), ids = mem.Read16(dir + 14);
        uint32_t first = 0;
        for (uint32_t i = 0; i < named + ids; ++i) {
            const uint32_t e = dir + 16 + i * 8;
            const uint32_t nameField = mem.Read32(e), target = mem.Read32(e + 4);
            if (!first) first = target;
            if (any) return target;
            if (nameField & 0x80000000u) {
                if (id != -1) continue;
                const uint32_t str = root + (nameField & 0x7FFFFFFFu);
                std::string entry = mem.ReadWCount(str + 2, mem.Read16(str));
                for (char& ch : entry) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
                if (entry == name) return target;
            } else if (static_cast<int64_t>(nameField) == id) {
                return target;
            }
        }
        return 0;
    };
    const uint32_t typeEntry = find(root, typeArg, false);
    if (!(typeEntry & 0x80000000u)) return 0;
    const uint32_t nameEntry = find(root + (typeEntry & 0x7FFFFFFFu), nameArg, false);
    if (!(nameEntry & 0x80000000u)) return 0;
    const uint32_t langDir = root + (nameEntry & 0x7FFFFFFFu);
    uint32_t data = lang ? find(langDir, lang, false) : 0;
    if (!data) data = find(langDir, 0, false);  // LANG_NEUTRAL
    if (!data) data = find(langDir, 0, true);
    if (!data || (data & 0x80000000u)) return 0;
    return root + data;
}

// MEMORY_BASIC_INFORMATION for addr, as VirtualQuery/NtQueryVirtualMemory
// report it: regions end where the page protection changes. False above the
// user address space.
bool WriteMemoryBasicInformation(Win32State& st, uint32_t addr, uint32_t out) {
    GuestMemory& mem = st.machine.Mem();
    if (addr > 0x7FFEFFFFu) return false;  // above lpMaximumApplicationAddress
    mem.Fill(out, 0, 28);
    const uint32_t page = addr & ~(Map::kPage - 1);
    auto committed = [&](uint32_t allocBase, uint32_t regionEnd, uint32_t type) {
        const bool image = type == 0x1000000u;
        mem.Write32(out + 0, page);
        mem.Write32(out + 4, allocBase);
        // Image views are created PAGE_EXECUTE_WRITECOPY; private memory keeps
        // the protection it was allocated with.
        mem.Write32(out + 8, image ? 0x80u : mem.Protect(allocBase));
        mem.Write32(out + 12, mem.ProtectRunEnd(page, regionEnd) - page);
        mem.Write32(out + 16, 0x1000);  // MEM_COMMIT
        mem.Write32(out + 20, mem.Protect(page));
        mem.Write32(out + 24, type);
    };
    uint32_t base = 0, size = 0;
    if (mem.QueryPages(addr, &base, &size)) {
        committed(base, base + size, base >= Map::kImageBegin && base < Map::kImageEnd ? 0x1000000u : 0x20000u);
    } else if (const uint32_t span = mem.FreeSpan(addr)) {
        mem.Write32(out + 0, page);
        mem.Write32(out + 12, span);
        mem.Write32(out + 16, 0x10000);  // MEM_FREE
        mem.Write32(out + 20, 0x01);     // PAGE_NOACCESS
    } else if (addr < Map::kNullGuard) {
        mem.Write32(out + 0, page);
        mem.Write32(out + 12, Map::kNullGuard - page);
        mem.Write32(out + 16, 0x10000);
        mem.Write32(out + 20, 0x01);
    } else {
        // Static data, thunks, shared user data: one committed block each.
        uint32_t lo = Map::kStaticBegin, hi = Map::kStaticEnd;
        if (addr >= Map::kThunkBegin && addr < Map::kThunkEnd) lo = Map::kThunkBegin, hi = Map::kThunkEnd;
        else if (addr >= Map::kSharedUserData) lo = Map::kSharedUserData, hi = Map::kSharedUserData + Map::kPage;
        else if (addr >= Map::kStaticEnd) lo = page, hi = page + Map::kPage;
        committed(lo, hi, 0x1000000u);
    }
    return true;
}

void InstallKernel32(Win32State& s) {
    Machine& m = s.machine;
    auto R = [&m](const char* name, int args, ShimFn fn) { m.RegisterShim(kK32, name, args < 0 ? -1 : args * 4, std::move(fn)); };
    Win32State* st = &s;

    // --- errors -----------------------------------------------------------
    R("GetLastError", 0, [](ShimCall& c) { c.Return(c.mem.Read32(c.thread.teb + 0x34)); });
    R("SetLastError", 1, [](ShimCall& c) {
        c.SetLastError(c.Arg(0));
        c.Return(0);
    });

    // --- process and environment --------------------------------------------
    R("GetCommandLineA", 0, [st](ShimCall& c) { c.Return(st->commandLineA); });
    R("GetCommandLineW", 0, [st](ShimCall& c) { c.Return(st->commandLineW); });
    R("GetEnvironmentStringsW", 0, [](ShimCall& c) {
        const char16_t block[] = u"SystemRoot=C:\\Windows\0windir=C:\\Windows\0OS=Windows_NT\0\0";
        const uint32_t va = c.mem.HeapAlloc(sizeof(block), false);
        c.mem.WriteBytes(va, block, sizeof(block));
        c.Return(va);
    });
    R("FreeEnvironmentStringsW", 1, [](ShimCall& c) {
        c.mem.HeapFree(c.Arg(0));
        c.Return(1);
    });
    R("GetEnvironmentVariableA", 3, [](ShimCall& c) {
        const std::string name = Lower(c.mem.ReadA(c.Arg(0), 256));
        std::string value;
        if (name == "systemroot" || name == "windir") value = "C:\\Windows";
        else if (name == "os") value = "Windows_NT";
        if (value.empty()) {
            c.SetLastError(203);  // ERROR_ENVVAR_NOT_FOUND
            c.Return(0);
            return;
        }
        c.Return(PutStringA(c, c.Arg(1), c.Arg(2), value));
    });
    R("SetEnvironmentVariableW", 2, [](ShimCall& c) { c.Return(1); });
    R("ExpandEnvironmentStringsW", 3, [st](ShimCall& c) {
        std::string src = c.mem.ReadW(c.Arg(0));
        const std::pair<const char*, std::string> vars[] = {
            {"%systemroot%", "C:\\Windows"},
            {"%windir%", "C:\\Windows"},
            {"%programfiles%", "C:\\Program Files"},
            {"%programfiles(x86)%", "C:\\Program Files (x86)"},
            {"%programdata%", "C:\\ProgramData"},
            {"%allusersprofile%", "C:\\ProgramData"},
            {"%appdata%", "C:\\Users\\" + st->env.userName + "\\AppData\\Roaming"},
            {"%localappdata%", "C:\\Users\\" + st->env.userName + "\\AppData\\Local"},
            {"%temp%", "C:\\Users\\" + st->env.userName + "\\AppData\\Local\\Temp"},
            {"%tmp%", "C:\\Users\\" + st->env.userName + "\\AppData\\Local\\Temp"},
            {"%userprofile%", "C:\\Users\\" + st->env.userName},
        };
        for (const auto& [k, v] : vars) {
            for (size_t pos; (pos = Lower(src).find(k)) != std::string::npos;) src.replace(pos, std::strlen(k), v);
        }
        const uint32_t need = static_cast<uint32_t>(src.size() + 1);
        if (c.Arg(1) && c.Arg(2) >= need) c.mem.WriteW(c.Arg(1), src, c.Arg(2));
        c.Return(need);
    });
    R("GetStartupInfoW", 1, [](ShimCall& c) {
        c.mem.Fill(c.Arg(0), 0, 68);
        c.mem.Write32(c.Arg(0), 68);
        c.Return(0);
    });
    R("GetStdHandle", 1, [st](ShimCall& c) {
        const uint32_t which = c.Arg(0);
        if (which == 0xFFFFFFF6u) c.Return(st->stdHandles[0]);
        else if (which == 0xFFFFFFF5u) c.Return(st->stdHandles[1]);
        else if (which == 0xFFFFFFF4u) c.Return(st->stdHandles[2]);
        else c.Return(Win32::kInvalidHandle);
    });
    R("SetStdHandle", 2, [](ShimCall& c) { c.Return(1); });
    R("GetConsoleMode", 2, [](ShimCall& c) {
        c.SetLastError(Win32::kErrorInvalidHandle);
        c.Return(0);
    });
    R("GetConsoleOutputCP", 0, [](ShimCall& c) { c.Return(65001); });
    R("ReadConsoleW", 5, [](ShimCall& c) { c.Return(0); });
    R("WriteConsoleW", 5, [](ShimCall& c) {
        NETEMU_LOGI("guest console: %s", c.mem.ReadWCount(c.Arg(1), c.Arg(2)).c_str());
        if (c.Arg(3)) c.mem.Write32(c.Arg(3), c.Arg(2));
        c.Return(1);
    });
    R("GetCurrentProcess", 0, [](ShimCall& c) { c.Return(Win32::kCurrentProcess); });
    R("GetCurrentProcessId", 0, [](ShimCall& c) { c.Return(0x1000); });
    R("GetProcessId", 1, [st](ShimCall& c) {
        const uint32_t pid = st->ProcessPid(c.Arg(0));
        if (!pid) c.SetLastError(Win32::kErrorInvalidHandle);
        c.Return(pid);
    });
    R("GetCurrentThread", 0, [](ShimCall& c) { c.Return(Win32::kCurrentThread); });
    R("GetCurrentThreadId", 0, [](ShimCall& c) { c.Return(c.thread.id); });
    R("ExitProcess", 1, [st](ShimCall& c) {
        NETEMU_LOGE("guest called ExitProcess(%u) from 0x%08X; stopping that thread only", c.Arg(0), c.ReturnAddress());
        st->processExitRequested = true;
        st->processExitCode = c.Arg(0);
        c.Exit(c.Arg(0));
    });
    R("TerminateProcess", 2, [st](ShimCall& c) {
        if (c.Arg(0) == Win32::kCurrentProcess) {
            NETEMU_LOGE("guest called TerminateProcess(self, %u) from 0x%08X", c.Arg(1), c.ReturnAddress());
            st->processExitRequested = true;
            st->processExitCode = c.Arg(1);
            c.Exit(c.Arg(1));
            return;
        }
        c.Return(1);
    });
    R("OpenProcess", 3, [st](ShimCall& c) {
        const uint32_t pid = c.Arg(2);
        if (!st->ProcessImage(pid).empty()) {
            c.Return(st->OpenProcessHandle(pid));
            return;
        }
        // The idle and System processes refuse a medium-integrity caller.
        c.SetLastError(pid == 4 ? Win32::kErrorAccessDenied : Win32::kErrorInvalidParameter);
        c.Return(0);
    });
    R("DuplicateHandle", 7, [st](ShimCall& c) {
        // (srcProcess, src, dstProcess, *dst, access, inherit, options). Only
        // this process exists; pseudo handles become real ones.
        const uint32_t src = c.Arg(1), out = c.Arg(3), options = c.Arg(6);
        const bool self = st->ProcessPid(c.Arg(0)) == Win32State::kSelfPid && st->ProcessPid(c.Arg(2)) == Win32State::kSelfPid;
        const bool pseudo = src == Win32::kCurrentProcess || src == Win32::kCurrentThread;
        std::shared_ptr<KObject> obj;
        if (src == Win32::kCurrentThread) {
            auto t = std::make_shared<KThread>();
            t->tid = c.thread.id;
            obj = t;
        } else if (!pseudo) {
            obj = st->Get(src);
        }
        if (!self || !out || (!pseudo && !obj)) {
            c.SetLastError(Win32::kErrorInvalidHandle);
            c.Return(0);
            return;
        }
        c.mem.Write32(out, src == Win32::kCurrentProcess ? st->OpenProcessHandle(Win32State::kSelfPid) : st->Add(obj));
        if ((options & 1) && !pseudo) st->Close(src);  // DUPLICATE_CLOSE_SOURCE
        c.Return(1);
    });
    R("GetExitCodeProcess", 2, [](ShimCall& c) {
        c.mem.Write32(c.Arg(1), 259);
        c.Return(1);
    });
    R("CreateProcessW", 10, [st](ShimCall& c) {
        st->NoteStub("CreateProcessW " + c.mem.ReadW(c.Arg(1), 260));
        c.SetLastError(Win32::kErrorAccessDenied);
        c.Return(0);
    });
    R("GetProcessAffinityMask", 3, [](ShimCall& c) {
        c.mem.Write32(c.Arg(1), 0xF);
        c.mem.Write32(c.Arg(2), 0xF);
        c.Return(1);
    });
    R("SetProcessAffinityMask", 2, [](ShimCall& c) { c.Return(1); });
    R("SetThreadAffinityMask", 2, [](ShimCall& c) { c.Return(0xF); });
    R("IsDebuggerPresent", 0, [](ShimCall& c) { c.Return(0); });
    R("DebugActiveProcessStop", 1, [](ShimCall& c) {
        c.SetLastError(Win32::kErrorInvalidParameter);
        c.Return(0);
    });
    R("IsProcessorFeaturePresent", 1, [](ShimCall& c) {
        switch (c.Arg(0)) {
        case 2:   // PF_COMPARE_EXCHANGE_DOUBLE
        case 6:   // PF_XMMI_INSTRUCTIONS_AVAILABLE
        case 8:   // PF_RDTSC_INSTRUCTION_AVAILABLE
        case 10:  // PF_XMMI64_INSTRUCTIONS_AVAILABLE
        case 12:  // PF_NX_ENABLED
        case 13:  // PF_SSE3_INSTRUCTIONS_AVAILABLE
        case 17:  // PF_XSAVE? no: PF_COMPARE_EXCHANGE128 is 14; 17 = PF_SECOND_LEVEL_ADDRESS_TRANSLATION
            c.Return(c.Arg(0) == 17 ? 0 : 1);
            return;
        case 23:  // PF_FASTFAIL_AVAILABLE: __fastfail reaches the interrupt hook and is logged
            c.Return(1);
            return;
        default:
            c.Return(0);
        }
    });
    R("GetSystemInfo", 1, [](ShimCall& c) {
        const uint32_t p = c.Arg(0);
        c.mem.Fill(p, 0, 36);
        c.mem.Write32(p + 4, 0x1000);
        c.mem.Write32(p + 8, 0x10000);
        c.mem.Write32(p + 12, 0x7FFEFFFF);
        c.mem.Write32(p + 16, 0xF);
        c.mem.Write32(p + 20, 4);
        c.mem.Write32(p + 24, 586);
        c.mem.Write32(p + 28, 0x10000);
        c.mem.Write16(p + 32, 6);
        c.mem.Write16(p + 34, 0x9E0A);
        c.Return(0);
    });
    R("GetSystemDirectoryA", 2, [](ShimCall& c) { c.Return(PutStringA(c, c.Arg(0), c.Arg(1), "C:\\Windows\\system32")); });
    R("GetSystemDirectoryW", 2, [](ShimCall& c) { c.Return(PutStringW(c, c.Arg(0), c.Arg(1), "C:\\Windows\\system32")); });
    R("GetSystemWow64DirectoryW", 2, [](ShimCall& c) { c.Return(PutStringW(c, c.Arg(0), c.Arg(1), "C:\\Windows\\SysWOW64")); });
    R("GetLogicalDrives", 0, [](ShimCall& c) { c.Return(0x4); });
    R("GetDriveTypeW", 1, [](ShimCall& c) { c.Return(3); });  // DRIVE_FIXED
    R("VerSetConditionMask", 4, [](ShimCall& c) {
        uint64_t mask = c.Arg64(0);
        const uint32_t type = c.Arg(2);
        const uint32_t cond = c.Arg(3) & 7;
        for (uint32_t bit = 0; bit < 8; ++bit)
            if (type & (1u << bit)) mask |= static_cast<uint64_t>(cond) << (bit * 3);
        c.Return(static_cast<uint32_t>(mask), static_cast<uint32_t>(mask >> 32));
    });
    R("VerifyVersionInfoW", 4, [](ShimCall& c) { c.Return(1); });
    R("PeekNamedPipe", 6, [](ShimCall& c) { c.Return(0); });

    // --- modules -------------------------------------------------------------
    auto moduleFileName = [st](uint32_t hModule) -> std::string {
        if (!hModule || hModule == st->exeBase) return st->env.gtaDir + "\\gta_sa.exe";
        if (hModule == st->machine.MainModule()) return st->env.moduleGuestPath;
        const std::string n = st->ModuleName(hModule);
        return n.empty() ? std::string() : "C:\\Windows\\SysWOW64\\" + n;
    };
    R("GetModuleFileNameW", 3, [moduleFileName](ShimCall& c) {
        const std::string path = moduleFileName(c.Arg(0));
        if (path.empty()) {
            c.SetLastError(Win32::kErrorModNotFound);
            c.Return(0);
            return;
        }
        const uint32_t cap = c.Arg(2);
        if (!cap) {
            c.SetLastError(Win32::kErrorInsufficientBuffer);
            c.Return(0);
            return;
        }
        const size_t n = c.mem.WriteW(c.Arg(1), path, cap);
        if (n < path.size()) {
            c.SetLastError(Win32::kErrorInsufficientBuffer);
            c.Return(cap);
            return;
        }
        c.SetLastError(0);
        c.Return(static_cast<uint32_t>(n));
    });
    auto moduleHandle = [st](const std::string& name) -> uint32_t {
        if (name.empty()) return st->exeBase;
        std::string n = Lower(name);
        const size_t slash = n.find_last_of("\\/");
        if (slash != std::string::npos) n = n.substr(slash + 1);
        if (n == "netc.dll" || n == "netc" || n == "netc_d.dll") return st->machine.MainModule();
        if (n == "gta_sa.exe" || n == "proxy_sa.exe") return st->exeBase;
        return st->ModuleFor(n);
    };
    R("GetModuleHandleA", 1, [moduleHandle](ShimCall& c) {
        const uint32_t h = moduleHandle(c.Arg(0) ? c.mem.ReadA(c.Arg(0), 260) : "");
        if (!h) c.SetLastError(Win32::kErrorModNotFound);
        c.Return(h);
    });
    R("GetModuleHandleW", 1, [moduleHandle](ShimCall& c) {
        const uint32_t h = moduleHandle(c.Arg(0) ? c.mem.ReadW(c.Arg(0), 260) : "");
        if (!h) c.SetLastError(Win32::kErrorModNotFound);
        c.Return(h);
    });
    auto moduleHandleEx = [st, moduleHandle](ShimCall& c, bool wide) {
        const uint32_t flags = c.Arg(0);
        uint32_t h = 0;
        if (flags & 4) {  // GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
            const uint32_t addr = c.Arg(1);
            const uint32_t base = st->machine.MainModule();
            uint32_t b = 0, size = 0;
            if (base && c.mem.QueryPages(addr, &b, &size) && b == base) h = base;
            else if (addr >= Map::kThunkBegin && addr < Map::kThunkEnd) h = st->ModuleFor("kernel32.dll");
        } else {
            h = moduleHandle(c.Arg(1) ? (wide ? c.mem.ReadW(c.Arg(1), 260) : c.mem.ReadA(c.Arg(1), 260)) : "");
        }
        if (c.Arg(2)) c.mem.Write32(c.Arg(2), h);
        if (!h) c.SetLastError(Win32::kErrorModNotFound);
        c.Return(h ? 1 : 0);
    };
    R("GetModuleHandleExA", 3, [moduleHandleEx](ShimCall& c) { moduleHandleEx(c, false); });
    R("GetModuleHandleExW", 3, [moduleHandleEx](ShimCall& c) { moduleHandleEx(c, true); });
    auto loadLibrary = [st, moduleHandle](ShimCall& c, const std::string& name) {
        const uint32_t h = moduleHandle(name);
        if (!h) {
            st->NoteStub("LoadLibrary " + name);
            c.SetLastError(Win32::kErrorModNotFound);
        }
        c.Return(h);
    };
    R("LoadLibraryA", 1, [loadLibrary](ShimCall& c) { loadLibrary(c, c.mem.ReadA(c.Arg(0), 260)); });
    R("LoadLibraryW", 1, [loadLibrary](ShimCall& c) { loadLibrary(c, c.mem.ReadW(c.Arg(0), 260)); });
    R("LoadLibraryExA", 3, [loadLibrary](ShimCall& c) { loadLibrary(c, c.mem.ReadA(c.Arg(0), 260)); });
    R("LoadLibraryExW", 3, [loadLibrary](ShimCall& c) { loadLibrary(c, c.mem.ReadW(c.Arg(0), 260)); });
    R("FreeLibrary", 1, [](ShimCall& c) { c.Return(1); });
    R("FreeLibraryAndExitThread", 2, [](ShimCall& c) { c.Exit(c.Arg(1)); });
    R("GetProcAddress", 2, [st](ShimCall& c) {
        const uint32_t module = c.Arg(0);
        const uint32_t nameVa = c.Arg(1);
        if (nameVa < 0x10000) {
            st->NoteStub("GetProcAddress by ordinal " + std::to_string(nameVa));
            c.SetLastError(Win32::kErrorProcNotFound);
            c.Return(0);
            return;
        }
        const std::string name = c.mem.ReadA(nameVa, 256);
        uint32_t fn = 0;
        if (module == st->machine.MainModule() && st->mainExport) fn = st->mainExport(name);
        if (!fn) {
            const std::string dll = st->ModuleName(module);
            if (!dll.empty()) fn = st->machine.ResolveImport(dll, name, 0);
            // kernelbase and ntdll forward much of kernel32's surface (and the reverse).
            if (!fn && (dll == "kernelbase.dll" || dll == "ntdll.dll")) fn = st->machine.ResolveImport("kernel32.dll", name, 0);
            if (!fn && dll == "kernel32.dll") fn = st->machine.ResolveImport("ntdll.dll", name, 0);
            if (!fn && (dll.rfind("api-ms-", 0) == 0 || dll.rfind("ext-ms-", 0) == 0)) fn = st->machine.ResolveByName(name);
        }
        NETEMU_TRACE("GetProcAddress %s!%s -> 0x%08X", st->ModuleName(module).c_str(), name.c_str(), fn);
        if (!fn) {
            st->NoteStub("GetProcAddress " + st->ModuleName(module) + "!" + name);
            c.SetLastError(Win32::kErrorProcNotFound);
        }
        c.Return(fn);
    });
    R("EncodePointer", 1, [](ShimCall& c) { c.Return(c.Arg(0)); });
    R("DecodePointer", 1, [](ShimCall& c) { c.Return(c.Arg(0)); });

    // --- memory --------------------------------------------------------------
    R("GetProcessHeap", 0, [st](ShimCall& c) { c.Return(st->processHeap); });
    R("HeapAlloc", 3, [](ShimCall& c) {
        const uint32_t p = c.mem.HeapAlloc(c.Arg(2), (c.Arg(1) & 0x8) != 0);
        if (!p) c.SetLastError(Win32::kErrorNotEnoughMemory);
        c.Return(p);
    });
    R("HeapFree", 3, [](ShimCall& c) { c.Return(c.mem.HeapFree(c.Arg(2)) ? 1 : 0); });
    R("HeapReAlloc", 4, [](ShimCall& c) {
        const uint32_t flags = c.Arg(1);
        const uint32_t old = c.Arg(2);
        const uint32_t size = c.Arg(3);
        if (flags & 0x10) {  // HEAP_REALLOC_IN_PLACE_ONLY
            const uint32_t cur = c.mem.HeapSize(old);
            c.Return(cur != 0xFFFFFFFFu && size <= cur ? old : 0);
            return;
        }
        c.Return(c.mem.HeapRealloc(old, size, (flags & 0x8) != 0));
    });
    R("HeapSize", 3, [](ShimCall& c) { c.Return(c.mem.HeapSize(c.Arg(2))); });
    R("HeapQueryInformation", 5, [](ShimCall& c) { c.Return(0); });
    auto globalAlloc = [](ShimCall& c) { c.Return(c.mem.HeapAlloc(c.Arg(1), (c.Arg(0) & 0x40) != 0)); };
    R("GlobalAlloc", 2, globalAlloc);
    R("LocalAlloc", 2, globalAlloc);
    R("GlobalFree", 1, [](ShimCall& c) {
        c.mem.HeapFree(c.Arg(0));
        c.Return(0);
    });
    R("LocalFree", 1, [](ShimCall& c) {
        c.mem.HeapFree(c.Arg(0));
        c.Return(0);
    });
    R("GlobalLock", 1, [](ShimCall& c) { c.Return(c.Arg(0)); });
    R("GlobalUnlock", 1, [](ShimCall& c) { c.Return(1); });
    // Virtual memory. The *Ex forms work on this process; another process's
    // address space is out of reach, as for a medium-integrity caller.
    auto virtualAlloc = [](ShimCall& c, uint32_t addr, uint32_t size, uint32_t prot) {
        uint32_t base = 0, len = 0;
        if (addr && c.mem.QueryPages(addr, &base, &len)) {
            // Commit inside an existing reservation.
            c.mem.SetProtect(addr & ~(Map::kPage - 1), size + (addr & (Map::kPage - 1)), prot);
            c.Return(addr & ~(Map::kPage - 1));
            return;
        }
        uint32_t p = addr ? c.mem.AllocPagesAt(addr & ~0xFFFFu, size + (addr & 0xFFFFu)) : c.mem.AllocPages(size);
        if (p && addr) p = addr & ~(Map::kPage - 1);
        if (!p) c.SetLastError(Win32::kErrorNotEnoughMemory);
        else c.mem.SetProtect(p, size, prot);
        c.Return(p);
    };
    auto virtualFree = [](ShimCall& c, uint32_t addr, uint32_t type) {
        if (type & 0x8000) {  // MEM_RELEASE
            c.Return(c.mem.FreePages(addr) ? 1 : 0);
            return;
        }
        c.Return(1);
    };
    auto virtualProtect = [st](ShimCall& c, uint32_t addr, uint32_t size, uint32_t prot, uint32_t oldOut) {
        if (!oldOut) {  // lpflOldProtect is required
            c.SetLastError(998);  // ERROR_NOACCESS
            c.Return(0);
            return;
        }
        c.mem.Write32(oldOut, c.mem.Protect(addr));
        c.mem.SetProtect(addr & ~(Map::kPage - 1), size + (addr & (Map::kPage - 1)), prot);
        st->machine.FlushCode(addr, addr + size);
        c.Return(1);
    };
    auto virtualQuery = [st](ShimCall& c, uint32_t addr, uint32_t out, uint32_t len) {
        if (len < 28) {
            c.SetLastError(Win32::kErrorInsufficientBuffer);
            c.Return(0);
            return;
        }
        if (!WriteMemoryBasicInformation(*st, addr, out)) {
            c.SetLastError(Win32::kErrorInvalidParameter);
            c.Return(0);
            return;
        }
        c.Return(28);
    };
    // True when the process handle is this process; otherwise fails the call.
    auto self = [st](ShimCall& c, uint32_t process) {
        if (st->ProcessPid(process) == Win32State::kSelfPid) return true;
        c.SetLastError(Win32::kErrorAccessDenied);
        c.Return(0);
        return false;
    };
    R("VirtualAlloc", 4, [virtualAlloc](ShimCall& c) { virtualAlloc(c, c.Arg(0), c.Arg(1), c.Arg(3)); });
    R("VirtualAllocEx", 5, [virtualAlloc, self](ShimCall& c) {
        if (self(c, c.Arg(0))) virtualAlloc(c, c.Arg(1), c.Arg(2), c.Arg(4));
    });
    R("VirtualFree", 3, [virtualFree](ShimCall& c) { virtualFree(c, c.Arg(0), c.Arg(2)); });
    R("VirtualFreeEx", 4, [virtualFree, self](ShimCall& c) {
        if (self(c, c.Arg(0))) virtualFree(c, c.Arg(1), c.Arg(3));
    });
    R("VirtualProtect", 4, [virtualProtect](ShimCall& c) { virtualProtect(c, c.Arg(0), c.Arg(1), c.Arg(2), c.Arg(3)); });
    R("VirtualProtectEx", 5, [virtualProtect, self](ShimCall& c) {
        if (self(c, c.Arg(0))) virtualProtect(c, c.Arg(1), c.Arg(2), c.Arg(3), c.Arg(4));
    });
    R("VirtualQuery", 3, [virtualQuery](ShimCall& c) { virtualQuery(c, c.Arg(0), c.Arg(1), c.Arg(2)); });
    R("VirtualQueryEx", 4, [virtualQuery, self](ShimCall& c) {
        if (self(c, c.Arg(0))) virtualQuery(c, c.Arg(1), c.Arg(2), c.Arg(3));
    });
    // (process, address, buffer, size, *transferred)
    auto processMemory = [self, st](ShimCall& c, bool write) {
        if (!self(c, c.Arg(0))) return;
        const uint32_t remote = c.Arg(1), local = c.Arg(2), size = c.Arg(3), done = c.Arg(4);
        if (done) c.mem.Write32(done, 0);
        if (size && (!c.mem.Valid(remote, size) || !c.mem.Valid(local, size))) {
            c.SetLastError(299);  // ERROR_PARTIAL_COPY
            c.Return(0);
            return;
        }
        std::vector<uint8_t> tmp(size);
        c.mem.ReadBytes(write ? local : remote, tmp.data(), size);
        c.mem.WriteBytes(write ? remote : local, tmp.data(), size);
        // Windows flushes the instruction cache of the target on write; drop
        // any code Unicorn already translated from those bytes.
        if (write) st->machine.FlushCode(remote, remote + size);
        if (done) c.mem.Write32(done, size);
        c.Return(1);
    };
    R("ReadProcessMemory", 5, [processMemory](ShimCall& c) { processMemory(c, false); });
    R("WriteProcessMemory", 5, [processMemory](ShimCall& c) { processMemory(c, true); });

    // --- resources -------------------------------------------------------------
    auto findResource = [st](ShimCall& c, uint32_t module, uint32_t name, uint32_t type, uint16_t lang, bool wide) {
        if (!module) module = st->exeBase;
        const uint32_t h = FindImageResource(c.mem, module, type, name, lang, wide);
        if (!h) c.SetLastError(1813);  // ERROR_RESOURCE_TYPE_NOT_FOUND
        c.Return(h);
    };
    R("FindResourceA", 3, [findResource](ShimCall& c) { findResource(c, c.Arg(0), c.Arg(1), c.Arg(2), 0, false); });
    R("FindResourceW", 3, [findResource](ShimCall& c) { findResource(c, c.Arg(0), c.Arg(1), c.Arg(2), 0, true); });
    R("FindResourceExA", 4, [findResource](ShimCall& c) {
        findResource(c, c.Arg(0), c.Arg(2), c.Arg(1), static_cast<uint16_t>(c.Arg(3)), false);
    });
    R("FindResourceExW", 4, [findResource](ShimCall& c) {
        findResource(c, c.Arg(0), c.Arg(2), c.Arg(1), static_cast<uint16_t>(c.Arg(3)), true);
    });
    R("LoadResource", 2, [st](ShimCall& c) {
        // The HGLOBAL of an image resource is its mapped address.
        const uint32_t module = c.Arg(0) ? c.Arg(0) : st->exeBase;
        c.Return(c.Arg(1) ? module + c.mem.Read32(c.Arg(1)) : 0);
    });
    R("LockResource", 1, [](ShimCall& c) { c.Return(c.Arg(0)); });
    R("SizeofResource", 2, [](ShimCall& c) { c.Return(c.Arg(1) ? c.mem.Read32(c.Arg(1) + 4) : 0); });
    R("FreeResource", 1, [](ShimCall& c) { c.Return(0); });

    R("FlushInstructionCache", 3, [st](ShimCall& c) {
        st->machine.FlushCode(c.Arg(1), c.Arg(1) + (c.Arg(2) ? c.Arg(2) : Map::kPage));
        c.Return(1);
    });
    R("InitializeSListHead", 1, [](ShimCall& c) {
        c.mem.Write64(c.Arg(0), 0);
        c.Return(0);
    });
    R("InterlockedPushEntrySList", 2, [](ShimCall& c) {
        const uint32_t head = c.Arg(0);
        const uint32_t entry = c.Arg(1);
        const uint32_t first = c.mem.Read32(head);
        c.mem.Write32(entry, first);
        c.mem.Write32(head, entry);
        c.mem.Write16(head + 4, static_cast<uint16_t>(c.mem.Read16(head + 4) + 1));
        c.mem.Write16(head + 6, static_cast<uint16_t>(c.mem.Read16(head + 6) + 1));
        c.Return(first);
    });
    R("InterlockedFlushSList", 1, [](ShimCall& c) {
        const uint32_t head = c.Arg(0);
        const uint32_t first = c.mem.Read32(head);
        c.mem.Write32(head, 0);
        c.mem.Write16(head + 4, 0);
        c.Return(first);
    });

    // --- time ----------------------------------------------------------------
    // Tick counts start at an hour so code that subtracts small intervals at
    // startup never wraps.
    R("GetTickCount", 0, [st](ShimCall& c) { c.Return(static_cast<uint32_t>(st->machine.NowMs() + 3600000ull)); });
    R("QueryPerformanceFrequency", 1, [](ShimCall& c) {
        c.mem.Write64(c.Arg(0), 10000000ull);
        c.Return(1);
    });
    R("QueryPerformanceCounter", 1, [st](ShimCall& c) {
        c.mem.Write64(c.Arg(0), (st->machine.NowUs() + 3600000000ull) * 10ull);
        c.Return(1);
    });
    R("GetSystemTimeAsFileTime", 1, [](ShimCall& c) {
        c.mem.Write64(c.Arg(0), FileTimeNow());
        c.Return(0);
    });
    R("GetSystemTimePreciseAsFileTime", 1, [](ShimCall& c) {
        c.mem.Write64(c.Arg(0), FileTimeNow());
        c.Return(0);
    });
    R("GetSystemTime", 1, [](ShimCall& c) {
        WriteSystemTime(c.mem, c.Arg(0), FileTimeNow(), false);
        c.Return(0);
    });
    R("GetLocalTime", 1, [](ShimCall& c) {
        WriteSystemTime(c.mem, c.Arg(0), FileTimeNow(), true);
        c.Return(0);
    });
    R("FileTimeToSystemTime", 2, [](ShimCall& c) {
        WriteSystemTime(c.mem, c.Arg(1), c.mem.Read64(c.Arg(0)), false);
        c.Return(1);
    });
    R("FileTimeToLocalFileTime", 2, [](ShimCall& c) {
        c.mem.Write64(c.Arg(1), c.mem.Read64(c.Arg(0)));
        c.Return(1);
    });
    R("SystemTimeToTzSpecificLocalTime", 3, [](ShimCall& c) {
        uint8_t st16[16];
        c.mem.ReadBytes(c.Arg(1), st16, 16);
        c.mem.WriteBytes(c.Arg(2), st16, 16);
        c.Return(1);
    });
    R("GetTimeZoneInformation", 1, [](ShimCall& c) {
        c.mem.Fill(c.Arg(0), 0, 172);
        c.Return(0);  // TIME_ZONE_ID_UNKNOWN: UTC
    });
    R("GetThreadTimes", 5, [](ShimCall& c) {
        for (int i = 1; i <= 4; ++i)
            if (c.Arg(i)) c.mem.Write64(c.Arg(i), 0);
        c.Return(1);
    });
    auto noFormat = [st](ShimCall& c) {
        st->NoteStub(c.def.name);
        c.SetLastError(Win32::kErrorNotSupported);
        c.Return(0);
    };
    R("GetDateFormatW", 6, noFormat);
    R("GetTimeFormatW", 6, noFormat);

    // --- code pages and strings ----------------------------------------------
    R("GetACP", 0, [](ShimCall& c) { c.Return(1252); });
    R("GetOEMCP", 0, [](ShimCall& c) { c.Return(437); });
    R("AreFileApisANSI", 0, [](ShimCall& c) { c.Return(1); });
    R("IsValidCodePage", 1, [](ShimCall& c) {
        const uint32_t cp = c.Arg(0);
        c.Return(cp == 1252 || cp == 65001 || cp == 437 || cp == 20127 || cp == 28591 ? 1 : 0);
    });
    R("GetCPInfo", 2, [](ShimCall& c) {
        const uint32_t p = c.Arg(1);
        c.mem.Fill(p, 0, 20);
        c.mem.Write32(p, c.Arg(0) == 65001 ? 4 : 1);
        c.mem.Write8(p + 4, '?');
        c.Return(1);
    });
    R("MultiByteToWideChar", 6, [](ShimCall& c) {
        uint32_t cp = c.Arg(0);
        if (cp == 0 || cp == 1 || cp == 2 || cp == 3) cp = 1252;  // CP_ACP, OEM, MAC, THREAD_ACP
        const uint32_t src = c.Arg(2);
        const int32_t srcLen = static_cast<int32_t>(c.Arg(3));
        const uint32_t dst = c.Arg(4);
        const uint32_t dstLen = c.Arg(5);
        std::string in;
        if (srcLen < 0) {
            in = c.mem.ReadA(src, 1u << 20);
            in.push_back('\0');
        } else {
            in = c.mem.ReadA(src, static_cast<size_t>(srcLen), true);
        }
        const std::u16string out = DecodeBytes(in, cp);
        if (!dstLen) {
            c.Return(static_cast<uint32_t>(out.size()));
            return;
        }
        if (dstLen < out.size()) {
            c.SetLastError(Win32::kErrorInsufficientBuffer);
            c.Return(0);
            return;
        }
        c.mem.WriteBytes(dst, out.data(), out.size() * 2);
        c.Return(static_cast<uint32_t>(out.size()));
    });
    R("WideCharToMultiByte", 8, [](ShimCall& c) {
        uint32_t cp = c.Arg(0);
        if (cp == 0 || cp == 1 || cp == 2 || cp == 3) cp = 1252;
        const std::u16string in = ReadUnits(c.mem, c.Arg(2), static_cast<int32_t>(c.Arg(3)));
        bool usedDefault = false;
        const std::string out = EncodeUnits(in, cp, &usedDefault);
        if (c.Arg(7)) c.mem.Write32(c.Arg(7), usedDefault ? 1 : 0);
        const uint32_t dstLen = c.Arg(5);
        if (!dstLen) {
            c.Return(static_cast<uint32_t>(out.size()));
            return;
        }
        if (dstLen < out.size()) {
            c.SetLastError(Win32::kErrorInsufficientBuffer);
            c.Return(0);
            return;
        }
        c.mem.WriteBytes(c.Arg(4), out.data(), out.size());
        c.Return(static_cast<uint32_t>(out.size()));
    });
    R("CompareStringW", 6, [](ShimCall& c) {
        std::u16string a = ReadUnits(c.mem, c.Arg(2), static_cast<int32_t>(c.Arg(3)));
        std::u16string b = ReadUnits(c.mem, c.Arg(4), static_cast<int32_t>(c.Arg(5)));
        if (!a.empty() && a.back() == 0) a.pop_back();
        if (!b.empty() && b.back() == 0) b.pop_back();
        if (c.Arg(1) & 1) {  // NORM_IGNORECASE
            for (auto& ch : a) ch = FoldCase(ch, false);
            for (auto& ch : b) ch = FoldCase(ch, false);
        }
        const int r = a.compare(b);
        c.Return(r < 0 ? 1 : r == 0 ? 2 : 3);
    });
    auto lcmap = [](ShimCall& c, uint32_t flags, uint32_t src, int32_t n, uint32_t dst, uint32_t dstN) {
        std::u16string s = ReadUnits(c.mem, src, n);
        if (flags & 0x100) for (auto& ch : s) ch = FoldCase(ch, false);
        if (flags & 0x200) for (auto& ch : s) ch = FoldCase(ch, true);
        if (flags & 0x400) {  // LCMAP_SORTKEY: bytes of the folded string plus a terminator
            std::string key;
            for (char16_t ch : s) if (ch) key.push_back(static_cast<char>(FoldCase(ch, false) & 0xFF));
            key.push_back('\0');
            if (!dstN) {
                c.Return(static_cast<uint32_t>(key.size()));
                return;
            }
            if (dstN < key.size()) {
                c.SetLastError(Win32::kErrorInsufficientBuffer);
                c.Return(0);
                return;
            }
            c.mem.WriteBytes(dst, key.data(), key.size());
            c.Return(static_cast<uint32_t>(key.size()));
            return;
        }
        if (!dstN) {
            c.Return(static_cast<uint32_t>(s.size()));
            return;
        }
        if (dstN < s.size()) {
            c.SetLastError(Win32::kErrorInsufficientBuffer);
            c.Return(0);
            return;
        }
        c.mem.WriteBytes(dst, s.data(), s.size() * 2);
        c.Return(static_cast<uint32_t>(s.size()));
    };
    R("LCMapStringW", 6, [lcmap](ShimCall& c) { lcmap(c, c.Arg(1), c.Arg(2), static_cast<int32_t>(c.Arg(3)), c.Arg(4), c.Arg(5)); });
    R("LCMapStringEx", 9, [lcmap](ShimCall& c) { lcmap(c, c.Arg(1), c.Arg(2), static_cast<int32_t>(c.Arg(3)), c.Arg(4), c.Arg(5)); });
    R("GetStringTypeW", 4, [](ShimCall& c) {
        const std::u16string s = ReadUnits(c.mem, c.Arg(1), static_cast<int32_t>(c.Arg(2)));
        const uint32_t out = c.Arg(3);
        for (size_t i = 0; i < s.size(); ++i) {
            const char16_t ch = s[i];
            uint16_t t = 0;
            if (c.Arg(0) == 1) {  // CT_CTYPE1
                if (ch < 0x80) {
                    if (std::isupper(ch)) t |= 0x001;
                    if (std::islower(ch)) t |= 0x002;
                    if (std::isdigit(ch)) t |= 0x004;
                    if (std::isspace(ch)) t |= 0x008;
                    if (std::ispunct(ch)) t |= 0x010;
                    if (std::iscntrl(ch)) t |= 0x020;
                    if (ch == ' ' || ch == '\t') t |= 0x040;
                    if (std::isxdigit(ch)) t |= 0x080;
                    if (std::isalpha(ch)) t |= 0x100;
                } else if (ch >= 0xC0 && ch != 0xD7 && ch != 0xF7 && ch <= 0xFF) {
                    t |= 0x100 | (ch < 0xE0 ? 0x001 : 0x002);
                } else if (ch >= 0xA0) {
                    t |= 0x010;
                }
                t |= 0x200;  // C1_DEFINED
            }
            c.mem.Write16(out + static_cast<uint32_t>(i) * 2, t);
        }
        c.Return(1);
    });
    auto localeInfo = [](ShimCall& c, uint32_t type, uint32_t buf, uint32_t cap) {
        if (type & 0x20000000u) {  // LOCALE_RETURN_NUMBER
            const uint32_t base = type & 0x0FFFFFFFu;
            uint32_t v = 0;
            if (base == 0x1004) v = 1252;
            else if (base == 0x000B) v = 437;
            else if (base == 0x0001) v = 0x409;
            if (cap < 2) {
                c.Return(2);
                return;
            }
            c.mem.Write32(buf, v);
            c.Return(2);
            return;
        }
        const std::string v = LocaleValue(type);
        if (v.empty()) {
            c.SetLastError(Win32::kErrorInvalidParameter);
            c.Return(0);
            return;
        }
        if (!cap) {
            c.Return(static_cast<uint32_t>(v.size() + 1));
            return;
        }
        const uint32_t n = PutStringW(c, buf, cap, v);
        c.Return(n == v.size() ? n + 1 : 0);
    };
    R("GetLocaleInfoW", 4, [localeInfo](ShimCall& c) { localeInfo(c, c.Arg(1), c.Arg(2), c.Arg(3)); });
    R("GetLocaleInfoEx", 4, [localeInfo](ShimCall& c) { localeInfo(c, c.Arg(1), c.Arg(2), c.Arg(3)); });
    R("GetUserDefaultLCID", 0, [](ShimCall& c) { c.Return(0x409); });
    R("IsValidLocale", 2, [](ShimCall& c) { c.Return(1); });
    R("EnumSystemLocalesW", 2, [](ShimCall& c) { c.Return(1); });
    auto formatMessage = [](ShimCall& c, bool wide) {
        const uint32_t flags = c.Arg(0);
        const uint32_t id = c.Arg(2);
        char text[64];
        std::snprintf(text, sizeof(text), "Error %u\r\n", id);
        const std::string msg = text;
        const uint32_t buf = c.Arg(4);
        const uint32_t cap = c.Arg(5);
        if (flags & 0x100) {  // FORMAT_MESSAGE_ALLOCATE_BUFFER
            const uint32_t p = wide ? c.mem.NewW(msg) : c.mem.NewA(msg);
            c.mem.Write32(buf, p);
            c.Return(static_cast<uint32_t>(msg.size()));
            return;
        }
        if (cap <= msg.size()) {
            c.SetLastError(Win32::kErrorInsufficientBuffer);
            c.Return(0);
            return;
        }
        c.Return(static_cast<uint32_t>(wide ? c.mem.WriteW(buf, msg, cap) : c.mem.WriteA(buf, msg, cap)));
    };
    R("FormatMessageA", 7, [formatMessage](ShimCall& c) { formatMessage(c, false); });
    R("FormatMessageW", 7, [formatMessage](ShimCall& c) { formatMessage(c, true); });
    R("OutputDebugStringA", 1, [](ShimCall& c) {
        NETEMU_LOGI("guest debug: %s", c.mem.ReadA(c.Arg(0), 2048).c_str());
        c.Return(0);
    });
    R("OutputDebugStringW", 1, [](ShimCall& c) {
        NETEMU_LOGI("guest debug: %s", c.mem.ReadW(c.Arg(0), 2048).c_str());
        c.Return(0);
    });

    // --- exceptions and debugging --------------------------------------------
    R("SetUnhandledExceptionFilter", 1, [](ShimCall& c) { c.Return(0); });
    R("UnhandledExceptionFilter", 1, [](ShimCall& c) { c.Return(1); });
    R("GetThreadContext", 2, [](ShimCall& c) {
        // Debug registers read as clear: no hardware breakpoints.
        const uint32_t ctx = c.Arg(1);
        for (uint32_t off = 4; off <= 0x18; off += 4) c.mem.Write32(ctx + off, 0);
        c.Return(1);
    });
    R("SetThreadContext", 2, [](ShimCall& c) { c.Return(1); });
    R("Thread32First", 2, [](ShimCall& c) {
        c.SetLastError(Win32::kErrorNoMoreFiles);
        c.Return(0);
    });
    R("Thread32Next", 2, [](ShimCall& c) {
        c.SetLastError(Win32::kErrorNoMoreFiles);
        c.Return(0);
    });
}

}  // namespace NetEmu
