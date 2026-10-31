# Menus e estados de interação — evidência Phase 2

Referência: `.state/fidelity/reference/phase2/20261031T0022-0300/`. Foram conferidos os 152 frames e os três ciclos. A ROI é 640×400 em `(480,240)` da tela (janela cliente 1123×592, origem `(194,162)`, DPI 96). Captura alcançou mediana 65,228 ms/frame (15,331 fps), com intervalos de 43,082–187,571 ms.

## O que a sequência mostra

Os eventos informativos são os alvos de entrada, não os rótulos inferidos do operador: clique em `(650,274)` cai no botão **Classificar**, e o popup dessa ação inclui Nome, Data de modificação, Tipo, Tamanho e opções de ordem/agrupamento. Movimento/clique em `(856,274)` cai em **Mais**; o popup observado inclui Inverter seleção, Propriedades e Opções. Ambos são repetidos em três ciclos, com Escape fechando o popup. O rótulo `menu-1` do arquivo de análise não identifica sozinho qual menu abriu.

Na imagem da referência, os popups são superfícies claras quase brancas, texto escuro, contorno cinza fino, cantos arredondados, itens compactos e separadores sutis. O item sob hover é um cinza muito claro. As cores CSS aqui são aproximações calibráveis da observação visual; o conjunto não permite afirmar valores RGB exatos nem uma curva de easing.

## Implementação

`pcmanfm/exploreranimation.{h,cpp}` contém o helper reutilizável para QMenu e QToolButton. O menu usa fundo `#f9f9f9`, texto `#202020`, borda `#d7d7d7`, raio 8 px, padding 4 px; itens com padding compacto, hover `#f0f0f0`, disabled `#a0a0a0` e separador `#e2e2e2`. Controles têm hover `#f3f3f3`, pressed `#e5e5e5`, checked e foco visível azul. São seletores de estado nativos Qt; QAction, QMenu, foco, navegação por teclado e Escape permanecem responsáveis pela interação.

Não foi introduzida animação temporizada: os frames não sustentam duração/curva de hover, pressão, abertura ou fechamento abaixo da resolução de captura, e parte dos deltas ocorre em um único frame. Os estados mudam imediatamente e cancelam hover/pressed pelo mecanismo normal de eventos Qt ao sair/soltar.

## Integração pendente

A frente não alterou CMake compartilhado conforme contrato. Integrador deve adicionar `pcmanfm/exploreranimation.cpp` à lista de fontes do target `pcmanfm-core` (ou target comum que compila `explorerchrome.cpp`) e incluir `exploreranimation.h` nos headers do target. Teste standalone:

```sh
source /home/pedro/win11-explorer-qt/.deps/env.sh
cmake -S tests/animation -B .state/animation-test -G Ninja -DCMAKE_MAKE_PROGRAM="$NINJA_BIN"
cmake --build .state/animation-test --parallel 1
QT_QPA_PLATFORM=offscreen .state/animation-test/tst_exploreranimation
```

Essa suíte valida estilo e preservação das ações/toggle QToolButton, mas não substitui timeline visual do app completo. É necessária uma captura Linux xcb do popup no mesmo ROI/estado/tamanho após o wiring do integrador para comparar deltas por frame; não declarar paridade temporal a partir das médias globais.
