#pragma once
// i386 user-mode machine for netc.dll on top of Unicorn.
//
// One host thread (the "emu thread") owns the Unicorn engine. Guest threads
// are cooperative: each has its own register context, stack, TEB and FS
// descriptor, and the scheduler runs them in slices on the emu thread. A
// watchdog stops a slice after kSliceMs so a spinning thread cannot starve
// the others.
//
// Win32 imports are bound to 16-byte thunks in [Map::kThunkBegin,
// Map::kThunkEnd). A code hook on that range runs the matching C++ shim.
// A shim can:
//   Return()  finish: set EAX/EDX, pop its stdcall arguments, return.
//   Block()   park the thread at the thunk; the shim runs again on wakeup
//             (deadline reached or any kernel object changed state).
//   Call()    call guest code (for example a vtable method) and continue in
//             a C++ continuation when it returns.
//   Jump()    replace ESP/EIP (thread start).
//   Exit()    end the guest thread.
//
// Host threads talk to the guest only through CallSync() and RunSync(),
// which queue work for the emu thread and wait for it.

#include "GuestMemory.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

struct uc_struct;
struct uc_context;

namespace NetEmu {

class Machine;
class ShimCall;
struct GuestThread;

using ShimFn = std::function<void(ShimCall&)>;

// argBytes < 0 marks a cdecl function (caller pops).
struct ShimDef {
    std::string dll;
    std::string name;
    int argBytes = 0;
    ShimFn fn;
    uint32_t thunk = 0;
    uint64_t calls = 0;
};

struct Continuation {
    ShimFn fn;
    const ShimDef* origin = nullptr;
    uint32_t originEsp = 0;  // ESP at the original shim entry
    uint32_t resumeEsp = 0;  // ESP before the call frame was pushed
};

struct GuestThread {
    enum class State { Runnable, Blocked, Idle, Exited };

    uint32_t id = 0;
    uint32_t handle = 0;
    uc_context* ctx = nullptr;
    bool fresh = true;  // ctx not saved yet: start from the baseline CPU
    uint32_t initEip = 0;
    uint32_t initEsp = 0;
    uint32_t stackBase = 0;
    uint32_t stackSize = 0;
    uint32_t teb = 0;
    uint32_t gdtIndex = 0;
    State state = State::Runnable;
    uint64_t wakeAtMs = 0;
    uint64_t waitGen = 0;
    bool waitActive = false;
    uint64_t waitStartMs = 0;
    uint32_t waitPhase = 0;   // free for multi-step shims, reset on Return
    uint32_t waitAux = 0;     // scratch kept across Block() retries
    uint64_t waitDeadline = 0;
    bool cancelRequested = false;  // pthread_cancel, honoured at cancellation points
    uint32_t exitCode = 259;  // STILL_ACTIVE
    uint32_t suspendCount = 0;
    uint64_t createdUs = 0;        // Machine::NowUs() at creation (GetThreadTimes); main is 0
    uint64_t exitedUs = 0;         // Machine::NowUs() when it exited (0 while running)
    int32_t priority = 0;          // THREAD_PRIORITY_NORMAL; bookkeeping (the scheduler is round-robin)
    bool priorityBoostOff = false;
    // Hardware-exception loop guard. The dispatcher records where a handler
    // resumed the thread; any shim call clears it (progress). Faulting again
    // right there means the handler resumed without fixing the cause.
    uint32_t resumedEip = 0, resumedEsp = 0, exceptionRepeats = 0;
    std::vector<Continuation> conts;
    std::unordered_map<uint32_t, uint32_t> fls;
    std::string name;
    // Host call bookkeeping (main thread only).
    uint32_t jobEsp = 0;
};

class ShimCall {
public:
    enum class Outcome { Return, Block, Call, Jump, Exit, Resume };

    ShimCall(Machine& machine, GuestThread& thread, const ShimDef& def, uint32_t esp);

    Machine& machine;
    GuestThread& thread;
    GuestMemory& mem;
    const ShimDef& def;
    const uint32_t esp;  // [esp] = return address, arguments from esp+4

    uint32_t Arg(int i) const;
    uint64_t Arg64(int i) const;  // two consecutive argument slots
    uint32_t Ecx() const;
    uint32_t ReturnAddress() const;

    // Valid inside a continuation: the guest call's EAX/EDX.
    uint32_t result = 0;
    uint32_t resultEdx = 0;

    void Return(uint32_t eax, uint32_t edx = 0);
    // Run the shim again after at most recheckMs, or as soon as any kernel
    // object changes state. Use WaitElapsedMs() for timeouts.
    void Block(uint32_t recheckMs = 1000);
    // reserveBytes keeps that much stack below ESP untouched (exception records).
    void Call(uint32_t fn, const std::vector<uint32_t>& args, uint32_t ecx, ShimFn continuation, uint32_t reserveBytes = 0);
    // Continue at a full register set (SEH ContinueExecution).
    struct Regs {
        uint32_t eax, ebx, ecx, edx, esi, edi, ebp, esp, eip, eflags;
    };
    void Resume(const Regs& regs);
    void Jump(uint32_t eip, uint32_t newEsp);
    void Exit(uint32_t code);

    uint64_t WaitElapsedMs() const;
    void SetLastError(uint32_t code);

    Outcome outcome = Outcome::Return;
    uint32_t outEax = 0, outEdx = 0;
    uint32_t blockMs = 0;
    uint32_t callFn = 0, callEcx = 0, callReserve = 0;
    Regs resumeRegs{};
    std::vector<uint32_t> callArgs;
    ShimFn callCont;
    uint32_t jumpEip = 0, jumpEsp = 0;
    bool decided = false;
};

struct MachineStats {
    uint64_t slices = 0;
    uint64_t shimCalls = 0;
    uint64_t faults = 0;
};

class Machine {
public:
    Machine();
    ~Machine();
    Machine(const Machine&) = delete;
    Machine& operator=(const Machine&) = delete;

    bool Init(std::string* error);
    GuestMemory& Mem() { return mem_; }

    // Shim registry. Names are matched case-insensitively for the DLL.
    void RegisterShim(const std::string& dll, const std::string& name, int argBytes, ShimFn fn);
    void RegisterOrdinal(const std::string& dll, uint16_t ordinal, const std::string& name);
    // Thunk for dll!name, 0 when no shim exists.
    uint32_t ResolveImport(const std::string& dll, const std::string& name, uint16_t ordinal);
    uint32_t ResolveByName(const std::string& name);  // GetProcAddress across all DLLs
    // Anonymous thunk the guest can call (callbacks, fake vtables).
    uint32_t MakeThunk(const std::string& label, int argBytes, ShimFn fn);
    std::vector<const ShimDef*> Shims() const;

    // Module bookkeeping used by GetModuleHandle and TLS setup.
    void SetMainModule(uint32_t base, uint32_t entry, const std::vector<uint32_t>& tlsCallbacks,
                       uint32_t tlsStart, uint32_t tlsEnd, uint32_t tlsZero, uint32_t tlsIndexVa);
    uint32_t MainModule() const { return moduleBase_; }
    uint32_t Peb() const { return peb_; }

    // Threads. Called on the emu thread (from shims) or before Start().
    GuestThread* CreateThread(uint32_t start, uint32_t param, bool suspended, const std::string& name);
    GuestThread* Current() { return current_; }
    // EBP of the running guest thread; valid inside a shim (emu thread only).
    uint32_t CurrentEbp() const;
    GuestThread* ThreadById(uint32_t id);
    GuestThread* ThreadByHandle(uint32_t handle);
    void ResumeThread(GuestThread* t);
    const std::vector<std::unique_ptr<GuestThread>>& Threads() const { return threads_; }

    // Handle values for kernel objects owned by the shim layer.
    uint32_t NewHandle();

    // Drop translated code for [begin, end) after the host patched it.
    void FlushCode(uint32_t begin, uint32_t end);

    // Any kernel object changed state: blocked threads re-check.
    void Signal();

    uint64_t NowMs() const;
    uint64_t NowUs() const;

    // Emu thread lifecycle.
    bool Start();
    void Stop();
    bool Running() const { return running_.load(); }

    // From any host thread except the emu thread. Calls fn on the main guest
    // thread with ECX = ecx and the given stack arguments. Returns EDX:EAX.
    uint64_t CallSync(uint32_t fn, uint32_t ecx, const std::vector<uint32_t>& args, bool* ok, uint32_t timeoutMs = 30000);
    // A guest call timed out while running: the machine is unusable, every
    // later CallSync fails at once, and guest memory handed to the guest must
    // not be freed (the stuck call may still read it).
    bool Wedged() const { return wedged_.load(); }
    // Runs fn on the emu thread while no guest code executes.
    void RunSync(const std::function<void()>& fn);
    bool OnEmuThread() const;

    // Hardware exceptions (access violation, divide error, breakpoint) go to the
    // guest the way KiUserExceptionDispatcher delivers them: an EXCEPTION_RECORD
    // and a CONTEXT on the faulting thread's stack, then a call into this thunk
    // with (record, context) as its two stack arguments. 0 = every fault is fatal.
    void SetExceptionDispatcher(uint32_t thunk) { exceptionThunk_ = thunk; }
    // CONTEXT (x86, CONTEXT_FULL) offsets used by the dispatcher and RaiseException.
    static constexpr uint32_t kContextSize = 0x2CC;

    MachineStats Stats() const;
    // One line per guest thread: id, name, state, EIP of its saved context.
    std::string DescribeThreads();
    std::string LastFault() const;

    // Engine access for the hooks (internal).
    void OnThunk(uint32_t addr);
    void OnInterrupt(uint32_t intno);
    void OnInvalidMemory(int type, uint64_t addr, int size);
    void OnInvalidInsn();
    void OnPrivilegedInsn();
    void OnThunkWrite(uint32_t addr, int size, uint64_t value, uint32_t eip);

private:
    struct Job {
        uint32_t fn = 0;
        uint32_t ecx = 0;
        std::vector<uint32_t> args;
        std::function<void()> task;  // RunSync work instead of a guest call
        bool done = false;
        bool ok = false;
        uint64_t result = 0;
    };

    bool SetupCpu(std::string* error);
    uint32_t AllocThunk();
    void SetupThreadTls(GuestThread& t);
    void LoadContext(GuestThread& t);
    void Apply(ShimCall& call, uint32_t thunkAddr);
    void PushCall(GuestThread& t, uint32_t fn, const std::vector<uint32_t>& args, uint32_t ecx, uint32_t retAddr, uint32_t reserve = 0);
    void FinishJob(bool ok, uint64_t result);
    void EmuLoop();
    void WatchdogLoop();
    GuestThread* Pick(uint64_t now);
    void RunSlice(GuestThread& t);
    void FaultCurrent(const char* why);
    // Redirects the current thread into the exception dispatcher. false when
    // none is installed or dispatch is already nested too deeply.
    bool RaiseHardwareException(uint32_t code, uint32_t eip, uint32_t nparams, uint32_t p0, uint32_t p1);
    void ReleaseThread(GuestThread& t);
    void UpdateSharedUserData();
    void ThreadStartShim(ShimCall& call);

    GuestMemory mem_;
    uc_struct* uc_ = nullptr;
    uc_context* baseline_ = nullptr;  // CPU state right after SetupCpu (tied to uc_)
    std::vector<std::unique_ptr<ShimDef>> defs_;            // index = thunk slot
    std::unordered_map<std::string, ShimDef*> byName_;      // "dll!name"
    std::unordered_map<std::string, std::string> ordinals_; // "dll#n" -> name
    uint32_t nextThunk_ = Map::kThunkBegin;
    uint32_t retHostThunk_ = 0, contThunk_ = 0, threadExitThunk_ = 0, threadStartThunk_ = 0;

    std::vector<std::unique_ptr<GuestThread>> threads_;
    GuestThread* current_ = nullptr;
    GuestThread* main_ = nullptr;
    uint32_t nextThreadId_ = 0x100;
    uint32_t nextHandle_ = 0x1000;
    std::vector<uint32_t> freeGdt_;
    uint32_t peb_ = 0;
    uint32_t moduleBase_ = 0, moduleEntry_ = 0;
    std::vector<uint32_t> tlsCallbacks_;
    uint32_t tlsStart_ = 0, tlsEnd_ = 0, tlsZero_ = 0;
    bool tlsPresent_ = false;
    size_t rr_ = 0;

    std::atomic<uint64_t> eventGen_{0};
    std::atomic<bool> running_{false};
    std::atomic<bool> stopping_{false};
    std::atomic<bool> wedged_{false};
    std::thread emuThread_;
    std::thread watchdog_;
    std::thread::id emuThreadId_;

    mutable std::mutex jobMutex_;
    std::condition_variable jobCv_;      // wakes the emu loop
    std::condition_variable jobDoneCv_;  // wakes CallSync waiters
    std::deque<std::shared_ptr<Job>> jobs_;
    std::shared_ptr<Job> activeJob_;
    // Where the time of the active guest call goes (emulator thread only).
    // A call slower than kSlowCallMs logs the split (see ReportJobProfile).
    struct JobProfile {
        uint64_t startUs = 0, idleUs = 0, shimUs = 0, slices = 0;
        std::unordered_map<uint32_t, uint64_t> threadUs;  // guest thread id -> slice time
        std::unordered_map<const ShimDef*, std::pair<uint64_t, uint64_t>> shims;  // calls, us
    } profile_;
    void ProfileShim(const ShimDef* def, uint64_t us);
    void ReportJobProfile(uint32_t fn);

    std::mutex sliceMutex_;
    std::condition_variable sliceCv_;
    uint64_t sliceId_ = 0;
    uint64_t sliceDeadlineUs_ = 0;
    bool sliceActive_ = false;
    bool stopRequested_ = false;  // a shim asked to leave the slice

    mutable std::mutex statsMutex_;
    MachineStats stats_;
    std::string lastFault_;
    uint64_t startUs_ = 0;
    uint64_t lastSharedUpdateMs_ = 0;
    bool traceShims_ = false;  // NETEMU_TRACE_SHIMS=1 logs every shim call
    uint32_t exceptionThunk_ = 0;
    int lastMemType_ = 0;       // uc_mem_type of the last invalid access
    uint32_t lastMemAddr_ = 0;
    bool pendingRedirect_ = false;  // an instruction hook raised an exception mid-instruction
    uint32_t redirectEsp_ = 0, redirectEip_ = 0;
};

}  // namespace NetEmu
