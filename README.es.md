# MTA:SA Neon para Android

Cliente nativo ARM64 de **Multi Theft Auto: San Andreas** (MTA:SA) para Android, basado en MTA Neon y en el motor de Grand Theft Auto: San Andreas Mobile.

## Idiomas del README

[English](README.md) · [Português (Brasil)](README.pt-BR.md) · **Español** · [Deutsch](README.de.md) · [简体中文](README.zh-Hans.md) · [Русский](README.ru.md)

## Idioma del lanzador

El lanzador sigue las preferencias de idioma de Android de forma predeterminada y usa el inglés si no hay una traducción disponible. Abre **Ajustes → Idioma** para seleccionar otro idioma. El cambio se aplica de inmediato y se guarda. Selecciona **Idioma del dispositivo** para volver a seguir las preferencias de Android.

## Requisitos

- Teléfono ARM64 (`arm64-v8a`) con Android 8.0 o posterior.
- Copia original de **GTA San Andreas Mobile 2.10** para Android, adquirida legalmente, de la que extraerás los datos y las bibliotecas del juego.
- Para la instalación manual, un ordenador con [ADB](https://developer.android.com/tools/releases/platform-tools) o un gestor de archivos con acceso a `Android/data`.

## Instalación y primeros pasos

1. Descarga e instala `mta-sa-neon-android-<versión>.apk`. Abre el lanzador una vez para que Android cree la carpeta de la aplicación. [Versiones publicadas](https://github.com/samuk190/mta-sa-neon-android/releases)
2. Si el APK se compiló con `CDN_ROOT_URL`, abre **Ajustes → Sincronizar archivos del juego**. El lanzador verifica las descargas e instala los datos y las tres bibliotecas ARM64 en el almacenamiento privado. Para las compilaciones sin CDN, sigue los pasos de instalación manual.
3. En **Servidores**, elige un servidor, pulsa **Unirse**, introduce tu apodo y pulsa **Jugar**. Si el servidor requiere una cuenta de SA:O, vincula tu cuenta en **Ajustes → Iniciar sesión con Discord**.

El identificador de la aplicación es `com.saonline.neonlauncher`. Esta instalación usa su propia carpeta y clave, también en dispositivos con versiones anteriores del lanzador World RPG.

### Instalación manual de los archivos del juego

El APK y los archivos OBB de tu copia de GTA son archivos ZIP. Extrae los siguientes elementos.

| Origen | Cómo localizarlo | Qué extraer |
|---|---|---|
| APK de GTA | Ejecuta `adb shell pm path com.rockstargames.gtasa` y copia el `base.apk` que aparece con `adb pull` | El contenido de `assets/` y las bibliotecas `libGTASA.so`, `libSCAnd.so` y `libOpenAL64.so` de `lib/arm64-v8a/` |
| OBB de GTA | `Android/obb/com.rockstargames.gtasa/*.obb` | Todo el contenido, incluidos `anim/`, `audio/`, `data/`, `models/` y `texdb/` |

Organiza los archivos así en la carpeta `game_assets` del dispositivo. Respeta las mayúsculas y minúsculas de los nombres.

```text
/sdcard/Android/data/com.saonline.neonlauncher/files/game_assets/
├── lib/
│   ├── libGTASA.so
│   ├── libSCAnd.so
│   └── libOpenAL64.so
├── anim/  audio/  models/  Text/  Textures/
├── data/
│   ├── gta.dat
│   └── handling.cfg
├── texdb/
│   ├── gta3.img
│   └── menu/menu.pvr.dat
├── assetfile.txt
└── scache.txt  scache_small.txt  scache_small_low.txt
```

Si preparaste los archivos en `gta_sa/` en tu ordenador, envía el contenido de esa carpeta mediante ADB.

```bash
adb push gta_sa/. /sdcard/Android/data/com.saonline.neonlauncher/files/game_assets/
```

Android elimina la carpeta de la aplicación al desinstalarla. Guarda una copia de los archivos extraídos en el ordenador.

### Imágenes de carga

El APK incluye 14 imágenes en `MTA/cgui/images/loadscreens/`. El juego da prioridad a las imágenes de la instalación y recurre a las del APK cuando sea necesario. La fuente opcional se encuentra en `MTA/cgui/sabankgothic.ttf`. Para distribuir imágenes o la fuente por CDN, publica los archivos en `game_files_android_mta/MTA/cgui/`, añade sus tamaños y hashes SHA-256 a `manifest_android_mta.json` y vuelve a sincronizar.

## Compilación

Instala Android Studio (Hedgehog o posterior) con JDK 17, Android SDK 34, NDK `25.1.8937393` y CMake `3.22.1`. Obtén la [rama del PR #135 del servidor](https://github.com/Dryxio/san-andreas-online/pull/135) en una carpeta hermana llamada `san-andreas-online`. Instala la versión de Go indicada en `Shared/transport/go.mod` y añádela al `PATH`. Si usas otra ubicación, apunta la variable de CMake `NEON_TRANSPORT_DIR` al directorio `Shared/transport` de esa copia.

Crea el archivo `.env` a partir de [`.env.example`](.env.example) y modifica las rutas y opciones del lanzador.

```bash
cp .env.example .env
```

| Variable | Uso |
|---|---|
| `GAME_ASSETS_DIR` | Carpeta de archivos extraídos en el ordenador. El valor predeterminado es `./game_assets`. |
| `DEVICE_ASSETS_PATH` | Carpeta del juego en el dispositivo. |
| `CDN_ROOT_URL` | URL opcional de un CDN con los archivos y el manifiesto. |
| `LAUNCHER_TITLE` | Nombre que muestra el lanzador. |
| `LAUNCHER_PLAY_BUTTON_TEXT` | Etiqueta opcional para todos los idiomas. Déjala vacía para usar la traducción de **Jugar**. |

```bash
./gradlew assembleDebug
./gradlew assembleRelease
./gradlew installDebug
```

Los APK se generan en `app/build/outputs/apk/debug/` o `app/build/outputs/apk/release/`. Para firmar la versión de publicación, define `storeFile`, `storePassword`, `keyAlias` y `keyPassword` en `keystore.properties`. La versión de publicación incluye el lanzador y las imágenes de carga de MTA. Para probar la instalación de archivos del juego sin empaquetarlos en un APK de depuración, ejecuta `./gradlew assembleDebug -PnoGameFiles`.

Para verificar y enviar a un dispositivo conectado los archivos locales definidos en `.env`, ejecuta `bash tools/sync_local_assets.sh` o `./gradlew pushAssetsToDevice`.

## Servidores compatibles

Compila el servidor según las instrucciones de [San Andreas Online](https://github.com/Dryxio/san-andreas-online). La conexión estándar usa el protocolo Neon `0x41E7` y el formato de flujo de bits móvil `0x0030`. Para verificar el acceso desde Android, el lanzador debe confiar en el emisor HTTPS del operador, y el emisor debe autorizar el identificador de la aplicación y el certificado de firma del APK.

Para un servidor QUIC, abre **Favoritos → Añadir**, introduce el nombre de host del certificado con el prefijo `quic://` y usa su puerto UDP para QUIC. La conexión usa la clave de instalación de Android Keystore y el certificado de cliente de Neon Identity. El operador puede exigir la verificación de integridad de Google Play. Para la conexión clásica, usa el nombre de host y el puerto clásicos.

## Documentación y comunidad

- [Plan de sincronización y estado del proyecto](README.md#project-roadmap-mta-sync-porting-plan-pc-to-android)
- [Catálogo de funciones Lua del cliente](docs/lua-functions/README.md) y [matriz de compatibilidad](docs/PARITY_MATRIX.md)
- [Referencia del protocolo](docs/PROTOCOL_REFERENCE.md) y [especificación de sincronización](docs/MTA_SYNC_ANDROID_PORT.md)
- [Licencia y créditos](README.md#license--attribution)
- [Comunidad en Discord](https://discord.gg/DKrpU3e4pF) · [Apoya el proyecto en Ko-fi](https://ko-fi.com/samuk190)
