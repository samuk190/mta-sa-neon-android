#include "MtaClientTransport.h"
#include <array>
#include <chrono>
#include <iostream>
#include <thread>
using namespace WorldRPG::Net::Mta;
int main() {
    ClientTransport client;
    if (!client.startup(1,1) || !client.connect("worldrpgbroficial.duckdns.org",22003)) {
        std::cerr << client.lastError() << '\n'; return 1;
    }
    const auto deadline = std::chrono::steady_clock::now()+std::chrono::seconds(12);
    std::array<unsigned char,65536> buffer{};
    bool accepted = false;
    while (std::chrono::steady_clock::now()<deadline) {
        std::size_t bits = 0;
        const auto length = client.receive(buffer.data(),buffer.size(),&bits);
        if (!length) { std::this_thread::sleep_for(std::chrono::milliseconds(5)); continue; }
        std::cout << "{\"event\":\"native_client_packet\",\"id\":" << unsigned(buffer[0]) << ",\"bytes\":" << length << ",\"bits\":" << bits << "}\n";
        if (buffer[0] == 14) accepted = true;
        if (buffer[0] == 15 || buffer[0] == 20) { std::cerr << client.lastError() << '\n'; return 1; }
        if (buffer[0] == 106) {
            if (!accepted || !client.isConnected() || length < 15) return 1;
            std::cout << "{\"outcome\":\"native_client_mod_name\",\"bitstream\":" << unsigned(buffer[1] | (buffer[2]<<8)) << ",\"scope\":\"Transport, no game authentication\"}\n";
            client.shutdown();
            return 0;
        }
    }
    std::cerr << "Native client probe timed out: " << client.lastError() << '\n'; return 1;
}
