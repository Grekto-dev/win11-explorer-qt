// SPDX-License-Identifier: GPL-2.0-or-later
#include "explorericons.h"
#include <QFile>
#include <QHash>
#include <QResource>
#include <QIconEngine>
#include <QImage>
#include <QPainter>
#include <QPixmap>
#include <QRgb>
#include "exploreranimation.h"

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

// Recolours the dark, fixed-colour SVG artwork for a dark palette. In a light
// scheme every call delegates to the plain SVG icon, so the light rendering is
// the unmodified artwork. Dark pixmaps are cached per size/mode/state.
class ThemedSvgEngine final : public QIconEngine {
public:
    explicit ThemedSvgEngine(QIcon plain) : plain_(std::move(plain)) {}
    QIconEngine* clone() const override { return new ThemedSvgEngine(plain_); }
    QString key() const override { return QStringLiteral("explorer-themed-svg"); }
    QString iconName() override { return plain_.name(); }
    bool isNull() override { return plain_.isNull(); }
    QSize actualSize(const QSize& size, QIcon::Mode mode, QIcon::State state) override { return plain_.actualSize(size, mode, state); }
    QList<QSize> availableSizes(QIcon::Mode mode, QIcon::State state) override { return plain_.availableSizes(mode, state); }
    QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override {
        const QPixmap base = plain_.pixmap(size, mode, state);
        if(!ExplorerAnimation::darkMode()) return base;
        const quint64 key = (quint64(quint32(size.width())) << 32) | (quint64(quint32(size.height())) << 8) | (quint64(mode) << 4) | quint64(state);
        const auto hit = cache_.constFind(key);
        if(hit != cache_.cend()) return hit.value();
        const QPixmap tinted = QPixmap::fromImage(tintForDark(base.toImage()));
        cache_.insert(key, tinted);
        return tinted;
    }
    void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode, QIcon::State state) override {
        if(!ExplorerAnimation::darkMode()) { plain_.paint(painter, rect, Qt::AlignCenter, mode, state); return; }
        painter->drawPixmap(rect, pixmap(rect.size(), mode, state));
    }
    // Neutral dark strokes take the dark-theme ink; dark accents are lightened until legible.
    static QImage tintForDark(const QImage& source) {
        QImage img = source.convertToFormat(QImage::Format_ARGB32);
        constexpr int ink = 235, wantedY = 175, darkY = 140, neutralSpread = 40;
        for(int y = 0; y < img.height(); ++y) {
            QRgb* line = reinterpret_cast<QRgb*>(img.scanLine(y));
            for(int x = 0; x < img.width(); ++x) {
                const QRgb px = line[x];
                const int r = qRed(px), g = qGreen(px), b = qBlue(px);
                const int luma = (r * 299 + g * 587 + b * 114) / 1000;
                if(qAlpha(px) == 0 || luma >= darkY) continue;
                if(qMax(r, qMax(g, b)) - qMin(r, qMin(g, b)) < neutralSpread) { line[x] = qRgba(ink, ink, ink, qAlpha(px)); continue; }
                const qreal t = qreal(wantedY - luma) / qreal(255 - luma);
                line[x] = qRgba(r + qRound((255 - r) * t), g + qRound((255 - g) * t), b + qRound((255 - b) * t), qAlpha(px));
            }
        }
        return img;
    }
private:
    QIcon plain_;
    mutable QHash<quint64, QPixmap> cache_;
};
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
    if(normalState == normalStateResources.cend()) return QIcon(new ThemedSvgEngine(QIcon(path)));

    const QString normalPath = QStringLiteral(":/icons/win11-explorer-qt/scalable/") + normalState.value();
    if(!QFile::exists(normalPath)) return QIcon(new ThemedSvgEngine(QIcon(path)));

    QIcon icon;
    icon.addFile(normalPath, QSize(24, 24), QIcon::Normal, QIcon::Off);
    icon.addFile(path, QSize(24, 24), QIcon::Disabled, QIcon::Off);
    return QIcon(new ThemedSvgEngine(icon));
}
}
