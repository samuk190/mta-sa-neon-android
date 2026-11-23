# MTA:SA Neon für Android

[Offizielle Website](https://saonlinemobile.web.app/) · [APK herunterladen](https://saonlinemobile.web.app/download)

Nativer ARM64-Android-Client für **Multi Theft Auto: San Andreas** (MTA:SA) auf Basis von MTA Neon und der Engine von Grand Theft Auto: San Andreas Mobile.

## Sprachen der README

[English](README.md) · [Português (Brasil)](README.pt-BR.md) · [Español](README.es.md) · **Deutsch** · [简体中文](README.zh-Hans.md) · [Русский](README.ru.md)

## Sprache des Launchers

Der Launcher übernimmt standardmäßig die Spracheinstellungen von Android. Falls keine Übersetzung verfügbar ist, verwendet er Englisch. Unter **Einstellungen → Sprache** kannst du eine andere Sprache auswählen. Die Auswahl wird sofort übernommen und gespeichert. Mit **Gerätesprache** stellst du wieder auf die Android-Vorgabe um.

## Voraussetzungen

- Ein ARM64-Smartphone (`arm64-v8a`) mit Android 8.0 oder neuer.
- Eine legal erworbene Originalversion von **GTA San Andreas Mobile 2.10** für Android, aus der du die Spieldaten und Bibliotheken entnimmst.
- Für die manuelle Installation einen Rechner mit [ADB](https://developer.android.com/tools/releases/platform-tools) oder einen Dateimanager mit Zugriff auf `Android/data`.

## Installation und erste Schritte

1. Lade die APK von der [offiziellen Downloadseite](https://saonlinemobile.web.app/download) herunter, installiere sie und öffne den Launcher einmal, damit Android den App-Ordner anlegt.
2. Bei einem APK mit konfigurierter `CDN_ROOT_URL` wählst du **Einstellungen → Spieldateien synchronisieren**. Der Launcher prüft die Downloads und installiert die Daten und drei ARM64-Bibliotheken im privaten App-Speicher. Bei Builds ohne CDN gehst du wie unten beschrieben manuell vor.
3. Wähle unter **Server** einen Server aus, tippe auf **Beitreten**, gib deinen Spielernamen ein und tippe auf **Spielen**. Falls der Server ein SA:O-Konto voraussetzt, verbinde es über **Einstellungen → Mit Discord anmelden**.

Die Anwendungs-ID lautet `com.saonline.neonlauncher`. Diese Installation verwendet einen eigenen Ordner und einen eigenen Schlüssel, auch auf Geräten mit älteren Versionen des World-RPG-Launchers.

### Spieldateien manuell installieren

Die APK- und OBB-Dateien deines GTA-Spiels sind ZIP-Archive. Extrahiere die folgenden Dateien.

| Quelle | Speicherort | Zu extrahierende Dateien |
|---|---|---|
| GTA-APK | Führe `adb shell pm path com.rockstargames.gtasa` aus und kopiere die angezeigte `base.apk` mit `adb pull` | Inhalt von `assets/` und die Bibliotheken `libGTASA.so`, `libSCAnd.so` und `libOpenAL64.so` aus `lib/arm64-v8a/` |
| GTA-OBB | `Android/obb/com.rockstargames.gtasa/*.obb` | Gesamter Inhalt, einschließlich `anim/`, `audio/`, `data/`, `models/` und `texdb/` |

Ordne die Dateien im Geräteordner `game_assets` wie unten gezeigt an. Beachte die Groß- und Kleinschreibung.

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

[Vollständigen Referenzbaum anzeigen (410 Dateien)](docs/game-assets-tree.txt). Er enthält alle Pfade des Projektmanifests einschließlich der MTA-Ladebilder. Kopiere für die manuelle Installation den Inhalt von `assets/` aus deiner APK und den Inhalt der OBB-Archive direkt nach `game_assets/`. Behalte die ursprünglichen Namen und zusätzliche Dateien deiner Spielkopie bei, einschließlich `assetfile.txt`, falls vorhanden. Die drei ARM64-Bibliotheken gehören direkt nach `lib/`.

Wenn du die Dateien auf dem Rechner unter `gta_sa/` vorbereitet hast, übertrage den Ordnerinhalt mit ADB.

```bash
adb push gta_sa/. /sdcard/Android/data/com.saonline.neonlauncher/files/game_assets/
```

Bei der Deinstallation löscht Android den App-Ordner. Bewahre daher eine Kopie der extrahierten Dateien auf deinem Rechner auf.

### Ladebilder

Das APK enthält 14 Bilder unter `MTA/cgui/images/loadscreens/`. Bilder aus dem installierten Spielordner haben Vorrang. Die optionale Schrift liegt unter `MTA/cgui/sabankgothic.ttf`. Um Bilder oder die Schrift per CDN zu verteilen, veröffentliche die Dateien unter `game_files_android_mta/MTA/cgui/`, trage Größen und SHA-256-Prüfsummen in `manifest_android_mta.json` ein und synchronisiere erneut.

## Projekt bauen

Installiere Android Studio (Hedgehog oder neuer) mit JDK 17, Android SDK 34, NDK `25.1.8937393` und CMake `3.22.1`. Checke den [Server-Branch aus PR #135](https://github.com/Dryxio/san-andreas-online/pull/135) in einem benachbarten Ordner namens `san-andreas-online` aus. Installiere die in `Shared/transport/go.mod` angegebene Go-Version und füge sie zum `PATH` hinzu. Liegt der Checkout an einem anderen Ort, setze die CMake-Variable `NEON_TRANSPORT_DIR` auf dessen Ordner `Shared/transport`.

Erstelle die Datei `.env` anhand der Vorlage [`.env.example`](.env.example) und passe Pfade und Launcher-Einstellungen an.

```bash
cp .env.example .env
```

| Variable | Verwendung |
|---|---|
| `GAME_ASSETS_DIR` | Ordner mit den entpackten Dateien auf dem Rechner. Standardwert `./game_assets`. |
| `DEVICE_ASSETS_PATH` | Spielordner auf dem Android-Gerät. |
| `CDN_ROOT_URL` | Optionale CDN-URL für Spieldateien und Manifest. |
| `LAUNCHER_TITLE` | Name, den der Launcher anzeigt. |
| `LAUNCHER_PLAY_BUTTON_TEXT` | Optionale Beschriftung für alle Sprachen. Lass den Wert leer, um die Übersetzung von **Spielen** zu verwenden. |

```bash
./gradlew assembleDebug
./gradlew assembleRelease
./gradlew installDebug
```

Die APKs liegen unter `app/build/outputs/apk/debug/` oder `app/build/outputs/apk/release/`. Zum Signieren des Release-Builds setze `storeFile`, `storePassword`, `keyAlias` und `keyPassword` in `keystore.properties`. Der Release-Build enthält den Launcher und die MTA-Ladebilder. Mit `./gradlew assembleDebug -PnoGameFiles` erhältst du für Installationstests ein Debug-APK ohne mitgelieferte GTA-Dateien.

Mit `bash tools/sync_local_assets.sh` oder `./gradlew pushAssetsToDevice` kannst du die in `.env` konfigurierten lokalen Dateien prüfen und auf ein verbundenes Gerät übertragen.

## Kompatible Server

Baue den Server nach den Anweisungen von [San Andreas Online](https://github.com/Dryxio/san-andreas-online). Die Standardverbindung nutzt das Neon-Protokoll `0x41E7` und das mobile Bitstream-Format `0x0030`. Für einen verifizierten Android-Zugang muss der Launcher dem HTTPS-Aussteller des Serverbetreibers vertrauen. Der Aussteller muss die Anwendungs-ID und das Signaturzertifikat des APKs zulassen.

Wähle für einen QUIC-Server **Favoriten → Hinzufügen**. Gib den Hostnamen aus dem Serverzertifikat mit dem Präfix `quic://` und den separaten QUIC-UDP-Port ein. Die Verbindung nutzt den Installationsschlüssel im Android Keystore und das Clientzertifikat von Neon Identity. Der Betreiber kann eine Google-Play-Integritätsprüfung verlangen. Für die klassische Verbindung verwendest du den einfachen Hostnamen und den klassischen Port.

## Dokumentation und Community

- [Synchronisierungsplan und Projektstatus](README.md#project-roadmap-mta-sync-porting-plan-pc-to-android)
- [Lua-Funktionen des Clients](docs/lua-functions/README.md) und [Kompatibilitätsmatrix](docs/PARITY_MATRIX.md)
- [Protokollreferenz](docs/PROTOCOL_REFERENCE.md) und [Synchronisierungsspezifikation](docs/MTA_SYNC_ANDROID_PORT.md)
- [Lizenz und Danksagungen](README.md#license--attribution)
- [Discord-Community](https://discord.gg/DKrpU3e4pF) · [Projekt auf Ko-fi unterstützen](https://ko-fi.com/samuk190)
