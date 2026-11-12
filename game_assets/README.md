# Game Assets Directory

Place your original **GTA San Andreas Mobile v2.10** extracted files in this directory (or point `GAME_ASSETS_DIR` in `.env` to another local folder on your computer).

### Required Structure:
```text
game_assets/
├── anim/
│   └── anim.img, etc.
├── audio/
│   └── streams, sfx, etc.
├── data/
│   ├── gta.dat
│   ├── handling.cfg
│   ├── surface.dat
│   └── ...
├── models/
│   └── coll, generic, etc.
├── texdb/
│   ├── gta3.img
│   ├── gta3.txt
│   ├── gta_int.img
│   └── menu/
│       ├── menu.pvr.dat
│       └── menu.pvr.toc
└── Text/
    └── american.gxt
```

### Where to get these files:
These files come from the legally owned original **GTA San Andreas Mobile v2.10** for Android:
1. Extract the game's APK and OBB files (`main.210.com.rockstargames.gtasa.obb` and `patch.210.com.rockstargames.gtasa.obb`).
2. Rename `.obb` to `.zip` and extract the contents.
3. Place the extracted game folders into this `game_assets/` folder.
4. Run `tools/sync_local_assets.sh` (or `adb push`) to transfer them to your Android device.
