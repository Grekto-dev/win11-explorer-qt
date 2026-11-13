# Confiabilidade do teste de pausa visual real

Escopo: `liveOperationPauseButtonWaitsForBackendAck`, sobre o snapshot
`337002eb8b54ce9e486e7e1a1106eb24a0b50459`. Nenhuma mudança no controlador,
no diálogo, no motor libfm-qt ou nos patches do overlay.

## Reprodução e causalidade

O suite completo do integrador falhou com 22 passes e uma falha: o `QTRY`
da legenda `Retomar` tinha limite de 5.000 ms; Qt informou que 8.300 ms teriam
sido suficientes. A cópia independente de 2 GiB com SHA-256 passou em
5.469 ms (5,469 segundos), não em 5 ms. Log: `.state/parent-transfer-337.txt`.

Dois processos novos executaram somente o teste alvo, sem retry automático:

- `.state/pause-qa-target-1.log`: mantendo o limite original, o pedido foi
  observado em 1.062 ms, com 116.129.792 bytes no destino e ACK falso. Na
  amostra de 16.033 ms, ainda havia pedido sem ACK, `Pausando…`, progresso
  publicado zero e 1.069.285.376 bytes no destino. O teste falhou. Os eventos
  periódicos da GUI continuaram sendo processados. Portanto, o ACK estava
  ausente no backend; não era apenas uma legenda atrasada.
- `.state/pause-qa-target-2.log`: execução exclusivamente diagnóstica com
  janela maior para observar o checkpoint, não uma correção aceita. Pedido
  em 996 ms; ACK/legenda em 2.168 ms, com 2.147.479.552 bytes no destino.
  `.state/pause-qa-syscalls-2.log`, coletado com `strace -f -ttt -T`, mostrou
  `FICLONE` recusado (`EOPNOTSUPP`) e `copy_file_range` solicitando os 4 GiB,
  retornando 2.147.479.552 bytes após 2,045780 s. Após retomar, outra chamada
  desse tamanho levou 8,229534 s. Esses tempos variam entre execuções.

A implementação lida foi a do overlay realmente carregado:
`FileTransferJob::copyRegularFile()` chama `g_file_copy()`;
`gfileCopyProgressCallback()` chama `waitWhilePaused()` ANTES de publicar o
progresso. `JobPauseGate::wait()` é quem publica ACK. Portanto, tamanho no
filesystem pode crescer enquanto o progresso publicado permanece zero e o
pedido ainda aguarda a próxima callback. A chamada síncrona em curso não é
interrompida por esse pedido cooperativo. O trace de syscalls comprova essa
via de cópia; a execução vermelha não foi rastreada por strace, portanto sua
syscall específica e sua duração exata não foram medidas.

O diálogo normal é criado por timer de 1 s (`SHOW_DLG_DELAY`), depois do
início da operação. Esperar esse timer para então pedir pausa acrescentava
outra corrida: em armazenamento rápido a cópia poderia terminar antes da
criação/clique. Não se identificou nesta investigação um bug de estado que
justificasse modificar produção. Scheduler, cache e disco podem afetar os
tempos, mas suas contribuições individuais não foram isoladas. Há I/O
concorrente de outros trabalhos/VM; isto não é benchmark isolado.

## Fixture e contrato do teste corrigido

A fixture ainda materializa 4 GiB e executa o `Fm::FileOperation::Copy`, o
`FileTransferJob`, o diálogo e os cliques reais. Uma conexão de teste ao sinal
`preparedToRun`, posterior ao handler bloqueante normal de `FileOperation`,
retém o worker até o timer normal criar o diálogo e o clique solicitar pausa.
Esse rendezvous tem watchdog de 15 s e não publica ACK nem copia bytes.

Com o worker retido, o teste exige pedido real, ACK falso, estado `Pausing`,
legenda `Pausando…` desabilitada e despacho de eventos GUI sem oferecer
`Retomar`. Só então libera o worker. A pausa precisa ser confirmada pelo
checkpoint real do GIO, ter `0 < bytes no destino < 4 GiB` e habilitar
`Retomar`. A estabilidade é amostrada a cada aproximadamente 10 ms durante
pelo menos 300 ms, com ACK ainda verdadeiro e pelo menos cinco ticks do timer
GUI de 25 ms. Retomar precisa retirar o pedido e concluir a cópia de 4 GiB.
A prova independente `reportsRealTwoGiBCopyProgressAndIntegrity` permanece
inalterada: materialização, progresso, pausa, retomada e SHA-256 origem/destino.

Mudança explícita no contrato de teste: remover o SLA não contratado de ACK
em 5 s. ACK e conclusão compartilham UM orçamento finito de I/O de 180 s,
herdado do orçamento que o teste já concedia à conclusão, sem reiniciá-lo na
retomada. `qWaitFor` observa os eventos/estados e falha no limite, sem a
segunda janela implícita de `QTRY`. Esse orçamento é uma proteção operacional
do teste pesado, não uma garantia de latência de pausa do produto nem prova
de que todo disco responderá em 180 s. O timeout externo do runner/CTest
continua valendo e pode encerrar antes em máquinas excessivamente lentas.

`PAUSE_TRACE` registra relógio monotônico, fase, estado do controlador real,
pedido/ACK do job, progresso, bytes no destino, ticks GUI e legenda. A métrica
`ack_latency_ms` inclui os 75 ms deliberados de observação sem ACK no
rendezvous; `checkpoint_wait_ms` começa na liberação do worker. Não medir
latência de produto com esses números como se a fixture não interferisse.
A limpeza restaura a factory e libera/cancela o worker mesmo em falha; se o
cancelamento também exceder seu limite, preserva e identifica a fixture ainda
ativa em vez de remover arquivos debaixo do worker.

Não há promessa de pausa instantânea, de preempção de syscall, de cobertura
de todos os filesystems ou de paridade temporal com o Windows. Um filesystem
que complete os 4 GiB numa única operação/reflink não satisfaz a precondição
de pausa parcial desta fixture e deve falhar explicitamente, não virar skip
ou aceitar pausa após a cópia completa.

## Ambiente e logs

Build incremental: `.state/transfer-overlay-build`, no máximo dois jobs.
SDK somente leitura: `/home/pedro/win11-explorer-qt/.deps/env.sh`.
`LD_LIBRARY_PATH` coloca `.state/libfm-overlay-prefix/lib` antes do SDK;
`.state/pause-qa-ldd.txt` confirma a resolução de `libfm-qt6.so.17` para o
prefix privado desta worktree. `QT_QPA_PLATFORM=offscreen`, TMPDIR scratch e
runtime XDG privado; sem VM, display pessoal ou instalação de sistema.

O primeiro build encontrou um caminho absoluto de CMake removido do cache
uv; regenerar o mesmo build com o CMake resolvido pelo SDK corrigiu a
infraestrutura. Não foi uma falha do teste. O log de build diagnóstico foi
substituído pelo build incremental bem-sucedido; a falha original permanece
no resultado da ferramenta da sessão.
O espaço livre observado antes das fixtures foi 56 GiB e, depois dos dois
processos diagnósticos, 58 GiB. `QTemporaryDir` remove as fixtures normais.
Os logs de falha e diagnóstico são preservados em `.state/`, fora do Git.

## Validação final, sem retry

O build final passou com `QT_NO_CAST_FROM_ASCII`, `QT_NO_CAST_TO_ASCII` e
`QT_USE_QSTRINGBUILDER`, com no máximo dois jobs e sem warnings do compilador.
Logs: `.state/pause-qa-configure-final.log`, `.state/pause-qa-build-final.log`.
A resolução do overlay foi verificada novamente antes de executar, em
`.state/pause-qa-ldd-final.txt`.

Após a correção, o binário completo foi executado UMA vez, diretamente, com
limite externo de 360 s. Não foi reexecutado CTest nem o suite completo até
obter verde. Resultado em `.state/pause-qa-full-final.log`: **23 passed,
0 failed, 0 skipped**, 44.831 ms. A contagem de linhas PASS foi reconciliada
programaticamente com o total do Qt. Há quatro avisos conhecidos do plugin
offscreen sobre `propagateSizeHints`, não uma execução livre de avisos GUI.

A versão corrigida passou nessa execução completa:

- Pedido sem ACK e `Pausando…`: 965 ms após início da operação, worker retido.
- ACK/`Retomar`: 2.790 ms; `ack_latency_ms=1825`, incluindo a espera deliberada
  da fixture; `checkpoint_wait_ms=1749`, após liberar o worker.
- Destino pausado: 2.147.479.552 bytes, estritamente entre zero e 4 GiB.
- Estabilidade amostrada: 303 ms; 11 ticks GUI durante a pausa.
- Destino concluído após retomar: 4.294.967.296 bytes, em 11.747 ms desde o
  início da operação (não inclui materialização da origem).
- Teste independente de 2 GiB: 1.716 ms de operação; SHA-256 origem e destino
  iguais a `52cc8d15b836b3d498ad36d61a06e71ab0bd8118c22d43cef8db6419f2fff6bd`.

Foram somente três execuções da fixture de 4 GiB: duas diagnósticas isoladas
(uma vermelha) e a execução final dentro do suite completo. Nenhuma repetição
adicional do alvo corrigido foi feita para não ultrapassar esse orçamento;
não se afirma taxa estatística de ausência de flakes. Ao terminar, não havia
diretórios `tst_explorertransfercontroller-*` restantes no TMPDIR scratch.
O espaço livre observado ao final foi 54 GiB; outros trabalhos continuam
usando o disco e essa diferença não foi atribuída a esta fixture.
