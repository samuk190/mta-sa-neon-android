// Host check of the libsaonline_netemu C ABI (NetEmuApi.h): load a netc
// build through netemu_get_api(), read its serial, poll for packets, unload.
// Usage: netemu_api_test <netc.dll> <data-dir>

#include "netemu/NetEmuApi.h"

#include <cstdio>
#include <cstring>

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <netc.dll> <data-dir>\n", argv[0]);
        return 2;
    }
    const NetEmuApi* api = netemu_get_api();
    if (!api || api->abi_version != NETEMU_ABI_VERSION) {
        std::fprintf(stderr, "ABI mismatch\n");
        return 1;
    }
    NetEmuConfig cfg{};
    cfg.size = sizeof(cfg);
    cfg.dll_path = argv[1];
    cfg.data_dir = argv[2];
    char err[256];
    NetEmuHandle* h = api->load(&cfg, err, sizeof(err));
    if (!h) {
        std::fprintf(stderr, "load failed: %s\n", err);
        return 1;
    }
    char serial[64];
    const size_t n = api->get_serial(h, serial, sizeof(serial));
    uint8_t id = 0;
    uint32_t bits = 0, off = 0;
    uint8_t buf[16];
    const int popped = api->pop_packet(h, &id, buf, sizeof(buf), &bits, &off);
    std::printf("api ok: serial=%s (%zu) connected=%d pop=%d over=%d\n", serial, n, api->is_connected(h), popped,
                api->session_over(h));
    api->unload(h);
    return n == 32 && popped == 0 ? 0 : 1;
}
