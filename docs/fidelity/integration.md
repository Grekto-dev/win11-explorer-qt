# Integração da fase 2 — aplicação e CTest

## Wiring de produção

`pcmanfm/explorericons.cpp` integra `pcmanfm-core`; o único qrc de produção continua sendo `explorer_icons`, criado pelo glob recursivo já existente em `pcmanfm/CMakeLists.txt`. O registro inicializa esse resource para o link estático. `explorerchrome.cpp` prefere SVG do registro para os IDs observados (Novo, cortar/copiar/colar, renomear, excluir, classificar, visualizar e mais); IDs sem arte correspondente continuam no desenho original. Ações/conexões e enabled state são preservados.

A lateral mantém modelo, proxy, caminhos e comportamento de `Fm::PlacesView`. Um `QStyledItemDelegate` recebe os QTreeViews da side pane, tenta trocar somente a decoração por um SVG se o nome de tema existir no registro e delega a pintura normal nos demais casos. O tamanho configurado permanece 16×16. Os comandos de navegação/janela e outros sem SVG medido não foram inventados. Compartilhar não foi acrescentado como comando, pois não havia QAction existente equivalente.

O teste `chromeHasWindowsStructure` compara renderizações da toolbar com os SVGs esperados e contém verificação estrutural/layout. A mutação que forçou `LineIcon` no lugar dos SVGs fez esse teste falhar; após restauração, passou.

## Build isolado e captura

Comando de configuração (SDK via ambiente local; build e prefix são privados à worktree):

```sh
source /home/pedro/win11-explorer-qt/.deps/env.sh
cmake -S . -B .state/work-integration -G Ninja \
  -DCMAKE_MAKE_PROGRAM="$NINJA_BIN" -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_INSTALL_PREFIX="$PWD/.state/install-integration" \
  -DCMAKE_MODULE_PATH="$SDK_PREFIX/share/cmake/lxqt2-build-tools/find-modules" \
  -DXKB_INCLUDE_DIR="$SYSROOT/usr/include" \
  -DXKB_LIBRARY="$SYSROOT/usr/lib/x86_64-linux-gnu/libxkbcommon.so"
cmake --build .state/work-integration --parallel 2
```

Build completo concluído com Qt 6.8.3. O binário de produção `pcmanfm/win11-explorer-qt` foi executado com uma pasta fixture em Xephyr `:15` (1104×642), com Metacity e D-Bus de sessão isolados. Captura real da tela: `.state/proof-integration/production-app-xephyr.png`. QtTest também foi executado em XCB no mesmo display e salvou `.state/proof-integration/explorer-window.png` e `layout.json`. A captura real mostra o layout integrado; ícones de bookmarks (Documentos, Músicas e Imagens) usam os SVGs. Estados pressed/disabled não foram validados por falta de evidência medida equivalente.

## Testes/CTest

`tests/CMakeLists.txt` registra `explorer-icons`, `fidelity-harness`, `reference-capture-unit` e `selection-baseline`, além das suites preexistentes. `tests/icons` é um executável independente, compilando sua cópia de resource apenas no próprio processo; não há duplicação de qrc no `pcmanfm-core` ou no executável de produção.

A suite real de transferência de 2 GiB só é compilada/registrada com `-DENABLE_HEAVY_TRANSFER_TESTS=ON`, tem labels `heavy;transfer` e `RUN_SERIAL`; ficou desligada e não foi executada por segurança de espaço. Não integra diálogo nem simula pause/resume.

Executados diretamente (todos passaram): QtTest UI 11/11; QtTest icons 6/6; harness 9/9; captura de referência 2/2; probe de seleção confirmou somente ownership de libfm (não comportamento); isolamento 9/9; packaging 6/6; CLI 2/2. O teste dedicado da chrome passou também em XCB no display isolado.

O ambiente não contém executável `ctest` (não encontrado nem após carregar `.deps/env.sh`), portanto não foi possível invocar o runner CTest. A configuração CMake gerou os `CTestTestfile.cmake` com os testes listados; os mesmos executáveis/comandos foram rodados diretamente. O executável `ctest` continua sendo a pendência de execução CTest.

Avisos de teardown GVFS/portal/FUSE ocorreram na sessão D-Bus isolada por mounts indisponíveis; o processo e os testes terminaram com status zero. Nenhuma instalação foi feita no sistema ou na instalação ativa.
