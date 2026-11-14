# Controles do diálogo de cópia — referência Windows medida (2026-11-13)

Evidência (fora do Git, na máquina do desenvolvedor):
`.state/fidelity/reference/phase2/controls-20261113T235224Z/` (manifesto com
sha256 de 504 artefatos; `input_events.json` com os 18 eventos de entrada;
473 quadros PNG) e a calibração `calib-20261113T225654Z/`. Scripts do operador
no ramo local `reference-copy` (commit `1658b25`), ainda não integrados.

Ambiente: VM Windows (build 26300 reportado), pt-BR, 1440×900, 96 DPI, tema
escuro, cópia de um arquivo task-owned de 3 GiB, ~245 MiB/s. Entrada enviada
pelo Windows-MCP (SendInput), não pelo teclado QEMU. As latências abaixo são
**limites superiores** (incluem ~0,6 s da chamada MCP).

## Identidade do diálogo
- Classe `OperationStatusWindow`, hospedada pelo Explorer do **shell**
  (sem `/factory`), nunca pelo processo da pasta aberta. O HWND é permanente
  e fica oculto entre operações (o título pode ficar velho).
- Durante a cópia o título da janela é o progresso: `NN% concluído`,
  `Pausado - NN% concluído`, `Cancelando - NN% concluído`.
- Tamanho visível (DWM): recolhido 449×165; expandido 449×287. `GetWindowRect`
  é maior em 7/0/7/7 px (L/T/R/B).

## Estados observados
| Ação | Observado |
|---|---|
| Em andamento | barra azul-clara, glifo ⏸ à esquerda do ✕; corpo "Copiando 1 item de src para dst1" |
| Pausar (clique no ⏸) | título `Pausado - 32% concluído`, barra amarela, glifo ▶; percentual estagnado por 45,6 s (cópia B) e 15,4 s (C); ACK ≤ ~610 ms |
| Mais detalhes (pausado) | expande para 449×287: gráfico de velocidade (área amarela quando pausado), `Nome`, `Tempo restante: Calculando…`, `Itens restantes: 1 (2,03 GB)`; o botão vira "Menos detalhes" |
| Menos detalhes | volta ao tamanho recolhido |
| Retomar (▶) | título `39% concluído`, barra azul, glifo ⏸; ≤ ~427 ms; progresso até 100%; sha256 destino = fonte |
| Cancelar (✕) estando pausado | título `Cancelando - 53% concluído`, glifo ▶ esmaecido; **sem diálogo de confirmação**; janela some ~1,4 s depois; o destino parcial foi removido pelo Explorer (pasta vazia) |

Os controles de pausa/✕ não mudam de posição ao expandir (centros a ~(862,224)
e (909,224) em coordenadas de tela na janela medida).

## Não verificado
- Cancelar sem pausar; velocidade em texto com a cópia ativa e detalhes
  expandidos; ícone do diálogo; por que o percentual passou de 50% para 53%
  entre pausa e cancelamento; comportamento com taxa de atualização real
  (capturas ~5–8 fps); outras versões/temas do Windows.
- A cor da barra em andamento nesta captura é azul-clara; a referência de
  2026-10-31 descrevia barra verde. Não foi investigado o motivo.

## Lacunas conhecidas do `ExplorerTransferDialog` (Linux), a tratar
Ainda não comparado quadro a quadro. Pontos a verificar contra a tabela:
título `Pausado - N% concluído` e `Cancelando - N% concluído`; barra amarela
quando pausado; botão de pausa inline ao lado do ✕ com glifos ⏸/▶; gráfico de
velocidade e as três linhas de detalhes; ausência de confirmação ao cancelar;
dimensões 449×165/449×287; "Cancelando" sem fechar antes do job terminar.
