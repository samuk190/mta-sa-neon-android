# MTA:SA Neon для Android

[Официальный сайт](https://saonlinemobile.web.app/) · [Скачать APK](https://saonlinemobile.web.app/download)

Нативный клиент **Multi Theft Auto: San Andreas** (MTA:SA) для Android на ARM64. В основе лежат архитектура MTA Neon и движок Grand Theft Auto: San Andreas Mobile.

## Языки README

[English](README.md) · [Português (Brasil)](README.pt-BR.md) · [Español](README.es.md) · [Deutsch](README.de.md) · [简体中文](README.zh-Hans.md) · **Русский**

## Язык лаунчера

По умолчанию лаунчер использует языковые настройки Android. Если подходящего перевода нет, интерфейс отображается на английском. Чтобы выбрать другой язык, откройте **Настройки → Язык**. Изменение применяется сразу и сохраняется. Выберите **Язык устройства**, чтобы снова использовать настройки Android.

## Требования

- Телефон с процессором ARM64 (`arm64-v8a`) и Android 8.0 или новее.
- Легально приобретённая оригинальная версия **GTA San Andreas Mobile 2.10** для Android, из которой можно извлечь данные и библиотеки игры.
- Для ручной установки нужен компьютер с [ADB](https://developer.android.com/tools/releases/platform-tools) или файловый менеджер с доступом к `Android/data`.

## Установка и первый запуск

1. Скачайте APK с [официальной страницы загрузки](https://saonlinemobile.web.app/download), установите его и один раз откройте лаунчер, чтобы создать папку приложения.
2. Если в сборке задан `CDN_ROOT_URL`, откройте **Настройки → Синхронизировать файлы игры**. Лаунчер проверит загруженные файлы и установит данные и три библиотеки ARM64 в закрытое хранилище приложения. Для сборок без CDN выполните ручную установку по инструкции ниже.
3. На вкладке **Серверы** выберите сервер, нажмите **Войти**, введите никнейм и нажмите **Играть**. Если сервер требует входа в SA:O, подключите аккаунт через **Настройки → Войти через Discord**.

Идентификатор приложения — `com.saonline.neonlauncher`. У него собственная папка и ключ установки, поэтому его можно установить рядом со старыми версиями лаунчера World RPG.

### Ручная установка файлов игры

APK и OBB оригинальной игры имеют формат ZIP. Извлеките из них следующие файлы.

| Источник | Где найти | Что извлечь |
|---|---|---|
| APK GTA | Выполните `adb shell pm path com.rockstargames.gtasa`, затем скопируйте указанный `base.apk` командой `adb pull` | Содержимое `assets/` и библиотеки `libGTASA.so`, `libSCAnd.so` и `libOpenAL64.so` из `lib/arm64-v8a/` |
| OBB GTA | `Android/obb/com.rockstargames.gtasa/*.obb` | Всё содержимое, в том числе `anim/`, `audio/`, `data/`, `models/` и `texdb/` |

Разместите файлы в папке `game_assets` на устройстве по этой схеме. Сохраняйте регистр имён файлов и папок.

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

[Полное дерево файлов (410 файлов)](docs/game-assets-tree.txt) содержит все пути из эталонного манифеста проекта, включая загрузочные экраны MTA. Для ручной установки скопируйте содержимое `assets/` из своего APK и содержимое архивов OBB непосредственно в `game_assets/`. Сохраните исходные имена и дополнительные файлы своей копии игры, включая `assetfile.txt`, если он есть. Три библиотеки ARM64 поместите непосредственно в `lib/`.

Если файлы подготовлены в папке `gta_sa/` на компьютере, отправьте её содержимое через ADB.

```bash
adb push gta_sa/. /sdcard/Android/data/com.saonline.neonlauncher/files/game_assets/
```

При удалении приложения Android удаляет и его папку. Храните копию извлечённых файлов на компьютере.

### Экраны загрузки

APK включает 14 изображений в `MTA/cgui/images/loadscreens/`. В первую очередь используются изображения из папки установленной игры. Если загрузить их не удаётся, клиент использует изображения из APK. Дополнительный шрифт можно положить в `MTA/cgui/sabankgothic.ttf`. Для распространения изображений или шрифта через CDN загрузите их в `game_files_android_mta/MTA/cgui/`, укажите размеры файлов и хеши SHA-256 в `manifest_android_mta.json` и повторите синхронизацию.

## Сборка проекта

Установите Android Studio (Hedgehog или новее), JDK 17, Android SDK 34, NDK `25.1.8937393` и CMake `3.22.1`. Получите [ветку сервера из PR #135](https://github.com/Dryxio/san-andreas-online/pull/135) в соседнюю папку `san-andreas-online`. Установите версию Go из `Shared/transport/go.mod` и добавьте её в `PATH`. Если исходный код сервера лежит в другом месте, укажите путь к его папке `Shared/transport` в переменной CMake `NEON_TRANSPORT_DIR`.

Создайте файл `.env` по шаблону [`.env.example`](.env.example) и настройте пути и параметры лаунчера.

```bash
cp .env.example .env
```

| Переменная | Назначение |
|---|---|
| `GAME_ASSETS_DIR` | Папка с извлечёнными файлами на компьютере. По умолчанию `./game_assets`. |
| `DEVICE_ASSETS_PATH` | Папка игры на устройстве. |
| `CDN_ROOT_URL` | Необязательный адрес CDN с файлами игры и манифестом. |
| `LAUNCHER_TITLE` | Название, отображаемое лаунчером. |
| `LAUNCHER_PLAY_BUTTON_TEXT` | Необязательная подпись кнопки для всех языков. Оставьте поле пустым, чтобы использовать перевод кнопки **Играть**. |

```bash
./gradlew assembleDebug
./gradlew assembleRelease
./gradlew installDebug
```

Готовые APK находятся в `app/build/outputs/apk/debug/` или `app/build/outputs/apk/release/`. Для подписи релиза задайте `storeFile`, `storePassword`, `keyAlias` и `keyPassword` в `keystore.properties`. Релиз содержит лаунчер и изображения загрузки MTA. Для проверки установки файлов игры соберите отладочный APK без встроенных файлов GTA командой `./gradlew assembleDebug -PnoGameFiles`.

Чтобы проверить локальные файлы, указанные в `.env`, и перенести их на подключённое устройство, выполните `bash tools/sync_local_assets.sh` или `./gradlew pushAssetsToDevice`.

## Совместимые серверы

Соберите сервер по инструкции [San Andreas Online](https://github.com/Dryxio/san-andreas-online). Стандартное подключение использует протокол Neon `0x41E7` и формат мобильного битового потока `0x0030`. Для проверенного доступа с Android лаунчер должен доверять HTTPS-издателю оператора сервера, а издатель должен разрешить идентификатор приложения и сертификат подписи APK.

Для подключения к QUIC-серверу откройте **Избранное → Добавить**, введите имя хоста из сертификата с префиксом `quic://` и укажите отдельный UDP-порт QUIC. Клиент использует ключ установки из Android Keystore и клиентский сертификат Neon Identity. Оператор может потребовать проверку целостности устройства через Google Play. Для классического подключения используйте обычное имя хоста и классический порт.

## Документация и сообщество

- [План переноса синхронизации и состояние проекта](README.md#project-roadmap-mta-sync-porting-plan-pc-to-android)
- [Каталог функций Lua клиента](docs/lua-functions/README.md) и [матрица совместимости](docs/PARITY_MATRIX.md)
- [Описание протокола](docs/PROTOCOL_REFERENCE.md) и [спецификация синхронизации](docs/MTA_SYNC_ANDROID_PORT.md)
- Проект распространяется на условиях [GNU AGPL версии 3 (AGPL-3.0-only)](LICENSE). [Благодарности и лицензии сторонних компонентов](README.md#license--attribution).
- [Сообщество в Discord](https://discord.gg/DKrpU3e4pF) · [Поддержать проект на Ko-fi](https://ko-fi.com/samuk190)
