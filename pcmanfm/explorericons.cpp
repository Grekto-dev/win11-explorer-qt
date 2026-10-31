// SPDX-License-Identifier: GPL-2.0-or-later
#include "explorericons.h"
#include <QFile>
#include <QHash>
#include <QResource>

static void initializeExplorerIconResources() {
    static const bool initialized = [] { Q_INIT_RESOURCE(explorer_icons); return true; }();
    Q_UNUSED(initialized)
}

namespace PCManFM {
namespace {
const QHash<QString, QString>& registry() {
    static const QHash<QString, QString> entries{
        {QStringLiteral("folder"), QStringLiteral("places/folder.svg")},
        {QStringLiteral("inode-directory"), QStringLiteral("places/folder.svg")},
        {QStringLiteral("folder-publicshare"), QStringLiteral("places/folder.svg")},
        {QStringLiteral("public"), QStringLiteral("places/folder.svg")},
        {QStringLiteral("folder-download"), QStringLiteral("places/folder-downloads.svg")},
        {QStringLiteral("folder-downloads"), QStringLiteral("places/folder-downloads.svg")},
        {QStringLiteral("downloads"), QStringLiteral("places/downloads.svg")},
        {QStringLiteral("folder-documents"), QStringLiteral("places/folder-documents.svg")},
        {QStringLiteral("documents"), QStringLiteral("places/documents.svg")},
        {QStringLiteral("folder-pictures"), QStringLiteral("places/folder-pictures.svg")},
        {QStringLiteral("pictures"), QStringLiteral("places/pictures.svg")},
        {QStringLiteral("folder-music"), QStringLiteral("places/folder-music.svg")},
        {QStringLiteral("music"), QStringLiteral("places/music.svg")},
        {QStringLiteral("folder-videos"), QStringLiteral("places/folder-videos.svg")},
        {QStringLiteral("videos"), QStringLiteral("places/videos.svg")},
        {QStringLiteral("user-desktop"), QStringLiteral("places/user-desktop.svg")},
        {QStringLiteral("desktop"), QStringLiteral("places/user-desktop.svg")},
        {QStringLiteral("computer"), QStringLiteral("places/computer.svg")},
        {QStringLiteral("optical-drive"), QStringLiteral("places/drive-optical.svg")},
        {QStringLiteral("network"), QStringLiteral("places/network.svg")},
        {QStringLiteral("trash"), QStringLiteral("places/user-trash.svg")},
        {QStringLiteral("home"), QStringLiteral("places/user-home.svg")},
        {QStringLiteral("onedrive"), QStringLiteral("places/onedrive.svg")},
        {QStringLiteral("cut"), QStringLiteral("actions/cut.svg")},
        {QStringLiteral("copy"), QStringLiteral("actions/copy.svg")},
        {QStringLiteral("paste"), QStringLiteral("actions/paste.svg")},
        {QStringLiteral("rename"), QStringLiteral("actions/rename.svg")},
        {QStringLiteral("delete"), QStringLiteral("actions/delete.svg")},
        {QStringLiteral("sort"), QStringLiteral("actions/sort.svg")},
        {QStringLiteral("view"), QStringLiteral("actions/view.svg")},
        {QStringLiteral("more"), QStringLiteral("actions/more.svg")},
        {QStringLiteral("new"), QStringLiteral("actions/new.svg")},
        {QStringLiteral("share"), QStringLiteral("actions/share.svg")},
        {QStringLiteral("text-document"), QStringLiteral("mimetypes/text-plain.svg")},
    };
    return entries;
}
}

QString ExplorerIcons::resourcePath(const QString& name) {
    initializeExplorerIconResources();
    const auto it = registry().constFind(name);
    if(it == registry().cend()) return {};
    return QStringLiteral(":/icons/win11-explorer-qt/scalable/") + it.value();
}

QIcon ExplorerIcons::icon(const QString& name) {
    const QString path = resourcePath(name);
    if(path.isEmpty() || !QFile::exists(path)) return {};

    static const QHash<QString, QString> normalStateResources{
        {QStringLiteral("cut"), QStringLiteral("actions/cut-normal.svg")},
        {QStringLiteral("copy"), QStringLiteral("actions/copy-normal.svg")},
        {QStringLiteral("rename"), QStringLiteral("actions/rename-normal.svg")},
        {QStringLiteral("delete"), QStringLiteral("actions/delete-normal.svg")},
        // Clipboard content was not measured. Paste Normal deliberately keeps
        // the existing pale fallback; only Disabled is reference-observed.
        {QStringLiteral("paste"), QStringLiteral("actions/paste.svg")},
    };
    const auto normalState = normalStateResources.constFind(name);
    if(normalState == normalStateResources.cend()) return QIcon(path);

    const QString normalPath = QStringLiteral(":/icons/win11-explorer-qt/scalable/") + normalState.value();
    if(!QFile::exists(normalPath)) return QIcon(path);

    QIcon icon;
    icon.addFile(normalPath, QSize(24, 24), QIcon::Normal, QIcon::Off);
    icon.addFile(path, QSize(24, 24), QIcon::Disabled, QIcon::Off);
    return icon;
}
}
