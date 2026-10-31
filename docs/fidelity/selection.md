# Baseline da frente seleção/interação

**Status:** nenhum delta Windows implementado. Não foi encontrado `INTERACTIONS_READY.json` em `.state/fidelity/reference/phase2/` no momento desta execução. Isso bloqueia qualquer alteração de semântica/aparência para seleção, cancelamento ou DND; não é evidência de equivalência Windows.

## Probe reexecutável

```sh
python3 tests/selection/probe_baseline.py
```

O JSON emitido inspeciona os fontes da aplicação e da cópia somente leitura de libfm-qt e procura o manifesto de referência. Ele constata responsabilidade/implementação, não exercita gestos nem declara comportamento do Windows.

## Baseline observado no código upstream real (libfm-qt 2.4)

- `PCManFM::View` deriva de `Fm::FolderView`; em `pcmanfm/view.cpp` não há override dos handlers de mouse/drag/drop. Portanto não há uma implementação paralela de seleção nesta frente.
- A seleção estendida é configurada por libfm-qt. Os handlers de clique/pressão/movimento/soltura da grade/lista e a lógica do retângulo em detalhes estão em `.deps/src/libfm-qt/src/folderview.cpp` (`FolderViewListView` e `FolderViewTreeView`).
- Em detalhes, libfm-qt atualiza seleção durante `mouseMoveEvent`; o evento de soltura limpa o retângulo desenhado. A árvore aplica flags distintas para Ctrl, Shift e sem modificador. Isso descreve a implementação Linux atual, não a referência Windows.
- DND também é delegado a libfm-qt: `FolderView::childDropEvent` escolhe destino e resolve copy/move/link, com `FileOperation` real. `View` não implementa DND de arquivos.
- O smoke `tests/e2e_x11.py` existente usa XTEST para alguns gestos, mas não observa seleção por clique/retângulo, Escape ou DND de arquivos. Não o contar como prova desses cenários.

## Dependência para fechar a frente

O operador exclusivo da VM precisa publicar `INTERACTIONS_READY.json` com sequências/timestamps e resultados de seleção/retângulo, modificadores, soltura/cancelamento e drops válidos/inválidos (incluindo efeito real copy/move). Depois, acrescentar testes XTEST em `tests/selection/` que observem a seleção efetiva e efeitos no filesystem, executar RED/GREEN e implementar somente diferenças medidas nos arquivos permitidos. O Escape não tem efeito atribuído sem a observação correspondente.

## Limitação desta entrega

Não foram executados gestos XTEST nesta sessão: o driver existente depende de `python-xlib`, ausente no `/usr/bin/python3`; além disso, a evidência Windows requerida ainda não está publicada. A mudança é somente o probe de inspeção acima e esta documentação; nenhum comportamento de produção foi alterado.
