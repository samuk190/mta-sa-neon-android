# Android Physical Device Validation — 2026-11-11

The offline GTA engine checkpoint was successfully verified on physical hardware (Samsung Galaxy S25). The updated APK was installed and audited. The local single-player GTA world, rendering loop, and input systems are fully operational.

---

## 1. Test Environment & Artifacts

| Component | Value |
|---|---|
| Installed APK | `outputs/worldrpg-neon-device.apk`, debug variant, package `com.worldrpg.neonlauncher` |
| APK SHA-256 | `039d095d72eba94e452efe0da67c313d5853d1bf20ef469f25969a2a9bd14b8a` |
| Debug Keystore | Preserved to allow updating without clearing application data; v2/v3 signing schemes validated |
| Build System | Oracle ARM64 Builder, NDK `25.1.8937393`, Gradle 8.4 |
| Target Device | Samsung SM-S938B (Galaxy S25 Ultra), Android 15/17, aarch64, 4,096-byte memory pages |
| Native GTA Engine | `libGTASA.so` (2.10 War Drum Studios ARM64) |
| Asset Manifest | 358 verified SHA-256 hashes matching generation |

ELF and packaging audits confirm 64KB alignment for the prebuilt GTA engine and 16KB alignment for our custom native modules.

---

## 2. Test Execution & Verified Scenarios

| Scenario | Result | Evidence |
|---|---|---|
| **World Boot & Render** | World geometry, CJ model, and HUD rendered properly | `outputs/engine-final-world.png` |
| **Touch Controls & Resume** | Smooth on-screen touch navigation and graceful pause/resume cycles | `outputs/engine-player-movement.png`, `engine-resumed-world.png` |
| **Graceful World Exit** | Native loop exits before `onPause`; 41ms wait, cleanly returns to launcher UI | `outputs/engine-isolated-exit.log` |
| **Reopening & Cycle Retention** | New engine instance boots cleanly; exit completed in 34ms | `outputs/engine-isolated-reopen-resume.log` |
| **Rapid Early Exit** | Immediate termination without hangs; 18ms clean shutdown | `outputs/engine-isolated-early-exit.log` |
| **Task Re-use (Minimized GTA)** | Single preparation, same engine PID preserved; clean resumption in 33ms | `outputs/engine-final-host-reuse.log` |
| **Network Handshake Timeout** | Single clean terminal error: `Connection attempt failed` without cascade timeouts | `outputs/neon-transport-failure-final.log` |
| **Join Codec Regression** | PASSED on Oracle: verified late packet delivery, retry sequences, and disconnection handling | `outputs/join-tests-final.log` |
| **MTA Structural Codec** | PASSED on Oracle with ASan/UBSan: verified TLV boundaries and truncated payload rejections | `tests/evidence/mta_offline_codec_oracle.json` |

---

## 3. Architecture & Lifecycle Safeguards

1. **Dedicated Process Isolation (`:engine`)**:
   - The game engine runs inside an isolated Android service/process (`:engine`).
   - Upon session exit or error, the `:engine` process terminates completely, guaranteeing that static C++ singletons in `libGTASA.so` are wiped and re-initialized cleanly on subsequent launches.
   - The launcher UI process remains completely unaffected and responsive.
2. **Back Navigation & EGL Teardown**:
   - The Android Back gesture notifies the engine while the EGL Surface is still valid.
   - Processing one frame after resume confirms that game state advancement has unblocked before triggering teardown.
   - Eliminates deadlocks during paused shutdowns and avoids mutex destruction while in-flight locks exist.
3. **Re-use of Existing Host**:
   - Re-opening from the launcher brings an active task to the front (`FLAG_ACTIVITY_REORDER_TO_FRONT`), avoiding duplicate engine instances within the same process.
