// Host-only NetEmuApi fixture. It returns the recorded terminal error without opening a socket.
#include "netemu/NetEmuApi.h"

struct NetEmuHandle {};

extern "C" const NetEmuApi* netemu_get_api() {
    static const NetEmuApi api = [] {
        NetEmuApi a{};
        a.abi_version = NETEMU_ABI_VERSION;
        a.load = [](const NetEmuConfig*, char*, size_t) { return new NetEmuHandle; };
        a.unload = [](NetEmuHandle* h) { delete h; };
        a.start_network = [](NetEmuHandle*, const char*, uint16_t, int) { return 1; };
        a.do_pulse = a.stop_network = [](NetEmuHandle*) {};
        a.session_over = [](NetEmuHandle*) { return 0; };
        a.get_connection_error = [](NetEmuHandle*) -> uint8_t { return 20; };
        a.pop_packet = [](NetEmuHandle*, uint8_t*, uint8_t*, size_t, uint32_t*, uint32_t*) { return 0; };
        return a;
    }();
    return &api;
}
