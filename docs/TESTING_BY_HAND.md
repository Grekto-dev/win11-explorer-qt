# Teste manual do Win11 Explorer Qt

Este guia leva você a abrir o app e conferir à mão o que os testes automáticos
cobrem. Nada aqui altera Nemo, Cinnamon, o gerenciador padrão de pastas ou
associações MIME. O app usa identidade e configuração próprias.

## 1. Abrir a versão certa

O atalho `win11-explorer-qt` em `~/.local/bin` aponta para o checkout raiz, que
**ainda não foi recompilado** com a fase 3. Para ver o diálogo novo, rode da
worktree compilada (ou da raiz depois de compilar):

```bash
cd /home/pedro/win11-explorer-qt/.state/worktrees/transfer-dialog
./scripts/run.sh /tmp/explorer-teste
```

`run.sh` confere antes se a biblioteca libfm-qt privada é a do build. Se faltar
o build: `./scripts/build.sh Debug` (≈ 5 min).

## 2. Preparar arquivos de teste (só em /tmp, apagáveis)

```bash
mkdir -p /tmp/explorer-teste/origem /tmp/explorer-teste/destino
# um arquivo grande para dar tempo de usar os controles (3 GiB; precisa de ≥ 7 GiB livres)
dd if=/dev/urandom of=/tmp/explorer-teste/origem/grande.bin bs=1M count=3072 status=progress
# um arquivo com nome estranho, para o teste de nomes
touch "/tmp/explorer-teste/origem/x<b>negrito</b>&amp;.txt"
```

## 3. Roteiro (anote o que vir; "ok" ou o que diverge)

**Cópia e controles do diálogo**
1. Copie `grande.bin` (Ctrl+C) e cole em `destino` (Ctrl+V). O diálogo deve
   mostrar título `N% concluído`, barra azul-clara, ⏸ ao lado do ✕.
2. Clique ⏸. O botão fica desabilitado em "Pausando…" até o backend confirmar.
   **Limitação conhecida:** a confirmação só vem quando o glib termina um bloco
   de até 2 GiB, então pode demorar vários segundos (no Windows foi < 1 s).
3. Pausado: título `Pausado - N% concluído`, barra amarela, ▶. Clique em
   "Mais detalhes": o diálogo cresce; veja gráfico, Nome, Tempo restante
   ("Calculando…"), Itens restantes.
4. Clique ▶ e confirme que o progresso volta a andar até 100%.
5. Repita, pause e cancele com ✕: deve aparecer `Cancelando - N%` **sem**
   pedir confirmação, e a janela some quando o job termina; o arquivo parcial
   no destino é removido.
6. Tente Tab/Enter/Esc com o diálogo em foco: Tab mostra um anel de foco nos
   botões, Enter não aciona o ✕, Esc cancela uma vez.

**Nomes hostis:** copie o arquivo `x<b>negrito</b>&amp;.txt` e passe o mouse
sobre o resumo do diálogo: o texto deve aparecer literal, sem negrito.

**Seleção e arrastar:** em uma pasta com vários itens, teste clicar, Ctrl+clique,
Shift+clique, retângulo de seleção e arrastar para outra pasta/aba.

## 4. O que me reportar
- Prints (o diálogo nos 4 estados vale mais que descrição) e divergências do
  Explorer real do Windows.
- Qualquer travamento, janela que não fecha, ou arquivo parcial deixado.
- A demora do ACK da pausa em arquivos de tamanhos diferentes (256 MiB, 1 GiB,
  3 GiB), com cronômetro.

## 5. Limpeza
```bash
rm -rf /tmp/explorer-teste
```
