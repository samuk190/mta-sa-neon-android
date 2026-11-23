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
| `Win32Stubs.cpp` | Desktop-only surface (windows, clipboard, certificates, DPAPI, services, Credential Manager) answered the way a clean headless Windows process would. |
| `Win32Wmi.cpp` | An in-process WMI: `CoCreateInstance(CLSID_WbemLocator)` and the COM tree behind it (locator, services, enumerator, class objects) answering the hardware classes netc fingerprints. |
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

`netc.dll` is MTA's binary, so it is never committed here. Two builds exist:

- **official**: `mirror-cdn.multitheftauto.com/bdata/netc.dll`, which is also
  the one the Neon PC client ships. Every server uses it: an official MTA 1.6
  server ignores the fork-support build at the RakNet step.
- **fork**: `bdata/fork-support/netc.dll`, MTA's module for fork builds. Kept
  configurable, not picked automatically.

`.env` picks how it reaches the phone (the question is only ever asked when the
compatibility box is ticked):

- **Bundled** (`NETC_BUNDLE_IN_ASSETS=true`, our own builds): Gradle copies the
  files named by `NETC_OFFICIAL_DLL` and `NETC_FORK_DLL` into
  `assets/netc/official/netc.dll` and `assets/netc/fork/netc.dll`
  (`prepareNetcAssets`); `BuildConfig.NETC_BUNDLED_OFFICIAL/FORK` say which exist.
- **Imported** (`false`, the default): the player picks a `netc.dll` from their
  own PC install, or downloads it from `NETC_OFFICIAL_DOWNLOAD_URL` /
  `NETC_FORK_DOWNLOAD_URL`.

The InitNetRev product version follows the server, not the DLL: Neon servers
(Neon registry, or MTA 1.7) get `1.7.0-1.00000`, others `1.6.0-9.23081`.

## In the launcher

The join dialog's **Use PC Network — Compatibility** checkbox
(`ui/JoinDialog.kt`, `network/PcNetworkSettings.kt`) is per server. When it is
ticked and the module is not on the phone yet, `ui/NetcSourceDialog.kt` asks
once: choose the file, or download it. `network/NetcModuleStore.kt` stores it as
`files/netc/<build>/netc.dll` after checking it is an i386 DLL exporting
`InitNetInterface`, and each build gets its own emulator data folder
`files/netemu/<build>/` (registry, identity). The path, build, data folder and
product version go to native code with `EngineHostActivity.nativeSetPcNetwork`
before `nativeConnect`.

## Testing a join on the host

`tools/headless` runs the real client join; `--netc <dll> --netc-data <dir>`
makes it connect through a host build of `libsaonline_netemu.so` (on
`LD_LIBRARY_PATH`). MTA servers ignore loopback clients, so point it at the
server's LAN address. Against an official 1.6 server the join gets
`SERVER_JOIN`, `MOD_NAME` and sends `PLAYER_JOINDATA`; the server then needs
the Neon protocol the Android client speaks (MTA 0x170, bitstream 0x30), so a
Neon server is the end-to-end target. Against one (213.32.90.138:22013) the
join completed through this path (SERVER_JOINEDGAME, then the initial data
stream) once the netcode matched what that server runs (release 189, 0x41E6;
the client's default is 0x41E7).

## Building

`netemu/CMakeLists.txt` builds `libsaonline_netemu.so` (arm64, 16 KB pages,
about 2.2 MB stripped) with Unicorn 2.1.4 linked in statically, x86 only.
CMake downloads the pinned release (SHA-256 checked); set `UNICORN_SOURCE_DIR`
in `.env` to an unpacked 2.1.4 tree to build offline. The library exports one
symbol, `netemu_get_api`, which returns the C function table in
`NetEmuApi.h`; nothing else (not even its static libc++) is visible to the
main library.

Host tools (link the host Unicorn): `tools/netemu/netemu_probe.cpp` loads a
netc build and prints its identity; `tools/netemu/netemu_api_test.cpp` does
the same through the C ABI.

## Licensing

Unicorn is GPLv2; the repository is GPL-2.0-only, so the emulator links under
the same terms. It still builds into a **separate** shared library
(`libsaonline_netemu.so`) that is loaded only when the compatibility option is
on, so players who never use it don't pay for it in memory or startup time.

## Fidelity notes

netc's anti-tamper code checks that it runs on a real Windows box, so the
emulator reproduces behaviour a shortcut would get wrong:

- **Exceptions.** Access violations, illegal and privileged instructions,
  divide errors and `int3` are delivered like `KiUserExceptionDispatcher`
  does: a `CONTEXT` and `EXCEPTION_RECORD` on the faulting stack, vectored
  handlers first, then the `fs:[0]` chain. netc probes for VMs with
  VirtualPC's illegal opcode and VMware's backdoor port and expects the fault.
- **Hookable API thunks.** Each thunk starts with the hot-patchable
  `mov edi,edi; push ebp; mov ebp,esp` prologue, so Detours (which netc uses
  on `AddVectoredExceptionHandler`) can attach; a patched entry runs the
  guest's detour, and the trampoline's jump back lands on the shim.
- **Page protections.** `VirtualQuery` reports per-section protections of the
  image (headers read-only, `.text` execute-read, data read-write) and what
  `VirtualProtect`/`VirtualAlloc` set. netc verifies its `.cpivg` section is
  not executable.
- **Identity.** Disk serial (storage IOCTLs, fixed-media geometry), adapter
  MAC, volume serial, WMI and `HARDWARE\DESCRIPTION` all derive from one
  per-install seed and agree with each other. The computer's own host name
  resolves locally (no DNS round trip).

Debug aids (host or device, via environment): `NETEMU_TRACE_SHIMS=1` logs
every shim call; `NETEMU_TRACE_CALL=0xADDR` logs entries to a guest function
with its first argument; `NETEMU_WATCH=0xADDR` logs writes to a dword;
`NETEMU_WATCH_THUNKS=1` logs guest writes into the thunk area (inline hooks).

Known limitation: a privileged `in`/`out` raises its exception at the end of
the Unicorn translation block, so later instructions in the same block run
first. The `CONTEXT` is taken at the instruction, so registers are right; only
memory stores in that tail would differ from Windows.

## Status (device-unverified)

Driven on the host with `netemu_probe` against both the Neon and the stock PC
`netc.dll`: each loads end to end. `DllMain`, `InitNetRev`,
`CheckService(POST_CREATE)`, `CheckCompatibility` and `InitNetInterface` run,
a live `CNet` comes back, the `NetBitStreamInterface` slots calibrate, and
`GetSerial` returns a stable 32-digit MTA serial. A cold load takes about
0.5 s (Neon) and 1.9 s (stock) on a desktop host, warm about 0.3 s and 0.5 s.
Init does no network I/O.

Not yet done (next lots): the separate `.so` build and asset/import wiring,
the server-select checkbox, and routing `NetcModule` into the transport path
for a live join against a server.
