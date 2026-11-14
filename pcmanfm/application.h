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


#ifndef PCMANFM_APPLICATION_H
#define PCMANFM_APPLICATION_H

#include <QApplication>
#include "settings.h"
#include <libfm-qt6/libfmqt.h>
#include <libfm-qt6/editbookmarksdialog.h>
#include <QVector>
#include <QPointer>
#include <QProxyStyle>
#include <QTimer>
#include <QTranslator>
#include <gio/gio.h>

#include <libfm-qt6/core/filepath.h>
#include <libfm-qt6/core/fileinfo.h>

class QScreen;
class QMessageBox;
class QWidget;

class QFileSystemWatcher;

namespace PCManFM {

class MainWindow;
class DesktopWindow;
class PreferencesDialog;
class DesktopPreferencesDialog;

class ProxyStyle: public QProxyStyle {
    Q_OBJECT
public:
    ProxyStyle() : QProxyStyle() {}
    virtual ~ProxyStyle() {}
    virtual int styleHint(StyleHint hint, const QStyleOption* option = nullptr, const QWidget* widget = nullptr, QStyleHintReturn* returnData = nullptr) const;
};

class Application : public QApplication {
    Q_OBJECT
    Q_PROPERTY(bool desktopManagerEnabled READ desktopManagerEnabled)

public:
    Application(int& argc, char** argv);
    virtual ~Application();

    void init();
    int exec();

    Settings& settings() {
        return settings_;
    }

    Fm::LibFmQt& libFm() {
        return libFm_;
    }

    bool openingLastTabs() const {
      return openingLastTabs_;
    }

    bool underWayland() const {
      return underWayland_;
    }

    // public interface exported via dbus. It only queues the work: the D-Bus caller must
    // never wait for a nested event loop (error dialogs, FileInfoJob) of this instance.
    void launchFiles(const QString& cwd, const QStringList& paths, bool inNewWindow, bool reopenLastTabs);

    // Expands a leading "~", "~/..." or "$VAR"/"${VAR}" (variable must be set) in a path
    // typed by the user or given on the command line. Anything else is returned unchanged.
    // (an unset or empty variable is left untouched).
    static QString expandUserPath(const QString& path);
    // True for a local path argument; false for a URI ("sftp://...", "trash:///"). A path
    // starting with '/' is always local, even if it contains ":/".
    static bool isLocalPathArgument(const QString& path);
    // Command-line validation (first or second instance): returns the arguments that may be
    // forwarded (still UNEXPANDED, the launcher expands them exactly once). Missing local
    // paths are reported on stderr and flagged through anyMissing.
    // Review 4: --find-files / D-Bus findFiles search roots; drops 'menu:' ones (any depth) when the
    // Applications view is unusable and reports it through *refused.
    static QStringList filterFindFilesPaths(const QStringList& raw, bool* refused = nullptr);
    static QStringList validateCliPaths(const QStringList& raw, const QString& cwd, bool* anyMissing = nullptr);
    // One classification for CLI validation and launching: '/' leading = local absolute;
    // "scheme:/..." = URI; anything else is relative to cwd (so "foo:bar", "dir/a:/b" are local).
    static Fm::FilePath filePathFromArgument(const QString& expanded, const QString& cwd);
    // Error box that never interprets the (untrusted) text as rich text. Caller exec()s/shows it.
    static QMessageBox* makeErrorBox(QWidget* parent, const QString& title, const QString& text);
    // Address-bar text -> path: expansion + blank input rejected (empty result); surrounding
    // whitespace is kept when the path as typed exists.
    static QString normalizeTypedPath(const QString& typed);
    // Translation directories, relocatable ones (<bin>/../share/...) before PCMANFM_DATA_DIR.
    static QStringList translationDirs();

    // Persist the settings soon (debounced) so a crash does not lose the window state.
    void scheduleSettingsSave();
    void setWallpaper(const QString& path, const QString& modeString);
    void preferences(const QString& page);
    void desktopPrefrences(const QString& page);
    void editBookmarks();
    void desktopManager(bool enabled);
    void findFiles(QStringList paths = QStringList());
    void ShowFolders(const QStringList& uriList, const QString& startupId);
    void ShowItems(const QStringList& uriList, const QString& startupId);
    void ShowItemProperties(const QStringList& uriList, const QString& startupId);
    void connectToServer();

    bool desktopManagerEnabled() {
        return enableDesktopManager_;
    }

    void updateFromSettings();
    void updateDesktopsFromSettings(bool changeSlide = true, bool allowShortcutRemoval = false);

    void openFolderInTerminal(Fm::FilePath path);
    void openFolders(Fm::FileInfoList files);

    bool isPrimary() const {
        return isPrimaryInstance;
    }

    QString profileName() {
        return profileName_;
    }

    void cleanPerFolderConfig();

protected Q_SLOTS:
    void onAboutToQuit();
    void onSigtermNotified();
    void saveSettingsNow();

    void onLastWindowClosed();
    void onSaveStateRequest(QSessionManager& manager);
    void initVolumeManager();

    void onVirtualGeometryChanged(const QRect& rect);
    void onAvailableGeometryChanged(const QRect& rect);
    void onScreenDestroyed(QObject* screenObj);
    void onScreenAdded(QScreen* newScreen);
    void onScreenRemoved(QScreen* oldScreen);
    void reloadDesktopsAsNeeded();

    void onFindFileAccepted();
    void onConnectToServerAccepted();

protected:
    //virtual bool eventFilter(QObject* watched, QEvent* event);
    bool parseCommandLineArgs();
    DesktopWindow* createDesktopWindow(int screenNum, const QString& screenName = QString());
    bool autoMountVolume(GVolume* volume, bool interactive = true);

    static void onVolumeAdded(GVolumeMonitor* monitor, GVolume* volume, Application* pThis);

private Q_SLOTS:
    void onUserDirsChanged();
    void onPropJobFinished();

private:
    void initWatch();
    void installSigtermHandler();
    void doLaunchFiles(const QString& cwd, const QStringList& paths, bool inNewWindow, bool reopenLastTabs);

    QTimer* saveTimer_ = nullptr;
    int exitCode_ = 0;

    bool isPrimaryInstance;
    Fm::LibFmQt libFm_;
    Settings settings_;
    QString profileName_;
    bool daemonMode_;
    bool enableDesktopManager_;
    QVector<DesktopWindow*> desktopWindows_;
    QPointer<PreferencesDialog> preferencesDialog_;
    QPointer<DesktopPreferencesDialog> desktopPreferencesDialog_;
    QPointer<Fm::EditBookmarksDialog> editBookmarksialog_;
    QTranslator translator;
    QTranslator qtTranslator;
    GVolumeMonitor* volumeMonitor_;

    QFileSystemWatcher* userDirsWatcher_;
    QString userDirsFile_;
    QString userDesktopFolder_;
    bool lxqtRunning_;
    bool openingLastTabs_;

    bool underWayland_;

    int argc_;
    char** argv_;
};

}

#endif // PCMANFM_APPLICATION_H
