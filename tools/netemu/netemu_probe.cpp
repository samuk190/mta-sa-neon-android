// Host probe for the netc emulator: loads netc.dll, prints its identity and,
// with a server argument, connects and dumps the packets netc hands back.
//
//   netemu_probe <netc.dll> <data-dir> [host port [seconds]] [--version 0xNNN]

#include "netemu/NetcModule.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <netc.dll> <data-dir> [host port [seconds]] [--version 0xNNN]\n", argv[0]);
        return 2;
    }
    NetEmu::NetcConfig cfg;
    cfg.dllPath = argv[1];
    cfg.dataDir = argv[2];
    std::string host;
    int port = 0, seconds = 15;
    for (int i = 3; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--version") && i + 1 < argc) cfg.netModuleVersion = std::strtoul(argv[++i], nullptr, 0);
        else if (host.empty()) host = argv[i];
        else if (!port) port = std::atoi(argv[i]);
        else seconds = std::atoi(argv[i]);
    }
    NetEmu::NetcModule netc;
    std::string err;
    const auto t0 = std::chrono::steady_clock::now();
    if (!netc.Load(cfg, &err)) {
        std::fprintf(stderr, "LOAD FAILED: %s\n", err.c_str());
        return 1;
    }
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    std::printf("loaded in %lld ms: rev=%u rel=%u serial=%s\n", static_cast<long long>(ms), netc.GetNetRev(), netc.GetNetRel(),
                netc.GetSerial().c_str());
    if (host.empty()) return 0;
    if (!netc.StartNetwork(host, static_cast<uint16_t>(port), false)) {
        std::fprintf(stderr, "StartNetwork failed\n");
        return 1;
    }
    std::printf("StartNetwork(%s:%d) ok\n", host.c_str(), port);
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    bool wasConnected = false;
    while (std::chrono::steady_clock::now() < end) {
        netc.DoPulse();
        NetEmu::NetcPacket p;
        while (netc.PopPacket(&p)) {
            std::printf("packet id=%u bits=%u read=%u first=", p.id, p.bits, p.readOffsetBits);
            for (size_t i = 0; i < p.data.size() && i < 16; ++i) std::printf("%02X", p.data[i]);
            std::printf("\n");
        }
        const bool c = netc.IsConnected();
        if (c != wasConnected) {
            std::printf("connected=%d ping=%d error=%u\n", c, netc.GetPing(), netc.GetConnectionError());
            wasConnected = c;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(33));
    }
    netc.StopNetwork();
    return 0;
}
