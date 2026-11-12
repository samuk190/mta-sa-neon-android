# Diagnóstico do transporte Neon Android

Auditoria de 2026-11-10. O APK do S25 chega à tentativa de conexão do RakNet vendorizado e falha antes de receber `MOD_NAME`. O próximo requisito é demonstrar interoperabilidade do transporte MTA usado pelo servidor.

## Evidências observadas

| Origem | Observação | Alcance |
| --- | --- | --- |
| `/tmp/opencode/boot1.log`, 21h59 no S25 | `vendor packet ID 17 translated to RID 15`, seguido de `CONNECTION_ATTEMPT_FAILED` e timeout do `NeonJoin` | A tentativa RakNet terminou sem completar conexão |
| `raknet/Source/MessageIdentifiers.h`, linhas 111–133 | Vendor `ID_CONNECTION_ATTEMPT_FAILED=17`; `ID_INCOMPATIBLE_PROTOCOL_VERSION=25` | O número 17 do log significa falha da tentativa |
| `raknet/Source/RakPeer.cpp`, linhas 5720–5727 | O próprio RakPeer enfileira `ID_CONNECTION_ATTEMPT_FAILED` após esgotar tentativas | Esse evento pode ser produzido localmente, sem uma recusa explícita recebida do servidor |
| `raknet/Source/RakPeer.cpp`, linhas 5758–5761 | Abertura com ID `05`, magic de 16 bytes, versão de protocolo e padding até MTU menos 28 bytes | Formato efetivamente gerado pela biblioteca ligada ao APK |
| `net.so` da raiz | ELF AArch64 Linux, SHA-256 `e6d7ea540ff223927d2ab39474050b6747eb2f64d79422bd11eb683b3d4a7338`; strings `CCryptRC4.hpp` e `ReliabilityLayer4.cpp` | Indícios diretos de uma implementação modificada do transporte |
| `logs/mobile_handshake_20261108/client_final_cold.log`, linhas 1257–1299 | Cliente MobileSA anterior envia abertura de 51 bytes, interpreta resposta, ativa RC4, envia conexão e recebe CRA `0x0E`, depois `MOD_NAME 0x6A` | Referência histórica que interoperou com o servidor daquela sessão |

Os caminhos `raknet/` acima são relativos a `app/src/main/cpp/`. O `net.so` exporta `InitNetServerInterface`, enquanto o desktop carrega `netc` por `InitNetInterface`. O binário de servidor não fornece a implementação Android do cliente.

O código atual traduz notificações locais vendor para RID em `net/raknet/RakNetWrapper.cpp`. Essa tradução acontece depois de `RakPeerInterface::Receive()`. Ela não altera os datagramas de abertura, a troca criptográfica ou a codificação da confiabilidade dentro de `RakPeer`.

## Sonda reproduzível

`tests/probe_neon_transport.py` usa apenas Python 3 e biblioteca padrão. Deve ser executado na VM Oracle conforme as diretrizes do projeto. Envia uma consulta ASE e seis aberturas RakNet4, combinando versões 5/6 e MTUs 1492/1200/576. Cada combinação usa um socket novo e um prazo limitado. O resultado inclui endereço resolvido, tamanho, prefixo e SHA-256 do datagrama enviado.

```bash
python3 - --host worldrpgbroficial.duckdns.org --timeout 1.5 \
    < tests/probe_neon_transport.py
```

Para reproduzir a partir do WSL, enviando o script pela entrada padrão sem instalar arquivos no servidor, execute na raiz de `worldrpg_neon_launcher`.

```bash
ssh -o BatchMode=yes -i ~/.ssh/oracle_worldrpg_rsa ubuntu@146.235.24.222 \
    'python3 - --host worldrpgbroficial.duckdns.org --timeout 1.5' \
    < tests/probe_neon_transport.py
```

O destino `--host 127.0.0.1` permite comparar o mesmo serviço a partir da própria VM. A evidência registrada fica em `tests/evidence/transport_oracle_public.json` e contém o instante UTC da execução. A sonda termina após OPEN1 e consulta ASE. Ela observa respostas de transporte, sem executar autenticação ou alocar jogadores por meio de mensagens de jogo.

A execução registrada na Oracle em 2026-11-11 às 01h07 UTC, equivalente a 2026-11-10 às 22h07 em Brasília, resolveu o domínio para `146.235.24.222`. A consulta ASE recebeu 183 bytes com cabeçalho `EYE1`. As seis combinações de OPEN1 tiveram `no_response_within_timeout` em 1,5 segundo cada. Os datagramas enviados tinham 1464, 1172 e 548 bytes conforme o MTU.

## Interpretação dos resultados

- ASE `EYE1` mostra resposta do serviço de consulta naquela rota e instante.
- Resposta ao OPEN1 precisa ser interpretada pelo protocolo correspondente antes de continuar a sessão.
- `no_response_within_timeout` significa que a sonda não recebeu um datagrama naquele socket dentro do prazo. Sozinho, o resultado não distingue descarte de protocolo, filtragem, limitação de taxa ou outra política do servidor.
- O timeout observado nas versões 5 e 6 não fundamenta outra troca arbitrária do número de versão. A sequência histórica com abertura de 51 bytes e RC4 exige investigar o formato completo do fork MTA.

## Correção delimitada

1. Registrar os bytes de abertura e resposta de uma sessão desktop Neon autorizada, associando hashes do APK, `netc`, `net.so`, versão de bitstream e horário. Registrar metadados e campos necessários ao protocolo com credenciais suprimidas.
2. Implementar um transporte MTA nativo sob uma interface explícita. Portar a abertura, negociação criptográfica, quadros confiáveis, números de sequência, ACK/NAK, fragmentação, canais, ordenação, retries e encerramento a partir de contratos comprovados. Preservar a autenticação oficial do servidor.
3. Validar primeiro uma sessão sem interface gráfica na Oracle. O critério exige resposta válida de abertura, troca criptográfica, `CONNECTION_REQUEST_ACCEPTED`, confirmação de nova conexão e `MOD_NAME` decodificado. As mensagens de aplicação passam a ser enviadas depois desse estado.
4. Integrar o transporte com `NeonJoinClient`, preservando a distinção entre transporte conectado, sessão Neon verificada, recursos carregados e jogador em jogo. Propagar falha de transporte imediatamente, com uma única causa final, evitando um segundo timeout genérico do join.
5. Executar regressões de perda, reordenação, duplicação, fragmentação, desconexão e reconexão. Preservar contagem real de bits e pacote completo na fronteira do adapter. A interface atual ainda usa bytes e limita a recepção a 64 KiB.
6. Confirmar no S25 a mesma sequência de estados e a continuidade da sessão antes de declarar o transporte concluído.

Os testes de codec em `tests/test_join_codec.cpp` usam `raknet_adapter_stub.cpp`. Eles podem validar os layouts sintéticos do join, mas a validação da comunicação real depende do novo critério de transporte e dos testes de interoperabilidade acima.
