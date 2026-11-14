# Instalação para uso diário (usuário, sem root)

Uma instalação estável fica em um **prefixo relocável** (padrão `~/.local/opt/win11-explorer-qt/`), independente de `.deps/`, de `.state/` e do shell de desenvolvimento. O atalho do menu e o comando `win11-explorer-qt` apontam para ele.

## Requisitos de build (a máquina que monta o prefixo)

- `./scripts/build.sh RelWithDebInfo` já feito (SDK em `.deps`, ver `docs/BUILDING.md`).
- `python3`, `ldd`, `readelf`, `objdump`, `objcopy`, `strip` (binutils), `dpkg-deb` (só para o `.deb`), `desktop-file-utils` (`update-desktop-database`, `desktop-file-validate`), `unshare` (util-linux) e `strace` (opcionais, usados na prova de relocação).
- **`patchelf`**: usa o `patchelf` do sistema se existir (`sudo apt install patchelf`; Ubuntu 24.04/Mint 22 traz 0.18.0) e **registra caminho e versão em `BUNDLED.txt`**. Sem ele, o montador instala a versão **fixa `patchelf==0.19.3.0`** (wheel manylinux x86_64, `sha256 1668a30f…1e21f`, `uv pip install --require-hashes -r scripts/requirements-packaging.txt`) em `.state/tools/` — isso exige `uv` e **rede na primeira vez**. Offline: instale o patchelf pelo apt, ou copie para `.state/tools/patchelf-0.19.3.0/bin/patchelf` um binário já verificado. Nada é baixado em tempo de execução do app.

## Passo a passo

```bash
# 1) build otimizado (RelWithDebInfo; o overlay da libfm-qt também é otimizado)
./scripts/build.sh RelWithDebInfo            # ≈9 min com -j2 numa máquina de 12 núcleos ocupada
# 2) montar o prefixo (escreve SÓ no prefixo escolhido; ~1 s)
./scripts/make-portable-prefix.sh --prefix "$HOME/.local/opt/win11-explorer-qt"
# 3) provar a relocação (copia para um caminho com espaços/acentos, env -i, HOME temporário, offscreen,
#    LD_LIBRARY_PATH envenenado, árvore de dev ESCONDIDA por tmpfs num mount namespace `unshare -rm`, ldd do plugin xcb)
./scripts/verify-relocation.py "$HOME/.local/opt/win11-explorer-qt"
#    xcb de verdade (quem chama sobe o Xephyr próprio; o script recusa :0/:1 e confere o dono do socket):
./scripts/verify-xcb.sh "$HOME/.local/opt/win11-explorer-qt" :27
# 4) atalho de usuário (idempotente; --dry-run mostra o que faria; --uninstall remove)
./scripts/install-user.sh --prefix "$HOME/.local/opt/win11-explorer-qt" --dry-run
./scripts/install-user.sh --prefix "$HOME/.local/opt/win11-explorer-qt"
```

`install-user.sh` cria só três arquivos: `~/.local/bin/win11-explorer-qt` (link para o wrapper do prefixo), `~/.local/share/applications/win11-explorer-qt.desktop` (arquivo real, `Exec=` absoluto do prefixo) e `~/.local/share/icons/hicolor/scalable/apps/win11-explorer-qt.svg` (ícone original do fork, `packaging/icons/`). Recusa sobrescrever arquivo que não seja seu, valida tudo antes de escrever, e `--uninstall` só remove o que reconhece como seu. Executa **somente** `update-desktop-database -q ~/.local/share/applications` (gera `mimeinfo.cache` desse diretório, necessário para o GLib/GTK listar o app em "Abrir com"; `--dry-run` mostra o comando, `--no-desktop-db` pula). Não executa `xdg-mime`, nem toca `mimeapps.list`, autostart, Nemo ou Cinnamon. Sem um padrão explícito em `mimeapps.list`, a ordem do cache pode eleger este app para `inode/directory`; verifique o padrão do seu desktop depois de instalar. O `Exec=` é gravado entre aspas e escapado (espaço, `%`, `&`, aspas, `$`); `HOME` vazio, relativo ou `/` e prefixos com controle/newline são recusados; `--prefix` é resolvido (realpath) antes de qualquer decisão. O `.desktop` declara `MimeType=inode/directory;` apenas para o app aparecer em "Abrir com"; **não vira o padrão**. Há uma ação "Nova janela" (`-n`).

Atualizar = refazer os passos 1–2. O `make-portable-prefix.sh` monta o novo prefixo num diretório irmão (`.<nome>.new.<pid>`), roda a auto-verificação nele e só então troca por `rename` (o anterior vira `.<nome>.prev` e é removido ao final); se qualquer etapa falhar, o prefixo em uso fica intacto. Só substitui um diretório com o marcador `.win11-explorer-qt-prefix` de conteúdo válido; o destino é resolvido fisicamente (symlinks) e não pode estar em/ conter `.deps`, `.state`, `.git`, o repositório, o install dir ou o overlay. `install-dev.sh` (links para a árvore de dev) continua existindo para desenvolvimento e não deve ser usado junto: ele recusa sobrescrever links existentes.

## Layout do prefixo

```
bin/win11-explorer-qt        wrapper sh (resolve o próprio caminho, tira QT_PLUGIN_PATH/QT_QPA_PLATFORM_PLUGIN_PATH, executa libexec/ com -style Fusion)
libexec/win11-explorer-qt    ELF (RUNPATH $ORIGIN/../lib) + qt.conf (Prefix=.., Plugins=plugins, Libraries=lib)
lib/                         libfm-qt6 privada (overlay com pausa cooperativa), Qt 6.8.3 (Core DBus Gui Network Svg Widgets XcbQpa), ICU 73, libmenu-cache.so.3 — todos com RUNPATH $ORIGIN
lib/debug/*.debug            símbolos separados (binário e libfm-qt6 são entregues com strip e `.gnu_debuglink`). `lib/debug` NÃO é um caminho padrão do gdb: use `set debug-file-directory <prefixo>/lib/debug` (e `set substitute-path` se preciso); o gdb só acha o arquivo sozinho em `<dir do ELF>/`, `<dir>/.debug/` ou `/usr/lib/debug`.
plugins/                     platforms (xcb, offscreen, minimal), xcbglintegrations, platformthemes (gtk3, xdg-portal), imageformats, iconengines, tls, platforminputcontexts
share/                       desktop template, ícone, traduções, licenças, BUNDLED.txt (lista exata empacotada × do sistema)
```

Não há `LD_LIBRARY_PATH`, `QT_PLUGIN_PATH` nem `QT_STYLE_OVERRIDE` exportados: as bibliotecas empacotadas são achadas por **DT_RPATH** (`patchelf --force-rpath`, que vence um `LD_LIBRARY_PATH` herdado do shell) e `qt.conf`. Limite: um `LD_LIBRARY_PATH` com um glib/sysroot de dev ainda sombreia as libs NÃO empacotadas do sistema (medido: 6 libs, `verify-relocation.py` registra). O wrapper retira `QT_PLUGIN_PATH`/`QT_QPA_PLATFORM_PLUGIN_PATH` do processo (para o Qt 6.8 empacotado não carregar plugins de outro Qt) — consequência: os aplicativos que o explorador abre também não os herdam e o estilo Fusion vai por argumento (`-style Fusion`, consumido pelo Qt). Aplicativos abertos pelo explorador não herdam nenhuma variável do app (provado em `verify-relocation.py`, que inicia o app com um ambiente "envenenado" e lê `/proc/<pid>/environ`).

## O que vem do sistema e versões exigidas

Resolvidos pelo loader do host (lista completa em `share/win11-explorer-qt/BUNDLED.txt`): glibc ≥ 2.39 (construído em Linux Mint 22 / Ubuntu 24.04), libstdc++/libgcc, **GLib/GIO/GObject/GThread 2.80** (o mesmo da árvore de dev; versões menores não foram testadas), libxkbcommon, libxcb-* (incl. `libxcb-cursor0`, exigido pelo plugin xcb do Qt ≥ 6.5), X11/GL/EGL, fontconfig/freetype/harfbuzz, GTK3 (só para o plugin de tema `qgtk3`; sem ele o plugin é ignorado), libdbus, libpng/jpeg/zlib.
Empacotados: Qt 6.8.3 (o sistema só tem 6.4.2 e o projeto exige ≥ 6.6), ICU 73 (que o Qt 6.8 oficial pede), libfm-qt6 17.0.0 patched e `libmenu-cache.so.3` (não existe no sistema).

## Tamanho e tempo (medidos aqui)

- Build RelWithDebInfo completo (overlay libfm-qt + app + testes), `-j2`: ≈9 min de relógio.
- Prefixo: ≈75 MiB de runtime + 54 MiB de símbolos em `lib/debug` (maiores: `libicudata` 31 MiB, Qt Gui 11 MiB, Widgets 8 MiB, Core 7 MiB). Dá para apagar `lib/debug` sem efeito além de perder símbolos.
- Montagem do prefixo: ≈1 s. Início do app: sem `uv`, sem `.deps/env.sh`, sem verificação de overlay (`check-private-libfm-runtime.py` ficou só no `scripts/run.sh` de desenvolvimento).

## Limites conhecidos (registrados, não escondidos)

1. **Crash ao abrir arquivo sem aplicativo / "Abrir com → outro" (D-02) NÃO está resolvido por este pacote**: o `libmenu-cache.so.3` empacotado procura `/usr/lib/menu-cache/menu-cached` (caminho fixo na biblioteca), que não existe no host. Correção real exige instalar `libmenu-cache3` + `menu-cached` do sistema (`sudo apt install libmenu-cache3`) ou mudar o código para não chamar `menu_cache_*`. É código C++ de outra frente.
2. **Traduções/dados** (medido por `strace` em `verify-relocation.py` com a árvore de dev escondida: 9 caminhos absolutos tentados e falhos, traduções do app e `libfm-qt6/{archivers.list,translations}`): `application.cpp` carrega `PCMANFM_DATA_DIR/translations` com caminho de build absoluto (`CMAKE_INSTALL_PREFIX` = `.state/install` da worktree). No prefixo movido o app funciona, mas as traduções só aparecem enquanto aquele diretório de build existir. Correção de código: usar `QCoreApplication::applicationDirPath()/../share/win11-explorer-qt/translations` (já está copiado em `share/` do prefixo). O mesmo vale para `LIBFM_QT_DATA_DIR` dentro da libfm-qt.
3. Wayland não testado (plugin `libqwayland*` não é empacotado; só xcb). O montador e a verificação **falham** se `plugins/platforms/libqxcb.so` faltar ou tiver dependência sem resolver (`ldd` sob `env -i`). A prova automática roda `offscreen`; a execução xcb de verdade é `scripts/verify-xcb.sh PREFIXO :N` num Xephyr próprio (NÃO executado nesta rodada; fica para o coordenador).
4. glib do sistema < 2.80 não testado; Qt/ICU empacotados somam ≈45 MiB.

## Pacote `.deb` (experimental, só inspecionado — nunca instalado)

```bash
./scripts/make-deb.sh --prefix "$HOME/.local/opt/win11-explorer-qt" --out .state/deb [--without-debug]
```

Gera `win11-explorer-qt_2.4.1+win11explorer1_amd64.deb` (≈53 MiB com símbolos) com o prefixo em `/opt/win11-explorer-qt/`, link `/usr/bin/win11-explorer-qt`, `.desktop` com `Exec=/opt/win11-explorer-qt/bin/...` e o ícone em `/usr/share/icons`. `Depends:` explícito, alvo **Ubuntu 24.04 / Linux Mint 22** (`libglib2.0-0t64` é nome de 24.04/Debian 13), com todos os nomes conferidos contra `apt-cache policy` (o teste `DebPackage.test_every_depends_name_exists` falha para nome inexistente — um Depends anterior apontava para um pacote inexistente no 24.04; o daemon vem de `libmenu-cache-bin`, que `libmenu-cache3` já puxa). Os pisos de `libc6` e `libstdc++6` são **derivados dos símbolos `GLIBC_*`/`GLIBCXX_*` indefinidos dos ELF** (`scripts/elf_requirements.py`: neste build `libc6 >= 2.38`, `libstdc++6 >= 13.2`), não escritos à mão, porque as bibliotecas privadas não são conhecidas pelo dpkg (SHLIBDEPS não serve). O pacote leva os textos de licença em `/usr/share/doc/win11-explorer-qt/` (GPL-2 do app, LGPL-2.1 da libfm-qt, **LGPL-3/GPL-3 do Qt**, ICU, `THIRD_PARTY_NOTICES.md`) e um `copyright` no formato Debian; `make-deb.sh` e o montador **recusam** gerar sem esses arquivos. Sem scripts de manutenção: nada de `update-alternatives`, MIME default ou autostart. Testado por `dpkg-deb -I/-c` e pelo teste `DebPackage` (sem instalar; instalar exige root). O `.deb` **não** leva o `libmenu-cache.so.3` empacotado (confirmado com `strings`: ele procura o caminho fixo `/usr/lib/menu-cache/menu-cached`); usa o `libmenu-cache3` + `menu-cached` do sistema, que é o que deveria corrigir D-02. **Não verificado** (precisa instalar o `.deb` como root): se o `menu-cached` do pacote do Ubuntu fica em `/usr/lib/x86_64-linux-gnu/menu-cache/`, a lib do sistema (compilada para o multiarch) o acha, mas isso não foi exercitado aqui.

## Licenças e avisos (redistribuição)

O prefixo e o `.deb` redistribuem Qt 6.8.3 (LGPL-3.0/GPL-3.0), ICU 73.2 (licença Unicode/ICU), libfm-qt patched (LGPL-2.1+) e, no prefixo de usuário, `libmenu-cache.so.3` (GPL/LGPL). Em `share/doc/win11-explorer-qt/` (prefixo) e `/usr/share/doc/win11-explorer-qt/` (`.deb`): `LICENSE`, `LICENSE.libfm-qt.LGPL-2.1`, `LICENSE.Qt-LGPL-3.0`, `LICENSE.Qt-GPL-3.0`, `LICENSE.ICU`, `THIRD_PARTY_NOTICES.md` (componentes, versões, URL do código-fonte do Qt, oferta de código-fonte e **nota de substituição/relink**: as libs são `.so` trocáveis em `lib/`). Os textos do Qt vêm de `.deps/Qt/6.8.3/gcc_64` quando existirem (o SDK atual não traz `LICENSES/`, então vêm de `/usr/share/common-licenses`); sem nenhuma das duas fontes o montador falha. O texto ICU é o `copyright` do `libicu74` do Ubuntu (mesma família de licença do ICU 73.2 empacotado — confira contra o upstream antes de publicar para terceiros). Uso pessoal local não exige nada disso; distribuir o `.deb`/prefixo a terceiros exige. Não verificado juridicamente.

## Não feito nesta rodada

Dependências derivadas de `dpkg -S` das libs do sistema (a lista de `Depends` é manual + pisos derivados de ELF); instalação real do `.deb` em contêiner/VM; execução xcb real (`verify-xcb.sh`); correção das traduções/dados por caminho absoluto (código C++).
