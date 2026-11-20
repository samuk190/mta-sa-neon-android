# Revisão de elementos, GUI e DX

Revisão em 2026-11-15, branch `staging`. Escopo: os lotes de elementos,
GUI e DX deste chat. O commit inclui a base de DX/SVG necessária para essas
funções. As alterações anteriores de motor, rede e launcher permanecem locais.

## Problemas encontrados e corrigidos

| Prioridade | Problema | Correção e evidência |
|---|---|---|
| P2 | `dxDrawRectangle` e `dxDrawText` retornavam zero valores, quebrando scripts que verificam sucesso | Retornam booleano; entradas inválidas retornam `false`. Asserções Lua cobrem sucesso, tipo inválido, infinito e flag inválida. |
| P2 | Texto, retângulos e imagens ignoravam `postGUI`; a fonte de um texto adiado podia ficar fora da sua camada | As oito funções 2D compartilham o enfileiramento. Blend é capturado na chamada, e seleção/reset de fonte seguem o texto. Testes verificam as duas camadas, fonte personalizada e desenhos em render targets. |
| P2 | Uma textura existente era recusada por `dxDrawImage` se o chamador não tivesse dono de recurso | Elementos de textura são resolvidos independentemente do dono; caminhos continuam vinculados a um recurso. O teste desenha um render target fora de um recurso. |
| P2 | As consultas de elementos novas recusavam texturas, fontes e SVG válidos | `KnownElement` reconhece `DxElementType`, que também cobre SVG. Asserções verificam consultas de anexação em um render target vivo. |
| P2 | `attachElements` e `detachElements` informavam sucesso sem alterar corpo ou metadados | Os stubs retornam `false`. Asserções comprovam que não prometem uma mutação que o mobile ainda não executa. A anexação física continua pendente. |
| P2 | Erros de argumento das primitivas chamavam `luaL_error` com buffers C++ vivos; o `longjmp` podia pular seus destruidores | A construção dos comandos retorna a um handler sem `vector`/`string` vivos antes de levantar o erro. Leitura de tabelas ignora strings, como `ReadNumberTable` desktop. Asserções cobrem erro depois de um vértice válido e retomada pelo Lua; a liberação foi conferida por inspeção do fluxo, sem medição de heap em aparelho. |
| P2 | Uma exceção de Canvas ao desenhar primitivas com material deixava o bitmap shader no Paint compartilhado | `try/finally` limpa o shader. O Java compila; injeção dessa falha e validação visual ainda dependem do Android. |
| P2 | Memo não participava de foco/teclado; Enter e cursor não seguiam o controle multilinha | Memo aceita Unicode, Enter e backspace, preserva o newline final de CEGUI e emite Changed sem Accepted para Enter. Índices negativos seguem conversão unsigned e clamp. Testes cobrem somente leitura, cursor, CRLF, linhas e ocultação. |
| P2 | Normalizar CRLF apenas no código nativo deixava o buffer do teclado com contagem diferente; Enter enviado diretamente também não atualizava esse buffer | O Android normaliza o texto antes de calcular diferenças e inclui Enter no buffer observado. Esse caminho foi revisado e compilado; interação real com IMEs ainda precisa de aparelho. |

Os pontos da primeira etapa também foram conferidos: referências futuras de
anexação, remoção/reuso de ID, rotação em graus, conversão UTF-8/UTF-16 e
isolamento do blend da GUI. Não foram adicionadas escritas físicas no motor.

Na preparação para publicação, a validação SVG passou a descartar os nós XML
temporários e recusar dimensões não finitas ou que virariam zero. O teste verifica
que esses nós não permanecem acessíveis pelos handles XML.

## Continuação: layout de texto DX

O desenho DX deixou de usar o formato de texto da GUI, que centralizava todas
as chamadas e limitava a saída a três linhas. O comando separado preserva
newlines/backslashes e carrega alinhamento, clipping, quebra, escala por eixo,
subpixel, rotação/pivô e altura positiva de linha em texto colorido. Conforme o
caminho desktop, texto colorido não usa clip/wordBreak. Fontes personalizadas
continuam selecionadas e restauradas junto da chamada, inclusive após a GUI.

As verificações conferem a ordem dos argumentos quando há duas escalas, os
valores padrão do pivô, flags, texto escapado, fallback de fonte desconhecida,
retornos inválidos e posicionamento de texto maior que a caixa. Canvas ainda
tem métricas próprias; escala não positiva não desenha e a largura do layout
é limitada a 32768 pixels.

## Correções da revisão posterior

Os cinco problemas reportados depois dos commits `8acae8f` e `f872736` foram
corrigidos neste lote:

1. **Destruição e quadros substituídos:** `DxFrameQueue` retém desenhos de
   targets e comandos de remoção na ordem original. Remoções nunca são
   descartadas pelo orçamento de desenho, e comandos pendentes mantêm a view
   visível mesmo quando o quadro mais novo está vazio.
2. **Estado de desenho:** trechos adiados capturam blend e fonte na entrada.
   O quadro de tela seguinte restaura o estado inicial antes de seus comandos.
3. **Composição add:** Canvas usa source-over premultiplicado em vez de soma
   saturada, seguindo `CGraphics.cpp:437-527`; overwrite continua com SRC.
4. **Fontes e medidas:** a identidade nativa da criação faz parte da origem de
   fonte e da chave de medidas. Recriar no mesmo caminho recarrega o conteúdo.
   O cache Java tem limite LRU, e texto antigo em fila mantém sua própria versão.
5. **Altura de linha:** texto colorido com altura positiva usa bloco de
   `número de linhas × lineHeight` e avanços explícitos, incluindo a última
   linha, como `CGraphics.cpp:1310-1333`.

As regressões de view compilam os métodos reais contra o SDK e depois os
executam com doubles Android. Elas conferem a fila, bitmap ownership,
composição opaca, recarga de fontes e posição das linhas; não substituem
a validação de GPU, rasterização de fontes ou SVG em um aparelho.

## Continuação: fontes GUI e labels

- `guiCreateFont` deixou de falhar incondicionalmente. Cria `gui-font` com
  arquivo resolvido no recurso, clamp unsigned de 5–150 pontos e conversão a
  pixels por 96 DPI. No Android, o Typeface é carregado e validado na criação;
  o host usa uma assinatura sfnt sintética para testar o ciclo de vida.
- `guiSetFont` distingue fontes GUI e DX, aceita nomes presentes e preserva a
  fonte anterior diante de argumento inválido. `guiGetFont` retorna nome e
  elemento, como `CLuaGUIDefs.cpp:1359-1393,1730-1755`.
- Fontes mantêm o dono que as criou, mesmo com arquivo de outro recurso.
  Destruição e stop restauram `default-normal` nos controles usuários,
  incluindo controles de outros recursos (`CClientGuiFont::Unlink`).
- Labels usam um comando próprio `Y`: texto literal escapado, linhas sem
  limite artificial de três, alinhamento, quebra opcional e clipping.
  As medidas usam a mesma fonte/escala da view e retornam pixels, em vez de
  escala normalizada e estimativa fixa por quantidade de caracteres.
- A revisão conferiu retorno de erro, separação GUI/DX, reset por controle,
  título de tab com fonte própria, usuários por nome/elemento e a ordem de
  destruição entre recursos. Esses casos têm regressões no host/JVM.

Os nomes nativos de GUI usam faces do sistema Android. A aparência CEGUI,
geometria de skins e controles com fontes grandes ainda precisam de comparação
no aparelho. A quebra é feita pelo Android; doubles não verificam glifos/GPU.
`guiLabelGetFontHeight` ainda usa o espaçamento de linha Android como aproximação:
CEGUI retorna a altura dos glifos no cache (`CEGUIFont.cpp:1031-1037`), que não é
necessariamente o avanço entre linhas. Essa diferença continua pendente.

## Code review antes do commit de fontes GUI

| Prioridade | Falha confirmada | Correção e regressão |
|---|---|---|
| P2 | O LRU de 64 entradas podia expulsar uma fonte GUI ainda viva; uma alteração no arquivo passava a afetar o mesmo elemento | A face validada fica retida enquanto o elemento vive. Destruição/stop/reset emitem `D`, consumido depois dos desenhos anteriores. Testes usam pressão maior que 64 versões, substituição do arquivo e liberação com quadro substituído. |
| P2 | `T` dos edits/botões não recortava texto grande pela caixa do controle | O renderer aplica `clipRect` e protege `save/restore` com `finally`; a largura usa o limite comum de layout. O teste verifica o recorte e um desenho DX posterior sem deslocamento herdado. |
| P2 | `dxGetFontHeight`, `dxGetTextWidth` e `dxGetTextSize` recebiam `gui-font` e mediam a fonte padrão sem acusar o tipo | Altura/largura retornam `false`; tamanho gera o erro do argumento tipado, inclusive para texto vazio. A validação antecede strings C++ para não atravessar seus destruidores com `longjmp`. |

As três regressões foram reproduzidas antes da correção. Depois dos ajustes,
os testes passaram. A revisão final conferiu os caminhos de criação, desenho,
destruição, stop de recurso e reset, além dos retornos e da ponte JNI.

## Validação realizada

- `neon_resource_tests`: **274 verificações, zero falhas**.
- `neon_dx_text_tests`: **24 verificações Java, zero falhas**.
- `neon_dx_state_tests`: **29 verificações Java, zero falhas**.
- `neon_dx_view_tests`: **34 verificações Java, zero falhas**, com doubles Android.
- `WorldRPGNeon` ARM64: compilou e linkou com o NDK.
- Classes Java de jogo: compilaram; somente avisos de APIs obsoletas e
  bootstrap class path para Java 8.
- Catálogo de Lua regenerado: 651 handlers não constantes, 136 stubs e
  794 ausentes. Este lote amplia comportamento de funções existentes e corrige
  **3/3 problemas desta revisão**. As **5/5 correções DX anteriores** permanecem.
  A cobertura geral continua em **41,2%**, e GUI/DX em **87,9% — 175/199**.
  Os 24 bindings ausentes e os comportamentos parciais impedem marcar 100%.
- `git diff --check`: sem erros.

Não houve teste no aparelho nem geração de APK novo. A compilação nativa usa
a árvore completa com alterações anteriores do motor. A limitação do checkout
isolado está em [REVIEW_LUA_MOBILE.md](REVIEW_LUA_MOBILE.md).

## Pendências preservadas

- Anexação física e RPCs de anexação; consultas espaciais, bounding box/raio
  e transformações completas de elementos.
- Seleção de texto, posicionamento do cursor pelo toque, propriedades/fonte
  CEGUI, ordenação real de gridlist e quebra/rolagem automática de memo.
- Comparação visual de fontes e métricas do texto Canvas com Direct3D. O layout
  agora aceita alinhamento, clipping, quebras, escalas por eixo e rotação.
- DX 3D com profundidade, shaders e efeitos de modelo 2DFX.
- Validação visual de transparência, gradientes, render targets, teclado e
  eventos de foco nos dispositivos Android de destino.

Detalhes funcionais: [roadmap de elementos, GUI e DX](ROADMAP_ELEMENTS_GUI_DX_MOBILE.md).
