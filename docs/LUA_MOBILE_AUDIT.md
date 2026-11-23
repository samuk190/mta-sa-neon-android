# Auditoria das funções Lua no MTA Mobile

Data: 2026-11-15. Branch: `staging`, incluindo as alterações locais que já estavam presentes.
Referência desktop: `/home/samuel/projects/world-rpg-br-en-eu/mta_neon_source`.

Este documento registra o primeiro lote. O avanço posterior e os limites de
áudio/voz e armas/projéteis estão em [segundo lote](ROADMAP_AUDIO_WEAPONS_MOBILE.md);
o catálogo gerado contém as contagens mais recentes.

## Resultado

O levantamento por código encontrou **1.581 funções Lua do cliente desktop**:

| Estado | Antes deste lote | Depois deste lote |
|---|---:|---:|
| Registro com implementação não constante | 631 | 633 |
| Stub / retorno provisório | 136 | 136 |
| Ausente | 814 | 812 |
| Pendentes (stub + ausente) | 950 | 948 |

Esses números medem registros Lua, não equivalência completa com o desktop.
Uma função contada como implementada pode ter somente parte do comportamento,
depender de um hook ainda incompleto ou não ter sido verificada no aparelho.
Os estados foram extraídos novamente com `tools/parity_matrix.py` e
`tools/generate_lua_categories.py`.

## Onde estão as funções que faltam

Cada link abaixo lista **os nomes individuais**, o estado e a referência desktop.
As colunas contam somente as funções pendentes depois deste lote.

| Área / lista completa | Ausentes | Stubs |
|---|---:|---:|
| [Veículos](lua-functions/VEHICLES.md) | 137 | 19 |
| [Peds e jogadores](lua-functions/PEDS_AND_PLAYERS.md) | 102 | 77 |
| [Mundo e objetos](lua-functions/WORLD_AND_OBJECTS.md) | 92 | 11 |
| [Elementos](lua-functions/ELEMENTS.md) | 23 | 8 |
| [GUI e DX](lua-functions/GUI_AND_DX.md) | 24 | 0 |
| [Motor e modelos](lua-functions/ENGINE_AND_MODELS.md) | 109 | 15 |
| [Áudio e voz](lua-functions/AUDIO_AND_VOICE.md) | 37 | 1 |
| [Câmera](lua-functions/CAMERA.md) | 0 | 0 |
| [Armas e projéteis](lua-functions/WEAPONS_AND_COMBAT.md) | 23 | 0 |
| [Sistema, recursos e utilitários](lua-functions/CORE_AND_UTILITY.md) | 24 | 2 |
| [Markers, blips e colisão espacial](lua-functions/MARKERS_AND_SPATIAL.md) | 142 | 3 |
| [Navegador e compatibilidade](lua-functions/BROWSER_AND_COMPATIBILITY.md) | 99 | 0 |
| **Total** | **812** | **136** |

## Primeiro lote: consultas sem alteração do motor

### `getValidPedModels([includeCustom = true])`

Implementada em `app/src/main/cpp/shared/LuaPedApi.cpp`.
Retorna uma tabela nova, indexada a partir de 1 e ordenada por ID, com os 310
modelos stock de 0 a 312, excluindo 74, 149 e 208. Não depende de haver um ped
carregado no mundo. O argumento opcional não altera a lista enquanto não existir
alocação de modelos customizados no port.

Referências lidas: `Client/mods/deathmatch/logic/lua/CLuaFunctionDefs.Util.cpp:13-54`,
`Client/mods/deathmatch/logic/CClientPlayerManager.cpp:179-202`,
`Client/game_sa/CModelInfoSA.cpp:354-357` e
`Server/mods/deathmatch/logic/CPlayerManager.cpp:338-352`.
A lista stock usa a regra explícita do servidor, pois a consulta dinâmica de
`CModelInfo` do cliente desktop não está disponível no host.

**Limite:** `engineRequestModel` continua sendo stub. Quando a alocação customizada
for portada, será necessário consultar o registro de modelos, inclusive slots
substituídos abaixo de 313, respeitando `includeCustom=false`. A existência e o
carregamento de cada skin na instalação Android não foram verificados neste lote.

### `getBoundKeys(commandOrControl)`

Implementada em `app/src/main/cpp/shared/LuaKeyBindApi.cpp`.
Consulta os comandos registrados em `bindKey` por todos os recursos e retorna
`{ tecla = "down" ou "up" }`; retorna `false` quando não há associação.
O nome do comando é comparado sem distinguir maiúsculas de minúsculas.
Associações posteriores sobrescrevem o estado anterior da mesma tecla;
`both` pode, portanto, aparecer como `up`, como no percurso desktop.
Os vínculos de um recurso parado deixam de aparecer.

Referências lidas: `Client/mods/deathmatch/logic/lua/CLuaFunctionDefs.Input.cpp:363-418`,
`Client/core/CKeyBinds.cpp:1030-1046,1388-1405,1747-1761`.

**Limite:** controles GTA têm precedência sobre comandos de mesmo nome.
O mobile ainda não possui mapeamentos de teclas físicas para esses controles;
a consulta de um controle retorna `false`. Callbacks Lua registrados para
`fire` etc. não são mapeamentos de tecla física. Os botões de toque não são
inventados como teclas de teclado.

## Próximos lotes

| Prioridade | Funções / família | Dependência a resolver antes de portar |
|---|---|---|
| Avaliar com testes locais | `getElementsWithinRange` | Conferir busca espacial, interior, dimensão e tipos contra o desktop; não exige escrever no motor. |
| Avaliar com testes locais | `getResourceConfig` | O desktop procura configs declaradas no recurso e retorna o nó XML já carregado. Não basta chamar `xmlLoadFile` para qualquer arquivo. |
| Avaliar com testes locais | `load`, `call` | Preservar carregamento de scripts, contexto do recurso e resultados entre recursos; o port usa uma única VM. Não tratar o `load` padrão do Lua 5.3 como equivalente ao MTA. |
| Exige integração Android | `isMTAWindowFocused`, `clearDebugBox`, `createTrayNotification` | Estado real da Activity, saída de debug e notificações, respectivamente. |
| Exige testes de entrada no aparelho | `getAnalogControlState`, `setAnalogControlState`, `toggleControl`, `toggleAllControls` | Controles de toque e estado efetivo aplicado ao jogo; guardar flags não garante funcionamento. |
| Exige motor/aparelho | `setPedVoice`, `setPedWalkingStyle`, `fixVehicle`, `getVehicleHandling`, `createWeapon` | Hooks GTA, estado nativo e ciclo de vida dos elementos. |
| Exige renderização/aparelho | `dxDrawLine3D`, `dxCreateShader`, `engineLoadDFF`, `engineReplaceModel` | Pipeline gráfico, formatos de modelos e diferenças D3D/OpenGL. |
| Exige protocolo/servidor | `triggerLatentServerEvent`, `getLatentEventStatus`, `cancelLatentEvent`, `downloadFile` | Transferências, cancelamento, progresso e ciclo de vida dos recursos. |

Essas prioridades são uma triagem para os próximos lotes, não implementações
concluídas. O primeiro lote foi limitado às duas consultas acima para evitar
alterações de física, sincronização ou renderização sem validação no aparelho.

## Validação

- Antes: compilação local e `neon_resource_tests` passaram com 161 verificações.
- Depois: a mesma suíte passou com **164 verificações e zero falhas**. Os blocos
  Lua novos incluem múltiplas asserções sobre modelos, argumentos, teclas,
  maiúsculas/minúsculas, `both`, remoção de vínculos e parada de recursos.
- Compilação usada: `cmake --build tools/headless/build --target neon_resource_tests -j 4`.
- Execução: `tools/headless/build/neon_resource_tests`.
- Não houve geração de APK, teste no aparelho ou teste contra servidor remoto.
- As alterações locais anteriores foram preservadas; não houve commit ou push.
