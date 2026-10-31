# Transferências: núcleo verificável e seam de apresentação

## Estado da evidência

Na worktree não havia `.state/fidelity/reference/phase2/<run-id>/TRANSFER_READY.json` na inspeção de 31/10/2026. Portanto esta entrega não escolhe layout, tempos, animação, cores nem estados visuais do Explorer da VM. Progresso nativo observado no Linux não é evidência de aparência Windows.

## APIs observadas no libfm-qt 2.4.0

- `Fm::FileTransferJob` é o motor de cópia real baseado em GIO (`g_file_copy`), preservando flags/metadados, consulta de conflitos e erros. Ele expõe `FileOperationJob` com totais, bytes concluídos, arquivo atual/progresso, `progress()`, sinais `finished`, `cancelled`, `error` e `fileExists`.
- `Fm::Job::cancel()` é implementado e aciona `GCancellable`; o trabalho é assíncrono por `runAsync()`. `pause()`/`resume()` estão declarados no header, mas não há implementação no source/build libfm-qt disponível, e `FileTransferJob` não tem checkpoints cooperativos. O controlador retorna `false` para ambas as ações; nenhuma pausa/retomada é simulada.
- `Fm::FileOperation` cria o mesmo `FileTransferJob`, mas conecta erro e conflito ao seu `FileOperationDialog`. O diálogo é campo privado (`dlg_`) sem setter nem API pública para trocar sua apresentação. `FileOperation::job()` oferece acesso de observação ao `FileOperationJob`, mas não transfere propriedade.
- `runAsync()` inicia thread sobre o objeto Job. Não destruir nem apropriar-se do job em execução: nesta implementação o controlador só observa ponteiro Qt guardado via `QPointer`, emite snapshots, e só solicita cancelamento. A lógica de vida útil continua do lado do proprietário do Job.
- Erros/conflitos são perguntas síncronas com parâmetros de resposta por referência e exigem política de UI conectada adequadamente (incluindo `Qt::BlockingQueuedConnection`). Não substituir o `FileOperationDialog` apenas por cima dele: isso duplicaria/esconderia os prompts ou poderia mudar `SKIP`/`OVERWRITE`/`RENAME`/`RETRY`/`ABORT`. Uma futura troca de apresentação requer uma composição explícita que mantenha esses handlers e ownership, após a evidência da VM.

## Núcleo implementado

`pcmanfm/explorertransfercontroller.*` é um observador fino para um `Fm::FileOperationJob` emprestado; amostra a cada 100 ms valores do motor sob seus locks e não calcula velocidade/ETA, não altera progresso, não cria outro motor e não toma ownership. É uma seam para integrar mais tarde o estado verdadeiro. Não está ligado ao aplicativo nesta onda: a apresentação substituta está bloqueada pela falta de referência e pelo encapsulamento/roteamento de erros do `FileOperation`.

## Teste nativo

Build independente, sem tocar CMake compartilhado:

```sh
source /home/pedro/win11-explorer-qt/.deps/env.sh
cmake -S tests/transfer -B .state/transfer-build \
  -DCMAKE_PREFIX_PATH="$SDK_PREFIX;$QT_ROOT" \
  -DLIBFMQT_SOURCE_DIR=/home/pedro/win11-explorer-qt/.deps/src/libfm-qt/src \
  -DLIBFMQT_INCLUDE_DIR=/home/pedro/win11-explorer-qt/.deps/prefix/include/libfm-qt6
cmake --build .state/transfer-build --parallel 2
QT_QPA_PLATFORM=offscreen .state/transfer-build/tst_explorertransfercontroller
```

O teste de integração gera uma fixture regular de 2 GiB escrevendo todos os bytes (não sparse), verifica alocação via `st_blocks`, confere espaço livre antes da geração, executa o `FileTransferJob` real em COPY, mede timestamps/tempo, SHA-256 de ambos os lados e preservação da origem. Há ainda teste de cancelamento via `GCancellable` no job real e teste de conflito que responde `SKIP` usando `BlockingQueuedConnection`, confirmando preservação de origem e destino existente; não simula pausa. Erros genéricos não foram exercitados nesta fatia porque exigem política de resposta/UI distinta. Durações observadas são do host Linux e não são alvo de paridade com VM. O teste de cópia não promete observar pausa/retomada ou UI: a taxa deste host foi alta e `pause/resume` não estão implementados no libfm-qt testado.
