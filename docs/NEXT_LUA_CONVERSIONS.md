# Lotes Lua: consultas de peds e próximas conversões

Prioridade revista em 2026-11-18. O lote de browser e compatibilidade está no
catálogo: **103/103 implementadas, nenhum stub**
([roadmap](ROADMAP_ELEMENTS_GUI_DX_MOBILE.md)). Task manager, line-of-sight,
rotação nativa fora do ped de script, e attach/detach físico continuam sem
hook. Não há WebView nem cliente Discord. A validação no aparelho continua pendente.

Prioridade anterior, 2026-11-16, após inspecionar os scripts client/shared dos
recursos disponíveis em `backend_mta/mods/deathmatch/resources`.
A seleção considera chamadas reais no código, e não somente a quantidade de
funções ausentes no catálogo. As cinco consultas abaixo já têm handlers e
backend nativo/cache na `staging`; validação no aparelho permanece pendente.
Detalhes e limites: [revisão do lote](REVIEW_PED_WEAPON_QUERIES.md).

## Evidência de uso

| Função | Recursos / arquivos / chamadas estáticas, todos os recursos | Recursos / arquivos / chamadas, grupo WorldRPG |
|---|---|---|
| `isPedDead` | 15 / 19 / 37 | 5 / 6 / 18 |
| `getPedWeapon` | 9 / 10 / 20 | 2 / 3 / 8 |
| `getPedTotalAmmo` | 5 / 5 / 10 | 1 / 1 / 5 |
| `getPedWeaponSlot` | 3 / 3 / 4 | 1 / 1 / 1 |
| `getPedAmmoInClip` | 2 / 2 / 2 | 1 / 1 / 1 |

O recurso `native-ped-traffic/client.lua:1039-1054` usa arma e munição na
verificação de convergência do estado dos grupos de NPCs. Retornar zero
constantemente impede esse diagnóstico de representar o estado real.
A mesma verificação consulta `getPedStat`, que já possui handler; seu backend
também precisa ser conferido antes de considerar a validação de armas completa.

A auditoria leu **312 scripts declarados** client/shared no conjunto inteiro e
**38** no grupo WorldRPG. Encontrou **25** e **16** problemas de metadados/arquivos,
respectivamente; nenhum arquivo Lua compilado foi contado. Portanto, o recorte
é parcial. Comentários, strings, declarações de funções e scripts server foram
excluídos. Aliases, OOP, nomes locais que ocultam APIs e código gerado não são resolvidos. Os números medem
chamadas estáticas, sem confirmar recursos ativos ou frequência em execução.
A classificação usa o catálogo regenerado com a revisão GUI/DX e o lote
HUD/RPC commitado em `c1aaeda`; chamadas de controle já implementadas nesse
lote saíram da lista de stubs. As cinco consultas de ped também saíram dos
stubs após esta conversão.

## Lote implementado: consultas de peds e armamento

1. **Backend comum de consulta de ped:** separar jogador local, ped remoto e
   estado de cliente preservado quando o corpo não está carregado. Ler o corpo
   no game thread e aproveitar o estado dos RPCs de arma; não acessar um CPed
   livre nem fabricar zero quando faltam dados.
2. **`isPedDead`:** portar `IsDead() || IsDying()` e a janela de morte na rede,
   incluindo a recuperação após reviver. Saúde menor ou igual a zero, isoladamente,
   não reproduz a função desktop. Referência: `CLuaPedDefs.cpp:2168-2198`.
3. **`getPedWeaponSlot` e `getPedWeapon`:** slot atual e slot opcional, tipo
   de arma e retorno `false` para argumentos inválidos. Referências:
   `CLuaPedDefs.cpp:685-731`.
4. **`getPedTotalAmmo` e `getPedAmmoInClip`:** estoque por slot, munição no
   pente, slots sem munição e o cache para ped não carregado. Preservar as
   conversões `unsigned short` e a falha indicada pelo desktop. Referências:
   `CLuaPedDefs.cpp:733-807`.

No host foram verificados os retornos Lua, slots, morte/revival, RPCs de arma,
cache de stream-out, RPCs fora de streaming antes da restauração do corpo e reuso de ID.
O APK ARM64 sem arquivos do jogo compila. Corpo GTA, recarga, troca de arma
pela interface e streaming real precisam de aparelho; não há dispositivo ADB
conectado. O catálogo registra handlers, sem afirmar paridade completa.

## Próximo lote selecionado: proximidade e colshapes

Começar por `getElementsWithinRange`; em seguida preservar a geometria de
colshapes de `ENTITY_ADD` antes de converter `createColSphere`,
`createColCuboid`, `isElementWithinColShape` e `getElementsWithinColShape`.
As consultas devem considerar movimento, dimensão/interior, remoção e os
limites geométricos do desktop. Criação não pode ser contada como pronta sem
os eventos de entrada/saída correspondentes. Nenhuma destas cinco funções
foi contabilizada como convertida neste lote.

## Fila de conversão

| Ordem | Funções / área | Dependências |
|---|---|---|
| 2 | `getElementsWithinRange`, `createColSphere`, `createColCuboid`, `isElementWithinColShape`, `getElementsWithinColShape` | Preservar geometria de colshape hoje descartada em ENTITY_ADD; manter colisores com posição atual, interior/dimensão, remoção e eventos hit/leave. No inventário atual só `getElementsWithinRange` (2 recursos) e `createColSphere` (1) tiveram chamadas globais detectadas. |
| 3 | `acquireElementStreamingLease`, `releaseElementStreamingLease` | Aparecem em 5 recursos WorldRPG cada, com 27 e 21 chamadas. Portar referência/streaming real e liberação no stop; um token local sem impedir stream-out não resolve a dependência. |
| 4 | `call`, depois `downloadFile` | `call` apareceu em 6 recursos; exige exports e isolamento dos ambientes. Downloads exigem conclusão real, evento e cancelamento por geração do recurso. |
| 5 | `engineLoadTXD`, `engineImportTXD`, `engineLoadDFF`, `engineReplaceModel` | As 125 funções da categoria já têm handler. A carga e a substituição seguem os argumentos do desktop e falham fechadas: o decodificador RenderWare, os arquivos do recurso e a alocação de `engineRequestModel` ainda não estão ligados. Falta validar no aparelho. |
| 6 | Corpo nativo de `CVehicle` | As 165 funções Lua de veículo já têm handler. O `CVehicle` do libGTASA ainda não recebe trava, dano, handling, áudio, sirenes nem o spawn de `createVehicle`. |

## Reproduzir a busca

```sh
python3 tools/audit_lua_client_usage.py /caminho/do/servidor/mods/deathmatch/resources --json-out /tmp/client-api-usage.json
python3 tools/audit_lua_client_usage.py '/caminho/do/servidor/mods/deathmatch/resources/[worldrpg]' --json-out /tmp/worldrpg-api-usage.json
```

O comando só lê fontes/metadados. O JSON inclui exemplos com arquivo/linha e
os erros de leitura para que o recorte possa ser conferido. A cobertura GUI/DX
199/199 continua sendo de handlers, com as limitações visuais e nativas
registradas no roadmap.
