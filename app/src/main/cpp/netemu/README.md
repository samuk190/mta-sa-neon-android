# netemu — PC `netc.dll` under emulation on Android

This directory runs MTA's closed-source **`netc.dll`** (the PC network module,
x86/32-bit) on an arm64 Android phone, so the transport and the join handshake
behave like a real PC client. It backs the **"Use PC Network — Compatibility"**
option on the server-select screen: with it on, the client talks to a server
through the real PC netcode instead of the open Neon transport, which is what
makes joining backwards-compatible with unmodified servers.

We reuse the *approach* MobileSA uses (run the PC netcode under x86 emulation);
no code from that APK is used. `netc.dll` itself is MTA's binary and is never
committed here — it is supplied at runtime (see "Where netc comes from").

## Pieces

| File | Role |
|---|---|
| `GuestMemory.*` | One 2 GiB host reservation mapped as the guest's 32-bit address space, with a page allocator, a size-class heap, and string helpers. |
| `PeImage.*` | Maps a PE32 image into guest memory: sections, base relocations, imports (bound to shim thunks), exports, static TLS. Also `MapView` for `NtMapViewOfSection`. |
| `X86Machine.*` | The CPU: Unicorn (x86-32) driving guest code, cooperative guest threads with their own FS/TEB, a slice scheduler with a watchdog, Win32-style blocking via re-entrant shims, and host↔guest calls. |
| `Win32Shims.h` + `Win32State.cpp` | Shared shim state: handle table, kernel objects, path mapping, a file-backed registry, per-install identity. |
| `Win32Kernel32.cpp` | Process, modules, memory, time, code pages, locale, strings. |
| `Win32Sync.cpp` | Critical sections, SRW locks, condition variables, events/mutexes/semaphores/timers, threads, FLS, SEH, pthreads-win32. |
| `Win32Files.cpp` | Files and directories on a private host dir, plus the registry (A/W). |
| `Win32Winsock.cpp` | `ws2_32` over POSIX sockets; blocking modes and async DNS without stalling the CPU thread. |
| `Win32Ntdll.cpp` | `ntdll`/`psapi`/toolhelp: process & thread queries, virtual memory, native files, sections, the object manager, volume enumeration. |
| `Win32Stubs.cpp` | Desktop-only surface (windows, clipboard, COM, certificates, DPAPI, services) answered the way a clean headless Windows process would. |
| `NetcModule.*` | Drives `netc.dll` exactly as `core.dll` does: DllMain, `SetMTADirectory`/`SetGTADirectory`, `InitNetRev`, `CheckCompatibility`, `InitNetInterface`, then the `CNet` methods. Exposes a small C++ API (StartNetwork, DoPulse, SendPacket, PopPacket, …). |
| `../../../../tools/netemu/netemu_probe.cpp` | Host-only tool: loads a netc build, prints its identity, and (with a server) dumps the packets it hands back. Build it against the host Unicorn to debug without a device. |

## How a packet moves

`NetcModule` registers a host packet-handler thunk with netc's
`RegisterPacketHandler`. When netc dispatches a packet it calls that thunk; the
thunk reads the `NetBitStreamInterface` back through *its own* vtable
(bits-used, read-offset, data pointer) via guest continuations, copies the bytes
out, and queues a `NetcPacket`. The Android side drains the queue with
`PopPacket` and feeds it to the existing packet handling. Sending runs the
mirror image: Allocate → WriteBits → SendPacket → Deallocate as one guest call.

## Where netc comes from

`netc.dll` is MTA's binary, so it is never bundled in this repo. At runtime the
launcher supplies it one of two ways (chosen in `.env`, only asked when the
compatibility box is ticked):

- **bundle in APK assets** (`.env` default for our own builds): ship the DLL we
  are licensed to distribute under `assets/`, copied to app storage on first run;
- **user imports it**: the player points the launcher at a file (their own PC
  MTA install) or lets it download the stock module directly from MTA.

Two targets are supported, picked by the server's version: the Neon
`netc` (for Neon servers without the mobile transport) and the stock MTA
`netc` from `mirror-cdn.multitheftauto.com/bdata/fork-support/netc.dll`.

## Licensing

Unicorn is GPLv2. To keep the repository's MIT license clean, the emulator and
these shims build into a **separate** shared library (`libsaonline_netemu.so`)
that is loaded only when the compatibility option is on; the main
`libsaonlineneon.so` does not link Unicorn.

## Status (device-unverified)

Built and driven on the host with `netemu_probe` against both the Neon and the
stock PC `netc.dll`:

- both images map, relocate and bind imports with **0 unresolved**;
- the MSVC CRT initializes and `DllMain(DLL_PROCESS_ATTACH)` returns TRUE;
- `CheckCompatibility`, `InitNetRev` and the module-version check pass;
- netc's install validation passes (registry seeded; stock no longer shows
  "Error [U01]");
- the hardware-fingerprint path runs: volume serial, adapter MAC, DPAPI
  protect/unprotect round-trip, toolhelp/psapi module and process enumeration,
  service and debugger checks.

Remaining before a live join (next lots): stock `netc` reaches live DNS during
`InitNetInterface` (it contacts MTA's servers and needs real network); the Neon
build faults slightly earlier in a different fingerprint routine. After init
returns a `CNet`, the handshake is driven through `NetcModule` and wired to the
existing `RakNetWrapper` path behind the checkbox.
