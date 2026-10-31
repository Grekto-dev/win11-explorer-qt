// SPDX-License-Identifier: GPL-2.0-or-later
#include "explorericons.h"
#include <QDirIterator>
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
    void observedToolbarStatesRenderExplicitColors_data();
    void observedToolbarStatesRenderExplicitColors();
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

void ExplorerIconsTest::observedToolbarStatesRenderExplicitColors_data() {
    QTest::addColumn<QString>("name");
    QTest::addColumn<int>("mode");
    QTest::addColumn<int>("size");
    QTest::addColumn<QColor>("accent");
    QTest::addColumn<QColor>("neutral");
    // Measured screenshot palette, not Qt's theme-generated disabled colors.
    for(const QString& name : {QStringLiteral("cut"), QStringLiteral("copy"),
                              QStringLiteral("rename"), QStringLiteral("delete"), QStringLiteral("paste")}) {
        for(int mode : {int(QIcon::Normal), int(QIcon::Disabled)}) {
            // Paste Normal was not observed: retain its existing pale fallback,
            // without claiming that it depicts an enabled clipboard command.
            const bool pale = mode == QIcon::Disabled || name == QStringLiteral("paste");
            const QColor neutral = pale ? QColor("#c2c2c2") : QColor("#555555");
            const QColor accent = name == QStringLiteral("delete") ? neutral :
                pale ? QColor(name == QStringLiteral("cut") ? "#add4f1" : "#a3ceef") :
                QColor(name == QStringLiteral("cut") ? "#1c87d9" : "#0078d4");
            for(int size : {16, 20, 24, 32}) {
                const QByteArray label = QStringLiteral("%1-%2-%3").arg(name)
                    .arg(mode == QIcon::Normal ? QStringLiteral("Normal") : QStringLiteral("Disabled")).arg(size).toLatin1();
                QTest::newRow(label.constData()) << name << mode << size << accent << neutral;
            }
        }
    }
}

void ExplorerIconsTest::observedToolbarStatesRenderExplicitColors() {
    QFETCH(QString, name);
    QFETCH(int, mode);
    QFETCH(int, size);
    QFETCH(QColor, accent);
    QFETCH(QColor, neutral);
    const QIcon icon = ExplorerIcons::icon(name);
    QVERIFY(!icon.isNull());
    const QImage image = icon.pixmap(size, size, QIcon::Mode(mode)).toImage();
    QCOMPARE(image.size(), QSize(size, size));
    auto pixelsNear = [&image](const QColor& color) {
        int count = 0;
        for(int y = 0; y < image.height(); ++y) {
            for(int x = 0; x < image.width(); ++x) {
                const QColor pixel = image.pixelColor(x, y);
                // Thin SVG strokes need not contain opaque pixels at 16 px.
                // Compare unpremultiplied color, excluding low-alpha rounding.
                if(pixel.alpha() >= 64 && qAbs(pixel.red() - color.red()) <= 4
                    && qAbs(pixel.green() - color.green()) <= 4
                    && qAbs(pixel.blue() - color.blue()) <= 4)
                    ++count;
            }
        }
        return count;
    };
    QVERIFY2(pixelsNear(accent) >= 2, qPrintable(QStringLiteral("missing accent %1").arg(accent.name())));
    QVERIFY2(pixelsNear(neutral) >= 2, qPrintable(QStringLiteral("missing neutral %1").arg(neutral.name())));

    const QString directory = qEnvironmentVariable("EXPLORER_ICONS_STATE_ARTIFACTS");
    if(!directory.isEmpty()) {
        QVERIFY(image.save(QStringLiteral("%1/%2.png").arg(directory, QString::fromLatin1(QTest::currentDataTag()))));
    }
}

void ExplorerIconsTest::svgResourcesAreLocalAndPassive() {
    // Initialize resources and scan every bundled SVG, including mode-only
    // files which are intentionally not public icon(name) registry entries.
    QVERIFY(!ExplorerIcons::resourcePath(QStringLiteral("cut")).isEmpty());
    QDirIterator files(QStringLiteral(":/icons/win11-explorer-qt/scalable"),
                       QStringList{QStringLiteral("*.svg")}, QDir::Files, QDirIterator::Subdirectories);
    int count = 0;
    while(files.hasNext()) {
        const QString name = files.next();
        ++count;
        QFile f(name);
        QVERIFY2(f.open(QIODevice::ReadOnly), qPrintable(name));
        const QByteArray svg = f.readAll().toLower();
        QVERIFY(!svg.contains("<script"));
        QVERIFY(!svg.contains("javascript:"));
        QVERIFY(!svg.contains("href="));
        QVERIFY(!svg.contains("url("));
        QCOMPARE(svg.count("http://"), 1);
        QVERIFY(svg.contains("http://www.w3.org/2000/svg"));
        QVERIFY(!svg.contains("<!entity"));
        QVERIFY(!svg.contains("<!doctype"));
        QVERIFY(!svg.contains("<foreignobject"));
        QVERIFY(!svg.contains("<image"));
        QVERIFY(!svg.contains("<animate"));
    }
    QVERIFY(count >= 29); // Base set plus four observed Normal-only variants.
}

QTEST_MAIN(ExplorerIconsTest)
#include "tst_explorericons.moc"
