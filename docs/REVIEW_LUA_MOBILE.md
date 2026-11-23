# Revisão de Lua, áudio, codecs e projéteis

Revisão em 2026-11-15, branch `staging`. Escopo: trabalho deste chat, incluindo
os dois primeiros getters, áudio, consultas de armas, codecs Speex/Opus e ponte
de projéteis locais. Alterações anteriores de ped/sync, DX e SVG ficaram fora
do commit; os arquivos compartilhados foram separados a partir das cópias
anteriores a este trabalho.

## Problemas corrigidos na revisão

- **Leitura além do buffer de PCM:** o encoder antigo passava um frame completo
  para Opus mesmo quando recebia menos amostras. Os dois codecs agora exigem
  exatamente o tamanho de um frame intercalado antes de ler o buffer.
- **Ciclo de vida dos codecs:** inicializar novamente não vaza o encoder antigo;
  configuração inválida preserva a instância anterior. Ganhos não finitos e
  pacotes maiores que o limite são recusados.
- **Mutação tardia de projétil:** uma tarefa que não começou até o timeout é
  cancelada e não cria um projétil depois de a chamada Lua retornar `false`.
  Chamadas feitas na thread do jogo executam sem esperar pela própria fila.
- **Reentrada e corpos destruídos:** a ponte é servida dentro da proteção de
  reentrada do loop do jogo; criadores com vtable `CPlaceable` são recusados.
- **Layout nativo diferente:** instruções ARM64 verificadas protegem os acessos
  ao temporizador e à velocidade. Uma biblioteca diferente desativa a ponte.
- **Referências e recursos:** armas não mantêm dono/alvo após remoção e reuso
  de ID. Sons e projéteis são liberados pelo início do recurso que os criou.

## Validação

| Verificação | Árvore completa de trabalho | Conteúdo isolado do commit |
|---|---:|---:|
| Recursos/Lua | 207, sem falhas | 185, sem falhas |
| Speex e Opus | 50, sem falhas | 50, sem falhas |
| Fila e proteções de projéteis | 8, sem falhas | 8, sem falhas |
| Classes Java do jogo | Compilam | Compilam |
| Biblioteca nativa ARM64 | Compila e linka | Ver limitação abaixo |

A contagem menor de verificações no commit é esperada: testes de SVG/DX
anteriores, ainda não commitados, não foram incluídos. O catálogo do commit
foi regenerado a partir dos seus próprios fontes; o da árvore de trabalho
inclui também as alterações anteriores.

**Limitação herdada da branch:** a compilação nativa isolada encontrou chamadas
já presentes em `HEAD` para `HasPrivatePlayerData`, `CanAttachPrivatePlayerData`,
`DestroyRemotePlayerPed`, `RemoteTouchActive` e a forma de sete argumentos de
`SetRemoteStick`, mas o `PlayerPedPort.*` commitado ainda não as fornece.
As implementações correspondentes existem nas alterações locais anteriores.
Elas foram preservadas fora deste commit. A compilação ARM64 da árvore completa
passa; o checkout isolado depende da conclusão desse trabalho anterior do motor.

## Limites que permanecem explícitos

- Speex e Opus têm encode/decode compatível e seleção explícita. Ainda não
  representam um chat de voz funcionando de ponta a ponta: faltam captura,
  reprodução e transporte; o handshake desktop seleciona Speex, sem adivinhação.
- A ponte física de projéteis compila, mas trajetória, colisão e explosão não
  foram executadas no aparelho. Os testes locais verificam guardas e filas.
- Projéteis são locais; faltam pacotes/evento de criação, associação à árvore
  de elementos, rotação/modelo customizados e anexação de satchel.
- Consultas de armas ainda usam o último estado de `ENTITY_ADD`.
- O Gradle falhou ao aceitar a conexão com seu daemon; não foi gerado APK novo.

Detalhes: [áudio, voz e projéteis](ROADMAP_AUDIO_WEAPONS_MOBILE.md).
