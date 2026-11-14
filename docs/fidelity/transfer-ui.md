# Diálogo de progresso de transferências

A apresentação `ExplorerTransferDialog` é uma subclasse da classe real
`Fm::FileOperationDialog`; ela não cria nem substitui o motor GIO. O registro é
opt-in: o integrador pode chamar `Fm::FileOperation::setDialogFactory(&createExplorerTransferDialog)`
antes de iniciar operações e deve restaurar `nullptr` quando não houver jobs.
A integração registra essa fábrica na inicialização da aplicação; o diálogo e o controller pertencem ao target de produção.

A camada visual oculta os widgets originais, sem destruí-los, e acrescenta o
resumo, nome atual, barra e seção de detalhes ao layout existente. Os setters
virtuais usados pelo libfm-qt recebem percentuais/bytes/contagens/estimativa do
próprio motor. Detalhes só mostram bytes transferidos de verdade e uma
estimativa fornecida pelo libfm-qt. Não há velocidade inventada. A seção inicia
recolhida; cancelar usa `reject()` sobrescrito (ver "Cancelando" abaixo), que
pede o cancelamento ao backend. Conflitos e erros continuam nos handlers base e no mesmo diálogo.

A referência `20261031T0003-0300/paste_2s.png` mostra a cópia de 2 GiB em estado
recolhido: resumo com origem/destino, percentual no título, barra verde,
“Mais detalhes” e botão de cancelar. A imagem foi amostrada em 2,984 s e já não
estava visível em 6,259 s; isso não determina o tempo preciso de conclusão.
A aparência desta implementação é uma aproximação Qt, não paridade pixel a
pixel. Os controles Windows não foram exercitados. O upstream libfm-qt 2.4.0 não
oferece pausa, mas o overlay privado acrescenta checkpoint cooperativo somente
a Copy de um arquivo regular nativo local. O botão aguarda ACK do job antes de
mostrar “Retomar”; outros modos não anunciam capability.

## Verificação nesta frente

`transferDialogFactoryBuildsCollapsedRealProgressUi` instancia a fábrica e o
widget real com uma fixture local pequena de 10 bytes (deliberadamente apenas
para checar setters/layout; a percentagem atribuída manualmente é fixture, não
prova de operação). O mesmo teste então registra a factory opt-in, executa
`FileOperation` real para copiar esses bytes e compara o conteúdo do destino.
O conjunto existente de transferências também exercita cópia real/hash de 2 GiB,
cancelamento, conflito com resposta `SKIP` e erro via `FileOperation` real e
fábrica probe. Na integração de 2026-10-31,
`liveOperationPauseButtonWaitsForBackendAck` também materializou 4 GiB, iniciou
`Fm::FileOperation::Copy` real, clicou no botão visível, esperou ACK/“Retomar”,
conferiu tamanho parcial estável por 300 ms e retomou até o tamanho final.
A captura `.state/final-proof/paused-real-xcb.png` foi feita em Xephyr `:15`
durante essa operação real, usando a classe de diálogo de produção sob QtTest.
Não é E2E de pausa no executável standalone nem teste de controles Windows.
As capturas antigas de setters/layout continuam sendo fixtures estáticas;
não devem ser confundidas com essa captura viva. O atraso de exibição de
1.000 ms não foi removido. Veja `integration.md` para o gate completo.

## Fase 3: alinhamento com a referência Windows medida

Medições e nomes dos quadros usados: `transfer-dialog-measurements.md`. A
tabela comportamental está em `transfer-controls-reference.md`.

Implementado em `ExplorerTransferDialog` (sem tocar o motor/`patches/`):
- **Títulos** (janela e linha do percentual): `N% concluído`;
  `Pausado - N% concluído` somente após ACK real do backend
  (`State::Paused`, ou `Pausing` com `paused` verdadeiro); `Cancelando - N%
  concluído` assim que o cancelamento é solicitado. O diálogo **não** é
  escondido ao cancelar: `reject()` é sobrescrito (o da base esconde de
  imediato). Ele pede o cancelamento ao controller (`cancel()` devolve se o
  backend o aceitou) ou ao job (`requestCancellation()`); só entra em
  "Cancelando" se o pedido foi aceito. Sem nada a cancelar (job já terminado
  ou destacado), cai em `QDialog::reject()` e o diálogo pode ser fechado. O
  `FileOperation` o destrói quando o job realmente termina (`onJobFinish`).
  Mover/lixeira/excluir também ficam em "Cancelando" até o job acabar; não há
  saída se o GIO travar (lacuna conhecida).
- **Barra**: azul-clara `#60CDFF` em andamento, amarela `#FCE100` pausada ou
  cancelando (valores lidos dos pixels; o verde `#16833b` antigo foi removido).
- **Botão inline** ⏸/▶ à esquerda do ✕ (glifos desenhados com `QPainter`, sem
  fonte/recurso Microsoft; `accessibleName`/dica carregam "Pausar", "Pausando…",
  "Retomar"). `Pausando…` fica desabilitado: nenhum "Retomar" antes do ACK. Só
  aparece se o job anunciar `supportsPause()`; sem capability o botão fica oculto
  (inclusive Move/Trash/Delete). Cancelando: ▶ esmaecido `#797979`.
- **Cancelar sem confirmação**, também estando pausado (o controller acorda o
  worker; ver `liveDialogCancelWhilePausedShowsCancellingWithoutConfirmation`).
- **Detalhes**: gráfico de velocidade (`SpeedSample{fraction, bytesPerSecond, intervalSeconds}`; ≤512 amostras por
  decimação que conserva os bytes)
  alimentado só por `recordSpeedSample(bytes, total, ms)`, chamado a partir de
  `setDataTransferred` (bytes reais do libfm). Sem dados = fundo e grade, sem
  curva. Intervalos sem bytes novos (pausa, estagnação) não geram amostra zero
  inventada nem avançam o relógio da linha de base: a próxima taxa é a média
  honesta do intervalo todo. Ao retomar a linha de base reinicia; contadores que
  voltam atrás rebaseiam; um total diferente descarta a série.
  Linhas `Nome:`, `Tempo restante:` ("Calculando…" até o libfm informar uma
  estimativa > 0) e `Itens restantes: N (tamanho)`.
- **Nomes de arquivo são entrada hostil**: caracteres de controle, quebras de
  linha e de formatação/bidi (U+202A–202E, U+2066–2069, U+200E/F…) viram U+FFFD
  antes de qualquer exibição; o texto vai escapado para o resumo (RichText) e
  os tooltips são `<span>` com o texto escapado (o Qt trata ToolTip como rich
  text se parecer HTML, independente de `setTextFormat`). Teste:
  `transferDialogToolTipsAndLabelsNeutraliseHostileFileNames`.
- **Tamanhos** (cliente Qt, DPR 1, 96 DPI): 449×165 recolhido, 449×287
  expandido; centros do ⏸/▶ e ✕ em (366; 72,5) e (413; 72,5) nos dois estados.

### Aproximações Qt (não são paridade pixel a pixel)
- O tamanho Windows medido é o retângulo visível DWM, que inclui a barra de
  título (≈32 px, coberta pelo banner da VM; altura não medida). Aqui o
  cliente Qt tem 449×165/287 e a decoração vem do gerenciador de janelas, então
  a faixa superior do cliente é espaçamento vazio e a janela inteira com
  decoração é maior que a do Windows.
- Gráfico em andamento (azul), cor do texto-resumo, tipografia (sem Segoe UI),
  hover dos botões e escala vertical da curva não foram medidos; o desenho
  azul usa a mesma construção observada em amarelo.
- "Itens restantes" usa o número de itens de origem quando o libfm não envia
  contagem de arquivos (a cópia por tamanho não chama `setFilesProcessed`).
- Formato do "Tempo restante" (`1 min 15 s`) não foi observado no Windows.

- Revisão ciclo 1 (resumo): `#D0D0D0` do resumo escolhida (não medida); eixo X do gráfico = fração de bytes, não
  tempo; nomes elididos (resumo e "Nome:") em vez de word-wrap, tooltip com o texto completo; foco por teclado
  desenhado só com Tab; Esc/✕ cancelam sem confirmação (Esc não observado no Windows); tema escuro forçado;
  quadros de referência fora do Git (medições não reproduzíveis sem a VM); `grafico_cor_curva` ficou `diferente`
  pela regra "sem platô à esquerda" (primeira amostra em 5 %), não por regressão de renderização; gráfico azul
  não medido.
- Lacuna de backend: a pausa cooperativa só confirma o ACK depois que o glib termina cada `copy_file_range` (até
  2 GiB−4 KiB; arquivos menores só no fim) — limitação do backend, não do diálogo; o Windows confirmou em ≤ ~610 ms.

### Não verificado
Cancelar sem pausar no Windows; texto de velocidade com a cópia ativa;
comportamento com 5–8 fps reais; DPI ≠ 96; E2E com entrada real no executável
(nenhuma entrada automatizada foi usada nesta frente).

### Testes
`tests/tst_explorer.cpp`: `transferDialog*` (dialog estático offscreen; a lista
cresce, consulte o arquivo em vez de contar aqui). `tests/transfer/tst_explorertransfercontroller.cpp`: o teste vivo
existente agora usa `accessibleName()`/glifo/título no lugar do texto do botão
(botão agora é só ícone) e foi adicionado
`liveDialogCancelWhilePausedShowsCancellingWithoutConfirmation`.
