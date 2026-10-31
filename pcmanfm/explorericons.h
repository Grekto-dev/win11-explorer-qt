// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef EXPLORERICONS_H
#define EXPLORERICONS_H
#include <QIcon>
#include <QString>
namespace PCManFM {
class ExplorerIcons final {
public:
    // Stable semantic IDs used by the chrome and folder model integration.
    static QString resourcePath(const QString& name);
    static QIcon icon(const QString& name);
};
}
#endif
