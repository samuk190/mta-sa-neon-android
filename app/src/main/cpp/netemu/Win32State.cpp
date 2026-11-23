#include "NetEmuLog.h"
#include "Win32Shims.h"

#include <sys/stat.h>

#include <cstdlib>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>

namespace NetEmu {

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

uint64_t UnixMsNow() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
}

uint64_t FileTimeNow() {
    const uint64_t us = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
    return us * 10ull + 116444736000000000ull;
}

void WriteSystemTime(GuestMemory& mem, uint32_t va, uint64_t fileTime, bool local) {
    const uint64_t unix100ns = fileTime - 116444736000000000ull;
    const time_t secs = static_cast<time_t>(unix100ns / 10000000ull);
    const uint32_t ms = static_cast<uint32_t>((unix100ns / 10000ull) % 1000ull);
    std::tm tm{};
    if (local) localtime_r(&secs, &tm);
    else gmtime_r(&secs, &tm);
    mem.Write16(va + 0, static_cast<uint16_t>(tm.tm_year + 1900));
    mem.Write16(va + 2, static_cast<uint16_t>(tm.tm_mon + 1));
    mem.Write16(va + 4, static_cast<uint16_t>(tm.tm_wday));
    mem.Write16(va + 6, static_cast<uint16_t>(tm.tm_mday));
    mem.Write16(va + 8, static_cast<uint16_t>(tm.tm_hour));
    mem.Write16(va + 10, static_cast<uint16_t>(tm.tm_min));
    mem.Write16(va + 12, static_cast<uint16_t>(tm.tm_sec));
    mem.Write16(va + 14, static_cast<uint16_t>(ms));
}

KFile::~KFile() {
    if (fd >= 0 && !console) ::close(fd);
}

namespace {
// A PE header good enough for code that walks a module's headers.
void WriteFakePe(GuestMemory& mem, uint32_t base) {
    mem.Write16(base, 0x5A4D);
    mem.Write32(base + 0x3C, 0x80);
    mem.Write32(base + 0x80, 0x00004550);
    mem.Write16(base + 0x84, 0x014C);
    mem.Write16(base + 0x86, 0);
    mem.Write16(base + 0x94, 0xE0);
    mem.Write16(base + 0x96, 0x2102);
    mem.Write16(base + 0x98, 0x010B);
    mem.Write32(base + 0x98 + 28, base);
    mem.Write32(base + 0x98 + 56, 0x1000);
    mem.Write32(base + 0x98 + 60, 0x1000);
    mem.Write32(base + 0x98 + 92, 16);
}
}  // namespace

Win32State::Win32State(Machine& m, Win32Env e) : machine(m), env(std::move(e)) {
    GuestMemory& mem = machine.Mem();
    if (mem.AllocPagesAt(exeBase, 0x10000)) WriteFakePe(mem, exeBase);
    processHeap = mem.Read32(machine.Peb() + 0x18);
    const std::string cmd = "\"" + env.gtaDir + "\\gta_sa.exe\"";
    commandLineA = mem.StaticAlloc(static_cast<uint32_t>(cmd.size() + 1));
    mem.WriteA(commandLineA, cmd, cmd.size() + 1);
    commandLineW = mem.StaticAlloc(static_cast<uint32_t>(cmd.size() * 2 + 2));
    mem.WriteW(commandLineW, cmd, cmd.size() + 1);
    for (int i = 0; i < 3; ++i) {
        auto f = std::make_shared<KFile>();
        f->fd = i;
        f->console = true;
        stdHandles[i] = Add(f);
    }
    LoadRegistry();
    // The folders every Windows profile and MTA install has; netc keeps data
    // in alternate streams on some of them.
    for (const std::string& dir : std::vector<std::string>{"C:\\Windows\\System32", "C:\\ProgramData\\MTA San Andreas All\\Common",
          "C:\\Users\\" + env.userName + "\\AppData\\Roaming", "C:\\Users\\" + env.userName + "\\AppData\\Local\\Temp",
          "C:\\Users\\" + env.userName + "\\Documents\\GTA San Andreas User Files", env.mtaDir + "\\MTA", env.gtaDir}) {
        const std::string host = HostPath(dir);
        for (size_t pos = 1; pos != std::string::npos; pos = host.find('/', pos + 1)) ::mkdir(host.substr(0, pos).c_str(), 0700);
        ::mkdir(host.c_str(), 0700);
    }
    // Stable per-install identity.
    const std::string idPath = env.dataDir + "/machine_id";
    if (FILE* f = std::fopen(idPath.c_str(), "rb")) {
        if (std::fread(&machineSeed, sizeof(machineSeed), 1, f) != 1) machineSeed = 0;
        std::fclose(f);
    }
    if (!machineSeed) {
        if (FILE* r = std::fopen("/dev/urandom", "rb")) {
            if (std::fread(&machineSeed, sizeof(machineSeed), 1, r) != 1) machineSeed = 0;
            std::fclose(r);
        }
        if (!machineSeed) machineSeed = UnixMsNow() * 0x9E3779B97F4A7C15ull;
        if (FILE* f = std::fopen(idPath.c_str(), "wb")) {
            std::fwrite(&machineSeed, sizeof(machineSeed), 1, f);
            std::fclose(f);
        }
    }
    // HARDWARE\DESCRIPTION, filled by Windows at every boot from SMBIOS/CPUID.
    const std::string bios = "hklm\\hardware\\description\\system\\bios";
    RegSetSz(bios, "BIOSVendor", Hw::kVendor);
    RegSetSz(bios, "BIOSVersion", Hw::kBiosVersion);
    RegSetSz(bios, "BIOSReleaseDate", Hw::kBiosDateRegistry);
    RegSetSz(bios, "SystemManufacturer", Hw::kVendor);
    RegSetSz(bios, "SystemProductName", Hw::kSystemProduct);
    RegSetSz(bios, "SystemFamily", Hw::kSystemProduct);
    RegSetSz(bios, "SystemVersion", "1.0");
    RegSetSz(bios, "SystemSKU", "SAO-1");
    RegSetSz(bios, "BaseBoardManufacturer", Hw::kVendor);
    RegSetSz(bios, "BaseBoardProduct", Hw::kBoardProduct);
    RegSetSz(bios, "BaseBoardVersion", "1.0");
    const std::string cpu = "hklm\\hardware\\description\\system\\centralprocessor\\0";
    RegSetSz(cpu, "ProcessorNameString", Hw::kCpuName);
    RegSetSz(cpu, "Identifier", Hw::kCpuIdentifier);
    RegSetSz(cpu, "VendorIdentifier", "GenuineIntel");
    RegSetDword(cpu, "~MHz", Hw::kCpuMhz);
}

uint32_t Win32State::Add(std::shared_ptr<KObject> obj) {
    const uint32_t h = machine.NewHandle();
    handles_[h] = std::move(obj);
    return h;
}

std::shared_ptr<KObject> Win32State::Get(uint32_t handle) const {
    auto it = handles_.find(handle);
    return it == handles_.end() ? nullptr : it->second;
}

bool Win32State::Close(uint32_t handle) { return handles_.erase(handle) > 0; }

uint32_t Win32State::FindNamed(const std::string& name) const {
    if (name.empty()) return 0;
    for (const auto& [h, o] : handles_)
        if (o->name == name) return h;
    return 0;
}

uint32_t Win32State::ThreadHandle(GuestThread* t) {
    if (!t) return 0;
    for (const auto& [h, o] : handles_)
        if (o->kind == KObject::Kind::Thread && std::static_pointer_cast<KThread>(o)->tid == t->id) return h;
    auto k = std::make_shared<KThread>();
    k->tid = t->id;
    return Add(k);
}

bool Win32State::IsSignaled(uint32_t handle, uint32_t tid) {
    auto o = Get(handle);
    if (!o) return false;
    switch (o->kind) {
    case KObject::Kind::Event: {
        auto e = std::static_pointer_cast<KEvent>(o);
        if (e->signaled) return true;
        if (e->socketFd >= 0 && e->networkMask) {
            if (WinsockEventReady(e->socketFd, e->networkMask)) return true;
        }
        return false;
    }
    case KObject::Kind::Mutex: {
        auto m = std::static_pointer_cast<KMutex>(o);
        return m->owner == 0 || m->owner == tid;
    }
    case KObject::Kind::Semaphore:
        return std::static_pointer_cast<KSemaphore>(o)->count > 0;
    case KObject::Kind::Thread: {
        GuestThread* t = machine.ThreadById(std::static_pointer_cast<KThread>(o)->tid);
        return !t || t->state == GuestThread::State::Exited;
    }
    case KObject::Kind::Timer: {
        auto t = std::static_pointer_cast<KTimer>(o);
        if (t->armed && machine.NowMs() >= t->dueMs) {
            t->signaled = true;
            if (t->periodMs) t->dueMs += t->periodMs;
            else t->armed = false;
        }
        return t->signaled;
    }
    case KObject::Kind::Process:
        return false;
    default:
        return true;
    }
}

void Win32State::Acquire(uint32_t handle, uint32_t tid) {
    auto o = Get(handle);
    if (!o) return;
    switch (o->kind) {
    case KObject::Kind::Event: {
        auto e = std::static_pointer_cast<KEvent>(o);
        if (!e->manual) e->signaled = false;
        break;
    }
    case KObject::Kind::Mutex: {
        auto m = std::static_pointer_cast<KMutex>(o);
        m->owner = tid;
        ++m->recursion;
        break;
    }
    case KObject::Kind::Semaphore:
        --std::static_pointer_cast<KSemaphore>(o)->count;
        break;
    case KObject::Kind::Timer: {
        auto t = std::static_pointer_cast<KTimer>(o);
        if (!t->manual) t->signaled = false;
        break;
    }
    default:
        break;
    }
}

std::string Win32State::HostPath(const std::string& guestPath) const {
    std::string p = guestPath;
    std::replace(p.begin(), p.end(), '/', '\\');
    if (p.rfind("\\\\?\\", 0) == 0) p.erase(0, 4);
    if (p.size() < 2 || p[1] != ':') {
        std::string base = CurrentDirectory;
        if (!p.empty() && p[0] == '\\') p = base.substr(0, 2) + p;
        else p = base + "\\" + p;
    }
    const std::string lower = Lower(p);
    if (!env.moduleGuestPath.empty() && lower == Lower(env.moduleGuestPath)) return env.moduleHostPath;
    // Normalize "." and ".." segments.
    std::vector<std::string> parts;
    std::stringstream ss(lower.substr(3));
    std::string seg;
    while (std::getline(ss, seg, '\\')) {
        if (seg.empty() || seg == ".") continue;
        if (seg == "..") {
            if (!parts.empty()) parts.pop_back();
            continue;
        }
        parts.push_back(seg);
    }
    std::string out = env.dataDir + "/drive_" + std::string(1, lower[0]);
    for (const auto& part : parts) out += "/" + part;
    return out;
}

std::string Win32State::ProcessImage(uint32_t pid) const {
    if (pid == kSelfPid) return env.gtaDir + "\\gta_sa.exe";
    if (pid == kExplorerPid) return "C:\\Windows\\explorer.exe";
    return {};
}

uint32_t Win32State::OpenProcessHandle(uint32_t pid) {
    auto p = std::make_shared<KGeneric>();
    p->kind = KObject::Kind::Process;
    p->name = "process:" + std::to_string(pid);
    return Add(p);
}

uint32_t Win32State::ProcessPid(uint32_t handle) const {
    if (handle == Win32::kCurrentProcess) return kSelfPid;
    auto k = Get(handle);
    if (!k || k->kind != KObject::Kind::Process) return 0;
    if (k->name.rfind("process:", 0) == 0) return static_cast<uint32_t>(std::strtoul(k->name.c_str() + 8, nullptr, 10));
    return kSelfPid;
}

uint32_t Win32State::ModuleFor(const std::string& name) {
    std::string n = Lower(name);
    const size_t slash = n.find_last_of("\\/");
    if (slash != std::string::npos) n = n.substr(slash + 1);
    if (n.find('.') == std::string::npos) n += ".dll";
    auto it = modules.find(n);
    if (it != modules.end()) return it->second;
    static const char* const kKnown[] = {"kernel32.dll", "kernelbase.dll", "ntdll.dll",   "user32.dll",  "advapi32.dll",
                                         "ws2_32.dll",   "wininet.dll",    "bcrypt.dll",  "shell32.dll", "ole32.dll",
                                         "oleaut32.dll", "crypt32.dll",    "normaliz.dll", "pthread.dll", "winmm.dll",
                                         "wintrust.dll", "iphlpapi.dll",   "secur32.dll", "gdi32.dll",   "version.dll",
                                         "shlwapi.dll",  "dbghelp.dll",    "psapi.dll",   "userenv.dll", "iertutil.dll"};
    // API-set contracts (api-ms-win-*, ext-ms-*) resolve by function name.
    bool known = n.rfind("api-ms-win-", 0) == 0 || n.rfind("ext-ms-", 0) == 0;
    for (const char* k : kKnown) known = known || n == k;
    if (!known) return 0;
    const uint32_t base = machine.Mem().StaticAlloc(0x1000, 0x1000);
    if (!base) return 0;
    WriteFakePe(machine.Mem(), base);
    modules[n] = base;
    return base;
}

std::string Win32State::ModuleName(uint32_t base) const {
    for (const auto& [n, b] : modules)
        if (b == base) return n;
    return {};
}

void Win32State::NoteStub(const std::string& name) {
    if (stubHits[name]++ == 0) NETEMU_LOGW("stub: %s", name.c_str());
}

void Win32State::LoadRegistry() {
    if (env.dataDir.empty()) return;
    std::ifstream in(env.dataDir + "/registry.txt");
    std::string line;
    while (std::getline(in, line)) {
        // key \t value \t type \t hex
        std::stringstream ss(line);
        std::string key, value, type, hex;
        if (!std::getline(ss, key, '\t') || !std::getline(ss, value, '\t') || !std::getline(ss, type, '\t')) continue;
        std::getline(ss, hex);
        std::vector<uint8_t> data;
        for (size_t i = 0; i + 1 < hex.size(); i += 2) data.push_back(static_cast<uint8_t>(std::stoul(hex.substr(i, 2), nullptr, 16)));
        registry[key][value] = {static_cast<uint32_t>(std::stoul(type)), data};
    }
}

void Win32State::SaveRegistry() const {
    if (env.dataDir.empty()) return;
    const std::string path = env.dataDir + "/registry.txt";
    std::ofstream out(path + ".tmp", std::ios::trunc);
    static const char* hexDigits = "0123456789abcdef";
    for (const auto& [key, values] : registry)
        for (const auto& [name, tv] : values) {
            out << key << '\t' << name << '\t' << tv.first << '\t';
            for (uint8_t b : tv.second) out << hexDigits[b >> 4] << hexDigits[b & 15];
            out << '\n';
        }
    out.close();
    std::rename((path + ".tmp").c_str(), path.c_str());
}

void Win32State::RegSetSz(const std::string& keyPath, const std::string& valueName, const std::string& value) {
    std::vector<uint8_t> data;
    for (char ch : value) {
        data.push_back(static_cast<uint8_t>(ch));
        data.push_back(0);
    }
    data.push_back(0);
    data.push_back(0);
    registry[Lower(keyPath)][Lower(valueName)] = {1u /* REG_SZ */, data};
}

void Win32State::RegSetDword(const std::string& keyPath, const std::string& valueName, uint32_t value) {
    std::vector<uint8_t> data(4);
    std::memcpy(data.data(), &value, 4);
    registry[Lower(keyPath)][Lower(valueName)] = {4u /* REG_DWORD */, data};
}

void Win32State::SeedMtaRegistry(const std::string& registryPath, const std::string& version, const std::string& installDir) {
    const std::string root = "hklm\\" + Lower(registryPath);
    const std::string verKey = root + "\\" + Lower(version);
    const std::string commonKey = root + "\\common";
    // Make the keys exist even before a value is set.
    registry[verKey];
    registry[commonKey];
    registry[root];
    // Install location: both the versioned and common forms MTA uses.
    RegSetSz(verKey, "Last Install Location", installDir);
    RegSetSz(verKey, "Last Run Location", installDir);
    RegSetSz(verKey, "Install Location", installDir);
    RegSetSz(commonKey, "GTA:SA Path", env.gtaDir);
    RegSetSz(commonKey, "Last Install Location", installDir);
    RegSetSz(root, "Last Install Location", installDir);
    SaveRegistry();
}

void InstallAll(Win32State& s) {
    InstallKernel32(s);
    InstallSync(s);
    InstallFiles(s);
    InstallWinsock(s);
    InstallStubs(s);
    InstallNtdll(s);
    InstallWmi(s);
}

}  // namespace NetEmu
