#include "NetcModule.h"

#include "NetEmuLog.h"
#include "PeImage.h"
#include "Win32Shims.h"
#include "X86Machine.h"

#include <sys/stat.h>

#include <fstream>
#include <iterator>

namespace NetEmu {

namespace {
// CNet vtable (Client/sdk/net/CNet.h). No base class, so slot 0 is StartNetwork.
enum CNetSlot : uint32_t {
    kStartNetwork = 0,
    kStopNetwork = 1,
    kSetFakeLag = 2,
    kIsReady = 3,
    kIsConnected = 4,
    kDoPulse = 5,
    kShutdown = 6,
    kRegisterPacketHandler = 7,
    kAllocateNetBitStream = 8,
    kDeallocateNetBitStream = 9,
    kSendPacket = 10,
    kSetClientPort = 11,
    kGetConnectedServer = 12,
    kGetNetworkStatistics = 13,
    kGetPacketStats = 14,
    kGetPing = 15,
    kGetTime = 16,
    kGetLocalIP = 17,
    kGetSerial = 18,
    kGetConnectionError = 19,
    kSetConnectionError = 20,
    kReset = 21,
    kGetHTTPDownloadManager = 22,
    kSetServerBitStreamVersion = 23,
    kGetServerBitStreamVersion = 24,
    kGetStatus = 25,
    kGetNetRev = 26,
    kGetNetRel = 27,
    kSetTimeoutTime = 37,
};

constexpr uint32_t kCoreSlots = 192;
constexpr uint32_t kPacketQueueLimit = 8192;

bool ReadFile(const std::string& path, std::vector<uint8_t>* out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    out->assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return !out->empty();
}
}  // namespace

NetcModule::NetcModule() = default;

NetcModule::~NetcModule() { Unload(); }

void NetcModule::Unload() {
    if (machine_) machine_->Stop();
    cnet_ = 0;
    state_.reset();
    image_.reset();
    machine_.reset();
}

std::string NetcModule::LastError() const { return lastError_; }

bool NetcModule::ProcessExitRequested() const { return state_ && state_->processExitRequested; }

uint32_t NetcModule::GuestString(const std::string& s) {
    uint32_t va = 0;
    machine_->RunSync([&] { va = machine_->Mem().NewA(s); });
    return va;
}

void NetcModule::FreeGuest(uint32_t va) {
    if (va) machine_->RunSync([&] { machine_->Mem().HeapFree(va); });
}

uint32_t NetcModule::VCall(uint32_t index) const {
    const uint32_t vtable = machine_->Mem().Read32(cnet_);
    return machine_->Mem().Read32(vtable + index * 4);
}

uint64_t NetcModule::CallMethod(uint32_t index, const std::vector<uint32_t>& args, bool* ok) {
    if (!cnet_) {
        if (ok) *ok = false;
        return 0;
    }
    return machine_->CallSync(VCall(index), cnet_, args, ok);
}

void NetcModule::InstallHostThunks() {
    Machine& m = *machine_;
    GuestMemory& mem = m.Mem();

    // CCoreInterface stand-in: every slot logs. netc keeps the pointer; the
    // log shows which core services it actually reaches for.
    const uint32_t vtable = mem.StaticAlloc(kCoreSlots * 4);
    for (uint32_t i = 0; i < kCoreSlots; ++i) {
        const uint32_t thunk = m.MakeThunk("CCoreInterface::slot" + std::to_string(i), 0, [i](ShimCall& c) {
            NETEMU_LOGW("netc called CCoreInterface vtable slot %u from 0x%08X (returning 0)", i, c.ReturnAddress());
            c.Return(0);
        });
        mem.Write32(vtable + i * 4, thunk);
    }
    fakeCore_ = mem.StaticAlloc(64);
    mem.Write32(fakeCore_, vtable);

    // PPACKETHANDLER: bool (*)(unsigned char, NetBitStreamInterface&), cdecl.
    // The handler reads the bitstream through its own vtable, then copies it.
    handlerThunk_ = m.MakeThunk("<packet-handler>", -1, [this](ShimCall& c) {
        const uint8_t id = static_cast<uint8_t>(c.Arg(0));
        const uint32_t bs = c.Arg(1);
        const uint32_t vt = c.mem.Read32(bs);
        auto pkt = std::make_shared<NetcPacket>();
        pkt->id = id;
        const uint32_t fBits = c.mem.Read32(vt + bsBitsUsed_ * 4);
        const uint32_t fRead = c.mem.Read32(vt + bsGetReadOffset_ * 4);
        const uint32_t fData = c.mem.Read32(vt + bsGetData_ * 4);
        c.Call(fBits, {}, bs, [this, pkt, bs, fRead, fData](ShimCall& k1) {
            pkt->bits = k1.result;
            k1.Call(fRead, {}, bs, [this, pkt, bs, fData](ShimCall& k2) {
                pkt->readOffsetBits = k2.result;
                k2.Call(fData, {}, bs, [this, pkt](ShimCall& k3) {
                    const uint32_t bytes = (pkt->bits + 7) / 8;
                    if (bytes && k3.result) {
                        pkt->data.resize(bytes);
                        k3.mem.ReadBytes(k3.result, pkt->data.data(), bytes);
                    }
                    {
                        std::lock_guard lk(packetsMutex_);
                        if (packets_.size() < kPacketQueueLimit) packets_.push_back(std::move(*pkt));
                        else NETEMU_LOGE("netc packet queue full; dropping packet %u", pkt->id);
                    }
                    k3.Return(1);
                });
            });
        });
    });

    // Send helper, run as one guest call: Allocate, WriteBits, SendPacket, Deallocate.
    // Arguments: packetId, dataVa, bits, priority, reliability, ordering.
    sendThunk_ = m.MakeThunk("<send-packet>", -1, [this](ShimCall& c) {
        const uint32_t id = c.Arg(0), data = c.Arg(1), bits = c.Arg(2), prio = c.Arg(3), rel = c.Arg(4), ord = c.Arg(5);
        const uint32_t net = cnet_;
        const uint32_t netVt = c.mem.Read32(net);
        c.Call(c.mem.Read32(netVt + kAllocateNetBitStream * 4), {}, net,
               [this, net, netVt, id, data, bits, prio, rel, ord](ShimCall& k1) {
                   const uint32_t bs = k1.result;
                   if (!bs) {
                       k1.Return(0);
                       return;
                   }
                   const uint32_t writeBits = k1.mem.Read32(k1.mem.Read32(bs) + bsWriteBits_ * 4);
                   auto send = [net, netVt, id, bs, prio, rel, ord](ShimCall& k2) {
                       k2.Call(k2.mem.Read32(netVt + kSendPacket * 4), {id, bs, prio, rel, ord}, net, [net, netVt, bs](ShimCall& k3) {
                           const uint32_t sent = k3.result & 0xFF;
                           k3.Call(k3.mem.Read32(netVt + kDeallocateNetBitStream * 4), {bs}, net,
                                   [sent](ShimCall& k4) { k4.Return(sent); });
                       });
                   };
                   // NetBitStreamInterface::WriteBits is RakNet's WriteBits with
                   // rightAlignedBits = true: a trailing partial byte is taken
                   // from its low bits. Our buffers are MSB-first, so the whole
                   // bytes go in one call and the tail is shifted down first.
                   const uint32_t whole = bits & ~7u, rem = bits & 7u;
                   auto tail = [writeBits, bs, data, whole, rem, send](ShimCall& k) {
                       if (!rem) return send(k);
                       const uint32_t at = data + whole / 8;
                       k.mem.Write8(at, static_cast<uint8_t>(k.mem.Read8(at) >> (8 - rem)));
                       k.Call(writeBits, {at, rem}, bs, send);
                   };
                   if (!whole) {
                       tail(k1);
                       return;
                   }
                   k1.Call(writeBits, {data, whole}, bs, tail);
               });
    });
}

bool NetcModule::CalibrateBitStream(std::string* error) {
    // Write 20 bits (0xA5 0x5A and the low nibble of 0xC3) through WriteBits
    // and read them back. This checks the vtable slot numbers this build of
    // netc uses; WriteBits right-aligns a partial byte, so the tail is 0x3_.
    bool ok = false;
    const uint32_t bs = static_cast<uint32_t>(CallMethod(kAllocateNetBitStream, {}, &ok));
    if (!ok || !bs) {
        if (error) *error = "AllocateNetBitStream failed";
        return false;
    }
    GuestMemory& mem = machine_->Mem();
    const uint32_t vt = mem.Read32(bs);
    const uint8_t pattern[3] = {0xA5, 0x5A, 0xC3};
    uint32_t buf = 0;
    machine_->RunSync([&] {
        buf = mem.HeapAlloc(4, true);
        mem.WriteBytes(buf, pattern, 3);
    });
    machine_->CallSync(mem.Read32(vt + bsWriteBits_ * 4), bs, {buf, 20}, &ok);
    const uint32_t bits = static_cast<uint32_t>(machine_->CallSync(mem.Read32(vt + bsBitsUsed_ * 4), bs, {}, &ok));
    const uint32_t data = static_cast<uint32_t>(machine_->CallSync(mem.Read32(vt + bsGetData_ * 4), bs, {}, &ok));
    const uint32_t readOffset = static_cast<uint32_t>(machine_->CallSync(mem.Read32(vt + bsGetReadOffset_ * 4), bs, {}, &ok));
    uint8_t back[3] = {};
    mem.ReadBytes(data, back, 3);
    machine_->CallSync(VCall(kDeallocateNetBitStream), cnet_, {bs}, &ok);
    FreeGuest(buf);
    const bool match = bits == 20 && back[0] == 0xA5 && back[1] == 0x5A && (back[2] & 0xF0) == 0x30 && readOffset == 0;
    NETEMU_LOGI("bitstream calibration: bits=%u data=%02X %02X %02X readOffset=%u -> %s", bits, back[0], back[1], back[2], readOffset,
                match ? "ok" : "MISMATCH");
    if (!match && error) *error = "NetBitStreamInterface vtable layout does not match";
    return match;
}

bool NetcModule::Load(const NetcConfig& config, std::string* error) {
    auto fail = [&](const std::string& why) {
        lastError_ = why;
        if (error) *error = why;
        NETEMU_LOGE("netc load failed: %s", why.c_str());
        Unload();
        return false;
    };
    std::vector<uint8_t> file;
    if (!ReadFile(config.dllPath, &file)) return fail("cannot read " + config.dllPath);
    ::mkdir(config.dataDir.c_str(), 0700);

    machine_ = std::make_unique<Machine>();
    std::string err;
    if (!machine_->Init(&err)) return fail(err);
    Win32Env env;
    env.dataDir = config.dataDir;
    env.moduleGuestPath = env.mtaDir + "\\mta\\netc.dll";
    env.moduleHostPath = config.dllPath;
    state_ = std::make_unique<Win32State>(*machine_, env);
    InstallAll(*state_);
    // netc validates its install through the registry before it will run.
    for (const char* ver : {"1.7", "1.6", "1.5"})
        state_->SeedMtaRegistry(config.registryPath, ver, env.mtaDir);

    image_ = std::make_unique<PeImage>();
    Machine* m = machine_.get();
    const bool mapped = image_->Load(machine_->Mem(), file, [m](const std::string& dll, const std::string& name, uint16_t ord) {
        uint32_t t = m->ResolveImport(dll, name, ord);
        if (!t) {
            // Bind to a thunk that names the import when it is reached.
            const std::string label = dll + "!" + (name.empty() ? "#" + std::to_string(ord) : name);
            t = m->MakeThunk(label, 0, [label](ShimCall& c) {
                NETEMU_LOGE("netc called unimplemented import %s from 0x%08X", label.c_str(), c.ReturnAddress());
                c.Exit(0xC0000139u);  // STATUS_ENTRYPOINT_NOT_FOUND
            });
        }
        return t;
    }, &err);
    if (!mapped) return fail("PE load: " + err);
    const PeTls& tls = image_->Tls();
    machine_->SetMainModule(image_->Base(), image_->EntryPoint(), tls.callbacks, tls.rawStart, tls.rawEnd, tls.zeroFill, tls.indexVa);
    PeImage* img = image_.get();
    state_->mainExport = [img](const std::string& name) { return img->Export(name); };
    InstallHostThunks();
    if (!machine_->Start()) return fail("emulator thread did not start");

    bool ok = false;
    const uint32_t base = image_->Base();
    for (uint32_t cb : tls.callbacks) {
        machine_->CallSync(cb, 0, {base, 1, 0}, &ok);
        if (!ok) return fail("TLS callback faulted: " + machine_->LastFault());
    }
    if (image_->EntryPoint()) {
        const uint64_t r = machine_->CallSync(image_->EntryPoint(), 0, {base, 1, 0}, &ok, 60000);
        if (!ok) return fail("DllMain faulted: " + machine_->LastFault());
        if (!(r & 0xFFFFFFFFu)) return fail("DllMain(DLL_PROCESS_ATTACH) returned FALSE");
    }
    NETEMU_LOGI("netc DllMain done");

    // Directory exports take (const wchar_t*, size_t).
    auto setDir = [&](const char* exportName, const std::string& path) {
        const uint32_t fn = image_->Export(exportName);
        if (!fn) return;
        uint32_t w = 0;
        machine_->RunSync([&] { w = machine_->Mem().NewW(path); });
        machine_->CallSync(fn, 0, {w, static_cast<uint32_t>(path.size())}, &ok);
    };
    setDir("SetMTADirectory", env.mtaDir);
    setDir("SetGTADirectory", env.gtaDir);

    if (const uint32_t initRev = image_->Export("InitNetRev")) {
        const uint32_t a = GuestString(config.registryPath);
        const uint32_t b = GuestString(config.commonDataDir);
        const uint32_t c = GuestString(config.productVersion);
        machine_->CallSync(initRev, 0, {a, b, c}, &ok);
        if (!ok) return fail("InitNetRev faulted: " + machine_->LastFault());
    }

    // loader-proxy runs the anti-cheat service stage inside the game process
    // before core.dll loads: CheckService(CHECK_SERVICE_POST_CREATE).
    if (const uint32_t checkService = image_->Export("CheckService")) {
        const uint64_t r = machine_->CallSync(checkService, 0, {7}, &ok, 60000);
        if (!ok) return fail("CheckService faulted: " + machine_->LastFault());
        NETEMU_LOGI("netc CheckService(POST_CREATE) -> %u", static_cast<unsigned>(r & 0xFF));
    }

    if (const uint32_t check = image_->Export("CheckCompatibility")) {
        auto moduleVersion = [&]() {
            uint32_t out = 0;
            machine_->RunSync([&] { out = machine_->Mem().HeapAlloc(4, true); });
            machine_->CallSync(check, 0, {1, out}, &ok);
            const uint32_t version = machine_->Mem().Read32(out);
            FreeGuest(out);
            return version;
        };
        const uint32_t wanted = config.netModuleVersion ? config.netModuleVersion : moduleVersion();
        const uint64_t compatible = machine_->CallSync(check, 0, {wanted, 0}, &ok);
        if (!(compatible & 0xFF)) {
            char msg[128];
            std::snprintf(msg, sizeof(msg), "network module not compatible (expected 0x%X, got 0x%X)", wanted, moduleVersion());
            return fail(msg);
        }
        moduleVersion_ = wanted;
        NETEMU_LOGI("netc module version 0x%X", wanted);
    }

    const uint32_t init = image_->Export("InitNetInterface");
    if (!init) return fail("InitNetInterface export missing");
    cnet_ = static_cast<uint32_t>(machine_->CallSync(init, 0, {fakeCore_}, &ok, 60000));
    if (!ok || !cnet_) return fail("InitNetInterface failed: " + machine_->LastFault());
    NETEMU_LOGI("netc CNet at 0x%08X, net rev %u rel %u", cnet_, GetNetRev(), GetNetRel());

    if (!CalibrateBitStream(&err)) return fail(err);
    CallMethod(kRegisterPacketHandler, {handlerThunk_});
    return true;
}

bool NetcModule::StartNetwork(const std::string& host, uint16_t port, bool packetTag) {
    const uint32_t h = GuestString(host);
    bool ok = false;
    const uint64_t r = CallMethod(kStartNetwork, {h, port, packetTag ? 1u : 0u}, &ok);
    FreeGuest(h);
    return ok && (r & 0xFF);
}

void NetcModule::StopNetwork() { CallMethod(kStopNetwork, {}); }
void NetcModule::DoPulse() { CallMethod(kDoPulse, {}); }
bool NetcModule::IsConnected() { return CallMethod(kIsConnected, {}) & 0xFF; }
bool NetcModule::IsReady() { return CallMethod(kIsReady, {}) & 0xFF; }
int NetcModule::GetPing() { return static_cast<int>(CallMethod(kGetPing, {})); }
uint32_t NetcModule::GetTime() { return static_cast<uint32_t>(CallMethod(kGetTime, {})); }
void NetcModule::SetServerBitStreamVersion(uint16_t v) { CallMethod(kSetServerBitStreamVersion, {v}); }
uint16_t NetcModule::GetServerBitStreamVersion() { return static_cast<uint16_t>(CallMethod(kGetServerBitStreamVersion, {})); }
uint8_t NetcModule::GetConnectionError() { return static_cast<uint8_t>(CallMethod(kGetConnectionError, {})); }
uint16_t NetcModule::GetNetRev() { return static_cast<uint16_t>(CallMethod(kGetNetRev, {})); }
uint16_t NetcModule::GetNetRel() { return static_cast<uint16_t>(CallMethod(kGetNetRel, {})); }
void NetcModule::SetTimeoutTime(uint32_t ms) { CallMethod(kSetTimeoutTime, {ms}); }
void NetcModule::Reset() { CallMethod(kReset, {}); }

std::string NetcModule::GetSerial() {
    uint32_t buf = 0;
    machine_->RunSync([&] { buf = machine_->Mem().HeapAlloc(64, true); });
    CallMethod(kGetSerial, {buf, 64});
    const std::string serial = machine_->Mem().ReadA(buf, 64);
    FreeGuest(buf);
    return serial;
}

std::string NetcModule::GetConnectedServer(bool includePort) {
    const uint32_t s = static_cast<uint32_t>(CallMethod(kGetConnectedServer, {includePort ? 1u : 0u}));
    return s ? machine_->Mem().ReadA(s, 256) : std::string();
}

bool NetcModule::SendPacket(uint8_t packetId, const uint8_t* data, size_t bits, int priority, int reliability, int ordering) {
    if (!cnet_) return false;
    const uint32_t bytes = static_cast<uint32_t>((bits + 7) / 8);
    uint32_t buf = 0;
    machine_->RunSync([&] {
        buf = machine_->Mem().HeapAlloc(bytes ? bytes : 1, false);
        if (buf && bytes) machine_->Mem().WriteBytes(buf, data, bytes);
    });
    if (!buf) return false;
    bool ok = false;
    const uint64_t r = machine_->CallSync(sendThunk_, 0,
                                          {packetId, buf, static_cast<uint32_t>(bits), static_cast<uint32_t>(priority),
                                           static_cast<uint32_t>(reliability), static_cast<uint32_t>(ordering)},
                                          &ok);
    FreeGuest(buf);
    return ok && (r & 0xFF);
}

bool NetcModule::PopPacket(NetcPacket* out) {
    std::lock_guard lk(packetsMutex_);
    if (packets_.empty()) return false;
    *out = std::move(packets_.front());
    packets_.pop_front();
    return true;
}

}  // namespace NetEmu
