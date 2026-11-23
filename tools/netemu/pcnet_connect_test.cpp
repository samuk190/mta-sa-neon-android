// Host end-to-end check of net/PcNetworkTransport against a real MTA server:
// connect through the emulated netc.dll (dlopen libsaonline_netemu.so) and wait
// for PACKET_ID_MOD_NAME (7). netc performs the internal join (PLAYER_JOIN and
// the SERVER_JOIN packets) itself, as for the desktop CConnectManager. Run
// against a local server only.
// Usage: pcnet_connect_test <netc.dll> <data-dir> <host> <port> [productVersion]

#include "net/PcNetworkTransport.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

using WorldRPG::Net::PcNetworkTransport;

int main(int argc, char** argv) {
    if (argc < 5) {
        std::fprintf(stderr, "usage: %s <netc.dll> <data-dir> <host> <port> [productVersion]\n", argv[0]);
        return 2;
    }
    PcNetworkTransport::Options opt{argv[1], argv[2], argc > 5 ? argv[5] : ""};
    std::string error;
    auto t = PcNetworkTransport::Start(opt, argv[3], static_cast<uint16_t>(std::atoi(argv[4])), &error);
    if (!t) {
        std::fprintf(stderr, "start failed: %s\n", error.c_str());
        return 1;
    }
    const auto begin = std::chrono::steady_clock::now();
    bool connected = false;
    while (std::chrono::steady_clock::now() - begin < std::chrono::seconds(60)) {
        const auto status = t->Pulse(&error);
        if (status == PcNetworkTransport::Status::Failed) {
            std::fprintf(stderr, "FAILED: %s\n", error.c_str());
            return 1;
        }
        if (status == PcNetworkTransport::Status::Connected && !connected) {
            connected = true;
            std::printf("connected (serial %s, ping %d)\n", t->Serial().c_str(), t->Ping());
        }
        std::vector<uint8_t> packet;
        size_t bits = 0;
        while (t->Pop(packet, bits)) {
            std::printf("received packet id %u, %zu bits\n", packet[0], bits);
            if (packet[0] == 7) {
                std::printf("OK: MOD_NAME received\n");
                return 0;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    std::fprintf(stderr, "timed out\n");
    return 1;
}
