# Revisão: consultas de peds e armamento no mobile

Branch `staging`, 2026-11-16. Este lote converte cinco stubs em handlers que
consultam o corpo GTA ou o estado do elemento preservado fora de streaming.
O catálogo passa a **682/1.581 (43,1%)**, com **129 stubs** e **770 ausentes**.
Peds/jogadores passam a **32/206 (15,5%)**, com 72 stubs e 102 ausentes.
São números de handlers; não representam paridade completa com o desktop.

| Função | Referência desktop em `Client/mods/deathmatch/logic` | Comportamento |
|---|---|---|
| `isPedDead` | `luadefs/CLuaPedDefs.cpp:2168-2198`; `CClientPed.cpp:3774-3804` | Estado morto, tarefas de morte/afogamento e janela de morte do jogador na rede. Preserva a proteção contra flag antiga após revival. Argumento inválido retorna `nil`. |
| `getPedWeapon` | `luadefs/CLuaPedDefs.cpp:685-709`; `CClientPed.cpp:4174-4189` | Arma do slot atual ou explícito. Slot fora do inventário retorna 0; argumento inválido retorna `false`. |
| `getPedWeaponSlot` | `luadefs/CLuaPedDefs.cpp:711-731`; `CClientPed.cpp:4137-4144` | Slot atual do corpo; fora de streaming, slot do cliente. |
| `getPedAmmoInClip` | `luadefs/CLuaPedDefs.cpp:733-761` | Munição real do pente convertida para `uint16`. Sem corpo carregado, retorna `false`. |
| `getPedTotalAmmo` | `luadefs/CLuaPedDefs.cpp:763-807`; `CWeaponNames.h:20-35` | Munição real ou cache não zero do ped. Retorna 1 nos slots sem munição; preserva a conversão para `uint16`. |

## Backend e ciclo de vida

- Uma consulta coleta o inventário e a tarefa no thread do jogo. O ponteiro de
  `CPed` não atravessa esse thread. Timeout ou layout incompatível retornam falha.
- `ENTITY_ADD` inicializa os dados do ped; `PLAYER_LIST` preserva tipos de arma
  antes descartados. Os RPCs de arma seguem a ordem recebida.
- Com corpo, o RPC executa a operação nativa antes de atualizar o cache.
  Sem corpo, atualiza o estado do cliente; `_CreateModel` restaura esse estado.
  Reproduzir os mesmos RPCs sobre um inventário nativo antigo altera a munição
  na troca de arma, por isso esse caminho foi corrigido na revisão.
- Stream-out por dimensão, capacidade ou troca de modelo salva munição nativa
  das armas conhecidas do ped. O cache de munição exclusivo de peds não é
  inventado para jogadores; também não é fabricado um pente fora de streaming.
- Spawn limpa armas, morte e a flag de rede. `SetHealth` positivo pode limpar
  o estado morto; `LockHealth` só armazena a vida, conforme o desktop.
- Remoção, quit e nova sessão descartam os dados. Gerações impedem um corpo ou
  RPC antigo de alcançar um ID reutilizado. A exclusão adiada do corpo preserva
  RPCs já recebidos para a nova geração.
- A fila retira a operação antes de executá-la. Assim, uma nova chegada não
  remove a operação em execução nem faz seu término consumir a próxima.
- O envio das armas de `PLAYER_LIST` ocorre depois de soltar o lock do mundo,
  evitando introduzir uma inversão entre os locks do mundo e do motor.

## Evidência nativa e verificações

Biblioteca local: libGTASA 2.10 ARM64, usada apenas para leitura/compilação;
o APK de teste não inclui arquivos do jogo.

| Campo | Evidência no binário |
|---|---|
| Slot `CPed+0x8dc`; inventário `+0x730`, passo `0x20` | `CPed::SetCurrentWeapon`, loads em `0x59b7cc/0x59b7d4`, store em `0x59b81c`. |
| Tipo, pente `+8`, total `+0xc` de `CWeapon` | `CWeapon::Initialise` em `0x700b38/0x700b40/0x700b80`; `Reload` em `0x700a3c/0x700a7c`. |
| Vida `CPed+0x6ac` | Campo já lido pelo port, verificado em `CWidgetPlayerInfo::RenderHealthBar` (`0x37d770`). Nenhum novo store de vida/armadura foi adicionado. |
| Task manager em intelligence `+8`, primary `[2]`; GetTaskType no vtable `+0x28` | `CPedIntelligence` ctor em `0x5bc528`; `CTaskManager::SetTask` em `0x64e098/0x64e0c0`; acessores de morte retornam 212–218, como no desktop. |

O leitor recusa o layout se as quatro instruções de slot/tipo/total/pente não
coincidirem. `tools/headless/verify_ped_query_native_abi.py` verifica mais
instruções e os sete IDs de tarefa: **24 verificações**, sem executar GTA.

Testes do lote: retornos Lua, argumentos inválidos, slots explícitos/atuais,
estreitamento de munição, slots sem munição, tarefas 211–219, morte/revival,
RPCs de dar/tirar arma, clamp de 9999, overflow do cache `uint16`, troca de
arma fora de streaming, stream-out, remoção/reuso de ID, quit e reset.
O snapshot nativo do teste host é opt-in; não substitui o leitor Android nem
comprova o comportamento do corpo GTA.

Resultado final: **56 verificações do lote**, também com ASan/UBSan e detecção
de conversão numérica fora de faixa; **274 de regressão**, sem falhas. O APK
debug ARM64 sem arquivos do jogo compila e passa na auditoria de empacotamento,
com LOADs alinhados a 16 KB.

## Limites e validação pendente

Não há dispositivo ADB conectado. É necessário verificar no aparelho jogador
local, jogador remoto e NPC, disparo/recarga, troca de arma pela interface,
stream-out/in por dimensão e distância/capacidade, morte/respawn e reuso de ID.

Os caminhos anteriores de sync de arma/munição, morte do jogador local e
recriação nativa após revival ainda têm lacunas registradas no port. Estes
getters expõem o estado realmente disponível; não completam esses caminhos.
`getPedStat`, usado pelo diagnóstico de NPCs, não foi convertido neste lote.

As operações nativas continuam assíncronas: uma consulta pode observar o corpo
anterior ao próximo pulse ou durante o carregamento do modelo de arma. A fila
existente mantém orçamento de oito operações por pulse e limite de 32 na entrada
de RPCs; saturação continua sendo registrada e pode descartar a mais antiga.
Compilação e testes host não validam esses limites nem a física no aparelho.

O próximo lote selecionado é proximidade/colshapes, começando por
`getElementsWithinRange`, conforme [a fila de conversão](NEXT_LUA_CONVERSIONS.md).
