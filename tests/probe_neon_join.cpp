#include "RakNetWrapper.h"
#include "NeonJoinProtocol.h"
#include <chrono>
#include <iostream>
#include <thread>
using namespace WorldRPG::Net;
int main() {
    RakNetWrapper net;
    NeonJoinClient join;
    net.SetConnectionStateCallback([&](bool connected, const char* reason) {
        if (!connected) join.OnTransportFailure(reason ? reason : "Transport failed");
    });
    join.SetStateCallback([](JoinState state, const char* reason) {
        std::cout << "Join state: " << JoinStateName(state) << " detail=" << reason << '\n';
    });
    net.SetPacketHandler([&](std::uint8_t id,const std::uint8_t* data,std::size_t size,std::size_t bits) {
        if (id != 99 || size < 1 || bits < 8) return false;
        std::cout << "Application logical ID=" << unsigned(data[0]) << " bytes=" << size-1 << " bits=" << bits-8 << '\n';
        return join.ProcessGamePacket(data[0],data+1,size-1,bits-8);
    });
    if (!net.StartNetwork("worldrpgbroficial.duckdns.org",22003) || !join.Begin(net,"NativeProbe","")) return 1;
    const auto deadline = std::chrono::steady_clock::now()+std::chrono::seconds(25);
    while (std::chrono::steady_clock::now()<deadline) {
        net.DoPulse(); join.Pulse();
        if (join.State() == JoinState::Complete) {
            std::cout << "NEON_JOIN_COMPLETE (resource execution not part of this probe)\n";
            net.StopNetwork(); return 0;
        }
        if (join.State() == JoinState::Failed) {
            std::cout << "NEON_JOIN_FAILED " << join.Info().lastError << '\n';
            net.StopNetwork(); return 1;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    net.StopNetwork();
    std::cerr << "Join probe reached deadline\n"; return 1;
}
