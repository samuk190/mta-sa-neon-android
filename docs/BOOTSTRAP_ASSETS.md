# Engine Bootstrap Asset Inventory (P1A.4)

**Date:** 2026-11-10 · **Method:** Read-only inspection, offline reference analysis.
**Cross References:**
- Prototype APK `assetfile.txt` (engine bootstrap index, 120 declared / 120 listed entries).
- Original MobileSA 2.10 APK (`MobileSA-release-216c1e7f.apk`, 91,663,260 bytes).
- Android MTA manifest `manifest_android_mta.json` (358 files, 818,535,866 bytes).

The initial prototype APK had no bundled assets. The steps below established the bootstrap asset layer, ensuring the APK contains the regenerated catalog and repaired loading textures.

---

## 1. Classification of the 120 `assetfile.txt` Entries

| Class | Count | Description | Action |
|---|---|---|---|
| **A — In Prototype APK** | 27 | `scache*.txt`, `audio/android/NAV_*.ogg`, `data/*.cfg` (touch layouts), `data/fonts.dat`, `data/script/mainV1.scm` + `scriptv1.img`, `assetfile.txt` | Replicate in APK `assets/` or provide in local asset folder |
| **B — In Game Data Only** | 1 | `data/handling.cfg` (lowercase; engine requests uppercase `DATA/HANDLING.CFG` — resolved via case-aliases) | Covered by installed generation / local assets |
| **C — Indexed, Absent in ALL** | 92 | `socialclub/*` (54), `Fonts/*.TTF` R* branding (12), `images/*` including `Splash.png`/`RockstarIntro.png` (10), `xml/*` (8: app/audio/car/item/part_data, color_palette), `audio/audio_data.xml`, `json/socialClubAssets.json`, `social club/…` | **Non-blocking**: also DO NOT exist in original MobileSA 2.10 APK (verified via ZIP listing). Prototype reaches R* menu without them. Documented tolerance; do not synthesize placeholders |

> **Evidence for Class C:** `unzip -l MobileSA-release-216c1e7f.apk | grep -icE "assets/(images|xml|socialclub|json)/|audio_data.xml|BRADLEY|PRICEDOWN"` → `0`.

## 2. Additional Directories (Outside Index, Used by Engine)

| Directory | Size | Critical? | Proposed Destination |
|---|---|---|---|
| `Text/*.gxt` (7 languages) | 8.7 MiB | **YES** — native crash in `CText::Load`/`OS_FileRead` verified without them | `american.gxt` (1.3 MiB) in APK `assets/`; other languages optional |
| `Textures/` (fonts + JapaneseFont/RussianFont .met/.png) | 0.3 MiB | **YES** (engine fonts) | APK `assets/` |
| `cgui/` (mobile touch UI images) | 7.9 MiB | Probable (native touch UI) — validate at boot | Local game assets |
| `fonts/` (25 generic TTFs) | ~21 MiB | Partial — engine uses subset | Local game assets; audit active subset before bundling |
| `ac_modules/` (R* anti-cheat) | 4.0 MiB | **NO** — out of scope; custom validator replaces it | Excluded from port |
| `data/` (prototype script/cfg) | 3.7 MiB | Duplicate of game assets (installed generation is authoritative) | Kept in local game data |

## 3. Known Issues & Remediation

1. **`texdb/menu/menu.pvr.dat` Stream Corruption**: Missing 12 uncompressed bytes in the `mobilesc0` stream previously caused a crash at `CLoadingScreen::LoadSplashes → RLEDecompress`. Repaired via `tools/repair_menu_texture.py` and included in bootstrap assets.
2. **Missing GXT Tables**: Without `Text/american.gxt`, the game engine crashes immediately during `CText::Load`. Bundled in APK `assets/Text/american.gxt`.
3. **Case-Sensitivity Aliasing**: Android filesystem is case-sensitive, but the engine requests mixed/uppercase paths like `DATA/HANDLING.CFG`, `PEDSTATS.DAT`, etc. Handled dynamically via `EngineCaseAliases` without duplicating files on disk.
4. **`assetfile.txt` Regeneration**: Catalog must reflect actual packaged files in the APK rather than legacy missing paths.

## 4. Packaging Structure

```text
APK assets/                   (Lightweight, boot-critical: ~5.2 MiB)
├── assetfile.txt             (Regenerated index)
├── scache*.txt               (16 KB)
├── Text/american.gxt         (1.3 MiB)
├── Textures/                 (0.3 MiB engine fonts)
├── texdb/menu/menu.pvr.dat   (Repaired splash textures)
├── texdb/menu/menu.pvr.toc
└── audio/android/*.ogg       (UI navigation audio)

Local Game Assets Folder      (Supplied by user via .env: ~800 MiB)
├── anim/                     (anim.img)
├── audio/                    (SFX, streams)
├── cgui/                     (Mobile touch UI elements)
├── data/                     (Handling, surface, pedstats, default.dat, gta.dat)
├── fonts/                    (TrueType font subset)
├── models/                   (Collision & generic models)
└── texdb/                    (gta3.img, gta_int.img)
```

## 5. Device Validation (Samsung S25)

- Verified all 358 SHA-256 hashes of installed generation.
- `EngineHostActivity` initializes native GTASA Java/JNI host with dedicated EGL surface from Settings → Test GTA in debug APK.
- Fixed `CLoadingScreen::LoadSplashes` decompression error using repaired `menu.pvr.dat`.
- Mitigated crash in `LoadObjectTypes` caused by case-mismatched IDE/IPL declarations (e.g. `DATA/MAPS/LA/LAn.IDE` vs `LAn.ide`).
