#pragma once
// C ABI of libsaonline_netemu.so. The main library opens it with dlopen only
// when "Use PC Network — Compatibility" is on, resolves netemu_get_api and
// talks to the emulated netc.dll through this table. Plain C types only, so
// the two libraries share no C++ runtime state (each links its STL statically).
//
// Threading: every function may be called from any thread except the
// emulator's own; calls on one handle are serialized by the caller.

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Bump on any incompatible change to the structs or the function table.
#define NETEMU_ABI_VERSION 1

typedef struct NetEmuConfig {
    uint32_t size;                // sizeof(NetEmuConfig), for forward compatibility
    const char* dll_path;         // host path of netc.dll
    const char* data_dir;         // writable host directory (emulated drive, registry, identity)
    const char* registry_path;    // InitNetRev argument 1; NULL = "Software\\Multi Theft Auto: San Andreas All"
    const char* common_data_dir;  // InitNetRev argument 2; NULL = "MTA San Andreas All"
    const char* product_version;  // InitNetRev argument 3, e.g. "1.6.0-9.23081"; NULL = that default
    uint32_t net_module_version;  // MTA_DM_CLIENT_NET_MODULE_VERSION; 0 = ask netc
} NetEmuConfig;

typedef struct NetEmuHandle NetEmuHandle;

typedef struct NetEmuApi {
    uint32_t abi_version;  // NETEMU_ABI_VERSION of the library

    // Loads netc and runs it up to a live CNet. NULL on failure, with the
    // reason copied into error (always NUL-terminated when error_size > 0).
    NetEmuHandle* (*load)(const NetEmuConfig* config, char* error, size_t error_size);
    void (*unload)(NetEmuHandle* h);

    int (*start_network)(NetEmuHandle* h, const char* host, uint16_t port, int packet_tag);
    void (*stop_network)(NetEmuHandle* h);
    void (*do_pulse)(NetEmuHandle* h);
    int (*is_connected)(NetEmuHandle* h);
    int (*is_ready)(NetEmuHandle* h);
    int (*get_ping)(NetEmuHandle* h);
    uint8_t (*get_connection_error)(NetEmuHandle* h);
    void (*set_server_bitstream_version)(NetEmuHandle* h, uint16_t version);
    void (*set_timeout_time)(NetEmuHandle* h, uint32_t ms);
    // Copies the MTA serial (32 hex digits); returns its length.
    size_t (*get_serial)(NetEmuHandle* h, char* out, size_t out_size);

    // Sends packet_id with `bits` bits of MSB-first data. Returns 1 when queued.
    int (*send_packet)(NetEmuHandle* h, uint8_t packet_id, const uint8_t* data, size_t bits, int priority, int reliability,
                       int ordering);
    // Takes the next received packet. Returns 0 when none is waiting; 1 when
    // one was copied (data holds ceil(bits/8) bytes); -1 when data_size is too
    // small, with *bits set and the packet left queued.
    int (*pop_packet)(NetEmuHandle* h, uint8_t* packet_id, uint8_t* data, size_t data_size, uint32_t* bits,
                      uint32_t* read_offset_bits);
    // 1 when this handle is finished: netc asked to end the process (an
    // anti-tamper verdict) or a guest call hung. Unload it and stop.
    int (*session_over)(NetEmuHandle* h);
} NetEmuApi;

// The one exported symbol.
const NetEmuApi* netemu_get_api(void);
typedef const NetEmuApi* (*NetEmuGetApiFn)(void);

#ifdef __cplusplus
}
#endif
