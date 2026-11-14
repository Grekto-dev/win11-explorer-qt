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


#include "application.h"
#include "mainwindow.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDir>
#include <QFileInfo>
#include <QVector>
#include <QLocale>
#include <QLibraryInfo>
#include <QFile>
#include <QMessageBox>
#include <QCommandLineParser>
#include <QSocketNotifier>
#include <QScreen>
#include <QWindow>
#include <QFileSystemWatcher>
#include <QRegularExpression>
#include <cstdlib>

#include <algorithm>

#include <gio/gio.h>
#include <sys/socket.h>


#include <libfm-qt6/filepropsdialog.h>
#include <libfm-qt6/filesearchdialog.h>
#include <libfm-qt6/core/terminal.h>
#include <libfm-qt6/core/bookmarks.h>
#include <libfm-qt6/core/folderconfig.h>
#include <libfm-qt6/core/fileinfojob.h>

#include "applicationadaptor.h"

#include "preferencesdialog.h"

#include "launcher.h"
#include "xdgdir.h"
#include "connectserverdialog.h"

#include <X11/Xlib.h>


int qInitResources_explorer_icons();

namespace PCManFM {

static const char* serviceName = "org.grekto.Win11ExplorerQt";
static const char* ifaceName = "org.grekto.Win11ExplorerQt.Application";

int ProxyStyle::styleHint(StyleHint hint, const QStyleOption* option, const QWidget* widget, QStyleHintReturn* returnData) const {
    Application* app = static_cast<Application*>(qApp);
    if(hint == QStyle::SH_ItemView_ActivateItemOnSingleClick) {
        if (app->settings().singleClick()) {
            return true;
        }
        // follow the style
        return QCommonStyle::styleHint(hint,option,widget,returnData);
    }
    return QProxyStyle::styleHint(hint, option, widget, returnData);
}

Application::Application(int& argc, char** argv):
    QApplication(argc, argv),
    libFm_(),
    settings_(),
    profileName_(QStringLiteral("default")),
    daemonMode_(false),
    enableDesktopManager_(false),
    desktopWindows_(),
    preferencesDialog_(),
    editBookmarksialog_(),
    volumeMonitor_(nullptr),
    userDirsWatcher_(nullptr),
    lxqtRunning_(false),
    openingLastTabs_(false) {

    argc_ = argc;
    argv_ = argv;

    setApplicationVersion(QStringLiteral(PCMANFM_QT_VERSION));
    setDesktopFileName(QStringLiteral("win11-explorer-qt"));
    setApplicationName(QStringLiteral("win11-explorer-qt"));
    setApplicationDisplayName(QStringLiteral("Win11 Explorer Qt"));
    setOrganizationName(QStringLiteral("Grekto"));
    setOrganizationDomain(QStringLiteral("grekto.dev"));
    setWindowIcon(QIcon::fromTheme(QStringLiteral("win11-explorer-qt")));

    Launcher::installAppChooserGuard(this); // D-02: Properties -> Open with -> Customize
    underWayland_ = QGuiApplication::platformName() == QStringLiteral("wayland");

    // QDBusConnection::sessionBus().registerObject("/org/pcmanfm/Application", this);
    QDBusConnection dbus = QDBusConnection::sessionBus();
    if(!dbus.isConnected()) {
        // D-06: without a session bus there is no single-instance service. Do not pretend
        // to be a second instance (that exited silently with rc=0); run standalone.
        qWarning().noquote() << QStringLiteral("win11-explorer-qt: session D-Bus unavailable (%1); running without single-instance support")
                                .arg(dbus.lastError().message());
        isPrimaryInstance = true;
        setStyle(new ProxyStyle());
        connect(this, &Application::aboutToQuit, this, &Application::onAboutToQuit);
        installSigtermHandler();
    }
    else if(dbus.registerService(QLatin1String(serviceName))) {
        // we successfully registered the service
        isPrimaryInstance = true;
        setStyle(new ProxyStyle());
        //desktop()->installEventFilter(this);

        new ApplicationAdaptor(this);
        dbus.registerObject(QStringLiteral("/Application"), this);

        connect(this, &Application::aboutToQuit, this, &Application::onAboutToQuit);
        // aboutToQuit() is not signalled on SIGTERM, install signal handler
        installSigtermHandler();

        // Check if LXQt Session is running. LXQt has it's own Desktop Folder
        // editor. We just hide our editor when LXQt is running.
        QDBusInterface* lxqtSessionIface = new QDBusInterface(
            QStringLiteral("org.lxqt.session"),
            QStringLiteral("/LXQtSession"));
        if(lxqtSessionIface) {
            if(lxqtSessionIface->isValid()) {
                lxqtRunning_ = true;
                userDesktopFolder_ = XdgDir::readDesktopDir();
                initWatch();
            }
            delete lxqtSessionIface;
            lxqtSessionIface = nullptr;
        }



    }
    else {
        // an service of the same name is already registered.
        // we're not the first instance
        isPrimaryInstance = false;
    }
}

Application::~Application() {
    //desktop()->removeEventFilter(this);

    if(volumeMonitor_) {
        g_signal_handlers_disconnect_by_func(volumeMonitor_, gpointer(onVolumeAdded), this);
        g_object_unref(volumeMonitor_);
    }

    // if(enableDesktopManager_)
    //   removeNativeEventFilter(this);
}

void Application::initWatch() {
    QFile file_(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + QStringLiteral("/user-dirs.dirs"));
    if(! file_.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qDebug() << Q_FUNC_INFO << "Could not read: " << userDirsFile_;
        userDirsFile_ = QString();
    }
    else {
        userDirsFile_ = file_.fileName();
    }

    userDirsWatcher_ = new QFileSystemWatcher(this);
    userDirsWatcher_->addPath(userDirsFile_);
    connect(userDirsWatcher_, &QFileSystemWatcher::fileChanged, this, &Application::onUserDirsChanged);
}

bool Application::parseCommandLineArgs() {
    bool keepRunning = false;
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption profileOption(QStringList() << QStringLiteral("p") << QStringLiteral("profile"), tr("Name of configuration profile"), tr("PROFILE"));
    parser.addOption(profileOption);

    QCommandLineOption daemonOption(QStringList() << QStringLiteral("d") << QStringLiteral("daemon-mode"), tr("Run PCManFM-Qt as a daemon"));
    parser.addOption(daemonOption);

    QCommandLineOption quitOption(QStringList() << QStringLiteral("q") << QStringLiteral("quit"), tr("Quit PCManFM-Qt"));
    parser.addOption(quitOption);

    QCommandLineOption desktopOption(QStringLiteral("desktop"), tr("Launch desktop manager"));
    parser.addOption(desktopOption);

    QCommandLineOption desktopOffOption(QStringLiteral("desktop-off"), tr("Turn off desktop manager if it's running"));
    parser.addOption(desktopOffOption);

    QCommandLineOption desktopPrefOption(QStringLiteral("desktop-pref"), tr("Open desktop preference dialog on the page with the specified name") + QStringLiteral("\n") + tr("NAME") + QStringLiteral("=(general|bg|slide|advanced)"), tr("NAME"));
    parser.addOption(desktopPrefOption);

    QCommandLineOption newWindowOption(QStringList() << QStringLiteral("n") << QStringLiteral("new-window"), tr("Open new window"));
    parser.addOption(newWindowOption);

    QCommandLineOption findFilesOption(QStringList() << QStringLiteral("f") << QStringLiteral("find-files"), tr("Open Find Files utility"));
    parser.addOption(findFilesOption);

    QCommandLineOption setWallpaperOption(QStringList() << QStringLiteral("w") << QStringLiteral("set-wallpaper"), tr("Set desktop wallpaper from image FILE"), tr("FILE"));
    parser.addOption(setWallpaperOption);

    QCommandLineOption wallpaperModeOption(QStringLiteral("wallpaper-mode"), tr("Set mode of desktop wallpaper. MODE=(%1)").arg(QStringLiteral("color|stretch|fit|center|tile|zoom")), tr("MODE"));
    parser.addOption(wallpaperModeOption);

    QCommandLineOption showPrefOption(QStringLiteral("show-pref"), tr("Open Preferences dialog on the page with the specified name") + QStringLiteral("\n") + tr("NAME") + QStringLiteral("=(behavior|display|ui|thumbnail|volume|advanced)"), tr("NAME"));
    parser.addOption(showPrefOption);

    parser.addPositionalArgument(QStringLiteral("files"), tr("Files or directories to open"), tr("[FILE1, FILE2,...]"));

    parser.process(arguments());

    const QStringList unsupportedOptions = {
        QStringLiteral("desktop"), QStringLiteral("desktop-off"),
        QStringLiteral("desktop-pref"), QStringLiteral("set-wallpaper"),
        QStringLiteral("wallpaper-mode"), QStringLiteral("daemon-mode")
    };
    for(const QString& option : unsupportedOptions) {
        if(parser.isSet(option)) {
            qCritical().noquote() << QStringLiteral("Unsupported option in Win11 Explorer Qt: --") + option;
            std::exit(EXIT_FAILURE);
        }
    }

    if(isPrimaryInstance) {
        qDebug("isPrimaryInstance");

        if(parser.isSet(daemonOption)) {
            daemonMode_ = true;
        }
        if(parser.isSet(profileOption)) {
            profileName_ = parser.value(profileOption);
        }
        static const QRegularExpression safeProfile(QStringLiteral("^[A-Za-z0-9_-]+$"));
        if(!safeProfile.match(profileName_).hasMatch()) {
            qCritical() << "Invalid profile name";
            std::exit(EXIT_FAILURE);
        }

        // load app config
        settings_.load(profileName_);

        // init per-folder config
        QString perFolderConfigFile = settings_.profileDir(profileName_) + QStringLiteral("/dir-settings.conf");
        Fm::FolderConfig::init(perFolderConfigFile.toLocal8Bit().constData());


        if(settings_.useFallbackIconTheme()) {
            QIcon::setThemeName(settings_.fallbackIconThemeName());
        }

        // desktop icon management
        if(parser.isSet(desktopOption)) {
            desktopManager(true);
            keepRunning = true;
        }
        else if(parser.isSet(desktopOffOption)) {
            desktopManager(false);
        }

        if(parser.isSet(desktopPrefOption)) { // desktop preference dialog
            desktopPrefrences(parser.value(desktopPrefOption));
            keepRunning = true;
        }
        else if(parser.isSet(findFilesOption)) { // file searching utility
            findFiles(parser.positionalArguments());
            keepRunning = true;
        }
        else if(parser.isSet(showPrefOption)) { // preferences dialog
            preferences(parser.value(showPrefOption));
            keepRunning = true;
        }
        else if(parser.isSet(setWallpaperOption) || parser.isSet(wallpaperModeOption)) { // set wall paper
            setWallpaper(parser.value(setWallpaperOption), parser.value(wallpaperModeOption));
        }
        else {
            if(!parser.isSet(desktopOption) && !parser.isSet(desktopOffOption)) {
                bool reopenLastTabs = false;
                QStringList paths = parser.positionalArguments();
                if(paths.isEmpty()) {
                    // if no path is specified and we're using daemon mode,
                    // don't open current working directory
                    if(!daemonMode_) {
                        reopenLastTabs = true; // open last tab paths only if no path is specified
                        paths.push_back(QDir::currentPath());
                    }
                }
                if(!paths.isEmpty()) {
                    launchFiles(QDir::currentPath(), paths, parser.isSet(newWindowOption), reopenLastTabs);
                }
                keepRunning = true;
            }
        }
    }
    else {
        QDBusConnection dbus = QDBusConnection::sessionBus();
        QDBusInterface iface(QLatin1String(serviceName), QStringLiteral("/Application"), QLatin1String(ifaceName), dbus, this);
        iface.setTimeout(5000); // never hang the shell for the 25 s D-Bus default
        auto checkReply = [this](const QDBusMessage& reply) {
            if(reply.type() == QDBusMessage::ErrorMessage) {
                qCritical().noquote() << QStringLiteral("win11-explorer-qt: request to the running instance failed: %1").arg(reply.errorMessage());
                exitCode_ = EXIT_FAILURE;
            }
        };
        if(parser.isSet(quitOption)) {
            iface.call(QStringLiteral("quit")); // the reply may never arrive: the instance exits
            return false;
        }

        if(parser.isSet(desktopOption)) {
            iface.call(QStringLiteral("desktopManager"), true);
        }
        else if(parser.isSet(desktopOffOption)) {
            iface.call(QStringLiteral("desktopManager"), false);
        }

        if(parser.isSet(desktopPrefOption)) { // desktop preference dialog
            iface.call(QStringLiteral("desktopPrefrences"), parser.value(desktopPrefOption));
        }
        else if(parser.isSet(findFilesOption)) { // file searching utility
            checkReply(iface.call(QStringLiteral("findFiles"), parser.positionalArguments()));
        }
        else if(parser.isSet(showPrefOption)) { // preferences dialog
            checkReply(iface.call(QStringLiteral("preferences"), parser.value(showPrefOption)));
        }
        else if(parser.isSet(setWallpaperOption) || parser.isSet(wallpaperModeOption)) { // set wall paper
            iface.call(QStringLiteral("setWallpaper"), parser.value(setWallpaperOption), parser.value(wallpaperModeOption));
        }
        else {
            if(!parser.isSet(desktopOption) && !parser.isSet(desktopOffOption)) {
                bool reopenLastTabs = false;
                QStringList paths = parser.positionalArguments();
                if(paths.isEmpty()) {
                    reopenLastTabs = true; // open last tab paths only if no path is specified
                    paths.push_back(QDir::currentPath());
                }
                else {
                    // D-04/D-05: expand "~" here and reject missing local paths in the caller,
                    // with a message on stderr and rc!=0, instead of a modal in the server.
                    bool anyMissing = false;
                    paths = validateCliPaths(paths, QDir::currentPath(), &anyMissing);
                    if(anyMissing) {
                        exitCode_ = EXIT_FAILURE;
                    }
                }
                if(!paths.isEmpty()) {
                    checkReply(iface.call(QStringLiteral("launchFiles"), QDir::currentPath(), paths, parser.isSet(newWindowOption), reopenLastTabs));
                }
            }
        }
    }
    return keepRunning;
}

void Application::init() {
    ::qInitResources_explorer_icons();
    QIcon::setFallbackThemeName(QStringLiteral("Adwaita"));
    QIcon::setThemeName(QStringLiteral("win11-explorer-qt"));

    // install the translations built-into Qt itself
    if(qtTranslator.load(QStringLiteral("qt_") + QLocale::system().name(), QLibraryInfo::path(QLibraryInfo::TranslationsPath))) {
        installTranslator(&qtTranslator);
    }

    // install libfm-qt translator
    installTranslator(libFm_.translator());

    // install our own tranlations
    const QString translationFile = QStringLiteral("pcmanfm-qt_") + QLocale::system().name();
    for(const QString& dir : translationDirs()) {
        if(translator.load(translationFile, dir)) {
            installTranslator(&translator);
            break;
        }
    }
}

int Application::exec() {

    if(!parseCommandLineArgs()) {
        return exitCode_;
    }

    if(daemonMode_) { // keep running even when there is no window opened.
        setQuitOnLastWindowClosed(false);
    }

    return QCoreApplication::exec();
}


void Application::onUserDirsChanged() {
    // Desktop-folder watching is intentionally unsupported in this browser.
}

void Application::onAboutToQuit() {
    qDebug("aboutToQuit");

    settings_.save();
}

// D-11: settings were only written in aboutToQuit, so a crash lost window size/tabs.
void Application::scheduleSettingsSave() {
    if(!isPrimaryInstance) {
        return;
    }
    if(!saveTimer_) {
        saveTimer_ = new QTimer(this);
        saveTimer_->setSingleShot(true);
        connect(saveTimer_, &QTimer::timeout, this, &Application::saveSettingsNow);
    }
    saveTimer_->start(1000);
}

void Application::saveSettingsNow() {
    settings_.save();
}

void Application::cleanPerFolderConfig() {
    // first save the perfolder config cache to have the list of all custom folders
    Fm::FolderConfig::saveCache();
    // then remove non-existent native folders from the list of custom folders
    QByteArray perFolderConfig = (settings_.profileDir(profileName_) + QStringLiteral("/dir-settings.conf"))
                                 .toLocal8Bit();
    GKeyFile* kf = g_key_file_new();
    if(g_key_file_load_from_file(kf, perFolderConfig.constData(), G_KEY_FILE_NONE, nullptr)) {
        bool removed(false);
        gchar **groups = g_key_file_get_groups(kf, nullptr);
        for(int i = 0; groups[i] != nullptr; i++) {
            const gchar *g = groups[i];
            if(Fm::FilePath::fromPathStr(g).isNative() && !QDir(QString::fromUtf8(g)).exists()) {
                g_key_file_remove_group(kf, g, nullptr);
                removed = true;
            }
        }
        g_strfreev(groups);
        if(removed) {
            g_key_file_save_to_file(kf, perFolderConfig.constData(), nullptr);
        }
    }
    g_key_file_free(kf);
}

/*bool Application::eventFilter(QObject* watched, QEvent* event) {
    if(watched == desktop()) {
        if(event->type() == QEvent::StyleChange ||
                event->type() == QEvent::ThemeChange) {
            setStyle(new ProxyStyle());
        }
    }
    return QObject::eventFilter(watched, event);
}*/

void Application::onLastWindowClosed() {

}

void Application::onSaveStateRequest(QSessionManager& /*manager*/) {

}

void Application::desktopManager(bool enabled) {
    Q_UNUSED(enabled);
    enableDesktopManager_ = false;
}

void Application::desktopPrefrences(const QString& page) {
    Q_UNUSED(page);
}

void Application::onFindFileAccepted() {
    Fm::FileSearchDialog* dlg = static_cast<Fm::FileSearchDialog*>(sender());
    // get search settings
    settings_.setSearchNameCaseInsensitive(dlg->nameCaseInsensitive());
    settings_.setsearchContentCaseInsensitive(dlg->contentCaseInsensitive());
    settings_.setSearchNameRegexp(dlg->nameRegexp());
    settings_.setSearchContentRegexp(dlg->contentRegexp());
    settings_.setSearchRecursive(dlg->recursive());
    settings_.setSearchhHidden(dlg->searchhHidden());
    settings_.addNamePattern(dlg->namePattern());
    settings_.addContentPattern(dlg->contentPattern());

    Fm::FilePathList paths;
    paths.emplace_back(dlg->searchUri());
    MainWindow* window = MainWindow::lastActive();
    Launcher(window).launchPaths(nullptr, paths);
}

void Application::onConnectToServerAccepted() {
    ConnectServerDialog* dlg = static_cast<ConnectServerDialog*>(sender());
    QString uri = dlg->uriText();
    Fm::FilePathList paths;
    paths.push_back(Fm::FilePath::fromUri(uri.toUtf8().constData()));
    MainWindow* window = MainWindow::lastActive();
    Launcher(window).launchPaths(nullptr, paths);
}

void Application::findFiles(QStringList paths) {
    // launch file searching utility.
    Fm::FileSearchDialog* dlg = new Fm::FileSearchDialog(paths);
    connect(dlg, &QDialog::accepted, this, &Application::onFindFileAccepted);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    // set search settings
    dlg->setNameCaseInsensitive(settings_.searchNameCaseInsensitive());
    dlg->setContentCaseInsensitive(settings_.searchContentCaseInsensitive());
    dlg->setNameRegexp(settings_.searchNameRegexp());
    dlg->setContentRegexp(settings_.searchContentRegexp());
    dlg->setRecursive(settings_.searchRecursive());
    dlg->setSearchhHidden(settings_.searchhHidden());
    dlg->addNamePatterns(settings_.namePatterns());
    dlg->addContentPatterns(settings_.contentPatterns());

    dlg->show();
}

void Application::connectToServer() {
    ConnectServerDialog* dlg = new ConnectServerDialog();
    connect(dlg, &QDialog::accepted, this, &Application::onConnectToServerAccepted);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
}

QString Application::expandUserPath(const QString& path) {
    if(path == QLatin1String("~") || path.startsWith(QLatin1String("~/"))) {
        return QDir::homePath() + path.mid(1);
    }
    if(path.startsWith(QLatin1Char('$'))) {
        static const QRegularExpression varRe(QStringLiteral("^\\$(?:\\{([A-Za-z_][A-Za-z0-9_]*)\\}|([A-Za-z_][A-Za-z0-9_]*))"));
        const auto m = varRe.match(path);
        if(m.hasMatch()) {
            const QString name = m.captured(1).isEmpty() ? m.captured(2) : m.captured(1);
            const QByteArray key = name.toLocal8Bit();
            // an unset OR empty variable is left alone (an empty one would turn "$X/a" into "/a")
            if(!qEnvironmentVariableIsEmpty(key.constData())) {
                return qEnvironmentVariable(key.constData()) + path.mid(m.capturedLength());
            }
        }
    }
    return path;
}

// A command-line/address-bar argument is a local path unless it is a URI ("scheme:..."). An
// absolute path (leading '/') is always local, even if it contains ":/" (e.g. /mnt/a:/b).
bool Application::isLocalPathArgument(const QString& path) {
    if(path.startsWith(QLatin1Char('/'))) {
        return true;
    }
    // a URI is "scheme:/..." (sftp://, trash:///, file:///); "foo:bar" and "dir/a:/b" are local names
    static const QRegularExpression schemeRe(QStringLiteral("^[A-Za-z][A-Za-z0-9+.-]*:/"));
    return !schemeRe.match(path).hasMatch();
}

QStringList Application::validateCliPaths(const QStringList& raw, const QString& cwd, bool* anyMissing) {
    QStringList valid;
    for(const QString& r : raw) {
        const QString expanded = expandUserPath(r); // only to validate; r is what gets forwarded
        if(isLocalPathArgument(expanded)) {
            const QFileInfo info(QDir(cwd).absoluteFilePath(expanded));
            if(!info.exists() && !info.isSymLink()) {
                qCritical().noquote() << QStringLiteral("win11-explorer-qt: cannot open '%1': no such file or directory").arg(expanded);
                if(anyMissing) {
                    *anyMissing = true;
                }
                continue;
            }
        }
        valid.push_back(r);
    }
    return valid;
}

Fm::FilePath Application::filePathFromArgument(const QString& expanded, const QString& cwd) {
    const QByteArray pathName = expanded.toLocal8Bit();
    if(pathName.startsWith('/')) {
        return Fm::FilePath::fromLocalPath(pathName.constData());
    }
    if(!isLocalPathArgument(expanded)) {
        return Fm::FilePath::fromUri(pathName.constData());
    }
    return Fm::FilePath::fromLocalPath(cwd.toLocal8Bit().constData()).relativePath(pathName.constData());
}

QMessageBox* Application::makeErrorBox(QWidget* parent, const QString& title, const QString& text) {
    auto* box = new QMessageBox(QMessageBox::Critical, title, text, QMessageBox::Ok, parent);
    box->setTextFormat(Qt::PlainText); // GIO messages carry file names / remote text
    return box;
}

// Text typed in an address bar: expand "~"/"$VAR", ignore blank input, and only strip the
// surrounding whitespace when the path as typed does not exist (a POSIX name may end in a space).
QString Application::normalizeTypedPath(const QString& typed) {
    const QString trimmed = typed.trimmed();
    if(trimmed.isEmpty()) {
        return QString();
    }
    if(trimmed != typed) {
        const QString literal = expandUserPath(typed);
        if(isLocalPathArgument(literal) && QFileInfo::exists(literal)) {
            return literal;
        }
    }
    return expandUserPath(trimmed);
}

// Directories searched for translations: relocatable ones first, the build-time prefix last.
QStringList Application::translationDirs() {
    const QString appDir = QCoreApplication::applicationDirPath();
    return {
        QDir::cleanPath(appDir + QStringLiteral("/../share/win11-explorer-qt/translations")),
        QDir::cleanPath(appDir + QStringLiteral("/../share/pcmanfm-qt/translations")),
        QStringLiteral(PCMANFM_DATA_DIR) + QStringLiteral("/translations")};
}

void Application::launchFiles(const QString& cwd, const QStringList& paths, bool inNewWindow, bool reopenLastTabs) {
    // D-04: return to the (D-Bus) caller immediately; the work may run nested event loops
    // and modal dialogs, which used to hold the caller for the 25 s D-Bus timeout.
    QMetaObject::invokeMethod(this, [this, cwd, paths, inNewWindow, reopenLastTabs] {
        doLaunchFiles(cwd, paths, inNewWindow, reopenLastTabs);
    }, Qt::QueuedConnection);
}

void Application::doLaunchFiles(const QString& cwd, const QStringList& paths, bool inNewWindow, bool reopenLastTabs) {
    Fm::FilePathList pathList;
    auto _paths = paths;

    openingLastTabs_ = reopenLastTabs && settings_.reopenLastTabs() && !settings_.tabPaths().isEmpty();
    if(openingLastTabs_) {
        _paths = settings_.tabPaths();
        // forget tab paths with next windows until the last one is closed
        settings_.setTabPaths(QStringList());
    }

    for(const QString& it : std::as_const(_paths)) {
        // Expand exactly once: the CLI forwards raw arguments; the saved tab paths are already
        // final (a "$"/"~" in them is part of a real name).
        const QString arg = openingLastTabs_ ? it : expandUserPath(it);
        if(arg.isEmpty()) {
            continue;
        }
        pathList.push_back(filePathFromArgument(arg, cwd));
    }

    if(!inNewWindow && settings_.singleWindowMode()) {
        MainWindow* window = MainWindow::lastActive();
        // if there is no last active window, find the last created window
        if(window == nullptr) {
            QWidgetList windows = topLevelWidgets();
            for(int i = 0; i < windows.size(); ++i) {
                auto win = windows.at(windows.size() - 1 - i);
                if(win->inherits("PCManFM::MainWindow")) {
                    window = static_cast<MainWindow*>(win);
                    break;
                }
            }
        }
        if(window != nullptr && openingLastTabs_) {
            // other folders have been opened explicitly in this window;
            // restoring of tab split number does not make sense
            settings_.setSplitViewTabsNum(0);
        }
        auto launcher = Launcher(window);
        launcher.openInNewTab();
        launcher.launchPaths(nullptr, pathList);
    }
    else {
        Launcher(nullptr).launchPaths(nullptr, pathList);
    }

    if(openingLastTabs_) {
        openingLastTabs_ = false;

        // if none of the last tabs can be opened and there is no main window yet,
        // open the current directory
        bool hasWindow = false;
        const QWidgetList windows = topLevelWidgets();
        for(const auto& win : windows) {
            if(win->inherits("PCManFM::MainWindow")) {
                hasWindow = true;
                break;
            }
        }
        if(!hasWindow) {
            _paths.clear();
            _paths.push_back(QDir::currentPath());
            doLaunchFiles(QDir::currentPath(), _paths, inNewWindow, false);
        }
    }
}

void Application::openFolders(Fm::FileInfoList files) {
    Launcher(nullptr).launchFiles(nullptr, std::move(files));
}

void Application::openFolderInTerminal(Fm::FilePath path) {
    if(!settings_.terminal().isEmpty()) {
        Fm::GErrorPtr err;
        auto terminalName = settings_.terminal().toUtf8();
        if(!Fm::launchTerminal(terminalName.constData(), path, err)) {
            QScopedPointer<QMessageBox> box(makeErrorBox(nullptr, tr("Error"), err.message()));
            box->exec();
        }
    }
    else {
        // show an error message and ask the user to set the command
        QScopedPointer<QMessageBox> box(makeErrorBox(nullptr, tr("Error"), tr("Terminal emulator is not set.")));
        box->exec();
        preferences(QStringLiteral("advanced"));
    }
}

void Application::preferences(const QString& page) {
    // open preference dialog
    if(!preferencesDialog_) {
        preferencesDialog_ = new PreferencesDialog(page);
    }
    else {
        preferencesDialog_.data()->selectPage(page);
    }
    preferencesDialog_.data()->show();
    preferencesDialog_.data()->raise();
    preferencesDialog_.data()->activateWindow();
}

void Application::setWallpaper(const QString& path, const QString& modeString) {
    Q_UNUSED(path);
    Q_UNUSED(modeString);
}

/* This method receives a list of file:// URIs from DBus and for each URI opens
 * a tab showing its content.
 */
void Application::ShowFolders(const QStringList& uriList, const QString& startupId __attribute__((unused))) {
    if(!uriList.isEmpty()) {
        launchFiles(QDir::currentPath(), uriList, false, false);
    }
}

/* This method receives a list of file:// URIs from DBus and opens windows
 * or tabs for each folder, highlighting all listed items within each.
 */
void Application::ShowItems(const QStringList& uriList, const QString& startupId __attribute__((unused))) {
    std::unordered_map<Fm::FilePath, Fm::FilePathList, Fm::FilePathHash> groups;
    Fm::FilePathList folders; // used only for keeping the original order
    for(const auto& u : uriList) {
        if(auto path = Fm::FilePath::fromPathStr(u.toStdString().c_str())) {
            if(auto parent = path.parent()) {
                auto paths = groups[parent];
                if(std::find(paths.cbegin(), paths.cend(), path) == paths.cend()) {
                    groups[parent].push_back(std::move(path));
                }
                // also remember the order of parent folders
                if(std::find(folders.cbegin(), folders.cend(), parent) == folders.cend()) {
                    folders.push_back(std::move(parent));
                }
            }
        }
    }

    if(groups.empty()) {
        return;
    }

    PCManFM::MainWindow* window = nullptr;
    if(settings_.singleWindowMode()) {
        window = MainWindow::lastActive();
        if(window == nullptr) {
            QWidgetList windows = topLevelWidgets();
            for(int i = 0; i < windows.size(); ++i) {
                auto win = windows.at(windows.size() - 1 - i);
                if(win->inherits("PCManFM::MainWindow")) {
                    window = static_cast<MainWindow*>(win);
                    break;
                }
            }
        }
    }
    if(window == nullptr) {
        window = new MainWindow();
    }

    for(const auto& folder : folders) {
        window->openFolderAndSelectFiles(groups[folder]);
    }

    window->show();
    window->raise();
    window->activateWindow();
}

/* This method receives a list of file:// URIs from DBus and
 * for each valid URI opens a property dialog showing its information
 */
void Application::ShowItemProperties(const QStringList& uriList, const QString& startupId __attribute__((unused))) {
    // FIXME: Should we add "Fm::FilePropsDialog::showForPath()" to libfm-qt, instead of doing this?
    Fm::FilePathList paths;
    for(const auto& u : uriList) {
        Fm::FilePath path = Fm::FilePath::fromPathStr(u.toStdString().c_str());
        if(path) {
            paths.push_back(std::move(path));
        }
    }
    if(paths.empty()) {
        return;
    }
    auto job = new Fm::FileInfoJob{std::move(paths)};
    job->setAutoDelete(true);
    connect(job, &Fm::FileInfoJob::finished, this, &Application::onPropJobFinished, Qt::BlockingQueuedConnection);
    job->runAsync();
}

void Application::onPropJobFinished() {
    auto job = static_cast<Fm::FileInfoJob*>(sender());
    for(auto file: job->files()) {
        auto dialog = Fm::FilePropsDialog::showForFile(std::move(file));
        dialog->raise();
        dialog->activateWindow();
    }
}

DesktopWindow* Application::createDesktopWindow(int screenNum, const QString& screenName) {
    Q_UNUSED(screenNum);
    Q_UNUSED(screenName);
    return nullptr;
}

// called when Settings is changed to update UI
void Application::updateFromSettings() {
    // if(iconTheme.isEmpty())
    //  Fm::IconTheme::setThemeName(settings_.fallbackIconThemeName());

    // update main windows and desktop windows
    QWidgetList windows = this->topLevelWidgets();
    QWidgetList::iterator it;
    for(it = windows.begin(); it != windows.end(); ++it) {
        QWidget* window = *it;
        if(window->inherits("PCManFM::MainWindow")) {
            MainWindow* mainWindow = static_cast<MainWindow*>(window);
            mainWindow->updateFromSettings(settings_);
        }
    }
    if(desktopManagerEnabled()) {
        updateDesktopsFromSettings();
    }
}

void Application::updateDesktopsFromSettings(bool changeSlide, bool allowShortcutRemoval) {
    Q_UNUSED(changeSlide);
    Q_UNUSED(allowShortcutRemoval);
}

void Application::editBookmarks() {
    if(!editBookmarksialog_) {
        editBookmarksialog_ = new Fm::EditBookmarksDialog(Fm::Bookmarks::globalInstance());
    }
    editBookmarksialog_.data()->show();
}

void Application::initVolumeManager() {
}

bool Application::autoMountVolume(GVolume* volume, bool interactive) {
    Q_UNUSED(volume);
    Q_UNUSED(interactive);
    return false;
}

// static
void Application::onVolumeAdded(GVolumeMonitor* /*monitor*/, GVolume* volume, Application* pThis) {
    Q_UNUSED(volume);
    Q_UNUSED(pThis);
}

#if 0
bool Application::nativeEventFilter(const QByteArray& eventType, void* message, long* result) {
    if(eventType == "xcb_generic_event_t") { // XCB event
        // filter all native X11 events (xcb)
        xcb_generic_event_t* generic_event = reinterpret_cast<xcb_generic_event_t*>(message);
        // qDebug("XCB event: %d", generic_event->response_type & ~0x80);
        Q_FOREACH(DesktopWindow* window, desktopWindows_) {
        }
    }
    return false;
}
#endif

void Application::onScreenAdded(QScreen* newScreen) {
    Q_UNUSED(newScreen);
}

void Application::onScreenRemoved(QScreen* oldScreen) {
    Q_UNUSED(oldScreen);
}

void Application::onScreenDestroyed(QObject* screenObj) {
    // NOTE by PCMan: This is a workaround for Qt 5 bug #40681.
    // With this very dirty workaround, we can fix lxqt/lxqt bug #204, #205, and #206.
    // Qt 5 has two new regression bugs which breaks lxqt-panel in a multihead environment.
    // #40681: Regression bug: QWidget::winId() returns old value and QEvent::WinIdChange event is not emitted sometimes. (multihead setup)
    // #40791: Regression: QPlatformWindow, QWindow, and QWidget::winId() are out of sync.
    // Explanations for the workaround:
    // Internally, Qt maintains a list of QScreens and update it when XRandR configuration changes.
    // When the user turn off an monitor with xrandr --output <xxx> --off, this will destroy the QScreen
    // object which represent the output. If the QScreen being destroyed contains our panel widget,
    // Qt will call QWindow::setScreen(0) on the internal windowHandle() of our panel widget to move it
    // to the primary screen. However, moving a window to a different screen is more than just changing
    // its position. With XRandR, all screens are actually part of the same virtual desktop. However,
    // this is not the case in other setups, such as Xinerama and moving a window to another screen is
    // not possible unless you destroy the widget and create it again for a new screen.
    // Therefore, Qt destroy the widget and re-create it when moving our panel to a new screen.
    // Unfortunately, destroying the window also destroy the child windows embedded into it,
    // using XEMBED such as the tray icons. (#206)
    // Second, when the window is re-created, the winId of the QWidget is changed, but Qt failed to
    // generate QEvent::WinIdChange event so we have no way to know that. We have to set
    // some X11 window properties using the native winId() to make it a dock, but this stop working
    // because we cannot get the correct winId(), so this causes #204 and #205.
    //
    // The workaround is very simple. Just completely destroy the window before Qt has a chance to do
    // QWindow::setScreen() for it. Later, we recreate the window ourselves. So this can bypassing the Qt bugs.
    Q_UNUSED(screenObj);
}

void Application::reloadDesktopsAsNeeded() {
}

void Application::onVirtualGeometryChanged(const QRect& /*rect*/) {
}

void Application::onAvailableGeometryChanged(const QRect& /*rect*/) {
}


static int sigterm_fd[2];

static void sigtermHandler(int) {
    char c = 1;
    auto w = ::write(sigterm_fd[0], &c, sizeof(c));
    Q_UNUSED(w);
}

void Application::installSigtermHandler() {
    if(::socketpair(AF_UNIX, SOCK_STREAM, 0, sigterm_fd) == 0) {
        QSocketNotifier* notifier = new QSocketNotifier(sigterm_fd[1], QSocketNotifier::Read, this);
        connect(notifier, &QSocketNotifier::activated, this, &Application::onSigtermNotified);

        struct sigaction action;
        action.sa_handler = sigtermHandler;
        ::sigemptyset(&action.sa_mask);
        action.sa_flags = SA_RESTART;
        if(::sigaction(SIGTERM, &action, nullptr) != 0) {
            qWarning("Couldn't install SIGTERM handler");
        }
    }
    else {
        qWarning("Couldn't create SIGTERM socketpair");
    }
}

void Application::onSigtermNotified() {
    if(QSocketNotifier* notifier = qobject_cast<QSocketNotifier*>(sender())) {
        notifier->setEnabled(false);
        char c;
        auto r = ::read(sigterm_fd[1], &c, sizeof(c));
        Q_UNUSED(r);
        // close all windows cleanly; otherwise, we might get this warning:
        // "QBasicTimer::start: QBasicTimer can only be used with threads started with QThread"
        const auto windows = topLevelWidgets();
        for(const auto& win : windows) {
            if(win->inherits("PCManFM::MainWindow")) {
                MainWindow* mainWindow = static_cast<MainWindow*>(win);
                mainWindow->close();
            }
        }
        quit();
        notifier->setEnabled(true);
    }
}

} // namespace PCManFM
