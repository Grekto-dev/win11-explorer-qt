# Revisão limitada de ownership UI/X11

## Escopo

Correções dos achados S1/L1 da revisão de `597b585`, restritas ao harness de
seleção. Não há alteração de aplicação de produção, pintura, proxy de estilo,
DND, ícones ou backend.

## Cadeia de roteamento investigada

Fontes primárias do Qt, tag `v6.8.3`:

- `qtbase/src/gui/kernel/qguiapplication.cpp`,
  `QGuiApplicationPrivate::createPlatformIntegration()`: lê `QT_QPA_PLATFORM`,
  permite que `-platform`/`--platform` o substituam e processa o caminho do plugin.
- `qtbase/src/plugins/platforms/xcb/qxcbintegration.cpp`,
  `QXcbIntegration::QXcbIntegration()`: processa `-display`/`--display` e passa o
  destino explícito a `QXcbConnection`.
- `qtbase/src/plugins/platforms/xcb/qxcbconnection_basic.cpp`,
  `QXcbBasicConnection::QXcbBasicConnection()`: abre o destino explícito ou,
  na ausência dele, `DISPLAY`; com `xcb_xlib`, o XCB compartilha esse `Display`.
- O SDK instalado expõe `QNativeInterface::QX11Application::display()` em
  `QtGui/qguiapplication_platform.h`.

A verificação antiga de uma conexão Xlib separada não vinculava a rota Qt.
A solução mínima é falhar antes do construtor quando o modo XTEST contém
qualquer override de display/plataforma/caminho de plugin ou quando
`QT_QPA_PLATFORM` não é exatamente `xcb`. Não removemos argumentos de `argv`.
Depois do construtor, antes de `app.init()`, exigimos um `Display` Qt não nulo e
verificamos seu próprio socket contra a identidade do Xephyr reservado.
Não há fallback para uma nova conexão quando a interface nativa está ausente.
Os modos não XTEST continuam aceitando a plataforma offscreen dos testes.

O negativo do runner agora escolhe `:2`, ou `:3` se o alvo legítimo for `:2`,
e verifica a distinção antes de iniciar o subprocesso. Esses números são apenas
strings para rejeição: o negativo não abre nenhum desses destinos.

## Cobertura e evidências locais

Artefatos ignorados em `.state/authorized-ui/`:

- `routing-red/`: o filho com `-platform offscreen` completou o caso (exit 0),
  quando deveria ser rejeitado (exit 2).
- `mismatch-red/`: com Xephyr próprio em `:19`, a asserção de distinção falhou
  antes de iniciar o subprocesso; não se reproduziu o defeito iniciando outro WM.
- `routing-green/`, `native-19/`, `native-2/`: rejeição e fluxos nativos.
- `ctest.log`: suite completa configurada, incluindo regressões anteriores.
- `ldd.log`: Qt 6.8.3 do SDK e libfm-qt do overlay privado desta worktree.
- `mutations/`, `mutation-routing/`, `mutation-fixed-target/`: cópias compiladas
  com retirada da guarda pré-Qt e retorno ao alvo fixo; ambos os testes ficam RED.
- `qt-peer-fault.c`, `actual-qt-peer-rejected/`, `mutant-separate-accepts/`:
  interposição local falsifica apenas o PID retornado por SO_PEERCRED para o
  socket Qt. A versão corrigida rejeita antes de QtTest; o mutante que consulta
  uma conexão Xlib nova aceita. Todas as conexões reais continuam no Xephyr
  próprio, sem injetar input nessa contraprova.

`qtRoutingGuardDoesNotConsumeQtTestArguments` exercita a política pura sem abrir
conexões: opções curtas/longas, valores separados/com `=`, argumentos truncados,
plataforma ausente/vazia/fallback/parametrizada e argumentos QtTest. O caso
nativo verifica rejeição por subprocesso e executa um caso nomeado com relatório
JUnit. `test_selection_wrapper.py` verifica ausência de Popen, execução de
casos, abertura Xlib e XTEST no negativo, inclusive com alvo legítimo `:19`.
As verificações nativas prévias de seleção, DND e limpeza continuam presentes.

## Limites

A política é deliberadamente conservadora: no modo XTEST, overrides são
rejeitados mesmo quando repetem o destino aprovado. Não é uma sandbox contra
código malicioso com o mesmo UID nem contra plugins/SDK adulterados. A integração
nativa depende da interface X11 do Qt usado; sua ausência resulta em rejeição.
A contraprova de credenciais e os binários mutantes são evidências locais,
não novos alvos do CTest. Não houve VM, integração de backend ou novo estudo de
paridade Windows nesta rodada. Baselines históricas não devem ser normalizadas.
