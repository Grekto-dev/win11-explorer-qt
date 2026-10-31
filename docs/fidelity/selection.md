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

Não medidos na referência: clique/Ctrl/Shift, clique vazio, escape/cancelamento, seleção nas bordas e drag-and-drop. Em particular, não atribuir semântica a Ctrl+Shift+Escape/DND sem nova referência Windows. Nenhum delta apoiado em evidência foi encontrado nesta execução; não foi alterado `view.*`, `tabpage.*` ou libfm-qt.
