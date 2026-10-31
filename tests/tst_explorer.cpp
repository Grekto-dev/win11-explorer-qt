// SPDX-License-Identifier: GPL-2.0-or-later
// Behavioral tests use the real PCManFM-Qt window and libfm-qt folder model.
#include "application.h"
#include "mainwindow.h"
#include "tabpage.h"
#include "preferencesdialog.h"
#include "explorericons.h"
#include <QCheckBox>
#include <QtTest>
#include <QTemporaryDir>
#include <QToolBar>
#include <QMenuBar>
#include <QToolButton>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTreeView>
#include <QHeaderView>
#include <QClipboard>
#include <QMimeData>
#include <libfm-qt6/pathbar.h>
#include <QDir>
#include <QFile>
#include <QScopedPointer>
#include <libfm-qt6/core/folderconfig.h>
#include <QColor>
#include <QAbstractItemView>
#include <QLibrary>
#include <QImage>
#include <QStringList>
#include <memory>
#include <X11/Xlib.h>

class ExplorerTest : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void defaultSettingsMatchExplorerDensity() {
        PCManFM::Settings settings;
        settings.load(QStringLiteral("defaults-test"));
        QCOMPARE(settings.windowWidth(), 1120);
        QCOMPARE(settings.windowHeight(), 650);
        QCOMPARE(settings.smallIconSize(), 16);
        QCOMPARE(settings.sidePaneIconSize(), 16);
        QCOMPARE(settings.viewMode(), Fm::FolderView::DetailedListMode);
        QVERIFY(settings.switchToNewTab());
        QVERIFY(!settings.showMenuBar());
        QVERIFY(!settings.mountOnStartup());
        QVERIFY(!settings.mountRemovable());
        QVERIFY(!settings.autoRun());
    }
    void localFilterUsesTheRealFolderModel() {
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        for(const auto& name : {QStringLiteral("alpha.txt"), QStringLiteral("beta.txt")}) {
            QFile file(folder.filePath(name));
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("test\n");
        }
        QScopedPointer<PCManFM::MainWindow> window(new PCManFM::MainWindow(Fm::FilePath::fromLocalPath(folder.path().toUtf8().constData())));
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window.data()));
        auto* model = window->currentPage()->folderView()->model();
        QTRY_COMPARE(model->rowCount(), 2);
        auto* search = window->findChild<QLineEdit*>(QStringLiteral("explorerSearch"));
        QVERIFY(search);
        search->setFocus();
        QTest::keyClicks(search, QStringLiteral("alpha"));
        QTRY_COMPARE_WITH_TIMEOUT(model->rowCount(), 1, 1500);
        QCOMPARE(model->index(0,0).data().toString(), QStringLiteral("alpha.txt"));
        search->clear();
        QTRY_COMPARE(model->rowCount(), 2);
    }
    void unsupportedVolumeAutomationIsNotOffered() {
        PCManFM::PreferencesDialog preferences;
        for(const auto& name : {QStringLiteral("mountOnStartup"),QStringLiteral("mountRemovable"),QStringLiteral("autoRun")}) {
            auto* checkbox = preferences.findChild<QCheckBox*>(name);
            QVERIFY(checkbox);
            QVERIFY(!checkbox->isEnabled());
            QVERIFY(!checkbox->toolTip().isEmpty());
        }
    }
    void escapeClearsTheVisibleLocalFilter() {
        QTemporaryDir folder;
        QScopedPointer<PCManFM::MainWindow> window(new PCManFM::MainWindow(Fm::FilePath::fromLocalPath(folder.path().toUtf8().constData())));
        window->show(); QVERIFY(QTest::qWaitForWindowExposed(window.data()));
        auto* search = window->findChild<QLineEdit*>(QStringLiteral("explorerSearch"));
        search->setFocus(); search->setText(QStringLiteral("alpha"));
        QTest::keyClick(search,Qt::Key_Escape);
        QTRY_COMPARE(search->text(),QString());
        QCOMPARE(window->currentPage()->getFilterStr(),QString());
    }
    void filterStateFollowsTheActiveTab() {
        QTemporaryDir folder;
        auto path = Fm::FilePath::fromLocalPath(folder.path().toUtf8().constData());
        QScopedPointer<PCManFM::MainWindow> window(new PCManFM::MainWindow(path));
        window->show(); QVERIFY(QTest::qWaitForWindowExposed(window.data()));
        auto* search = window->findChild<QLineEdit*>(QStringLiteral("explorerSearch"));
        search->setText(QStringLiteral("alpha"));
        window->addTab(path);
        QCOMPARE(window->currentPage()->getFilterStr(),QString());
        QCOMPARE(search->text(),QString());
        auto* tabs = window->findChild<QTabBar*>(QStringLiteral("explorerTabs"));
        tabs->setCurrentIndex(0);
        QCOMPARE(search->text(),QStringLiteral("alpha"));
    }
    void nativeWindowIdentityIsDistinct() {
        QCOMPARE(qApp->applicationName(), QStringLiteral("win11-explorer-qt"));
        QCOMPARE(QGuiApplication::desktopFileName(), QStringLiteral("win11-explorer-qt"));
        QCOMPARE(QGuiApplication::applicationDisplayName(), QStringLiteral("Win11 Explorer Qt"));
    }
    void navigationAndTabsStayWiredAfterReparenting() {
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        QVERIFY(QDir(folder.path()).mkdir(QStringLiteral("child")));
        auto root = Fm::FilePath::fromLocalPath(folder.path().toUtf8().constData());
        auto child = Fm::FilePath::fromLocalPath(folder.filePath(QStringLiteral("child")).toUtf8().constData());
        QScopedPointer<PCManFM::MainWindow> window(new PCManFM::MainWindow(root));
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window.data()));
        auto* tabs = window->findChild<QTabBar*>(QStringLiteral("explorerTabs"));
        auto* add = window->findChild<QToolButton*>(QStringLiteral("explorerNewTab"));
        QVERIFY(tabs && add);
        QTest::mouseClick(add, Qt::LeftButton);
        QTRY_COMPARE(tabs->count(), 2);
        QCOMPARE(tabs->currentIndex(), 1);
        window->chdir(child);
        QTRY_VERIFY(window->currentPage()->path() == child);
        QTest::mouseClick(window->findChild<QToolButton*>(QStringLiteral("explorerUp")), Qt::LeftButton);
        QTRY_VERIFY(window->currentPage()->path() == root);
        QTest::mouseClick(window->findChild<QToolButton*>(QStringLiteral("explorerBack")), Qt::LeftButton);
        QTRY_VERIFY(window->currentPage()->path() == child);
        QTest::mouseClick(window->findChild<QToolButton*>(QStringLiteral("explorerForward")), Qt::LeftButton);
        QTRY_VERIFY(window->currentPage()->path() == root);
        tabs->setCurrentIndex(0);
        QCOMPARE(window->currentPage()->path(), root);
        auto* close = tabs->tabButton(1,QTabBar::RightSide);
        if(!close) close = tabs->tabButton(1,QTabBar::LeftSide);
        QVERIFY(close);
        QTest::mouseClick(close, Qt::LeftButton);
        QTRY_COMPARE(tabs->count(), 1);
        auto* maximize = window->findChild<QToolButton*>(QStringLiteral("explorerMaximize"));
        QTest::mouseClick(maximize, Qt::LeftButton);
        QTRY_VERIFY(window->isMaximized());
        QTest::mouseClick(maximize, Qt::LeftButton);
        QTRY_VERIFY(!window->isMaximized());
    }
    void toolbarCopyPasteAndRenameOperateOnRealFiles() {
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        QDir dir(folder.path());
        QVERIFY(dir.mkdir(QStringLiteral("source"))); QVERIFY(dir.mkdir(QStringLiteral("destination")));
        const auto sourceDir = folder.filePath(QStringLiteral("source"));
        const auto destinationDir = folder.filePath(QStringLiteral("destination"));
        QFile source(QDir(sourceDir).filePath(QStringLiteral("original.txt")));
        QVERIFY(source.open(QIODevice::WriteOnly)); source.write("engine-roundtrip"); source.close();
        QScopedPointer<PCManFM::MainWindow> window(new PCManFM::MainWindow(Fm::FilePath::fromLocalPath(sourceDir.toUtf8().constData())));
        window->show(); QVERIFY(QTest::qWaitForWindowExposed(window.data()));
        auto* view = window->currentPage()->folderView();
        QTRY_COMPARE(view->model()->rowCount(),1);
        view->childView()->setCurrentIndex(view->model()->index(0,0));
        QTRY_VERIFY(window->findChild<QToolButton*>(QStringLiteral("explorerCopy"))->isEnabled());
        QTest::mouseClick(window->findChild<QToolButton*>(QStringLiteral("explorerCopy")),Qt::LeftButton);
        const auto* clipboard = QApplication::clipboard()->mimeData();
        QVERIFY(clipboard && clipboard->hasUrls());
        QCOMPARE(clipboard->urls().first().toLocalFile(),source.fileName());
        auto destinationPath = Fm::FilePath::fromLocalPath(destinationDir.toUtf8().constData());
        window->chdir(destinationPath);
        QTRY_VERIFY(window->currentPage()->path() == destinationPath);
        auto* paste = window->findChild<QToolButton*>(QStringLiteral("explorerPaste"));
        QTRY_VERIFY(paste->isEnabled());
        QTest::mouseClick(paste,Qt::LeftButton);
        QFile copied(QDir(destinationDir).filePath(QStringLiteral("original.txt")));
        QTRY_VERIFY_WITH_TIMEOUT(copied.exists(),5000);
        QVERIFY(copied.open(QIODevice::ReadOnly)); QCOMPARE(copied.readAll(),QByteArray("engine-roundtrip")); copied.close();
        view = window->currentPage()->folderView();
        QTRY_COMPARE(view->model()->rowCount(),1);
        view->childView()->setCurrentIndex(view->model()->index(0,0));
        auto* rename = window->findChild<QToolButton*>(QStringLiteral("explorerRename"));
        QTRY_VERIFY(rename->isEnabled()); QTest::mouseClick(rename,Qt::LeftButton);
        QLineEdit* editor = nullptr;
        QTRY_VERIFY((editor = view->childView()->findChild<QLineEdit*>()));
        editor->setText(QStringLiteral("renamed.txt")); QTest::keyClick(editor,Qt::Key_Return);
        QTRY_VERIFY(QFile::exists(QDir(destinationDir).filePath(QStringLiteral("renamed.txt"))));
        QVERIFY(!QFile::exists(copied.fileName())); QVERIFY(QFile::exists(source.fileName()));
        QApplication::clipboard()->clear();
    }
    void rubberBandUsesObservedSelectionPixels() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        for(const auto& name : {QStringLiteral("Nested Notes.txt"), QStringLiteral("Readme.txt")}) {
            QFile file(fixture.filePath(name));
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("selection fixture\n");
        }
        QScopedPointer<PCManFM::MainWindow> window(new PCManFM::MainWindow(Fm::FilePath::fromLocalPath(fixture.path().toUtf8().constData())));
        window->resize(1104, 642);
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window.data()));
        auto* folderView = window->currentPage()->folderView();
        QTRY_COMPARE(folderView->model()->rowCount(), 2);
        QStyle* sharedApplicationStyle = qApp->style();
        for(const auto mode : {Fm::FolderView::DetailedListMode, Fm::FolderView::IconMode}) {
            window->currentPage()->setViewMode(mode);
            auto* child = folderView->childView();
            auto* viewport = child->viewport();
            QTRY_VERIFY(viewport->width() > 200 && viewport->height() > 100);
            QVERIFY(child->style() != sharedApplicationStyle);
            QCOMPARE(qApp->style(), sharedApplicationStyle);
            const QPoint start(12, viewport->height() - 32);
            const QPoint end(190, viewport->height() - 10);
            QTest::mousePress(viewport, Qt::LeftButton, Qt::NoModifier, start);
            QTest::mouseMove(viewport, end, 20);
            QCoreApplication::processEvents();
            const auto image = viewport->grab().toImage().convertToFormat(QImage::Format_ARGB32);
            QCOMPARE(image.pixelColor((start.x() + end.x()) / 2, (start.y() + end.y()) / 2), QColor(170, 204, 238));
            QCOMPARE(image.pixelColor(start.x(), (start.y() + end.y()) / 2), QColor(0, 120, 212));
            QTest::mouseRelease(viewport, Qt::LeftButton, Qt::NoModifier, end);
        }
    }

    void nativeXTestSelectionGestures() {
        if(!qEnvironmentVariableIsSet("EXPLORER_ENABLE_XTEST")) {
            QSKIP("Set EXPLORER_ENABLE_XTEST=1 on an isolated owned Xephyr display to run native XTEST gestures");
        }
        const auto target = qEnvironmentVariable("EXPLORER_XTEST_DISPLAY", QStringLiteral(":12"));
        bool validNumber = false;
        const auto number = target.mid(1).toInt(&validNumber);
        if(!target.startsWith(QLatin1Char(':')) || !validNumber || number < 2
           || target != QStringLiteral(":%1").arg(number) || qEnvironmentVariable("DISPLAY") != target) {
            QFAIL("Native XTEST selection test requires the explicitly configured isolated display (never :0/:1)");
        }
        QLibrary xtst(QStringLiteral("libXtst.so.6"));
        QVERIFY2(xtst.load(), "libXtst is required for the native XTEST selection test");
        using FakeMotion = int (*)(Display*, int, int, int, unsigned long);
        using FakeButton = int (*)(Display*, unsigned int, int, unsigned long);
        auto fakeMotion = reinterpret_cast<FakeMotion>(xtst.resolve("XTestFakeMotionEvent"));
        auto fakeButton = reinterpret_cast<FakeButton>(xtst.resolve("XTestFakeButtonEvent"));
        QVERIFY(fakeMotion && fakeButton);
        Display* display = XOpenDisplay(nullptr);
        QVERIFY(display);
        struct DisplayCloser { void operator()(Display* d) const { if(d) XCloseDisplay(d); } };
        std::unique_ptr<Display, DisplayCloser> xdisplay(display);

        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QStringList expected{QStringLiteral("Nested Notes.txt"), QStringLiteral("Readme.txt")};
        for(const auto& name : expected) {
            QFile file(fixture.filePath(name));
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("native XTEST selection fixture\n");
        }
        QScopedPointer<PCManFM::MainWindow> window(new PCManFM::MainWindow(Fm::FilePath::fromLocalPath(fixture.path().toUtf8().constData())));
        window->resize(1104, 642);
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window.data()));
        auto* folderView = window->currentPage()->folderView();
        QTRY_COMPARE(folderView->model()->rowCount(), 2);
        auto injectReverseMarquee = [&](Fm::FolderView::ViewMode mode) {
            window->currentPage()->setViewMode(mode);
            auto* viewport = folderView->childView()->viewport();
            QTRY_VERIFY(viewport->width() > 200 && viewport->height() > 100);
            folderView->selectionModel()->clearSelection();
            const QPoint start = viewport->mapToGlobal(QPoint(viewport->width() - 8, viewport->height() - 8));
            const QPoint end = viewport->mapToGlobal(QPoint(8, 8));
            const int screen = DefaultScreen(display);
            QVERIFY(fakeMotion(display, screen, start.x(), start.y(), 0));
            QVERIFY(fakeButton(display, 1, True, 0));
            QVERIFY(fakeMotion(display, screen, end.x(), end.y(), 80));
            XFlush(display);
            QTest::qWait(250);
            auto* selectionModel = folderView->selectionModel();
            QModelIndexList selectedIndexes = selectionModel->selectedRows(0);
            if(mode == Fm::FolderView::IconMode && selectedIndexes.isEmpty()) {
                for(const auto& index : selectionModel->selectedIndexes()) {
                    if(index.column() == 0) selectedIndexes << index;
                }
            }
            QCOMPARE(selectedIndexes.size(), 2);
            QStringList selected;
            for(const auto& index : selectedIndexes) selected << index.data().toString();
            selected.sort();
            QStringList sortedExpected = expected;
            sortedExpected.sort();
            QCOMPARE(selected, sortedExpected);
            if(!qEnvironmentVariable("EXPLORER_TEST_ARTIFACTS").isEmpty()) {
                const QString suffix = mode == Fm::FolderView::DetailedListMode ? QStringLiteral("details") : QStringLiteral("grid");
                const auto dir = qEnvironmentVariable("EXPLORER_TEST_ARTIFACTS");
                QVERIFY(QDir().mkpath(dir));
                QVERIFY(window->grab().save(QDir(dir).filePath(QStringLiteral("selection-%1-held.png").arg(suffix))));
            }
            QVERIFY(fakeButton(display, 1, False, 0));
            XFlush(display);
            QTest::qWait(250);
            if(!qEnvironmentVariable("EXPLORER_TEST_ARTIFACTS").isEmpty()) {
                const QString suffix = mode == Fm::FolderView::DetailedListMode ? QStringLiteral("details") : QStringLiteral("grid");
                const auto dir = qEnvironmentVariable("EXPLORER_TEST_ARTIFACTS");
                QVERIFY(window->grab().save(QDir(dir).filePath(QStringLiteral("selection-%1-released.png").arg(suffix))));
            }
        };
        injectReverseMarquee(Fm::FolderView::DetailedListMode);
        injectReverseMarquee(Fm::FolderView::IconMode);
    }

    void chromeHasWindowsStructure() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString path = fixture.filePath(QStringLiteral("ExplorerReference"));
        QVERIFY(QDir().mkpath(path));
        for(const auto& name : {QStringLiteral("Documentos"),QStringLiteral("Imagens"),QStringLiteral("Projetos")}) QVERIFY(QDir(path).mkdir(name));
        for(const auto& name : {QStringLiteral("Leia-me.txt"),QStringLiteral("Notas.txt")}) {
            QFile file(QDir(path).filePath(name)); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("Referencia visual\n");
        }
        QScopedPointer<PCManFM::MainWindow> window(new PCManFM::MainWindow(Fm::FilePath::fromLocalPath(path.toUtf8().constData())));
        window->resize(1104, 642);
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window.data()));
        auto* title = window->findChild<QWidget*>(QStringLiteral("explorerTitleBar"));
        QVERIFY2(title, "Explorer requires a tabs/title row above navigation and commands");
        auto* navigation = window->findChild<QToolBar*>(QStringLiteral("explorerNavigation"));
        QVERIFY(navigation);
        auto* commands = window->findChild<QToolBar*>(QStringLiteral("explorerCommands"));
        QVERIFY(commands);
        const QList<QPair<QString,QString>> wiredIcons{
            {QStringLiteral("explorerNew"),QStringLiteral("new")}, {QStringLiteral("explorerCut"),QStringLiteral("cut")},
            {QStringLiteral("explorerCopy"),QStringLiteral("copy")}, {QStringLiteral("explorerPaste"),QStringLiteral("paste")},
            {QStringLiteral("explorerRename"),QStringLiteral("rename")}, {QStringLiteral("explorerDelete"),QStringLiteral("delete")},
            {QStringLiteral("explorerSort"),QStringLiteral("sort")}, {QStringLiteral("explorerView"),QStringLiteral("view")},
            {QStringLiteral("explorerMore"),QStringLiteral("more")}};
        for(const auto& [widgetName, iconName] : wiredIcons) {
            auto* button = window->findChild<QToolButton*>(widgetName);
            QVERIFY2(button, qPrintable(widgetName));
            QVERIFY2(button->icon().pixmap(20,20).toImage() == PCManFM::ExplorerIcons::icon(iconName).pixmap(20,20).toImage(), qPrintable(widgetName));
        }
        QCOMPARE(title->height(),40);
        QCOMPARE(navigation->height(),49);
        QCOMPARE(commands->height(),47);
        QVERIFY2(!QIcon::fromTheme(QStringLiteral("folder")).pixmap(16,16).isNull(), "Folder icons must render without a platform theme plugin");
        QVERIFY(title->mapTo(window.data(), QPoint()).y() < navigation->mapTo(window.data(), QPoint()).y());
        QVERIFY(navigation->mapTo(window.data(), QPoint()).y() < commands->mapTo(window.data(), QPoint()).y());
        auto* tabs = title->findChild<QTabBar*>();
        QVERIFY(tabs);
        QCOMPARE(tabs->count(), 1);
        QVERIFY(tabs->isVisible());
        QVERIFY(!tabs->tabIcon(0).isNull());
        auto* tree = qobject_cast<QTreeView*>(window->currentPage()->folderView()->childView());
        QVERIFY(tree);
        QCOMPARE(tree->header()->visualIndex(Fm::FolderModel::ColumnFileMTime),1);
        QVERIFY(tree->isColumnHidden(Fm::FolderModel::ColumnFileOwner));
        QVERIFY(tree->isColumnHidden(Fm::FolderModel::ColumnFileGroup));
        QVERIFY(!tree->alternatingRowColors());
        auto* search = window->findChild<QLineEdit*>(QStringLiteral("explorerSearch"));
        QVERIFY(search);
        QVERIFY(search->placeholderText().contains(QStringLiteral("Filtrar")));
        QVERIFY(!window->findChild<QMenuBar*>()->isVisible());
        QTRY_COMPARE(window->currentPage()->folderView()->model()->rowCount(), 5);
        const QString artifacts = qEnvironmentVariable("EXPLORER_TEST_ARTIFACTS");
        if(!artifacts.isEmpty()) {
            QVERIFY(QDir().mkpath(artifacts));
            QVERIFY(window->grab().save(QDir(artifacts).filePath(QStringLiteral("explorer-window.png"))));
            QJsonObject geometry;
            for(const auto* widget : window->findChildren<QWidget*>()) {
                if(widget->objectName().startsWith(QStringLiteral("explorer"))) {
                    const QPoint origin = widget->mapTo(window.data(),QPoint());
                    geometry.insert(widget->objectName(),QJsonObject{{QStringLiteral("x"),origin.x()},{QStringLiteral("y"),origin.y()},{QStringLiteral("w"),widget->width()},{QStringLiteral("h"),widget->height()},{QStringLiteral("visible"),widget->isVisible()}});
                }
            }
            QFile file(QDir(artifacts).filePath(QStringLiteral("layout.json")));
            QVERIFY(file.open(QIODevice::WriteOnly)); file.write(QJsonDocument(geometry).toJson());
        }
    }
};

int main(int argc, char** argv) {
    QTemporaryDir config;
    if(!config.isValid()) return 2;
    qputenv("XDG_CONFIG_HOME", config.path().toUtf8());
    qputenv("XDG_CACHE_HOME", (config.path() + QStringLiteral("/cache")).toUtf8());
    PCManFM::Application app(argc, argv);
    app.init();
    app.settings().load(QStringLiteral("ui-test"));
    const auto folderConfig = (config.path() + QStringLiteral("/folders.conf")).toUtf8();
    Fm::FolderConfig::init(folderConfig.constData());
    app.setQuitOnLastWindowClosed(false);
    ExplorerTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "tst_explorer.moc"
