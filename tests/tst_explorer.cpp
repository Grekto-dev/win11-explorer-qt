// SPDX-License-Identifier: GPL-2.0-or-later
// Behavioral tests use the real PCManFM-Qt window and libfm-qt folder model.
#include "application.h"
#include "mainwindow.h"
#include "tabpage.h"
#include "preferencesdialog.h"
#include "explorericons.h"
#include <QCheckBox>
#include <QStandardItemModel>
#include <QStyledItemDelegate>
#include <QIconEngine>
#include <QPainter>
#include <QStyleOptionViewItem>
#include <QProxyStyle>
#include "view.h"
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
#include <QCryptographicHash>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <QTimer>
#include <QScopeGuard>
#include <QJsonArray>
#include <libfm-qt6/core/folderconfig.h>
#include <QColor>
#include <QAbstractItemView>
#include <QLibrary>
#include <QImage>
#include <QStringList>
#include <memory>

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
    void placesDelegatePaintsReplacementNotModelArtwork() {
        class NamedIcon final : public QIconEngine {
        public:
            QIconEngine* clone() const override { return new NamedIcon; }
            QString iconName() const override { return QStringLiteral("user-home"); }
            void paint(QPainter* p, const QRect& r, QIcon::Mode, QIcon::State) override {
                p->fillRect(r, Qt::magenta);
            }
        };
        QTemporaryDir fixture;
        PCManFM::MainWindow window(Fm::FilePath::fromLocalPath(fixture.path().toUtf8().constData()));
        auto* side = window.findChild<QWidget*>(QStringLiteral("sidePane"));
        QVERIFY(side);
        auto trees = side->findChildren<QTreeView*>();
        QVERIFY(!trees.isEmpty());
        auto* tree = trees.first();
        auto* delegate = tree->itemDelegate();
        QStandardItemModel model(1, 1);
        model.setData(model.index(0,0), QIcon(new NamedIcon), Qt::DecorationRole);
        QStyleOptionViewItem option;
        option.initFrom(tree);
        option.rect = QRect(0,0,160,32);
        option.decorationSize = QSize(16,16);
        auto render = [&](QAbstractItemDelegate* d) {
            QImage image(160,32,QImage::Format_ARGB32);
            image.fill(Qt::white);
            QPainter painter(&image);
            d->paint(&painter, option, model.index(0,0));
            return image;
        };
        const auto actual = render(delegate);
        QStyledItemDelegate upstream;
        QVERIFY(actual != render(&upstream));
        const auto replacement = PCManFM::ExplorerIcons::icon(QStringLiteral("user-home"));
        QVERIFY(!replacement.isNull());
        model.setData(model.index(0,0), replacement, Qt::DecorationRole);
        QCOMPARE(actual, render(&upstream));
    }
    void repeatedModeAndNavigationReuseOneLocalStyle() {
        QTemporaryDir fixture;
        QVERIFY(QDir(fixture.path()).mkdir(QStringLiteral("child")));
        const auto root = Fm::FilePath::fromLocalPath(fixture.path().toUtf8().constData());
        const auto nested = Fm::FilePath::fromLocalPath(fixture.filePath(QStringLiteral("child")).toUtf8().constData());
        QPointer<QStyle> shared = qApp->style();
        {
            PCManFM::MainWindow window(root);
            window.show();
            QVERIFY(QTest::qWaitForWindowExposed(&window));
            for(const auto mode : {Fm::FolderView::DetailedListMode, Fm::FolderView::IconMode,
                                   Fm::FolderView::DetailedListMode}) {
                window.currentPage()->setViewMode(mode);
                auto* view = window.currentPage()->folderView();
                auto* child = view->childView();
                auto* first = child->style();
                QVERIFY(first != shared);
                for(int i=0; i<8; ++i) {
                    window.currentPage()->setViewMode(mode);
                    QCOMPARE(view->childView(), child);
                    QCOMPARE(child->style(), first);
                    QCOMPARE(child->findChildren<QProxyStyle*>(QString(), Qt::FindDirectChildrenOnly).size(), 1);
                    const auto destination = i % 2 ? root : nested;
                    window.chdir(destination);
                    QTRY_VERIFY(window.currentPage()->path() == destination);
                    QCOMPARE(view->childView(), child);
                    QCOMPARE(child->style(), first);
                }
                QCOMPARE(qApp->style(), shared.data());
            }
        }
        QVERIFY(shared);
        QCOMPARE(qApp->style(), shared.data());
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
        const auto releaseButton = qScopeGuard([&] {
            fakeButton(display, 1, False, 0);
            XFlush(display);
        });

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
        QVERIFY(QTest::qWaitForWindowActive(window.data()));
        QTest::qWait(200);
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

    void nativeExplorerGesturesMatchWindows() {
        if(!qEnvironmentVariableIsSet("EXPLORER_ENABLE_XTEST")) QSKIP("Run with EXPLORER_ENABLE_XTEST=1 on isolated Xephyr :12");
        QCOMPARE(qEnvironmentVariable("DISPLAY"), QStringLiteral(":12"));
        QLibrary xtst(QStringLiteral("libXtst.so.6"));
        QVERIFY(xtst.load());
        using Motion=int(*)(Display*,int,int,int,unsigned long);
        using Button=int(*)(Display*,unsigned int,int,unsigned long);
        using Key=int(*)(Display*,unsigned int,int,unsigned long);
        auto motion=reinterpret_cast<Motion>(xtst.resolve("XTestFakeMotionEvent"));
        auto button=reinterpret_cast<Button>(xtst.resolve("XTestFakeButtonEvent"));
        auto key=reinterpret_cast<Key>(xtst.resolve("XTestFakeKeyEvent"));
        QVERIFY(motion && button && key);
        Display* d=XOpenDisplay(nullptr);
        QVERIFY(d);
        struct Close { void operator()(Display* p) const { XCloseDisplay(p);
            } };
        std::unique_ptr<Display,Close> xd(d);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        QDir root(fixture.path());
        QVERIFY(root.mkdir(QStringLiteral("Target")));
        const QMap<QString,QByteArray> content{{QStringLiteral("Alpha.txt"),"Alpha test bytes\n"},{QStringLiteral("Bravo.txt"),"Bravo test bytes\n"},{QStringLiteral("Charlie.txt"),"C"},{QStringLiteral("Delta.txt"),"D"},{QStringLiteral("Echo.txt"),"E"},{QStringLiteral("Gamma.txt"),"G"}};
        for(auto it=content.cbegin();it!=content.cend();++it){ QFile f(root.filePath(it.key()));
            QVERIFY(f.open(QIODevice::WriteOnly));
            QCOMPARE(f.write(it.value()),qint64(it.value().size()));
        }
        auto digest=[](const QByteArray& bytes){ return QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());
        };
        auto fileHash=[&](const QString& path){ QFile f(path);
            if(!f.open(QIODevice::ReadOnly)) return QString();
            return digest(f.readAll());
        };
        QScopedPointer<PCManFM::MainWindow> window(new PCManFM::MainWindow(Fm::FilePath::fromLocalPath(fixture.path().toUtf8().constData())));
        window->resize(1104,642);
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window.data()));
        auto* v=window->currentPage()->folderView();
        QTRY_COMPARE_WITH_TIMEOUT(v->model()->rowCount(),7,5000);
        auto names=[&](){ QStringList out;
            for(const auto& ix:v->selectionModel()->selectedRows(0)) out<<ix.data().toString();
            out.sort();
            return out;
        };
        auto point=[&](const QString& name){ for(int i=0;i<v->model()->rowCount();++i){ auto ix=v->model()->index(i,0);
                if(ix.data().toString()==name) return v->childView()->viewport()->mapToGlobal(v->childView()->visualRect(ix).center());
            } return QPoint();
        };
        const auto ctrl=XKeysymToKeycode(d,XK_Control_L), shift=XKeysymToKeycode(d,XK_Shift_L);
        const auto releaseInput = qScopeGuard([&] {
            button(d,1,False,0); key(d,ctrl,False,0); key(d,shift,False,0); XFlush(d);
        });
        QJsonObject observations;
        const QString artifacts=qEnvironmentVariable("EXPLORER_TEST_ARTIFACTS");
        auto record = [&](const QString& stage) {
            observations.insert(stage, QJsonArray::fromStringList(names()));
            if(!artifacts.isEmpty()) {
                QDir().mkpath(artifacts);
                if(!window->grab().save(QDir(artifacts).filePath(stage + QStringLiteral(".png"))))
                QTest::qFail("Could not save gesture screenshot", __FILE__, __LINE__);
            }
        };
        auto wait=[&](int ms){ QElapsedTimer t;
            t.start();
            while(t.elapsed()<ms){QCoreApplication::processEvents(QEventLoop::AllEvents,20);
                QThread::msleep(5);
            } };
        auto click=[&](const QString& name,bool c=false,bool s=false){ const auto p=point(name);
            QVERIFY(!p.isNull());
            if(c)key(d,ctrl,True,0);
            if(s)key(d,shift,True,0);
            motion(d,DefaultScreen(d),p.x(),p.y(),0);
            button(d,1,True,0);
            button(d,1,False,0);
            XFlush(d);
            wait(300);
            if(s)key(d,shift,False,0);
            if(c)key(d,ctrl,False,0);
            wait(250);
        };
        click(QStringLiteral("Alpha.txt"));
        record(QStringLiteral("ctrl-after-alpha"));
        QCOMPARE(names(),QStringList{QStringLiteral("Alpha.txt")});
        click(QStringLiteral("Bravo.txt"),true);
        record(QStringLiteral("ctrl-after-bravo"));
        QCOMPARE(names(),QStringList({QStringLiteral("Alpha.txt"),QStringLiteral("Bravo.txt")}));
        click(QStringLiteral("Alpha.txt"),true);
        record(QStringLiteral("ctrl-after-toggle-alpha"));
        QCOMPARE(names(),QStringList{QStringLiteral("Bravo.txt")});
        click(QStringLiteral("Alpha.txt"));
        click(QStringLiteral("Gamma.txt"),false,true);
        record(QStringLiteral("shift-after-gamma"));
        QCOMPARE(names(),QStringList({QStringLiteral("Alpha.txt"),QStringLiteral("Bravo.txt"),QStringLiteral("Charlie.txt"),QStringLiteral("Delta.txt"),QStringLiteral("Echo.txt"),QStringLiteral("Gamma.txt")}));
        auto vp=v->childView()->viewport();
        auto p=vp->mapToGlobal(QPoint(100,vp->height()-60));
        motion(d,DefaultScreen(d),p.x(),p.y(),0);
        button(d,1,True,0);
        button(d,1,False,0);
        XFlush(d);
        wait(300);
        record(QStringLiteral("empty-clears"));
        QVERIFY(names().isEmpty());
        // Queue the complete gesture BEFORE dispatching events: QDrag::exec()
        // enters a nested loop, so synchronous processEvents-driven input hangs.
        auto drag=[&](const QString& name,bool copy) {
            const auto a=point(name), b=point(QStringLiteral("Target"));
            QVERIFY(!a.isNull() && !b.isNull());
            if(copy) key(d,ctrl,True,0);
            motion(d,DefaultScreen(d),a.x(),a.y(),0);
            button(d,1,True,0);
            XFlush(d);
            for(int i=1;i<=12;++i) {
                QTimer::singleShot(200+i*50, window.data(), [=] {
                    motion(d,DefaultScreen(d),a.x()+(b.x()-a.x())*i/12,a.y()+(b.y()-a.y())*i/12,0);
                    XFlush(d);
                });
            }
            QTimer::singleShot(1100, window.data(), [=] { button(d,1,False,0); XFlush(d); });
            QTimer::singleShot(1250, window.data(), [=] { key(d,ctrl,False,0); XFlush(d); });
            QTimer::singleShot(1400, window.data(), [=] {
                // A regression to the upstream action menu must fail, not hang.
                if(QApplication::activePopupWidget()) {
                    qWarning("Unexpected popup after drop; cancelling for bounded failure");
                    const auto escape = XKeysymToKeycode(d, XK_Escape);
                    key(d,escape,True,0); key(d,escape,False,0); XFlush(d);
                }
            });
            wait(1600);
        };
        const auto alpha=digest(content.value(QStringLiteral("Alpha.txt"))), bravo=digest(content.value(QStringLiteral("Bravo.txt")));
        drag(QStringLiteral("Alpha.txt"),false);
        QTRY_VERIFY_WITH_TIMEOUT(!QFile::exists(root.filePath(QStringLiteral("Alpha.txt"))),5000);
        QCOMPARE(fileHash(root.filePath(QStringLiteral("Target/Alpha.txt"))),alpha);
        record(QStringLiteral("move-complete"));
        drag(QStringLiteral("Bravo.txt"),true);
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(root.filePath(QStringLiteral("Target/Bravo.txt"))),5000);
        QVERIFY(QFile::exists(root.filePath(QStringLiteral("Bravo.txt"))));
        QCOMPARE(fileHash(root.filePath(QStringLiteral("Bravo.txt"))),bravo);
        QCOMPARE(fileHash(root.filePath(QStringLiteral("Target/Bravo.txt"))),bravo);
        record(QStringLiteral("copy-complete"));
        if(!artifacts.isEmpty()){QDir().mkpath(artifacts);
            QJsonObject report;
            report.insert(QStringLiteral("reference"),QStringLiteral("20261031T0147-0300/INTERACTIONS_READY.json"));
            report.insert(QStringLiteral("cases"),QJsonObject{{QStringLiteral("ctrl-toggle"),true},{QStringLiteral("shift-range-empty"),true},{QStringLiteral("dnd-default-move"),true},{QStringLiteral("dnd-ctrl-copy"),true}});
            report.insert(QStringLiteral("selectedNames"),observations);
            report.insert(QStringLiteral("filesystem"), QJsonObject{{QStringLiteral("moveSourceExists"),QFile::exists(root.filePath(QStringLiteral("Alpha.txt")))},{QStringLiteral("moveDestinationSHA256"),fileHash(root.filePath(QStringLiteral("Target/Alpha.txt")))},{QStringLiteral("copySourceSHA256"),fileHash(root.filePath(QStringLiteral("Bravo.txt")))},{QStringLiteral("copyDestinationSHA256"),fileHash(root.filePath(QStringLiteral("Target/Bravo.txt")))}});
            report.insert(QStringLiteral("sha256"),QJsonObject{{QStringLiteral("Alpha.txt"),alpha},{QStringLiteral("Bravo.txt"),bravo}});
            QFile f(QDir(artifacts).filePath(QStringLiteral("native-gesture-comparison.json")));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(QJsonDocument(report).toJson(QJsonDocument::Indented));
        }
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
