# Revisão do lote das 24 funções GUI/DX

Branch `staging`, 2026-11-15. Os 24 nomes antes ausentes receberam handlers.
A cobertura de bindings é 199/199; os novos backends ainda têm suporte parcial.

## Correções feitas durante a revisão

| Problema | Correção e evidência |
|---|---|
| Erro Lua podia saltar a destruição de objetos C++ na leitura de primitivas/modelos | Workers devolvem o objeto de erro; o handler externo chama `lua_error` depois que os objetos locais foram destruídos. Argumentos inválidos são exercitados no host. |
| Conversão de número Lua para inteiro podia transbordar na leitura de propriedades 2DFX | Valores finitos, limites e integralidade são verificados como `double` antes da conversão. |
| Publicação nativa malsucedida podia perder a camada anterior do recurso | Estado seguinte é preparado separado e publicado somente após commit; regressão de rollback verifica que o valor anterior continua ativo. |
| Timeout na limpeza de recurso podia deixar efeitos ativos e liberar a identidade do dono | A identidade fica retida e a limpeza é repetida por `GameTasks::Defer` no game thread. O host verifica remoção por dono e repetição da fila; execução nativa da limpeza continua pendente. |
| Texturas originais de luz eram recarregadas ao alterar outro efeito | Efeitos nativos inalterados preservam seus ponteiros e referências; clones são preparados apenas para alterações. |
| Instância de modelo herdava depth-write desligado das linhas | Desenho de modelo liga escrita de profundidade e restaura o estado ao terminar. A aparência requer aparelho. |
| Esferas recriavam a subdivisão e emitiam milhares de desenhos separados | Geometria unitária é guardada por número de iterações; os quads das arestas são agrupados. Materiais repetidos compartilham o raster durante o batch. Desempenho no aparelho não foi medido. |
| Geometria finita de entrada podia produzir infinito na esfera | Vértices finais são conferidos antes de entrar na fila nativa; caso de overflow tem regressão. |
| Shader podia alterar o contexto gráfico do chamador | Contexto EGL próprio, com restauração de contexto, API, superfícies e estado do chamador. Regressão usa um segundo contexto real. |
| Construtor de constante emitia `const float(...)`, inválido em GLSL | Correção mínima no tradutor importado, documentada em `UPSTREAM.md`; valor inicial do uniforme e expressão constante são conferidos por pixels. |
| Tamanho do viewport podia ser o da tela durante desenho em target | O bake do shader usa as dimensões do destino ativo. Bitmaps temporários entram na fila de liberação ordenada existente. |

## Revisão antes do commit

| Prioridade | Problema | Correção / evidência |
|---|---|---|
| P1 | `overwrite` aplicava `SRC` ao bitmap do viewport inteiro e podia apagar o target fora da geometria do shader | Chamadas de imagem shader em `overwrite` retornam `false` antes de alocar bitmap ou emitir desenho. O suporte correto depende de máscara de cobertura e continua pendente. Regressão percorre a VM, o renderer GLES e a ponte de bitmap do host; `blend` continua desenhando. |
| P2 | Valor inicial ou parâmetro de `gWorldViewProjection` substituía a matriz do destino | Parâmetros comuns são aplicados depois dos parâmetros customizados, como `CTileBatcher.cpp:217-222` e `CEffectParameters::ApplyCommonHandles`. O teste de pixels falhava antes e passou com a ordem corrigida. |
| P2 | Transformação finita de entrada podia gerar matriz/vértice infinito e retornar sucesso | Matrizes e vértices finais são conferidos antes do envio. Regressão de offset extremo falhava antes e agora retorna `false`. |
| P2 | Uniforme inteiro tinha precisão padrão diferente entre VS e PS; defaults também escapavam da validação de conversão | GLSL ES recebe precisão inteira igual nos dois estágios. Defaults e setters validam o intervalo antes do cast; inteiro que não cabe na representação usada é recusado com diagnóstico. Testes conferem link, pixels e rejeição do extremo. |
| P2 | Falha ao criar o pbuffer podia deixar um contexto incompleto guardado para as chamadas seguintes | Inicialização EGL publica estado somente após criar surface/context, libera a surface ao falhar e seleciona a API GLES em cada chamada. Injeção de falha verifica recuperação na segunda tentativa, inclusive sem suporte a contextos sem surface. |

As três regressões de projeção, overflow e `overwrite` foram executadas antes
das correções e falharam. A revisão também foi executada com ASan/UBSan; a
verificação de leaks do driver GLES fica desativada, sem desativar bounds/UB.
As [próximas conversões sugeridas](NEXT_LUA_CONVERSIONS.md) registram prioridade,
dependências e condições de conclusão.

## Revisão de ciclo de vida e argumentos

| Prioridade | Problema reproduzido | Correção / evidência |
|---|---|---|
| P1 | Encerrar a VM sem criar outra deixava shader, textura, SVG e batch 3D vivos; finalizadores Lua também podiam criar novos elementos durante o fechamento | `DxReset` centraliza a limpeza, invalida handles e limpa as filas nativas. SVG libera referências enquanto a registry existe. A limpeza é repetida após `lua_close` para recursos criados em `__gc`. Bitmaps seguem a liberação ordenada existente; 2DFX mantém repetição no game thread se necessário. Regressões de shutdown, SVG e finalizador falharam antes e passaram após a correção. |
| P2 | Material line/section consumia o primeiro número de face-toward como estágio e recusava uma chamada válida | O parser só consome boolean/enum de estágio reconhecido, como `ReadIfNextIsEnumString` no desktop; coordenadas seguintes permanecem intactas. Testes conferem ausência/presença de estágio, UV e as três coordenadas. |
| P2 | Expansão de macros podia ultrapassar o teto de 512 KiB aplicado somente ao arquivo original | Cada append de expansão verifica o orçamento antes de alocar/copy; excesso retorna diagnóstico antes de parse/compilação. Regressão de expansão encadeada falhava antes e agora verifica o limite. |

O [próximo lote](NEXT_LUA_CONVERSIONS.md) foi repriorizado a partir dos recursos
client/shared disponíveis. O novo auditor distingue escopo client/shared,
comentários, strings e fontes compiladas, e não modifica os recursos.

## Limites ainda abertos após a revisão

- 3D: somente `postfx`; faltam `prefx`/`postgui`, materiais shader, iluminação
  de modelo diferente de zero e ordenação `DRAW_LAST`. O callback de câmera,
  RenderWare e a criação/destruição de instâncias ainda precisam de execução
  real no Android. A biblioteca desativa o caminho se as instruções esperadas
  ou símbolos não coincidirem.
- 2DFX: mutação somente de luz e brilho solar. Partículas, placas e escadas
  persistentes ainda precisam dos hooks de limpeza, recriação e restream.
  Dados de placa indisponíveis não são inventados.
- Shader: subconjunto FX de uma passagem, sem includes/condicionais e estados
  fora do conjunto explicitamente aceito. O sampler usa linear/clamp; aplicação
  no mundo/3D não está integrada. Valores de textura são snapshots, sem update
  vivo de render targets. Dimensões seguem a primeira textura declarada; sem
  textura atribuída o backend usa 1×1. A cópia de imagem inteira
  para Canvas é transitória e precisa de medição no aparelho. O modo `overwrite`
  é recusado até existir máscara de cobertura da geometria.
- O host testa matemática, estado, integração Lua e pixels GLES. Os testes não
  reproduzem o ciclo de câmera, os recursos persistentes de GTA ou a composição
  final no Android. ADB estava sem dispositivo durante a revisão.

## Verificação

`neon_dx_conversion_tests`: 8.264 verificações; `neon_resource_tests`: 274;
Java: 24/29/34; ABI nativo: 65. Sem falhas. O renderer GLES do host foi
Mesa/D3D12, Intel UHD Graphics. Muitas das verificações novas são as arestas e
vértices das quatro subdivisões de esfera. Build ARM64 e APK debug sem arquivos
do jogo passaram. Não há aprovação de execução dos novos caminhos no aparelho.
