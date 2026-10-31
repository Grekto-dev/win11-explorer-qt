/*

    Copyright (C) 2013  Hong Jen Yee (PCMan) <pcman.tw@gmail.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/


#include "view.h"
#include <libfm-qt6/filemenu.h>
#include <libfm-qt6/foldermenu.h>
#include "application.h"
#include "settings.h"
#include "application.h"
#include "mainwindow.h"
#include "launcher.h"
#include <QAction>
#include <QPainter>
#include <QProxyStyle>
#include <QStyleOption>
#include <QDropEvent>
#include <QMimeData>
#include <QTimer>
#include <libfm-qt6/fileoperation.h>
#include <libfm-qt6/proxyfoldermodel.h>
#include <sys/stat.h>

namespace PCManFM {
namespace {

class ExplorerSelectionStyle final : public QProxyStyle {
public:
    explicit ExplorerSelectionStyle(const QString& baseStyleName)
        : QProxyStyle(baseStyleName) {
    }

    void drawControl(ControlElement element, const QStyleOption* option, QPainter* painter,
                     const QWidget* widget = nullptr) const override {
        if(element == CE_RubberBand && option) {
            painter->save();
            painter->fillRect(option->rect, QColor(170, 204, 238));
            painter->setPen(QColor(0, 120, 212));
            painter->setBrush(Qt::NoBrush);
            painter->drawRect(option->rect.adjusted(0, 0, -1, -1));
            painter->restore();
            return;
        }
        QProxyStyle::drawControl(element, option, painter, widget);
    }
};

} // namespace

View::View(Fm::FolderView::ViewMode _mode, QWidget* parent):
    Fm::FolderView(_mode, parent) {

    // Keep the observed selection treatment local to this folder view. Each
    // child style gets an independent base, never qApp's shared style.
    applyRubberBandStyle();

    Settings& settings = static_cast<Application*>(qApp)->settings();
    updateFromSettings(settings);
}

View::~View() = default;

void View::applyRubberBandStyle() {
    auto* child = childView();
    if(!child) {
        return;
    }
    const QString baseStyleName = qApp->style()->objectName();
    auto* localStyle = new ExplorerSelectionStyle(baseStyleName);
    localStyle->setParent(child);
    child->setStyle(localStyle);
    child->viewport()->installEventFilter(this);
}

void View::setViewMode(Fm::FolderView::ViewMode mode) {
    Fm::FolderView::setViewMode(mode);
    applyRubberBandStyle();
}

bool View::eventFilter(QObject* watched, QEvent* event) {
    // libfm-qt asks which action to use for an unmodified drop. Explorer instead
    // moves local files on the same filesystem. Keep other drops upstream,
    // including explicit Ctrl-copy and nonlocal/unknown filesystem cases.
    if(childView() && watched == childView()->viewport()
       && (event->type() == QEvent::Drop || event->type() == QEvent::DragMove)) {
        auto* drop = static_cast<QDropEvent*>(event);
        if(drop->modifiers() == Qt::NoModifier && drop->possibleActions().testFlag(Qt::MoveAction)
           && drop->mimeData()->hasUrls()) {
            auto destination = path();
            const auto index = childView()->indexAt(drop->position().toPoint());
            const auto info = model()->fileInfoFromIndex(index);
            if(index.isValid() && (!info || !info->isDir())) {
                return Fm::FolderView::eventFilter(watched, event);
            }
            if(info) destination = info->path();
            const auto destinationName = destination.localPath();
            struct stat destinationStat;
            bool sameFilesystem = destinationName && ::stat(destinationName.get(), &destinationStat) == 0
                                  && S_ISDIR(destinationStat.st_mode);
            Fm::FilePathList sources;
            for(const auto& url : drop->mimeData()->urls()) {
                const auto sourceName = url.toLocalFile().toUtf8();
                struct stat sourceStat;
                if(!url.isLocalFile() || ::lstat(sourceName.constData(), &sourceStat) != 0
                   || !sameFilesystem || sourceStat.st_dev != destinationStat.st_dev) {
                    sameFilesystem = false;
                    break;
                }
                sources.emplace_back(Fm::FilePath::fromLocalPath(sourceName.constData()));
            }
            if(sameFilesystem && !sources.empty()) {
                drop->setDropAction(Qt::MoveAction);
                drop->accept();
                if(event->type() == QEvent::Drop) {
                    QTimer::singleShot(0, this, [this, sources, destination] {
                        Q_EMIT dropIsDecided(true);
                        Fm::FileOperation::moveFiles(sources, destination);
                    });
                }
                return true;
            }
        }
    }
    return Fm::FolderView::eventFilter(watched, event);
}

void View::onFileClicked(int type, const std::shared_ptr<const Fm::FileInfo>& fileInfo) {
    if(type == MiddleClick) {
        if(fileInfo && fileInfo->isDir()) {
            // fileInfo->path() shouldn't be used directly because
            // it won't work in places like computer:/// or network:///
            Fm::FileInfoList files;
            files.emplace_back(fileInfo);
            launchFiles(std::move(files), true);
        }
    }
    else {
        if(type == ActivatedClick) {
            if(fileLauncher()) { // launch all selected files
                auto files = selectedFiles();
                if(!files.empty()) {
                    if(files.size() > 20) {
                        QMessageBox::StandardButton r = QMessageBox::question(window(),
                                                        tr("Many files"),
                                                        tr("Do you want to open these %1 files?", nullptr, files.size()).arg(files.size()),
                                                        QMessageBox::Yes | QMessageBox::No,
                                                        QMessageBox::No);
                        if(r == QMessageBox::No) {
                            return;
                        }
                    }
                    launchFiles(std::move(files));
                }
            }
        }
        else {
            Fm::FolderView::onFileClicked(type, fileInfo);
        }
    }
}

void View::onNewWindow() {
    Fm::FileMenu* menu = static_cast<Fm::FileMenu*>(sender()->parent());
    auto files = menu->files();
    if(files.size() == 1 && !files.front()->isDir()) {
        openFolderAndSelectFile(files.front());
    }
    else {
        Application* app = static_cast<Application*>(qApp);
        app->openFolders(std::move(files));
    }
}

void View::onNewTab() {
    Fm::FileMenu* menu = static_cast<Fm::FileMenu*>(sender()->parent());
    auto files = menu->files();
    if(files.size() == 1 && !files.front()->isDir()) {
        openFolderAndSelectFile(files.front(), true);
    }
    else {
        launchFiles(std::move(files), true);
    }
}

void View::onOpenInTerminal() {
    Application* app = static_cast<Application*>(qApp);
    Fm::FileMenu* menu = static_cast<Fm::FileMenu*>(sender()->parent());
    auto files = menu->files();
    for(auto& file: files) {
        app->openFolderInTerminal(file->path());
    }
}

void View::onSearch() {

}

void View::prepareFileMenu(Fm::FileMenu* menu) {
    Application* app = static_cast<Application*>(qApp);
    menu->setConfirmDelete(app->settings().confirmDelete());
    menu->setConfirmTrash(app->settings().confirmTrash());
    menu->setUseTrash(app->settings().useTrash());

    // add some more menu items for dirs
    bool all_native = true;
    bool all_directory = true;
    auto files = menu->files();
    for(auto& fi: files) {
        if(!fi->isDir()) {
            all_directory = false;
        }
        else if(fi->isDir() && !fi->isNative()) {
            all_native = false;
        }
    }

    if(all_directory) {
        QAction* action = new QAction(QIcon::fromTheme(QStringLiteral("tab-new")), tr("Open in New T&ab"), menu);
        connect(action, &QAction::triggered, this, &View::onNewTab);
        menu->insertAction(menu->separator1(), action);

        action = new QAction(QIcon::fromTheme(QStringLiteral("window-new")), tr("Open in New Win&dow"), menu);
        connect(action, &QAction::triggered, this, &View::onNewWindow);
        menu->insertAction(menu->separator1(), action);

        // TODO: add search
        // action = menu->addAction(_("Search"));

        if(all_native) {
            action = new QAction(QIcon::fromTheme(QStringLiteral("utilities-terminal")), tr("Open in Termina&l"), menu);
            connect(action, &QAction::triggered, this, &View::onOpenInTerminal);
            menu->insertAction(menu->separator1(), action);
        }
    }
    else {
        if(menu->pasteAction()) { // nullptr for trash
            menu->pasteAction()->setVisible(false);
        }
        if(menu->createAction()) {
            menu->createAction()->setVisible(false);
        }

        if(folder() && folder()->path().hasUriScheme("search")
           && files.size() == 1 && !files.front()->isDir()) {
            QAction* action = new QAction(QIcon::fromTheme(QStringLiteral("tab-new")), tr("Show in New T&ab"), menu);
            connect(action, &QAction::triggered, this, &View::onNewTab);
            menu->insertAction(menu->separator1(), action);

            action = new QAction(QIcon::fromTheme(QStringLiteral("window-new")), tr("Show in New Win&dow"), menu);
            connect(action, &QAction::triggered, this, &View::onNewWindow);
            menu->insertAction(menu->separator1(), action);
        }
    }
}

void View::prepareFolderMenu(Fm::FolderMenu* menu) {
    auto folder = folderInfo();
    if(folder && folder->isNative()) {
        QAction *action = new QAction(QIcon::fromTheme(QStringLiteral("utilities-terminal")), tr("Open in Termina&l"), menu);
        connect(action, &QAction::triggered, this, [folder] {
            Application* app = static_cast<Application*>(qApp);
            app->openFolderInTerminal(folder->path());
        });
        menu->insertAction(menu->createAction(), action);
        menu->insertSeparator(menu->createAction());
    }
}

void View::updateFromSettings(Settings& settings) {

    setIconSize(Fm::FolderView::IconMode, QSize(settings.bigIconSize(), settings.bigIconSize()));
    setIconSize(Fm::FolderView::CompactMode, QSize(settings.smallIconSize(), settings.smallIconSize()));
    setIconSize(Fm::FolderView::ThumbnailMode, QSize(settings.thumbnailIconSize(), settings.thumbnailIconSize()));
    setIconSize(Fm::FolderView::DetailedListMode, QSize(settings.smallIconSize(), settings.smallIconSize()));

    setMargins(settings.folderViewCellMargins());

    setAutoSelectionDelay(settings.singleClick() ? settings.autoSelectionDelay() : 0);

    setCtrlRightClick(settings.ctrlRightClick());

    setScrollPerPixel(settings.scrollPerPixel());

    Fm::ProxyFolderModel* proxyModel = model();
    if(proxyModel) {
        proxyModel->setShowThumbnails(settings.showThumbnails());
        proxyModel->setBackupAsHidden(settings.backupAsHidden());
    }
}

void View::launchFiles(Fm::FileInfoList files, bool inNewTabs) {
    if(fileLauncher()) {
        if(auto launcher = dynamic_cast<Launcher*>(fileLauncher())) {
            // this happens on desktop
            if(!launcher->hasMainWindow()) {
                if(!inNewTabs && launcher->openWithDefaultFileManager()) {
                    launcher->launchFiles(nullptr, std::move(files));
                    return;
                }
                if(inNewTabs || static_cast<Application*>(qApp)->settings().singleWindowMode()) {
                    MainWindow* window = MainWindow::lastActive();
                    // if there is no last active window, find the last created window
                    if(window == nullptr) {
                        QWidgetList windows = qApp->topLevelWidgets();
                        for(int i = 0; i < windows.size(); ++i) {
                            auto win = windows.at(windows.size() - 1 - i);
                            if(win->inherits("PCManFM::MainWindow")) {
                                window = static_cast<MainWindow*>(win);
                                break;
                            }
                        }
                    }
                    auto tempLauncher = Launcher(window);
                    tempLauncher.openInNewTab();
                    tempLauncher.launchFiles(nullptr, std::move(files));
                    return;
                }
            }
            if(inNewTabs) {
                launcher->openInNewTab();
            }
        }
        fileLauncher()->launchFiles(nullptr, std::move(files));
    }
}

void View::openFolderAndSelectFile(const std::shared_ptr<const Fm::FileInfo>& fileInfo, bool inNewTab) {
    if(auto win = qobject_cast<MainWindow*>(window())) {
        Fm::FilePathList paths;
        paths.emplace_back(fileInfo->path());
        win->openFolderAndSelectFiles(std::move(paths), inNewTab);
    }
}

} // namespace PCManFM
