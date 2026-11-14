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

Na época do primeiro marco, a baseline local confirmou que continuaram iguais (a situação atual está em "Fase 2 / Limitações"): manipulador padrão de
pastas (`nemo.desktop`), proprietário do serviço FileManager1, applets do
Cinnamon, opção de ícones do desktop Nemo e arquivos de associações MIME.
`LICENSE`, `AUTHORS` e o conteúdo de `README.upstream.md` foram comparados
com a base upstream e preservados. O README upstream mantém inclusive o
whitespace original, por preservação byte a byte.

No primeiro marco, houve revisões independentes do código/UI e dos scripts/documentação sem
achados bloqueadores. Não foram publicados pacotes, tags ou releases.

## Ainda não coberto

Wayland, acessibilidade completa, todas as opções herdadas, operações
complexas de rede/montagem, restauração da lixeira, recuperação após falhas
ou conflitos extensivos de arquivos. Sidebar, menus, diálogos, tema escuro
e páginas especiais continuam como trabalho de reconstrução posterior.

## Fase 2 (candidato de integração `phase2/integration-r2`)

Esta seção registra o que foi exercitado depois do primeiro marco. **Não é uma
declaração de paridade com o Explorer do Windows.**

### Exercitado nesta integração

- Build limpo (`./scripts/build.sh Debug`) do overlay privado de libfm-qt e do
  aplicativo, seguido de **CTest 18/18**: contratos de isolamento/pacote/CLI,
  UI Qt, SVGs, harness de evidências, guardas do overlay/CMake, segurança do
  wrapper X11, teste pesado de transferência (cópia real de 2 GiB no Linux, com hash,
  pausa com ACK, cancelamento; só existe com `ENABLE_HEAVY_TRANSFER_TESTS=ON`,
  que o `build.sh` liga, e exige ≥12 GiB livres) e `job-cancel-lifetime`.
- `ldd` dos binários de teste e do aplicativo resolve `libfm-qt6.so.17` no
  overlay privado desta árvore; `scripts/fidelity/check-private-libfm-runtime.py`
  confere o artefato físico, o SONAME e o `ldd` sem executar o `main`
  (saídas: `.state/r3/evidence/ldd.txt` e `runtime-check.txt`, na árvore de
  integração, não versionadas).
- Seis casos nativos em Xephyr próprio (ownership, roteamento Qt, limpeza de
  entrada, gestos XTEST Ctrl/Shift/vazio/DND, comparação desses gestos com a
  referência medida — não é paridade geral — e composição alfa do retângulo de
  seleção) e os sete checks de `tests/e2e_x11.py` com o executável instalado.
- Isolamento por rodada: `scripts/check-isolation.py save` antes e `verify`
  depois da rodada nativa (`.state/r3/native-*/before.json`); o verify passou
  (manipulador de pastas, proprietário de FileManager1, applets do Cinnamon,
  desktop do Nemo e MIME iguais **ao snapshot tirado antes da rodada**). Isto
  não é comparação com a baseline original: ver Limitações. As rodadas
  anteriores (17/17) constam só nas notas do coordenador, sem log persistido.
- Estado da revisão: backend (`85f4f5a`), evidências, UI e build passaram em
  revisão independente após ciclos de correção (a revisão do build deixou um
  achado de documentação corrigido sem nova revisão); o delta de integração
  foi revisado e reprovado por esta documentação, corrigida depois. Não há
  revisão independente do diff integrado completo.

### Correções de segurança e confiabilidade desta fase

- Cancelamento de `Fm::Job` não acessa o job depois de acordar o worker
  (`requestCancellation()` retém o gate de pausa e o `GCancellable` antes);
  regressão determinística em `tests/transfer/tst_job_cancel_lifetime.cpp`.
  O chamador continua responsável por manter o `Job` vivo até entrar na chamada.
- O controller emite `cancellationRequested` uma vez sob reentrância.
- Saídas de fixtures/logs do build, prefixo canônico transmitido ao CTest e
  validação física do SONAME antes de iniciar o aplicativo.
- Plano de saída do harness rejeita colisões (incluindo ancestralidade
  arquivo/diretório); `input_events` desconhecido e o roteamento de formato de
  manifesto seguem o schema declarado.
- Testes X11 não dependem de `TMPDIR`; a rota Qt efetiva é vinculada ao display
  próprio antes de qualquer entrada.

### Limitações conhecidas (não resolvidas)

- **Controles de transferência do Windows não foram observados**: Mais
  detalhes, pausa, retomada e cancelamento na referência; na VM Windows só
  progresso vivo e hash de cópias de 2 e 4 GiB foram medidos. O comportamento da pausa no Linux
  é cooperativo, restrito a COPY de arquivo regular local, e não é comparado
  com o Windows.
- Taxa de atualização da referência desconhecida (capturas de ~15 fps);
  `equality_eligible=false` e `equivalence=not_evaluated` nos relatórios.
  Curvas de animação não foram reproduzidas.
- Escape durante o retângulo de seleção, bordas/autoscroll e estados de ícones
  não observados continuam sem referência.
- Estabilidade do teste nativo de seleção: duas execuções antigas tiveram zero
  itens na primeira seleção, sem causa demonstrada; as execuções desta rodada
  passaram.
- **Isolamento contra a baseline original (2026-10-30) falha hoje**, e também
  contra a baseline de retomada (2026-10-31): `check-isolation.py verify`
  aponta o proprietário de FileManager1 (`:1.57` → `:1.54`), a lista de
  applets do Cinnamon, `.config/mimeapps.list` e `.config/nemo/desktop-metadata`
  (saídas: `.state/r3/evidence/isolation-vs-*.txt`). Causa **não determinada**.
  Observado apenas: `mimeapps.list` tem mtime de 2026-11-04, `desktop-metadata`
  de 2026-11-13 17:13, o manipulador padrão de `inode/directory` continua
  `nemo.desktop` e as rodadas desta integração passaram contra snapshots
  próprios. As baselines não foram substituídas.
- Wayland, pacote `.deb` e reprodutibilidade pública completa não foram
  validados.

## Fase 4 (segurança de operações, navegação, UI e empacotamento)

Integração das frentes `backend-safety`, `crash-nav`, `ui-quick` e `packaging`
(candidato `phase4/integration4`, 2026-11-14).

- **Medido na integração final** (build `scripts/build.sh Debug`): build 0,
  teste pesado 25/25 e `ctest --parallel 1` 21/21. Uma rodada anterior falhou
  em `transfer-controller-heavy` (ACK de pausa em 7,25 s contra o limite de
  5 s) com a máquina sob carga; repetido isoladamente passou 3/3. A causa é o
  ponto de pausa só ocorrer entre chamadas `copy_file_range` (até 2 GiB), então
  o teste continua sensível à carga até existir um laço de cópia em blocos.
- **Prefixo portátil** montado em diretório de scratch: prova de relocação PASS
  (zero acessos ao prefixo original) e `scripts/verify-xcb.sh` PASS num Xephyr
  próprio (`:31`, dono verificado por `/proc/PID/exe`; janela xcb mapeada).
- **Isolamento**: `check-isolation.py verify` contra o snapshot anterior à
  publicação passou às ~13:20 de 2026-11-14. Repetido às ~14:07 (antes do push),
  falhou só em `.config/nemo/desktop-metadata`, com mtime 13:54: o conteúdo
  mudou em `nemo-icon-position-timestamp` do ícone da lixeira (chave que só o
  Nemo escreve), com `nemo-desktop` da sessão do usuário em execução e um novo
  processo `nemo`. É compatível com o próprio Nemo regravando o arquivo, mas a
  autoria **não foi provada**; o snapshot não foi substituído. Diretório padrão,
  dono do FileManager1, applets do Cinnamon e `mimeapps.list` seguiam iguais. A
  divergência contra a baseline original descrita na Fase 2 continua sem causa
  atribuída.
- **Revisões independentes (estáticas)**: cada frente passou por 2 a 6 ciclos;
  as aprovações finais são `review4-p4-backend-safety`, `review4-p4-packaging`,
  `review2-p4-ui-quick` e `review6-p4-crash-nav`. Revisão estática não substitui
  teste manual.
- **Limitações conhecidas**: `.desktop` do tipo Link com URL `menu://` aberto
  via `BasicFileLauncher::shortcutTargetPaths` não é filtrado (exigiria patch na
  libfm); `PlainText` não foi aplicado aos diálogos da própria libfm; o
  `op->wait()` com NOT_MOUNTED não tem teste (exige GVFS real); ENOSPC real,
  destinos remotos, hardlinks e xattr não foram testados; ícone desabilitado
  igual ao normal no tema escuro para SVGs sem versão própria; HiDPI não
  bit-idêntico no tema claro; o teste de esquema escuro com paleta clara é
  pulado no offscreen; texto da licença ICU conferido apenas por sha256 contra
  o upstream 73.2; `.deb` não instalado num contêiner limpo (não pega
  bibliotecas carregadas por `dlopen`); Wayland não testado.
