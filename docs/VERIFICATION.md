# Verificação do primeiro marco

Ambiente exercitado: Linux Mint, X11, Qt 6.8.3, libfm-qt 2.4.0.
A referência visual é a VM descrita em ARCHITECTURE.md. Este é um marco
funcional inicial, não uma declaração de paridade com o Explorer.

## Compilação e instalação

- `./scripts/build.sh Debug` configurou, compilou e instalou o executável.
- Todas as entradas do manifesto de instalação ficaram em `.state/install`.
- `./scripts/install-dev.sh` instalou apenas os três links próprios de
  desenvolvimento: launcher, desktop entry e ícone, dentro de `~/.local`.
- O launcher instalado respondeu a `--version`; o desktop entry foi validado
  e exercitado com `gio launch` em uma inicialização real.
- A interface de instância única exporta apenas a identidade do fork; não
  exporta operações de desktop nem assume `org.freedesktop.FileManager1`.

## Testes automatizados

Com o SDK local, na raiz do checkout:

```bash
source .deps/env.sh
cmake --build .state/build --target test --parallel 1
```

As quatro suítes passaram: `explorer-ui`, `isolation-contract`,
`packaging-contract` e `cli-boundary`.

Os testes Qt usam a janela e os modelos reais: hierarquia e dimensões das
barras, ícones, abas reparentadas, histórico, filtro por aba, Escape,
maximizar/restaurar, clipboard e operações reais de copiar/colar/renomear.
Os testes de instalação cobrem idempotência, prefixos com espaços e dois
pontos, arquivos conflitantes e ausência de instalação parcial por conflito.
As opções CLI de desktop/daemon/papel de parede e perfis inseguros foram
rejeitadas com código de saída diferente de zero.

## Fluxo nativo com XTEST

Em um display Xephyr isolado, com gerenciador de janelas próprio:

```bash
source .deps/env.sh
DISPLAY=:9 dbus-run-session -- /usr/bin/python3 tests/e2e_x11.py \
  --binary .state/install/bin/win11-explorer-qt
```

Foram verificados, com mouse e teclado reais do servidor X:

1. Botão de nova guia e fechamento por Ctrl+W.
2. Renomeação inline por F2.
3. Copiar/colar, comparando o conteúdo do arquivo.
4. Recortar/colar, confirmando remoção da origem.
5. Criação de pasta por Ctrl+Shift+N.
6. Movimento nativo pela barra de título desenhada pelo aplicativo.
7. Redimensionamento nativo pelo canto da janela.

Os testes criam somente suas próprias pastas e configurações temporárias.
Os resultados e capturas ficam em `.state/`, não são publicados no Git.
Algumas sessões D-Bus isoladas emitiram avisos de GVFS/FUSE/portal durante
a inicialização/encerramento do ambiente de teste; isso não foi apresentado
como evidência de acessibilidade ou integração de portais validada.

## Proteção do ambiente e atribuição

A baseline local confirmou que continuaram iguais: manipulador padrão de
pastas (`nemo.desktop`), proprietário do serviço FileManager1, applets do
Cinnamon, opção de ícones do desktop Nemo e arquivos de associações MIME.
`LICENSE`, `AUTHORS` e o conteúdo de `README.upstream.md` foram comparados
com a base upstream e preservados. O README upstream mantém inclusive o
whitespace original, por preservação byte a byte.

Houve revisões independentes do código/UI e dos scripts/documentação sem
achados bloqueadores. Não foram publicados pacotes, tags ou releases.

## Ainda não coberto

Wayland, acessibilidade completa, todas as opções herdadas, operações
complexas de rede/montagem, restauração da lixeira, recuperação após falhas
ou conflitos extensivos de arquivos. Sidebar, menus, diálogos, tema escuro
e páginas especiais continuam como trabalho de reconstrução posterior.
