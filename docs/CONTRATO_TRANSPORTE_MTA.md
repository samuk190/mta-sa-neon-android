# Contrato recuperável do transporte MTA

Investigação estática em 2026-11-11. A primeira unidade implementável é o codec de abertura e de resposta do transporte MTA nativo. O restante da sessão depende de recuperar constantes de inicialização e vetores de interoperabilidade. As informações abaixo permitem delimitar esse trabalho sem executar o cliente legado nem emular o `netc`.

## Origem e rastreabilidade

| Artefato local | Identificação |
| --- | --- |
| `mta_decompile_android_progress/libWorldRPG/source/libWorldRPG_decompiled.c` | Pseudo-C exportado pelo Ghidra, 55.815.326 bytes, SHA-256 `4704e5a9c6db286180fce99347a00023a9974c623092999f6e027f140ec57e96` |
| `mta_decompile_android_progress/libWorldRPG/libs/libWorldRPG.so` | Biblioteca MobileSA original renomeada, ELF64 AArch64 Android, 19.291.424 bytes, SHA-256 `6843ffb45dfee25be9b93d542499b66cfe1822c748a080e64a3ffdfc40dd9590` |
| `logs/mobile_handshake_20261108/installed.apk` | Contém a biblioteca acima com o mesmo SHA-256 |
| `worldrpg_mobilessa_patched/apk/original/MobileSA-release-216c1e7f.apk` | Biblioteca de referência com SHA-256 `6c554c287586d0e19a438e0a727243394d51cb7fbd2208833b61b7407e1de7c1`. A estrutura de abertura inspecionada tem os mesmos bytes de código |
| `net.so` na raiz | Servidor ELF64 AArch64 Linux, SHA-256 `e6d7ea540ff223927d2ab39474050b6747eb2f64d79422bd11eb683b3d4a7338`, build-id `9eaac55d5e599d7b622c7e35ba43fddb889c6906` |
| `mta_mobile_neon/libs/netc_pc.dll` | Binário de cliente Windows x86 usado somente para comparação estática do magic |

O pseudo-C é uma referência de investigação. Os tipos de Ghidra e os nomes `FUN_...` precisam de validação no assembly. As bibliotecas de referência conservam dependências e comportamento próprios, portanto o produto deve conter a implementação nativa produzida a partir de contratos e testes.

## Conversão dos endereços Ghidra

O programa exportado pelo Ghidra tem base `0x100000`. Os endereços do pseudo-C incluem esse deslocamento. No ELF inspecionado, o RVA é o endereço Ghidra menos `0x100000`.

Dois pontos foram conferidos diretamente no ELF.

- `JNI_OnLoad` aparece em `0x00697780` no pseudo-C e em `0x00597780` na tabela de símbolos ELF.
- O construtor de abertura aparece como `FUN_00e0f4c0` no pseudo-C. No RVA real `0x00d0f4c0`, o assembly escreve `0x0409` no início da saída e `0xDE` no deslocamento `0x32`.

Essa diferença também se aplica aos endereços de constantes e slots de ponteiros descritos abaixo. Um endereço de Ghidra não deve ser usado diretamente como offset de arquivo ou endereço de hook.

## Locais relevantes encontrados

Todas as linhas desta tabela pertencem ao arquivo `libWorldRPG_decompiled.c` identificado acima.

| Função Ghidra | RVA real | Linha inicial | Comportamento recuperado |
| --- | --- | --- | --- |
| `FUN_00e0da40` | `0x00d0da40` | 1027389 | Preparação da conexão. Nas linhas 1027675–1027682, chama o construtor, guarda a abertura e a envia com comprimento `0x33` |
| `FUN_00e0f4c0` | `0x00d0f4c0` | 1028509 | Constrói a abertura de 51 bytes e guarda o nonce de 16 bytes |
| `FUN_00e0f630` | `0x00d0f630` | 1028580 | Envio por `sendto`, contadores e telemetria |
| `FUN_00e0fb00` | `0x00d0fb00` | 1028852 | Recepção por `recvfrom`, verifica endereço/porta de origem e encaminha ao parser |
| `FUN_00e104e0` | `0x00d104e0` | 1029706 | Parser de resposta de abertura e recepção de datagramas cifrados |
| `FUN_00e1a860` | `0x00d1a860` | 1036436 | Empacota envio cifrado com checksum de 16 bits e byte de seleção do fluxo |
| `FUN_00e23b00` | `0x00d23b00` | 1043390 | Enfileiramento confiável, chamado pelo fluxo de conexão com confiabilidade, canal e horário |
| `FUN_010b11fc` | `0x00fb11fc` | 1508435 | Checksum de 16 bits, com tabela construída usando polinômio `0x1021`. Valor inicial e resultado final precisam de vetor confirmado |
| `FUN_010b12f0` | `0x00fb12f0` | 1509064 | Inicialização do estado de cifra e tabela de permutação |
| `FUN_010b1494` | `0x00fb1494` | 1509227 | Transformação de bytes usada no envio |
| `FUN_010b14dc` | `0x00fb14dc` | 1509252 | Transformação de bytes usada na recepção |
| `FUN_010b2598` | `0x00fb2598` | 1510212 | Parser de quadro de confiabilidade com leitura por bits |

## Abertura de 51 bytes

O construtor tem o contrato estrutural abaixo. Os bytes 34–49 permanecem sem vetor confirmado.

| Deslocamento | Comprimento | Conteúdo | Confiança |
| --- | --- | --- | --- |
| 0 | 1 | `09`, identificador de abertura | Escrita imediata confirmada em assembly |
| 1 | 1 | `04`, campo fixo do perfil observado | Escrita imediata confirmada. O significado exato precisa ser conferido com o servidor |
| 2 | 16 | Nonce gerado para a conexão | O construtor gera 16 bytes e copia a mesma sequência para o estado da conexão |
| 18 | 16 | Magic do fork MTA | Referência usada também na conexão subsequente e nos pings. Valor corroborado abaixo |
| 34 | 16 | Bloco fixo carregado de constante | Valor em runtime ainda não recuperado |
| 50 | 1 | `DE` | Escrita imediata confirmada em assembly |

O nonce do perfil observado usa um LCG com multiplicador `0x41c64e6d` e incremento `0x3039`, extraindo um byte do resultado deslocado em 16 bits a cada passo. É um campo variável. Sua sequência de uma sessão anterior não deve virar constante do codec. Um codec pode receber o nonce por parâmetro para permitir vetores determinísticos, com a geração tratada separadamente.

O bloco fixo de 16 bytes vem de `DAT_003f5680` no Ghidra, RVA real `0x002f5680`. O magic vem de `DAT_0044eaba`, RVA real `0x0034eaba`. Os bytes dessas regiões no arquivo Android permanecem ofuscados. Copiar bytes diretamente do arquivo para a rede não recupera os valores em runtime.

## Magic fixo comprovado

```text
00 ff 00 ff fe fe fe fe fd fd fd fd 78 56 34 17
```

Há três confirmações independentes desse valor.

- O parser Android compara dois inteiros imediatos de 64 bits. A expansão little-endian de `-0x101010100ff0100` resulta em `00ff00fffefefefe`. A de `0x17345678fdfdfdfd` resulta em `fdfdfdfd78563417`.
- O `net.so` do servidor contém os 16 bytes no offset/RVA `0x434c30`.
- O arquivo `netc_pc.dll` contém os mesmos bytes no offset de arquivo `0x536da0`.

O magic do RakNet4 vendorizado começa com `00 ff ff 00` e termina com `12 34 56 78`. O bloco comprovado acima pertence ao fork MTA e deve ter um nome próprio no codec.

## Resposta de abertura e extensões

No estado de abertura, `FUN_00e104e0` valida o formato estrutural abaixo antes de inicializar a cifra.

| Deslocamento | Conteúdo observado |
| --- | --- |
| 0 | Identificador `0A` |
| 1–16 | Magic MTA completo |
| 17–32 | Bloco opaco de 16 bytes lido pelo parser. A função inspecionada não demonstra que seja um eco do nonce |
| 33 | Marcador `ED` para a seção de extensões |
| 34 em diante | Campos TLV com tag de um byte, comprimento de um byte e valor |
| Final da seção | Marcador `DE` |

O parser exige mais de 34 bytes para entrar na seção de extensões. Reconhece os campos abaixo e verifica seus limites antes de copiar.

| Tag | Limite/forma observada | Uso recuperado |
| --- | --- | --- |
| `6E` | Até 64 bytes | Buffer passado como material de inicialização da cifra e seu comprimento |
| `1A` | Até 16 bytes | Buffer e comprimento de tag/prefixo passados à inicialização |
| `A8` | Comprimento exatamente 4 | Inteiro de 32 bits repassado à inicialização |
| `C8` | Comprimento exatamente 4 | Outro inteiro de 32 bits repassado à inicialização |

A semântica completa dos inteiros, a obrigatoriedade de cada campo e o tratamento de campos duplicados precisam de vetores da revisão do servidor. Não há chaves ou valores de sessão publicados neste documento.

## Localização dos helpers por relocação

Os slots usados pelo pseudo-C foram resolvidos estaticamente pela tabela `.rela.dyn` do ELF. Isso evita tratar um ponteiro indireto como implementação ausente.

| Slot de ponteiro no ELF | Relocação relativa | Papel recuperado |
| --- | --- | --- |
| `0x01256d60` | `0x00fb12f0` | Inicialização da cifra |
| `0x01256d30` | `0x00fb11fc` | Checksum |
| `0x01256e38` | `0x00fb1494` | Transformação de envio |
| `0x01256d38` | `0x00fb14dc` | Transformação de recepção |
| `0x01256d40` | `0x00fb2598` | Parser de confiabilidade |
| `0x01256ce0` | `0x00d23b00` | Enfileiramento confiável |
| `0x01256cd8` | `0x00d24060` | Pulso do estado confiável |

Uma consulta reproduzível é `readelf -rW` sobre a biblioteca Android de referência. O addend de uma relocação `R_AARCH64_RELATIVE` fornece o RVA do helper. O pseudo-C correspondente usa esse RVA mais a base Ghidra.

## Cifra e confiabilidade ainda exigem vetores

A inicialização de cifra limpa um estado de aproximadamente `0x11c` bytes, guarda campos de tag/metadados e inicializa uma permutação de 256 bytes em `state+0x1c` a partir de constantes ofuscadas. Há 256 passos de mistura com o material recebido e, depois, 1000 passos adicionais de trocas na tabela.

As funções de transformação de envio e recepção apresentam o mesmo corpo no pseudo-C. Para cada byte, usam `i & 255`, acumulam um seletor com `S[i & 255]` e fazem XOR com um byte derivado da tabela. Nesse laço não há a troca de elementos característica do PRGA RC4 convencional. Portanto, um wrapper de RC4 padrão não está comprovado como equivalente a esse contrato.

No caminho inspecionado de envio, `FUN_00e1a860` calcula a transformação e gera `[checksum LE de 2 bytes][seletor de 1 byte][dados cifrados]`, com comprimento total `N+3`. A recepção contempla um prefixo de comprimento negociado, verifica o checksum sobre os dados cifrados e chama a transformação com o seletor do datagrama. O parser de confiabilidade é chamado sobre o conteúdo decifrado.

Os layouts de sequência, ACK/NAK, canais, ordenação e fragmentação devem ser extraídos de `FUN_010b2598`, `FUN_00e23b00` e seus consumidores. O corpo de `backend_mta/scripts/mobile_handshake.hpp` ajuda a testar o bitstream de aplicação, especialmente `SERVER_JOINEDGAME`. Ele não contém o codec de abertura nem a confiabilidade do transporte.

## Capturas existentes e lacunas

Os logs em `logs/mobile_handshake_20261108/` e `logs/mobile_compat_20261108/` registram o envio de 51 bytes e a transição após uma resposta de abertura válida. A busca realizada nesses logs, nas amostras binárias existentes e nos artefatos relevantes de `/tmp` não encontrou um vetor completo de OPEN de 51 bytes ou uma captura PCAP da negociação.

As amostras `worldrpg-compat-captures/*.bin` existentes são pacotes de aplicação de recursos e sincronização. Elas não fornecem o bloco fixo faltante da abertura. O material atual tampouco comprova a inicialização exata da permutação da cifra ou sua interoperabilidade em uma implementação nova.

## Unidade estrutural e próxima integração

### Unidade estrutural entregue em 2026-11-11

`app/src/main/cpp/net/mta/MtaOfflineCodec.{h,cpp}` contém o construtor OPEN51 e o parser de resposta. Os testes em `tests/test_mta_offline.cpp` passaram na Oracle com C++20, AddressSanitizer e UndefinedBehaviorSanitizer. O resumo rastreável está em `tests/evidence/mta_offline_codec_oracle.json`.

A unidade exige nonce e bloco fixo explícitos, preserva os campos desconhecidos/duplicados e não envia datagramas. Adota duas políticas mais estritas que o parser histórico: rejeita dados após DE e rejeita A8/C8 com comprimento diferente de 4. Essas políticas precisam ser confrontadas com um vetor real. As fixtures são sintéticas; o aceite de interoperabilidade dos passos abaixo permanece pendente.

### Sequência de integração

1. Criar um codec independente do motor para a abertura. Receber nonce e bloco fixo como parâmetros explícitos, preservar os 51 bytes e registrar a revisão do contrato. O envio contra o servidor fica condicionado ao vetor confirmado do bloco fixo.
2. Implementar um parser de resposta com magic correto, limites TLV, prazo, origem da resposta e erro explícito para pacote truncado. Usar vetores sintéticos para os limites estruturais e identificar separadamente o vetor real da revisão.
3. Recuperar estaticamente o bloco fixo e os vetores iniciais da permutação, ou obter uma captura autorizada independente que já contenha a negociação. Registrar a origem e os hashes. Não preencher lacunas com zeros presumidos.
4. Validar OPEN e resposta numa sonda nativa sem interface gráfica, executada na Oracle. Registrar tamanhos, estados e hashes de payloads com material de sessão protegido. Não avançar para o join de aplicação antes de obter resposta válida.
5. Reimplementar checksum e cifra com vetores para dados vazios, tamanhos de borda, seletores diferentes e prefixos negociados. Confirmar equivalência das transformações antes de ligar confiabilidade.
6. Implementar confiabilidade por famílias de mensagens e provar conexão, confirmação, `MOD_NAME`, ACK/NAK, retransmissão e fragmentação. Só então conectar a saída ao `NeonJoinClient` existente.

O primeiro marco de interoperabilidade é uma resposta de abertura corretamente analisada pela implementação própria. A passagem para `MOD_NAME` exige o restante da troca criptográfica e de confiabilidade. Nenhuma dessas etapas foi declarada concluída por esta análise estática.
