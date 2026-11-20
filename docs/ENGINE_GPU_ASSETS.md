# GPU texture validation and engine diagnostics

GTA SA 2.10 ARM64 chooses texture databases using GL extensions, rather than the
phone model. `GpuProbe` reads them from a temporary OpenGL ES 2 pbuffer before
loading GTA or starting its native threads. `GpuTextureSupport` follows the
selection in `TextureDatabaseRuntime::Load` at `0x2877ec–0x287818`:

| GL capability | Automatic database format |
| --- | --- |
| `GL_IMG_texture_compression_pvrtc` | PVR |
| `GL_EXT_texture_compression_dxt1`, `GL_EXT_texture_compression_s3tc` or `GL_AMD_compressed_ATC_texture` | DXT |
| Otherwise | ETC |

PVR takes priority. The ATC-to-DXT rule is GTA's own database selection; the
database filename does not imply every texture inside uses the same encoding.

Before native initialization, the host requires readable, nonempty `.txt`,
`.tmb`, `.toc` and `.dat` files for `mobile`, `txd`, `gta3` and `gta_int` in that
format. `player` and `menu` always require PVR, matching
`CGame::InitialiseRenderWare` at `0x55b684–0x55b720` and the bundled
`UseHiDetailPlayer()` implementation, which returns false. Cutscenes are optional
in this port; an installed cutscene catalogue requires its selected-format files.

For example, missing ETC files produce an English dialog beginning with
`Missing game files for MALI (Mali-G68) (ETC textures)`, listing the missing files
and asking the player to sync game assets in Settings. Detection failures also
stop initialization. This check verifies availability; download SHA-256 checks
remain responsible for content integrity.

## Debug option

Settings contains **Engine debug**, disabled by default and saved immediately.
The next engine session reads the setting from an atomic private file, avoiding
stale SharedPreferences caches between the launcher and `:engine` processes.

When enabled, an engine-only logcat reader keeps two private log files, bounded
to 256 KiB each. Its watcher stops the reader even after a native crash. A small
session record survives process death. On returning to the launcher, or opening
it again after a crash, an unclosed session opens **Engine failure log**, with
selectable text and **Copy log**. Normal shutdown marks the session closed; a
reported failure is not presented again. Focus changes also trigger the check,
so a lock screen or system crash dialog does not consume a report prematurely.

On Android 11+, the report includes the matching ApplicationExitInfo record.
On Android 12+, available native tombstones are protobuf messages, decoded into
signal, fault address, cause, registers and backtraces by `NativeCrashTrace`.
Android may not retain a trace; the report still contains the captured engine
logs. The parser has bounded input/output and ignores unknown schema fields.
With debug disabled, failures show the ordinary error dialog without logs;
turning the setting off removes the previous captured log files.

## GPU dataset update — 2026-11-16

[Dataset commit 041c6281d8251557f662e54c4b612081f89b75b0](https://huggingface.co/datasets/WorldRPG/worldrpg-game-files/commit/041c6281d8251557f662e54c4b612081f89b75b0)
contains 28 texture files and the updated `manifest_android_mta.json`:

- Original ETC/PVR `.dat`, `.tmb` and `.toc` for `mobile`, `txd` and `gta3`.
- The complete original `gta_int` catalogue and matching DXT/ETC/PVR files,
  as selected by the user. Its 2,541 entries replace the optimized 2,532-entry
  interior database; metadata and GPU files must be updated together.
- All other assets, including the repaired menu database, are preserved.

Sources are the main and patch OBBs in the supplied `GTA-SA-2.10.7z` archive.
The similarly named ZIP contains only the APK. Every uploaded thumbnail stream
was walked against its catalogue; TOC entry counts, data lengths and individual
data-record bounds were checked. All 28 remote file sizes and SHA-256 hashes
were verified after publication. The new manifest has 397 files, totalling
1,272,973,315 bytes. Existing installations need **Sync game assets**.

## Validation

Run the host regression suite against the production Java classes:

```sh
javac -d /tmp/neon-startup-tests \
  app/src/main/java/com/worldrpg/neonlauncher/game/GpuTextureSupport.java \
  app/src/main/java/com/worldrpg/neonlauncher/game/NativeCrashTrace.java \
  tools/headless/java/EngineStartupRegressionTest.java
java -cp /tmp/neon-startup-tests com.worldrpg.neonlauncher.game.EngineStartupRegressionTest
```

This exercises format priority, exact extension matching, a DXT-only installation
on Mali, empty thumbnail tables, optional cutscenes and readable native traces.
The suite also passed against the actual GPU files for ETC, DXT and PVR.

Debug and signed release builds passed, with APK signatures verified and the
debug APK's ARM64/16 KiB packaging audit passing. Release packaging was checked
to exclude GTA native libraries and texture assets.

An isolated Android instrumentation harness compiled the production classes and
passed on the connected Samsung S25 (SM-S938B, Adreno 830): real EGL detection,
cross-process debug settings, clean exit, an intentional SIGSEGV in its own
`:engine` process, persistent logs, readable Android native tombstones, the log
window/Copy log action, and the ordinary missing-files dialog with debug off.
The harness used a separate temporary application and was removed afterwards;
it did not replace or modify the installed launcher. Device acceptance on the
affected A54 is still required.
