/*
 * <one line to give the library's name and an idea of what it does.>
 * Copyright (C) 2014  <copyright holder> <email>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#include "launcher.h"
#include <QFileInfo>
#include <QFile>
#include <QFileDialog>
#include <QMessageBox>
#include <QComboBox>
#include <QSignalBlocker>
#include <QEvent>
#include <memory>
#include <libfm-qt6/appchoosercombobox.h>
#include <QStringList>
#include <dlfcn.h>
#include "mainwindow.h"
#include "application.h"
#include <libfm-qt6/core/filepath.h>

namespace PCManFM {

Launcher::Launcher(PCManFM::MainWindow* mainWindow):
    Fm::FileLauncher(),
    mainWindow_(mainWindow),
    openInNewTab_(false),
    openWithDefaultFileManager_(false) {

    Application* app = static_cast<Application*>(qApp);
    setQuickExec(app->settings().quickExec());
}

Launcher::~Launcher() = default;

namespace {
int appChooserOverride = -1;

// Path of the helper the *loaded* libmenu-cache will exec: it is a string compiled into the
// library ("/usr/lib/menu-cache/menu-cached" for the sysroot build), so read it from the binary.
QString readMenuCachedPathOfLoadedLibrary() {
    void* sym = dlsym(RTLD_DEFAULT, "menu_cache_lookup");
    Dl_info info;
    if(!sym || !dladdr(sym, &info) || !info.dli_fname) {
        return QString();
    }
    QFile lib(QString::fromLocal8Bit(info.dli_fname));
    if(!lib.open(QIODevice::ReadOnly)) {
        return QString();
    }
    const QByteArray data = lib.readAll();
    const QByteArray tail("/menu-cached");
    int idx = 0;
    while((idx = data.indexOf(tail, idx)) >= 0) {
        const int start = data.lastIndexOf('\0', idx) + 1;
        const int end = data.indexOf('\0', idx);
        const QByteArray candidate = data.mid(start, end < 0 ? -1 : end - start);
        if(candidate.startsWith('/') && candidate.endsWith(tail)) {
            return QString::fromLocal8Bit(candidate);
        }
        idx += tail.size();
    }
    return QString();
}

// The lookup reads the whole library: do it once (the loaded library cannot change).
QString menuCachedPathOfLoadedLibrary() {
    static const QString cached = readMenuCachedPathOfLoadedLibrary();
    return cached;
}

// D-02 (all entry points): Fm::AppChooserComboBox (Properties -> "Open with" -> "Customize")
// builds an AppChooserDialog -> AppMenuView -> menu_cache_lookup(), which g_error()s (SIGTRAP)
// when the helper is missing. libfm-qt is not patched here: when the chooser is unavailable the
// combo's own handler is detached and replaced by one that never builds the dialog.
class AppChooserGuard : public QObject {
public:
    using QObject::QObject;
    bool eventFilter(QObject* watched, QEvent* event) override {
        if(event->type() == QEvent::Polish) {
            if(auto* combo = qobject_cast<Fm::AppChooserComboBox*>(watched)) {
                guard(combo);
            }
        }
        return false;
    }
    static void guard(Fm::AppChooserComboBox* combo) {
        if(Launcher::appChooserAvailable() || combo->property("explorerChooserGuarded").toBool()) {
            return;
        }
        combo->setProperty("explorerChooserGuarded", true);
        QObject::disconnect(combo, &QComboBox::currentIndexChanged, combo, nullptr);
        auto previous = std::make_shared<int>(combo->currentIndex());
        QObject::connect(combo, &QComboBox::currentIndexChanged, combo, [combo, previous](int index) {
            if(index < 0) {
                return;
            }
            if(index == combo->count() - 1) { // "Customize"
                {
                    QSignalBlocker blocker(combo);
                    combo->setCurrentIndex(*previous);
                }
                Launcher::showChooserUnavailableMessage(combo->window());
            }
            else {
                *previous = index;
            }
        });
    }
};
}

void Launcher::installAppChooserGuard(QObject* app) {
    app->installEventFilter(new AppChooserGuard(app));
}

QString Launcher::menuCachedHelperPath() {
    return menuCachedPathOfLoadedLibrary();
}

void Launcher::setAppChooserAvailableForTest(int state) {
    appChooserOverride = state;
}

bool Launcher::appChooserAvailable() {
    if(appChooserOverride >= 0) {
        return appChooserOverride == 1;
    }
    if(!dlsym(RTLD_DEFAULT, "menu_cache_lookup")) {
        return true; // libmenu-cache not in use: nothing can abort
    }
    const QString helper = menuCachedPathOfLoadedLibrary();
    if(!helper.isEmpty()) {
        return QFileInfo(helper).isExecutable();
    }
    static const QStringList candidates = {
        QStringLiteral("/usr/lib/menu-cache/menu-cached"),
        QStringLiteral("/usr/lib/x86_64-linux-gnu/menu-cache/menu-cached"),
        QStringLiteral("/usr/lib/aarch64-linux-gnu/menu-cache/menu-cached"),
        QStringLiteral("/usr/lib64/menu-cache/menu-cached"),
        QStringLiteral("/usr/libexec/menu-cache/menu-cached")};
    for(const QString& c : candidates) {
        if(QFileInfo(c).isExecutable()) {
            return true;
        }
    }
    return false;
}

void Launcher::showNoAppMessage(QWidget* parent) {
    auto* box = new QMessageBox(QMessageBox::Information, QObject::tr("No application associated"),
                                QObject::tr("No application is associated with this file type."),
                                QMessageBox::Ok, parent);
    box->setTextFormat(Qt::PlainText);
    box->setInformativeText(QObject::tr("Use \"Open with\" to choose an application."));
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->setObjectName(QStringLiteral("explorerNoAppMessage"));
    box->setWindowModality(Qt::NonModal);
    box->show(); // non-blocking on purpose
}

void Launcher::showChooserUnavailableMessage(QWidget* parent) {
    auto* box = new QMessageBox(QMessageBox::Information, QObject::tr("Application chooser unavailable"),
                                QObject::tr("The application chooser is not available in this environment "
                                            "(the libmenu-cache helper \"menu-cached\" was not found)."),
                                QMessageBox::Ok, parent);
    box->setTextFormat(Qt::PlainText);
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->setObjectName(QStringLiteral("explorerChooserUnavailableMessage"));
    box->setWindowModality(Qt::NonModal);
    box->show();
}

Fm::GAppInfoPtr Launcher::pickApplicationWithoutMenuCache(QWidget* parent) {
    const QString exe = QFileDialog::getOpenFileName(parent, QObject::tr("Choose an application"),
                                                     QStringLiteral("/usr/bin"));
    if(exe.isEmpty()) {
        return Fm::GAppInfoPtr{};
    }
    gchar* quoted = g_shell_quote(exe.toLocal8Bit().constData());
    const QByteArray cmd = QByteArray(quoted) + " %F";
    g_free(quoted);
    Fm::GErrorPtr err;
    GAppInfo* app = g_app_info_create_from_commandline(cmd.constData(), nullptr, G_APP_INFO_CREATE_NONE, &err);
    return Fm::GAppInfoPtr{app, false};
}

Fm::GAppInfoPtr Launcher::chooseApp(const Fm::FileInfoList& fileInfos, const char* mimeType, Fm::GErrorPtr& err) {
    if(!appChooserAvailable()) {
        showNoAppMessage();
        return Fm::GAppInfoPtr{};
    }
    return Fm::FileLauncher::chooseApp(fileInfos, mimeType, err);
}

bool Launcher::openFolder(GAppLaunchContext* ctx, const Fm::FileInfoList& folderInfos, Fm::GErrorPtr& /*err*/) {
    auto fi = folderInfos[0];
    Application* app = static_cast<Application*>(qApp);
    MainWindow* mainWindow = mainWindow_;
    Fm::FilePath path = fi->path();
    if(!mainWindow) {
        // Launch folders with the default file manager if:
        //   1. There is no main window (i.e., folders are on desktop),
        //   2. The folders are not supposed to be opened in new tabs, and
        //   3. The default file manager exists and is not PCManFM-Qt.
        if(openWithDefaultFileManager_ && !openInNewTab_) {
            auto defaultApp = Fm::GAppInfoPtr{g_app_info_get_default_for_type("inode/directory", FALSE), false};
            if(defaultApp != nullptr
               && strcmp(g_app_info_get_id(defaultApp.get()), "pcmanfm-qt.desktop") != 0) {
                for(const auto & folder : folderInfos) {
                    Fm::FileLauncher::launchWithDefaultApp(folder, ctx);
                }
                return true;
            }
        }
        mainWindow = new MainWindow(std::move(path));
        mainWindow->resize(app->settings().windowWidth(), app->settings().windowHeight());

        if(app->settings().windowMaximized()) {
            mainWindow->setWindowState(mainWindow->windowState() | Qt::WindowMaximized);
        }
    }
    else {
        if(openInNewTab_) {
            mainWindow->addTab(std::move(path));
        }
        else {
            mainWindow->chdir(std::move(path));
        }
    }

    for(size_t i = 1; i < folderInfos.size(); ++i) {
        fi = folderInfos[i];
        path = fi->path();
        mainWindow->addTab(std::move(path));
    }
    mainWindow->show();
    if(!app->underWayland()) {
        mainWindow->raise();
        mainWindow->activateWindow();
    }
    openInNewTab_ = false;
    return true;
}

void Launcher::launchedFiles(const Fm::FileInfoList& files) const {
    Application* app = static_cast<Application*>(qApp);
    if(app->settings().getRecentFilesNumber() > 0) {
        for(const auto& file : files) {
            if(file->isNative() && !file->isDir()) {
                app->settings().addRecentFile(QString::fromUtf8(file->path().localPath().get()));
            }
        }
    }
}
void Launcher::launchedPaths(const Fm::FilePathList& paths) const {
    Application* app = static_cast<Application*>(qApp);
    if(app->settings().getRecentFilesNumber() > 0) {
        for(const auto& path : paths) {
            if(path.isNative()) {
                auto pathStr = QString::fromUtf8(path.localPath().get());
                if(!QFileInfo(pathStr).isDir()) { // this is fast because the path is native
                    app->settings().addRecentFile(pathStr);
                }
            }
        }
    }
}

} //namespace PCManFM
