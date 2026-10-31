// SPDX-License-Identifier: GPL-2.0-or-later
#include "explorericons.h"
#include <QImage>
#include <QPainter>
#include <QTest>
using namespace PCManFM;

class ExplorerIconsTest : public QObject {
    Q_OBJECT
private slots:
    void aliasesShareResource();
    void unknownNameReturnsNull();
    void iconsRenderAtRequestedSizes();
    void svgResourcesAreLocalAndPassive();
};

void ExplorerIconsTest::aliasesShareResource() {
    QCOMPARE(ExplorerIcons::resourcePath(QStringLiteral("folder")), ExplorerIcons::resourcePath(QStringLiteral("inode-directory")));
    QCOMPARE(ExplorerIcons::resourcePath(QStringLiteral("folder")), ExplorerIcons::resourcePath(QStringLiteral("folder-publicshare")));
    QVERIFY(ExplorerIcons::resourcePath(QStringLiteral("downloads")) != ExplorerIcons::resourcePath(QStringLiteral("folder-downloads")));
    QVERIFY(!ExplorerIcons::resourcePath(QStringLiteral("documents")).isEmpty());
    QVERIFY(!ExplorerIcons::resourcePath(QStringLiteral("computer")).isEmpty());
    QVERIFY(!ExplorerIcons::resourcePath(QStringLiteral("onedrive")).isEmpty());
    QVERIFY(!ExplorerIcons::resourcePath(QStringLiteral("cut")).isEmpty());
}

void ExplorerIconsTest::unknownNameReturnsNull() {
    QVERIFY(ExplorerIcons::icon(QStringLiteral("not-an-explorer-icon")).isNull());
    QVERIFY(ExplorerIcons::resourcePath(QStringLiteral("not-an-explorer-icon")).isEmpty());
}

void ExplorerIconsTest::iconsRenderAtRequestedSizes() {
    const QStringList names{QStringLiteral("folder"), QStringLiteral("documents"), QStringLiteral("downloads"),
        QStringLiteral("pictures"), QStringLiteral("music"), QStringLiteral("videos"), QStringLiteral("desktop"), QStringLiteral("computer"),
        QStringLiteral("optical-drive"), QStringLiteral("network"), QStringLiteral("trash"), QStringLiteral("home"), QStringLiteral("public"),
        QStringLiteral("text-document"), QStringLiteral("onedrive"), QStringLiteral("cut"), QStringLiteral("copy"), QStringLiteral("paste"),
        QStringLiteral("rename"), QStringLiteral("delete"), QStringLiteral("sort"), QStringLiteral("view"), QStringLiteral("more"),
        QStringLiteral("new"), QStringLiteral("share")};
    for(const auto& name : names) {
        const QIcon icon = ExplorerIcons::icon(name);
        QVERIFY2(!icon.isNull(), qPrintable(name));
        for(int size : {16, 20, 24, 32}) {
            const QPixmap pixmap = icon.pixmap(size, size);
            QCOMPARE(pixmap.size(), QSize(size, size));
            QVERIFY2(!pixmap.isNull(), qPrintable(name));
            QVERIFY2(!pixmap.toImage().isNull(), qPrintable(name));
            bool hasInk = false;
            const QImage rendered = pixmap.toImage();
            for(int y = 0; y < size && !hasInk; ++y)
                for(int x = 0; x < size; ++x)
                    if(rendered.pixelColor(x, y).alpha() > 0) { hasInk = true; break; }
            QVERIFY2(hasInk, qPrintable(name));
        }
    }
    const QString output = qEnvironmentVariable("EXPLORER_ICONS_CONTACT_SHEET");
    if(!output.isEmpty()) {
        const QStringList labels{QStringLiteral("folder"), QStringLiteral("documents"), QStringLiteral("downloads"),
            QStringLiteral("pictures"), QStringLiteral("music"), QStringLiteral("videos"), QStringLiteral("computer"),
            QStringLiteral("onedrive"), QStringLiteral("cut"), QStringLiteral("copy"), QStringLiteral("paste"),
            QStringLiteral("rename"), QStringLiteral("delete"), QStringLiteral("sort"), QStringLiteral("view"), QStringLiteral("more")};
        QImage sheet(640, 160, QImage::Format_ARGB32_Premultiplied);
        sheet.fill(Qt::white);
        QPainter painter(&sheet);
        for(int i = 0; i < labels.size(); ++i) {
            const int x = (i % 8) * 80;
            const int y = (i / 8) * 80;
            ExplorerIcons::icon(labels[i]).paint(&painter, QRect(x + 24, y + 4, 32, 32));
            painter.setPen(QColor(32, 32, 32));
            painter.drawText(QRect(x, y + 42, 80, 24), Qt::AlignHCenter, labels[i]);
        }
        QVERIFY(sheet.save(output));
    }
}

void ExplorerIconsTest::svgResourcesAreLocalAndPassive() {
    const QStringList names{QStringLiteral("onedrive"), QStringLiteral("documents"), QStringLiteral("downloads"),
        QStringLiteral("pictures"), QStringLiteral("music"), QStringLiteral("videos"), QStringLiteral("new"),
        QStringLiteral("share"), QStringLiteral("cut"), QStringLiteral("copy"), QStringLiteral("paste"),
        QStringLiteral("rename"), QStringLiteral("delete"), QStringLiteral("sort"), QStringLiteral("view"), QStringLiteral("more")};
    for(const auto& name : names) {
        QFile f(ExplorerIcons::resourcePath(name));
        QVERIFY2(f.open(QIODevice::ReadOnly), qPrintable(name));
        const QByteArray svg = f.readAll().toLower();
        QVERIFY(!svg.contains("<script"));
        QVERIFY(!svg.contains("javascript:"));
        QVERIFY(!svg.contains("href="));
        QVERIFY(!svg.contains("url("));
        QCOMPARE(svg.count("http://"), 1);
        QVERIFY(svg.contains("http://www.w3.org/2000/svg"));
        QVERIFY(!svg.contains("<!entity"));
    }
}

QTEST_MAIN(ExplorerIconsTest)
#include "tst_explorericons.moc"
