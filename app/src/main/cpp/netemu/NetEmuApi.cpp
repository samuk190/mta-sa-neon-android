// C ABI over NetcModule (see NetEmuApi.h).

#include "NetEmuApi.h"
#include <vector>
#include <cstdlib>

#include "NetcModule.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>

struct NetEmuHandle {
    NetEmu::NetcModule netc;
    std::mutex popMutex;
    NetEmu::NetcPacket pending;  // a packet that did not fit the caller's buffer
    bool hasPending = false;
};

namespace {

void CopyOut(const std::string& s, char* out, size_t size) {
    if (!out || !size) return;
    const size_t n = std::min(s.size(), size - 1);
    std::memcpy(out, s.data(), n);
    out[n] = 0;
}

NetEmuHandle* Load(const NetEmuConfig* config, char* error, size_t errorSize) {
    if (!config || config->size < sizeof(NetEmuConfig) || !config->dll_path || !config->data_dir) {
        CopyOut("invalid NetEmuConfig", error, errorSize);
        return nullptr;
    }
    NetEmu::NetcConfig c;
    c.dllPath = config->dll_path;
    c.dataDir = config->data_dir;
    if (config->registry_path) c.registryPath = config->registry_path;
    if (config->common_data_dir) c.commonDataDir = config->common_data_dir;
    if (config->product_version) c.productVersion = config->product_version;
    c.netModuleVersion = config->net_module_version;
    auto h = std::make_unique<NetEmuHandle>();
    std::string why;
    if (!h->netc.Load(c, &why)) {
        CopyOut(why, error, errorSize);
        return nullptr;
    }
    CopyOut("", error, errorSize);
    return h.release();
}

void Unload(NetEmuHandle* h) { delete h; }

int StartNetwork(NetEmuHandle* h, const char* host, uint16_t port, int packetTag) {
    return h && host && h->netc.StartNetwork(host, port, packetTag != 0) ? 1 : 0;
}
void StopNetwork(NetEmuHandle* h) {
    if (h) h->netc.StopNetwork();
}
void DoPulse(NetEmuHandle* h) {
    if (h) h->netc.DoPulse();
}
int IsConnected(NetEmuHandle* h) { return h && h->netc.IsConnected() ? 1 : 0; }
int IsReady(NetEmuHandle* h) { return h && h->netc.IsReady() ? 1 : 0; }
int GetPing(NetEmuHandle* h) { return h ? h->netc.GetPing() : -1; }
uint8_t GetConnectionError(NetEmuHandle* h) { return h ? h->netc.GetConnectionError() : 0; }
void SetServerBitStreamVersion(NetEmuHandle* h, uint16_t v) {
    if (h) h->netc.SetServerBitStreamVersion(v);
}
void SetTimeoutTime(NetEmuHandle* h, uint32_t ms) {
    if (h) h->netc.SetTimeoutTime(ms);
}
size_t GetSerial(NetEmuHandle* h, char* out, size_t size) {
    const std::string s = h ? h->netc.GetSerial() : std::string();
    CopyOut(s, out, size);
    return s.size();
}

int SendPacket(NetEmuHandle* h, uint8_t id, const uint8_t* data, size_t bits, int priority, int reliability, int ordering) {
    if (!h || (bits && !data)) return 0;
    return h->netc.SendPacket(id, data, bits, priority, reliability, ordering) ? 1 : 0;
}

int PopPacket(NetEmuHandle* h, uint8_t* id, uint8_t* data, size_t size, uint32_t* bits, uint32_t* readOffset) {
    if (!h) return 0;
    std::lock_guard lk(h->popMutex);
    if (!h->hasPending) {
        if (!h->netc.PopPacket(&h->pending)) return 0;
        h->hasPending = true;
    }
    const NetEmu::NetcPacket& p = h->pending;
    if (bits) *bits = p.bits;
    if (p.data.size() > size || (!data && !p.data.empty())) return -1;
    if (id) *id = p.id;
    if (!p.data.empty()) std::memcpy(data, p.data.data(), p.data.size());
    if (readOffset) *readOffset = p.readOffsetBits;
    h->hasPending = false;
    return 1;
}

int DropPacket(NetEmuHandle* h) {
    if (!h) return 0;
    std::lock_guard lk(h->popMutex);
    if (h->hasPending) {
        h->hasPending = false;
        h->pending = {};
        return 1;
    }
    NetEmu::NetcPacket skipped;
    return h->netc.PopPacket(&skipped) ? 1 : 0;
}

int SessionOver(NetEmuHandle* h) { return !h || h->netc.SessionOver() ? 1 : 0; }

int DeobfuscateScript(NetEmuHandle* h, const uint8_t* in, size_t size, const char* name, uint8_t** out, size_t* outSize) {
    if (out) *out = nullptr;
    if (outSize) *outSize = 0;
    if (!h || !out || !outSize) return 0;
    std::vector<uint8_t> plain;
    if (!h->netc.DeobfuscateScript(in, size, name ? name : "", plain)) return 0;
    auto* buffer = static_cast<uint8_t*>(std::malloc(plain.size()));
    if (!buffer) return 0;
    std::memcpy(buffer, plain.data(), plain.size());
    std::fill(plain.begin(), plain.end(), 0);
    *out = buffer;
    *outSize = plain.size();
    return 1;
}

void FreeBuffer(void* p) { std::free(p); }

const NetEmuApi kApi = {
    NETEMU_ABI_VERSION, Load,       Unload,          StartNetwork,         StopNetwork,
    DoPulse,            IsConnected, IsReady,        GetPing,              GetConnectionError,
    SetServerBitStreamVersion,       SetTimeoutTime, GetSerial,            SendPacket,
    PopPacket,          SessionOver,     DropPacket,
    DeobfuscateScript,  FreeBuffer,
};

}  // namespace

extern "C" __attribute__((visibility("default"))) const NetEmuApi* netemu_get_api(void) { return &kApi; }
