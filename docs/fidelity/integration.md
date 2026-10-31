# Integração da fase 2 — candidato local

## Wiring de produção

`pcmanfm-core` inclui os registros de ícones, o helper de estados de menus/botões, o controller e o diálogo de transferência. O resource `explorer_icons` é inicializado para o link estático; QAction, modelos e operações continuam sendo os reais do libfm-qt. A lateral mantém `Fm::PlacesView`; a decoração própria não substitui seu modelo.

`application.cpp` registra a fábrica `createExplorerTransferDialog` antes das operações. O diálogo recebe fonte, bytes, progresso e conclusão do job real. Pausa só é anunciada pelo overlay para Copy de um único arquivo regular nativo local. O botão aguarda ACK; pedir pausa não significa que o checkpoint já parou. MOVE, LINK, diretórios e provedores remotos não têm essa capability.

## Build e ABI

Em 2026-10-31, a retomada reconstruiu o overlay ausente e o aplicativo com `./scripts/build.sh Debug`, limitado a dois jobs por etapa, usando o SDK somente para leitura. Instalações ficam em `.state/libfm-overlay-prefix` e `.state/install` desta worktree. O script valida a revisão upstream `4294f29ed314388f1f6d051ae0128320354e417a` e os hashes antes dos patches; LGPL e avisos upstream são preservados.

Fonte em `.state/overlay-source/libfm-qt/src`, headers instalados, flags de compilação, link e `ldd` foram conferidos. `libfm-qt6.so.17` resolve no overlay privado desta mesma worktree, nunca na biblioteca ABI-incompatível do SDK. O intérprete CMake é `/usr/bin/python3`, com Pillow/Xlib.

## Testes executados na retomada

O runner existe: `uv tool run --from cmake ctest --test-dir .state/build --output-on-failure -V`. Não depende de `ctest` estar diretamente no PATH. O registro final contém dez entradas CTest, todas aprovadas, incluindo `transfer-controller-heavy` e a segurança do wrapper de seleção. O opt-in XTEST fica pulado na execução offscreen, e foi executado separadamente em Xephyr `:17` (três casos QtTest contando init/cleanup, nenhum skip).

A suíte pesada passou os 18 casos QtTest contando init/cleanup, sem skips: wake de GCancellable cancelado diretamente durante pausa, attach tardio, conflito SKIP, erro, cancelamento e cópia/hash real de 2 GiB. O teste da UI cria uma `Fm::FileOperation::Copy` real com fonte materializada de 4 GiB, encontra o diálogo de produção, clica pausa, espera “Retomar”, verifica tamanho parcial estável por 300 ms, retoma e confere tamanho final. Não é uma operação fake. Foi repetido em XCB `:15`; a captura mostra “Retomar” e progresso recebido do motor, não valores atribuídos pelo teste. Isso verifica a classe de diálogo usada em produção sob QtTest; não é uma medição dos controles Windows nem um E2E de pausa acionado pelo executável standalone.

A suíte standalone de menus passou quatro casos QtTest contando init/cleanup. Menus têm estados Qt estáticos refinados, não animações temporizadas perfeitamente replicadas. A referência de cerca de 15 fps não sustenta curvas exatas.

O executável de produção iniciou frio em Xephyr `:15`, com Metacity, D-Bus e XDG privados. Os sete checks existentes passaram: nova aba/fechar, F2, copiar com igualdade de conteúdo, recortar com remoção da origem, nova pasta, arraste nativo da barra de título e redimensionamento nativo pelo canto. Capturas da seleção (detalhes/grid), janela normal e diálogo pausado, logs, comandos e contagens estão em `.state/final-proof/` local, fora do Git. Os novos estados SVG pendentes de outra frente não integram este candidato.

## Isolamento e diferenças restantes

A baseline original da árvore principal foi preservada. Sua única divergência em relação à baseline suplementar de retomada é o hash de `.config/nemo/desktop-metadata`, já diferente antes dos testes desta retomada; não há evidência para atribuir causa. Nada foi restaurado ou sobrescrito para esconder a diferença. Handler de pastas, dono FileManager1, applets, configuração de ícones e arquivos MIME continuam estáveis; a verificação suplementar passa antes e depois.

Aparência é aproximação Qt, não paridade completa. Sem curvas temporais exatas, sem pausa Windows medida, sem cobertura de acessibilidade equivalente; a pausa cooperativa permanece restrita. Avisos GVFS/portal/FUSE da sessão isolada estão preservados nos logs e não foram classificados como falhas de testes que terminaram com zero. Não houve instalação no sistema, troca de defaults nem publicação dos artefatos privados.
