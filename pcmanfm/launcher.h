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

#ifndef PCMANFM_LAUNCHER_H
#define PCMANFM_LAUNCHER_H

#include <libfm-qt6/filelauncher.h>

namespace Fm {
class FileMenu;
}

namespace PCManFM {

class MainWindow;

class Launcher : public Fm::FileLauncher {
public:
    Launcher(MainWindow* mainWindow = nullptr);
    ~Launcher();

    bool hasMainWindow() const {
        return mainWindow_ != nullptr;
    }

    void openInNewTab() {
        openInNewTab_ = true;
    }

    bool openWithDefaultFileManager() const {
        return openWithDefaultFileManager_;
    }
    void setOpenWithDefaultFileManager(bool open) {
        openWithDefaultFileManager_ = open;
    }

    // D-02: the libfm-qt app chooser builds a libmenu-cache view; when the loaded
    // libmenu-cache cannot find its menu-cached helper it calls g_error() and the whole
    // process aborts (SIGTRAP). This tells whether building the chooser is safe.
    static bool appChooserAvailable();
    // Test hook: 1 = pretend available, 0 = pretend unavailable, -1 = autodetect.
    static void setAppChooserAvailableForTest(int state);
    // Path of the menu-cached helper compiled into the loaded libmenu-cache (cached; empty if unknown).
    static QString menuCachedHelperPath();
    // Installs the application-wide guard that keeps every Fm::AppChooserComboBox ("Customize")
    // from building the libmenu-cache chooser when it is unavailable.
    static void installAppChooserGuard(QObject* app);
    // Non-blocking, PlainText. An empty text selects the generic "chooser unavailable" wording.
    static void showChooserUnavailableMessage(QWidget* parent = nullptr, const QString& text = QString());
    // True for the libmenu-cache backed locations (menu:///applications/...): they need menu-cached.
    static bool isMenuCachePath(const Fm::FilePath& path);
    // D-02: makes the "Open with > Other Applications" entry of any Fm::FileMenu safe (side pane
    // tree, views, desktop): when the chooser is unavailable it opens an executable picker
    // instead of libfm-qt's AppChooserDialog. No-op when the chooser is available.
    static void guardFileMenu(Fm::FileMenu* menu, QWidget* parent);
    // Non-blocking "no application associated" message (never aborts, never nests an event loop).
    static void showNoAppMessage(QWidget* parent = nullptr);
    // Chooser replacement used when appChooserAvailable() is false: pick an executable.
    static Fm::GAppInfoPtr pickApplicationWithoutMenuCache(QWidget* parent);

protected:
    Fm::GAppInfoPtr chooseApp(const Fm::FileInfoList& fileInfos, const char* mimeType, Fm::GErrorPtr& err) override;
    bool openFolder(GAppLaunchContext* ctx, const Fm::FileInfoList& folderInfos, Fm::GErrorPtr& err) override;
    void launchedFiles(const Fm::FileInfoList& files) const override;
    void launchedPaths(const Fm::FilePathList& paths) const override;

private:
    MainWindow* mainWindow_;
    bool openInNewTab_;
    bool openWithDefaultFileManager_;
};

}

#endif // PCMANFM_LAUNCHER_H
