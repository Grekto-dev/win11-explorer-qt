# Ícones — reconstrução estática (evidência: 30/10/2026)

## Evidência e recortes medidos

Referência antiga conferida: `.state/reference/explorer-window.png`, 1104×642 px; as medidas gerais e as cores de superfície vêm de `.state/reference/measurements.json` (build 26300.9711, Display Version 26H2). Não havia `STATIC_READY.json` da fase 2 no início da execução, portanto não foi usada evidência nova. A captura é estado estático, não há quadros de hover/pressed/disabled/seleção.

Coordenadas abaixo são pixels do PNG (origem no canto superior esquerdo), aproximadas pelo recorte visual; caixas incluem antialias e não são precisão subpixel. Cores são descrições visuais, não amostras RGB instrumentadas. Medidas de layout preexistentes: file list/topo y=135; ícones de linha de 16×16 aproximadamente. Captura não permite identificar asset original ou medir vetores subjacentes.

| Região / item efetivamente visível | Caixa aproximada (x,y,w,h) px | Observação visual usada |
|---|---:|---|
| Comandos: Novo (+) | (17,103,17,18) | círculo/mais escuro; rótulo Novo |
| Cortar | (119,103,18,19) | tesoura azul-clara |
| Copiar | (168,103,17,18) | folhas sobrepostas azul-claro |
| Colar | (216,103,18,19) | prancheta azul-clara |
| Renomear | (264,103,18,19) | letras/caixa azul-claro |
| Compartilhar | (312,103,18,19) | seta de saída azul-clara |
| Excluir | (359,103,18,19) | lixeira cinza |
| Classificar | (410,103,21,19) | setas azul/cinza; separada do texto |
| Visualizar | (536,103,21,19) | linhas cinza |
| Mais | (650,103,19,19) | três pontos escuros |
| OneDrive (nuvem) | (33,143,36,24) | nuvem azul, aparece maior que ícones de pasta |
| Área de Trabalho | (33,208,19,17) | monitor ciano com base; nome truncado |
| Downloads | (34,239,16,18) | seta verde para baixo/linha |
| Documentos | (34,272,17,18) | folha azul-acinzentada, linhas claras |
| Imagens | (33,305,18,17) | quadro azul com montanha clara |
| Músicas | (33,337,18,18) | disco circular rosa/salmão, nota branca |
| Vídeos | (33,370,18,17) | tira roxa, perfurações e play |
| Pastas Capturas de Tela e Público | (33,400,34,49) total | duas pastas amarelas comuns; cada glyph ~17×15 |
| Este Computador | (33,496,19,18) | monitor ciano; linha selecionada cinza, estado do item e não do ícone |
| Unidade de CD | (34,529,18,18) | unidade cinza e disco claro |
| Rede | (33,562,18,18) | globo azul |
| Amostras RGB mais frequentes nas caixas de recorte | caixas acima | Cortar/copiar/colar/renomear usam azul claro; nas caixas coloridas houve `#a3ceef` (16 px); o sinal de Novo mostrou `#0078d4` (9 px); Downloads `#39cf8e` (3 px); Documentos `#92adc8` (3 px); Imagens `#1691d9` (4 px); Música `#e58268` (3 px); Vídeos `#8e4beb` (8 px). Contagens baixas decorrem da escala e antialias; são pixels exatos do PNG, não estimativas vetoriais. |
| Pastas nos itens da lista | (177,171,18,16) | pastas amarelas idênticas em Documentos/Imagens/Projetos |
| Arquivos de texto | (180,255,14,17), (180,283,14,17) | folhas brancas/cinzas com linhas cinza-azuis |

A geometria colorida da seleção em “Este Computador” pertence ao fundo da linha e não foi incorporada ao SVG do ícone. O screenshot deixa ícones da janela e barra de navegação visíveis, mas sua reconstrução existente em código não é coberta pelo registry novo. O ícone de pin (fixado) na lateral também foi observado (~13×17 px, cinza), mas não foi incluído no registry novo. Sem asset/crop de resolução superior ou manifesto estático novo, estes casos e todos os estados animados/alternativos são **NÃO VALIDADOS**.

## SVGs e API

`icons/explorer/` já era um tema compilado pelo glob recursivo de `pcmanfm/CMakeLists.txt`; nenhuma mudança ao CMake de produção é necessária para os SVGs. Foram acrescentadas recriações SVG autorais dos comandos observados (`actions/{cut,copy,paste,rename,delete,sort,view,more,new,share}.svg` e `places/{documents,downloads,pictures,music,videos}.svg`) e da nuvem `places/onedrive.svg`; sem screenshot raster, DLL, fonte ou recurso Microsoft. `index.theme` agora lista a seção `scalable/actions`.

`pcmanfm/explorericons.{h,cpp}` expõe `PCManFM::ExplorerIcons::icon(name)` e `resourcePath(name)`, força o registro do qrc `explorer_icons` (necessário no link estático), retorna ícone nulo para nome desconhecido e enumera aliases explicitamente. Aliases de diretório: `folder`, `inode-directory` e `folder-publicshare` compartilham exatamente o SVG `places/folder.svg`. Nomes canônicos de lugares apontam para SVGs do tema preexistentes, incluindo `downloads`, `documents`, `pictures`, `music`, `videos`, `desktop`, `computer`, `optical-drive`, `network`, `trash`, `home` e `text-document`. Esses SVGs preexistentes não foram redesenhados nesta entrega.

## Testes e render

Teste QtTest separado (não conectado ao CMake compartilhado):

```sh
source /home/pedro/win11-explorer-qt/.deps/env.sh
cmake -S tests/icons -B .state/icons-test -G Ninja -DCMAKE_MAKE_PROGRAM="$NINJA_BIN" -DCMAKE_PREFIX_PATH="$CMAKE_PREFIX_PATH"
cmake --build .state/icons-test --parallel 2
EXPLORER_ICONS_CONTACT_SHEET="$PWD/.state/icons-contact-sheet.png" QT_QPA_PLATFORM=offscreen .state/icons-test/tst_explorericons
```

Executado em Qt 6.8.3 real: 6/6 casos QtTest passaram, renderizando 25 IDs/aliases selecionados em 16, 20, 24 e 32 px; também verifica aliases, desconhecido→nulo, presença de pixels e que SVGs novos não têm script/URL externa/entidade. Os SVGs standalone de Documentos, Downloads, Imagens, Música e Vídeos foram feitos a partir dos glyphs da lateral observados; `folder-*` continua separado para ícones de pastas. Home e lixeira são mapeamentos de artwork preexistente, não vistos nessa captura: **NÃO VALIDADO**. O SVG “properties” não foi incluído porque não há evidência para ele. Os testes renderizam modo normal apenas; estados Disabled/Active/Selected não são simulados porque não foram observados/documentados para esses ícones. Contact sheet PNG: `.state/icons-contact-sheet.png`, 640×160, ícones a 32 px; verificação visual confirma 16 amostras sem glyph vazio. `ctest` não estava disponível no PATH desta imagem, mas o binário QtTest foi executado diretamente e passou.

## Instruções exatas de integração (fora do escopo deste commit)

1. Em `pcmanfm/CMakeLists.txt`, adicionar `explorericons.cpp` à lista `pcmanfm_CORE_SRCS`; não adicionar outro resource glob nem um segundo qrc (o glob e resource `explorer_icons` já estão configurados).
2. Incluir `explorericons.h` somente nos pontos de integração desejados. Mapear os ícones do modelo de lugares para os IDs semânticos acima e os comandos correspondentes para `new`, `cut`, `copy`, `paste`, `rename`, `share`, `delete`, `sort`, `view` e `more`; preservar as QActions/conexões existentes. As operações de fechar/voltar/avançar/atualizar/janela não foram objeto deste pacote.
3. Criar target de teste à parte a partir de `tests/icons/CMakeLists.txt` se desejado; não editar CMake de testes compartilhado nesta onda. Recursos de produção usam exatamente o nome `explorer_icons`, que a API inicializa uma vez para evitar descarte de qrc em link estático.
4. Conferir a exibição em tamanho de toolbar e lugares no Linux integrado e comparar estado equivalente da referência Windows; este pacote ainda não declara fidelidade visual em pixels.
