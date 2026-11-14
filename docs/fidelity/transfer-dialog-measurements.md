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
