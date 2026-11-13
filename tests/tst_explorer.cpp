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
#include <libfm-qt6/folderitemdelegate.h>
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
#include <stdexcept>
#include <functional>
#include <QDragMoveEvent>
#include <QProcess>
#include <sys/stat.h>
#include "selection_x11_ownership.h"

namespace {
template<typename Body, typename Release>
bool checkedInputGesture(Body body, Release release) {
    const auto cleanup = qScopeGuard(release);
    bool completed = false;
    body(completed);
    return completed;
}
template<typename Gesture>
bool selectionModes(Gesture gesture) {
    for(const auto mode : {Fm::FolderView::DetailedListMode, Fm::FolderView::IconMode}) {
        if(!gesture(mode)) return false;
    }
    return true;
}
}

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
            explicit NamedIcon(QString name) : name_(std::move(name)) {}
            QIconEngine* clone() const override { return new NamedIcon(name_); }
            QString iconName() override { return name_; }
            QString name_;
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
        model.setData(model.index(0,0), QUrl::fromLocalFile(fixture.path()), Qt::UserRole);
        const auto unchangedPath = model.index(0,0).data(Qt::UserRole);
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
        QStyledItemDelegate upstream;
        const QList<QPair<QString,QString>> places{
            {QStringLiteral("folder"), QStringLiteral("folder")},
            {QStringLiteral("user-home"), QStringLiteral("home")},
            {QStringLiteral("folder-download"), QStringLiteral("downloads")},
            {QStringLiteral("folder-documents"), QStringLiteral("documents")},
            {QStringLiteral("folder-pictures"), QStringLiteral("pictures")},
            {QStringLiteral("folder-music"), QStringLiteral("music")},
            {QStringLiteral("folder-videos"), QStringLiteral("videos")},
            {QStringLiteral("user-desktop"), QStringLiteral("desktop")},
            {QStringLiteral("computer"), QStringLiteral("computer")},
            {QStringLiteral("network-workgroup"), QStringLiteral("network")},
            {QStringLiteral("drive-optical"), QStringLiteral("optical-drive")},
            {QStringLiteral("user-trash"), QStringLiteral("trash")}};
        for(const auto& [sourceName, semanticRole] : places) {
            model.setData(model.index(0,0), QIcon(new NamedIcon(sourceName)), Qt::DecorationRole);
            const auto actual = render(delegate);
            QVERIFY2(actual != render(&upstream), qPrintable(sourceName));
            QCOMPARE(model.index(0,0).data(Qt::DecorationRole).value<QIcon>().name(), sourceName);
            QCOMPARE(model.index(0,0).data(Qt::UserRole), unchangedPath);
            const auto replacement = PCManFM::ExplorerIcons::icon(semanticRole);
            QVERIFY(!replacement.isNull());
            model.setData(model.index(0,0), replacement, Qt::DecorationRole);
            QCOMPARE(actual, render(&upstream));
        }
        model.setData(model.index(0,0), QIcon(new NamedIcon(QStringLiteral("unobserved-service"))), Qt::DecorationRole);
        QCOMPARE(render(delegate), render(&upstream));
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
                QCOMPARE(child->findChildren<QProxyStyle*>(QString(), Qt::FindDirectChildrenOnly).size(), 1);
                QPointer<QProxyStyle> first = child->findChildren<QProxyStyle*>(QString(), Qt::FindDirectChildrenOnly).first();
                QVERIFY(child->style() != shared);
                for(int i=0; i<8; ++i) {
                    window.currentPage()->setViewMode(mode);
                    QCOMPARE(view->childView(), child);
                    QVERIFY(first);
                    QVERIFY(child->findChildren<QProxyStyle*>(QString(), Qt::FindDirectChildrenOnly).contains(first.data()));
                    QCOMPARE(child->findChildren<QProxyStyle*>(QString(), Qt::FindDirectChildrenOnly).size(), 1);
                    const auto destination = i % 2 ? root : nested;
                    window.chdir(destination);
                    QTRY_VERIFY(window.currentPage()->path() == destination);
                    // Folder settings can legitimately switch mode/recreate the
                    // child on navigation. Bound ownership on that child too.
                    child = view->childView();
                    QCOMPARE(child->findChildren<QProxyStyle*>(QString(), Qt::FindDirectChildrenOnly).size(), 1);
                    window.currentPage()->setViewMode(mode);
                    child = view->childView();
                    QCOMPARE(child->findChildren<QProxyStyle*>(QString(), Qt::FindDirectChildrenOnly).size(), 1);
                    first = child->findChildren<QProxyStyle*>(QString(), Qt::FindDirectChildrenOnly).first();
                }
                QCOMPARE(qApp->style(), shared.data());
            }
        }
        QVERIFY(shared);
        QCOMPARE(qApp->style(), shared.data());
    }
    void syntheticInputFailureCleanup() {
        class InputProbe : public QWidget {
        public:
            int releases = 0;
            bool pressed = false, ctrl = false, shift = false;
            void mousePressEvent(QMouseEvent*) override { pressed = true; }
            void mouseReleaseEvent(QMouseEvent*) override { pressed = false; ++releases; }
            void keyPressEvent(QKeyEvent* e) override {
                if(e->key() == Qt::Key_Control) ctrl = true;
                if(e->key() == Qt::Key_Shift) shift = true;
            }
            void keyReleaseEvent(QKeyEvent* e) override {
                if(e->key() == Qt::Key_Control) ctrl = false;
                if(e->key() == Qt::Key_Shift) shift = false;
            }
        } probe;
        probe.show();
        for(bool throws : {false, true}) {
            int attempts = 0;
            bool completed = false;
            try {
                completed = selectionModes([&](auto) {
                    ++attempts;
                    return checkedInputGesture([&](bool&) {
                        QTest::keyPress(&probe, Qt::Key_Control);
                        QTest::keyPress(&probe, Qt::Key_Shift);
                        QTest::mousePress(&probe, Qt::LeftButton, Qt::ControlModifier | Qt::ShiftModifier);
                        if(throws) throw std::runtime_error("injected after press");
                        return; // Same return path as a Qt assertion in a lambda.
                    }, [&] {
                        QTest::mouseRelease(&probe, Qt::LeftButton);
                        QTest::keyRelease(&probe, Qt::Key_Shift);
                        QTest::keyRelease(&probe, Qt::Key_Control);
                    });
                });
            } catch(const std::runtime_error&) {
                QVERIFY(throws);
            }
            QVERIFY(!completed);
            QCOMPARE(attempts, 1);
            QVERIFY(!probe.pressed);
            QVERIFY(!probe.ctrl && !probe.shift);
            QCOMPARE(probe.releases, throws ? 2 : 1);
        }
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
        QVERIFY(selectionModes([&](auto mode) {
            QWidget* viewport = nullptr;
            QPoint end;
            bool pressed = false;
            return checkedInputGesture([&](bool& complete) {
            window->currentPage()->setViewMode(mode);
            auto* child = folderView->childView();
            viewport = child->viewport();
            QTRY_VERIFY(viewport->width() > 200 && viewport->height() > 100);
            QVERIFY(child->style() != sharedApplicationStyle);
            QCOMPARE(qApp->style(), sharedApplicationStyle);
            const QPoint start(12, viewport->height() - 32);
            end = QPoint(190, viewport->height() - 10);
            pressed = true;
            QTest::mousePress(viewport, Qt::LeftButton, Qt::NoModifier, start);
            QTest::mouseMove(viewport, end, 20);
            QCoreApplication::processEvents();
            const auto image = viewport->grab().toImage().convertToFormat(QImage::Format_ARGB32);
            QCOMPARE(image.pixelColor((start.x() + end.x()) / 2, (start.y() + end.y()) / 2), QColor(170, 204, 238));
            QCOMPARE(image.pixelColor(start.x(), (start.y() + end.y()) / 2), QColor(0, 120, 212));
            QTest::mouseRelease(viewport, Qt::LeftButton, Qt::NoModifier, end);
            pressed = false;
            complete = true;
            }, [&] {
                if(pressed && viewport) QTest::mouseRelease(viewport, Qt::LeftButton, Qt::NoModifier, end);
            });
        }));
    }

    void rubberBandCompositesOverContent_data() {
        QTest::addColumn<int>("mode");
        QTest::newRow("details") << int(Fm::FolderView::DetailedListMode);
        QTest::newRow("grid") << int(Fm::FolderView::IconMode);
    }
    void rubberBandCompositesOverContent() {
        QFETCH(int, mode);
        class Markers final : public Fm::FolderItemDelegate {
        public:
            using Fm::FolderItemDelegate::FolderItemDelegate;
            void paint(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& index) const override {
                Fm::FolderItemDelegate::paint(p, opt, index);
                if(index.column() != 0) return;
                // Fixed colors independent of selected/hover state: the same
                // backing content must survive both real painting paths.
                p->fillRect(QRect(opt.rect.topLeft() + QPoint(12,4), QSize(12,8)), Qt::black);
                p->fillRect(QRect(opt.rect.topLeft() + QPoint(28,4), QSize(12,8)), Qt::white);
                p->fillRect(QRect(opt.rect.topLeft() + QPoint(44,4), QSize(12,8)), QColor(60,150,210));
            }
        };
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QStringList expected{QStringLiteral("Nested Notes.txt"), QStringLiteral("Readme.txt")};
        for(const auto& name : expected) {
            QFile file(fixture.filePath(name));
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("alpha regression\n");
        }
        PCManFM::MainWindow window(Fm::FilePath::fromLocalPath(fixture.path().toUtf8().constData()));
        window.resize(1104,642);
        window.currentPage()->setViewMode(static_cast<Fm::FolderView::ViewMode>(mode));
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto* view = window.currentPage()->folderView();
        QTRY_COMPARE(view->model()->rowCount(), 2);
        auto* child = view->childView();
        auto* original = qobject_cast<Fm::FolderItemDelegate*>(child->itemDelegateForColumn(0));
        QVERIFY(original);
        auto* markers = new Markers(child, child);
        markers->setItemSize(original->itemSize());
        markers->setIconSize(original->iconSize());
        markers->setMargins(original->getMargins());
        child->setItemDelegateForColumn(0, markers);
        auto* viewport = child->viewport();
        const auto index = view->model()->index(0,0);
        QTRY_VERIFY(child->visualRect(index).isValid());
        const auto origin = child->visualRect(index).topLeft();
        const QPoint black = origin + QPoint(16,8), white = origin + QPoint(32,8), color = origin + QPoint(48,8);
        QTest::qWait(50);
        const auto before = viewport->grab().toImage();
        QCOMPARE(before.pixelColor(black), QColor(Qt::black));
        QCOMPARE(before.pixelColor(white), QColor(Qt::white));
        QCOMPARE(before.pixelColor(color), QColor(60,150,210));
        const QPoint start(viewport->width()-8, viewport->height()-8), end(2,2);
        QTest::mousePress(viewport, Qt::LeftButton, Qt::NoModifier, start);
        const auto release = qScopeGuard([&] { QTest::mouseRelease(viewport, Qt::LeftButton, Qt::NoModifier, end); });
        QTest::mouseMove(viewport, end, 20);
        QTest::qWait(250);
        const auto held = viewport->grab().toImage();
        const auto artifacts = qEnvironmentVariable("EXPLORER_TEST_ARTIFACTS");
        const auto suffix = QString::fromLatin1(QTest::currentDataTag());
        if(!artifacts.isEmpty()) {
            QVERIFY(QDir().mkpath(artifacts));
            QVERIFY(before.save(QDir(artifacts).filePath(QStringLiteral("alpha-%1-before.png").arg(suffix))));
            QVERIFY(held.save(QDir(artifacts).filePath(QStringLiteral("alpha-%1-held.png").arg(suffix))));
        }
        QCOMPARE(held.pixelColor(black), QColor(0,34,68));
        QCOMPARE(held.pixelColor(white), QColor(170,204,238));
        QCOMPARE(held.pixelColor(color), QColor(40,134,208));
        QCOMPARE(held.pixelColor(end.x()+1, viewport->height()/2), QColor(0,120,212));
        const QPoint outside(viewport->width()-3, viewport->height()/2);
        QCOMPARE(held.pixelColor(outside), before.pixelColor(outside));
        QTest::mouseRelease(viewport, Qt::LeftButton, Qt::NoModifier, end);
        QTest::qWait(250);
        const auto released = viewport->grab().toImage();
        QCOMPARE(released.pixelColor(black), before.pixelColor(black));
        QCOMPARE(released.pixelColor(white), before.pixelColor(white));
        QCOMPARE(released.pixelColor(color), before.pixelColor(color));
        QCOMPARE(released.pixelColor(end.x()+1, viewport->height()/2), before.pixelColor(end.x()+1, viewport->height()/2));
        QStringList names;
        for(const auto& selected : view->selectionModel()->selectedIndexes())
            if(selected.column() == 0) names << selected.data().toString();
        names.sort();
        QCOMPARE(names, expected);
        if(!artifacts.isEmpty()) QVERIFY(released.save(QDir(artifacts).filePath(QStringLiteral("alpha-%1-released.png").arg(suffix))));
    }

    void localMovePolicyDoesNotClaimUnobservedDrops() {
        QTemporaryDir fixture;
        QFile source(fixture.filePath(QStringLiteral("source.txt")));
        QVERIFY(source.open(QIODevice::WriteOnly));
        source.write("drop fixture"); source.close();
        PCManFM::MainWindow window(Fm::FilePath::fromLocalPath(fixture.path().toUtf8().constData()));
        QTRY_COMPARE(window.currentPage()->folderView()->model()->rowCount(), 1);
        class Probe : public PCManFM::View {
        public:
            using PCManFM::View::eventFilter;
        } probe;
        probe.setModel(window.currentPage()->folderView()->model());
        probe.resize(800,500);
        probe.show();
        QVERIFY(QTest::qWaitForWindowExposed(&probe));
        QMimeData mime;
        const auto localUrl = QUrl::fromLocalFile(source.fileName());
        auto consumed = [&](Qt::MouseButtons buttons, Qt::KeyboardModifiers mods,
                            Qt::DropActions actions, const QList<QUrl>& urls) {
            mime.setUrls(urls);
            QDragMoveEvent event(QPoint(500,400), actions, &mime, buttons, mods);
            event.ignore();
            return probe.eventFilter(probe.childView()->viewport(), &event);
        };
        QVERIFY(consumed(Qt::LeftButton, Qt::NoModifier, Qt::CopyAction | Qt::MoveAction, {localUrl}));
        QVERIFY(!consumed(Qt::RightButton, Qt::NoModifier, Qt::CopyAction | Qt::MoveAction, {localUrl}));
        QVERIFY(!consumed(Qt::NoButton, Qt::NoModifier, Qt::CopyAction | Qt::MoveAction, {localUrl}));
        for(auto mods : {Qt::ControlModifier, Qt::ShiftModifier, Qt::AltModifier})
            QVERIFY(!consumed(Qt::LeftButton, mods, Qt::CopyAction | Qt::MoveAction, {localUrl}));
        QVERIFY(!consumed(Qt::LeftButton, Qt::NoModifier, Qt::CopyAction, {localUrl}));
        QVERIFY(!consumed(Qt::LeftButton, Qt::NoModifier, Qt::CopyAction | Qt::MoveAction, {}));
        QVERIFY(!consumed(Qt::LeftButton, Qt::NoModifier, Qt::CopyAction | Qt::MoveAction,
                          {QUrl::fromLocalFile(fixture.filePath(QStringLiteral("missing")))}));
        QVERIFY(!consumed(Qt::LeftButton, Qt::NoModifier, Qt::CopyAction | Qt::MoveAction,
                          {QUrl(QStringLiteral("sftp://invalid.example/owned-test"))}));
        // Read-only own-process procfs file: no foreign fixture or operation.
        const QString foreign = QStringLiteral("/proc/self/comm");
        QFile ownComm(foreign);
        QVERIFY(ownComm.open(QIODevice::ReadOnly));
        QVERIFY(!ownComm.readAll().isEmpty());
        struct stat a{}, b{};
        QVERIFY(::stat(source.fileName().toUtf8().constData(), &a) == 0);
        QVERIFY(::stat(foreign.toUtf8().constData(), &b) == 0);
        QVERIFY2(a.st_dev != b.st_dev, "Cross-filesystem regression needs a distinct procfs device");
        QVERIFY(!consumed(Qt::LeftButton, Qt::NoModifier, Qt::CopyAction | Qt::MoveAction,
                          {QUrl::fromLocalFile(foreign)}));

        auto leftMove = [&] {
            return consumed(Qt::LeftButton, Qt::NoModifier, Qt::CopyAction | Qt::MoveAction, {localUrl});
        };
        auto drop = [&](const QList<QUrl>& urls, Qt::KeyboardModifiers mods = Qt::NoModifier) {
            mime.setUrls(urls);
            QDropEvent event(QPointF(500,400), Qt::CopyAction | Qt::MoveAction, &mime, Qt::NoButton, mods);
            event.ignore();
            const bool claimed = probe.eventFilter(probe.childView()->viewport(), &event);
            if(claimed && (!event.isAccepted() || event.dropAction() != Qt::MoveAction))
                QTest::qFail("Claimed drop must explicitly accept Move", __FILE__, __LINE__);
            return claimed;
        };
        QVERIFY(!drop({localUrl}));
        QVERIFY(leftMove());
        QVERIFY(!drop({QUrl::fromLocalFile(fixture.path())})); // Different URLs consume record.
        QVERIFY(!drop({localUrl}));
        for(auto type : {QEvent::DragLeave, QEvent::DragEnter}) {
            QVERIFY(leftMove());
            QEvent boundary(type);
            probe.eventFilter(probe.childView()->viewport(), &boundary);
            QVERIFY(!drop({localUrl}));
        }
        for(auto mods : {Qt::ControlModifier, Qt::ShiftModifier, Qt::AltModifier}) {
            QVERIFY(leftMove());
            QVERIFY(!consumed(Qt::LeftButton, mods, Qt::CopyAction | Qt::MoveAction, {localUrl}));
            QVERIFY(!drop({localUrl}));
            QVERIFY(leftMove());
            QVERIFY(!drop({localUrl}, mods));
            QVERIFY(!drop({localUrl}));
        }
        QVERIFY(leftMove());
        QVERIFY(!consumed(Qt::RightButton, Qt::NoModifier, Qt::CopyAction | Qt::MoveAction, {localUrl}));
        QVERIFY(!drop({localUrl}));
        QVERIFY(leftMove());
        QVERIFY(drop({localUrl}));
        QVERIFY(!drop({localUrl})); // Single use after an eligible NoButton drop.
        // No event dispatch after accepting: probe destruction cancels its queued
        // FileOperation callback. These are state-policy tests, never transfers.
    }
    void qtRoutingGuardDoesNotConsumeQtTestArguments() {
        const bool wasSet = qEnvironmentVariableIsSet("QT_QPA_PLATFORM");
        const auto saved = qgetenv("QT_QPA_PLATFORM");
        const auto restore = qScopeGuard([&] {
            if(wasSet) qputenv("QT_QPA_PLATFORM", saved);
            else qunsetenv("QT_QPA_PLATFORM");
        });
        qputenv("QT_QPA_PLATFORM", "xcb");
        auto allowed = [](QList<QByteArray> args) {
            QList<char*> argv;
            for(auto& arg : args) argv.append(arg.data());
            return SelectionOwnership::qtRoutingAllowed(argv.size(), argv.data());
        };
        // Pure routing policy: these strings never reach XOpenDisplay or Qt.
        for(const auto& option : {QByteArray("-display"), QByteArray("--display"),
                                 QByteArray("-platform"), QByteArray("--platform"),
                                 QByteArray("-platformpluginpath"), QByteArray("--platformpluginpath")}) {
            QVERIFY(!allowed({"test", option, "unowned-route"}));
            QVERIFY(!allowed({"test", option + "=unowned-route"}));
            QVERIFY(!allowed({"test", option}));
        }
        QVERIFY(allowed({"test", "-o", "report.xml,junitxml", "-v1", "case:row"}));
        QVERIFY(allowed({"test", "-functions"}));
        QVERIFY(allowed({"test", "-datatags"}));
        for(const auto& platform : {QByteArray("offscreen"), QByteArray("xcb;offscreen"), QByteArray("xcb:display=unowned-route"), QByteArray()}) {
            qputenv("QT_QPA_PLATFORM", platform);
            QVERIFY(!allowed({"test"}));
        }
        qunsetenv("QT_QPA_PLATFORM");
        QVERIFY(!allowed({"test"}));
    }
    void nativeQtRoutingRejectsOverrides() {
        if(!qEnvironmentVariableIsSet("EXPLORER_ENABLE_XTEST")) QSKIP("Owned Xephyr required");
        QVERIFY(SelectionOwnership::verify());
        const QString target = qEnvironmentVariable("EXPLORER_XTEST_DISPLAY");
        const QList<QStringList> routes = {
            {QStringLiteral("-platform"), QStringLiteral("offscreen")},
            {QStringLiteral("--platform"), QStringLiteral("offscreen")},
            {QStringLiteral("-display"), target},
            {QStringLiteral("--display"), target},
            {QStringLiteral("-platformpluginpath"), QStringLiteral("/nonexistent")}
        };
        for(const auto& route : routes) {
            QProcess child;
            auto arguments = route;
            arguments << QStringLiteral("defaultSettingsMatchExplorerDensity");
            child.start(QCoreApplication::applicationFilePath(), arguments);
            QVERIFY(child.waitForFinished(5000));
            QCOMPARE(child.exitCode(), 2);
            QVERIFY(child.readAllStandardError().contains("XTEST Qt routing rejected"));
            QVERIFY(!child.readAllStandardOutput().contains("Start testing"));
        }
        for(const auto& platform : {QStringLiteral("offscreen"), QStringLiteral("xcb;offscreen"), QString()}) {
            QProcess child;
            auto environment = QProcessEnvironment::systemEnvironment();
            environment.insert(QStringLiteral("QT_QPA_PLATFORM"), platform);
            child.setProcessEnvironment(environment);
            child.start(QCoreApplication::applicationFilePath(), {QStringLiteral("defaultSettingsMatchExplorerDensity")});
            QVERIFY(child.waitForFinished(5000));
            QCOMPARE(child.exitCode(), 2);
            QVERIFY(child.readAllStandardError().contains("XTEST Qt routing rejected"));
        }
        QTemporaryDir reports;
        QVERIFY(reports.isValid());
        QProcess child;
        const auto report = reports.filePath(QStringLiteral("result.xml"));
        child.start(QCoreApplication::applicationFilePath(), {
            QStringLiteral("defaultSettingsMatchExplorerDensity"), QStringLiteral("-o"), report + QStringLiteral(",junitxml")});
        QVERIFY(child.waitForFinished(5000));
        QCOMPARE(child.exitCode(), 0);
        QVERIFY(QFile::exists(report));
    }
    void nativeOwnershipRejectsMismatches() {
        if(!qEnvironmentVariableIsSet("EXPLORER_ENABLE_XTEST")) QSKIP("Owned Xephyr required");
        QVERIFY(SelectionOwnership::verify());
        for(const auto* name : {"EXPLORER_XSERVER_START", "EXPLORER_XOWNER_START",
                               "EXPLORER_XSERVER_PID", "EXPLORER_XOWNER_PID"}) {
            const QByteArray saved = qgetenv(name);
            const auto restore = qScopeGuard([&] { qputenv(name, saved); });
            qputenv(name, "1");
            QVERIFY(!SelectionOwnership::verify());
        }
        QVERIFY(SelectionOwnership::verify());
        QTemporaryDir rejected;
        QVERIFY(rejected.isValid());
        // Assert distinctness before even starting the runner: a matching target
        // would start another WM instead of exercising its rejection path.
        const QString target = qEnvironmentVariable("EXPLORER_XTEST_DISPLAY");
        const QString mismatchedTarget = target == QStringLiteral(":2") ? QStringLiteral(":3") : QStringLiteral(":2");
        QVERIFY(mismatchedTarget != qEnvironmentVariable("EXPLORER_XTEST_DISPLAY"));
        QProcess mismatched;
        mismatched.start(QStringLiteral("/usr/bin/python3"), {
            QDir(QFileInfo(QString::fromUtf8(__FILE__)).absolutePath()).filePath(QStringLiteral("run_selection_x11.py")),
            QStringLiteral("--session"), QStringLiteral("--display"), mismatchedTarget,
            QStringLiteral("--binary"), QCoreApplication::applicationFilePath(),
            QStringLiteral("--artifacts"), rejected.path()});
        QVERIFY(mismatched.waitForFinished(5000));
        QCOMPARE(mismatched.exitCode(), 2);
        QVERIFY(mismatched.readAllStandardError().contains("does not match validated target"));
        QVERIFY(!QFile::exists(rejected.filePath(QStringLiteral("wm.log"))));
    }
    void nativeInputFailureCleanup() {
        if(!qEnvironmentVariableIsSet("EXPLORER_ENABLE_XTEST")) QSKIP("Owned Xephyr required");
        QVERIFY(SelectionOwnership::verify());
        QLibrary xtst(QStringLiteral("libXtst.so.6"));
        QVERIFY(xtst.load());
        using Button = int(*)(Display*, unsigned int, int, unsigned long);
        auto button = reinterpret_cast<Button>(xtst.resolve("XTestFakeButtonEvent"));
        auto key = reinterpret_cast<Button>(xtst.resolve("XTestFakeKeyEvent"));
        QVERIFY(button && key);
        Display* d = XOpenDisplay(nullptr);
        QVERIFY(d);
        const auto close = qScopeGuard([&] { XCloseDisplay(d); });
        QVERIFY(SelectionOwnership::verify(d));
        const auto ctrl = XKeysymToKeycode(d, XK_Control_L), shift = XKeysymToKeycode(d, XK_Shift_L);
        auto release = [&] {
            button(d, 1, False, 0);
            key(d, ctrl, False, 0);
            key(d, shift, False, 0);
            XSync(d, False);
        };
        const auto emergencyRelease = qScopeGuard(release);
        auto state = [&] {
            ::Window root, child;
            int rx, ry, wx, wy;
            unsigned int mask = 0;
            XQueryPointer(d, DefaultRootWindow(d), &root, &child, &rx, &ry, &wx, &wy, &mask);
            return mask & (Button1Mask | ControlMask | ShiftMask);
        };
        QCOMPARE(state(), 0u);
        for(bool throws : {false, true}) {
            int attempts = 0;
            bool completed = false, observedPress = false;
            try {
                completed = selectionModes([&](auto) {
                    ++attempts;
                    return checkedInputGesture([&](bool&) {
                        key(d, ctrl, True, 0);
                        key(d, shift, True, 0);
                        button(d, 1, True, 0);
                        XSync(d, False);
                        observedPress = state() == (Button1Mask | ControlMask | ShiftMask);
                        if(throws) throw std::runtime_error("injected after XTEST press");
                        return;
                    }, release);
                });
            } catch(const std::runtime_error&) {
                QVERIFY(throws);
            }
            QVERIFY(observedPress);
            QVERIFY(!completed);
            QCOMPARE(attempts, 1);
            QCOMPARE(state(), 0u); // Read back real Xserver state, not callbacks.
        }
    }
    void nativeXTestSelectionGestures() {
        if(!qEnvironmentVariableIsSet("EXPLORER_ENABLE_XTEST")) {
            QSKIP("Set EXPLORER_ENABLE_XTEST=1 on an isolated owned Xephyr display to run native XTEST gestures");
        }
        QVERIFY2(SelectionOwnership::verify(), "XTEST ownership changed before selection");
        QLibrary xtst(QStringLiteral("libXtst.so.6"));
        QVERIFY2(xtst.load(), "libXtst is required for the native XTEST selection test");
        using FakeMotion = int (*)(Display*, int, int, int, unsigned long);
        using FakeButton = int (*)(Display*, unsigned int, int, unsigned long);
        auto fakeMotion = reinterpret_cast<FakeMotion>(xtst.resolve("XTestFakeMotionEvent"));
        auto fakeButton = reinterpret_cast<FakeButton>(xtst.resolve("XTestFakeButtonEvent"));
        QVERIFY(fakeMotion && fakeButton);
        Display* display = XOpenDisplay(nullptr);
        QVERIFY(display);
        QVERIFY(SelectionOwnership::verify(display));
        struct DisplayCloser { void operator()(Display* d) const { if(d) XCloseDisplay(d); } };
        std::unique_ptr<Display, DisplayCloser> xdisplay(display);
        const auto releaseInput = [&] {
            fakeButton(display, 1, False, 0);
            XSync(display, False);
        };

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
        QJsonObject observations;
        auto selectedNames = [&] {
            QStringList selected;
            for(const auto& index : folderView->selectionModel()->selectedIndexes()) {
                if(index.column() == 0) selected << index.data().toString();
            }
            selected.sort();
            return selected;
        };
        auto injectReverseMarquee = [&](Fm::FolderView::ViewMode mode) {
            return checkedInputGesture([&](bool& complete) {
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
            const auto captureDir = qEnvironmentVariable("EXPLORER_TEST_ARTIFACTS");
            if(!captureDir.isEmpty()) {
                const auto label = mode == Fm::FolderView::DetailedListMode ? QStringLiteral("details") : QStringLiteral("grid");
                QVERIFY(window->grab().save(QDir(captureDir).filePath(QStringLiteral("selection-%1-attempt.png").arg(label))));
            }
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
            const auto heldContent = viewport->grab().toImage();
            if(!qEnvironmentVariable("EXPLORER_TEST_ARTIFACTS").isEmpty()) {
                const QString suffix = mode == Fm::FolderView::DetailedListMode ? QStringLiteral("details") : QStringLiteral("grid");
                const auto dir = qEnvironmentVariable("EXPLORER_TEST_ARTIFACTS");
                QVERIFY(QDir().mkpath(dir));
                QVERIFY(window->grab().save(QDir(dir).filePath(QStringLiteral("selection-%1-held.png").arg(suffix))));
            }
            QVERIFY(fakeButton(display, 1, False, 0));
            XSync(display, False);
            QTest::qWait(250);
            QCOMPARE(selectedNames(), sortedExpected);
            const QString suffix = mode == Fm::FolderView::DetailedListMode ? QStringLiteral("details") : QStringLiteral("grid");
            observations.insert(suffix + QStringLiteral("-held"), QJsonArray::fromStringList(selected));
            observations.insert(suffix + QStringLiteral("-released"), QJsonArray::fromStringList(selectedNames()));
            if(!qEnvironmentVariable("EXPLORER_TEST_ARTIFACTS").isEmpty()) {
                const QString suffix = mode == Fm::FolderView::DetailedListMode ? QStringLiteral("details") : QStringLiteral("grid");
                const auto dir = qEnvironmentVariable("EXPLORER_TEST_ARTIFACTS");
                QVERIFY(window->grab().save(QDir(dir).filePath(QStringLiteral("selection-%1-released.png").arg(suffix))));
            }
            const auto releasedContent = viewport->grab().toImage();
            int darkPixels = 0;
            const QRect interior(12,12,viewport->width()-24,viewport->height()-24);
            for(const auto& index : selectedIndexes) {
                const auto area = folderView->childView()->visualRect(index).intersected(interior);
                for(int y=area.top(); y<=area.bottom(); ++y) {
                    for(int x=area.left(); x<=area.right(); ++x) {
                        const auto under = releasedContent.pixelColor(x,y);
                        if(qMax(under.red(), qMax(under.green(), under.blue())) > 100) continue;
                        const auto over = heldContent.pixelColor(x,y);
                        const QColor predicted(qRound(under.red()*2.0/3),
                                               qRound(under.green()*2.0/3+34),
                                               qRound(under.blue()*2.0/3+68));
                        QVERIFY2(qAbs(over.red()-predicted.red()) <= 1
                                 && qAbs(over.green()-predicted.green()) <= 1
                                 && qAbs(over.blue()-predicted.blue()) <= 1,
                                 "Native held marquee erased/changed backing text or icon");
                        ++darkPixels;
                    }
                }
            }
            QVERIFY2(darkPixels >= 20, "Need actual dark name/icon pixels under native marquee");
            observations.insert(suffix + QStringLiteral("-dark-pixels-preserved"), darkPixels);
            complete = true;
            }, releaseInput);
        };
        QVERIFY(selectionModes(injectReverseMarquee));
        const auto artifacts = qEnvironmentVariable("EXPLORER_TEST_ARTIFACTS");
        if(!artifacts.isEmpty()) {
            QFile file(QDir(artifacts).filePath(QStringLiteral("selection-names.json")));
            QVERIFY(file.open(QIODevice::WriteOnly));
            QVERIFY(file.write(QJsonDocument(observations).toJson()) > 0);
        }
    }

    void nativeExplorerGesturesMatchWindows() {
        if(!qEnvironmentVariableIsSet("EXPLORER_ENABLE_XTEST")) QSKIP("Run with EXPLORER_ENABLE_XTEST=1 on owned isolated Xephyr");
        QVERIFY2(SelectionOwnership::verify(), "XTEST ownership changed before native gestures");
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
        QVERIFY(SelectionOwnership::verify(d));
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
                if(!window->grab().save(QDir(artifacts).filePath(stage + QStringLiteral(".png")))) return false;
            }
            return true;
        };
        auto wait=[&](int ms){ QElapsedTimer t;
            t.start();
            while(t.elapsed()<ms){QCoreApplication::processEvents(QEventLoop::AllEvents,20);
                QThread::msleep(5);
            } };
        auto click=[&](const QString& name,bool c=false,bool s=false){
            return checkedInputGesture([&](bool& complete) {
            const auto p=point(name);
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
            XSync(d, False);
            wait(250);
            complete = true;
            }, [&] { button(d,1,False,0); key(d,ctrl,False,0); key(d,shift,False,0); XSync(d,False); });
        };
        QVERIFY(click(QStringLiteral("Alpha.txt")));
        QVERIFY(record(QStringLiteral("ctrl-after-alpha")));
        QCOMPARE(names(),QStringList{QStringLiteral("Alpha.txt")});
        QVERIFY(click(QStringLiteral("Bravo.txt"),true));
        QVERIFY(record(QStringLiteral("ctrl-after-bravo")));
        QCOMPARE(names(),QStringList({QStringLiteral("Alpha.txt"),QStringLiteral("Bravo.txt")}));
        QVERIFY(click(QStringLiteral("Alpha.txt"),true));
        QVERIFY(record(QStringLiteral("ctrl-after-toggle-alpha")));
        QCOMPARE(names(),QStringList{QStringLiteral("Bravo.txt")});
        QVERIFY(click(QStringLiteral("Alpha.txt")));
        QVERIFY(click(QStringLiteral("Gamma.txt"),false,true));
        QVERIFY(record(QStringLiteral("shift-after-gamma")));
        QCOMPARE(names(),QStringList({QStringLiteral("Alpha.txt"),QStringLiteral("Bravo.txt"),QStringLiteral("Charlie.txt"),QStringLiteral("Delta.txt"),QStringLiteral("Echo.txt"),QStringLiteral("Gamma.txt")}));
        auto vp=v->childView()->viewport();
        auto p=vp->mapToGlobal(QPoint(100,vp->height()-60));
        motion(d,DefaultScreen(d),p.x(),p.y(),0);
        button(d,1,True,0);
        button(d,1,False,0);
        XFlush(d);
        wait(300);
        QVERIFY(record(QStringLiteral("empty-clears")));
        QVERIFY(names().isEmpty());
        // Queue the complete gesture BEFORE dispatching events: QDrag::exec()
        // enters a nested loop, so synchronous processEvents-driven input hangs.
        auto drag=[&](const QString& name,bool copy) {
            // Destroy pending timer callbacks before releasing input on any exit.
            QObject timers;
            return checkedInputGesture([&](bool& complete) {
            const auto a=point(name), b=point(QStringLiteral("Target"));
            QVERIFY(!a.isNull() && !b.isNull());
            if(copy) key(d,ctrl,True,0);
            motion(d,DefaultScreen(d),a.x(),a.y(),0);
            button(d,1,True,0);
            XFlush(d);
            for(int i=1;i<=12;++i) {
                QTimer::singleShot(200+i*50, &timers, [=] {
                    motion(d,DefaultScreen(d),a.x()+(b.x()-a.x())*i/12,a.y()+(b.y()-a.y())*i/12,0);
                    XFlush(d);
                });
            }
            QTimer::singleShot(1100, &timers, [=] { button(d,1,False,0); XFlush(d); });
            QTimer::singleShot(1250, &timers, [=] { key(d,ctrl,False,0); XFlush(d); });
            QTimer::singleShot(1400, &timers, [=] {
                // A regression to the upstream action menu must fail, not hang.
                if(QApplication::activePopupWidget()) {
                    qWarning("Unexpected popup after drop; cancelling for bounded failure");
                    const auto escape = XKeysymToKeycode(d, XK_Escape);
                    key(d,escape,True,0); key(d,escape,False,0); XFlush(d);
                }
            });
            wait(1600);
            complete = true;
            }, [&] { button(d,1,False,0); key(d,ctrl,False,0); key(d,shift,False,0); XSync(d,False); });
        };
        const auto alpha=digest(content.value(QStringLiteral("Alpha.txt"))), bravo=digest(content.value(QStringLiteral("Bravo.txt")));
        QVERIFY(drag(QStringLiteral("Alpha.txt"),false));
        QTRY_VERIFY_WITH_TIMEOUT(!QFile::exists(root.filePath(QStringLiteral("Alpha.txt"))),5000);
        QCOMPARE(fileHash(root.filePath(QStringLiteral("Target/Alpha.txt"))),alpha);
        QVERIFY(record(QStringLiteral("move-complete")));
        QVERIFY(drag(QStringLiteral("Bravo.txt"),true));
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(root.filePath(QStringLiteral("Target/Bravo.txt"))),5000);
        QVERIFY(QFile::exists(root.filePath(QStringLiteral("Bravo.txt"))));
        QCOMPARE(fileHash(root.filePath(QStringLiteral("Bravo.txt"))),bravo);
        QCOMPARE(fileHash(root.filePath(QStringLiteral("Target/Bravo.txt"))),bravo);
        QVERIFY(record(QStringLiteral("copy-complete")));
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
    const bool native = qEnvironmentVariableIsSet("EXPLORER_ENABLE_XTEST");
    if(native && !SelectionOwnership::qtRoutingAllowed(argc, argv)) {
        fprintf(stderr, "XTEST Qt routing rejected: use runner xcb environment without routing overrides\n");
        return 2;
    }
    if(native && !SelectionOwnership::verify()) {
        fprintf(stderr, "XTEST ownership rejected: use run_selection_x11.py with a task-owned Xephyr\n");
        return 2;
    }
    QTemporaryDir config;
    if(!config.isValid()) return 2;
    qputenv("XDG_CONFIG_HOME", config.path().toUtf8());
    qputenv("XDG_CACHE_HOME", (config.path() + QStringLiteral("/cache")).toUtf8());
    PCManFM::Application app(argc, argv);
    if(native) {
        auto* x11 = app.nativeInterface<QNativeInterface::QX11Application>();
        Display* qtDisplay = x11 ? x11->display() : nullptr;
        if(!qtDisplay || !SelectionOwnership::verify(qtDisplay)) {
            fprintf(stderr, "XTEST Qt connection ownership rejected\n");
            return 2;
        }
    }
    app.init();
    app.settings().load(QStringLiteral("ui-test"));
    const auto folderConfig = (config.path() + QStringLiteral("/folders.conf")).toUtf8();
    Fm::FolderConfig::init(folderConfig.constData());
    app.setQuitOnLastWindowClosed(false);
    ExplorerTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "tst_explorer.moc"
