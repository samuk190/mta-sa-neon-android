#pragma once
// Win32 API surface that netc.dll imports, implemented on POSIX.
//
// Every import is registered with its stdcall argument size so the stack is
// unwound exactly as the real DLL would. Functions that only matter on a
// desktop (windows, clipboard, certificates, services) are stubs that fail
// cleanly; the network, threading, time, heap, file and registry paths are
// implemented.

#include "X86Machine.h"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace NetEmu {

// Paths and identity the emulated process reports.
struct Win32Env {
    std::string dataDir;  // host directory for the emulated C: drive and the registry file
    std::string mtaDir = "C:\\Program Files (x86)\\MTA San Andreas 1.6";
    std::string gtaDir = "C:\\Program Files (x86)\\Rockstar Games\\GTA San Andreas";
    std::string moduleGuestPath;  // guest path of netc.dll
    std::string moduleHostPath;   // host file that guest path opens
    std::string computerName = "SAONLINE-PC";
    std::string userName = "player";
    std::string localIp = "192.168.1.100";  // the one virtual adapter's address
};

// The virtual machine's hardware, shared by WMI, the registry's
// HARDWARE\DESCRIPTION tree and the other identity shims so they agree.
namespace Hw {
constexpr const char* kVendor = "SAONLINE";
constexpr const char* kSystemProduct = "SAONLINE Virtual PC";
constexpr const char* kBoardProduct = "SAO-VIRTUAL-BOARD";
constexpr const char* kBiosVersion = "1.0";
constexpr const char* kBiosDateWmi = "20240101000000.000000+000";
constexpr const char* kBiosDateRegistry = "01/01/2024";
constexpr const char* kCpuName = "Intel(R) Core(TM) i5 CPU @ 3.00GHz";
constexpr const char* kCpuIdentifier = "Intel64 Family 6 Model 158 Stepping 10";
constexpr uint32_t kCpuMhz = 3000;
}  // namespace Hw

namespace Win32 {
constexpr uint32_t kErrorSuccess = 0;
constexpr uint32_t kErrorFileNotFound = 2;
constexpr uint32_t kErrorPathNotFound = 3;
constexpr uint32_t kErrorAccessDenied = 5;
constexpr uint32_t kErrorInvalidHandle = 6;
constexpr uint32_t kErrorNotEnoughMemory = 8;
constexpr uint32_t kErrorNoMoreFiles = 18;
constexpr uint32_t kErrorNotSupported = 50;
constexpr uint32_t kErrorFileExists = 80;
constexpr uint32_t kErrorInvalidParameter = 87;
constexpr uint32_t kErrorInsufficientBuffer = 122;
constexpr uint32_t kErrorModNotFound = 126;
constexpr uint32_t kErrorProcNotFound = 127;
constexpr uint32_t kErrorAlreadyExists = 183;
constexpr uint32_t kErrorMoreData = 234;
constexpr uint32_t kErrorNoMoreItems = 259;
constexpr uint32_t kWaitObject0 = 0;
constexpr uint32_t kWaitAbandoned = 0x80;
constexpr uint32_t kWaitTimeout = 258;
constexpr uint32_t kWaitFailed = 0xFFFFFFFFu;
constexpr uint32_t kInfinite = 0xFFFFFFFFu;
constexpr uint32_t kInvalidHandle = 0xFFFFFFFFu;
constexpr uint32_t kCurrentProcess = 0xFFFFFFFFu;
constexpr uint32_t kCurrentThread = 0xFFFFFFFEu;
}  // namespace Win32

struct KObject {
    enum class Kind { Event, Mutex, Semaphore, Thread, File, Find, Timer, Process, Mapping, Generic };
    explicit KObject(Kind k) : kind(k) {}
    virtual ~KObject() = default;
    Kind kind;
    std::string name;
};

struct KEvent : KObject {
    KEvent() : KObject(Kind::Event) {}
    bool manual = false;
    bool signaled = false;
    // WSAEventSelect binding: the event also reads signaled while the socket
    // has one of the selected network events.
    int socketFd = -1;
    uint32_t networkMask = 0;
};

struct KMutex : KObject {
    KMutex() : KObject(Kind::Mutex) {}
    uint32_t owner = 0;
    uint32_t recursion = 0;
};

struct KSemaphore : KObject {
    KSemaphore() : KObject(Kind::Semaphore) {}
    int32_t count = 0;
    int32_t maximum = 1;
};

struct KThread : KObject {
    KThread() : KObject(Kind::Thread) {}
    uint32_t tid = 0;
};

struct KTimer : KObject {
    KTimer() : KObject(Kind::Timer) {}
    bool manual = false;
    bool signaled = false;
    bool armed = false;
    uint64_t dueMs = 0;
    uint32_t periodMs = 0;
};

struct KFile : KObject {
    KFile() : KObject(Kind::File) {}
    int fd = -1;
    std::string hostPath;
    bool console = false;
    bool disk = false;  // \\.\PhysicalDrive0 stand-in, answers storage IOCTLs
    ~KFile() override;
};

struct KFind : KObject {
    KFind() : KObject(Kind::Find) {}
    std::vector<std::string> names;  // host entries matching the pattern
    std::string hostDir;
    size_t next = 0;
};

struct KMapping : KObject {
    KMapping() : KObject(Kind::Mapping) {}
    uint32_t size = 0;
    uint32_t view = 0;
};

struct KSection : KObject {
    KSection() : KObject(Kind::Generic) {}
    std::string hostPath;
    bool image = false;
    uint32_t size = 0;
};

struct KGeneric : KObject {
    KGeneric() : KObject(Kind::Generic) {}
};

// Shared state of the shim layer. Only touched on the emu thread.
class Win32State {
public:
    Win32State(Machine& m, Win32Env env);

    Machine& machine;
    Win32Env env;

    uint32_t Add(std::shared_ptr<KObject> obj);
    std::shared_ptr<KObject> Get(uint32_t handle) const;
    template <class T>
    std::shared_ptr<T> GetAs(uint32_t handle, KObject::Kind kind) const {
        auto o = Get(handle);
        return o && o->kind == kind ? std::static_pointer_cast<T>(o) : nullptr;
    }
    bool Close(uint32_t handle);
    uint32_t FindNamed(const std::string& name) const;

    // Thread handle for a guest thread (created on demand).
    uint32_t ThreadHandle(GuestThread* t);

    // Waits: signaled state and the acquire side effect.
    bool IsSignaled(uint32_t handle, uint32_t tid);
    void Acquire(uint32_t handle, uint32_t tid);

    // Guest path (C:\..., relative, forward slashes) to a host path.
    std::string HostPath(const std::string& guestPath) const;
    std::string CurrentDirectory = "C:\\Program Files (x86)\\MTA San Andreas 1.6\\MTA";

    // Registry (HKEY handles are kept apart from kernel handles).
    struct RegKey {
        std::string path;  // lower case, backslash separated
    };
    std::map<std::string, std::map<std::string, std::pair<uint32_t, std::vector<uint8_t>>>> registry;
    std::unordered_map<uint32_t, RegKey> regKeys;
    uint32_t nextRegKey = 0x2000;
    void LoadRegistry();
    void SaveRegistry() const;
    // Seed HKLM\Software\<registryPath>\<version> and \Common with the install
    // location so netc's install check passes (otherwise "Error [U01]").
    void SeedMtaRegistry(const std::string& registryPath, const std::string& version, const std::string& installDir);
    void RegSetSz(const std::string& keyPath, const std::string& valueName, const std::string& value);
    void RegSetDword(const std::string& keyPath, const std::string& valueName, uint32_t value);

    // FLS slots: index -> destructor callback (guest VA).
    std::vector<uint32_t> flsCallbacks;
    std::vector<bool> flsUsed;

    // Modules visible to GetModuleHandle / LoadLibrary (lower-case name -> fake base).
    std::map<std::string, uint32_t> modules;
    uint32_t ModuleFor(const std::string& name);
    std::string ModuleName(uint32_t base) const;

    // Strings that must stay alive in guest memory (command line, environment).
    uint32_t commandLineA = 0, commandLineW = 0;

    // pthreads-win32 objects keyed by the guest handle value.
    struct PMutex {
        uint32_t owner = 0;
        uint32_t recursion = 0;
        bool recursive = false;
    };
    struct PCond {
        uint64_t generation = 0;
        uint32_t waiters = 0;
        uint32_t pendingSignals = 0;
    };
    std::unordered_map<uint32_t, PMutex> pmutexes;
    std::unordered_map<uint32_t, PCond> pconds;
    uint32_t nextPObject = 0x10;

    // Win32 condition variables and SRW locks keyed by guest address.
    std::unordered_map<uint32_t, PCond> conditionVars;
    struct Srw {
        uint32_t exclusiveOwner = 0;
        uint32_t shared = 0;
    };
    std::unordered_map<uint32_t, Srw> srwLocks;

    // Per-install random identity (dataDir/machine_id): volume serial and
    // other hardware ids derive from it, so each phone gets its own MTA serial.
    uint64_t machineSeed = 0;

    // RtlAddVectoredExceptionHandler registrations (handler VAs, first = called first).
    std::vector<uint32_t> vectoredHandlers;

    // Exports of the loaded netc image, for GetProcAddress on its own handle.
    std::function<uint32_t(const std::string&)> mainExport;
    uint32_t exeBase = 0x00400000u;
    uint32_t processHeap = 0;
    uint32_t stdHandles[3] = {0, 0, 0};
    // Set when the guest called ExitProcess/TerminateProcess on itself.
    bool processExitRequested = false;
    uint32_t processExitCode = 0;

    // Sockets, DNS lookups and static buffers of ws2_32 (Win32Winsock.cpp).
    std::shared_ptr<struct WinsockState> winsock;

    // Shim-owned helpers (e.g. the WMI provider) that registered thunks capture.
    std::vector<std::shared_ptr<void>> keepAlive;

    // Processes this machine pretends exist (pid -> image path), and the pid
    // behind a process handle (pseudo handle included); 0 when not a process.
    static constexpr uint32_t kSelfPid = 0x1000, kExplorerPid = 0x0FA0;
    std::string ProcessImage(uint32_t pid) const;
    uint32_t OpenProcessHandle(uint32_t pid);
    uint32_t ProcessPid(uint32_t handle) const;

    // Diagnostics: unimplemented calls seen at least once.
    std::map<std::string, uint64_t> stubHits;
    void NoteStub(const std::string& name);

private:
    std::unordered_map<uint32_t, std::shared_ptr<KObject>> handles_;
};

void InstallKernel32(Win32State& s);
void InstallSync(Win32State& s);
void InstallFiles(Win32State& s);
void InstallWinsock(Win32State& s);
void InstallStubs(Win32State& s);
void InstallNtdll(Win32State& s);
void InstallWmi(Win32State& s);
// MEMORY_BASIC_INFORMATION (28 bytes) for addr; false above user space.
bool WriteMemoryBasicInformation(Win32State& st, uint32_t addr, uint32_t out);
void InstallAll(Win32State& s);

// Socket readiness for an event bound with WSAEventSelect (Win32Winsock.cpp).
bool WinsockEventReady(const Win32State& st, int fd, uint32_t mask);

// Helpers shared by the shim files.
std::string Lower(std::string s);
uint64_t UnixMsNow();
uint64_t FileTimeNow();  // 100 ns since 1601
void WriteSystemTime(GuestMemory& mem, uint32_t va, uint64_t fileTime, bool local);

}  // namespace NetEmu
