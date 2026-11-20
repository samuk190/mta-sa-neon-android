## Device check (2026-11-20)

Built and installed this branch on a real device to see how far the change gets.

**Device:** Redmi Note 13 Pro 5G (`garnet_global`, `2312DRA50G`), Android 16 / SDK 36, `arm64-v8a`, page size 4096.

**Build:** `./gradlew :app:assembleDebug -PnoGameFiles` (SDK 34, NDK 25.1.8937393, CMake 3.22.1, JDK 17 — AGP 8.2 fails on JDK 21 in `jlink`).

### Passed

- APK installs and the launcher starts on Android 16: `MainActivity` focused, no `FATAL EXCEPTION`, no `UnsatisfiedLinkError`.
- Server browser works: 11 servers listed with ping and player counts, so the network path is alive.
- The shipped `libWorldRPGNeon.so` contains the symbols this branch adds:
  `WorldRPG::RemoteVehicle::ApplyUnoccupied`, `WorldRPG::VehicleSync::WriteUnoccupiedEntry`,
  `WorldRPG::Net::NeonJoinClient::Impl::SendUnoccupiedVehicleSync`.

### Not verified

- **Packet 56 was never executed at runtime.** The native library only loads when a server is joined, and joining requires the GTA San Andreas Mobile 2.10 files (`libGTASA.so`, `libSCAnd.so`, `libOpenAL64.so` plus the OBB data), which the test device does not have. `README.md:17-21` states this requirement.
- Therefore this change is backed by the host tests (`neon_vehicle_sync_tests`, 15 checks) and by the desktop reference, **not** by an in-game acceptance run.
- `mta_neon_source` is not present in this working copy, so the line-by-line comparison against `CUnoccupiedVehicleSync.cpp:178-208` could not be re-run here. The read order and field widths in the commit follow the ranges already recorded in `AGENTS.md` and `docs/sync-port/`; a reviewer with the reference tree should confirm them.

### Install note (HyperOS)

`adb install` is refused with `INSTALL_FAILED_USER_RESTRICTED` on HyperOS 3.0 unless *Install via USB* is enabled (Mi account + SIM). Installing through the system package installer works and is what was used here.
