// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QByteArray>
#include <X11/Xlib.h>
#include <sys/socket.h>
#include <unistd.h>

// Fail closed before QApplication creates windows and again on the actual input
// connection. This prevents accidental concurrency, not malicious same-UID code.
namespace SelectionOwnership {
inline QByteArray read(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
inline QList<QByteArray> stat(qint64 pid) {
    auto text = read(QStringLiteral("/proc/%1/stat").arg(pid));
    return text.mid(text.lastIndexOf(')') + 2).simplified().split(' ');
}
// Qt 6.8.3 QGuiApplicationPrivate::createPlatformIntegration() consumes
// -platform/-platformpluginpath (also --); QXcbIntegration consumes -display.
// Reject rather than strip: QtTest case/report arguments remain untouched.
// Require one explicit backend, never environment-driven fallback selection.
inline bool qtRoutingAllowed(int argc, char* const* argv) {
    if(qgetenv("QT_QPA_PLATFORM") != "xcb") return false;
    for(int i = 1; i < argc; ++i) {
        QByteArray arg(argv[i]);
        if(arg.startsWith("--")) arg.remove(0, 1);
        arg = arg.split('=').first();
        if(arg == "-display" || arg == "-platform" || arg == "-platformpluginpath") return false;
    }
    return true;
}
inline bool verify(Display* display = nullptr) {
    const auto target = qEnvironmentVariable("EXPLORER_XTEST_DISPLAY");
    if(!QRegularExpression(QStringLiteral("^:(?:[2-9]|[1-9][0-9]+)$")).match(target).hasMatch()
       || qEnvironmentVariable("DISPLAY") != target) return false;
    const auto server = qEnvironmentVariableIntValue("EXPLORER_XSERVER_PID");
    const auto owner = qEnvironmentVariableIntValue("EXPLORER_XOWNER_PID");
    if(server <= 1 || owner <= 1) return false;
    const auto ss = stat(server), os = stat(owner);
    if(ss.size() <= 19 || os.size() <= 19 || ss[0] == "Z" || os[0] == "Z"
       || ss[1].toLongLong() != owner
       || ss[19] != qgetenv("EXPLORER_XSERVER_START") || os[19] != qgetenv("EXPLORER_XOWNER_START")) return false;
    const auto exe = QFileInfo(QStringLiteral("/proc/%1/exe").arg(server)).symLinkTarget();
    const auto args = read(QStringLiteral("/proc/%1/cmdline").arg(server)).split('\0');
    if(QFileInfo(exe).fileName() != QStringLiteral("Xephyr") || !args.contains(target.toUtf8())) return false;
    qint64 ancestor = getpid();
    while(ancestor > 1 && ancestor != owner) {
        const auto fields = stat(ancestor);
        if(fields.size() <= 1) return false;
        ancestor = fields[1].toLongLong();
    }
    if(ancestor != owner) return false;
    const bool close = !display;
    if(close) display = XOpenDisplay(nullptr);
    if(!display) return false;
    struct ucred peer {};
    socklen_t size = sizeof(peer);
    const bool owned = getsockopt(ConnectionNumber(display), SOL_SOCKET, SO_PEERCRED, &peer, &size) == 0
                       && size == sizeof(peer) && peer.pid == server && peer.uid == getuid();
    if(close) XCloseDisplay(display);
    const auto afterServer = stat(server), afterOwner = stat(owner);
    return owned && afterServer.size() > 19 && afterOwner.size() > 19
           && afterServer[0] != "Z" && afterOwner[0] != "Z"
           && afterServer[1] == ss[1] && afterServer[19] == ss[19] && afterOwner[19] == os[19];
}
} // namespace SelectionOwnership
