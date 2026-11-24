#pragma once
// "Use PC Network — Compatibility" transport: MTA's PC netc.dll running in
// libsaonline_netemu.so (cpp/netemu). The library is opened on first use;
// netc is loaded and connected on a worker thread so nativeConnect never
// waits for it. RakNetWrapper drives this the way it drives QUIC.
//
// Packets use the same shape as the QUIC path: byte 0 is the game packet id
// (MTA ePacketID, 0-based, which is also what CNet uses), then the payload,
// MSB-first.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

struct NetEmuApi;
struct NetEmuHandle;

namespace WorldRPG::Net {

class PcNetworkTransport {
public:
    struct Options {
        std::string netcPath;        // files/netc/<build>/netc.dll
        std::string dataDir;         // files/netemu/<build>/
        std::string productVersion;  // InitNetRev product version for that build
    };

    enum class Status { Loading, Connecting, Connected, Failed };

    // Starts loading netc and connecting to host:port. Null only when the
    // emulator library cannot be opened (the error says why).
    static std::unique_ptr<PcNetworkTransport> Start(const Options& options, const std::string& host, uint16_t port,
                                                     std::string* error);
    ~PcNetworkTransport();
    PcNetworkTransport(const PcNetworkTransport&) = delete;
    PcNetworkTransport& operator=(const PcNetworkTransport&) = delete;

    // Runs CNet::DoPulse and reports where the session is. On Failed, error
    // holds the reason.
    Status Pulse(std::string* error);
    // Next received game packet as [id][payload]; false when none is waiting.
    bool Pop(std::vector<uint8_t>& packet, size_t& bits);
    // Sends game packet `id` with `bits` bits of payload (MSB-first).
    bool Send(uint8_t id, const uint8_t* payload, size_t bits, int priority, int reliability, int ordering);
    int Ping();
    std::string Serial();
    bool IsConnected();
    uint8_t ConnectionError();
    // Applied now, or as soon as netc is up.
    void SetServerBitStreamVersion(uint16_t version);
    void SetTimeoutTime(uint32_t ms);
    // CNet::DeobfuscateScript through the emulated netc (encrypted
    // luac.mtasa.com scripts). False until netc is up or when it refuses.
    bool DeobfuscateScript(const uint8_t* data, size_t size, const std::string& name, std::vector<uint8_t>& out);

private:
    PcNetworkTransport() = default;
    struct Session;
    bool Up();  // netc loaded and StartNetwork done

    // Shared with the loader thread, so destroying the transport mid-load
    // never waits: the loader unloads netc itself when it finishes.
    std::shared_ptr<Session> session_;
    bool connected_ = false;
    uint64_t connectStartMs_ = 0;
    std::vector<uint8_t> scratch_;
};

}  // namespace WorldRPG::Net
