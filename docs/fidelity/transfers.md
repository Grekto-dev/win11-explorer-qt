# Transferências: apresentação opt-in sobre o motor libfm-qt

## Evidência visual disponível

A referência Windows está em `.state/fidelity/reference/phase2/20261031T0003-0300/TRANSFER_READY.json` (fora do Git). O diálogo real apareceu em amostras aos ~2,984 s e já não estava visível aos ~6,259 s; a cópia de 2 GiB terminou e o SHA-256 coincidiu. O estado observado em `paste_2s.png` estava recolhido, indicava 100%, barra verde, “Copiando 1 item”, “Mais detalhes” e ícones de pausa/cancelamento. O intervalo de desaparecimento é amostrado, não um tempo exato. Os controles não foram exercitados; nenhuma animação ou semântica de pause/resume é inferida.

## Seam opt-in versionada

`patches/libfm-qt/0001-opt-in-progress-dialog.patch` adiciona à `Fm::FileOperation` um `DialogFactory` opt-in. Sem `setDialogFactory()`, o construtor de diálogo permanece o padrão upstream. Um consumidor pode registrar uma subclasse de `Fm::FileOperationDialog` que controla sua apresentação; os métodos de progresso, source/dest, conflito e erro são virtuais. O `FileOperation` continua sendo dono do job e continua roteando `fileExists` e erro à mesma instância do diálogo. Portanto não há segundo motor, duplicação do prompt nem mudança na política `SKIP`/`OVERWRITE`/`RENAME`/`RETRY`/`ABORT`.

A API original de erro usa `ErrorAction&`; no Qt 6.8 a conexão `BlockingQueuedConnection` não conseguia enfileirar essa referência (aviso observado em execução e handler não chamado). O overlay acrescenta o sinal `Job::errorRequested(..., ErrorAction*)`, preserva o sinal antigo para consumidores existentes e conecta o fluxo de `FileOperation` ao novo sinal com `BlockingQueuedConnection`. O ponteiro é válido durante a chamada bloqueante e permite a resposta síncrona no mesmo contrato. Isso adiciona/alterar símbolos C++; reconstrua consumidores contra o overlay e não misture headers/ABI do SDK base com a biblioteca overlay.

A fábrica é estática/global e deve ser instalada uma vez na inicialização, antes de iniciar operações, e limpa apenas quando não houver operação pendente. O default é `nullptr`. O patch mantém o motor GIO e as rotinas upstream de erro/conflito.

## Build overlay isolado

`./scripts/fidelity/build-libfm-overlay.sh`:

1. exige o checkout local libfm-qt no revision `4294f29` e verifica SHA-256 dos arquivos upstream listados no script;
2. copia a fonte, incluindo a licença LGPL-2.1-or-later, para `.state/overlay-source/libfm-qt` e aplica o patch versionado;
3. compila/instala exclusivamente em `.state/libfm-overlay-build` e `.state/libfm-overlay-prefix`;
4. compila e executa `tests/transfer` usando a biblioteca privada.

O script não escreve em `.deps`, no prefixo SDK compartilhado nem na instalação do sistema. Os paths CMake exportados são absolutos ao worktree local, como o SDK existente.

## Observer e lifecycle

`ExplorerTransferController` é um observer emprestado, não dono do `Fm::FileOperationJob`. Estados: `Idle` antes de `markStarted()`, `Preparing` quando o proprietário declara que chamará `runAsync()`, `Running` após `preparedToRun`, `Cancelling` após pedido/sinal de `GCancellable`, `Finished` na publicação terminal do backend imediatamente antes de `Job::finished`, ou `Detached` se o job é destruído sem conclusão observada. `cancelled`/`cancellationRequested` descreve pedido de cancelamento, não terminalidade. O cancelamento não para timer nem conclui a operação.

`Fm::Job` não fornece sinal de início separado de `preparedToRun`; o chamador do observer deve chamar `markStarted()` imediatamente antes de `runAsync()`. A conclusão publica contadores e o pedido real de cancelamento em `FileOperationJob::Completion`, com mutex e vida útil compartilhada independente, antes de emitir `finished`. O controller conserva somente um `shared_ptr<const Completion>`; todas as conexões do backend ao controller são enfileiradas. Nenhum callback worker captura/acessa o controller, seu relógio ou um `QPointer` do job. Assim, um handler de `finished` registrado antes pelo proprietário pode destruir o diálogo/controller sem disputar uma captura de counters. UI, estados locais e timer ficam exclusivamente na thread do controller; o snapshot terminal imutável tem precedência sobre qualquer estado local e sobre a destruição posterior do job. Destruir o observer não destrói nem cancela o job. Uso concorrente exige respeitar a thread-affinity do controller e o proprietário continua responsável por manter o job vivo até seu sinal terminal.

## Testes/evidência

O teste usa `FileOperation` real e verifica factory opt-in, conflito com resposta `SKIP`, erro de destino inválido por `BlockingQueuedConnection`, preservação do conteúdo do destino, cópia real/hash de 2 GiB, cancelamento durante cópia, estados idle/preparing/running/cancelling/finished, snapshot final e destruição precoce do job/observer. Erro de origem ausente não produziu o fluxo de erro neste libfm-qt; o teste usa destino que é arquivo regular, não diretório. Teste de permissão foi omitido para não depender de execução não-root nem de permissões de host variáveis.

## Pausa cooperativa delimitada

`Job::pause()`/`resume()` agora têm implementação para um caminho delimitado: `FileTransferJob::Mode::COPY` de exatamente um arquivo regular nativo para destino nativo, usando o callback de progresso de `g_file_copy()`. A cada callback, o motor alcança um checkpoint cooperativo e aguarda numa condition variable própria, sem manter o mutex dos contadores de progresso. `pause()` confirma apenas o pedido; o controller passa de `Pausing` a `Paused` somente após o callback reconhecer o checkpoint. `resume()` libera a espera. Cancelar acorda a condition variable e deixa GIO concluir a operação cancelada; pedidos repetidos e pedidos após `Finished` são no-op.

`supportsPause()` retorna falso para MOVE/LINK, múltiplas fontes, diretórios, caminhos não nativos, provedores remotos e jobs que não sejam `FileTransferJob`. Não se anuncia suporte geral para GIO: callback, frequência e latência continuam dependentes do provedor; o contrato medido aqui é somente para arquivo local regular com cópia via `g_file_copy()`. Um pedido não equivale a confirmação: consumidores devem aguardar snapshot `paused`/estado `Paused`. O novo marcador `LIBFM_QT_COOPERATIVE_COPY_PAUSE_API` identifica a API overlay; reconstruir aplicação e consumidores contra o prefixo privado é obrigatório por mudança ABI.

O teste de integração usa uma fixture local materializada de 2 GiB: solicita pausa antes do início do job, espera confirmação do checkpoint, confirma que o destino já contém bytes mas ainda não chegou ao tamanho final, verifica tamanho estável por pelo menos 350 ms e ticks do event loop responsivos, retoma e compara SHA-256 de origem/destino. Testes adicionais cancelam enquanto pausado, verificam uma notificação por cancelamento interno/externo, capability falsa para LINK, e no-op de pause/resume após conclusão. A cópia completa de 2 GiB e o caminho de erro pointer-based também rodam contra a biblioteca overlay real sem `qRegisterMetaType` artificial nos testes.


## Correção de concorrência S1/L1/L2/L3

O patch existente `0002` agora inclui `core/jobpausegate_p.h` (helper interno,
sem API de debug da aplicação), `FileOperationJob::Completion`,
`completion()` e o hook protegido `Job::publishCompletion(bool)`.
`Job::requestCancellation()` retorna se o pedido venceu o corte terminal;
o slot `cancel()` preserva sua assinatura e encaminha a esse método.
`LIBFM_QT_COOPERATIVE_COPY_PAUSE_API` passa a 2. Há mudança de layout/vtable:
reconstruir a biblioteca privada, aplicação e todos os testes/consumidores.
Não misturar headers novos e a `.so` antiga.

A publicação terminal e os controles do backend usam o mesmo mutex da pausa.
O predicado usa uma flag de cancelamento protegida por esse mutex, não uma
leitura externa de GCancellable que possa mudar/notificar no intervalo
predicado/wait. Tanto `Job::cancel()` quanto o callback do GCancellable
publicam por esse caminho. `g_cancellable_cancel()` e o sinal `cancelled`
ficam fora do lock: o callback GLib é síncrono e readquire o mutex.
O corte terminal sela os controles e publica o snapshot antes de qualquer
handler de `finished`. Pedidos aceitos antes do corte permanecem registrados,
mesmo se o sinal GUI só for despachado depois; pedidos posteriores não
reabrem o job. `cancellationRequested` e o alias `cancelled` continuam sendo
pedido, não uma declaração de sucesso/falha/cancelamento como resultado.
Pausa/ACK são falsos no snapshot terminal. Finished e Detached são terminais
para a interface pública; o controller continua emprestado e não cancela
nem destrói o job ativo quando é destruído.

Regressões executadas nesta correção:

- Cancelamento no worker e GCancellable direto, com join antes de qualquer
  despacho GUI: snapshot terminal já contém o pedido e a notificação posterior
  é única. O teste original falhou na flag (RED).
- Handler de finished conectado antes do observer bloqueia numa barreira;
  GUI consulta counters/Finished, tenta controles e destrói o observer antes
  de liberar o worker. O teste original retornou Preparing (RED).
- A mesma primitiva privada utilizada por Job bloqueia no intervalo entre
  predicado falso e wait. O cancelador é iniciado ali; a variante sem mutex
  termina a notificação antes de wait e perde o wake (RED). O teste resgata
  threads bloqueadas com resume; a versão corrigida acorda sem esse resgate.
  Os deadlines de 200 ms/2 s são limites de observação/falha, não sleeps
  usados para produzir a janela. Também foi exercitado GCancellable direto
  no teste real de cópia pausada.
- Controles do backend depois da publicação não podem reabrir pausa nem
  cancelar um job já terminado (RED: pause era aceito).

Logs locais da worktree de correção: `.state/logs/red-cancel.log`,
`green-cancel.log`, `red-publication.log`, `green-publication.log`,
`red-wake.log`, `green-wake.log`, `red-terminal.log`, `green-terminal.log`
e `full-suite-final.log`. A última execução completa retornou 23 passed,
0 failed, 0 skipped, incluindo cópia materializada 2 GiB, pausa/ACK,
estabilidade do destino, event loop, resume/hash, cancelamento, erro e SKIP.
SHA-256 de origem/destino de 2 GiB:
`52cc8d15b836b3d498ad36d61a06e71ab0bd8118c22d43cef8db6419f2fff6bd`.
A primeira execução completa teve um timeout do ACK visual de 4 GiB
(5.200 ms versus limite de 5.000 ms); foi preservada em `full-suite.log`.
O mesmo teste, sem alterar timeout/assertivas, passou isoladamente e na
execução completa final. Avisos propagateSizeHints são do plugin offscreen.

Não foi executado ASAN/TSAN. Barreiras cobrem as ordens descritas, não uma
prova por enumeração de todos os escalonamentos; não se afirma que testes
temporizados provem ausência universal de corrida. A segurança de lifetime
se apoia também na remoção de todo callback worker que acessava o observer.
Revisão independente e integração permanecem responsabilidade do pai.

### Reprodução e integração

Todos os diretórios abaixo pertencem à `.state/` da worktree; SDK é somente
leitura. Não há novo patch a conectar: aplicar 0001 e o 0002 atualizado,
nessa ordem, ao pin `4294f29ed314388f1f6d051ae0128320354e417a`.
O 0002 também passa a modificar `core/fileoperationjob.h/.cpp`; o integrador
responsável pelos guards deve acrescentar esses dois arquivos à verificação
de hashes upstream. Nenhum script/guard/CMake foi modificado nesta correção.

Com o ambiente de `/home/pedro/win11-explorer-qt/.deps/env.sh` carregado,
a biblioteca foi configurada em `.state/libfm-overlay-build`, instalada em
`.state/libfm-overlay-prefix` e os testes configurados com:

```sh
cmake -S tests/transfer -B .state/transfer-overlay-build \
  -Dfm-qt6_DIR="$PWD/.state/libfm-overlay-prefix/share/cmake/fm-qt6" \
  -DCMAKE_PREFIX_PATH="$PWD/.state/libfm-overlay-prefix;$SDK_PREFIX;$QT_ROOT" \
  -DLIBFMQT_SOURCE_DIR="$PWD/.state/overlay-source/libfm-qt/src" \
  -DLIBFMQT_INCLUDE_DIR="$PWD/.state/libfm-overlay-prefix/include/libfm-qt6"
cmake --build .state/libfm-overlay-build --parallel 2
cmake --install .state/libfm-overlay-build
cmake --build .state/transfer-overlay-build --parallel 2
LD_LIBRARY_PATH="$PWD/.state/libfm-overlay-prefix/lib:$LD_LIBRARY_PATH" \
 QT_QPA_PLATFORM=offscreen .state/transfer-overlay-build/tst_explorertransfercontroller
```

O `ldd` dos testes resolveu a `.so` dentro do prefixo privado desta worktree
(`.state/logs/ldd-tests.log`). Aplicação/core e testes gerais também foram
recompilados em `.state/app-build`, fixando `fm-qt6_DIR` explicitamente para
o mesmo overlay; configure/build retornaram 0. Os testes gerais da aplicação
não foram executados nesta etapa. Não houve execução em :0, Xephyr ou VM.
