# Etapas do port nativo Neon para Android

O [plano completo](PLANO_PORT_NEON_ANDROID.md) descreve a auditoria, as dependências e os critérios de paridade. Cada entrega deve passar pelo fluxo normal do APK e registrar a evidência de aceite. A conclusão de um item não implica a conclusão da etapa.

| Entrega | Depende de | Aceite verificável | Estado |
|---|---|---|---|
| **P0.1 Inventário técnico** | — | Identificar APK, engine, fontes, módulo do servidor e revisão CDN. | Auditado no plano |
| **P0.2 Referência desktop** | P0.1 | Capturar a sessão e os recursos de um desktop Neon funcional em homologação Oracle. Catalogar APIs da revisão alvo e chamadas efetivas do WorldRPG. | Pendente |
| **P1A.1 Instalação GTA** | P0.1 | Manifesto MTA na raiz, dados da mesma revisão, validação tamanho/SHA-256, staging, recuperação sem perder a geração anterior e bloqueio de Jogar sem dados prontos. | Em execução |
| **P1A.2 APK ARM64** | P0.1 | Compilação Oracle, APK inspecionado com todos os LOADs alinhados a 16 KB e empacotamento válido. Instalação e execução em dispositivo de teste. | Build/auditoria aprovados e S25 validado com páginas de 4 KB; execução em 16 KB e release pendentes. |
| **P1A.3 Fronteira Kotlin/JNI** | P1A.1 | Assinaturas compatíveis, falhas reais retornadas ao launcher, toque com identidade de ponteiro e cleanup ao sair. Estado de transporte distinto de sessão Neon. | Em execução |
| **P1A.4 Boot GTA** | P1A.1–3 | Host WarMedia/NvEventQueue, arquivos, render, toque e ciclo de vida no aparelho. | Checkpoint offline aceito no S25: mundo, CJ, HUD, movimento, retomada, saída e reabertura. Integração multiplayer pendente. |
| **P1A.5 Integração engine/sessão** | P1A.4, P1B.2 | Uma sessão possui a engine, rede e fila de mutações executadas na thread GTA. Recursos e entidades têm ciclo de vida comprovado. | Pendente |
| **P1B.1 Transporte nativo** | P0.2 | Harness ARM64 sem render, vetores de envelope e confiabilidade contra referência Neon. | RakNet4 não interoperou; codec estrutural MTA implementado para vetores sintéticos, sem envio nem cifra. |
| **P1B.2 Sessão nativa** | P1B.1 | Mod, versão, verificação, join e reconnect no servidor Neon, com estados comprovados nos dois lados. | Em execução (máquina de estados implementada; homologação pendente) |
| **P2. Recursos e sessão de jogo** | P1A.4, P1B.2 | Root, resource start/stop, cache HTTP, Lua de diagnóstico e evento de ida e volta. | Pendente |
| **P3. Mundo e sync** | P2 | Ped, veículo e objeto vistos nos dois clientes. Streaming, autoridade, movimento, saúde e respawn testados. | Pendente |
| **P4. Lua, DX, GUI e browser** | P2, P3 | Recursos WorldRPG reais desenham e recebem interação. Isolamento e cleanup dos scripts validados. | Pendente |
| **P5. Paridade Neon** | P3, P4 | Matriz de APIs, eventos e elementos da revisão, com aceite de cada família no Android. | Pendente |
| **P6. Distribuição** | P5 | Release WorldRPG rastreável, matriz de aparelhos, atualização e rollback validados. | Pendente |

P1A e P1B são frentes independentes. A implementação de P1B é Android ARM64 nativo com fonte. As capturas da rota 1.6 servem como casos de regressão, e a aprovação do port exige uma sessão Neon nativa.

## Registro de execução

### 2026-11-11 (ciclo CDN Regra 5 — bootstrap assets publicados)

- `cgui/` (69), `fonts/` (+25 TTF) e `data/script/mainV1.scm` publicados em `game_files_android_mta/` no HF; manifesto regenerado **264 → 358 arquivos** (818.535.866 bytes, rev. 2026-11-11T00:10:36Z). `NativeAssetBridge` já aponta para `manifest_android_mta.json` na raiz — a próxima instalação baixa a geração completa com SHA-256.
- **Bug de produção corrigido**: `file_server.py` na Oracle era pré-suporte-MTA (prefixava tudo com `game_files/` → 404 duplo-prefixo). Deploy da versão MTA-aware + `manifest_android_mta.json` + builder via rsync direcionado; `docker restart worldrpg-file-server`.
- **Evidência de aceite (Regra 4 do skill CDN)**: `GET :7785/manifest_android_mta.json` → 200 (358); 4 paths de amostra → 200 via 307→HF; SHA-256+tamanho byte a byte OK em `mainV1.scm` (3.169.739), `map_1024.png` (1.689.065), `unifont.ttf` (11.934.928).
- Nota: espelho de disco `game_files_android_mta/` NÃO vai para a Oracle (780 MB); servido via redirect 307 — mesmo padrão dos demais `game_files*` (excluídos do rsync do `start.sh deploy`).

### 2026-11-10 (terceiro lote — build de verificação + bootstrap de assets P1A.4)

- **Build de verificação local**: `assembleDebug` BUILD SUCCESSFUL (JDK 17 + NDK 26.1.10909125; Gradle 8.4 incompatível com JDK 27 default — usar `JAVA_HOME=/usr/lib/jvm/java-17-openjdk`). APK 13.350.855 bytes. Símbolos JNI `nativeBootEngine/nativePause/nativeResume` exportados; 66 símbolos `NeonJoinClient` + `TranslateVendorPacketId` linkados. Auditoria 16 KB do APK final: `libWorldRPGNeon.so` LOAD 0x4000, prebuilts 0x10000, sem `libc++_shared.so`, libs Deflate (extração na instalação).
- **Inventário de bootstrap** (`docs/BOOTSTRAP_ASSETS.md`, novo): `assetfile.txt` (120 entradas) cruzado contra protótipo, APK MobileSA 2.10 original e manifesto CDN (264 arquivos). 27 entradas no protótipo, 1 só CDN, **92 ausentes em todas as distribuições (incl. APK oficial MobileSA) — não-bloqueantes** (socialclub 56, xml 8, fonts marca 12, images 10, outros). Descoberto **gap do CDN: `data/script/mainV1.scm`** ausente do manifesto.
- **Assets boot-critical embutidos no APK** (`app/src/main/assets/`, 5,2 MiB, origem protótipo registrada): `assetfile.txt`, `scache*.txt`, `audio/android/NAV_*.ogg`, `data/script/mainV1.scm`+`scriptv1.img`, `Text/american.gxt` (fix do crash `CText::Load`), `Textures/fonts/`. Merge validado no APK rebuildado.
- **Pendente**: publicar `cgui/`+`fonts/` no CDN (Regra 5), corrigir `menu.pvr.dat`, adicionar `mainV1.scm` ao manifesto, validação de boot em dispositivo.

### 2026-11-10 (segundo lote — P1B.2 join real + P1A.2 16 KB)

- **Descoberta crítica de wire format**: a verdade do fio MTA foi confirmada contra a referência production-proven `backend_mta/scripts/mobile_handshake.hpp` (parser que interoperou com pacotes reais do servidor): stream **MSB-first por byte**, inteiros **little-endian**, campos parciais **right-aligned** (semântica `RakNet WriteBits(rightAlignedBits=true)`). O `BitStreamWriter/Reader` do launcher tinha ordem de bits incorreta para campos parciais (bytes completos já estavam corretos). Corrigido em `RakNetWrapper.cpp`.
- **`cpp/net/NeonJoinProtocol.{h,cpp}` (novo)**: máquina de estados do join Neon implementada com layouts extraídos do fonte: `PLAYER_JOIN`(3) vazio → `MOD_NAME`(7) `[u16 bsVer][String mod][u8 authMode][String serverId?]` → `PLAYER_JOINDATA`(4) `[u16 netcode=0x41E6][u16 mta=0x170][u16 bs=0x3D][String playerVer][bit optUpdate][u8 gameVer][char[22] nick][16B md5][char[32] legacy][String neonTicket]` → `SERVER_JOIN_COMPLETE`(2) `[String conn][String sortable]` → RPC(24) `PLAYER_INGAME_NOTICE`(0) → `SERVER_JOINEDGAME`(22) `[ElementID 17b][u8][ElementID 17b][i32 checks][bit voice][2b rate][4b qual][CompressedU32 bitrate][bit fakelag][i32 httpMax][u8 httpType][porta/URL][String name]` → RPC `INITIAL_DATA_STREAM`(1). IDs do `ePacketID` calculados entrada a entrada de `Packets.h` (cross-check: RESOURCE_START=82, ENTITY_ADD=50 nas capturas de `logs/mobile_compat_20261108/`). Estrito por desenho: pacote inesperado durante o join = falha explícita (regra P1B do Plano 22).
- **Enums corrigidos**: `SERVER_JOINEDGAME` 25→**22**, `SERVER_DISCONNECTED` 26→**23**, `RPC` 27→**24** (contagem anterior dos RESERVED estava errada; agora com `static_assert`).
- **`RakNetWrapper`**: auto-join legado removido do caso CRA (o join pertence ao `NeonJoinClient`); timeout de transporte restrito à fase `RAKNET_CONNECTING` (join tem timeouts próprios de 15 s/etapa); falso "Connected to server" no primeiro pacote de jogo removido — `connected=true` só no join completo; novo `IsRakNetConnected()`.
- **JNI**: `NeonJoinClient` no estado; roteamento composto `[99][gameId]` (join primeiro, `PacketHandler` depois); `nativeConnect` inicia o join real; `nativePulse` pulsa o join; `nativeDestroy` desacopla callbacks antes do teardown; `PostConnectionState` fatorado.
- **Testes de wire em host (`tests/`, novo)**: `run_host_tests.sh` compila e executa `test_join_codec.cpp` contra referência portada de `mobile_handshake.hpp`/`mobile_resources.hpp`: u16 LE, bit único, CompressedU32 nos dois sentidos (byte-idêntico), ElementID 17 bits nos dois sentidos (incl. all-ones→INVALID), layout de String, campos 2/4 bits, padding fixo, e parse completo de `SERVER_JOINEDGAME`/`MOD_NAME` sintéticos pela máquina de estados real. **ALL PASSED**.
- **P1A.2 (16 KB)**: auditado em fonte — prebuilts da engine com LOAD 0x10000 (64 KB) ✓, `WorldRPGNeon` com `-Wl,-z,max-page-size=16384` ✓, `ANDROID_STL=c++_static` elimina `libc++_shared.so` do APK ✓, `useLegacyPackaging=true` documentado. **Evidência de APK (build local de verificação, 2026-11-10 20:50)**: `assembleDebug` BUILD SUCCESSFUL (JDK 17, NDK 26.1.10909125); `app-debug.apk` 11.186.496 bytes; LOAD align de todas as libs do APK: `libWorldRPGNeon.so` 0x4000, `libGTASA.so`/`libOpenAL64.so`/`libSCAnd.so` 0x10000; sem `libc++_shared.so`; libs Deflate (extraídas na instalação). Símbolos confirmados no ELF: `nativeBootEngine`/`nativePause`/`nativeResume`, 66 símbolos `NeonJoinClient`, `TranslateVendorPacketId`. Build de release rastreável permanece na Oracle (aceite P1A.2 final).
- **Divergências registradas (bloqueios conhecidos para homologação)**: (1) o serial do jogador vem de `GetClientSerialAndVersion` do módulo net fechado — o aperto de mão serial vive no protocolo interno do netc e precisa de análise de capturas; sem ele o servidor recusa com `SERIAL_VERIFICATION` (regex `^[A-F0-9]{32}$`); (2) `NEON_GAME_VERSION=20` (VERSION_20) é provisório até captura de referência; (3) servidor com `neonauth=required` exige ticket SA:O (recusado explicitamente); (4) senha não vazia exige MD5 (recusado explicitamente); (5) interoperabilidade do handshake RakNet 4.081 vendorizado (protocolo patcheado para 5) com o net module do servidor ainda não provada em dispositivo.

### 2026-11-10 (execução do primeiro lote)

- **P1B.1 (transporte)** — Espaço de IDs de `RakNetWrapper.h` realinhado ao RID de fio do Neon, medido entrada a entrada em `mta_neon_source/Shared/sdk/net/packetenums.h`: `USER_PACKET_ENUM` 271 -> **99**, `INCOMPATIBLE_PROTOCOL_VERSION` 270 -> **95**, RID 28-30/96-98 adicionados. Tradução vendor->RID (`TranslateVendorPacketId`) para as notificações locais do RakNet 4.081 vendorizado (CRA 16->14, DIS 21->19, LOST 22->20, BAN 23->22, PWD 24->23, IPV 25->95); payload do servidor em espaço RID passa intacto. Prefixo UPE duplo removido do `SendJoinRequest` (envelope escrito uma vez pelo `SendPacket`). Buffer de recepção 4 KiB -> 64 KiB com log de truncamento (`bitSize`) no adapter. `PacketHandler` descasca o envelope `[99][gameId][corpo]`; handlers registrados por ID de jogo (base fabricada 100 removida; numeração local continua provisória até P2/ePacketTypes). Sintaxe validada em host (g++ -std=c++20) para as TUs alteradas.
- **P1A.3/P1A.4 (fronteira JNI/boot)** — `GameEngine::initialize` finalmente chamado no fluxo: novo native `nativeBootEngine` (exige `assetsReady`, dlopen `libGTASA.so`, tabela `EngineOffsets::forVersion("2.10")` agora derivada de `GTASAOffsets` **somente com offsets de assinatura comprovada** — `loadAllModels`/`hasModelLoaded`; campos com função divergente documentada (`requestModel`=RemoveModel, `getPlayerPed`=FindPlayerSpeed) permanecem zerados, anti-segfault). `GameActivity.kt` encadeia `nativeInit -> nativeSetAssetPath -> nativeBootEngine -> nativeConnect` com falha explícita por etapa. Lifecycle: `GameEngine::pause/resume/isPaused`, natives `nativePause`/`nativeResume` serializados via `queueEvent` no `onPause`/`onResume`, `nativePulse` suprimido enquanto pausado.
- **Pendente do lote**: boot real do mundo (host WarMedia/NvEventQueue, Surface/EGL da engine, splash), validação em dispositivo (build Oracle), auditoria 16 KB do APK final, P1B.2 (join real `PLAYER_JOINDATA`/`SERVER_JOIN_COMPLETE` com corpus desktop).

### 2026-11-10

- Plano colocado em `worldrpg_neon_launcher/docs/` e rota nativa definida.
- Primeiro lote delimitado a instalação, empacotamento e fronteira JNI. O bootstrap da engine e o cliente de protocolo têm critérios próprios.


### 2026-11-10 — Continuação a partir do teste do S25

- Conferidos os 358 hashes da geração instalada. `outputs/device-assets-validation.json` registra zero divergências.
- Build remoto reproduzido na Oracle. Corrigida a descoberta incremental de NeonJoinProtocol.cpp pelo CMake. As verificações de codec do join passaram na Oracle.
- O diagnóstico e a sonda de rede estão em [TRANSPORTE_NEON.md](TRANSPORTE_NEON.md) e `tests/probe_neon_transport.py`. A abertura RakNet4 ainda falha antes de MOD_NAME, inclusive em sondas na Oracle. O próximo componente de rede é o transporte nativo do fork MTA com sua negociação, confiabilidade e criptografia.
- O botão Testar GTA do APK debug agora usa o host real da engine. Menu, carregamento, mundo 3D, personagem, HUD e movimento responderam no S25. A retomada de Surface após minimizar também renderizou.
- O reparo RLE do loading e a resolução dos nomes referenciados por gta.dat removeram dois crashes de leitura. A saída revelou mutex destruído com a thread NVEvent ainda ativa. A sequência simples quitAndWait -> cleanup em onDestroy bloqueava porque o jogo já estava pausado; a continuação de 11/11 abaixo substitui essa ordem.
- O handoff de GameActivity para o host de mundo e a aplicação de entidades MTA na thread GTA continuam pendentes. O teste offline valida a base de plataforma. A entrada na sessão depende da implementação do transporte Neon.


### 2026-11-11 — Saída da engine e início do transporte MTA próprio

- `EngineHostActivity` agora reside no processo Android `:engine`. A saída por Voltar mantém a Surface ativa, espera um frame real após retomada e executa `quitAndWait` fora da thread de interface. Depois libera a fila nativa e fecha a Activity. O processo da engine é aposentado; os singletons GTA serão novos na próxima abertura e o launcher permanece vivo.
- Aceite no S25: saída do mundo (41 ms de espera nativa), reabertura, minimizar/retomar e sair (34 ms), e saída logo após abrir (18 ms). Logs em `outputs/engine-isolated-*.log`. Os três testes preservaram o PID do launcher e retornaram à tela anterior. Saída espontânea da engine, chamadas/SO sob pressão e áudio ainda têm aceites próprios.
- `NeonJoinClient::OnTransportFailure` finaliza a tentativa na primeira falha de transporte. Pacotes tardios e prazo do join não substituem a causa. JNI deixa de anunciar CRA do transporte como sessão aceita. Testes de codec e regressão passaram na Oracle; o S25 registrou uma única falha, sem o segundo timeout.
- [CONTRATO_TRANSPORTE_MTA.md](CONTRATO_TRANSPORTE_MTA.md) identifica OPEN51, magic MTA, extensões da resposta e helpers da cifra/confiabilidade. `net/mta/MtaOfflineCodec` implementa a estrutura da abertura e a leitura delimitada de respostas. Nonce e bloco fixo são parâmetros obrigatórios; nenhum envio de rede ou valor de sessão presumido foi adicionado.
- [MATRIZ_PARIDADE_NEON.md](MATRIZ_PARIDADE_NEON.md) separa o componente existente de seu uso real. Recursos MTA, bindings Lua, DX/GUI/browser, peds/veículos da sessão e codecs de sync ainda exigem port e integração. Saúde/IDs simplificados não constituem o protocolo Neon e a morte ao andar permanece caso de regressão pendente.
- Próxima entrega P1B: recuperar o bloco fixo da abertura e a permutação inicial da cifra com vetores comprovados; validar OPEN_REPLY real na Oracle, depois conexão confiável e MOD_NAME. Não trocar versões arbitrariamente nem acoplar o join a uma abertura não validada.

- O APK final foi instalado e seu hash conferido no aparelho. O teste de reuso do host pausado preservou a instância da engine e retomou o mundo. Resultado, artefato e limitações em [VALIDACAO_ANDROID_2026-11-11.md](VALIDACAO_ANDROID_2026-11-11.md).
