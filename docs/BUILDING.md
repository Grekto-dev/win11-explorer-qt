# Compilação e ambiente de desenvolvimento

## Dependências e SDK

O projeto requer Qt **6.6 ou superior**, libfm-qt **2.4** e lxqt-build-tools **2.4**, com CMake e Ninja. O script `scripts/build.sh` cria um **overlay privado** de libfm-qt dentro de `.state/libfm-overlay-prefix`, aplicando patches versionados e validando SHA-256 dos fontes upstream antes de compilar. A instalação ativa do sistema, `.deps/prefix` e overlays de outras worktrees não são alterados.

Neste checkout, o SDK de compilação fica em `.deps/`: Qt 6.8.3 (`linux_gcc_64`), libfm-qt 2.4.0, lxqt-build-tools 2.4.0, lxqt-menu-data 2.4.0, CMake 4.4.4 e Ninja 1.13.2, com bibliotecas de desenvolvimento adicionais extraídas sob `.deps/sysroot`. `.deps/env.sh` configura caminhos de ferramentas e runtime. Esses arquivos são locais a este checkout; para provisionar outro ambiente, consulte `.deps/BUILD_NOTES.md` (se disponível) e obtenha exatamente a revisão libfm-qt esperada pelo script.

## Build, CTest e execução isolada

Na raiz do repositório:

```bash
./scripts/build.sh                 # RelWithDebInfo; overlay e app em .state
./scripts/build.sh Debug --overlay-prefix "$PWD/.state/libfm-overlay-prefix"
uv tool run --from cmake ctest --test-dir .state/build --output-on-failure
./scripts/run.sh [caminho-de-fixture]
```

`build.sh` usa no máximo dois jobs para a dependência privada e dois para o app, testa Pillow/Xlib em `/usr/bin/python3` antes do configure, seleciona esse intérprete explicitamente e inclui os testes reais de transferência. Sem espaço/interpreter/dependências, falha com explicação; a suite não é pulada para produzir um falso verde. CTest é fornecido pelo pacote CMake (via `uv tool run --from cmake`), mesmo quando `ctest` não consta isoladamente no `PATH`.

O overlay e o app são instalados apenas em `.state/libfm-overlay-prefix` e `.state/install`. `run.sh` coloca o overlay primeiro no `LD_LIBRARY_PATH` e valida, sem executar o `main`, o artefato físico configurado em `share/win11-explorer-qt/private-libfm-runtime.txt`: o arquivo precisa estar nesse prefix, seu SONAME (lido com `readelf`) precisa apontar fisicamente para ele e o `ldd` do binário precisa resolver para o mesmo arquivo; um link de SONAME externo ou outra biblioteca vencendo a busca impede a inicialização. Para usar outro prefix privado, passe `--overlay-prefix PATH` a `build.sh` e exporte `LIBFMQT_OVERLAY_PREFIX=PATH` antes de `run.sh`; **PATH precisa ficar dentro do `.state/` desta worktree**. Os scripts recusam prefixos fora dela e não removem overlays compartilhados, `/usr`, `.deps/prefix` ou overlays de outras worktrees.

## Guardas de fonte, caminhos e seleção de ABI

Antes de qualquer `mkdir`, remoção ou extração do overlay,
`scripts/fidelity/overlay-preflight.py` verifica os quatro destinos (fonte
extraída, build da biblioteca, prefixo instalado e build dos testes). Rejeita
`.state` como destino, `..`, symlinks no destino/ancestrais, escapes da `.state`
física desta worktree, sobreposição entre destinos e sobreposição com a fonte
do SDK. `build.sh` também valida seus diretórios de build/install antes de
reconstruir o overlay. Não canonicalize previamente um argumento para esconder
um symlink. Esta é uma guarda contra configuração estática insegura; não é uma
sandbox contra outro processo hostil trocando entradas do filesystem durante o
build.

A extração usa `git archive` da revisão completa
`4294f29ed314388f1f6d051ae0128320354e417a`, não `cp` do checkout. Os hashes
pinados são calculados sobre os blobs dessa revisão; arquivos ignorados,
untracked e alterações locais não entram no overlay. `LICENSE` (LGPL) e
`AUTHORS` upstream são preservados. Nenhuma operação escreve em `.deps`.

`cmake/PrivateLibfm.cmake` invalida explicitamente `fm-qt6_DIR` antes da busca
Config restrita ao prefixo solicitado. Além do arquivo de configuração,
verifica o target shared importado, os caminhos físicos de todas as localizações
importadas anunciadas e as localizações efetivamente resolvidas por
`LOCATION_<CONFIG>` para cada configuração do gerador (ou `LOCATION` sem
build type). Isso inclui `MAP_IMPORTED_CONFIG_<CONFIG>` e os defaults/cache
`CMAKE_MAP_IMPORTED_CONFIG_<CONFIG>`, mesmo quando o destino não consta em
`IMPORTED_CONFIGURATIONS`. A resolução fica com o próprio CMake, preservando
ordem e entradas vazias dos mapeamentos. Referências primárias:
[LOCATION](https://cmake.org/cmake/help/latest/prop_tgt/LOCATION_CONFIG.html),
[MAP_IMPORTED_CONFIG](https://cmake.org/cmake/help/latest/prop_tgt/MAP_IMPORTED_CONFIG_CONFIG.html)
e `cmTarget::GetMappedConfig` em `Source/cmTarget.cxx` do CMake v3.18.0.
Os includes de libfm expostos pelo target (inclusive system includes) e cada
arquivo/diretório da árvore instalada são validados fisicamente. Cada entrada
é verificada antes de enumerar seu conteúdo: symlinks de subdiretório que
escapam são recusados sem percorrer a árvore externa. Isso cobre os headers
`core/job.h`, `core/fileoperationjob.h`, `core/filetransferjob.h` e suas
dependências internas, não apenas os sentinels. Dependências como GLib
continuam podendo vir do SDK. Tanto `LIBFMQT_SOURCE_DIR` (nome legado) quanto
`LIBFMQT_INCLUDE_DIR` devem agora ser o diretório de headers instalado no
próprio prefixo: `PREFIX/include/libfm-qt6`. Não misture headers internos do
checkout com uma biblioteca de outro build. A configuração independente do
harness é protegida pelo mesmo módulo via `CMAKE_PROJECT_<nome>_INCLUDE`, sem
alterar o código do harness.

Cada configure aceito grava `private-libfm-selection.txt` no build, com config,
biblioteca real por configuração e diretórios de headers selecionados. Trocar
`--overlay-prefix` reconfigura essa seleção mesmo com cache antigo. O guard de
runtime em `run.sh` é uma defesa adicional, não um substituto dessas validações.

Regressões reproduzíveis (somente sandboxes pertencentes à tarefa em `.state`,
clones/copias mock e sentinels; nunca apontar um teste destrutivo ao SDK real):

```bash
/usr/bin/python3 tests/test_overlay_safety.py -v
/usr/bin/python3 tests/test_private_libfm_guards.py -v
/usr/bin/python3 tests/test_private_libfm_fixture_safety.py -v
/usr/bin/python3 tests/test_private_libfm_cmake.py -v
uv tool run --from cmake ctest --test-dir .state/build --output-on-failure
```

O teste `test_private_libfm_cmake.py` requer um overlay já construído. No CTest,
`LIBFMQT_OVERLAY_PREFIX` configurado é transmitido explicitamente como
`L4_TEST_PREFIX`, sem depender dessa variável no shell. Para execução standalone
com prefixo customizado, use
`L4_TEST_PREFIX="$PWD/.state/overlay-custom" /usr/bin/python3 tests/test_private_libfm_cmake.py -v`.
A cópia, as fontes do probe e os builds ficam em um diretório temporário exclusivo
`private-libfm-*` dentro de `.state`, removido ao terminar, inclusive em falhas.
Os caminhos físicos da cópia e do cleanup não podem coincidir nem se sobrepor
(como ancestral, descendente ou alias por symlink) ao input. Nenhum prefixo fixo
ou overlay de entrada é removido. Os logs persistem em
`.state/authorized-build/private-libfm-*/`. O teste de segurança separado usa
somente markers próprios em sandbox, incluindo a colisão com o antigo nome
`cache-test-prefix`, e executa CTest com apenas um prefixo customizado existente.
O teste de cache configura com `fm-qt6_DIR` obsoleto no SDK e depois
com cache no overlay anterior. Um consumer mínimo compilado chama a API nova
`FileOperation::setDialogFactory`, relinka após a troca e verifica com `dladdr`
o caminho real da biblioteca carregada, registrando também `ldd`, sem abrir GUI.
Os cinco testes de overlay, pacote, fixtures, runtime e cache estão registrados no CTest; seus logs persistentes ficam em `.state/authorized-build/`, ou em `.state/libfm-logs-*` quando esse nome colide com o overlay de entrada.
Os fixtures negativos de pacote verificam config e biblioteca por symlink,
localização genérica/per-config externa, mapeamentos target/default/cache em
single e multi-config, headers individuais e subdiretórios por symlink. Ambos
os testes de seleção criam seus diretórios de logs, sem depender de execução
anterior. Mutações opcionais `FM_GUARD_MUTATION=map` e
`FM_GUARD_MUTATION=headers` retiram só a guarda correspondente na cópia do
módulo usada pelos fixtures e devem deixar seus testes negativos vermelhos.
Os testes de fonte verificam que arquivos ocultos/untracked e `AUTHORS` sujo não são copiados,
preservando o `LICENSE` exato do pin.

O harness de overlay validado por hashes e os patches aplicados ficam em `patches/libfm-qt/` e `scripts/fidelity/build-libfm-overlay.sh`. A API cooperativa de pausa só é oferecida para uma cópia local de um arquivo regular com o checkpoint GIO; os demais modos mantêm a apresentação de transferência sem botão de pausa. O cancelamento continua sendo o cancelamento real de `Fm::FileOperation`.

Testes pesados materializam arquivos reais (incluindo 2 GiB e, no teste da pausa visual, 4 GiB), verificam espaço livre antes de escrever e validam destino/progresso/ACK. Não use o suite pesado em disco com menos de 12 GiB livres. `EXPLORER_TRANSFER_UI_SCREENSHOT=/caminho/arquivo.png` grava a captura do diálogo observado pelo QtTest. A aplicação em execução para prova visual deve usar um display isolado Xephyr e uma sessão D-Bus privada; nunca `:0` nem a VM Windows.

A configuração do SDK inclui os caminhos locais absolutos definidos por `.deps/env.sh`; esse SDK não é relocável. Nenhuma compilação/execução deste procedimento instala dependências de sistema ou muda o gerenciador padrão de arquivos.
