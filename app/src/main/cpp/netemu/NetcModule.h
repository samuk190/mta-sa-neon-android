#pragma once
// Drives the emulated netc.dll the way MTA's core.dll does on Windows:
// DllMain, SetMTADirectory/SetGTADirectory, InitNetRev, CheckCompatibility,
// InitNetInterface, then the CNet methods (Client/sdk/net/CNet.h).

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace NetEmu {

class Machine;
class Win32State;
class PeImage;

struct NetcConfig {
    std::string dllPath;  // host path of netc.dll
    std::string dataDir;  // writable host directory for the emulated drive and registry
    // Strings core.dll passes to InitNetRev (SharedUtil::GetProductRegistryPath & co).
    std::string registryPath = "Software\\Multi Theft Auto: San Andreas All";
    std::string commonDataDir = "MTA San Andreas All";
    std::string productVersion = "1.6.0-9.23081";
    // MTA_DM_CLIENT_NET_MODULE_VERSION of the core this netc was built for.
    // 0 asks netc for its own version (CheckCompatibility(1, &out)), which is
    // what a matching core.dll would pass: 0x0B5 on public builds, 0x40B5 on Neon.
    uint32_t netModuleVersion = 0;
};

struct NetcPacket {
    uint8_t id = 0;               // ePacketID (game id) as netc reports it
    std::vector<uint8_t> data;    // bitstream bytes from bit 0
    uint32_t bits = 0;            // bits used
    uint32_t readOffsetBits = 0;  // where netc left the read cursor
};

class NetcModule {
public:
    NetcModule();
    ~NetcModule();
    NetcModule(const NetcModule&) = delete;
    NetcModule& operator=(const NetcModule&) = delete;

    bool Load(const NetcConfig& config, std::string* error);
    bool Loaded() const { return cnet_ != 0; }
    void Unload();

    // CNet. All of these are safe from any host thread except the emu thread.
    bool StartNetwork(const std::string& host, uint16_t port, bool packetTag);
    void StopNetwork();
    void DoPulse();
    bool IsConnected();
    bool IsReady();
    int GetPing();
    uint32_t GetTime();
    // Sends ucPacketID with a bitstream of `bits` bits copied from data.
    bool SendPacket(uint8_t packetId, const uint8_t* data, size_t bits, int priority, int reliability, int ordering);
    void SetServerBitStreamVersion(uint16_t version);
    uint16_t GetServerBitStreamVersion();
    std::string GetSerial();
    uint8_t GetConnectionError();
    uint16_t GetNetRev();
    uint16_t GetNetRel();
    std::string GetConnectedServer(bool includePort);
    void SetTimeoutTime(uint32_t ms);
    void Reset();

    // Packets handed to the registered packet handler since the last call.
    bool PopPacket(NetcPacket* out);
    std::string LastError() const;
    bool ProcessExitRequested() const;
    uint32_t ModuleVersion() const { return moduleVersion_; }

private:
    uint32_t VCall(uint32_t index) const;  // CNet vtable slot
    uint64_t CallMethod(uint32_t index, const std::vector<uint32_t>& args, bool* ok = nullptr);
    uint32_t GuestString(const std::string& s);
    void FreeGuest(uint32_t va);
    bool CalibrateBitStream(std::string* error);
    void InstallHostThunks();

    std::unique_ptr<Machine> machine_;
    std::unique_ptr<Win32State> state_;
    std::unique_ptr<PeImage> image_;
    uint32_t cnet_ = 0;
    uint32_t fakeCore_ = 0;
    uint32_t handlerThunk_ = 0;
    uint32_t sendThunk_ = 0;
    uint32_t moduleVersion_ = 0;

    // NetBitStreamInterface vtable slots, checked by CalibrateBitStream.
    uint32_t bsGetReadOffset_ = 1;
    uint32_t bsWriteBits_ = 23;
    uint32_t bsBitsUsed_ = 53;
    uint32_t bsGetData_ = 58;

    mutable std::mutex packetsMutex_;
    std::deque<NetcPacket> packets_;
    std::string lastError_;
};

}  // namespace NetEmu
