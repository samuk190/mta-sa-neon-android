# Elementos, GUI e DX no mobile

Revisão em 2026-11-18 na branch `staging`.

## Element tree (2026-11-18)

Catalog: **81 of 81 implemented (100%).** None of these names are still missing or constant stubs. The Lua catalog in `README.md` is **1526 / 1,581 (96.5%)**. The binding scan in `docs/PARITY_MATRIX.md` counts `CLuaElementDefs` as **81 / 81** as well.

What the new handlers store, and what they do not call:

| Area | What runs | What does not |
|---|---|---|
| Velocity | `setElementVelocity` and `setElementAngularVelocity` store the vector and walk children while call propagation is on. A frozen vehicle keeps the stored vector; move speed reads as zero until it thaws. | `CPhysical::SetMoveSpeed` and `SetTurnSpeed` are not called. A projectile accepts the write and stores nothing. |
| Bounds | Ped, player, vehicle, object, weapon and building take the desktop type check. Distance from centre of mass to the base of the model returns 0 for those types. | `CModelInfo::GetBoundingBox` is not loaded, so the box and the radius stay false. |
| Marker collision | `createMarker` creates the circle, sphere or tube. `getElementColShape` returns it. `isElementWithinMarker` reads the cached collider list. Script `destroyElement` cannot remove that collision. | A pickup sphere is created only when `CPickup` streams in, so `getElementColShape` stays false. |
| Matrix and offsets | A 4×4 table or 12 numbers are stored, and the position row is passed to `setElementPosition`. Attachment offsets are stored in degrees. | `CEntity::SetMatrix`, the Euler conversion and `CPhysical::SetAttachedOffsets` are not called. |
| Low LOD | Object links follow `SetLowLodObject`. Building links follow `SetLowLodBuilding`, including a clear that stays true. | `CBuilding::SetLod` is not called. The ENTITY_ADD low-LOD id is not stored. |
| Fire and leases | A local ped or the local player stores the fire flag. Streamable elements receive a lease token. | `CPed::SetOnFire`, `CVehicle::SetOnFire`, `CObject::SetOnFire` and streamer force-in / force-out are not called. |

Host check: `neon_spatial_tests` 92 checks and `neon_resource_tests` 446 checks, 0 failures. Not run on a device.

## Web browser and compatibility (2026-11-18)

Catalog: **103 of 103 implemented (100%).** None of these names are still missing or constant stubs. Overall Lua catalog **821 / 1,581 (51.9%)**. Elements **62 / 81**. Peds **51 / 206**.

What the handlers actually do:

| Area | What runs | What does not |
|---|---|---|
| Browser | Size errors for `createBrowser` / `guiCreateBrowser`. `getBrowserSettings` returns RemoteEnabled, RemoteJavascript and PluginsEnabled, all false. Domain check returns nil. | No WebView and no CEF. A valid `createBrowser` returns false. A local `guiCreateBrowser` raises the desktop create-failure error. Pages are not drawn. |
| Discord | Length, party-size and button-URL checks raise the desktop errors. | No Discord client, so a valid call returns false and the user id is empty. |
| Ped compatibility | Jetpack from ENTITY_ADD. Fire and water from PED_SYNC. Duck, choke and aim origin from keysync, stored beside the weapon snapshot. Weapon slot, knock-off-bike and control state are stored and read back. Ground uses `FindGroundZFor3DPosition` when that call returns a height. `setPedRotation` writes a script ped's heading. | `CTaskManager`, line-of-sight, `CPed::SetCurrentRotation` for anyone but a script ped, and `CPed::SetCanBeKnockedOffBike` / `CPad` are not called. Valid stat ids return 0. `attachElements` and `detachElements` return false and do not change the server attachment. |
| `setElementModel` | Stock ped ids 0–312 except 74, 149 and 208, and vehicle ids 400–611, are stored. The same id returns false. | The GTA body is not rebuilt. `onClientElementModelChange` is not fired. Object models return false. |

Host check: `neon_resource_tests`, 305 checks, 0 failures, including the alias, fire, skin, browser-size and Discord-length cases. Not run on a device.

The attack button is no longer copied into the remote ped duck flag.

## Progresso das funções Lua

**Geral: 821 de 1.581 funções com handler não constante — 51,9%.**
Elementos: **62/81 — 76,5%**. GUI/DX: **199/199 — 100% dos bindings**.
Browser e compatibilidade: **103/103 — 100% dos bindings**. Isso não é CEF nem o task manager.

As 24 funções antes ausentes agora têm handlers e caminhos de execução.
**Isso não significa 100% de compatibilidade com o desktop.** Os três novos
backends são parciais e ainda não foram executados no aparelho. O catálogo
mede presença de código; não mede equivalência visual, desempenho ou cobertura
completa de argumentos, tipos e estados do motor.

| Lote das 24 funções | Caminho implementado | Pendências de compatibilidade |
|---|---|---|
| 7 de desenho 3D | Câmera principal e chamadas RenderWare nativas, profundidade da cena, primitivas coloridas/texturizadas, linhas em billboard, esfera por octaedro subdividido e instâncias de modelos atomic/clump carregados | Só `postfx`; `prefx`/`postgui`, material shader, iluminação de modelo diferente de zero e ordenação `DRAW_LAST` não estão portados. Chamadas sem backend/modelo válido retornam falha. Aparência, ciclo de câmera e desempenho precisam de aparelho. |
| 4 de shader | Subconjunto FX/HLSL de uma passagem traduzido para GLSL ES 100, compilação/link GLES reais em contexto EGL isolado e saída usada por `dxDrawImage`/`dxDrawImageSection`; parâmetros numéricos/bool/textura, valores iniciais constantes, tesselação de quad e transformação | Sem includes, condicionais do preprocessador, múltiplas passagens, aplicação no mundo/3D e estados não suportados. Estes casos falham; não geram sucesso simulado. O modo `overwrite` é recusado por depender de máscara de cobertura. Texturas são snapshots de pixels. Leitura completa da imagem e cópia para Canvas precisam de medição no Android. |
| 13 de modelo 2DFX | Parsing/retornos Lua, pilhas de propriedades e remoções por recurso, índices customizados compactados, publicação transacional e limpeza com repetição no game thread. Getter e contador nativos verificados em ARM64 | Mutação nativa só de `LIGHT` e `SUN_GLARE`. Consultas de partícula e escada leem os campos disponíveis; payload de placa não é exposto. Partículas, placas e escadas precisam de limpeza/recriação dos corpos persistentes e restream antes de liberar mutação. |

Os símbolos e instruções usados pelo novo código nativo são verificados por
`tools/headless/verify_dx_native_abi.py`; **65 verificações passaram** no binário
GTA 2.10 ARM64 local. Isso confere o ABI esperado, sem executar os hooks.
A dependência de tradução HLSL é MIT e está fixada por revisão em
`app/src/main/cpp/third_party/hlslparser/UPSTREAM.md`.

## Validação do lote das 24

- `neon_dx_conversion_tests`: **8.264 verificações passaram**, incluindo shutdown/finalizadores, face-toward sem estágio, orçamento de macros e estado
  2DFX/rollback/recursos, geometria e fila 3D, Lua real e pixels GLES. Muitas
  verificações são de vértices/arestas da esfera, não cenários independentes.
- Os pixels foram renderizados pelo Mesa/D3D12 no host (Intel UHD Graphics):
  cores, textura, valores iniciais, troca de uniformes, shader de vértice,
  tesselação que altera a curvatura, rotação, fallback de técnica e preservação
  do contexto/superfícies/viewport do chamador. Não é execução no Android.
- Regressões anteriores: **274 verificações nativas**, **24 de texto Java**,
  **29 de estado Java** e **34 da view Java**, sem falhas.
- A biblioteca Android ARM64 compila e linka com o NDK; o APK debug sem arquivos do jogo também foi gerado.
- A [revisão deste lote](REVIEW_GUI_DX_24.md) registra correções e limites abertos.
- Teste dos novos caminhos no aparelho: **pendente; ADB sem dispositivo**.

O [lote de consultas de peds e armas](REVIEW_PED_WEAPON_QUERIES.md) adicionou
`isPedDead`, `getPedWeapon`, `getPedWeaponSlot`, `getPedTotalAmmo` e
`getPedAmmoInClip`, com leitura nativa no thread do jogo e cache por elemento.
Essas cinco funções saíram dos stubs; validação no aparelho continua pendente.
O [próximo lote](NEXT_LUA_CONVERSIONS.md) é proximidade/colshapes; depois, streaming.

## Implementado nos lotes anteriores

| Área | Alteração | Limite atual |
|---|---|---|
| Fontes GUI | `guiCreateFont` cria elementos `gui-font`, resolve arquivos de recursos, valida/carrega o Typeface no Android e aplica o clamp de 5–150 pontos a 96 DPI. A face fica retida até a remoção ordenada do elemento | No host, apenas a assinatura do arquivo é conferida; fontes nativas nomeadas usam faces do sistema Android. O LRU limita versões não retidas; fontes GUI vivas seguem o tempo de vida dos elementos. |
| Fonte dos controles | `guiSetFont` aceita fonte GUI/nome válido; `guiGetFont` retorna nome e elemento; destruição/stop restaura `default-normal`, inclusive em outro recurso | Geometria dos skins CEGUI e aparência das fontes nativas ainda diferem. |
| Labels | Alinhamento horizontal/vertical, quebra opcional, clipping, texto literal multilinha e fonte própria | Quebra e métricas Canvas precisam de comparação no aparelho. |
| Medidas GUI | `guiLabelGetFontHeight` retorna pixels; `guiLabelGetTextExtent` mede a maior linha com a fonte atribuída | A altura usa o espaçamento de linha Android como aproximação; CEGUI mede a altura dos glifos em cache. O host estima medidas; não reproduz glifos Android. |
| Elementos | `isElementAttached`, `getElementAttachedTo`, `getAttachedElements` e `getElementAttachedOffsets` | Consultam os dados de `ENTITY_ADD`; não criam anexação física. |
| Referências | Resolvem elementos declarados depois no mesmo pacote e limpam vínculos na remoção | Atualizações posteriores por RPC ainda não são aplicadas. |
| GUI edit | Inserção e backspace na posição do cursor, contagem por caracteres Unicode, respeito a somente leitura | Seleção de texto e posicionamento pelo toque ainda precisam de implementação. |
| GUI memo | Foco, digitação Unicode, backspace, Enter e desenho por linha; newline final e cursor seguem CEGUI | Faltam seleção, quebra automática de linha e rolagem automática do cursor. |
| Limite de texto | `guiSetText` e `guiEditSetMaxLength` preservam caracteres completos; limite zero aceita texto vazio | Digitação mantém o limite adicional de 4096 bytes do mobile. |
| JNI | Conversão UTF-16/UTF-8 para entrada do teclado, medição e envio do frame DX | Outras interfaces JNI não foram revisadas neste lote. |
| DX 2D | `postGUI` nas oito funções de desenho 2D, com blend capturado e comandos de fonte junto do texto | O texto Canvas usa alinhamento, clipping, quebra, escalas por eixo e rotação; métricas ainda diferem do desktop. |
| Retornos DX | `dxDrawRectangle` e `dxDrawText` agora retornam `true`/`false` e recusam números não finitos | Registradas antes, essas funções retornavam zero valores Lua. |
| Texturas | `dxDrawImage`/`dxDrawImageSection` aceitam uma textura existente sem exigir dono de recurso | Caminhos de arquivo ainda precisam de recurso para resolução. |
| Composição da GUI | A GUI usa blend normal, independente do último modo DX do script | Desenhos depois da GUI restauram o modo capturado na chamada. |
| Render targets | Desenhos nesses destinos mantêm a sequência original, inclusive quando `postGUI=true` | Não representa equivalência completa com Direct3D. |
| Primitivas | Linhas interpolam as cores dos vértices; entradas não finitas e círculos que transbordam são recusados | Interpolação visual e composição ainda precisam de teste no aparelho. |

Os retornos dos quatro getters seguem `CLuaElementDefs.cpp:1138-1232,1281-1311`.
Os offsets de rotação são expostos em graus. Para um elemento válido sem
anexação, `isElementAttached` retorna `nil`, `getElementAttachedTo` retorna
`false` e `getElementAttachedOffsets` retorna seis valores, como no desktop.
Remover o elemento de destino não reconecta seus filhos se o ID for reutilizado.

O cursor inicial de um edit continua em zero, conforme CEGUI. Alterar seu índice
agora afeta a digitação e o backspace. O máximo padrão corresponde à capacidade
CEGUI exposta como `unsigned int`; um valor Lua negativo segue essa conversão.

## Validação dos lotes anteriores

- `neon_resource_tests`: **274 verificações, zero falhas**. Inclui referências
  futuras, ordem dos filhos, remoção/reuso de ID, offsets em graus, Unicode,
  digitação/backspace de emoji, texto somente leitura, limite zero, camadas DX,
  blend capturado, fonte personalizada, render targets, memo/Enter/CRLF,
  retornos booleanos, geometria inválida e opções de layout de texto. Também
  cobre fontes GUI, retornos de fonte, medidas, separação de fontes DX/GUI,
  uso por outro recurso e limpeza ao destruir/parar o dono.
- `neon_dx_text_tests`: **24 verificações Java, zero falhas** — texto e altura
  total de linha personalizada.
- `neon_dx_state_tests`: **29 verificações Java, zero falhas** — ordem da fila,
  destruição de targets, orçamento e cache de fontes, inclusive falhas de carga,
  retenção de fontes vivas e liberação por instância.
- `neon_dx_view_tests`: **34 verificações Java, zero falhas** — executam o
  renderer real com doubles Android para os problemas da revisão e os labels/fontes GUI.
  Esses testes não verificam glifos ou GPU reais.
- Biblioteca Android ARM64: compilou e linkou com o NDK.
- Classes Java do jogo: compilaram, incluindo o gradiente de linhas.
- `git diff --check`: sem erros de whitespace.

Esses lotes anteriores não tiveram teste visual no aparelho nem geração de APK novo.
A compilação nativa usa as alterações anteriores do motor presentes na árvore
de trabalho; a limitação do checkout isolado está em [REVIEW_LUA_MOBILE.md](REVIEW_LUA_MOBILE.md).

## Revisão de fontes GUI e labels — 3/3 corrigidos

| Prioridade | Problema reproduzido | Correção |
|---|---|---|
| P2 | Mais de 64 versões podiam expulsar uma fonte GUI viva; substituir o arquivo mudava sua aparência | Fontes GUI vivas ficam retidas; o comando `D` as libera depois dos desenhos anteriores. Instâncias liberadas voltam ao LRU. |
| P2 | Fonte grande de edit/botão desenhava fora do controle | Texto `T` recebe recorte da própria caixa e restaura o Canvas em `finally`. |
| P2 | Medidas DX aceitavam uma fonte GUI e mediam a fonte DX padrão silenciosamente | Altura/largura recusam o tipo; `dxGetTextSize` segue erro do argumento tipado, antes de criar strings C++. |

As regressões falharam com o código anterior e passaram após as correções.
O relatório detalhado está em [REVIEW_ELEMENTS_GUI_DX.md](REVIEW_ELEMENTS_GUI_DX.md).

## Revisão DX anterior — 5/5 corrigidos

| Problema | Estado | Evidência |
|---|---|---|
| Target destruído era recriado por quadro adiado | Corrigido | Remoção é executada depois dos desenhos anteriores e sobrevive a quadros substituídos. |
| Blend era perdido ao adiar render targets | Corrigido | Cada trecho preserva blend/fonte; a tela seguinte restaura o estado inicial. |
| `add` fazia soma saturada de cores | Corrigido | Composição source-over para a fonte premultiplicada; vermelho opaco sobre azul permanece vermelho. |
| Recriar fonte mantinha arquivo/medidas antigos | Corrigido | Identidade por criação em ambos os caches, com limite LRU no Java. |
| `lineHeight` não contava a última linha | Corrigido | Altura do bloco e avanço explícito de linhas seguem o desktop. |

## Próximas pendências

1. **Elementos:** portar `attachElements`, `detachElements` e
   `setElementAttachedOffsets` com chamadas físicas e RPCs, antes de considerar
   anexação compatível de ponta a ponta. Os dois primeiros ainda são stubs e
   agora retornam `false` porque não executam a operação física.
   Faltam também consultas espaciais, bounding box, raio e integração completa
   das transformações com o corpo nativo.
2. **GUI:** seleção, posicionamento do cursor pelo toque, propriedades CEGUI,
   quebra/rolagem automática de memo e ordenação efetiva
   de gridlist. Funções registradas podem ter
   comportamento parcial; o catálogo não comprova equivalência visual.
   Conferir faces nativas, geometria dos controles com fontes grandes e
   diferenças de quebra/métricas dos labels no aparelho.
3. **DX:** comparar métricas e quebras de texto no aparelho, ampliar fontes
   nativas e completar os limites de 3D, shader e modelo 2DFX descritos na tabela deste lote.
4. **Aparelho:** verificar cursor no meio do texto, acentos/emoji, composição
   com transparência, gradientes, GUI antes/depois do DX e desenho em texturas.

## Catálogo atualizado

A [revisão dos lotes](REVIEW_ELEMENTS_GUI_DX.md) registra problemas
corrigidos, evidências e limites de validação. Os lotes seguintes completam
comportamentos de funções já registradas; este lote adicionou os 24 bindings ausentes, com suporte parcial explicitado.

O layout DX usa o comando `X`, separado do comando `T` da GUI. Seu texto é
escapado antes de atravessar a ponte JNI; seleção de fonte e reset seguem na
mesma fila. O renderer Android usa `StaticLayout`, com largura máxima de 32768
pixels. Escalas não positivas não desenham. Os testes Java verificam a geometria
e o payload; não verificam os glifos ou a composição no aparelho.

| Categoria | Implementadas/total no catálogo | Cobertura | Stubs | Ausentes |
|---|---:|---:|---:|---:|
| Veículos | 9/165 | **5,5%** | 19 | 137 |
| Peds e jogadores | 32/206 | **15,5%** | 72 | 102 |
| Mundo e objetos | 124/227 | **54,6%** | 11 | 92 |
| Elementos | 54/81 | **66,7%** | 8 | 19 |
| GUI e DX | 199/199 | **100,0%** | 0 | 0 |
| Motor e modelos | 1/125 | **0,8%** | 15 | 109 |
| Áudio e voz | 21/57 | **36,8%** | 1 | 35 |
| Câmera | 47/47 | **100,0%** | 0 | 0 |
| Armas e projéteis | 20/31 | **64,5%** | 0 | 11 |
| Sistema e utilitários | 171/195 | **87,7%** | 0 | 24 |
| Markers, blips e colshapes | 0/145 | **0,0%** | 3 | 142 |
| Browser e compatibilidade | 4/103 | **3,9%** | 0 | 99 |
| **TOTAL** | **682/1581** | **43,1%** | **129** | **770** |

Os números incluem as alterações locais anteriores. “Implemented” no catálogo
significa apenas que há uma função C com corpo não constante; não significa
paridade completa. As restrições deste lote também foram registradas nas
[consultas de elementos](lua-functions/ELEMENTS.md) e na
[categoria GUI/DX](lua-functions/GUI_AND_DX.md).
