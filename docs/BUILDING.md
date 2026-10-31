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

O overlay e o app são instalados apenas em `.state/libfm-overlay-prefix` e `.state/install`. `run.sh` coloca o overlay primeiro no `LD_LIBRARY_PATH` e verifica com `ldd` que `libfm-qt6.so` resolve exatamente dentro desse prefix; se a biblioteca estiver ausente ou outra versão vencer a busca, não inicia. Para usar outro prefix privado, passe `--overlay-prefix PATH` a `build.sh` e exporte `LIBFMQT_OVERLAY_PREFIX=PATH` antes de `run.sh`; **PATH precisa ficar dentro do `.state/` desta worktree**. Os scripts recusam prefixos fora dela e não removem overlays compartilhados, `/usr`, `.deps/prefix` ou overlays de outras worktrees.

O harness de overlay validado por hashes e os patches aplicados ficam em `patches/libfm-qt/` e `scripts/fidelity/build-libfm-overlay.sh`. A API cooperativa de pausa só é oferecida para uma cópia local de um arquivo regular com o checkpoint GIO; os demais modos mantêm a apresentação de transferência sem botão de pausa. O cancelamento continua sendo o cancelamento real de `Fm::FileOperation`.

Testes pesados materializam arquivos reais (incluindo 2 GiB e, no teste da pausa visual, 4 GiB), verificam espaço livre antes de escrever e validam destino/progresso/ACK. Não use o suite pesado em disco com menos de 12 GiB livres. `EXPLORER_TRANSFER_UI_SCREENSHOT=/caminho/arquivo.png` grava a captura do diálogo observado pelo QtTest. A aplicação em execução para prova visual deve usar um display isolado Xephyr e uma sessão D-Bus privada; nunca `:0` nem a VM Windows.

A configuração do SDK inclui os caminhos locais absolutos definidos por `.deps/env.sh`; esse SDK não é relocável. Nenhuma compilação/execução deste procedimento instala dependências de sistema ou muda o gerenciador padrão de arquivos.
