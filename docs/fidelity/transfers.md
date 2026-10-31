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

`ExplorerTransferController` é um observer emprestado, não dono do `Fm::FileOperationJob`. Estados: `Idle` antes de `markStarted()`, `Preparing` quando o proprietário declara que chamará `runAsync()`, `Running` após `preparedToRun`, `Cancelling` após pedido/sinal de `GCancellable`, `Finished` somente no sinal real `Job::finished`, ou `Detached` se o job é destruído sem conclusão observada. `cancelled`/`cancellationRequested` descreve pedido de cancelamento, não terminalidade. O cancelamento não para timer nem conclui a operação.

`Fm::Job` não fornece sinal de início separado de `preparedToRun`; o chamador do observer deve chamar `markStarted()` imediatamente antes de `runAsync()`. A conclusão captura contadores no handler direto de `finished` antes do auto-delete do job; o encaminhamento de UI e stop do timer ocorre na thread do controller. Destruir o observer não destrói nem cancela o job. Uso concorrente exige respeitar a thread-affinity do controller e o proprietário continua responsável por manter o job vivo até seu sinal terminal.

## Testes/evidência

O teste usa `FileOperation` real e verifica factory opt-in, conflito com resposta `SKIP`, erro de destino inválido por `BlockingQueuedConnection`, preservação do conteúdo do destino, cópia real/hash de 2 GiB, cancelamento durante cópia, estados idle/preparing/running/cancelling/finished, snapshot final e destruição precoce do job/observer. Erro de origem ausente não produziu o fluxo de erro neste libfm-qt; o teste usa destino que é arquivo regular, não diretório. Teste de permissão foi omitido para não depender de execução não-root nem de permissões de host variáveis.

## Pausa cooperativa delimitada

`Job::pause()`/`resume()` agora têm implementação para um caminho delimitado: `FileTransferJob::Mode::COPY` de exatamente um arquivo regular nativo para destino nativo, usando o callback de progresso de `g_file_copy()`. A cada callback, o motor alcança um checkpoint cooperativo e aguarda numa condition variable própria, sem manter o mutex dos contadores de progresso. `pause()` confirma apenas o pedido; o controller passa de `Pausing` a `Paused` somente após o callback reconhecer o checkpoint. `resume()` libera a espera. Cancelar acorda a condition variable e deixa GIO concluir a operação cancelada; pedidos repetidos e pedidos após `Finished` são no-op.

`supportsPause()` retorna falso para MOVE/LINK, múltiplas fontes, diretórios, caminhos não nativos, provedores remotos e jobs que não sejam `FileTransferJob`. Não se anuncia suporte geral para GIO: callback, frequência e latência continuam dependentes do provedor; o contrato medido aqui é somente para arquivo local regular com cópia via `g_file_copy()`. Um pedido não equivale a confirmação: consumidores devem aguardar snapshot `paused`/estado `Paused`. O novo marcador `LIBFM_QT_COOPERATIVE_COPY_PAUSE_API` identifica a API overlay; reconstruir aplicação e consumidores contra o prefixo privado é obrigatório por mudança ABI.

O teste de integração usa uma fixture local materializada de 2 GiB: solicita pausa antes do início do job, espera confirmação do checkpoint, confirma que o destino já contém bytes mas ainda não chegou ao tamanho final, verifica tamanho estável por pelo menos 350 ms e ticks do event loop responsivos, retoma e compara SHA-256 de origem/destino. Testes adicionais cancelam enquanto pausado, verificam uma notificação por cancelamento interno/externo, capability falsa para LINK, e no-op de pause/resume após conclusão. A cópia completa de 2 GiB e o caminho de erro pointer-based também rodam contra a biblioteca overlay real sem `qRegisterMetaType` artificial nos testes.
