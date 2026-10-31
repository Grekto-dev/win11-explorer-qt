# Win11 Explorer Qt

Fork de PCManFM-Qt que está reconstruindo, em Qt, uma interface inspirada no Explorador de Arquivos do Windows 11. Este repositório contém o **primeiro marco funcional**, não um clone completo.

## Estado atual

A janela já combina abas CSD, navegação, comandos e a visualização de pastas/arquivos baseada nos modelos do libfm-qt. A interface está apenas no tema claro. Sidebar e menus ainda estão sendo reconstruídos; o filtro atual procura na pasta aberta e **não** é uma busca recursiva.

## Compilar e executar

Dependências de desenvolvimento: Qt 6.6 ou superior, libfm-qt 2.4 e lxqt-build-tools 2.4, além de CMake e Ninja. Consulte [docs/BUILDING.md](docs/BUILDING.md) para detalhes do SDK local verificado; não há bootstrap portátil de uma etapa para um checkout novo.

```bash
./scripts/build.sh                 # RelWithDebInfo; instala em .state/install
./scripts/build.sh Debug           # alternativa para depuração
./scripts/run.sh                   # executa o binário instalado
```

Opcionalmente, `./scripts/install-dev.sh` cria apenas links de desenvolvimento do launcher, desktop entry e ícone próprio no escopo do usuário (`~/.local`). `--prefix HOME_DE_TESTE` permite testar em outro diretório. O instalador não muda associações MIME, padrões, autostart, configurações do desktop ou arquivos do sistema, e não abre a aplicação.

## Isolamento e limites

O projeto não se registra como gerenciador padrão de pastas, não substitui Nemo/Cinnamon e não instala serviço de sessão nem autostart. Veja [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) e use `python3 scripts/check-isolation.py save .state/desktop-baseline.json` antes da primeira execução e `python3 scripts/check-isolation.py verify .state/desktop-baseline.json` depois. Não sobrescreva uma baseline existente. Ainda faltam acabamento visual, funcionalidades e testes extensivos; não há pacote `.deb` neste marco. O painel duplo legado não é exposto nesta interface de painel único.

## Verificação

Os comandos, cenários exercitados e limites dos testes estão em [docs/VERIFICATION.md](docs/VERIFICATION.md).

## Créditos e licenças

O código deriva do [PCManFM-Qt](https://github.com/lxqt/pcmanfm-qt), licenciado sob GPL-2.0-or-later; o [libfm-qt](https://github.com/lxqt/libfm-qt) é LGPL-2.1-or-later. Os avisos e atribuições do fork permanecem em [LICENSE](LICENSE) e [AUTHORS](AUTHORS). Ícones e interface adicionados ao fork são recursos vetoriais originais; não foram usados ativos da Microsoft.

O README original do upstream foi preservado em [README.upstream.md](README.upstream.md).
