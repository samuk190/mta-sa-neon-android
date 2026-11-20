// Join-only network probe: no engine, scripts, account login or chat commands.
// Stops as soon as the server accepts/refuses PLAYER_JOINDATA.
#include "JoinDataCodec.h"
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>
using namespace WorldRPG::Net;

int main(int argc, char** argv) {
    if (argc != 4) { std::cerr << "usage: probe_join_profiles host port auto|standard|legacy\n"; return 2; }
    const std::string mode = argv[3];
    if (mode != "auto" && mode != "standard" && mode != "legacy") return 2;
    auto requestedProfile = JoinProfile::Automatic;
    if (mode == "standard") requestedProfile = JoinProfile::Standard;
    if (mode == "legacy") requestedProfile = JoinProfile::LegacyMobile;
    const auto profile = SelectJoinProfile(requestedProfile);
    RakNetWrapper net;
    bool finished = false, accepted = false, sent = false;
    net.SetConnectionStateCallback([&](bool connected, const char* reason) {
        std::cout << "Transport connected=" << connected << " " << (reason ? reason : "") << '\n';
        if (!connected) finished = true;
    });
    net.SetPacketHandler([&](uint8_t id, const uint8_t* data, size_t size, size_t bits) {
        if (id != 99 || !size) return false;
        std::cout << "Application packet=" << unsigned(data[0]) << " bytes=" << size << '\n';
        if (data[0] == 7 && !sent) {
            BitStreamReader reader(data+1, size-1, bits >= 8 ? bits-8 : 0);
            uint16_t version = 0; std::string mod; uint8_t auth = 0;
            if (!reader.Read(version) || !reader.ReadString(mod,32)) { finished = true; return true; }
            if (reader.GetRemainingBits() >= 8) reader.Read(auth);
            std::cout << "MOD_NAME version=" << version << " auth=" << unsigned(auth) << '\n';
            if (auth == 2) { finished = true; std::cout << "IDENTITY_REQUIRED\n"; return true; }
            BitStreamWriter packet;
            if (!BuildJoinData(profile,"JoinCompatProbe","","",packet)) { finished = true; return true; }
            sent = net.SendPacket(99,packet,PacketPriority::HIGH,PacketReliability::RELIABLE_ORDERED);
            std::cout << "Join sent=" << sent << " logical id=" << unsigned(packet.GetData()[0]) << '\n';
        } else if (data[0] == 2) { accepted = finished = true; }
        else if (data[0] == 23) { finished = true; }
        return true;
    });
    if (!net.StartNetwork(argv[1],static_cast<uint16_t>(std::atoi(argv[2])))) return 1;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (!finished && std::chrono::steady_clock::now() < deadline) {
        net.DoPulse(); std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    net.StopNetwork();
    std::cout << (accepted ? "JOIN_DATA_ACCEPTED" : "JOIN_DATA_NOT_ACCEPTED") << '\n';
    return accepted ? 0 : 1;
}
