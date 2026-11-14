# Medições do diálogo de cópia (Windows 11 pt-BR, tema escuro, 96 DPI)

Origem: quadros da rodada `controls-20261113T235224Z` (VM, 1440×900; artefatos
fora do Git em `.state/fidelity/reference/phase2/`). Só números medidos com PIL
aparecem aqui; nenhum pixel/recurso Microsoft foi copiado para o repositório.
Coordenadas são relativas ao canto superior esquerdo do retângulo visível (DWM)
da janela, que nos quadros fica em tela (496,152). Medido: recolhido 449×165,
expandido 449×287.

## Quadros usados
| Rótulo | Quadro | Estado |
|---|---|---|
| A | `A_1794614114758.png` (e vizinhos) | cópia ativa, recolhido, sem cliques (79%) |
| B-pausa | `B_1794614304648.png` | pausado 32%, recolhido |
| B-exp | `B_1794614319274.png` | pausado 32%, expandido |
| B-ret | `B_post_cmd3_1794614338821.png` | retomado |
| C-pausa | `C_1794614481415.png` | pausado antes de cancelar |
| C-canc | `C_post_cmd1_1794614487559.png` | "Cancelando - 53% concluído" |

## Cores (RGB lidos dos pixels)
| Elemento | RGB | Quadro |
|---|---|---|
| Fundo do corpo | 44,44,44 (#2C2C2C) | A |
| Borda da janela | 61,61,61 (#3D3D3D) | A |
| Separador do rodapé | 77,77,77 (#4D4D4D), 1 px, x 3..445 | A, B-pausa |
| Trilho da barra | 56,56,56 (#383838) | A, B-pausa |
| Borda da barra | 87,87,87 (#575757) | A, B-pausa |
| Preenchimento em andamento | 96,205,255 (#60CDFF) | A |
| Preenchimento pausado/cancelando | 252,225,0 (#FCE100) | B-pausa, C-canc |
| Link (origem/destino) | ≈96,205,255 (mesma cor do preenchimento) | A |
| Texto do percentual, glifos ⏸/▶ ativos | 255,255,255 | A, B-pausa |
| Glifo ▶ esmaecido (Cancelando) | 121,121,121 (#797979) | C-canc |
| Gráfico: fundo | 44,44,44 | B-exp |
| Gráfico: área até o progresso (pausado) | 75,71,37 (= amarelo a 15% sobre o fundo) | B-exp |
| Gráfico: curva preenchida (pausado) | 252,225,0 | B-exp |
| Gráfico: linhas de grade | 60,58,40 (vertical) / 89,83,34 (horizontal sobre a área tingida) | B-exp |

**Divergência registrada (contrato, lacuna 2):** o código antigo usava verde
`#16833b` (referência de 31/10). Os quadros desta rodada mostram barra
**azul-clara #60CDFF** em andamento; a decisão seguiu os pixels. O motivo da
mudança entre as duas capturas não foi investigado.

## Geometria (px, relativa ao canto DWM)
| Elemento | Medida |
|---|---|
| Texto-resumo ("Copiando 1 item de src para dst1") | x 28.., linhas de texto y 46..57 (corpo ≈12 px) |
| Linha do percentual ("32% concluído") | x 28.., y 66..76 (≈14 px) |
| Glifo ⏸/▶ | caixa x 362..370, y 69..76/77 → centro (366; 72,5) |
| Glifo ✕ | caixa x 409..417, y 69..76 → centro (413; 72,5) |
| Barra (incl. borda) | x 27..421 (395 px), y 90..104 (15 px); interior y 91..103 |
| Proporção do preenchimento | 125 px de 393 px ≈ 31,8 % (percentual 32) |
| Separador (recolhido / expandido) | y 120 / y 242 |
| Rodapé "Mais/Menos detalhes" | chevron x 32..40, texto x 52..121, centro vertical = separador + 22 |
| Base da janela | borda inferior em y 163..164 (recolhido), 286 (expandido) |
| Gráfico (expandido) | x 27..421, y 90..169 (395×80); grade vertical a cada ≈39,5 px (10 divisões); horizontal a cada 15 px a partir de y 92 |
| Linhas de detalhes | "Nome:" y 183..194; "Tempo restante:" y 199..210; "Itens restantes:" y 215..225 (passo 16 px), x 28.. |
| Altura expandido − recolhido | 122 px |

Pausa/✕ não mudam de posição ao expandir (verificado em B-pausa × B-exp).

## Não medido / lacunas
- Altura da barra de título e conteúdo dela (coberta pelo banner da VM nos quadros).
- Cor do texto-resumo não-link (halo do cursor/banner sobrepõe a região).
- Gráfico em andamento (não pausado), comportamento do eixo vertical e formato
  do texto de "Tempo restante" com estimativa real — só foi observado
  "Calculando…" com a cópia pausada.
- Cancelar sem pausar; ícone do diálogo; outras versões/temas.

### Decisões e limites registrados (revisão do diálogo, ciclo 1)
- Cor do resumo `#D0D0D0`: **escolhida, não medida** (o halo/banner cobre a região).
- Eixo X do gráfico: **fração de bytes** copiados, não tempo.
- Nomes longos são **elididos** (meio, largura 393 px, tooltip com o texto completo) em vez de quebra de linha;
  o Windows não foi observado com nomes longos.
- Foco por teclado: indicador desenhado (1 px branco) só com foco por Tab/Backtab/atalho; em repouso o visual
  medido não muda. Botões desenhados não são `default`/`autoDefault` (Enter não os aciona).
- Esc e ✕ cancelam **sem confirmação** (comportamento do `QDialog::reject()`); **Esc não foi observado no Windows**.
- Tema escuro forçado (o diálogo não segue o tema claro do sistema).
- "Itens restantes" cai para a contagem de itens de origem quando o libfm não envia contagem de arquivos.
- Os quadros de referência ficam **fora do Git** (`.state/`); as medições não são reproduzíveis sem a VM.
- A região `grafico_cor_curva` do comparador passou a `diferente` por causa da regra "sem platô à esquerda"
  com fixture cuja primeira amostra está em 5 %, **não** por regressão de renderização.
- O estado azul (em andamento) do gráfico **não foi medido**.
- **Pausa cooperativa: o ACK só é confirmado depois que o glib termina cada chamada `copy_file_range` (até
  2 GiB−4 KiB; arquivos menores só confirmam no fim). É limitação do backend, não do diálogo; o Windows
  confirmou em ≤ ~610 ms.**

## Comparação (captura Qt × quadros Windows)

Como reproduzir (nada disto escreve no Git; quadros e saídas ficam em `.state/`):

```bash
source .deps/env.sh
export LD_LIBRARY_PATH=$PWD/.state/libfm-overlay-prefix/lib:$LD_LIBRARY_PATH
EXPLORER_TEST_ARTIFACTS=$PWD/.state/capture-run QT_QPA_PLATFORM=offscreen \
  .state/build/tests/tst_explorer transferDialogCapturesMeasuredStatesWhenArtifactsRequested
/usr/bin/python3 tools/fidelity/compare_transfer_dialog.py --ours .state/capture-run/transfer-dialog
```

O caso de teste grava (`grab()`, DPR 1, offscreen) `running-79.png`, `paused-32.png`,
`paused-32-expanded.png` (com amostras de velocidade simuladas via `recordSpeedSample`, a mesma
rota dos contadores reais) e `cancelling-53.png`. Sem `EXPLORER_TEST_ARTIFACTS` não grava nada.
O script recorta os quadros da VM em (496,152) com 449×165/449×287, exclui a faixa de título
(y<32; nas estatísticas de pixels, y<40 por causa do brilho do banner da VM) e as regiões de
texto, e escreve `comparison.json`, `side_by_side_*.png` e `diff_*.png` em
`.state/fidelity/transfer-dialog-compare/<UTC>/` com sha256 de cada insumo.
Classes: `igual` (≤2 px e ΔRGB≤4), `proxima` (≤2 px, ΔRGB 4–8), `diferente` (>2 px ou ΔRGB>8),
`ausente` (não mensurável de um lado). Não há limiar de "passa/falha".

Execução final (pós-correções): `…/transfer-dialog-compare/20261114T004639Z/`, `comparison.json`
sha256 `bd5517b8…cfab`. Contagem por estado (igual/próxima/diferente/ausente):
andamento-79 25/0/4/0 · pausado-32 23/0/2/2 · pausado-32-expandido 29/1/2/0 · cancelando-53 23/0/4/2.
Fração de pixels não-texto com ΔRGB>8 (inclui halo do cursor, forma da curva e brilho do banner,
não é métrica de fidelidade): 6,6 % / 10,7 % / 12,6 % / 12,2 %.

### Desvios corrigidos (teste RED→GREEN e commit por desvio)
1. **Conteúdo com altura 0 ao abrir recolhido** (achado pela própria captura): `transferContent`
   saía 449×0 até alternar "Mais detalhes", deixando o diálogo cinza e em branco; o layout herdado
   da libfm (oculto) o espremia. Agora o layout herdado é desativado e a geometria usa as constantes.
2. **Anel interno do gráfico**: o quadro de referência tem, dentro da borda #575757, um anel de 1 px
   #383838 nos quatro lados (ΔRGB 12–19 contra o nosso); passou a ser desenhado.
3. **Grade horizontal do gráfico**: a referência tem 4 linhas internas em y=107/122/138/153 (locais
   17/32/48/63); o código desenhava 6 linhas a cada 15 px (extras em y local 2 e 77, ΔRGB 14–17).

### Desvios que ficam só no relatório
- **Preenchimento da barra × percentual**: no quadro da referência a barra termina em x=327 para o texto "79 %"
  (76,3 % do interior), em x=224 para "53 %" (47,8 %, nos dois quadros C), e em x=152 para "32 %" (31,8 %, igual ao nosso).
  Nossa barra é exatamente o percentual. Não se sabe se o Windows anima a barra ou usa outro contador; sem mais
  medição não se altera a lógica.
- **Cor de pico do glifo ✕**: referência 229 × nosso 241 (ΔRGB 12) no pixel mais claro. É pico de antialiasing de
  um traço de ≈1 px; centro (≤0,72 px) e caixa batem. Tinta integrada: referência ≈20–21 px² (quadros limpos)
  × nosso 25,8 px² — ambíguo (largura do traço), não corrigido.
- **Cantos inferiores arredondados**: referência tem 5 colunas "fora da janela" na última linha, o nosso é
  retangular. O arredondamento é do DWM/WM, não do widget.
- **Borda inferior**: ≈#373737–#393939 na referência × #3D3D3D nosso (ΔRGB 4–6, `proxima`).
- **Extensão do separador**: referência x 1..447, nosso 3..445 (2 px de cada lado, limite de `igual`);
  corrige a nota anterior "x 3..445" desta página, que não bate com a leitura atual dos quadros.
- **Linhas de grade verticais**: referência 185/224/263/302/342/381, nosso 185/225/264/304/343/383
  (≤2 px, `igual`). A referência corresponde a 28+39,2·k; o nosso usa 27+39,5·k. Não alterado (≤2 px).

- **Gráfico — curva sem platô à esquerda (revisão C4, diferença registrada)**: a curva agora começa na primeira
  amostra REAL (nada é desenhado à esquerda dela). A referência Windows tem a curva preenchida desde o canto
  inferior esquerdo; na captura `paused-32-expanded` a região `grafico_cor_curva` (pixel do canto inferior
  esquerdo) passou de `igual` (252,225,0) para `diferente` (nosso 75,71,37 = área tingida), porque a primeira
  amostra do fixture está em 5 % (a observação em 2 % é só a linha de base). Efeito deliberado do contrato (sem
  série inventada); o restante da comparação ficou idêntico (fração de pixels dRGB>8 inalterada: 6,55 / 10,66 /
  12,56 / 12,22 %). Não revertido.
- **Cores da grade (revisão H)**: medido pausado vertical 60,58,40 / horizontal sobre a área tingida 89,83,34.
  Nenhum alfa único do `blend` reproduz (60,58,40) (R exige a<0,0793, B exige a>0,0795), então o estado pausado
  usa as cores medidas literalmente; o estado em execução (azul) não tem medição e mantém o `blend` (0,08/0,22).

### Medido igual
Cor do fundo (#2C2C2C), borda da janela (#3D3D3D, 1 px), separador (y 120/242, #4D4D4D), borda/trilho/
preenchimento da barra (cores exatas; x 27..421, y 90..104; preenchimento y 91..103), centro dos glifos
⏸/▶/✕ (≤0,95 px), cor do ▶ esmaecido (121,121,121), chevron do rodapé (≤0,85 px), borda/área tingida/curva
do gráfico (cores exatas, área tingida terminando em x=153).

### Limitações
- Fontes: Segoe UI × fonte Qt; todo texto (resumo, percentual, detalhes, "Mais detalhes") foi excluído.
- Título: a faixa superior (≈32 px) não existe no cliente Qt (decoração do WM) e só aparece sem banner no quadro
  suplementar `B_pre_cmd2_1794614329586.png`, usado também como referência do gráfico porque o quadro-chave
  `B_1794614319274.png` tem o halo do cursor sobre o lado esquerdo do gráfico. Barra de título não comparada.
- Halo/cursor da VM cobre o ▶ no quadro pausado-32, o ✕ no quadro cancelando-53 e o lado direito da barra
  nesses dois quadros: essas leituras ficam `ausente` (o ▶ vem do quadro expandido, o ✕ dos quadros A/B-exp).
- Só 96 DPI, DPR 1; sem hover/pressed; sem estado em andamento do gráfico; a forma da curva não é comparada
  (nossas amostras são simuladas, as da referência são de uma cópia real).
- Captura offscreen: sem decoração, sombras nem cantos do DWM; não prova pixel a pixel nem aparência no X11 real.
