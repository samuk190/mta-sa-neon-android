# Game Assets Directory

Place your original **GTA San Andreas Mobile v2.10** extracted files in this directory (or point `GAME_ASSETS_DIR` in `.env` to another local folder on your computer).

### Installed game folder

```text
/sdcard/Android/data/com.saonline.neonlauncher/files/game_assets/
├── lib/
│   ├── libGTASA.so
│   ├── libOpenAL64.so
│   └── libSCAnd.so
├── anim/
├── audio/
│   ├── android/
│   ├── SFX/
│   └── STREAMS/
├── cgui/
├── data/
│   ├── default.dat
│   ├── gta.dat
│   ├── handling.cfg
│   └── script/
├── fonts/
├── models/
├── MTA/cgui/
├── Text/american.gxt
├── Textures/fonts/
├── texdb/
│   ├── gta3.img
│   ├── gta_int.img
│   └── menu/menu.pvr.dat
├── scache.txt
├── scache_small.txt
├── scache_small_low.txt
└── stream.ini
```

[Complete reference tree with all 410 file paths](../docs/game-assets-tree.txt). The tree matches the [reference manifest](https://huggingface.co/datasets/WorldRPG/worldrpg-game-files/blob/f4839e688328d11836d43a4583d2de22c96bd82b/manifest_android_mta.json). For a manual installation, keep all files from your own game archives, including additional files such as `assetfile.txt` when present. Merge the contents of the APK’s `assets/` and the OBB archives into this folder. Copy the three engine libraries from the APK’s `lib/arm64-v8a/` into `lib/`.

### Where to get these files:
These files come from the legally owned original **GTA San Andreas Mobile v2.10** for Android:
1. Extract the game's APK and OBB files (`main.210.com.rockstargames.gtasa.obb` and `patch.210.com.rockstargames.gtasa.obb`).
2. Rename `.obb` to `.zip` and extract the contents.
3. Place the extracted game folders into this `game_assets/` folder.
4. Run `tools/sync_local_assets.sh` (or `adb push`) to transfer them to your Android device.
