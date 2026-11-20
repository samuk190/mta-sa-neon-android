# Áudio, voz, armas e projéteis no mobile

Auditoria e segundo lote em `staging`, 2026-11-15. Inclui as alterações locais
anteriores; referência desktop em `mta_neon_source`.

## Atualização após revisão: dois codecs e ponte física de projéteis

O restante deste documento preserva o diagnóstico do segundo lote. O avanço
posterior adicionou Speex ao wrapper de voz e sete funções de projéteis:

- `VoiceCodecStub` agora suporta **Speex e Opus**, com seleção explícita por
  configuração. `DesktopVoiceConfig` escolhe Speex mono a 8/16/32 kHz conforme
  o código recebido do desktop. Opus continua disponível para um protocolo
  que o selecione explicitamente; não há autodetecção nem negociação Opus
  inventada no handshake MTA. Ainda faltam captura, reprodução e transporte.
- Speex 1.2.1 foi incluído com licença e hash da origem. Os 50 testes do codec
  verificam compatibilidade com as chamadas Speex do desktop, Opus mono/stereo,
  reinicialização, pacotes malformados e frames incompletos. A revisão corrigiu
  uma leitura além do buffer no encoder antigo quando recebia poucas amostras.
- `createProjectile`, consultas de tipo/criador/alvo/força/temporizador e
  `setProjectileCounter` agora usam uma ponte física nativa para GTA 2.10 arm64.
  A criação só retorna um elemento depois que `CProjectileInfo::AddProjectile`
  criou um objeto identificável no pool. GTA integra velocidade e colisões;
  não há movimento de projétil simulado por Lua. Posição e tempo são copiados
  do motor a cada frame. Sem motor ou corpo de criador válido, retorna `false`.
- Foram conferidos os símbolos, o tamanho dos pools e as instruções ARM64
  que leem/escrevem os campos usados. O backend verifica essas instruções em
  tempo de execução e se desativa se não corresponderem à biblioteca conhecida.
  Criação/remoção/temporizador executam na thread do jogo, com cancelamento de
  tarefas que não começaram antes do timeout. Corpos destruídos são recusados.
- Limites explícitos: rotação inicial não zero e modelo customizado são
  recusados; anexação de satchel, `onClientProjectileCreation`, associação à
  árvore de elementos e pacotes de projéteis permanecem pendentes. A ponte
  atual cria **projéteis locais**, sem alegar sincronização multiplayer.
- Validação atual: **207 verificações de recursos, 50 de codecs e 8 de filas e
  proteção de projéteis**, todas sem falhas. As oito últimas verificam o
  comportamento sem motor e a execução/cancelamento na thread correta;
  **não comprovam trajetória, colisão ou explosão no aparelho**.

O catálogo completo da árvore de trabalho passa a 647 registros com
implementação, 136 stubs e 798 ausentes. Armas/projéteis têm 20 registros
com implementação de 31, com os limites acima. Essas contagens incluem as
alterações locais anteriores; a revisão do commit preserva esse trabalho.

## Entregue neste lote

| Área | Antes: registros não constantes / total | Depois | Ausentes depois | Stubs depois |
|---|---:|---:|---:|---:|
| Áudio e voz | 19 / 57 | 21 / 57 | 35 | 1 |
| Armas e projéteis | 8 / 31 | 13 / 31 | 18 | 0 |

O catálogo global passa de 633 para **640 registros com implementação**, com
**805 ausentes e 136 stubs**. Isso mede registros, não equivalência completa.
Os cinco getters de arma abaixo consultam uma fotografia do estado recebido;
não significam que armas nativas ou disparos já funcionem.

### Áudio

- `getRadioChannelName`: nomes desktop dos canais 0..12, conversão de argumento
  numérico e limites. Não liga nem troca a estação no GTA.
- `getSoundLength`: duração de um som 2D em segundos, informada pelo MediaPlayer
  ao terminar a preparação; retorna 0 enquanto a duração não é conhecida.
  Não implementa a sobrecarga para voz de um jogador. No Android, a preparação
  é assíncrona, inclusive para arquivo local, diferente do carregamento imediato
  que o desktop pode fazer para consultar a duração de um arquivo.
- `setSoundPan`: agora aplica o balanço aos ganhos esquerdo/direito do
  MediaPlayer. Alterar o volume preserva esse balanço. A sobrecarga de voz
  continua pendente.
- Sons simultâneos não encerram uns aos outros. O limite nativo atual continua
  sendo oito slots. Cada início de recurso tem seus próprios sons: parar um
  recurso libera seus sons, sem interromper os de outro recurso ou outro início.
- Slots reutilizados recebem valores padrão novos. Callbacks antigos de duração
  são ignorados. Término, erro, encerramento da VM e destruição da Activity
  liberam os players.

Referências: `CLuaAudioDefs.cpp:446-497,685-731,2773-2858,2895-2922`,
`CClientSound.cpp:385-414,863-885` e `CStaticFunctionDefinitions.cpp:8281-8285`.

Ainda pendentes: áudio 3D, velocidade efetivamente aplicada ao backend,
efeitos/DSP, FFT, ondas, metadados, rádio nativo, áudio de missões e a
compatibilidade completa de caminhos/formatos de `playSound`. As funções de
distância e velocidade já presentes no catálogo ainda guardam valores sem
um mixer 3D equivalente ao desktop.

### Armas

Novas consultas para **elementos `weapon` do servidor**, não armas de peds:

| Função | Comportamento |
|---|---|
| `getWeaponAmmo` | Munição total do último `ENTITY_ADD`. |
| `getWeaponClipAmmo` | Munição do carregador do último `ENTITY_ADD`. |
| `getWeaponState` | Estado recebido, traduzido para `ready`, `firing` ou `reloading`. |
| `getWeaponOwner` | Jogador dono, ou `false` sem dono válido. |
| `getWeaponTarget` | Três coordenadas para vetor; elemento para alvo entidade; `nil` para alvo fixo ou entidade destruída. |

O parser preserva também dano, precisão, alcances e os 13 flags, mas os getters
de flags e seus setters não foram registrados neste lote. A leitura do subalvo
agora acompanha o tipo da entidade: ped/jogador tem 8 bits, veículo tem 4 bits.
Alvos desconhecidos seguem a ramificação desktop, sem consumo de subalvo.
O dono só é aceito como jogador no momento da leitura. Remover uma entidade ou
um jogador limpa as referências; reutilizar o ID não ressuscita o alvo/dono.

Referências: `CPacketHandler.cpp:3139-3359`,
`CLuaWeaponDefs.cpp:288-307,363-415,606-648`,
`CClientWeapon.cpp:22-23,543-569`,
`CStaticFunctionDefinitions.cpp:8052-8070` e
`lua/CLuaFunctionParseHelpers.cpp:374-378`.

**Limites:** nenhum `CClientWeapon` nativo é criado; tiros, consumo de munição,
alterações posteriores por RPC e renderização não foram portados. Por isso as
consultas são limitadas ao último estado recebido em `ENTITY_ADD`. O protocolo
continua em `0x0030` e não houve novas escritas em estruturas GTA.

## Voz: incompatibilidade confirmada

O desktop usa **Speex**, não Opus:

- `CVoiceRecorder.cpp:55-124` inicializa um encoder Speex.
- `CVoiceRecorder.h:33-44` e `CVoiceRecorder.cpp:165-191` mapeiam a configuração
  recebida do servidor para 8, 16 ou 32 kHz.
- `CVoiceRecorder.cpp:246-337` codifica Speex, envia `VOICE_DATA` (71) com
  comprimento `uint16` e encerra com `VOICE_END` (72).
- `CPacketHandler.cpp:5798-5824` recebe jogador + comprimento + dados e chama
  o decoder de voz desse jogador.
- `CClientPlayerVoice.cpp:121-150` decodifica com `speex_decode_int`.

`VoiceCodecStub.cpp` no mobile contém um encoder/decoder **Opus real**, mas isso
não o torna compatível com esses pacotes. Não foi conectado ao protocolo.
`isVoiceEnabled` permanece falso, pois não existe gravador de voz MTA ativo.

Próxima sequência para voz:

1. Integrar Speex e verificar frames conhecidos de 8/16/32 kHz contra o desktop.
2. Receber e validar pacotes 71 por jogador, com limites de tamanho e limpeza
   de decoder/filas na saída do jogador e na desconexão.
3. Ligar PCM decodificado a AudioTrack, com volume/pausa e eventos de voz.
4. Integrar AudioRecord, permissão de microfone e push-to-talk; depois portar
   envio/encerramento na mesma ordem e canal do desktop.
5. Implementar as sobrecargas Lua de som que recebem jogador, sem substituir
   ausência de áudio por valores de sucesso.

Essas etapas exigem validação com servidor e aparelho e permanecem pendentes.

## Projéteis: dependência do motor

`CLuaProjectileDefs.cpp:51-265` foi conferido. `createProjectile` depende de
criador válido, recurso, modelo, tipo de arma e objeto físico no motor.
As consultas `getProjectileType`, `getProjectileCreator`, `getProjectileTarget`,
`getProjectileForce`, `getProjectileCounter` e `setProjectileCounter` dependem
desse objeto e de seu ciclo de vida.

O mobile ainda não possui um registro de projéteis integrado ao GTA. Registrar
apenas retornos falsos ou criar tabelas Lua sem o projétil físico não resolve
essas funções. O próximo lote deve começar por criação/destruição no game
thread, modelo e física, com teste no aparelho; depois expor as consultas.
Explosão, temporizador e sincronização exigem validação própria.

## Validação realizada

- `neon_resource_tests`: **206 verificações, zero falhas**, incluindo campos e
  alinhamento de armas, corpo truncado, múltiplas entidades no mesmo pacote,
  saída/reutilização de ID, duração assíncrona e limpeza de áudio por recurso.
- Compilação Linux: `cmake --build tools/headless/build --target neon_resource_tests -j 4`.
- Compilação nativa ARM64/NDK: alvo `WorldRPGNeon` compilado e linkado com sucesso
  usando o diretório Android já configurado em `app/.cxx/Debug/2l1o3s5b/arm64-v8a`.
- As seis classes Java de `game/` compilaram com `javac`, Android API 34 e
  AndroidSVG 1.4; arquivos temporários ficaram fora do repositório.
- `:app:assembleDebug` foi tentado. O Gradle não conseguiu conectar ao daemon;
  o daemon registrou `java.io.IOException: Invalid argument` em `Net.accept`.
  Isso também ocorreu com Java 17 e IPv4. **Não foi gerado um APK atualizado.**
- Sem teste no aparelho, sem teste de voz com servidor, sem commit/push.

Listas completas atualizadas: [áudio/voz](lua-functions/AUDIO_AND_VOICE.md) e
[armas/projéteis](lua-functions/WEAPONS_AND_COMBAT.md).
