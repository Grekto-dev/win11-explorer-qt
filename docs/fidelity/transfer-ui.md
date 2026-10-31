# Diálogo de progresso de transferências

A apresentação `ExplorerTransferDialog` é uma subclasse da classe real
`Fm::FileOperationDialog`; ela não cria nem substitui o motor GIO. O registro é
opt-in: o integrador pode chamar `Fm::FileOperation::setDialogFactory(&createExplorerTransferDialog)`
antes de iniciar operações e deve restaurar `nullptr` quando não houver jobs.
Esta worktree não muda a inicialização global nem a ligação da fábrica no app.

A camada visual oculta os widgets originais, sem destruí-los, e acrescenta o
resumo, nome atual, barra e seção de detalhes ao layout existente. Os setters
virtuais usados pelo libfm-qt recebem percentuais/bytes/contagens/estimativa do
próprio motor. Detalhes só mostram bytes transferidos de verdade e uma
estimativa fornecida pelo libfm-qt. Não há velocidade inventada. A seção inicia
recolhida; cancelar usa `reject()` da classe base, que encaminha cancelamento à
operação. Conflitos e erros continuam nos handlers base e no mesmo diálogo.

A referência `20261031T0003-0300/paste_2s.png` mostra a cópia de 2 GiB em estado
recolhido: resumo com origem/destino, percentual no título, barra verde,
“Mais detalhes” e botão de cancelar. A imagem foi amostrada em 2,984 s e já não
estava visível em 6,259 s; isso não determina o tempo preciso de conclusão.
A aparência desta implementação é uma aproximação Qt, não paridade pixel a
pixel. Os controles Windows não foram exercitados; pausa não é oferecida porque
o motor libfm-qt 2.4.0 não implementa pausa cooperativa. O método do observer
continua reportando capability indisponível.

## Verificação nesta frente

`transferDialogFactoryBuildsCollapsedRealProgressUi` instancia a fábrica e o
widget real com uma fixture local pequena de 10 bytes (deliberadamente apenas
para checar setters/layout; a percentagem atribuída manualmente é fixture, não
prova de operação). O mesmo teste então registra a factory opt-in, executa
`FileOperation` real para copiar esses bytes e compara o conteúdo do destino.
O conjunto existente de transferências também exercita cópia real/hash de 2 GiB,
cancelamento, conflito com resposta `SKIP` e erro via `FileOperation` real e
fábrica probe; esses cenários verificam o seam e o filesystem, não demonstram
ainda o diálogo visual durante uma cópia longa. A cópia de 2 GiB neste host
terminou em cerca de 0,8 s e não justifica remover o atraso de exibição de
1.000 ms nem inventar progresso. Capturas do widget são layout estático, não
telemetria viva.
