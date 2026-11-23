# MTA:SA Neon para Android

[Site oficial](https://saonlinemobile.web.app/) · [Baixar APK](https://saonlinemobile.web.app/pt/download)

Cliente Android nativo ARM64 do **Multi Theft Auto: San Andreas** (MTA:SA), baseado no MTA Neon e no motor do Grand Theft Auto: San Andreas Mobile.

## Idiomas do README

[English](README.md) · **Português (Brasil)** · [Español](README.es.md) · [Deutsch](README.de.md) · [简体中文](README.zh-Hans.md) · [Русский](README.ru.md)

## Idioma do launcher

Por padrão, o launcher segue as preferências de idioma do Android e usa inglês quando não há uma tradução disponível. Abra **Configurações → Idioma** para selecionar outro idioma. A alteração é imediata e fica salva. Selecione **Idioma do aparelho** para voltar a seguir o Android.

## Requisitos

- Celular ARM64 (`arm64-v8a`) com Android 8.0 ou superior.
- Cópia original de **GTA San Andreas Mobile 2.10** para Android, adquirida legalmente, de onde você extrairá os dados e bibliotecas do jogo.
- Para a instalação manual, um computador com [ADB](https://developer.android.com/tools/releases/platform-tools) ou um gerenciador de arquivos com acesso a `Android/data`.

## Instalação e primeiros passos

1. Baixe o APK na [página oficial de download](https://saonlinemobile.web.app/pt/download), instale e abra o launcher uma vez para criar a pasta do aplicativo.
2. Se o APK foi compilado com `CDN_ROOT_URL`, abra **Configurações → Sincronizar arquivos do jogo**. O launcher verifica os downloads e instala os dados e as três bibliotecas ARM64 em armazenamento privado. Nas versões sem CDN, siga a instalação manual abaixo.
3. Em **Servidores**, escolha um servidor, toque em **Entrar**, informe seu apelido e toque em **Jogar**. Conecte sua conta em **Configurações → Entrar com Discord** se o servidor exigir login no SA:O.

O identificador do aplicativo é `com.saonline.neonlauncher`. A instalação usa uma pasta e uma chave próprias, inclusive em aparelhos com versões antigas do launcher World RPG.

### Instalação manual dos arquivos do jogo

O APK e os arquivos OBB do seu GTA são arquivos ZIP. Extraia os seguintes itens.

| Origem | Como localizar | O que extrair |
|---|---|---|
| APK do GTA | Execute `adb shell pm path com.rockstargames.gtasa` e copie o `base.apk` listado com `adb pull` | O conteúdo de `assets/` e as bibliotecas `libGTASA.so`, `libSCAnd.so` e `libOpenAL64.so` de `lib/arm64-v8a/` |
| OBB do GTA | `Android/obb/com.rockstargames.gtasa/*.obb` | Todo o conteúdo, incluindo `anim/`, `audio/`, `data/`, `models/` e `texdb/` |

Organize os arquivos desta forma na pasta `game_assets` do aparelho, mantendo as maiúsculas e minúsculas dos nomes.

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

[Ver a árvore de referência completa (410 arquivos)](docs/game-assets-tree.txt). Ela mostra cada caminho do manifesto do projeto, incluindo as telas de carregamento do MTA. Na instalação manual, copie o conteúdo de `assets/` do seu APK e dos arquivos OBB diretamente para `game_assets/`. Mantenha os nomes originais e os arquivos adicionais da sua cópia, inclusive `assetfile.txt` se existir. As três bibliotecas ARM64 ficam diretamente em `lib/`.

Se você organizou os arquivos em `gta_sa/` no computador, envie o conteúdo da pasta com ADB.

```bash
adb push gta_sa/. /sdcard/Android/data/com.saonline.neonlauncher/files/game_assets/
```

O Android apaga a pasta do aplicativo ao desinstalá-lo. Guarde uma cópia dos arquivos extraídos no computador.

### Imagens de carregamento

O APK inclui 14 imagens em `MTA/cgui/images/loadscreens/`. O jogo usa primeiro as imagens encontradas na instalação e recorre às do APK quando necessário. A fonte opcional fica em `MTA/cgui/sabankgothic.ttf`. Para distribuir imagens ou a fonte por CDN, publique os arquivos em `game_files_android_mta/MTA/cgui/`, inclua seus tamanhos e hashes SHA-256 em `manifest_android_mta.json` e sincronize novamente.

## Compilação

Instale o Android Studio (Hedgehog ou posterior) com JDK 17, SDK Android 34, NDK `25.1.8937393` e CMake `3.22.1`. Faça checkout da [branch do PR #135 do servidor](https://github.com/Dryxio/san-andreas-online/pull/135) em uma pasta irmã chamada `san-andreas-online`. Instale a versão do Go indicada em `Shared/transport/go.mod` e coloque-a no `PATH`. Se o checkout estiver em outro local, aponte a variável CMake `NEON_TRANSPORT_DIR` para a pasta `Shared/transport` desse checkout.

Crie o `.env` a partir de [`.env.example`](.env.example) e ajuste os caminhos e as opções do launcher.

```bash
cp .env.example .env
```

| Variável | Uso |
|---|---|
| `GAME_ASSETS_DIR` | Pasta dos arquivos extraídos no computador. Padrão `./game_assets`. |
| `DEVICE_ASSETS_PATH` | Pasta do jogo no aparelho. |
| `CDN_ROOT_URL` | URL opcional de um CDN com os arquivos e o manifesto. |
| `LAUNCHER_TITLE` | Nome exibido no launcher. |
| `LAUNCHER_PLAY_BUTTON_TEXT` | Rótulo opcional para todos os idiomas. Deixe vazio para usar a tradução de **Jogar**. |

```bash
./gradlew assembleDebug
./gradlew assembleRelease
./gradlew installDebug
```

Os APKs ficam em `app/build/outputs/apk/debug/` ou `app/build/outputs/apk/release/`. Para assinar a versão de release, configure `storeFile`, `storePassword`, `keyAlias` e `keyPassword` em `keystore.properties`. A versão de release inclui o launcher e as imagens de carregamento do MTA. Para testar a instalação de arquivos do jogo sem empacotá-los em um APK de depuração, execute `./gradlew assembleDebug -PnoGameFiles`.

Para verificar e enviar os arquivos locais definidos no `.env` a um aparelho conectado, execute `bash tools/sync_local_assets.sh` ou `./gradlew pushAssetsToDevice`.

## Servidores compatíveis

Compile o servidor conforme as instruções do [San Andreas Online](https://github.com/Dryxio/san-andreas-online). A conexão padrão usa o protocolo Neon `0x41E7` e o bitstream móvel `0x0030`. Para admissão Android verificada, o launcher precisa confiar no emissor HTTPS do operador, e o emissor precisa autorizar o ID do aplicativo e o certificado de assinatura do APK.

Para um servidor QUIC, acesse **Favoritos → Adicionar**, informe o nome de host do certificado com o prefixo `quic://` e use a porta UDP específica do servidor QUIC. A conexão usa a chave de instalação do Android Keystore e o certificado de cliente do Neon Identity. O operador pode exigir verificação de integridade via Google Play. Para a conexão clássica, use o nome de host simples e a porta clássica.

## Documentação e comunidade

- [Roteiro de sincronização e status do projeto](README.md#project-roadmap-mta-sync-porting-plan-pc-to-android)
- [Catálogo de funções Lua do cliente](docs/lua-functions/README.md) e [matriz de compatibilidade](docs/PARITY_MATRIX.md)
- [Referência de protocolo](docs/PROTOCOL_REFERENCE.md) e [especificação de sincronização](docs/MTA_SYNC_ANDROID_PORT.md)
- [Licença e créditos](README.md#license--attribution)
- [Comunidade no Discord](https://discord.gg/DKrpU3e4pF) · [Apoie o projeto no Ko-fi](https://ko-fi.com/samuk190)
