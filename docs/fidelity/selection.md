# Prova interativa de seleção — fase 2

## Execução Linux/XTEST (2026-10-31)

**Resultado:** os gestos reais de retângulo reverso funcionaram em detalhes e em ícones grandes; os três itens da fixture (`Nested`, `Notes.txt`, `Readme.txt`) ficaram selecionados durante e após o gesto. Não foi necessário mudar código de produção. Isso verifica o comportamento nativo existente de libfm-qt nesta execução, não estabelece paridade Windows para gestos não observados.

### Ambiente e reprodução

- Worktree `phase2/selection`, executável compilado desta worktree em `.state/install/bin/win11-explorer-qt`, Qt 6.8.3/libfm-qt 2.4 local.
- Xephyr isolado `:12`, 1104×642, metacity; entrada real XTEST via `/usr/bin/python3` com `python-xlib` do sistema. Nenhum evento foi enviado a `:0`.
- Instância em sessão D-Bus isolada, perfil/config/cache/data separados sob `.state/selection-proof/`.
- Fixture isolada: `Nested/Notes.txt`, `Notes.txt`, `Readme.txt`.
- Ação: arrastar com botão esquerdo mantido do vazio `(950,500)` até `(181,171)` em detalhes; depois alternar por **Visualizar → Visualização em ícones** e arrastar do vazio `(900,500)` até `(180,140)`. Captura held após ~0,9 s; outra captura após soltar.
- Execução do aplicativo: `source .deps/env.sh`; `DISPLAY=:12 dbus-run-session -- .state/install/bin/win11-explorer-qt --profile selection-proof <fixture>` com `QT_QPA_PLATFORM=xcb`, `QT_STYLE_OVERRIDE=Fusion` e `XDG_*` apontando ao diretório privado. O Xephyr e o WM devem ser iniciados antes.

### Evidência e medição

Artefatos de execução (ignorados pelo Git): `.state/selection-proof/` — `details-3-held.png`, `details-3-released.png`, `grid-held.png`, `grid-released.png`, `grid-before.png` e logs. Os PNGs são capturas reais do root de `:12`, decodificadas como BGRX; a fixture foi criada nesta execução.

- **Detalhes:** a captura mantida mostra o retângulo translúcido e as três linhas destacadas; a barra de status mostra “3 itens selecionados”. Após soltar, as três permanecem selecionadas e a barra mantém a contagem.
- **Ícones grandes/grade:** os três rótulos aparecem destacados dentro do retângulo mantido; após soltar, a seleção persiste e a barra mostra “3 itens selecionados”.
- **Referência Windows** (`.state/fidelity/reference/phase2/20261031T0003-0300/interactions/05_details-drag-held.png`): amostra RGB do preenchimento `(170,204,238)`; pixels exatos dessa cor ocupam bbox `(354,326)–(1049,479)`. Borda azul amostrada `(0,120,212)` no topo. Após soltar, manifesto informa `Nested`, `Notes.txt`, `Readme.txt` selecionados.
- **Referência Windows grade** (`08_grid-drag-held.png`): mesmo preenchimento RGB `(170,204,238)`, bbox segmentado `(354,301)–(849,519)`; três itens selecionados segundo a captura/manifesto.
- **Linux detalhes, captura mantida:** fill do marquee fora das linhas em `(233,240,245)`; bbox dos pixels dessa cor `(184,174)–(947,497)`. Contorno amostrado na borda esquerda `(193,210,224)`.
- **Linux grade, captura mantida:** fill `(233,240,245)`, bbox `(183,143)–(897,497)`. As linhas selecionadas e o status mostram 3 itens.

Cores são valores RGB efetivamente amostrados em PNGs, não alpha intrínseco: a captura não contém camada alpha do compositor para inferir opacidade. Bboxes são de pixels exatos de preenchimento; pixels de borda/itens selecionados podem não pertencer ao conjunto. Resoluções, tamanho/posição da janela e pontos finais dos gestos diferem da VM; portanto os bboxes não são apresentados como comparação de geometria normalizada nem como tolerância de paridade. O dado diretamente comparável aqui é seleção de três nomes nos dois modos; as cores observadas também ficam registradas sem alegação de igualdade.

### Limites

A observação foi interativa: XTEST enviado ao app real, captura held/released e estado de seleção visível na UI, incluindo contagem real na barra. Não foi incluído um accessor de teste que leia diretamente `selectedFiles()`/paths em memória; nomes foram conferidos visualmente nas capturas e no manifesto Windows. Não houve mutação de filesystem por esses gestos.

Não medidos nesta primeira referência: clique/Ctrl/Shift, clique vazio, escape/cancelamento, seleção nas bordas e drag-and-drop. Este relato é histórico: a rodada abaixo exercita separadamente os quatro casos já associados à referência posterior, sem ampliar a cobertura da primeira captura.

## Correções da revisão S1/L1/L2/L3/N1 (2026-10-31)

Worktree de correção: `.state/worktrees/fix-ui-review`, base `785bc3b`. Evidências desta rodada ficam na `.state/` dessa worktree, não na `.state/selection-proof/` histórica.

- L1: substituição do ícone em `initStyleOption`, depois da base Qt. A regressão pinta efetivamente um `DecorationRole` magenta com nome conhecido e compara a imagem com a pintura padrão usando o SVG esperado. Verifica também preservação do papel de caminho e do ícone do modelo e fallback para nome desconhecido. Os aliases da lateral traduzem papéis existentes (Home, documentos, downloads, mídia, computador, rede, unidade óptica, lixeira), sem criar caminhos, serviços, contas ou uma entrada OneDrive.
- L2: cada child view reutiliza seu único proxy de estilo próprio, localizado entre os filhos diretos mesmo quando QSS envolve `style()`. Repetição, navegação e alternância detalhes/ícones preservam esse limite; o estilo global continua vivo e intacto. A regressão de pintura continua verificando separadamente fill RGB `(170,204,238)` e borda `(0,120,212)`.
- S1: ambos os wrappers convergem para `run_selection_x11.py`; o display é configurável, padrão `:18`, com recusa de `:0`, `:1`, variantes não canônicas, display ocupado e reserva concorrente. `flock` em arquivo persistente no TMPDIR comum é adquirido antes da sondagem/inicialização. Não se remove o arquivo de lock, para não trocar seu inode. O runner exige Xephyr próprio vivo, PID/starttime/parent corretos, executable Xephyr, display no cmdline e peer UNIX `SO_PEERCRED` correspondente antes do WM/input. O binário QtTest valida esses dados e a ancestralidade do wrapper antes de criar QApplication e novamente na conexão de entrada efetiva. Não é sandbox contra código malicioso do mesmo UID.
- L3: cleanup por escopo em cada marquee sintético/XTEST e nos helpers de clique/DND; conclusão explícita da lambda e curto-circuito impedem o próximo gesto após retorno prematuro. Regressões injetam retorno de falha e exceção depois de pressionar mouse/Ctrl/Shift: o sintético observa os eventos de release e o XTEST consulta o estado real do servidor, exigindo botões/modificadores soltos e uma única tentativa. O cleanup do wrapper é apenas uma proteção adicional contra timeout.
- N1: a seleção é lida novamente após release nos dois modos e gravada em `selection-names.json`, além das capturas held/released.

### Execução e resultados reais

Reprodução, após o overlay privado já compilado:

```sh
source .deps/env.sh
cmake --build .state/build --parallel 2
uv tool run --from cmake ctest --test-dir .state/build --output-on-failure
LD_LIBRARY_PATH="$PWD/.state/libfm-overlay-prefix/lib:$LD_LIBRARY_PATH" \
  /usr/bin/python3 tests/run_selection_x11.py \
  --binary .state/build/tests/tst_explorer --display :18 \
  --artifacts .state/native-proof-2 \
  --case nativeOwnershipRejectsMismatches \
  --case nativeInputFailureCleanup \
  --case nativeXTestSelectionGestures \
  --case nativeExplorerGesturesMatchWindows
```

`ctest.log`: **10/10 testes passaram**, incluindo `explorer-ui`, wrapper safety e a transferência pesada. A suíte offscreen pula os casos nativos por desenho; a prova separada `native-proof-2/runner-result.json` registra **exitCode 0 nos quatro casos nativos**. `ldd` resolveu libfm-qt para o overlay privado desta worktree.

REDs efetivamente executados: `ui-red.log` (pintura não substituída e três proxies onde deveria haver um); `places-alias-red.log` (user-home sem mapeamento); `cleanup-red.log` (mouse sintético permaneceu pressionado); `ownership-direct-red.log` (binário direto aceitou ausência de ownership); `drop-red.log` (drag direito indevidamente consumido). `wrapper-red.log` mostrou a ausência de exclusão; o caminho de Xephyr morto também excedeu o timeout no wrapper antigo. Os verdes correspondentes e o CTest estão na mesma `.state/`.

O primeiro ensaio com restrição ingênua `buttons()==LeftButton` falhou no DND real (`native-proof/`, `drop-probe/`). O trace registrou DragMove com botão 1 e Drop com botão 0: no XDND, o Drop chega após a soltura. A correção mantém somente a identidade das URLs de um DragMove esquerdo elegível, consome/limpa essa memória em cada movimento/drop e em enter/leave/troca de modo e revalida o destino/filesystem no drop. Nenhum novo motor: MOVE/COPY continuam em libfm-qt. Modificadores, botão direito/ausente sem origem esquerda observada, URLs inválidas/não locais, alvo não diretório e outro filesystem não entram no atalho de MOVE. O teste negativo usa somente fixtures próprias, inclusive uma temporária em `/dev/shm` para comprovar devices distintos.

`native-proof-2/selection-names.json` contém `Nested Notes.txt` e `Readme.txt`, durante e após release, em detalhes e ícones. `native-gesture-comparison.json` contém os nomes selecionados nas etapas Ctrl-toggle/Shift-range/clique vazio e os SHA-256 lidos dos arquivos reais no MOVE e Ctrl-COPY; as capturas de cada etapa estão ao lado. A origem do MOVE desaparece; a do COPY permanece; hashes de destino/origem são comparados no próprio QtTest.

### Limitações e revisão pendente

Não há aprovação independente nesta entrega. Não foram exercitados autoscroll, Escape/cancelamento de seleção, acessibilidade, modos adicionais ou novos gestos Windows, nem medida paridade temporal/geométrica. A inspeção visual independente das novas capturas ainda cabe à integração; os resultados aqui são verificações programáticas e capturas produzidas, não aprovação visual.

A baseline histórica principal de isolamento apresentou diferenças em applets, `mimeapps.list` e metadados de desktop Nemo na consulta após os ensaios. Não foi regravada nem "corrigida"; não houve snapshot anterior desta rodada que permita atribuir essas diferenças. Esta worktree não modifica esses arquivos/configurações. O pai deve reconciliar a baseline antes de declarar isolamento global aprovado. Nenhum input foi enviado ao host: Xephyr usa o display herdado apenas para hospedar sua janela, e todos os eventos injetados desta rodada usaram o servidor próprio `:18`.
