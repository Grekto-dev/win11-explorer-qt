# Instalação para uso diário (usuário, sem root)

Uma instalação estável fica em um **prefixo relocável** (padrão `~/.local/opt/win11-explorer-qt/`), independente de `.deps/`, de `.state/` e do shell de desenvolvimento. O atalho do menu e o comando `win11-explorer-qt` apontam para ele.

## Passo a passo

```bash
# 1) build otimizado (RelWithDebInfo; o overlay da libfm-qt também é otimizado)
./scripts/build.sh RelWithDebInfo            # ≈9 min com -j2 numa máquina de 12 núcleos ocupada
# 2) montar o prefixo (escreve SÓ no prefixo escolhido; ~1 s)
./scripts/make-portable-prefix.sh --prefix "$HOME/.local/opt/win11-explorer-qt"
# 3) provar a relocação (copia para um caminho com espaços/acentos, env -i, HOME temporário, offscreen)
./scripts/verify-relocation.py "$HOME/.local/opt/win11-explorer-qt"
# 4) atalho de usuário (idempotente; --dry-run mostra o que faria; --uninstall remove)
./scripts/install-user.sh --prefix "$HOME/.local/opt/win11-explorer-qt" --dry-run
./scripts/install-user.sh --prefix "$HOME/.local/opt/win11-explorer-qt"
```

`install-user.sh` cria só três arquivos: `~/.local/bin/win11-explorer-qt` (link para o wrapper do prefixo), `~/.local/share/applications/win11-explorer-qt.desktop` (arquivo real, `Exec=` absoluto do prefixo) e `~/.local/share/icons/hicolor/scalable/apps/win11-explorer-qt.svg` (ícone original do fork, `packaging/icons/`). Recusa sobrescrever arquivo que não seja seu, valida tudo antes de escrever, e `--uninstall` só remove o que reconhece como seu. Não executa `xdg-mime`, `update-desktop-database`, nem toca `mimeapps.list`, autostart, Nemo ou Cinnamon. O `.desktop` declara `MimeType=inode/directory;` apenas para o app aparecer em "Abrir com"; **não vira o padrão**. Há uma ação "Nova janela" (`-n`).

Atualizar = refazer os passos 1–2 (o `make-portable-prefix.sh` só substitui um diretório que tenha o marcador `.win11-explorer-qt-prefix`, criado por ele). `install-dev.sh` (links para a árvore de dev) continua existindo para desenvolvimento e não deve ser usado junto: ele recusa sobrescrever links existentes.

## Layout do prefixo

```
bin/win11-explorer-qt        wrapper sh (resolve o próprio caminho, tira QT_PLUGIN_PATH/QT_QPA_PLATFORM_PLUGIN_PATH, executa libexec/ com -style Fusion)
libexec/win11-explorer-qt    ELF (RUNPATH $ORIGIN/../lib) + qt.conf (Prefix=.., Plugins=plugins, Libraries=lib)
lib/                         libfm-qt6 privada (overlay com pausa cooperativa), Qt 6.8.3 (Core DBus Gui Network Svg Widgets XcbQpa), ICU 73, libmenu-cache.so.3 — todos com RUNPATH $ORIGIN
lib/debug/*.debug            símbolos separados (binário e libfm-qt6 são entregues com strip; gdb via .gnu_debuglink)
plugins/                     platforms (xcb, offscreen, minimal), xcbglintegrations, platformthemes (gtk3, xdg-portal), imageformats, iconengines, tls, platforminputcontexts
share/                       desktop template, ícone, traduções, licenças, BUNDLED.txt (lista exata empacotada × do sistema)
```

Não há `LD_LIBRARY_PATH`, `QT_PLUGIN_PATH` nem `QT_STYLE_OVERRIDE` exportados: as bibliotecas são achadas por RUNPATH/`qt.conf` e o estilo Fusion vai por argumento (`-style Fusion`, consumido pelo Qt). Aplicativos abertos pelo explorador não herdam nenhuma variável do app (provado em `verify-relocation.py`, que inicia o app com um ambiente "envenenado" e lê `/proc/<pid>/environ`).

## O que vem do sistema e versões exigidas

Resolvidos pelo loader do host (lista completa em `share/win11-explorer-qt/BUNDLED.txt`): glibc ≥ 2.39 (construído em Linux Mint 22 / Ubuntu 24.04), libstdc++/libgcc, **GLib/GIO/GObject/GThread 2.80** (o mesmo da árvore de dev; versões menores não foram testadas), libxkbcommon, libxcb-* (incl. `libxcb-cursor0`, exigido pelo plugin xcb do Qt ≥ 6.5), X11/GL/EGL, fontconfig/freetype/harfbuzz, GTK3 (só para o plugin de tema `qgtk3`; sem ele o plugin é ignorado), libdbus, libpng/jpeg/zlib.
Empacotados: Qt 6.8.3 (o sistema só tem 6.4.2 e o projeto exige ≥ 6.6), ICU 73 (que o Qt 6.8 oficial pede), libfm-qt6 17.0.0 patched e `libmenu-cache.so.3` (não existe no sistema).

## Tamanho e tempo (medidos aqui)

- Build RelWithDebInfo completo (overlay libfm-qt + app + testes), `-j2`: ≈9 min de relógio.
- Prefixo: ≈75 MiB de runtime + 54 MiB de símbolos em `lib/debug` (maiores: `libicudata` 31 MiB, Qt Gui 11 MiB, Widgets 8 MiB, Core 7 MiB). Dá para apagar `lib/debug` sem efeito além de perder símbolos.
- Montagem do prefixo: ≈1 s. Início do app: sem `uv`, sem `.deps/env.sh`, sem verificação de overlay (`check-private-libfm-runtime.py` ficou só no `scripts/run.sh` de desenvolvimento).

## Limites conhecidos (registrados, não escondidos)

1. **Crash ao abrir arquivo sem aplicativo / "Abrir com → outro" (D-02) NÃO está resolvido por este pacote**: o `libmenu-cache.so.3` empacotado procura `/usr/lib/menu-cache/menu-cached` (caminho fixo na biblioteca), que não existe no host. Correção real exige instalar `libmenu-cache3` + `menu-cached` do sistema (`sudo apt install libmenu-cache3`) ou mudar o código para não chamar `menu_cache_*`. É código C++ de outra frente.
2. **Traduções**: `application.cpp` carrega `PCMANFM_DATA_DIR/translations` com caminho de build absoluto (`CMAKE_INSTALL_PREFIX` = `.state/install` da worktree). No prefixo movido o app funciona, mas as traduções só aparecem enquanto aquele diretório de build existir. Correção de código: usar `QCoreApplication::applicationDirPath()/../share/win11-explorer-qt/translations` (já está copiado em `share/` do prefixo). O mesmo vale para `LIBFM_QT_DATA_DIR` dentro da libfm-qt.
3. Wayland não testado (plugin `libqwayland*` não é empacotado; só xcb). Plugin `xcb` testado apenas por carga de biblioteca; a execução real foi feita com `offscreen` — nenhuma janela foi aberta na sessão gráfica do usuário.
4. glib do sistema < 2.80 não testado; Qt/ICU empacotados somam ≈45 MiB.

## Plano `.deb` (não implementado nesta rodada)

Para distribuir: instalar o prefixo em `/opt/win11-explorer-qt/` e usar CPack DEB (`CPACK_GENERATOR=DEB`, `CPACK_PACKAGING_INSTALL_PREFIX=/opt/win11-explorer-qt`) sobre um alvo `install(...)` que reproduza `portable_prefix.py` (binário em `libexec/`, wrapper em `bin/`, libs Qt/ICU/libfm em `lib/`); `Depends:` explícito (`libc6 (>= 2.39), libglib2.0-0t64 (>= 2.80), libmenu-cache3, menu-cached, libxcb-cursor0, libxkbcommon-x11-0, libgl1, libgtk-3-0t64`), não `SHLIBDEPS`, porque as libs privadas não estão no dpkg. `postinst` só `update-desktop-database` do próprio diretório e `gtk-update-icon-cache`, sem `update-alternatives` nem MIME default. Link `/usr/bin/win11-explorer-qt` → wrapper. Este ambiente não tem `dpkg-deb`/`fakeroot` verificados, então não foi gerado.
