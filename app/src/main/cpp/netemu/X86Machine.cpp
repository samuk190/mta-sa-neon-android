#include "X86Machine.h"

#include "NetEmuLog.h"

#include <unicorn/unicorn.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cinttypes>
#include <cstdlib>
#include <future>

namespace NetEmu {

namespace {
constexpr uint32_t kNeverAddr = 0xFFFFFFF0u;
constexpr uint32_t kSliceUs = 4000;
constexpr uint32_t kStackSize = 0x00100000u;  // 1 MiB like MSVC's default reserve
constexpr uint32_t kDataSelector = 2u << 3;
constexpr uint32_t kFirstFsIndex = 16;
constexpr uint32_t kGdtEntries = 0x10000u / 8;

uint64_t Descriptor(uint32_t base, uint32_t limit, uint8_t access, uint8_t flags) {
    uint64_t d = limit & 0xFFFFu;
    d |= static_cast<uint64_t>(base & 0xFFFFFFu) << 16;
    d |= static_cast<uint64_t>(access) << 40;
    d |= static_cast<uint64_t>((limit >> 16) & 0xFu) << 48;
    d |= static_cast<uint64_t>(flags & 0xFu) << 52;
    d |= static_cast<uint64_t>(base >> 24) << 56;
    return d;
}

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

uint32_t Reg(uc_engine* uc, int id) {
    uint32_t v = 0;
    uc_reg_read(uc, id, &v);
    return v;
}
void SetReg(uc_engine* uc, int id, uint32_t v) { uc_reg_write(uc, id, &v); }

void CodeHook(uc_engine*, uint64_t addr, uint32_t, void* user) {
    static_cast<Machine*>(user)->OnThunk(static_cast<uint32_t>(addr));
}
void IntrHook(uc_engine*, uint32_t intno, void* user) { static_cast<Machine*>(user)->OnInterrupt(intno); }
bool MemHook(uc_engine*, uc_mem_type type, uint64_t addr, int size, int64_t, void* user) {
    static_cast<Machine*>(user)->OnInvalidMemory(static_cast<int>(type), addr, size);
    return false;
}
void WatchHook(uc_engine* uc, uc_mem_type, uint64_t addr, int size, int64_t value, void*) {
    NETEMU_LOGI("watch: write%d 0x%08" PRIX64 " = 0x%08" PRIX64 " at EIP 0x%08X", size * 8, addr,
                static_cast<uint64_t>(value), Reg(uc, UC_X86_REG_EIP));
}
void ThunkWriteHook(uc_engine* uc, uc_mem_type, uint64_t addr, int size, int64_t value, void* user) {
    static_cast<Machine*>(user)->OnThunkWrite(static_cast<uint32_t>(addr), size, static_cast<uint64_t>(value), Reg(uc, UC_X86_REG_EIP));
}
void TraceCallHook(uc_engine* uc, uint64_t addr, uint32_t, void* user) {
    GuestMemory& mem = static_cast<Machine*>(user)->Mem();
    const uint32_t esp = Reg(uc, UC_X86_REG_ESP);
    const uint32_t arg = mem.Read32(esp + 4);
    const std::string str = mem.Valid(arg, 1) ? mem.ReadA(arg, 64) : std::string();
    NETEMU_LOGI("call 0x%08" PRIX64 " from 0x%08X arg0=0x%08X \"%s\" ecx=0x%08X", addr, mem.Read32(esp), arg, str.c_str(),
                Reg(uc, UC_X86_REG_ECX));
}
uint32_t PortInHook(uc_engine* uc, uint32_t, int, void* user) {
    static_cast<Machine*>(user)->OnPrivilegedInsn();
    return Reg(uc, UC_X86_REG_EAX);  // the instruction is discarded; keep EAX as it was
}
void PortOutHook(uc_engine*, uint32_t, int, uint32_t, void* user) { static_cast<Machine*>(user)->OnPrivilegedInsn(); }
bool InsnHook(uc_engine*, void* user) {
    static_cast<Machine*>(user)->OnInvalidInsn();
    return false;
}

}  // namespace

// ---------------------------------------------------------------------------
// ShimCall

ShimCall::ShimCall(Machine& m, GuestThread& t, const ShimDef& d, uint32_t e)
    : machine(m), thread(t), mem(m.Mem()), def(d), esp(e) {}

uint32_t ShimCall::Arg(int i) const { return mem.Read32(esp + 4 + static_cast<uint32_t>(i) * 4); }
uint64_t ShimCall::Arg64(int i) const { return static_cast<uint64_t>(Arg(i)) | (static_cast<uint64_t>(Arg(i + 1)) << 32); }
uint32_t ShimCall::ReturnAddress() const { return mem.Read32(esp); }

void ShimCall::Return(uint32_t eax, uint32_t edx) {
    outcome = Outcome::Return;
    outEax = eax;
    outEdx = edx;
    decided = true;
}
void ShimCall::Block(uint32_t recheckMs) {
    outcome = Outcome::Block;
    blockMs = recheckMs;
    decided = true;
}
void ShimCall::Call(uint32_t fn, const std::vector<uint32_t>& args, uint32_t ecx, ShimFn continuation, uint32_t reserveBytes) {
    outcome = Outcome::Call;
    callReserve = reserveBytes;
    callFn = fn;
    callArgs = args;
    callEcx = ecx;
    callCont = std::move(continuation);
    decided = true;
}
void ShimCall::Resume(const Regs& regs) {
    outcome = Outcome::Resume;
    resumeRegs = regs;
    decided = true;
}
void ShimCall::Jump(uint32_t eip, uint32_t newEsp) {
    outcome = Outcome::Jump;
    jumpEip = eip;
    jumpEsp = newEsp;
    decided = true;
}
void ShimCall::Exit(uint32_t code) {
    outcome = Outcome::Exit;
    outEax = code;
    decided = true;
}
uint64_t ShimCall::WaitElapsedMs() const {
    return thread.waitActive ? machine.NowMs() - thread.waitStartMs : 0;
}
void ShimCall::SetLastError(uint32_t code) { mem.Write32(thread.teb + 0x34, code); }

// ---------------------------------------------------------------------------
// Machine

Machine::Machine() {
    startUs_ = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

Machine::~Machine() {
    Stop();
    for (auto& t : threads_)
        if (t->ctx) uc_context_free(t->ctx);
    if (baseline_) uc_context_free(baseline_);
    if (uc_) uc_close(uc_);
}

uint64_t Machine::NowUs() const {
    const uint64_t now = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
    return now - startUs_;
}
uint64_t Machine::NowMs() const { return NowUs() / 1000; }

bool Machine::Init(std::string* error) {
    if (const char* trace = std::getenv("NETEMU_TRACE_SHIMS")) traceShims_ = trace[0] == '1';
    if (!mem_.Init(error)) return false;
    if (uc_open(UC_ARCH_X86, UC_MODE_32, &uc_) != UC_ERR_OK) {
        if (error) *error = "uc_open failed";
        return false;
    }
    const uc_err mapErr = uc_mem_map_ptr(uc_, Map::kNullGuard, Map::kArenaSize - Map::kNullGuard, UC_PROT_ALL,
                                         mem_.Arena() + Map::kNullGuard);
    if (mapErr != UC_ERR_OK) {
        if (error) *error = std::string("uc_mem_map_ptr: ") + uc_strerror(mapErr);
        return false;
    }
    // Thunk bodies (see Map::kThunkDispatch); the code hook runs the shim.
    static constexpr uint8_t kThunkBody[Map::kThunkStride] = {0x8B, 0xFF, 0x55, 0x8B, 0xEC, 0x5D, 0xC3, 0xCC,
                                                              0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC};
    for (uint32_t va = Map::kThunkBegin; va < Map::kThunkEnd; va += Map::kThunkStride) mem_.WriteBytes(va, kThunkBody, sizeof(kThunkBody));
    if (!SetupCpu(error)) return false;

    uc_hook h;
    uc_hook_add(uc_, &h, UC_HOOK_CODE, reinterpret_cast<void*>(CodeHook), this, Map::kThunkBegin, Map::kThunkEnd - 1);
    uc_hook_add(uc_, &h, UC_HOOK_INTR, reinterpret_cast<void*>(IntrHook), this, 1, 0);
    uc_hook_add(uc_, &h, UC_HOOK_MEM_INVALID, reinterpret_cast<void*>(MemHook), this, 1, 0);
    uc_hook_add(uc_, &h, UC_HOOK_INSN_INVALID, reinterpret_cast<void*>(InsnHook), this, 1, 0);
    // User-mode port I/O is privileged on Windows (IOPL 0): it raises #GP.
    // VM probes (VMware's backdoor port) rely on exactly that.
    uc_hook_add(uc_, &h, UC_HOOK_INSN, reinterpret_cast<void*>(PortInHook), this, 1, 0, UC_X86_INS_IN);
    uc_hook_add(uc_, &h, UC_HOOK_INSN, reinterpret_cast<void*>(PortOutHook), this, 1, 0, UC_X86_INS_OUT);
    // Debug aid: NETEMU_WATCH_THUNKS=1 logs guest writes into the thunk area
    // (inline hooks). Off by default: a write hook can slow every guest store.
    if (const char* w = std::getenv("NETEMU_WATCH_THUNKS"); w && w[0] == '1')
        uc_hook_add(uc_, &h, UC_HOOK_MEM_WRITE, reinterpret_cast<void*>(ThunkWriteHook), this, Map::kThunkBegin, Map::kThunkEnd - 1);
    // Debug aid: NETEMU_TRACE_CALL=0xADDR logs each entry with its first stack
    // argument (and that argument as a string when it points at one).
    if (const char* at = std::getenv("NETEMU_TRACE_CALL")) {
        const uint32_t va = static_cast<uint32_t>(std::strtoul(at, nullptr, 0));
        if (va) uc_hook_add(uc_, &h, UC_HOOK_CODE, reinterpret_cast<void*>(TraceCallHook), this, va, va);
    }
    // Debug aid: NETEMU_WATCH=0xADDR logs every guest write to that dword.
    if (const char* watch = std::getenv("NETEMU_WATCH")) {
        const uint32_t va = static_cast<uint32_t>(std::strtoul(watch, nullptr, 0));
        if (va) uc_hook_add(uc_, &h, UC_HOOK_MEM_WRITE, reinterpret_cast<void*>(WatchHook), this, va, va + 3);
    }

    retHostThunk_ = MakeThunk("<return-to-host>", 0, nullptr);
    contThunk_ = MakeThunk("<continuation>", 0, nullptr);
    threadExitThunk_ = MakeThunk("<thread-exit>", 0, nullptr);
    threadStartThunk_ = MakeThunk("<thread-start>", 0, [this](ShimCall& c) { ThreadStartShim(c); });

    // PEB with an empty loader list, a process heap header and process parameters.
    peb_ = mem_.StaticAlloc(0x1000, 0x1000);
    const uint32_t ldr = peb_ + 0x200;
    mem_.Write32(ldr + 0x00, 0x30);
    mem_.Write8(ldr + 0x04, 1);
    for (uint32_t off : {0x0Cu, 0x14u, 0x1Cu}) {
        mem_.Write32(ldr + off, ldr + off);
        mem_.Write32(ldr + off + 4, ldr + off);
    }
    const uint32_t heap = mem_.StaticAlloc(0x1000, 0x1000);
    mem_.Write32(heap + 0x40, 2);  // HEAP_GROWABLE, no debug flags
    mem_.Write32(heap + 0x44, 0);
    const uint32_t params = peb_ + 0x400;
    const std::string image = "C:\\Program Files (x86)\\Rockstar Games\\GTA San Andreas\\gta_sa.exe";
    const uint32_t imageW = mem_.StaticAlloc(static_cast<uint32_t>(image.size() * 2 + 2));
    mem_.WriteW(imageW, image, image.size() + 1);
    mem_.Write16(params + 0x38, static_cast<uint16_t>(image.size() * 2));
    mem_.Write16(params + 0x3A, static_cast<uint16_t>(image.size() * 2 + 2));
    mem_.Write32(params + 0x3C, imageW);
    mem_.Write16(params + 0x40, static_cast<uint16_t>(image.size() * 2));
    mem_.Write16(params + 0x42, static_cast<uint16_t>(image.size() * 2 + 2));
    mem_.Write32(params + 0x44, imageW);
    mem_.Write8(peb_ + 0x02, 0);  // BeingDebugged
    mem_.Write32(peb_ + 0x08, 0x00400000u);
    mem_.Write32(peb_ + 0x0C, ldr);
    mem_.Write32(peb_ + 0x10, params);
    mem_.Write32(peb_ + 0x18, heap);
    mem_.Write32(peb_ + 0x64, 4);       // NumberOfProcessors
    mem_.Write32(peb_ + 0x68, 0);       // NtGlobalFlag
    mem_.Write32(peb_ + 0xA4, 10);      // OSMajorVersion
    mem_.Write32(peb_ + 0xA8, 0);
    mem_.Write16(peb_ + 0xAC, 19045);   // Windows 10 22H2
    mem_.Write32(peb_ + 0xB0, 2);       // VER_PLATFORM_WIN32_NT

    mem_.Write32(Map::kSharedUserData + 0x04, 0x0FA00000u);  // TickCountMultiplier
    mem_.Write32(Map::kSharedUserData + 0x26C, 10);
    mem_.Write32(Map::kSharedUserData + 0x270, 0);
    UpdateSharedUserData();

    main_ = CreateThread(0, 0, false, "main");
    if (!main_) {
        if (error) *error = "main thread setup failed";
        return false;
    }
    main_->state = GuestThread::State::Idle;
    return true;
}

bool Machine::SetupCpu(std::string* error) {
    mem_.Write64(Map::kGdt + 8 * 1, Descriptor(0, 0xFFFFF, 0x9B, 0xC));
    mem_.Write64(Map::kGdt + 8 * 2, Descriptor(0, 0xFFFFF, 0x93, 0xC));
    uc_x86_mmr gdtr{0, Map::kGdt, 0x10000 - 1, 0};
    if (uc_reg_write(uc_, UC_X86_REG_GDTR, &gdtr) != UC_ERR_OK) {
        if (error) *error = "GDTR write failed";
        return false;
    }
    // The default 16-bit stack segment would wrap ESP at 64 KiB.
    for (int r : {UC_X86_REG_SS, UC_X86_REG_DS, UC_X86_REG_ES, UC_X86_REG_GS}) SetReg(uc_, r, kDataSelector);
    // SSE: CR0.MP set, CR0.EM clear, CR4.OSFXSR and OSXMMEXCPT set.
    uint32_t cr0 = Reg(uc_, UC_X86_REG_CR0);
    cr0 = (cr0 & ~0x4u) | 0x2u;
    SetReg(uc_, UC_X86_REG_CR0, cr0);
    SetReg(uc_, UC_X86_REG_CR4, Reg(uc_, UC_X86_REG_CR4) | 0x600u);
    if (!baseline_) uc_context_alloc(uc_, &baseline_);
    uc_context_save(uc_, baseline_);
    for (uint32_t i = kGdtEntries - 1; i >= kFirstFsIndex; --i) freeGdt_.push_back(i);
    return true;
}

void Machine::UpdateSharedUserData() {
    const uint64_t ms = NowMs();
    if (ms == lastSharedUpdateMs_ && ms) return;
    lastSharedUpdateMs_ = ms;
    const uint64_t interrupt = ms * 10000ull;
    // 100 ns units since 1601 for the system time.
    const uint64_t unixMs = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
    const uint64_t sys = unixMs * 10000ull + 116444736000000000ull;
    auto ksys = [&](uint32_t off, uint64_t v) {
        mem_.Write32(Map::kSharedUserData + off, static_cast<uint32_t>(v));
        mem_.Write32(Map::kSharedUserData + off + 4, static_cast<uint32_t>(v >> 32));
        mem_.Write32(Map::kSharedUserData + off + 8, static_cast<uint32_t>(v >> 32));
    };
    ksys(0x08, interrupt);
    ksys(0x14, sys);
    const uint64_t ticks = ms;  // TickCountMultiplier 0x0FA00000 maps one tick to 1 ms
    mem_.Write32(Map::kSharedUserData + 0x00, static_cast<uint32_t>(ticks));
    ksys(0x320, ticks);
}

// ---------------------------------------------------------------------------
// Shim registry

uint32_t Machine::AllocThunk() {
    if (nextThunk_ >= Map::kThunkEnd) return 0;
    const uint32_t va = nextThunk_;
    nextThunk_ += Map::kThunkStride;
    return va;
}

void Machine::RegisterShim(const std::string& dll, const std::string& name, int argBytes, ShimFn fn) {
    const std::string key = Lower(dll) + "!" + name;
    auto it = byName_.find(key);
    if (it != byName_.end()) {
        it->second->argBytes = argBytes;
        it->second->fn = std::move(fn);
        return;
    }
    const uint32_t thunk = AllocThunk();
    if (!thunk) {
        NETEMU_LOGE("thunk space exhausted at %s", key.c_str());
        return;
    }
    auto def = std::make_unique<ShimDef>();
    def->dll = Lower(dll);
    def->name = name;
    def->argBytes = argBytes;
    def->fn = std::move(fn);
    def->thunk = thunk;
    byName_[key] = def.get();
    const size_t slot = (thunk - Map::kThunkBegin) / Map::kThunkStride;
    if (defs_.size() <= slot) defs_.resize(slot + 1);
    defs_[slot] = std::move(def);
}

void Machine::RegisterOrdinal(const std::string& dll, uint16_t ordinal, const std::string& name) {
    ordinals_[Lower(dll) + "#" + std::to_string(ordinal)] = name;
}

uint32_t Machine::ResolveImport(const std::string& dll, const std::string& name, uint16_t ordinal) {
    std::string fn = name;
    if (fn.empty()) {
        auto o = ordinals_.find(Lower(dll) + "#" + std::to_string(ordinal));
        if (o == ordinals_.end()) return 0;
        fn = o->second;
    }
    auto it = byName_.find(Lower(dll) + "!" + fn);
    return it == byName_.end() ? 0 : it->second->thunk;
}

uint32_t Machine::ResolveByName(const std::string& name) {
    for (const auto& [key, def] : byName_)
        if (def->name == name) return def->thunk;
    return 0;
}

uint32_t Machine::MakeThunk(const std::string& label, int argBytes, ShimFn fn) {
    const uint32_t thunk = AllocThunk();
    if (!thunk) return 0;
    auto def = std::make_unique<ShimDef>();
    def->dll = "<host>";
    def->name = label;
    def->argBytes = argBytes;
    def->fn = std::move(fn);
    def->thunk = thunk;
    const size_t slot = (thunk - Map::kThunkBegin) / Map::kThunkStride;
    if (defs_.size() <= slot) defs_.resize(slot + 1);
    defs_[slot] = std::move(def);
    return thunk;
}

std::vector<const ShimDef*> Machine::Shims() const {
    std::vector<const ShimDef*> out;
    for (const auto& d : defs_)
        if (d) out.push_back(d.get());
    return out;
}

void Machine::SetMainModule(uint32_t base, uint32_t entry, const std::vector<uint32_t>& tlsCallbacks, uint32_t tlsStart,
                            uint32_t tlsEnd, uint32_t tlsZero, uint32_t tlsIndexVa) {
    moduleBase_ = base;
    moduleEntry_ = entry;
    tlsCallbacks_ = tlsCallbacks;
    tlsStart_ = tlsStart;
    tlsEnd_ = tlsEnd;
    tlsZero_ = tlsZero;
    tlsPresent_ = tlsIndexVa != 0;
    if (tlsIndexVa) mem_.Write32(tlsIndexVa, 0);
    mem_.Write32(peb_ + 0x08, base);
    for (auto& t : threads_)
        if (t->state != GuestThread::State::Exited) SetupThreadTls(*t);
}

// ---------------------------------------------------------------------------
// Threads

void Machine::SetupThreadTls(GuestThread& t) {
    if (!tlsPresent_) return;
    const uint32_t raw = tlsEnd_ > tlsStart_ ? tlsEnd_ - tlsStart_ : 0;
    const uint32_t block = mem_.HeapAlloc(raw + tlsZero_ + 16, true);
    if (raw) {
        std::vector<uint8_t> tmp(raw);
        mem_.ReadBytes(tlsStart_, tmp.data(), raw);
        mem_.WriteBytes(block, tmp.data(), raw);
    }
    uint32_t array = mem_.Read32(t.teb + 0x2C);
    if (!array) {
        array = mem_.HeapAlloc(64 * 4, true);
        mem_.Write32(t.teb + 0x2C, array);
    }
    mem_.Write32(array, block);
}

GuestThread* Machine::CreateThread(uint32_t start, uint32_t param, bool suspended, const std::string& name) {
    if (freeGdt_.empty()) return nullptr;
    auto t = std::make_unique<GuestThread>();
    t->id = (nextThreadId_ += 4);
    t->handle = NewHandle();
    t->name = name;
    t->stackSize = kStackSize;
    t->stackBase = mem_.AllocPages(kStackSize + Map::kPage, 0x10000u, true);
    t->teb = mem_.AllocPages(0x2000, Map::kPage, true);
    if (!t->stackBase || !t->teb) return nullptr;
    t->gdtIndex = freeGdt_.back();
    freeGdt_.pop_back();
    const uint32_t top = t->stackBase + Map::kPage + kStackSize;
    mem_.Write32(t->teb + 0x00, 0xFFFFFFFFu);  // SEH chain end
    mem_.Write32(t->teb + 0x04, top);
    mem_.Write32(t->teb + 0x08, t->stackBase + Map::kPage);
    mem_.Write32(t->teb + 0x18, t->teb);
    mem_.Write32(t->teb + 0x20, 0x1000);  // process id
    mem_.Write32(t->teb + 0x24, t->id);
    mem_.Write32(t->teb + 0x30, peb_);
    mem_.Write64(Map::kGdt + 8ull * t->gdtIndex, Descriptor(t->teb, 0x1FFF, 0x93, 0x4));
    SetupThreadTls(*t);
    if (uc_context_alloc(uc_, &t->ctx) != UC_ERR_OK) return nullptr;

    // Fresh context: baseline CPU, own FS, stack frame for the start thunk.
    uint32_t esp = top - 64;
    if (start) {
        mem_.Write32(esp + 0, 0);
        mem_.Write32(esp + 4, start);
        mem_.Write32(esp + 8, param);
    }
    // The context is built on the first slice (LoadContext), so a thread
    // created from a shim does not disturb the running CPU state.
    t->initEsp = esp;
    t->initEip = start ? threadStartThunk_ : retHostThunk_;
    t->state = GuestThread::State::Runnable;
    t->suspendCount = suspended ? 1 : 0;
    GuestThread* raw = t.get();
    threads_.push_back(std::move(t));
    NETEMU_TRACE("thread %u created start=0x%08X param=0x%08X", raw->id, start, param);
    return raw;
}

void Machine::LoadContext(GuestThread& t) {
    if (!t.fresh) {
        uc_context_restore(uc_, t.ctx);
        return;
    }
    uc_context_restore(uc_, baseline_);
    SetReg(uc_, UC_X86_REG_FS, t.gdtIndex << 3);
    SetReg(uc_, UC_X86_REG_ESP, t.initEsp);
    SetReg(uc_, UC_X86_REG_EBP, 0);
    SetReg(uc_, UC_X86_REG_EIP, t.initEip);
    t.fresh = false;
}

void Machine::ThreadStartShim(ShimCall& call) {
    // Frame written by CreateThread: [esp+4] start, [esp+8] param.
    const uint32_t start = call.Arg(0);
    const uint32_t param = call.Arg(1);
    GuestThread& t = call.thread;
    // TLS callbacks, then DllMain(DLL_THREAD_ATTACH), then the start routine.
    auto steps = std::make_shared<std::vector<uint32_t>>(tlsCallbacks_);
    if (moduleEntry_) steps->push_back(moduleEntry_);
    auto next = std::make_shared<ShimFn>();
    auto index = std::make_shared<size_t>(0);
    const uint32_t base = moduleBase_;
    const uint32_t exitThunk = threadExitThunk_;
    const uint32_t stackTop = mem_.Read32(t.teb + 0x04);
    *next = [steps, next, index, base, start, param, exitThunk, stackTop](ShimCall& c) {
        if (*index < steps->size()) {
            const uint32_t fn = (*steps)[(*index)++];
            c.Call(fn, {base, 2 /* DLL_THREAD_ATTACH */, 0}, 0, *next);
            return;
        }
        const uint32_t esp = stackTop - 64;
        c.mem.Write32(esp, exitThunk);
        c.mem.Write32(esp + 4, param);
        c.Jump(start, esp);
    };
    (*next)(call);
}

GuestThread* Machine::ThreadById(uint32_t id) {
    for (auto& t : threads_)
        if (t->id == id) return t.get();
    return nullptr;
}

GuestThread* Machine::ThreadByHandle(uint32_t handle) {
    for (auto& t : threads_)
        if (t->handle == handle) return t.get();
    return nullptr;
}

void Machine::ResumeThread(GuestThread* t) {
    if (t && t->suspendCount) --t->suspendCount;
    Signal();
}

uint32_t Machine::NewHandle() { return nextHandle_ += 4; }

void Machine::Signal() { ++eventGen_; }

void Machine::FlushCode(uint32_t begin, uint32_t end) {
    if (end > begin) uc_ctl_remove_cache(uc_, begin, end);
}

void Machine::ReleaseThread(GuestThread& t) {
    if (t.stackBase) mem_.FreePages(t.stackBase);
    t.stackBase = 0;
    // The TEB stays mapped: other threads may still hold its address.
    t.conts.clear();
    if (t.gdtIndex) {
        freeGdt_.push_back(t.gdtIndex);
        t.gdtIndex = 0;
    }
}

// ---------------------------------------------------------------------------
// Hooks

void Machine::PushCall(GuestThread&, uint32_t fn, const std::vector<uint32_t>& args, uint32_t ecx, uint32_t retAddr,
                       uint32_t reserve) {
    uint32_t esp = Reg(uc_, UC_X86_REG_ESP) - reserve;
    esp &= ~3u;
    for (auto it = args.rbegin(); it != args.rend(); ++it) {
        esp -= 4;
        mem_.Write32(esp, *it);
    }
    esp -= 4;
    mem_.Write32(esp, retAddr);
    SetReg(uc_, UC_X86_REG_ESP, esp);
    SetReg(uc_, UC_X86_REG_ECX, ecx);
    SetReg(uc_, UC_X86_REG_EIP, fn);
}

void Machine::Apply(ShimCall& call, uint32_t thunkAddr) {
    GuestThread& t = call.thread;
    if (!call.decided) call.Return(0);
    switch (call.outcome) {
    case ShimCall::Outcome::Return: {
        const int pop = call.def.argBytes > 0 ? call.def.argBytes : 0;
        const uint32_t ret = mem_.Read32(call.esp);
        SetReg(uc_, UC_X86_REG_EAX, call.outEax);
        SetReg(uc_, UC_X86_REG_EDX, call.outEdx);
        SetReg(uc_, UC_X86_REG_ESP, call.esp + 4 + static_cast<uint32_t>(pop));
        SetReg(uc_, UC_X86_REG_EIP, ret);
        t.waitActive = false;
        t.waitPhase = 0;
        break;
    }
    case ShimCall::Outcome::Block:
        if (thunkAddr == contThunk_) {
            NETEMU_LOGE("%s blocked inside a continuation; returning 0", call.def.name.c_str());
            call.Return(0);
            Apply(call, thunkAddr);
            return;
        }
        if (!t.waitActive) {
            t.waitActive = true;
            t.waitStartMs = NowMs();
        }
        t.state = GuestThread::State::Blocked;
        t.wakeAtMs = NowMs() + call.blockMs;
        t.waitGen = eventGen_.load();
        SetReg(uc_, UC_X86_REG_EIP, thunkAddr);
        stopRequested_ = true;
        uc_emu_stop(uc_);
        break;
    case ShimCall::Outcome::Call: {
        Continuation c;
        c.fn = std::move(call.callCont);
        c.origin = &call.def;
        c.originEsp = call.esp;
        c.resumeEsp = Reg(uc_, UC_X86_REG_ESP);
        t.conts.push_back(std::move(c));
        PushCall(t, call.callFn, call.callArgs, call.callEcx, contThunk_, call.callReserve);
        break;
    }
    case ShimCall::Outcome::Resume: {
        const auto& r = call.resumeRegs;
        SetReg(uc_, UC_X86_REG_EAX, r.eax);
        SetReg(uc_, UC_X86_REG_EBX, r.ebx);
        SetReg(uc_, UC_X86_REG_ECX, r.ecx);
        SetReg(uc_, UC_X86_REG_EDX, r.edx);
        SetReg(uc_, UC_X86_REG_ESI, r.esi);
        SetReg(uc_, UC_X86_REG_EDI, r.edi);
        SetReg(uc_, UC_X86_REG_EBP, r.ebp);
        SetReg(uc_, UC_X86_REG_ESP, r.esp);
        SetReg(uc_, UC_X86_REG_EFLAGS, r.eflags);
        SetReg(uc_, UC_X86_REG_EIP, r.eip);
        t.waitActive = false;
        t.waitPhase = 0;
        break;
    }
    case ShimCall::Outcome::Jump:
        SetReg(uc_, UC_X86_REG_ESP, call.jumpEsp);
        SetReg(uc_, UC_X86_REG_EBP, 0);
        SetReg(uc_, UC_X86_REG_EIP, call.jumpEip);
        break;
    case ShimCall::Outcome::Exit:
        if (&t == main_) {
            // The host-call thread cannot end; the call fails instead.
            NETEMU_LOGE("guest ended the host-call thread (code 0x%X); failing the call", call.outEax);
            {
                std::lock_guard lk(statsMutex_);
                lastFault_ = "guest exited the calling thread";
            }
            SetReg(uc_, UC_X86_REG_ESP, main_->jobEsp);
            main_->state = GuestThread::State::Idle;
            main_->conts.clear();
            FinishJob(false, 0);
        } else {
            t.exitCode = call.outEax;
            t.state = GuestThread::State::Exited;
            Signal();
        }
        stopRequested_ = true;
        uc_emu_stop(uc_);
        break;
    }
}

void Machine::OnThunk(uint32_t addr) {
    GuestThread* t = current_;
    if (!t) return;
    // Only two places in a slot run the shim: the entry while the prologue is
    // intact, and the dispatch point a hook trampoline returns to. A patched
    // entry (inline hook) and the rest of the slot execute as guest code.
    // Where the shim was entered: a Block retry must re-enter there, not at a
    // hooked +0 that would run the guest's detour again.
    const uint32_t entry = addr;
    t->resumedEip = 0;  // the thread made progress since any exception resume
    const uint32_t off = (addr - Map::kThunkBegin) % Map::kThunkStride;
    addr -= off;
    if (off == 0) {
        if (mem_.Read16(addr) != 0xFF8B) return;  // `mov edi,edi` replaced by a hook jump
    } else if (off == 2 || off == 3 || off == 5 || off >= 11) {
        // Prologue tail run by a trampoline, or MinHook's hot-patch jump in
        // the padding before the next slot: guest code.
        return;
    } else if (off != Map::kThunkDispatch) {
        FaultCurrent("jump into the thunk area outside a thunk");
        return;
    }
    {
        std::lock_guard lk(statsMutex_);
        ++stats_.shimCalls;
    }
    if (addr == retHostThunk_) {
        const uint64_t r = Reg(uc_, UC_X86_REG_EAX) | (static_cast<uint64_t>(Reg(uc_, UC_X86_REG_EDX)) << 32);
        if (t == main_) {
            SetReg(uc_, UC_X86_REG_ESP, t->jobEsp);
            t->state = GuestThread::State::Idle;
            t->conts.clear();
            FinishJob(true, r);
        } else {
            NETEMU_LOGE("thread %u returned to the host thunk; exiting it", t->id);
            t->state = GuestThread::State::Exited;
            t->exitCode = static_cast<uint32_t>(r);
            Signal();
        }
        stopRequested_ = true;
        uc_emu_stop(uc_);
        return;
    }
    if (addr == threadExitThunk_) {
        t->exitCode = Reg(uc_, UC_X86_REG_EAX);
        t->state = GuestThread::State::Exited;
        NETEMU_TRACE("thread %u returned %u", t->id, t->exitCode);
        Signal();
        stopRequested_ = true;
        uc_emu_stop(uc_);
        return;
    }
    if (addr == contThunk_) {
        if (t->conts.empty()) {
            FaultCurrent("continuation thunk without a pending continuation");
            return;
        }
        Continuation c = std::move(t->conts.back());
        t->conts.pop_back();
        const uint32_t eax = Reg(uc_, UC_X86_REG_EAX);
        const uint32_t edx = Reg(uc_, UC_X86_REG_EDX);
        SetReg(uc_, UC_X86_REG_ESP, c.resumeEsp);
        ShimCall call(*this, *t, *c.origin, c.originEsp);
        call.result = eax;
        call.resultEdx = edx;
        c.fn(call);
        Apply(call, addr);
        return;
    }
    const size_t slot = (addr - Map::kThunkBegin) / Map::kThunkStride;
    if (slot >= defs_.size() || !defs_[slot]) {
        FaultCurrent("jump into the thunk area outside a thunk");
        return;
    }
    ShimDef& def = *defs_[slot];
    ++def.calls;
    if (traceShims_) {
        const uint32_t esp = Reg(uc_, UC_X86_REG_ESP);
        NETEMU_LOGI("[t%u] %s!%s ret=0x%08X args %08X %08X %08X %08X", t->id, def.dll.c_str(), def.name.c_str(), mem_.Read32(esp),
                    mem_.Read32(esp + 4), mem_.Read32(esp + 8), mem_.Read32(esp + 12), mem_.Read32(esp + 16));
    }
    ShimCall call(*this, *t, def, Reg(uc_, UC_X86_REG_ESP));
    if (!def.fn) {
        NETEMU_LOGE("no shim body for %s!%s", def.dll.c_str(), def.name.c_str());
        FaultCurrent("missing shim");
        return;
    }
    def.fn(call);
    Apply(call, entry);
}

void Machine::OnThunkWrite(uint32_t addr, int size, uint64_t value, uint32_t eip) {
    const size_t slot = (addr - Map::kThunkBegin) / Map::kThunkStride;
    const ShimDef* d = slot < defs_.size() ? defs_[slot].get() : nullptr;
    NETEMU_LOGW("guest patches thunk %s!%s (+%u, %d bytes = 0x%llX) from EIP 0x%08X", d ? d->dll.c_str() : "?",
                d ? d->name.c_str() : "?", (addr - Map::kThunkBegin) % Map::kThunkStride, size,
                static_cast<unsigned long long>(value), eip);
}

void Machine::OnInterrupt(uint32_t intno) {
    const uint32_t eip = Reg(uc_, UC_X86_REG_EIP);
    // #DE leaves EIP on the faulting instruction; int3 has already stepped past
    // it, and Windows reports the breakpoint at the int3 itself.
    if (intno == 0 && RaiseHardwareException(0xC0000094u, eip, 0, 0, 0)) return;  // STATUS_INTEGER_DIVIDE_BY_ZERO
    if (intno == 3 && RaiseHardwareException(0x80000003u, eip - 1, 1, 0, 0)) return;  // STATUS_BREAKPOINT
    char why[96];
    std::snprintf(why, sizeof(why), "interrupt 0x%X at EIP 0x%08X", intno, Reg(uc_, UC_X86_REG_EIP));
    FaultCurrent(why);
}

void Machine::OnInvalidMemory(int type, uint64_t addr, int size) {
    lastMemType_ = type;
    lastMemAddr_ = static_cast<uint32_t>(addr);
    char why[128];
    std::snprintf(why, sizeof(why), "invalid memory access type %d at 0x%08" PRIX64 " size %d, EIP 0x%08X", type, addr, size,
                  Reg(uc_, UC_X86_REG_EIP));
    std::lock_guard lk(statsMutex_);
    lastFault_ = why;
}

void Machine::OnPrivilegedInsn() {
    const uint32_t eip = Reg(uc_, UC_X86_REG_EIP);
    // Unicorn finishes the instruction after the hook returns, so the redirect
    // into the dispatcher is re-applied once the slice stops. Limitation:
    // uc_emu_stop takes effect at the end of the translation block, so the
    // instructions after IN/OUT in the same block still run (on Windows the
    // #GP comes first). The CONTEXT is captured here, before them, so a handler
    // that resumes or unwinds sees the right registers; only memory stores in
    // that tail would leak through. The VM probes seen so far store nothing
    // before their next branch.
    if (RaiseHardwareException(0xC0000096u, eip, 0, 0, 0)) {  // STATUS_PRIVILEGED_INSTRUCTION
        pendingRedirect_ = true;
        redirectEsp_ = Reg(uc_, UC_X86_REG_ESP);
        redirectEip_ = Reg(uc_, UC_X86_REG_EIP);
        stopRequested_ = true;
        uc_emu_stop(uc_);
        return;
    }
    char why[96];
    std::snprintf(why, sizeof(why), "privileged instruction at EIP 0x%08X", eip);
    FaultCurrent(why);
}

void Machine::OnInvalidInsn() {
    char why[96];
    std::snprintf(why, sizeof(why), "invalid instruction at EIP 0x%08X", Reg(uc_, UC_X86_REG_EIP));
    std::lock_guard lk(statsMutex_);
    lastFault_ = why;
}

bool Machine::RaiseHardwareException(uint32_t code, uint32_t eip, uint32_t nparams, uint32_t p0, uint32_t p1) {
    GuestThread* t = current_;
    if (!exceptionThunk_ || !t) return false;
    const uint32_t esp = Reg(uc_, UC_X86_REG_ESP);
    if (eip == t->resumedEip && esp == t->resumedEsp) {
        if (++t->exceptionRepeats > 16) return false;
    } else {
        t->exceptionRepeats = 0;
    }
    t->resumedEip = t->resumedEsp = 0;
    // CONTEXT, then EXCEPTION_RECORD, then the dispatcher's call frame, all
    // below the faulting ESP (as the kernel lays them out for user mode).
    const uint32_t ctx = (esp - kContextSize - 0x40) & ~15u;
    const uint32_t rec = ctx - 0x60;
    const uint32_t frame = rec - 12;
    if (!mem_.Valid(frame, esp - frame)) return false;
    mem_.Fill(rec, 0, 0x50);
    mem_.Write32(rec + 0, code);
    mem_.Write32(rec + 12, eip);  // ExceptionAddress
    mem_.Write32(rec + 16, nparams);
    mem_.Write32(rec + 20, p0);
    mem_.Write32(rec + 24, p1);
    mem_.Fill(ctx, 0, kContextSize);
    mem_.Write32(ctx + 0x00, 0x10007);  // CONTEXT_FULL (i386)
    const std::pair<uint32_t, int> regs[] = {
        {0x8C, UC_X86_REG_GS},  {0x90, UC_X86_REG_FS},  {0x94, UC_X86_REG_ES},  {0x98, UC_X86_REG_DS},
        {0x9C, UC_X86_REG_EDI}, {0xA0, UC_X86_REG_ESI}, {0xA4, UC_X86_REG_EBX}, {0xA8, UC_X86_REG_EDX},
        {0xAC, UC_X86_REG_ECX}, {0xB0, UC_X86_REG_EAX}, {0xB4, UC_X86_REG_EBP}, {0xBC, UC_X86_REG_CS},
        {0xC0, UC_X86_REG_EFLAGS}, {0xC8, UC_X86_REG_SS},
    };
    for (const auto& [off, reg] : regs) mem_.Write32(ctx + off, Reg(uc_, reg));
    mem_.Write32(ctx + 0xB8, eip);
    mem_.Write32(ctx + 0xC4, esp);
    mem_.Write32(frame + 0, 0);  // no return address: the dispatcher never returns
    mem_.Write32(frame + 4, rec);
    mem_.Write32(frame + 8, ctx);
    NETEMU_LOGW("guest exception 0x%08X at EIP 0x%08X (info %u, 0x%08X) on thread %u -> SEH dispatch", code, eip, p0, p1,
                t->id);
    SetReg(uc_, UC_X86_REG_ESP, frame);
    SetReg(uc_, UC_X86_REG_EIP, exceptionThunk_);
    return true;
}

void Machine::FaultCurrent(const char* why) {
    GuestThread* t = current_;
    {
        std::lock_guard lk(statsMutex_);
        ++stats_.faults;
        lastFault_ = why;
    }
    const uint32_t esp = Reg(uc_, UC_X86_REG_ESP);
    NETEMU_LOGE("guest fault on thread %u (%s): %s | EAX=%08X EBX=%08X ECX=%08X EDX=%08X ESI=%08X EDI=%08X EBP=%08X ESP=%08X "
                "[esp]=%08X [esp+4]=%08X",
                t ? t->id : 0, t ? t->name.c_str() : "?", why, Reg(uc_, UC_X86_REG_EAX), Reg(uc_, UC_X86_REG_EBX),
                Reg(uc_, UC_X86_REG_ECX), Reg(uc_, UC_X86_REG_EDX), Reg(uc_, UC_X86_REG_ESI), Reg(uc_, UC_X86_REG_EDI),
                Reg(uc_, UC_X86_REG_EBP), esp, mem_.Read32(esp), mem_.Read32(esp + 4));
    // EBP frame chain: enough to find the caller of a crashing routine in a disassembly.
    {
        char chain[512];
        size_t n = 0;
        uint32_t ebp = Reg(uc_, UC_X86_REG_EBP);
        for (int depth = 0; depth < 32 && mem_.Valid(ebp, 8) && n < sizeof(chain) - 12; ++depth) {
            n += std::snprintf(chain + n, sizeof(chain) - n, " %08X", mem_.Read32(ebp + 4));
            const uint32_t next = mem_.Read32(ebp);
            if (next <= ebp) break;
            ebp = next;
        }
        chain[n] = 0;
        NETEMU_LOGE("  return chain:%s", n ? chain : " (none)");
    }
    if (t == main_) {
        SetReg(uc_, UC_X86_REG_ESP, main_->jobEsp);
        main_->state = GuestThread::State::Idle;
        main_->conts.clear();
        FinishJob(false, 0);
    } else if (t) {
        t->state = GuestThread::State::Exited;
        t->exitCode = 0xC0000005u;
        Signal();
    }
    stopRequested_ = true;
    uc_emu_stop(uc_);
}

// ---------------------------------------------------------------------------
// Scheduler

bool Machine::Start() {
    if (running_.load()) return true;
    stopping_ = false;
    running_ = true;
    watchdog_ = std::thread([this] { WatchdogLoop(); });
    emuThread_ = std::thread([this] { EmuLoop(); });
    return true;
}

void Machine::Stop() {
    if (!running_.load()) return;
    stopping_ = true;
    {
        std::lock_guard lk(jobMutex_);
        jobCv_.notify_all();
    }
    {
        std::lock_guard lk(sliceMutex_);
        if (sliceActive_) uc_emu_stop(uc_);
        sliceCv_.notify_all();
    }
    if (emuThread_.joinable()) emuThread_.join();
    if (watchdog_.joinable()) watchdog_.join();
    running_ = false;
    std::lock_guard lk(jobMutex_);
    for (auto& j : jobs_) j->done = true;
    jobs_.clear();
    if (activeJob_) activeJob_->done = true;
    activeJob_.reset();
    jobDoneCv_.notify_all();
}

bool Machine::OnEmuThread() const { return std::this_thread::get_id() == emuThreadId_; }

void Machine::WatchdogLoop() {
    std::unique_lock lk(sliceMutex_);
    while (!stopping_.load()) {
        if (!sliceActive_) {
            sliceCv_.wait(lk);
            continue;
        }
        const uint64_t id = sliceId_;
        const uint64_t deadline = sliceDeadlineUs_;
        const uint64_t now = NowUs();
        if (now >= deadline) {
            if (sliceActive_ && sliceId_ == id) {
                uc_emu_stop(uc_);
                sliceActive_ = false;
            }
            continue;
        }
        sliceCv_.wait_for(lk, std::chrono::microseconds(deadline - now));
    }
}

GuestThread* Machine::Pick(uint64_t now) {
    auto eligible = [&](GuestThread* t) {
        if (t->suspendCount) return false;
        if (t->state == GuestThread::State::Runnable) return true;
        if (t->state == GuestThread::State::Blocked) return now >= t->wakeAtMs || t->waitGen != eventGen_.load();
        return false;
    };
    const size_t n = threads_.size();
    for (size_t k = 0; k < n; ++k) {
        GuestThread* t = threads_[(rr_ + k) % n].get();
        if (t == main_ && !activeJob_) continue;
        if (eligible(t)) {
            rr_ = (rr_ + k + 1) % n;
            return t;
        }
    }
    return nullptr;
}

void Machine::RunSlice(GuestThread& t) {
    LoadContext(t);
    current_ = &t;
    t.state = GuestThread::State::Runnable;
    stopRequested_ = false;
    UpdateSharedUserData();
    {
        std::lock_guard lk(sliceMutex_);
        ++sliceId_;
        sliceDeadlineUs_ = NowUs() + kSliceUs;
        sliceActive_ = true;
        sliceCv_.notify_all();
    }
    const uint32_t eip = Reg(uc_, UC_X86_REG_EIP);
    const uc_err err = uc_emu_start(uc_, eip, kNeverAddr, 0, 0);
    {
        std::lock_guard lk(sliceMutex_);
        sliceActive_ = false;
    }
    {
        std::lock_guard lk(statsMutex_);
        ++stats_.slices;
    }
    if (pendingRedirect_) {
        pendingRedirect_ = false;
        SetReg(uc_, UC_X86_REG_ESP, redirectEsp_);
        SetReg(uc_, UC_X86_REG_EIP, redirectEip_);
    }
    if (err == UC_ERR_INSN_INVALID && t.state != GuestThread::State::Exited &&
        RaiseHardwareException(0xC000001Du, Reg(uc_, UC_X86_REG_EIP), 0, 0, 0)) {  // STATUS_ILLEGAL_INSTRUCTION
        current_ = nullptr;
        uc_context_save(uc_, t.ctx);
        return;
    }
    if (err != UC_ERR_OK && t.state != GuestThread::State::Exited) {
        const bool memFault = err == UC_ERR_READ_UNMAPPED || err == UC_ERR_WRITE_UNMAPPED || err == UC_ERR_FETCH_UNMAPPED ||
                              err == UC_ERR_READ_PROT || err == UC_ERR_WRITE_PROT || err == UC_ERR_FETCH_PROT;
        if (memFault) {
            // ExceptionInformation[0]: 0 read, 1 write, 8 execute (DEP).
            const bool write = lastMemType_ == UC_MEM_WRITE_UNMAPPED || lastMemType_ == UC_MEM_WRITE_PROT;
            const bool fetch = lastMemType_ == UC_MEM_FETCH_UNMAPPED || lastMemType_ == UC_MEM_FETCH_PROT;
            if (RaiseHardwareException(0xC0000005u, Reg(uc_, UC_X86_REG_EIP), 2, fetch ? 8u : write ? 1u : 0u, lastMemAddr_)) {
                current_ = nullptr;
                uc_context_save(uc_, t.ctx);
                return;
            }
        }
        std::string why;
        {
            std::lock_guard lk(statsMutex_);
            why = lastFault_;
        }
        char buf[256];
        std::snprintf(buf, sizeof(buf), "%s (%s)", uc_strerror(err), why.c_str());
        FaultCurrent(buf);
    }
    current_ = nullptr;
    if (t.state == GuestThread::State::Exited) {
        ReleaseThread(t);
        return;
    }
    uc_context_save(uc_, t.ctx);
}

void Machine::EmuLoop() {
    emuThreadId_ = std::this_thread::get_id();
    while (!stopping_.load()) {
        // Host work first: RunSync tasks, then a guest call when main is idle.
        {
            std::unique_lock lk(jobMutex_);
            while (!jobs_.empty() && jobs_.front()->task) {
                auto job = jobs_.front();
                jobs_.pop_front();
                lk.unlock();
                job->task();
                lk.lock();
                job->done = true;
                job->ok = true;
                jobDoneCv_.notify_all();
            }
            if (!activeJob_ && main_->state == GuestThread::State::Idle && !jobs_.empty()) {
                activeJob_ = jobs_.front();
                jobs_.pop_front();
                lk.unlock();
                LoadContext(*main_);
                const uint32_t top = mem_.Read32(main_->teb + 0x04);
                main_->jobEsp = top - 64;
                SetReg(uc_, UC_X86_REG_ESP, main_->jobEsp);
                PushCall(*main_, activeJob_->fn, activeJob_->args, activeJob_->ecx, retHostThunk_);
                uc_context_save(uc_, main_->ctx);
                main_->state = GuestThread::State::Runnable;
                main_->conts.clear();
            }
        }
        const uint64_t now = NowMs();
        GuestThread* t = Pick(now);
        if (!t) {
            uint64_t wake = now + 10;
            for (auto& th : threads_)
                if (th->state == GuestThread::State::Blocked && !th->suspendCount) wake = std::min(wake, th->wakeAtMs);
            std::unique_lock lk(jobMutex_);
            if (jobs_.empty() || (activeJob_ && !jobs_.front()->task))
                jobCv_.wait_for(lk, std::chrono::milliseconds(wake > now ? wake - now : 0));
            continue;
        }
        RunSlice(*t);
    }
}

void Machine::FinishJob(bool ok, uint64_t result) {
    std::lock_guard lk(jobMutex_);
    if (!activeJob_) return;
    activeJob_->ok = ok;
    activeJob_->result = result;
    activeJob_->done = true;
    activeJob_.reset();
    jobDoneCv_.notify_all();
}

uint64_t Machine::CallSync(uint32_t fn, uint32_t ecx, const std::vector<uint32_t>& args, bool* ok, uint32_t timeoutMs) {
    if (ok) *ok = false;
    if (!running_.load() || OnEmuThread() || !fn) return 0;
    if (wedged_.load()) return 0;  // an earlier call never came back; see below
    auto job = std::make_shared<Job>();
    job->fn = fn;
    job->ecx = ecx;
    job->args = args;
    std::unique_lock lk(jobMutex_);
    jobs_.push_back(job);
    jobCv_.notify_all();
    if (!jobDoneCv_.wait_for(lk, std::chrono::milliseconds(timeoutMs), [&] { return job->done; })) {
        NETEMU_LOGE("guest call 0x%08X timed out after %u ms", fn, timeoutMs);
        // Not started yet: drop it. Running: the guest may still use the
        // caller's buffers and the main thread stays busy, so no later call
        // can be trusted. Mark the machine wedged; callers keep their buffers.
        auto queued = std::find(jobs_.begin(), jobs_.end(), job);
        if (queued != jobs_.end()) jobs_.erase(queued);
        else wedged_.store(true);
        lk.unlock();
        NETEMU_LOGE("%s", DescribeThreads().c_str());
        return 0;
    }
    if (ok) *ok = job->ok;
    return job->result;
}

void Machine::RunSync(const std::function<void()>& fn) {
    if (!running_.load() || OnEmuThread()) {
        fn();
        return;
    }
    auto job = std::make_shared<Job>();
    job->task = fn;
    std::unique_lock lk(jobMutex_);
    // Host tasks never wait behind a guest call: they only touch memory.
    jobs_.push_front(job);
    jobCv_.notify_all();
    jobDoneCv_.wait(lk, [&] { return job->done; });
}

std::string Machine::DescribeThreads() {
    std::string out;
    RunSync([&] {
        for (auto& t : threads_) {
            uint32_t eip = t->initEip;
            if (!t->fresh && t->state != GuestThread::State::Exited) {
                uc_context_restore(uc_, t->ctx);
                eip = Reg(uc_, UC_X86_REG_EIP);
            }
            static const char* names[] = {"runnable", "blocked", "idle", "exited"};
            char line[160];
            std::snprintf(line, sizeof(line), "thread %u (%s) %s eip=0x%08X\n", t->id, t->name.c_str(),
                          names[static_cast<int>(t->state)], eip);
            out += line;
        }
    });
    return out;
}

MachineStats Machine::Stats() const {
    std::lock_guard lk(statsMutex_);
    return stats_;
}

std::string Machine::LastFault() const {
    std::lock_guard lk(statsMutex_);
    return lastFault_;
}

}  // namespace NetEmu
