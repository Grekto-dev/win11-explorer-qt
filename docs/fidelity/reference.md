# Evidência de referência — Explorer real, fase 2

Run: `20261031T0003-0300` (31/10/2026, horário local UTC−03). As capturas são PNGs integrais de 1440×900 via `virsh screenshot windows11`; a captura `fixture-root.png` contém o banner transitório do MCP e deve ser excluída de comparações visuais. `fixture-clean-check.png` e os frames de interação/transferência foram inspecionados sem esse banner; algumas janelas do Explorer ficam sobrepostas ao fundo. Os registros usam timestamps UTC/monotônicos reais.

## Ambiente reconfirmado

- Build reportado pelo guest: produto `Windows 10 Pro`, `OsBuildNumber=26300`; documentação prévia atribui 26H2, mas essa edição não foi reconfirmada nesta rodada.
- Idioma/cultura `pt-BR`; tema claro (`AppsUseLightTheme=1`, `SystemUsesLightTheme=1`).
- Tela 1440×900. A API reportou 1 Hz, valor incompatível com inferir atualização real e tratado como suspeito. DPI e retângulo exato do cliente DWM não foram recuperados; a janela de fixture aparece aproximadamente em x=194..1318, y=163..754.
- Fixture exclusiva: `C:\Users\Public\ExplorerFidelityPhase2`.

## Medido

- Aparência: pastas sintéticas Destination/Grid/Source em Detalhes; pastas e arquivos em ícones grandes; hover de Nested e estado selecionado. O marquee reverso começou no vazio e selecionou Nested, Notes.txt e Readme.txt em Detalhes e na grade. Frames mantidos/soltos e eventos de entrada têm timestamps.
- Cópia: arquivo de 2 GiB (`2147483648` bytes) criado com `fsutil file createnew`, alocado integralmente e confirmado como não esparso. Ctrl+C/Ctrl+V foi executado pela interface do Explorer (QEMU); não foi usado Copy-Item. O diálogo foi capturado a ~2,984 s, mostrando “Copiando 1 item de Source para Destination”, título “99% concluído” e corpo “100% concluído”; não apareceu até 6,259 s. O arquivo estava presente em Destination até a captura em 13,526 ms. Os SHA-256 calculados no guest coincidiram: `A7C744C13CC101ED66C29F672F92455547889CC586CE6D44FE76AE824958EA51`.
- O painel “Mais detalhes” ficou recolhido; velocidade, bytes, ETA, pausa, retomada e cancelamento não foram medidos. O intervalo temporal de conclusão é limitado pela amostragem, não é um tempo exato.
- Captura discreta via `virsh`, não recorder guest a 30 FPS. FPS alcançado encontra-se nos manifests (não alegar 60 FPS); animações curtas não foram temporizadas.

## Não observado / bloqueios desta rodada

Ctrl/Shift-click, clique duplo, cancelar com Escape, bordas da seleção, retângulo normal (sentido contrário), DND real entre pastas/destino inválido, menus com timeline, comandos desabilitados/pressionados, abertura/fechamento animados, pausa/retomada/cancelamento da cópia e medidas pixel-a-pixel de cor/opacidade. Não foram inferidos ou preenchidos por memória.

## Artefatos

- `STATIC_READY.json`, `INTERACTIONS_READY.json`, `TRANSFER_READY.json`, `manifest.json` no diretório do run.
- `interactions/input-log.json`, `gesture-log.json`, `gesture-grid-log.json`; `transfer-ui-timeline.json`.
- A fonte `scripts/fidelity/reference/capture.py` oferece entrada QEMU e captura timestampada; `test_capture.py` cobre escala de coordenadas e ordem de eventos.
- Os arquivos e caminhos exatos estão enumerados em `.state/fidelity/reference/phase2/20261031T0003-0300/manifest.json`.

A fixture guest foi deixada intacta para permitir inspeção/reprodução; nenhum asset proprietário foi extraído.
