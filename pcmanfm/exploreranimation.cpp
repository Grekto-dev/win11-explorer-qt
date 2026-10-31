#include "exploreranimation.h"
#include <QMenu>
#include <QWidget>

namespace PCManFM {
QString ExplorerAnimation::menuStyleSheet() {
    // Measured visual target: light opaque popup, thin neutral edge and compact rows.
    // Exact transition duration/easing remain indeterminate at ~65 ms median sampling.
    return QStringLiteral(
        "QMenu { background:#f9f9f9; color:#202020; border:1px solid #d7d7d7;"
        " border-radius:8px; padding:4px; }"
        "QMenu::item { min-height:24px; padding:4px 28px 4px 12px;"
        " border-radius:4px; margin:1px 2px; }"
        "QMenu::item:selected { background:#f0f0f0; color:#202020; }"
        "QMenu::item:disabled { color:#a0a0a0; }"
        "QMenu::separator { height:1px; background:#e2e2e2; margin:4px 8px; }");
}
QString ExplorerAnimation::buttonStyleSheet() {
    return QStringLiteral(
        "QToolButton { border:0; background:transparent; border-radius:4px; }"
        "QToolButton:hover { background:#f3f3f3; }"
        "QToolButton:pressed { background:#e5e5e5; }"
        "QToolButton:checked { background:#ededed; }"
        "QToolButton:focus { outline:1px solid #0067c0; outline-offset:-2px; }"
        "QToolButton:disabled { color:#a0a0a0; }");
}
void ExplorerAnimation::styleMenu(QMenu* menu) {
    if(menu) menu->setStyleSheet(menuStyleSheet());
}
void ExplorerAnimation::styleButton(QWidget* button) {
    if(button && button->objectName() == QStringLiteral("explorerClose")) {
        button->setStyleSheet(buttonStyleSheet() + QStringLiteral("QToolButton#explorerClose:hover { background:#e81123; }"));
    } else if(button) {
        button->setStyleSheet(buttonStyleSheet());
    }
}
}
