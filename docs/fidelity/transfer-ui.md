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
recolhida; cancelar usa `reject()` da classe base, que encaminha cancelamento à
operação. Conflitos e erros continuam nos handlers base e no mesmo diálogo.

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
