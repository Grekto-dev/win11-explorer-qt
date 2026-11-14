#include "exploreranimation.h"
#include <QMenu>
#include <QWidget>
#include <QApplication>
#include <QGuiApplication>
#include <QStyleHints>
#include <QPalette>
#include <QRegularExpression>
#include <QHash>

namespace PCManFM {
bool ExplorerAnimation::darkMode() {
    const auto scheme = QGuiApplication::styleHints()->colorScheme();
    if(scheme != Qt::ColorScheme::Unknown) {
        return scheme == Qt::ColorScheme::Dark;
    }
    return QApplication::palette().color(QPalette::Window).lightness() < 128;
}
QString ExplorerAnimation::themed(const QString& css, bool dark) {
    if(!dark) {
        return css;
    }
    static const QHash<QString,QString> map{
        {QStringLiteral("#f8f8f8"), QStringLiteral("#202020")}, {QStringLiteral("#cfcfcf"), QStringLiteral("#3a3a3a")},
        {QStringLiteral("#202020"), QStringLiteral("#f0f0f0")}, {QStringLiteral("#e8e8e8"), QStringLiteral("#191919")},
        {QStringLiteral("#e7e7e7"), QStringLiteral("#2e2e2e")}, {QStringLiteral("white"), QStringLiteral("#272727")},
        {QStringLiteral("#d6d6d6"), QStringLiteral("#3c3c3c")}, {QStringLiteral("#a0a0a0"), QStringLiteral("#7d7d7d")},
        {QStringLiteral("#eaeaea"), QStringLiteral("#383838")}, {QStringLiteral("#dedede"), QStringLiteral("#444444")},
        {QStringLiteral("#e5e5e5"), QStringLiteral("#3a3a3a")}, {QStringLiteral("#eeeeee"), QStringLiteral("#333333")},
        {QStringLiteral("#dddddd"), QStringLiteral("#444444")}, {QStringLiteral("#fdfdfd"), QStringLiteral("#2d2d2d")},
        {QStringLiteral("#e1e1e1"), QStringLiteral("#444444")}, {QStringLiteral("#dfdfdf"), QStringLiteral("#444444")},
        {QStringLiteral("#cce8ff"), QStringLiteral("#1d4e7a")}, {QStringLiteral("#0067c0"), QStringLiteral("#60cdff")},
        {QStringLiteral("#f1f1f1"), QStringLiteral("#333333")}, {QStringLiteral("#555"), QStringLiteral("#c0c0c0")},
        {QStringLiteral("#ededed"), QStringLiteral("#3a3a3a")}, {QStringLiteral("#fafafa"), QStringLiteral("#2b2b2b")},
        {QStringLiteral("#d8d8d8"), QStringLiteral("#454545")}, {QStringLiteral("#e9e9e9"), QStringLiteral("#3a3a3a")},
        {QStringLiteral("#f9f9f9"), QStringLiteral("#2b2b2b")}, {QStringLiteral("#d7d7d7"), QStringLiteral("#454545")},
        {QStringLiteral("#f0f0f0"), QStringLiteral("#3a3a3a")}, {QStringLiteral("#e2e2e2"), QStringLiteral("#444444")},
        {QStringLiteral("#f3f3f3"), QStringLiteral("#383838")}};
    static const QRegularExpression token(QStringLiteral("#[0-9a-fA-F]{3,6}\\b|\\bwhite\\b"));
    QString out; qsizetype last = 0;
    for(auto it = token.globalMatch(css); it.hasNext();) {
        const auto m = it.next();
        out += css.mid(last, m.capturedStart() - last);
        out += map.value(m.captured().toLower(), m.captured());
        last = m.capturedEnd();
    }
    return out + css.mid(last);
}
QString ExplorerAnimation::menuStyleSheet() {
    // Measured visual target: light opaque popup, thin neutral edge and compact rows.
    // Exact transition duration/easing remain indeterminate at ~65 ms median sampling.
    return themed(QStringLiteral(
        "QMenu { background:#f9f9f9; color:#202020; border:1px solid #d7d7d7;"
        " border-radius:8px; padding:4px; }"
        "QMenu::item { min-height:24px; padding:4px 28px 4px 12px;"
        " border-radius:4px; margin:1px 2px; }"
        "QMenu::item:selected { background:#f0f0f0; color:#202020; }"
        "QMenu::item:disabled { color:#a0a0a0; }"
        "QMenu::separator { height:1px; background:#e2e2e2; margin:4px 8px; }"));
}
QString ExplorerAnimation::buttonStyleSheet() {
    return themed(QStringLiteral(
        "QToolButton { border:0; background:transparent; border-radius:4px; }"
        "QToolButton:hover { background:#f3f3f3; }"
        "QToolButton:pressed { background:#e5e5e5; }"
        "QToolButton:checked { background:#ededed; }"
        "QToolButton:focus { outline:1px solid #0067c0; outline-offset:-2px; }"
        "QToolButton:disabled { color:#a0a0a0; }"));
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
