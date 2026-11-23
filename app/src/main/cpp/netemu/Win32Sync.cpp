// kernel32 synchronization, threads, FLS, waits, SEH, and pthreads-win32.
//
// Every blocking call follows the same pattern: check the condition, take it
// and Return() when it holds, otherwise Block(). The scheduler runs the shim
// again on the next kernel-object change or when the recheck interval ends.
// Multi-step waits (condition variables) keep their step in waitPhase.

#include "NetEmuLog.h"
#include "Win32Shims.h"

#include <algorithm>

namespace NetEmu {

namespace {
constexpr char kK32[] = "kernel32.dll";
constexpr char kPthread[] = "pthread.dll";
constexpr uint32_t kErrorTimeout = 1460;
constexpr uint32_t kEtimedout = 138;  // MSVC errno.h, used by pthreads-win32 built with VS2010+
constexpr uint32_t kEbusy = 16;
constexpr uint32_t kEinval = 22;
constexpr uint32_t kCanceled = 0xFFFFFFFFu;  // PTHREAD_CANCELED

// Remaining wait budget; INFINITE never times out.
bool TimedOut(ShimCall& c, uint32_t ms) { return ms != Win32::kInfinite && c.WaitElapsedMs() >= ms; }
uint32_t Recheck(ShimCall& c, uint32_t ms, uint32_t cap = 1000) {
    if (ms == Win32::kInfinite) return cap;
    const uint64_t left = ms > c.WaitElapsedMs() ? ms - c.WaitElapsedMs() : 0;
    return static_cast<uint32_t>(std::min<uint64_t>(left, cap));
}

// CRITICAL_SECTION: DebugInfo, LockCount, RecursionCount, OwningThread, LockSemaphore, SpinCount.
bool TryEnterCs(GuestMemory& mem, uint32_t cs, uint32_t tid) {
    const uint32_t owner = mem.Read32(cs + 12);
    if (owner == 0) {
        mem.Write32(cs + 12, tid);
        mem.Write32(cs + 8, 1);
        mem.Write32(cs + 4, 0);
        return true;
    }
    if (owner == tid) {
        mem.Write32(cs + 8, mem.Read32(cs + 8) + 1);
        return true;
    }
    return false;
}

// Release fully; returns the recursion count so a condition wait can restore it.
uint32_t ReleaseCsFully(GuestMemory& mem, uint32_t cs) {
    const uint32_t rec = mem.Read32(cs + 8);
    mem.Write32(cs + 12, 0);
    mem.Write32(cs + 8, 0);
    mem.Write32(cs + 4, 0xFFFFFFFFu);
    return rec;
}

// pthreads-win32 static initializers are (pthread_*_t)-1, -2, -3.
bool IsStaticInit(uint32_t v) { return v >= 0xFFFFFFFDu; }

}  // namespace

// Defined in Win32Winsock.cpp.


void InstallSync(Win32State& s) {
    Machine& m = s.machine;
    Win32State* st = &s;
    auto R = [&m](const char* name, int args, ShimFn fn) { m.RegisterShim(kK32, name, args * 4, std::move(fn)); };
    auto P = [&m](const char* name, ShimFn fn) { m.RegisterShim(kPthread, name, -1, std::move(fn)); };

    // --- handles -------------------------------------------------------------
    R("CloseHandle", 1, [st](ShimCall& c) {
        const uint32_t h = c.Arg(0);
        if (h == Win32::kCurrentProcess || h == Win32::kCurrentThread) {
            c.Return(1);
            return;
        }
        if (!st->Close(h)) {
            c.SetLastError(Win32::kErrorInvalidHandle);
            c.Return(0);
            return;
        }
        c.Return(1);
    });

    // --- critical sections -----------------------------------------------------
    auto initCs = [](ShimCall& c, uint32_t spin) {
        const uint32_t cs = c.Arg(0);
        c.mem.Fill(cs, 0, 24);
        c.mem.Write32(cs + 4, 0xFFFFFFFFu);
        c.mem.Write32(cs + 20, spin);
    };
    R("InitializeCriticalSection", 1, [initCs](ShimCall& c) {
        initCs(c, 0);
        c.Return(0);
    });
    R("InitializeCriticalSectionEx", 3, [initCs](ShimCall& c) {
        initCs(c, c.Arg(1));
        c.Return(1);
    });
    R("DeleteCriticalSection", 1, [](ShimCall& c) { c.Return(0); });
    R("EnterCriticalSection", 1, [](ShimCall& c) {
        if (TryEnterCs(c.mem, c.Arg(0), c.thread.id)) c.Return(0);
        else c.Block(100);
    });
    R("LeaveCriticalSection", 1, [st](ShimCall& c) {
        const uint32_t cs = c.Arg(0);
        if (c.mem.Read32(cs + 12) == c.thread.id) {
            const uint32_t rec = c.mem.Read32(cs + 8);
            if (rec <= 1) {
                ReleaseCsFully(c.mem, cs);
                st->machine.Signal();
            } else {
                c.mem.Write32(cs + 8, rec - 1);
            }
        }
        c.Return(0);
    });

    // --- SRW locks -------------------------------------------------------------
    auto srwFree = [st](uint32_t lock) {
        auto& l = st->srwLocks[lock];
        return l.exclusiveOwner == 0 && l.shared == 0;
    };
    R("AcquireSRWLockExclusive", 1, [st, srwFree](ShimCall& c) {
        if (srwFree(c.Arg(0))) {
            st->srwLocks[c.Arg(0)].exclusiveOwner = c.thread.id;
            c.mem.Write32(c.Arg(0), 1);
            c.Return(0);
        } else {
            c.Block(100);
        }
    });
    R("TryAcquireSRWLockExclusive", 1, [st, srwFree](ShimCall& c) {
        if (srwFree(c.Arg(0))) {
            st->srwLocks[c.Arg(0)].exclusiveOwner = c.thread.id;
            c.mem.Write32(c.Arg(0), 1);
            c.Return(1);
        } else {
            c.Return(0);
        }
    });
    R("AcquireSRWLockShared", 1, [st](ShimCall& c) {
        auto& l = st->srwLocks[c.Arg(0)];
        if (l.exclusiveOwner == 0) {
            ++l.shared;
            c.mem.Write32(c.Arg(0), 0x11);
            c.Return(0);
        } else {
            c.Block(100);
        }
    });
    R("ReleaseSRWLockExclusive", 1, [st](ShimCall& c) {
        auto& l = st->srwLocks[c.Arg(0)];
        l.exclusiveOwner = 0;
        if (!l.shared) c.mem.Write32(c.Arg(0), 0);
        st->machine.Signal();
        c.Return(0);
    });
    R("ReleaseSRWLockShared", 1, [st](ShimCall& c) {
        auto& l = st->srwLocks[c.Arg(0)];
        if (l.shared) --l.shared;
        if (!l.shared && !l.exclusiveOwner) c.mem.Write32(c.Arg(0), 0);
        st->machine.Signal();
        c.Return(0);
    });

    // --- condition variables -----------------------------------------------------
    // Phase 0: release the lock and register. Phase 1: wait for a wake ticket
    // or the timeout. Phase 2: take the lock back (waitAux holds the CS
    // recursion count, bit 31 records a timeout).
    R("InitializeConditionVariable", 1, [](ShimCall& c) {
        c.mem.Write32(c.Arg(0), 0);
        c.Return(0);
    });
    R("SleepConditionVariableCS", 3, [st](ShimCall& c) {
        const uint32_t cvAddr = c.Arg(0), cs = c.Arg(1), ms = c.Arg(2);
        auto& cv = st->conditionVars[cvAddr];
        GuestThread& t = c.thread;
        if (t.waitPhase == 0) {
            t.waitAux = ReleaseCsFully(c.mem, cs);
            ++cv.waiters;
            t.waitPhase = 1;
            st->machine.Signal();
        }
        if (t.waitPhase == 1) {
            if (cv.pendingSignals) {
                --cv.pendingSignals;
                --cv.waiters;
                t.waitPhase = 2;
            } else if (TimedOut(c, ms)) {
                --cv.waiters;
                t.waitAux |= 0x80000000u;
                t.waitPhase = 2;
            } else {
                c.Block(Recheck(c, ms));
                return;
            }
        }
        if (c.mem.Read32(cs + 12) != 0 && c.mem.Read32(cs + 12) != t.id) {
            c.Block(100);
            return;
        }
        const bool timedOut = (t.waitAux & 0x80000000u) != 0;
        c.mem.Write32(cs + 12, t.id);
        c.mem.Write32(cs + 8, std::max<uint32_t>(1, t.waitAux & 0x7FFFFFFFu));
        c.mem.Write32(cs + 4, 0);
        if (timedOut) {
            c.SetLastError(kErrorTimeout);
            c.Return(0);
        } else {
            c.Return(1);
        }
    });
    R("SleepConditionVariableSRW", 4, [st](ShimCall& c) {
        const uint32_t cvAddr = c.Arg(0), lock = c.Arg(1), ms = c.Arg(2);
        const bool shared = (c.Arg(3) & 1) != 0;
        auto& cv = st->conditionVars[cvAddr];
        GuestThread& t = c.thread;
        if (t.waitPhase == 0) {
            auto& l = st->srwLocks[lock];
            if (shared) {
                if (l.shared) --l.shared;
            } else {
                l.exclusiveOwner = 0;
            }
            if (!l.shared && !l.exclusiveOwner) c.mem.Write32(lock, 0);
            ++cv.waiters;
            t.waitAux = 0;
            t.waitPhase = 1;
            st->machine.Signal();
        }
        if (t.waitPhase == 1) {
            if (cv.pendingSignals) {
                --cv.pendingSignals;
                --cv.waiters;
                t.waitPhase = 2;
            } else if (TimedOut(c, ms)) {
                --cv.waiters;
                t.waitAux = 1;
                t.waitPhase = 2;
            } else {
                c.Block(Recheck(c, ms));
                return;
            }
        }
        auto& l = st->srwLocks[lock];
        if (l.exclusiveOwner || (!shared && l.shared)) {
            c.Block(100);
            return;
        }
        if (shared) ++l.shared;
        else l.exclusiveOwner = t.id;
        c.mem.Write32(lock, 1);
        if (t.waitAux) {
            c.SetLastError(kErrorTimeout);
            c.Return(0);
        } else {
            c.Return(1);
        }
    });
    R("WakeConditionVariable", 1, [st](ShimCall& c) {
        auto& cv = st->conditionVars[c.Arg(0)];
        if (cv.waiters > cv.pendingSignals) ++cv.pendingSignals;
        st->machine.Signal();
        c.Return(0);
    });
    R("WakeAllConditionVariable", 1, [st](ShimCall& c) {
        auto& cv = st->conditionVars[c.Arg(0)];
        cv.pendingSignals = cv.waiters;
        st->machine.Signal();
        c.Return(0);
    });

    // --- InitOnce ----------------------------------------------------------------
    // The INIT_ONCE word: 0 untouched, 1 in progress, 2 done.
    R("InitOnceBeginInitialize", 4, [](ShimCall& c) {
        const uint32_t once = c.Arg(0), pending = c.Arg(2);
        const uint32_t state = c.mem.Read32(once);
        if (state == 0) {
            c.mem.Write32(once, 1);
            if (pending) c.mem.Write32(pending, 1);
            c.Return(1);
        } else if ((state & 3) == 2) {
            if (pending) c.mem.Write32(pending, 0);
            if (c.Arg(3)) c.mem.Write32(c.Arg(3), state & ~3u);
            c.Return(1);
        } else if (c.Arg(1) & 2) {  // INIT_ONCE_CHECK_ONLY
            c.Return(0);
        } else {
            c.Block(50);
        }
    });
    R("InitOnceComplete", 3, [st](ShimCall& c) {
        const uint32_t once = c.Arg(0);
        if (c.Arg(1) & 4) c.mem.Write32(once, 0);  // INIT_ONCE_INIT_FAILED
        else c.mem.Write32(once, (c.Arg(2) & ~3u) | 2);
        st->machine.Signal();
        c.Return(1);
    });

    // --- kernel objects ------------------------------------------------------------
    auto createEvent = [st](ShimCall& c, const std::string& name) {
        if (const uint32_t existing = st->FindNamed(name)) {
            c.SetLastError(Win32::kErrorAlreadyExists);
            c.Return(existing);
            return;
        }
        auto e = std::make_shared<KEvent>();
        e->manual = c.Arg(1) != 0;
        e->signaled = c.Arg(2) != 0;
        e->name = name;
        c.SetLastError(0);
        c.Return(st->Add(e));
    };
    R("CreateEventA", 4, [createEvent](ShimCall& c) { createEvent(c, c.Arg(3) ? c.mem.ReadA(c.Arg(3), 260) : ""); });
    R("CreateEventW", 4, [createEvent](ShimCall& c) { createEvent(c, c.Arg(3) ? c.mem.ReadW(c.Arg(3), 260) : ""); });
    R("SetEvent", 1, [st](ShimCall& c) {
        auto e = st->GetAs<KEvent>(c.Arg(0), KObject::Kind::Event);
        if (!e) {
            c.SetLastError(Win32::kErrorInvalidHandle);
            c.Return(0);
            return;
        }
        e->signaled = true;
        st->machine.Signal();
        c.Return(1);
    });
    R("CreateMutexA", 3, [st](ShimCall& c) {
        const std::string name = c.Arg(2) ? c.mem.ReadA(c.Arg(2), 260) : "";
        if (const uint32_t existing = st->FindNamed(name)) {
            c.SetLastError(Win32::kErrorAlreadyExists);
            c.Return(existing);
            return;
        }
        auto mx = std::make_shared<KMutex>();
        mx->name = name;
        if (c.Arg(1)) {
            mx->owner = c.thread.id;
            mx->recursion = 1;
        }
        c.SetLastError(0);
        c.Return(st->Add(mx));
    });
    R("ReleaseMutex", 1, [st](ShimCall& c) {
        auto mx = st->GetAs<KMutex>(c.Arg(0), KObject::Kind::Mutex);
        if (!mx || mx->owner != c.thread.id) {
            c.SetLastError(288);  // ERROR_NOT_OWNER
            c.Return(0);
            return;
        }
        if (--mx->recursion == 0) mx->owner = 0;
        st->machine.Signal();
        c.Return(1);
    });
    R("CreateWaitableTimerA", 3, [st](ShimCall& c) {
        auto t = std::make_shared<KTimer>();
        t->manual = c.Arg(1) != 0;
        c.Return(st->Add(t));
    });
    R("SetWaitableTimer", 6, [st](ShimCall& c) {
        auto t = st->GetAs<KTimer>(c.Arg(0), KObject::Kind::Timer);
        if (!t) {
            c.SetLastError(Win32::kErrorInvalidHandle);
            c.Return(0);
            return;
        }
        const int64_t due = static_cast<int64_t>(c.mem.Read64(c.Arg(1)));
        const uint64_t now = st->machine.NowMs();
        if (due < 0) {
            t->dueMs = now + static_cast<uint64_t>(-due) / 10000ull;
        } else {
            const uint64_t ft = FileTimeNow();
            t->dueMs = now + (static_cast<uint64_t>(due) > ft ? (static_cast<uint64_t>(due) - ft) / 10000ull : 0);
        }
        t->periodMs = c.Arg(2);
        t->armed = true;
        t->signaled = false;
        st->machine.Signal();
        c.Return(1);
    });

    auto waitOne = [st](ShimCall& c, uint32_t handle, uint32_t ms) {
        if (handle == Win32::kCurrentThread || handle == Win32::kCurrentProcess) {
            if (TimedOut(c, ms)) c.Return(Win32::kWaitTimeout);
            else c.Block(Recheck(c, ms));
            return;
        }
        auto obj = st->Get(handle);
        if (!obj) {
            c.SetLastError(Win32::kErrorInvalidHandle);
            c.Return(Win32::kWaitFailed);
            return;
        }
        if (st->IsSignaled(handle, c.thread.id)) {
            st->Acquire(handle, c.thread.id);
            c.Return(Win32::kWaitObject0);
            return;
        }
        if (ms == 0 || TimedOut(c, ms)) {
            c.Return(Win32::kWaitTimeout);
            return;
        }
        // Socket-bound events and timers have no state-change signal: poll them.
        bool poll = obj->kind == KObject::Kind::Timer;
        if (obj->kind == KObject::Kind::Event) poll = std::static_pointer_cast<KEvent>(obj)->socketFd >= 0;
        c.Block(Recheck(c, ms, poll ? 2 : 1000));
    };
    R("WaitForSingleObject", 2, [waitOne](ShimCall& c) { waitOne(c, c.Arg(0), c.Arg(1)); });
    R("WaitForSingleObjectEx", 3, [waitOne](ShimCall& c) { waitOne(c, c.Arg(0), c.Arg(1)); });
    auto sleep = [](ShimCall& c, uint32_t ms) {
        if (ms == 0) {
            // Yield once.
            if (c.thread.waitPhase == 0) {
                c.thread.waitPhase = 1;
                c.Block(0);
            } else {
                c.Return(0);
            }
            return;
        }
        if (TimedOut(c, ms)) c.Return(0);
        else c.Block(Recheck(c, ms, 0xFFFFFFFFu));
    };
    R("Sleep", 1, [sleep](ShimCall& c) { sleep(c, c.Arg(0)); });
    R("SleepEx", 2, [sleep](ShimCall& c) { sleep(c, c.Arg(0)); });

    // --- threads -----------------------------------------------------------------
    R("CreateThread", 6, [st](ShimCall& c) {
        const bool suspended = (c.Arg(4) & 4) != 0;
        GuestThread* t = st->machine.CreateThread(c.Arg(2), c.Arg(3), suspended, "win32");
        if (!t) {
            c.SetLastError(Win32::kErrorNotEnoughMemory);
            c.Return(0);
            return;
        }
        if (c.Arg(5)) c.mem.Write32(c.Arg(5), t->id);
        NETEMU_LOGI("CreateThread start=0x%08X param=0x%08X -> tid %u", c.Arg(2), c.Arg(3), t->id);
        c.Return(st->ThreadHandle(t));
    });
    auto threadOf = [st](ShimCall& c, uint32_t h) -> GuestThread* {
        if (h == Win32::kCurrentThread) return &c.thread;
        auto k = st->GetAs<KThread>(h, KObject::Kind::Thread);
        return k ? st->machine.ThreadById(k->tid) : nullptr;
    };
    R("ExitThread", 1, [](ShimCall& c) { c.Exit(c.Arg(0)); });
    // Priorities are recorded and read back; the cooperative scheduler is
    // round-robin, as fair as Windows for threads that all sleep or wait.
    auto withThread = [threadOf](ShimCall& c, uint32_t h, auto fn) {
        GuestThread* t = threadOf(c, h);
        if (!t) {
            c.SetLastError(Win32::kErrorInvalidHandle);
            return false;
        }
        fn(*t);
        return true;
    };
    R("GetThreadPriority", 1, [withThread](ShimCall& c) {
        int32_t p = 0x7FFFFFFF;  // THREAD_PRIORITY_ERROR_RETURN
        withThread(c, c.Arg(0), [&](GuestThread& t) { p = t.priority; });
        c.Return(static_cast<uint32_t>(p));
    });
    R("SetThreadPriority", 2, [withThread](ShimCall& c) {
        const int32_t p = static_cast<int32_t>(c.Arg(1));
        // -7..6 are valid in REALTIME class; the background-mode switches are
        // accepted and change nothing here.
        const bool background = p == 0x10000 || p == 0x20000;  // THREAD_MODE_BACKGROUND_BEGIN/END
        if (background) {
            c.Return(1);
            return;
        }
        const bool valid = (p >= -7 && p <= 6) || p == -15 || p == 15;
        if (!valid) {
            c.SetLastError(Win32::kErrorInvalidParameter);
            c.Return(0);
            return;
        }
        c.Return(withThread(c, c.Arg(0), [&](GuestThread& t) { t.priority = p; }) ? 1 : 0);
    });
    R("GetThreadPriorityBoost", 2, [withThread](ShimCall& c) {
        c.Return(withThread(c, c.Arg(0), [&](GuestThread& t) {
            if (c.Arg(1)) c.mem.Write32(c.Arg(1), t.priorityBoostOff ? 1 : 0);
        }) ? 1 : 0);
    });
    R("SetThreadPriorityBoost", 2, [withThread](ShimCall& c) {
        c.Return(withThread(c, c.Arg(0), [&](GuestThread& t) { t.priorityBoostOff = c.Arg(1) != 0; }) ? 1 : 0);
    });
    R("GetThreadId", 1, [withThread](ShimCall& c) {
        uint32_t id = 0;
        withThread(c, c.Arg(0), [&](GuestThread& t) { id = t.id; });
        c.Return(id);
    });
    R("GetProcessIdOfThread", 1, [withThread](ShimCall& c) {
        c.Return(withThread(c, c.Arg(0), [](GuestThread&) {}) ? Win32State::kSelfPid : 0);
    });
    R("SetThreadIdealProcessor", 2, [withThread](ShimCall& c) {
        c.Return(withThread(c, c.Arg(0), [](GuestThread&) {}) ? 0u : 0xFFFFFFFFu);  // previous ideal: CPU 0
    });
    R("GetThreadIOPendingFlag", 2, [withThread](ShimCall& c) {
        c.Return(withThread(c, c.Arg(0), [&](GuestThread&) {
            if (c.Arg(1)) c.mem.Write32(c.Arg(1), 0);
        }) ? 1 : 0);
    });
    // Yields like Sleep(0), but reports that another thread got to run.
    R("SwitchToThread", 0, [sleep](ShimCall& c) {
        sleep(c, 0);
        if (c.outcome == ShimCall::Outcome::Return) c.outEax = 1;
    });
    R("GetPriorityClass", 1, [st](ShimCall& c) {
        if (st->ProcessPid(c.Arg(0)) != Win32State::kSelfPid) {
            c.SetLastError(Win32::kErrorAccessDenied);
            c.Return(0);
            return;
        }
        c.Return(st->priorityClass);
    });
    R("SetPriorityClass", 2, [st](ShimCall& c) {
        // The priority classes, plus PROCESS_MODE_BACKGROUND_BEGIN/END.
        if (c.Arg(1) == 0x100000 || c.Arg(1) == 0x200000) {
            c.Return(st->ProcessPid(c.Arg(0)) == Win32State::kSelfPid ? 1 : 0);
            return;
        }
        static const uint32_t kClasses[] = {0x20, 0x40, 0x80, 0x100, 0x4000, 0x8000};
        const bool valid = std::find(std::begin(kClasses), std::end(kClasses), c.Arg(1)) != std::end(kClasses);
        if (st->ProcessPid(c.Arg(0)) != Win32State::kSelfPid || !valid) {
            c.SetLastError(valid ? Win32::kErrorAccessDenied : Win32::kErrorInvalidParameter);
            c.Return(0);
            return;
        }
        st->priorityClass = c.Arg(1);
        c.Return(1);
    });
    R("GetExitCodeThread", 2, [threadOf](ShimCall& c) {
        GuestThread* t = threadOf(c, c.Arg(0));
        if (!t || !c.Arg(1)) {
            c.SetLastError(Win32::kErrorInvalidHandle);
            c.Return(0);
            return;
        }
        c.mem.Write32(c.Arg(1), t->state == GuestThread::State::Exited ? t->exitCode : 259u);  // STILL_ACTIVE
        c.Return(1);
    });
    R("ResumeThread", 1, [st, threadOf](ShimCall& c) {
        GuestThread* t = threadOf(c, c.Arg(0));
        if (!t) {
            c.Return(0xFFFFFFFFu);
            return;
        }
        const uint32_t prev = t->suspendCount;
        st->machine.ResumeThread(t);
        c.Return(prev);
    });
    R("SuspendThread", 1, [threadOf](ShimCall& c) {
        GuestThread* t = threadOf(c, c.Arg(0));
        if (!t || t == &c.thread) {
            // Suspending yourself would never resume in this model.
            c.Return(t ? 0 : 0xFFFFFFFFu);
            return;
        }
        c.Return(t->suspendCount++);
    });
    R("OpenThread", 3, [st](ShimCall& c) {
        GuestThread* t = st->machine.ThreadById(c.Arg(2));
        if (!t) {
            c.SetLastError(Win32::kErrorInvalidParameter);
            c.Return(0);
            return;
        }
        auto k = std::make_shared<KThread>();
        k->tid = t->id;
        c.Return(st->Add(k));
    });
    R("TerminateThread", 2, [st, threadOf](ShimCall& c) {
        GuestThread* t = threadOf(c, c.Arg(0));
        if (!t) {
            c.Return(0);
            return;
        }
        if (t == &c.thread) {
            c.Exit(c.Arg(1));
            return;
        }
        t->exitCode = c.Arg(1);
        t->state = GuestThread::State::Exited;
        st->machine.Signal();
        c.Return(1);
    });

    // --- fiber local storage -----------------------------------------------------
    R("FlsAlloc", 1, [st](ShimCall& c) {
        for (uint32_t i = 1; i < 128; ++i) {
            if (i >= st->flsUsed.size()) {
                st->flsUsed.resize(i + 1, false);
                st->flsCallbacks.resize(i + 1, 0);
            }
            if (!st->flsUsed[i]) {
                st->flsUsed[i] = true;
                st->flsCallbacks[i] = c.Arg(0);
                c.Return(i);
                return;
            }
        }
        c.Return(0xFFFFFFFFu);
    });
    R("FlsFree", 1, [st](ShimCall& c) {
        if (c.Arg(0) < st->flsUsed.size()) st->flsUsed[c.Arg(0)] = false;
        c.Return(1);
    });
    R("FlsGetValue", 1, [](ShimCall& c) {
        auto it = c.thread.fls.find(c.Arg(0));
        c.SetLastError(0);
        c.Return(it == c.thread.fls.end() ? 0 : it->second);
    });
    R("FlsSetValue", 2, [](ShimCall& c) {
        c.thread.fls[c.Arg(0)] = c.Arg(1);
        c.Return(1);
    });

    // --- structured exceptions -----------------------------------------------------
    // Dispatch follows RtlDispatchException: vectored handlers first, then the
    // fs:[0] frame chain. A handler that returns "continue execution" resumes
    // the thread from the (possibly edited) CONTEXT; MSVC's C++ and __except
    // handlers never return, they unwind through RtlUnwind into the catch
    // block. The record is 0x50 bytes at rec, EXCEPTION_POINTERS {rec, ctx}
    // sits at rec+0x50 and the CONTEXT at ctx.
    auto dispatch = [st](ShimCall& c, uint32_t rec, uint32_t ctx, uint32_t reserve) {
        const uint32_t code = c.mem.Read32(rec);
        const uint32_t pointers = rec + 0x50;
        c.mem.Write32(pointers, rec);
        c.mem.Write32(pointers + 4, ctx);
        auto resume = [ctx](ShimCall& r) {
            ShimCall::Regs regs{};
            regs.edi = r.mem.Read32(ctx + 0x9C);
            regs.esi = r.mem.Read32(ctx + 0xA0);
            regs.ebx = r.mem.Read32(ctx + 0xA4);
            regs.edx = r.mem.Read32(ctx + 0xA8);
            regs.ecx = r.mem.Read32(ctx + 0xAC);
            regs.eax = r.mem.Read32(ctx + 0xB0);
            regs.ebp = r.mem.Read32(ctx + 0xB4);
            regs.eip = r.mem.Read32(ctx + 0xB8);
            regs.eflags = r.mem.Read32(ctx + 0xC0);
            regs.esp = r.mem.Read32(ctx + 0xC4);
            r.thread.resumedEip = regs.eip;
            r.thread.resumedEsp = regs.esp;
            r.Resume(regs);
        };
        auto vehs = std::make_shared<std::vector<uint32_t>>(st->vectoredHandlers);
        auto vehIndex = std::make_shared<size_t>(0);
        auto frame = std::make_shared<uint32_t>(c.mem.Read32(c.thread.teb));
        // The walker holds itself weakly; only a pending continuation keeps it
        // alive, so it is freed once the exception is resumed or unwound.
        auto next = std::make_shared<ShimFn>();
        std::weak_ptr<ShimFn> weakNext = next;
        *next = [weakNext, vehs, vehIndex, frame, rec, ctx, code, pointers, reserve, resume](ShimCall& k) {
            auto next = weakNext.lock();
            if (*vehIndex < vehs->size()) {
                const uint32_t handler = (*vehs)[(*vehIndex)++];
                k.Call(handler, {pointers}, 0,
                       [next, resume](ShimCall& r) {
                           if (r.result == 0xFFFFFFFFu) return resume(r);  // EXCEPTION_CONTINUE_EXECUTION
                           (*next)(r);
                       },
                       reserve);
                return;
            }
            if (*frame == 0xFFFFFFFFu || *frame == 0 || !k.mem.Valid(*frame, 8)) {
                NETEMU_LOGE("unhandled guest exception 0x%08X at 0x%08X on thread %u", code, k.mem.Read32(rec + 12),
                            k.thread.id);
                k.Exit(code);
                return;
            }
            const uint32_t handler = k.mem.Read32(*frame + 4);
            const uint32_t current = *frame;
            *frame = k.mem.Read32(current);
            NETEMU_TRACE("SEH frame 0x%08X handler 0x%08X for 0x%08X", current, handler, code);
            k.Call(handler, {rec, current, ctx, 0}, 0,
                   [next, resume](ShimCall& r) {
                       NETEMU_TRACE("SEH handler returned %u", r.result);
                       if (r.result == 0) return resume(r);  // ExceptionContinueExecution
                       (*next)(r);                           // continue search (nested/collided too)
                   },
                   reserve);
        };
        (*next)(c);
    };

    // RaiseException builds the record and CONTEXT below the caller's stack.
    R("RaiseException", 4, [dispatch](ShimCall& c) {
        const uint32_t code = c.Arg(0);
        const uint32_t retAddr = c.ReturnAddress();
        const uint32_t rec = (c.esp - 0x400) & ~15u;
        const uint32_t ctx = rec + 0x60;
        const uint32_t nparams = std::min<uint32_t>(c.Arg(2), 15);
        c.mem.Fill(rec, 0, 0x50);
        c.mem.Write32(rec + 0, code);
        c.mem.Write32(rec + 4, c.Arg(1) & 1);
        c.mem.Write32(rec + 12, retAddr);
        c.mem.Write32(rec + 16, nparams);
        for (uint32_t i = 0; i < nparams; ++i) c.mem.Write32(rec + 20 + i * 4, c.mem.Read32(c.Arg(3) + i * 4));
        c.mem.Fill(ctx, 0, Machine::kContextSize);
        c.mem.Write32(ctx + 0x00, 0x10007);
        c.mem.Write32(ctx + 0xB8, retAddr);
        c.mem.Write32(ctx + 0xC4, c.esp + 4 + 16);
        c.mem.Write32(ctx + 0xC0, 0x202);
        if (code != 0xE06D7363u && code != 0x406D1388u)  // C++ throw, thread naming
            NETEMU_LOGW("RaiseException 0x%08X from 0x%08X on thread %u", code, retAddr, c.thread.id);
        if (code == 0x406D1388u) {  // MS_VC_EXCEPTION thread name: nobody handles it
            c.Return(0);
            return;
        }
        dispatch(c, rec, ctx, 0x500);
    });
    // Hardware faults (Machine::RaiseHardwareException) enter here with the
    // record and CONTEXT already on the stack: [esp] 0, [esp+4] rec, [esp+8] ctx.
    m.SetExceptionDispatcher(m.MakeThunk("<KiUserExceptionDispatcher>", 0, [dispatch](ShimCall& c) {
        dispatch(c, c.Arg(0), c.Arg(1), 0);
    }));
    R("RtlUnwind", 4, [](ShimCall& c) {
        const uint32_t target = c.Arg(0);
        uint32_t rec = c.Arg(2);
        if (!rec) {
            rec = (c.esp - 0x200) & ~15u;
            c.mem.Fill(rec, 0, 0x50);
            c.mem.Write32(rec, 0xC0000027u);  // STATUS_UNWIND
        }
        c.mem.Write32(rec + 4, c.mem.Read32(rec + 4) | 2);  // EXCEPTION_UNWINDING
        if (!target) c.mem.Write32(rec + 4, c.mem.Read32(rec + 4) | 4);  // EXCEPTION_EXIT_UNWIND
        auto step = std::make_shared<ShimFn>();
        std::weak_ptr<ShimFn> weakStep = step;  // freed with the last continuation, as in dispatch
        *step = [weakStep, target, rec](ShimCall& k) {
            auto step = weakStep.lock();
            const uint32_t frame = k.mem.Read32(k.thread.teb);
            if (frame == target || frame == 0xFFFFFFFFu || frame == 0 || !k.mem.Valid(frame, 8)) {
                k.Return(0);
                return;
            }
            const uint32_t handler = k.mem.Read32(frame + 4);
            k.mem.Write32(k.thread.teb, k.mem.Read32(frame));
            k.Call(handler, {rec, frame, 0, 0}, 0, [step](ShimCall& r) { (*step)(r); }, 0x100);
        };
        (*step)(c);
    });

    // --- pthreads-win32 (cdecl) ------------------------------------------------------
    P("pthread_create", [st](ShimCall& c) {
        GuestThread* t = st->machine.CreateThread(c.Arg(2), c.Arg(3), false, "pthread");
        if (!t) {
            c.Return(11);  // EAGAIN
            return;
        }
        const uint32_t h = st->ThreadHandle(t);
        // pthread_t is { void* p; unsigned x; }.
        c.mem.Write32(c.Arg(0), h);
        c.mem.Write32(c.Arg(0) + 4, 0);
        NETEMU_LOGI("pthread_create start=0x%08X arg=0x%08X -> tid %u", c.Arg(2), c.Arg(3), t->id);
        c.Return(0);
    });
    P("pthread_cancel", [st](ShimCall& c) {
        auto k = st->GetAs<KThread>(c.Arg(0), KObject::Kind::Thread);
        GuestThread* t = k ? st->machine.ThreadById(k->tid) : nullptr;
        if (!t) {
            c.Return(3);  // ESRCH
            return;
        }
        t->cancelRequested = true;
        st->machine.Signal();
        c.Return(0);
    });
    P("pthread_setcancelstate", [](ShimCall& c) {
        if (c.Arg(1)) c.mem.Write32(c.Arg(1), 0);
        c.Return(0);
    });
    P("pthread_testcancel", [](ShimCall& c) {
        if (c.thread.cancelRequested) c.Exit(kCanceled);
        else c.Return(0);
    });
    auto pmutexId = [st](ShimCall& c, uint32_t ptr) -> uint32_t {
        uint32_t id = c.mem.Read32(ptr);
        if (IsStaticInit(id)) {
            id = ++st->nextPObject;
            c.mem.Write32(ptr, id);
            st->pmutexes[id].recursive = true;
        }
        return id;
    };
    P("pthread_mutex_init", [st](ShimCall& c) {
        const uint32_t id = ++st->nextPObject;
        st->pmutexes[id].recursive = true;
        c.mem.Write32(c.Arg(0), id);
        c.Return(0);
    });
    P("pthread_mutex_destroy", [st](ShimCall& c) {
        const uint32_t id = c.mem.Read32(c.Arg(0));
        auto it = st->pmutexes.find(id);
        if (it != st->pmutexes.end() && it->second.owner) {
            c.Return(kEbusy);
            return;
        }
        st->pmutexes.erase(id);
        c.mem.Write32(c.Arg(0), 0);
        c.Return(0);
    });
    P("pthread_mutex_lock", [st, pmutexId](ShimCall& c) {
        const uint32_t id = pmutexId(c, c.Arg(0));
        auto it = st->pmutexes.find(id);
        if (it == st->pmutexes.end()) {
            c.Return(kEinval);
            return;
        }
        auto& mx = it->second;
        if (mx.owner == 0) {
            mx.owner = c.thread.id;
            mx.recursion = 1;
            c.Return(0);
        } else if (mx.owner == c.thread.id) {
            ++mx.recursion;
            c.Return(0);
        } else {
            c.Block(100);
        }
    });
    P("pthread_mutex_unlock", [st](ShimCall& c) {
        auto it = st->pmutexes.find(c.mem.Read32(c.Arg(0)));
        if (it == st->pmutexes.end() || it->second.owner != c.thread.id) {
            c.Return(1);  // EPERM
            return;
        }
        if (--it->second.recursion == 0) it->second.owner = 0;
        st->machine.Signal();
        c.Return(0);
    });
    auto pcondId = [st](ShimCall& c, uint32_t ptr) -> uint32_t {
        uint32_t id = c.mem.Read32(ptr);
        if (IsStaticInit(id) || !st->pconds.count(id)) {
            if (!IsStaticInit(id) && id) return id;
            id = ++st->nextPObject;
            c.mem.Write32(ptr, id);
            st->pconds[id];
        }
        return id;
    };
    P("pthread_cond_init", [st](ShimCall& c) {
        const uint32_t id = ++st->nextPObject;
        st->pconds[id];
        c.mem.Write32(c.Arg(0), id);
        c.Return(0);
    });
    P("pthread_cond_destroy", [st](ShimCall& c) {
        st->pconds.erase(c.mem.Read32(c.Arg(0)));
        c.mem.Write32(c.Arg(0), 0);
        c.Return(0);
    });
    P("pthread_cond_signal", [st, pcondId](ShimCall& c) {
        auto& cv = st->pconds[pcondId(c, c.Arg(0))];
        if (cv.waiters > cv.pendingSignals) ++cv.pendingSignals;
        st->machine.Signal();
        c.Return(0);
    });
    // Shared by cond_wait and cond_timedwait. deadlineMs is machine time or ~0.
    auto condWait = [st, pcondId, pmutexId](ShimCall& c, uint64_t deadlineMs) {
        GuestThread& t = c.thread;
        const uint32_t condId = pcondId(c, c.Arg(0));
        const uint32_t mutexId = pmutexId(c, c.Arg(1));
        auto& cv = st->pconds[condId];
        auto& mx = st->pmutexes[mutexId];
        if (t.waitPhase == 0) {
            if (mx.owner != t.id) {
                c.Return(1);  // EPERM
                return;
            }
            t.waitAux = mx.recursion;
            mx.owner = 0;
            mx.recursion = 0;
            ++cv.waiters;
            t.waitPhase = 1;
            st->machine.Signal();
        }
        if (t.waitPhase == 1) {
            const uint64_t now = st->machine.NowMs();
            if (t.cancelRequested) {
                --cv.waiters;
                t.waitAux |= 0x40000000u;
                t.waitPhase = 2;
            } else if (cv.pendingSignals) {
                --cv.pendingSignals;
                --cv.waiters;
                t.waitPhase = 2;
            } else if (now >= deadlineMs) {
                --cv.waiters;
                t.waitAux |= 0x80000000u;
                t.waitPhase = 2;
            } else {
                c.Block(static_cast<uint32_t>(std::min<uint64_t>(deadlineMs - now, 1000)));
                return;
            }
        }
        if (mx.owner && mx.owner != t.id) {
            c.Block(100);
            return;
        }
        mx.owner = t.id;
        mx.recursion = std::max<uint32_t>(1, t.waitAux & 0x3FFFFFFFu);
        if (t.waitAux & 0x40000000u) {
            // Cancellation point: the real library runs cleanup handlers and
            // exits with the mutex held.
            c.Exit(kCanceled);
            return;
        }
        c.Return((t.waitAux & 0x80000000u) ? kEtimedout : 0);
    };
    P("pthread_cond_wait", [condWait](ShimCall& c) { condWait(c, ~0ull); });
    P("pthread_cond_timedwait", [st, condWait](ShimCall& c) {
        // struct timespec is { time_t tv_sec; long tv_nsec } with a 64-bit
        // time_t, or { long; long } with _USE_32BIT_TIME_T. A zero second
        // word means the 64-bit layout (seconds fit in 32 bits until 2106).
        GuestThread& t = c.thread;
        if (t.waitPhase == 0) {
            const uint32_t ts = c.Arg(2);
            const uint32_t a = c.mem.Read32(ts), b = c.mem.Read32(ts + 4), d = c.mem.Read32(ts + 8);
            uint64_t sec = a, nsec = b;
            if (b == 0) nsec = d;
            if (nsec >= 1000000000ull) nsec = 0;
            const uint64_t absMs = sec * 1000ull + nsec / 1000000ull;
            const uint64_t nowUnix = UnixMsNow();
            t.waitDeadline = st->machine.NowMs() + (absMs > nowUnix ? absMs - nowUnix : 0);
        }
        condWait(c, t.waitDeadline);
    });
}

}  // namespace NetEmu
