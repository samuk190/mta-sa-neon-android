# MTA:SA Neon Android 客户端

[官方网站](https://saonlinemobile.web.app/) · [下载 APK](https://saonlinemobile.web.app/download)

这是 **Multi Theft Auto: San Andreas**（MTA:SA）的原生 ARM64 Android 客户端，基于 MTA Neon 和 Grand Theft Auto: San Andreas Mobile 游戏引擎。

## README 语言

[English](README.md) · [Português (Brasil)](README.pt-BR.md) · [Español](README.es.md) · [Deutsch](README.de.md) · **简体中文** · [Русский](README.ru.md)

## 启动器语言

启动器默认跟随 Android 的语言设置。如果没有对应的翻译，则使用英语。打开 **设置 → 语言** 即可切换语言，更改会立即生效并保存。选择 **跟随系统** 可以恢复为 Android 当前使用的语言。

## 安装要求

- ARM64（`arm64-v8a`）手机，运行 Android 8.0 或更高版本。
- 一份合法购买的 **GTA San Andreas Mobile 2.10** Android 原版游戏，用来提取游戏数据和原生库。
- 手动安装时需要一台装有 [ADB](https://developer.android.com/tools/releases/platform-tools) 的电脑，或能够访问 `Android/data` 的文件管理器。

## 安装并开始游戏

1. 从[官方下载页面](https://saonlinemobile.web.app/download)下载并安装 APK，然后打开一次启动器，让 Android 创建应用目录。
2. 如果 APK 配置了 `CDN_ROOT_URL`，请打开 **设置 → 同步游戏文件**。启动器会校验下载内容，并将游戏数据和三个 ARM64 原生库安装到应用的私有存储空间。未配置 CDN 的构建版本请按下方步骤手动安装。
3. 在 **服务器** 中选择服务器，点按 **加入**，输入昵称后点按 **开始游戏**。如果服务器要求登录 SA:O，请在 **设置 → 使用 Discord 登录** 中绑定账号。

应用 ID 为 `com.saonline.neonlauncher`。安装后使用独立的目录和密钥，可以与旧版 World RPG 启动器共存。

### 手动安装游戏文件

GTA 游戏的 APK 和 OBB 文件均为 ZIP 格式。请从原版游戏中提取以下内容。

| 来源 | 位置 | 提取内容 |
|---|---|---|
| GTA APK | 运行 `adb shell pm path com.rockstargames.gtasa`，再用 `adb pull` 复制显示的 `base.apk` | `assets/` 中的全部内容，以及 `lib/arm64-v8a/` 中的 `libGTASA.so`、`libSCAnd.so` 和 `libOpenAL64.so` |
| GTA OBB | `Android/obb/com.rockstargames.gtasa/*.obb` | 全部内容，包括 `anim/`、`audio/`、`data/`、`models/` 和 `texdb/` |

按下列结构将文件放入设备上的 `game_assets` 目录。文件名区分大小写。

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

[查看完整参考目录树（410 个文件）](docs/game-assets-tree.txt)。其中列出了项目参考清单中的每个路径，包括 MTA 加载画面。手动安装时，将自己的 APK 中 `assets/` 的内容和 OBB 压缩包中的内容直接合并到 `game_assets/`。保留原始文件名和游戏副本中的其他文件，包括可能存在的 `assetfile.txt`。三个 ARM64 库直接放在 `lib/` 下。

如果已在电脑上的 `gta_sa/` 目录中整理好文件，可通过 ADB 传输目录中的内容。

```bash
adb push gta_sa/. /sdcard/Android/data/com.saonline.neonlauncher/files/game_assets/
```

卸载应用会删除该应用目录。请在电脑上保留一份已提取文件的备份。

### 加载画面

APK 在 `MTA/cgui/images/loadscreens/` 中附带 14 张加载图片。客户端优先使用游戏安装目录中的图片，读取失败时使用 APK 内置图片。可选字体位于 `MTA/cgui/sabankgothic.ttf`。若通过 CDN 分发自定义图片或字体，请将文件发布到 `game_files_android_mta/MTA/cgui/`，在 `manifest_android_mta.json` 中记录文件大小和 SHA-256 哈希，然后重新同步。

## 构建项目

安装 Android Studio（Hedgehog 或更新版本）、JDK 17、Android SDK 34、NDK `25.1.8937393` 和 CMake `3.22.1`。将[服务器 PR #135 的分支](https://github.com/Dryxio/san-andreas-online/pull/135)检出到名为 `san-andreas-online` 的同级目录。安装 `Shared/transport/go.mod` 指定的 Go 版本并将其加入 `PATH`。如将服务器代码检出到其他位置，请将 CMake 变量 `NEON_TRANSPORT_DIR` 指向该检出目录下的 `Shared/transport`。

使用 [`.env.example`](.env.example) 创建 `.env`，然后调整路径与启动器选项。

```bash
cp .env.example .env
```

| 变量 | 用途 |
|---|---|
| `GAME_ASSETS_DIR` | 电脑上存放已提取文件的目录，默认为 `./game_assets`。 |
| `DEVICE_ASSETS_PATH` | Android 设备上的游戏目录。 |
| `CDN_ROOT_URL` | 存放游戏文件和清单的可选 CDN 地址。 |
| `LAUNCHER_TITLE` | 启动器显示的名称。 |
| `LAUNCHER_PLAY_BUTTON_TEXT` | 可为所有语言自定义开始按钮的文字。留空时使用 **开始游戏** 对应的本地化文本。 |

```bash
./gradlew assembleDebug
./gradlew assembleRelease
./gradlew installDebug
```

生成的 APK 位于 `app/build/outputs/apk/debug/` 或 `app/build/outputs/apk/release/`。如需对发布版签名，请在 `keystore.properties` 中配置 `storeFile`、`storePassword`、`keyAlias` 和 `keyPassword`。发布版包含启动器和 MTA 加载图片。如需测试安装流程，可运行 `./gradlew assembleDebug -PnoGameFiles`，生成不附带 GTA 文件的调试 APK。

运行 `bash tools/sync_local_assets.sh` 或 `./gradlew pushAssetsToDevice`，即可校验 `.env` 中配置的本地文件并将其传输到已连接的设备。

## 兼容服务器

请按 [San Andreas Online](https://github.com/Dryxio/san-andreas-online) 的说明编译服务器。标准连接使用 Neon 协议 `0x41E7` 和移动端位流格式 `0x0030`。如需启用 Android 接入验证，启动器须信任服务器运营方的 HTTPS 签发方，而签发方须授权该应用 ID 和 APK 的签名证书。

要连接 QUIC 服务器，请打开 **收藏 → 添加**，输入带有 `quic://` 前缀的服务器证书主机名及其专用 QUIC UDP 端口。客户端会使用 Android Keystore 中的安装密钥和 Neon Identity 客户端证书连接。服务器运营方可以要求通过 Google Play 完成设备完整性验证。连接传统服务器时，请使用普通主机名和传统端口。

## 文档和社区

- [同步移植路线图与项目状态](README.md#project-roadmap-mta-sync-porting-plan-pc-to-android)
- [客户端 Lua 函数目录](docs/lua-functions/README.md)和[兼容性矩阵](docs/PARITY_MATRIX.md)
- [协议参考](docs/PROTOCOL_REFERENCE.md)和[同步规范](docs/MTA_SYNC_ANDROID_PORT.md)
- 本项目采用 [GNU 通用公共许可证第 2 版（GPL-2.0-only）](LICENSE)。[致谢及第三方组件许可证](README.md#license--attribution)。
- [Discord 社区](https://discord.gg/DKrpU3e4pF) · [在 Ko-fi 上支持项目](https://ko-fi.com/samuk190)
