# Matriz de paridade do Neon no Android

Auditoria de fonte em 11 de novembro de 2026, no horário de Brasília. Referência desktop `mta_neon_source`, revisão `c3fe13e6af5824608a5127f86e936c58e37eb997`. Implementação Android examinada em `worldrpg_neon_launcher/app/src/main/cpp` e em seus pontos de entrada Java/Kotlin.

O checkpoint de execução posterior desta rodada está em [VALIDACAO_ANDROID_2026-11-11.md](VALIDACAO_ANDROID_2026-11-11.md).

A classificação descreve o comportamento encontrado no código atual. A certificação de execução exige build remoto e evidência no dispositivo. Esta auditoria não executou compilação nem testes de jogo. O histórico de `CHANGELOG.md` serve de contexto, enquanto o estado abaixo foi obtido dos corpos das funções e de seus chamadores.

## Estados usados

| Estado | Critério |
| --- | --- |
| **Implementado** | O componente delimitado tem lógica executável no fonte. A coluna de integração informa se o caminho de jogo o utiliza. |
| **Parcial** | Existem partes funcionais, mas faltam contratos, integração ou validação necessária ao comportamento Neon. |
| **Stub** | O método registra a chamada, retorna valor padrão ou possui corpo sem a operação anunciada. |
| **Ausente** | O runtime Android examinado ainda não contém a implementação do contrato. |

Os estados pertencem ao escopo de cada linha. Um interpretador implementado pode coexistir com bindings Lua em estado Stub. Um arquivo de interface, offset preenchido ou símbolo exportado precisa ter assinatura, objeto, thread, ciclo de vida e comportamento comprovados antes de representar uma função portável.

## 1. Bootstrap, transporte e recursos

| Capacidade | Estado Android | Evidência e integração atual | Dependência para equivalência |
| --- | --- | --- | --- |
| Instalação versionada dos arquivos GTA | **Implementado** como instalador | `NativeAssetBridge.syncAssets`, gerações, hashes, publicação atômica de `active` e trava de instalação. `getReadyStoragePath` verifica a presença e o tamanho do subconjunto obrigatório no início. [A1] | Integridade semântica dos dados de jogo e retenção da geração utilizada pela engine. SHA-256 confirma o conteúdo publicado, incluindo eventual defeito que já exista na origem. |
| Biblioteca GTA residente | **Implementado** como carregamento | `GameEngine::initialize` faz `dlopen`, encontra a base, guarda a tabela e marca estado. `nativeBootEngine` expõe esse estágio. [A2] | Boot completo da aplicação GTA e uso das funções com ABI confirmada. |
| Host nativo Android com Surface/EGL | **Parcial** | `EngineHostActivity` deriva de `GTASA`, carrega a biblioteca por `System.loadLibrary`, usa a geração ativa e os wrappers NVIDIA/WarMedia. A entrada é o botão debug **Testar GTA**. [A3] | Evidência de mundo jogável, controles, áudio, pausa, retomada, encerramento e reabertura. O host debug e o caminho de rede de `GameActivity` ainda são entradas separadas. |
| Bootstrap do modo multiplayer | **Ausente** como caminho integrado | `GameActivity` usa `GLSurfaceView`. Seu `nativePulse` processa rede e join. A Activity de teste GTA não recebe o estado MTA desse caminho. [A2, A3] | Uma sessão deve possuir a engine, os recursos e o estado de rede e entregar mutações na thread do GTA. Definir os estados reais de prontidão para cada etapa. |
| Transporte RakNet e join Neon | **Parcial** | `RakNetAdapter`, `RakNetWrapper` e `NeonJoinClient` contêm transporte e máquina de estados reais. O join conhece ElementID de 17 bits. Há limitações explícitas de serial, autenticação e senha nos contratos/documentação. [A4] | Validar a sessão contra o servidor Neon da revisão e obter seus dados iniciais. O `net.so` do servidor e o cliente Android têm papéis e interfaces diferentes. |
| Envelope de pacotes de jogo | **Parcial** | `PacketHandler::handlePacket` descasca `[99][gameId][body]`. O enum `MTAPacketID` mantém numeração provisória de 0 a 11. [A5] | Derivar IDs e schemas de `Shared/sdk/net/Packets.h`, `CPacketHandler` e versão de bitstream, incluindo sentido cliente/servidor e limites de pacote. |
| `ENTITY_ADD`, remoção e raízes | **Stub** para o contrato Neon | O handler `ELEMENT_CREATE` lê somente `u8 tipo + u16 id`, depois cria um registro local. O Neon tem `Packet_EntityAdd` com dados por tipo e relações. [A5, D1] | Decoder completo, ElementID real, raízes de servidor/recurso/GUI, relacionamentos, propriedades iniciais, referências adiadas, destruição e rollback de pacotes malformados. |
| Start/stop de recurso enviado pelo servidor | **Ausente** | Nenhum handler Android de `RESOURCE_START` ou gerenciador equivalente a `CResourceManager` foi encontrado no caminho atual. O fonte desktop possui `Packet_ResourceStart`. [A5, D1, D2] | Ler metadados, prioridades e tipos de arquivo, criar o recurso e suas raízes e executar o ciclo completo de inicialização e parada. |
| Arquivos de recursos Lua/GUI/HTML do servidor | **Ausente** como pipeline MTA | O instalador do CDN trata os arquivos GTA. Os componentes Android atuais não ligam o download de recursos do servidor a `LuaVMStub::executeBuffer/executeFile`. [A1, A6, D2] | HTTP do servidor, checksum exigido pelo protocolo, cache por servidor/recurso, `autoDownload`, `downloadFile`, evento de conclusão e isolamento de caminhos. |

## 2. Elementos, engine, sincronização e saúde

| Capacidade | Estado Android | Evidência e integração atual | Dependência para equivalência |
| --- | --- | --- | --- |
| Registro local de jogadores, peds, veículos e objetos | **Implementado** como estrutura em memória | `ElementManager` possui pools, create/destroy e cópia de dados de sync. Somente quatro tipos têm operações de pool. [A7] | Ligar ElementID, ciclo de streaming e callbacks à criação e destruição de objetos reais do GTA. |
| Identidade de elementos do Neon | **Parcial** | O join lê 17 bits, enquanto `shared/ElementTypes.h` usa `uint16_t`, inválido `0xFFFF`, e o enum local de tipos tem sete entradas. [A4, A7] | Uma representação consistente que preserve todo o domínio, o valor inválido, os tipos da revisão e referências entre elementos. |
| Peds e jogadores visíveis | **Stub** na ponte de engine | `GameEngine::spawnPed` retorna `INVALID_PED` quando o offset é zero. `EngineOffsets::forVersion` mantém create/delete/position/health de peds sem ligação comprovada. [A8] | Confirmar ABI da engine exata, carregar modelo, criar ped, inseri-lo no mundo e aplicar posição, rotação, estado e remoção na thread correta. |
| Veículos e objetos visíveis | **Stub** na ponte de engine | Mesma estrutura de delegação condicionada a offsets, sem ponte comprovada para create/delete, física e propriedades. `CWorld::Add` recebe uma entidade existente e não substitui seu construtor. [A8, D3] | Spawn/streaming de modelos, referências de motor, motorista/passageiro, dimensões/interiores, colisão, remoção e prevenção de uso após destruição. |
| Solicitação e residência de modelos | **Parcial** | A tabela efetivamente mapeada em `EngineOffsets::forVersion` possui apenas `loadAllModels` e `hasModelLoaded`. `requestModel` continua sem ligação. [A8] | Confirmar assinaturas e semântica no ELF exato, implementar request/release/refcount, espera pelo carregamento e tratamento de modelo ausente. |
| Sincronização de player, ped, veículo e objeto | **Stub** para o wire Neon | `SyncTypes.h` serializa IDs `u16` e sequências fixas de floats. O desktop usa campos opcionais, flags, compressão, contexto e estruturas específicas. [A9, D4] | Codecs por tipo/direção/versão com capturas e testes de byte/bit exatos. Depois aplicar o estado à engine e produzir o estado local de saída. |
| Interpolação, timeout e correção de posição | **Stub** | `SyncManager` atualiza um alpha global e deixa a comparação de timestamps em TODO. O tick JNI atual não chama `SyncManager::update`. [A2, A9] | Histórico por entidade, relógio de rede, interpolação/extrapolação limitada, reconciliação e descarte de atualizações antigas. |
| Autoridade de syncer e migração de peds/veículos | **Ausente** | Não há equivalente Android ao `CPedSync`/`CUnoccupiedVehicleSync` desktop, nem início/parada de sincronização ou apresentação de tarefas por autoridade. [D4, D6] | Propriedade no servidor, eleição/migração, congelamento e retomada consistentes e filas de atualização na thread GTA. |
| Saúde, colete, dano, morte e respawn | **Stub** para a sessão MTA | A memória local tem campos de saúde/colete e o Lua expõe nomes vazios. A ponte GTA correspondente permanece zerada. O desktop comprime saúde de player em 8 bits e de veículo em 12 bits. [A6, A8, A9, D4] | Codecs corretos, aplicação local e remota, eventos, ordem de morte/spawn e regras de autoridade. Instrumentar valores antes/depois, dano, arma, água, posição e motivo de morte. |
| Skin/modelo de ped, animação e tarefas | **Ausente** como runtime Neon | `setPedAnimation` é binding vazio. O campo `taskType` do schema simplificado não implementa a árvore de tarefas nativas do desktop. [A6, A9, D6] | Modelo e dicionário de animações, tarefas reais, controle remoto, cancelamento, apresentação e ownership. |
| Blips, pickups, radar areas, markers e colshapes | **Ausente** como elementos completos | Parte dos nomes existe no enum local, mas não há pools/handlers completos nem renderização equivalente no caminho atual. [A5, A7] | Tipos e propriedades de protocolo, streaming, visibilidade, dimension/interior, eventos e integração com radar/mundo. |

## 3. Lua, eventos e interfaces

| Capacidade | Estado Android | Evidência e integração atual | Dependência para equivalência |
| --- | --- | --- | --- |
| Interpretar Lua e reportar erro | **Implementado** como componente | `LuaVMStub` usa `luaL_newstate`, `luaL_loadbuffer`/`luaL_loadfile` e `lua_pcall` com Lua 5.3.6 vendorizado. [A6] | Execução pelo recurso correto, limites e ciclo de vida. Atualmente só há criação do interpretador no JNI, sem chamada que execute um arquivo de recurso recebido. |
| Semântica e bytecode Lua do Neon | **Parcial** | O desktop constrói `Lua_Client` como `lua5.1c` e adiciona classes, segurança e carregamento em `CLuaMain`. O Android usa Lua 5.3.6. [A6, D5] | Definir a VM compatível com scripts 5.1, extensões e bytecode aceito pela revisão. Validar diferenças de linguagem, bibliotecas, ambiente/OOP e compilação de scripts. |
| Bindings MTA para Lua | **Stub** | Os 31 nomes registrados em `LuaVMStub::initialize` apontam para lambdas que fazem log e retornam zero. Incluem saúde, posição, eventos, GUI, DX, arquivos, timers e JSON. [A6] | Binding com leitura/validação de argumentos, retornos, userdata, erros, referências Lua e acesso ao recurso/engine. Cada função precisa de teste de contrato. |
| `root`, `resourceRoot`, `localPlayer`, element data e OOP | **Ausente** | O runtime atual não cria ambientes de recurso, userdata nem a árvore e propagação de eventos do `CClientEntity` desktop. [A6, A7, D2, D5] | Árvore de elementos real, lifetime, herança de eventos, sync de element data e classes/metamétodos compatíveis. |
| Eventos locais/remotos, timers, exports e comandos | **Stub** | `addEventHandler`, `triggerServerEvent`, `setTimer` e similares são stubs. Dispatch de eventos, timers por recurso, exports e comando remoto não foram encontrados. [A6, D5] | VM compatível, registro/refcount, ordem de eventos, serialização de argumentos, cancelamento no stop e controles do servidor. |
| Sandbox, arquivo, JSON/XML e cleanup por recurso | **Stub** | `luaL_openlibs` abre todas as bibliotecas padrão. `fileOpen/fileRead/fileClose/toJSON/fromJSON` são vazios. Não há equivalente a `CLuaMain::InitSecurity`. [A6, D2, D5] | Política de filesystem por recurso, APIs compatíveis, orçamento de execução, liberação de funções/timers/elementos e isolamento entre recursos. |
| Primitivas GLES e fonte bitmap | **Implementado** como componente de desenho | `RenderWareHooks::drawRect` emite chamadas GLES. `BitmapFont` cria atlas, shader e texto ASCII. [A10] | Inicializar no contexto GTA, informar resolução, preservar/restaurar estado GL e inserir o desenho no frame real. O JNI atual apenas aloca o renderizador. |
| `dxDrawText`, retângulos, imagens e fontes MTA | **Stub** na API Lua | Os bindings DX são vazios. `drawTexturedRect` somente faz log e `loadTextureFromTexDb` retorna zero. [A6, A10, D7] | Ponte Lua, filas/frame, textura/fontes, cores, clipping, prioridade pre/postGUI, medição e escala. |
| Render targets, shaders e materiais | **Ausente** | Não há equivalente Android para `dxCreateRenderTarget`, `dxCreateShader` e os materiais Direct3D da engine desktop. [D7] | Backend GLES com contratos de perda de contexto, render targets e tradução/compatibilidade de shaders. A sintaxe HLSL do desktop exige uma decisão de compatibilidade explícita. |
| GUI MTA e foco/teclado | **Stub** | `guiCreateWindow/guiCreateButton/guiSetText/guiGetText` são nomes vazios. Widgets, layout, grid list, foco, cursor e eventos não foram implementados. [A6, D7] | Runtime GUI, resolução, hierarquia, estilos, toque, IME e eventos Lua. As telas Kotlin do launcher atendem a navegação do app e não expõem esses contratos aos recursos. |
| Browser MTA | **Ausente** | Nenhum host WebView/browser ou binding `createBrowser/guiCreateBrowser` foi encontrado no projeto Android atual. O desktop usa `CClientWebBrowser` e render item Direct3D. [D8] | Navegador Android, páginas locais do recurso, URLs remotas, política de domínios, JavaScript/eventos, textura, toque, teclado, áudio e cleanup no stop do recurso. |
| HUD WorldRPG | **Parcial** como componentes de suporte | Há `HUDRenderer` próprio e controles de layout via JNI. O recurso real `worldrpg_hud` depende de `onClientRender`, element data, saúde/colete e bindings DX que ainda faltam. [A6, A10, W1] | Recursos + Lua/eventos + estado da sessão + desenho GTA. Validar o mesmo script WorldRPG no desktop e Android. |
| Voz e áudio 3D | **Parcial** | `VoiceCodecStub` usa Opus real. Não há integração desse codec no estado JNI atual. O rádio WorldRPG utiliza `playSound3D` e eventos/element data. [A11, W2, D9] | Microfone, reprodução, protocolo/codec negociado, buffers, mute, perda de pacotes, distância, streaming HTTP e lifetime por recurso. |
| Capacidades, ACL e integridade do cliente | **Parcial** | Há `AdminPermissionGate` e um validador local de integridade. O hash de binário do validador contém TODO de comparação. Não se encontrou integração desse validador no tick JNI. [A2, A11] | Ações administrativas autorizadas pelo servidor, divulgação verdadeira de capacidades e contrato de validação/autenticação compatível com a sessão Neon. |

## 4. Extensões específicas do Neon

O README desktop descreve famílias além da API MTA base. A contagem de funções anunciada pelo README precisa de inventário por revisão para certificar cada assinatura, evento e comportamento. A matriz abaixo classifica essas famílias no runtime Android próprio examinado. [D10]

| Família Neon | Estado Android | Referência desktop e dependências |
| --- | --- | --- |
| NPCs com IA nativa, wander/navigation, perfis, grupos, casais e respostas a dano | **Ausente** | `CLuaPedDefs`, `CClientPed`, `CPedSync` e `game_sa/CTasksSA`. Exige peds reais, tarefas mobile equivalentes, autoridade e apresentação sincronizada. [D3, D6] |
| Tráfego nativo, autopilot e gravação de rotas | **Ausente** | `CLuaVehicleDefs`, `CLuaTaskDefs`, `CClientVehicle` e cenários native-drive. Exige veículos, syncer, tarefas/colisão e handoff. [D3, D6] |
| Mundo estendido, água, mapa F11 e radar ampliado | **Ausente** | `EXTENDED_RADAR.md`, `NATIVE_WORLD_HANDOFF.md`, `CLuaWorldDefs` e testes native-world. Exige streaming, colisão, limites e representação de radar no mobile. [D10] |
| Pools elevados e modelo/asset registry | **Ausente** | `LIMIT_PATCHING.md`, `ENTITY_PERFORMANCE.md` e `server-model-registry-test`. As constantes locais de pools não equivalem aos pools e patches reais do GTA. [A7, D10] |
| Colisão criada/editada em runtime, folhagem e efeitos 2DFX | **Ausente** | `CLuaRuntimeCollisionDefs`, `CLuaEngineDefs`, `CLua2DFXDefs`, modelo COL/streaming da revisão. [D7, D10] |
| Física dinâmica de objetos, fratura, cordas e cargo | **Ausente** | `CLuaObjectDefs`, `CLuaRopeDefs`, elementos correspondentes e testes native-cargo/native-simulation. Exige objetos, física, leases e transporte de estado. [D3, D10] |
| Fogo e pássaros sincronizados | **Ausente** | `CLuaFireDefs`, `CLuaBirdDefs` e gerenciadores/elementos da revisão. [D10] |
| Áudio de veículos e bancos de som, backfire | **Ausente** | `CLuaAudioDefs` e `CVehicleSoundManager`. Exige runtime e formatos de áudio mobile e associação a veículo/estado. [D9] |
| UI nativa Neon, story, script camera e missão | **Ausente** | `CLuaNativeUIDefs`, `STORY_RUNTIME.md`, `STORY_HANDOFF.md` e testes native-ui/native-script-camera/native-mission. Exige contratos e ownership por recurso e equivalentes da engine Android. [D7, D10] |

## 5. Ordem mínima para WorldRPG

| Ordem | Entrega | Dependências | Critério observável |
| --- | --- | --- | --- |
| 1 | Bootstrap GTA homologado | Geração CDN validada, aliases, JNI e Surface/EGL | Abrir pelo caminho **Configurações → Testar GTA**, entrar no mundo, movimentar um personagem, verificar colisão e modelos e repetir pausa/retomada/saída/reabertura. Registrar APK, hashes de bibliotecas, geração, logs e imagem. |
| 2 | Sessão Neon ARM64 validada | Transporte, autenticação, versão e join | Servidor registra o cliente correto, cliente recebe a raiz e os dados iniciais. Manter a conexão e demonstrar reconexão. Esta frente pode avançar em paralelo ao item 1. |
| 3 | Entidades e um recurso diagnóstico | Itens 1 e 2, IDs/codecs, árvore de elementos, gerenciador de recursos | Mesmos ped/veículo/objeto vistos no desktop e Android. Recurso baixa, valida, inicia, envia evento ao servidor e para com limpeza de estado. |
| 4 | Lua compatível e APIs usadas pelo core/HUD | Item 3, sandbox, eventos, timers, element data e desenho | Executar `worldrpg_core` cliente/shared e `worldrpg_hud` reais. Login, dados de conta, saúde/colete, dinheiro, render e comandos têm comportamento correspondente. [W1] |
| 5 | Movimento, saúde, dano, morte, spawn e veículos | Itens 3 e 4, streaming, modelos, sync e física | Caminhar/correr, cair, entrar/sair de veículo, trocar syncer, morrer e reaparecer. Registrar o caminho de causa de cada mudança de saúde. Comparar capturas desktop/Android e interpretação do servidor. |
| 6 | NPCs/tráfego e demais recursos WorldRPG | Item 5, tarefas nativas, autoridade e apresentação | Recursos `native-ped-traffic` e `native-vehicle-traffic` executam os contratos identificados. Políticas de fallback devem declarar a capacidade reduzida e permanecer separadas do aceite de paridade nativa. |
| 7 | Rádio, interfaces avançadas e paridade Neon completa | APIs usadas por cada recurso e catálogo da revisão | Integrar `worldrpg_radio`, GUI/browser/shaders e as famílias Neon com cenários por função/evento/elemento, incluindo falha, cleanup e limites. [W2, D10] |

A investigação de morte deve acompanhar, por entidade e pacote, saúde/colete antes e depois, posição/interior/dimensão, contato com água, dano/arma/autor e motivo do respawn. O servidor lê e aplica saúde em `CPlayerPuresyncPacket`, portanto erro de layout e dano real do mundo devem poder ser diferenciados pelos registros. Preservar cenários de queda, afogamento, veículo, login com posição persistida e migração de syncer. [D4]

## 6. Evidências de fonte

Os caminhos Android abaixo partem de `worldrpg_neon_launcher/`. Os caminhos desktop partem de `mta_neon_source/`. Âncoras de linha refletem o momento desta auditoria e devem acompanhar a revisão em futuras atualizações.

| Chave | Arquivos e símbolos examinados |
| --- | --- |
| **A1** | `app/src/main/java/com/worldrpg/neonlauncher/utils/NativeAssetBridge.kt` — `getReadyStoragePath`, `syncAssets`, `activeGeneration`, `assetFile`. `app/src/main/cpp/shared/AssetManager.cpp:234` — `initialize` guarda a pasta. |
| **A2** | `app/src/main/cpp/jni/JNI_OnLoad.cpp:114` — `nativeInit` e componentes construídos. `:197` — `nativeBootEngine`. `:292` — `nativePulse`. `:321` — `nativeSetAssetPath`. `app/src/main/cpp/gtasa/GameEngine.cpp:106` — `initialize`. |
| **A3** | `app/src/main/java/com/worldrpg/neonlauncher/game/EngineHostActivity.java` — `onCreate`, `GetGameBaseDirectory`, `shouldStartEngine`. `app/src/main/java/com/nvidia/devtech/NvEventQueueActivity.java` — SurfaceHolder, `jniNvAPKInit`, init/resume/EGL/cleanup. `app/src/main/java/com/wardrumstudios/utils/WarMedia.java` — `STORAGE_ROOT`. `app/src/main/java/com/worldrpg/neonlauncher/game/GameActivity.kt:207` — fluxo GLSurfaceView de rede. `app/src/main/java/com/worldrpg/neonlauncher/ui/SettingsFragment.kt` — entrada debug. |
| **A4** | `app/src/main/cpp/net/NeonJoinProtocol.h:70` — versões e domínio ElementID. `NeonJoinProtocol.cpp:110` — `ReadElementID`. `net/raknet/RakNetWrapper.cpp` e `RakNetAdapter.cpp` — transporte e tradução de notificações. `docs/TRANSPORTE_NEON.md` — limites conhecidos e evidência de interoperação. |
| **A5** | `app/src/main/cpp/shared/PacketHandler.h:24` — enum de jogo provisório. `PacketHandler.cpp:29` — registros de oito handlers. `:118` — envelope. |
| **A6** | `app/src/main/cpp/shared/LuaVMStub.cpp:63` — bibliotecas padrão. `:91` — inicialização e 31 bindings log-only. `:136`/`:177` — execução real Lua. `LuaVMStub.h` — interfaces existentes. `app/src/main/cpp/lua53/` — VM vendorizada. |
| **A7** | `app/src/main/cpp/shared/ElementTypes.h:9` — tipos e `uint16_t`. `ElementManager.cpp:72` — pools, create/destroy e `update*Sync`. |
| **A8** | `app/src/main/cpp/gtasa/GameEngine.cpp:22` — seleção de dois offsets e lacunas de ABI documentadas. `:165` — `spawnPed`. `:276` — `requestModel`. `GTASAOffsets.h` — tabela histórica cuja identificação textual exige conferência do símbolo e da assinatura. |
| **A9** | `app/src/main/cpp/shared/SyncTypes.h:12`/`:57`/`:102`/`:136` — schemas fixos locais. `SyncManager.cpp:15` — alpha global. `:30` — timeout TODO. `:64` — update. |
| **A10** | `app/src/main/cpp/gtasa/RenderWareHooks.cpp:110` — inicialização. `:133` — hooks vazios. `:143`/`:182` — primitivas. `:188`/`:194` — textura stub. `BitmapFont.cpp:348` — atlas. `HUDRenderer.cpp:110`/`:118` — init/desenho próprio. `jni/JNI_HUD.cpp` — calibração. |
| **A11** | `app/src/main/cpp/shared/VoiceCodecStub.cpp:25`/`:94`/`:129` — codec Opus. `AntiCheatValidator.cpp:219` — comparação de hash pendente. `app/src/main/java/com/worldrpg/neonlauncher/game/AdminPermissionGate.kt` — gate do cliente. |
| **D1** | `Client/mods/deathmatch/logic/CPacketHandler.cpp:2860` — `Packet_EntityAdd`. `:5253` — `Packet_ResourceStart`. `Shared/sdk/net/Packets.h` — enum real de pacotes. |
| **D2** | `Client/mods/deathmatch/logic/CResource.cpp`, `CResourceManager.cpp`, `CDownloadableResource.cpp` e `CResourceFileDownloadManager.cpp` — ciclo de recurso, checksum, prioridade e arquivos. `CClientEntity.cpp` — parentes, raízes e propagação. |
| **D3** | `Client/mods/deathmatch/logic/CClientPed.cpp`, `CClientVehicle.cpp`, `CClientObject.cpp` e respectivos managers. `Client/game_sa/` — ponte x86/PC e estruturas reais da engine desktop. |
| **D4** | `Client/mods/deathmatch/logic/CNetAPI.cpp:830`/`:1116` — leitura/escrita de puresync. `CPedSync.cpp` e `CUnoccupiedVehicleSync.cpp` — autoridade. `Shared/sdk/net/SyncStructures.h:189`/`:201`/`:607` — saúde e flags. `Server/mods/deathmatch/logic/packets/CPlayerPuresyncPacket.cpp:191`/`:332` — servidor lê/aplica saúde. |
| **D5** | `vendor/lua/premake5.lua:38` — `Lua_Client`. `Client/mods/deathmatch/logic/lua/CLuaMain.cpp:77`/`:137`/`:216`/`:541` — segurança, VM e scripts. `luadefs/CLuaElementDefs.cpp` e `lua/CLuaFunctionDefs.Event.cpp` — bindings e eventos. |
| **D6** | `Client/mods/deathmatch/logic/luadefs/CLuaPedDefs.cpp:308`/`:319`/`:338`/`:3157` — tarefas/perfis/wander. `CLuaVehicleDefs.cpp:148`/`:1934` — diagnóstico autopilot. `CPedSync.cpp:211`/`:269`/`:289`/`:479`/`:806` — ownership e apresentação. |
| **D7** | `Client/mods/deathmatch/logic/luadefs/CLuaDrawingDefs.cpp`, `CLuaGUIDefs.cpp`, `CLuaEngineDefs.cpp`, `CLuaWorldDefs.cpp`, `CLuaNativeUIDefs.cpp`. `Client/core/Graphics/` e `Client/gui/` — backend desktop. |
| **D8** | `Client/mods/deathmatch/logic/luadefs/CLuaBrowserDefs.cpp`, `CClientWebBrowser.cpp`, `Client/core/Graphics/CRenderItem.WebBrowser.cpp:93` — navegador e textura D3D. |
| **D9** | `Client/mods/deathmatch/logic/luadefs/CLuaAudioDefs.cpp`, `CVehicleSoundManager.cpp` e `lua/CLuaFunctionDefs.Voice.cpp`. |
| **D10** | `README.md:105`, `LIMIT_PATCHING.md`, `EXTENDED_RADAR.md`, `NATIVE_WORLD_HANDOFF.md`, `STORY_RUNTIME.md`, `STORY_HANDOFF.md`, `ENTITY_PERFORMANCE.md`. `test-resources/` contém cenários por família. |
| **W1** | Raiz do projeto, `backend_mta/mods/deathmatch/resources/[worldrpg]/worldrpg_core/meta.xml` e `worldrpg_hud/client/hud_client.lua:13`. HUD usa tela, eventos, timers, element data, saúde/colete, cor e DX. |
| **W2** | Raiz do projeto, `backend_mta/mods/deathmatch/resources/[worldrpg]/worldrpg_radio/client/radio_client.lua:56`. Rádio usa som 3D, estado de veículo, element data, eventos e interface DX. |

Para as próximas validações, aproveitar os contratos e vetores em `Tests/client/MockBitStream_Tests.cpp`, `SyncDataTypes_Tests.cpp`, `SyncPlayer_Tests.cpp`, `SyncVehicle_Tests.cpp` e `Tests/native-ui/resource_protocol.lua`. Completar com cenários desktop/Android em `test-resources/world-sync-regression-test`, `native-simulation-lease-test`, `native-task-handoff-test`, `native-ped-enter-car-test`, `native-world-v3-transport-test`, `native-ui-test`, `extended-radar-test` e `server-model-registry-test`. A aprovação de cada linha deve anexar a revisão e a evidência do caminho correspondente no dispositivo.
