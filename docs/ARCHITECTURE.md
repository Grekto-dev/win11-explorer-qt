# Win11 Explorer Qt — arquitetura e referência

## Escopo do primeiro marco

Fork real de `lxqt/pcmanfm-qt`, base fixada na tag **2.4.1**
(`56312ce4a0c728155a9127372bc7b84651caad08`), não uma interface web nem
um gerenciador novo com `QFileSystemModel` substituindo o motor existente.
Qt 6 + `libfm-qt` mantêm as páginas, histórico, modelos, clipboard e operações.

O novo executável chama-se `win11-explorer-qt`. O nome é provisório e não
indica vínculo com Microsoft ou LXQt. A interface é código original; não
extraímos fontes, ícones, bibliotecas, imagens de marca ou outros recursos
proprietários do Windows.

## Limites de isolamento

- Nemo continua como manipulador padrão de `inode/directory`.
- Não registrar nem entrar na fila de `org.freedesktop.FileManager1`.
- Não instalar autostart, ativação D-Bus genérica nem associações MIME.
- Configurações e serviço de instância única têm nomes próprios.
- Backend de desktop, alteração de papel de parede, daemon de sessão,
  automontagem e autorun não fazem parte desta aplicação.
- Nenhum `gsettings set`, modificação de applets, atalho Win+E ou
  substituição de arquivos de pacotes do sistema.
- Testes de escrita usam pastas temporárias próprias; nunca dados pessoais.

`scripts/check-isolation.py` registra/verifica metadados locais, incluindo o
proprietário do serviço FileManager1, o handler de pastas, os applets ativos,
a configuração de ícones do Nemo e hashes de arquivos de associações MIME.
A baseline real fica em `.state/`, ignorada pelo Git.

## Referência observada em 2026-10-30

VM Windows, Explorer em português, `DisplayVersion=26H2`, build
`26300.9711`. É a referência específica deste projeto, não uma afirmação
sobre todos os lançamentos estáveis do Windows 11.

MCP verificado por execução de comandos e árvore UIA. Captura limpa com
`virsh screenshot`, após desaparecerem os indicadores transitórios do MCP.
Janela pedida: `(120,100)`, `1120×650`; recorte visual sem bordas invisíveis
do DWM: `(128,100)–(1232,742)`, **1104×642**.

| Região | Intervalo vertical na captura | Altura |
|---|---:|---:|
| Abas e controles da janela | 100–140 | 40 px |
| Voltar, avançar, subir, atualizar, endereço, pesquisa | 140–189 | 49 px |
| Novo, operações, Classificar, Visualizar, mais | 189–236 | 47 px |
| Barra de status | 718–742 | 24 px |

Cores amostradas dos pixels, sem estimativa por miniatura:

| Superfície | Cor |
|---|---|
| Fundo das abas inativas | `#e8e8e8` |
| Navegação | `#f8f8f8` |
| Campo de endereço | `#fdfdfd` |
| Comandos, conteúdo, lateral, status | `#ffffff` |
| Divisória horizontal | `#d6d6d6` |

A referência contém um conjunto sintético: pastas Documentos, Imagens e
Projetos; arquivos Leia-me.txt e Notas.txt. Capturas e medições brutas ficam
em `.state/reference/` e não são incorporadas como recursos da aplicação.

## Etapas seguintes, sem promessa de paridade nesta primeira versão

1. Estrutura da janela e navegação funcional sobre os componentes upstream.
2. Lapidar sidebar, breadcrumbs, colunas, seleção, ícones e menus por
   comparação de capturas equivalentes, incluindo temas claro e escuro.
3. Páginas próprias equivalentes a Início/Este Computador, painéis de
   detalhes/visualização, opções e fluxos que fizerem sentido no Linux.
4. Acessibilidade, idiomas, testes extensivos de operações e empacotamento
   nativo. Não simular OneDrive/conta Microsoft ou letras de unidades.

Cada marco precisa compilar, abrir e ter os controles realmente exercitados.
Uma captura bonita não prova que copiar, renomear ou navegar funciona.
