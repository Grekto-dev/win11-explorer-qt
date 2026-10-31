# Compilação e ambiente de desenvolvimento

## Dependências

O projeto requer Qt **6.6 ou superior**, libfm-qt **2.4** e lxqt-build-tools **2.4**, com CMake e Ninja. Um SDK de sistema pode ser usado se seus arquivos CMake/pkg-config estiverem visíveis para as ferramentas. O script de build não baixa nem instala dependências e, se a configuração falhar, informa os requisitos a conferir.

Neste checkout, a compilação foi preparada sem privilégios administrativos em `.deps/`: Qt 6.8.3 (`linux_gcc_64`), libfm-qt 2.4.0, lxqt-build-tools 2.4.0, lxqt-menu-data 2.4.0, CMake 4.4.4 e Ninja 1.13.2. Bibliotecas e cabeçalhos adicionais foram extraídos localmente sob `.deps/sysroot`; libfm-qt foi instalado em `.deps/prefix`. Os caminhos absolutos do SDK e os exports CMake não são relocáveis.

`.deps/env.sh` define os caminhos do SDK, pkg-config, plugins/bibliotecas Qt e wrappers locais de CMake/Ninja. `scripts/build.sh` carrega esse arquivo se estiver presente. Esses arquivos e artefatos estão no checkout local e não constituem um bootstrap reproduzível para um clone limpo. A preparação completa desse SDK está registrada em `.deps/BUILD_NOTES.md`, incluindo a configuração de XKB usada para compilar a dependência; ela é uma anotação específica deste ambiente, não uma receita universal.

## Build e instalação local

Na raiz do repositório:

```bash
./scripts/build.sh                 # RelWithDebInfo
./scripts/build.sh Debug
```

O build configura em `.state/build`, usa no máximo quatro jobs e instala apenas em `.state/install`. O comando não instala em `/usr` nem em outro prefixo de sistema. Depois, `./scripts/run.sh` carrega `.deps/env.sh` quando disponível e inicia o executável instalado. O script de build não executa testes; rode-os separadamente conforme a configuração do projeto.

Para requisitos adicionais, configuração CMake, SDK rootless e verificações realizadas, consulte o arquivo local `.deps/BUILD_NOTES.md`, quando presente. Ele não é versionado; não existe em clones novos. A pasta `.deps` é específica deste ambiente.
