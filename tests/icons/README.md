# Estados de ícones — teste standalone

Esta fatia mantém `icon(name)` e `resourcePath(name)`/aliases sem alteração de API.
Cut, Copy, Rename e Delete têm recursos explícitos Normal e Disabled. Paste
registra explicitamente o recurso pálido nos dois modos para não sofrer uma
segunda dessaturação automática do Qt. Seu Normal é fallback, NÃO um estado
habilitado observado: o conteúdo do clipboard não foi medido.

## Evidência e limites

Referências locais reais (não distribuídas como assets):
`.state/fidelity/reference/phase2/20261031T0115-0300/toolbar-empty.png` e
`toolbar-selected.png`, no checkout principal. A inspeção visual foi feita sobre
as capturas inteiras e o crop ampliado. Origem de cliente informada: (194,162).
Os ROIs conferidos na imagem de tela são [cx-10,264,cx+10,285), com cx:
Cut 322, Copy 370, Paste 418, Rename 466, Delete 562. Centro vertical aproximado
274 (local ao cliente: y112). Esses ROIs são de tela, não coordenadas de crop.

Amostras RGB efetivamente presentes nos ROIs (contagens por captura):

| Elemento | Selected | Empty |
| --- | --- | --- |
| Cut, azul | #1c87d9 (8 px) | #add4f1 (8 px) |
| Copy, azul | #0078d4 (16 px) | #a3ceef (16 px) |
| Rename, azul | #0078d4 (20 px) | #a3ceef (20 px) |
| Delete, cinza | #555555 (28 px) | #c2c2c2 (28 px) |
| Paste, azul | #a3ceef (22 px) | #a3ceef (22 px) |

O cinza #555555 também aparece em Copy/Rename (9/26 px); #c2c2c2 aparece
nos mesmos elementos Disabled (9/26 px) e em Paste (20 px nos dois captures).
Os desenhos originais usam essa paleta medida; as linhas finas de Cut têm
antialiasing, portanto suas amostras não estabelecem a cor vetorial subjacente.
As capturas de Paste são pixel-idênticas dentro do ROI, sem evidência de Normal.

Os SVGs são reconstruções originais por observação, não assets extraídos.
Não se reivindica igualdade pixel a pixel de silhueta, stroke ou antialiasing,
nem equivalência da toolbar inteira ou acionamento real de operações. A fatia
comprova renderização dos modos e da paleta; não testa habilitação de QAction,
clipboard ou geometria da janela de produção. Share está fora desta fatia.

## Execução reproduzível

Executar na raiz deste worktree; SDK somente leitura, build privado e até dois jobs:

```sh
source /home/pedro/win11-explorer-qt/.deps/env.sh
cmake -S tests/icons -B .state/icons-build -G Ninja -DCMAKE_MAKE_PROGRAM="$NINJA_BIN"
cmake --build .state/icons-build --parallel 2
mkdir -p .state/icons-proof
QT_QPA_PLATFORM=offscreen \
  EXPLORER_ICONS_STATE_ARTIFACTS="$PWD/.state/icons-proof" \
  EXPLORER_ICONS_CONTACT_SHEET="$PWD/.state/icons-proof/all-icons.png" \
  .state/icons-build/tst_explorericons
cmake --build .state/icons-build --target test --parallel 2
/usr/bin/python3 tests/icons/make_state_sheet.py .state/icons-proof \
  --reference-dir /home/pedro/win11-explorer-qt/.state/fidelity/reference/phase2/20261031T0115-0300
```

O script de sheet requer Pillow, não gera desenhos substitutos: lê os 40 PNGs
produzidos por QIcon no teste (5 comandos × 2 modos × 4 tamanhos). Exibe cada
render em tamanho nativo e ampliado 2× nearest-neighbor. Gera também crops
reais ampliados e `reference-pixels.json` com contagens de pixels. PNGs e JSON
ficam ignorados em `.state/`, nunca nos recursos da aplicação.

## Recuperação e RED/GREEN

Foram recuperados de um agente interrompido: registro de três recursos Normal
em explorericons.cpp, três SVGs Normal monocromáticos e teste parcial de cores.
O primeiro teste executado nessa recuperação falhou em Copy Normal 16 px por
exigir alpha >200 numa linha fina. O teste foi substituído por 40 casos reais,
exigindo os dois tons medidos com alpha >=64 e tolerância RGB de 4 unidades
(só para arredondamento da representação premultiplicada).

Antes de alterar produção, os 40 casos falharam por cores ausentes, incluindo
a dessaturação automática de Paste Disabled. Depois da implementação: 46
passes QtTest, zero falhas (40 casos de modo/tamanho + 4 funções existentes
+ init/cleanup); CTest 1/1. O teste de SVG varre todos os recursos, incluindo
os quatro arquivos Normal que não pertencem ao registry público.
