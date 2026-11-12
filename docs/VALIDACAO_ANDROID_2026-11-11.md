# Validação Android — 11/11/2026

O checkpoint GTA offline passou no Samsung S25. O APK atualizado está instalado, com o mesmo SHA-256 do artefato auditado. A sessão multiplayer Neon ainda não conecta; recursos, interfaces e entidades MTA continuam nas etapas seguintes.

## Artefato e ambiente

| Item | Valor |
| --- | --- |
| APK instalado | `outputs/worldrpg-neon-device.apk`, debug, pacote `com.worldrpg.neonlauncher` |
| SHA-256 do APK | `039d095d72eba94e452efe0da67c313d5853d1bf20ef469f25969a2a9bd14b8a` |
| Assinatura | Debug preservada para atualizar sem apagar os dados; esquemas v2/v3 conferidos |
| Build | Oracle ARM64, NDK `25.1.8937393`, Gradle 8.4; `outputs/build-final-host-reuse.log` |
| Dispositivo | SM-S938B, Android 17, ARM64, páginas de 4.096 bytes |
| Engine GTA | SHA-256 `2b24e426055f9b8b6b16a27aff0ed00e4ff6c122afacc5d9864909a5c5aadaff` |
| Geração CDN | `2bf9fb2f6aac9f0ac62f2c87bf4702768e7ebb59`; 358 hashes conferidos, sem divergências |

`outputs/validation-final.json` reúne a conferência do APK instalado, os dados do aparelho e as observações de ciclo de vida. A auditoria de ELFs/empacotamento está em `outputs/apk-audit-final.json`: engine com alinhamento de 64 KB e biblioteca própria de 16 KB, todas comprimidas para extração na instalação. Execução com páginas de 16 KB ainda precisa de aparelho correspondente.

## Resultados observados

| Cenário | Resultado | Evidência |
| --- | --- | --- |
| Abrir GTA e entrar no mundo | Mundo, CJ e HUD renderizados | `outputs/engine-final-world.png` |
| Controles e retomada da rodada inicial | Movimento de toque e mundo após minimizar/retomar | `outputs/engine-player-movement.png`, `engine-resumed-world.png` |
| Sair do mundo | Loop nativo saiu antes de onPause; espera de 41 ms, retorno ao launcher | `outputs/engine-isolated-exit.log` |
| Reabrir, minimizar/retomar e sair | Nova engine abriu; saída esperou 34 ms | `outputs/engine-isolated-reopen-resume.log` |
| Sair logo após abrir | Encerramento observado, espera de 18 ms | `outputs/engine-isolated-early-exit.log` |
| Abrir novamente pelo launcher com GTA minimizado | Uma única preparação e mesmo PID da engine; mundo retomado, saída em 33 ms e launcher preservado | `outputs/engine-final-host-reuse.log`, `device-final-host-reuse.json`, `engine-final-reused-world.png` |
| Tentativa de rede | Uma causa terminal: `Connection attempt failed`; sem segundo timeout do join na janela observada | `outputs/neon-transport-failure-final.log` |
| Codec do join e regressão de falha | PASS na Oracle, incluindo causa preservada, pacote tardio, nova tentativa e perda pós-join | `outputs/join-tests-final.log` |
| Codec estrutural MTA | PASS na Oracle com ASan/UBSan, limites TLV e truncamento a cada byte da fixture | `tests/evidence/mta_offline_codec_oracle.json` |

As imagens do mundo e os testes de movimento são do GTA offline. Não comprovam NPCs de servidor, HUD Lua do WorldRPG ou sincronização multiplayer. As fixtures de transporte são sintéticas e não contêm uma negociação real.

## Correções exercitadas

O Voltar agora solicita saída com a engine ativa e Surface disponível. Um frame real após a retomada indica que o jogo voltou a progredir; `resumeEvent()` sozinho apenas enfileira um pedido. O join acontece numa thread auxiliar, a fila nativa é liberada depois e a Activity é fechada. Isso removeu o deadlock da saída já pausada e o crash anterior por destruir mutexes ainda utilizados.

O host vive no processo Android `:engine`. Após o encerramento, esse processo é aposentado para que os singletons GTA sejam novos na próxima abertura. O launcher permanece vivo. Configurações reutiliza um host existente no mesmo task, evitando inicializar duas engines no mesmo processo. Falha fatal de bootstrap usa o encerramento do processo isolado para recuperar recursos, sem esperar por uma Surface que não chegou a existir.

Saída espontânea iniciada pela engine e destruição forçada pelo Android permanecem cenários separados. A análise nativa mostrou que `quitAndWait()` possui um atalho quando a engine já marcou a saída; seu retorno não é uma prova universal de `pthread_join`. Os logs acima demonstram o fluxo explícito de Voltar exercitado no aparelho.

## Próximo aceite

Recuperar o bloco fixo de OPEN51 e os valores iniciais da cifra, validar vetores reais e obter OPEN_REPLY na implementação própria. Depois completar confiabilidade, conexão, `MOD_NAME` e join. O [contrato recuperado](CONTRATO_TRANSPORTE_MTA.md), a [matriz de paridade](MATRIZ_PARIDADE_NEON.md) e as [etapas](ETAPAS_PORT_NEON_ANDROID.md) detalham os componentes seguintes.

Interfaces MTA, pedestres sincronizados e a regressão de morte ao andar seguem pendentes. A divergência de IDs/saúde encontrada no fonte precisa de codecs corretos e observação dos dois lados antes de atribuir a causa da morte. Áudio, instalação sob falhas, sessão longa e dispositivos de 16 KB também têm aceites próprios.
