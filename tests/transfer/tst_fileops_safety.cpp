// SPDX-License-Identifier: GPL-2.0-or-later
// Data-loss / file-operation regressions (audit FO-01..FO-07) against the REAL private libfm-qt overlay.
// Only the dialog is scripted (answers to conflict/error prompts); jobs, conflict logic, clipboard and
// trash are the real ones. All paths live inside test-owned temporary directories. HOME/XDG_* are
// redirected by run-fileops-safety.sh BEFORE the private D-Bus session (and thus gvfsd-trash) starts;
// main() refuses to run otherwise, so neither the real trash nor the user's configuration is touched.
#include <QtTest>
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMimeData>
#include <QMessageBox>
#include <QPointer>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTimer>
#include <atomic>
#include <csignal>
#include <dlfcn.h>
#include <pwd.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <cstdio>
#include <cstring>
#include <glib.h>
#include <gio/gio.h>
#include <fileoperation.h>
#include <fileoperationdialog.h>
#include <utilities.h>
#include "core/filepath.h"
#include "core/fileinfo.h"

namespace {
enum class Ask { Cancel, Overwrite, Skip };
Ask gAsk = Ask::Cancel;
int gAskCount = 0;
int gErrorCount = 0;      // errors the user would SEE (severity >= MODERATE, like FileOperationDialog)
int gMildCount = 0;       // MILD/WARNING: ignored by the real dialog, not counted as errors
QStringList gErrorMessages;

class ScriptDialog final : public Fm::FileOperationDialog {
public:
    explicit ScriptDialog(Fm::FileOperation* operation) : FileOperationDialog(operation) {}
    Fm::FileOperationJob::FileExistsAction askRename(const Fm::FileInfo&, const Fm::FileInfo&, Fm::FilePath&) override {
        ++gAskCount;
        if(gAskCount > 4) return Fm::FileOperationJob::CANCEL;  // never loop forever on a broken build
        switch(gAsk) {
        case Ask::Overwrite: return Fm::FileOperationJob::OVERWRITE;
        case Ask::Skip: return Fm::FileOperationJob::SKIP;
        default: return Fm::FileOperationJob::CANCEL;
        }
    }
    Fm::Job::ErrorAction error(GError* err, Fm::Job::ErrorSeverity severity) override {
        if(severity < Fm::Job::ErrorSeverity::MODERATE) {  // the real dialog shows nothing for these
            ++gMildCount;
            return Fm::Job::ErrorAction::CONTINUE;
        }
        ++gErrorCount;
        gErrorMessages << QString::fromUtf8(err->message);
        return Fm::Job::ErrorAction::CONTINUE;
    }
};

Fm::FileOperationDialog* scriptDialogFactory(Fm::FileOperation* operation) {
    return new ScriptDialog(operation);
}

// Fault injection by symbol interposition: this executable exports g_file_query_info/g_file_move, so
// the calls made by libfm-qt (through the PLT) land here first and forward to the real GIO.
std::atomic<bool> gQueryFaultArmed{false};
std::atomic<int> gQueryFaults{0};
std::atomic<bool> gQueryFaultNotFound{false};  // inject NOT_FOUND (a lying "absent") instead of TIMED_OUT
std::atomic<int> gQueryFaultMinAsks{0};        // ... only once the conflict prompt was answered this many times
QByteArray gQueryFaultName;
std::atomic<bool> gMoveExistsArmed{false};
std::atomic<int> gMoveFaults{0};

Fm::FilePath P(const QString& path) {
    return Fm::FilePath::fromLocalPath(path.toUtf8().constData());
}
Fm::FilePath U(const QString& uri) {
    return Fm::FilePath::fromUri(uri.toUtf8().constData());
}

// Process-wide write-size limit: a deterministic, root-free stand-in for ENOSPC/EIO on write.
class WriteLimit {
public:
    explicit WriteLimit(rlim_t bytes) {
        std::signal(SIGXFSZ, SIG_IGN);
        getrlimit(RLIMIT_FSIZE, &old_);
        rlimit limit = old_;
        limit.rlim_cur = bytes;
        setrlimit(RLIMIT_FSIZE, &limit);
    }
    ~WriteLimit() { setrlimit(RLIMIT_FSIZE, &old_); }
private:
    rlimit old_{};
};

QByteArray pattern(qsizetype size, char seed) {
    QByteArray data(size, seed);
    for(qsizetype i = 0; i < size; i += 4099) data[i] = char('a' + (i / 4099) % 26);
    return data;
}
bool writeFile(const QString& path, const QByteArray& data) {
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(data) == data.size() && (f.close(), true);
}
QByteArray readFile(const QString& path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray("<unreadable>");
}
QStringList names(const QString& dir) {
    return QDir(dir).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System, QDir::Name);
}
// Wait for the FileOperation to finish (it auto-destroys afterwards).
bool waitFinished(Fm::FileOperation* op, int timeoutMs = 20000) {
    if(!op) return true;
    QEventLoop loop;
    bool done = false;
    QObject::connect(op, &Fm::FileOperation::finished, &loop, [&] { done = true; QTimer::singleShot(0, &loop, &QEventLoop::quit); });
    QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
    loop.exec();
    return done;
}
bool clipboardHasFiles() {
    const QMimeData* data = QApplication::clipboard()->mimeData();
    return data && data->hasFormat(QStringLiteral("x-special/gnome-copied-files"));
}
void resetCounters(Ask ask) {
    gAsk = ask;
    gAskCount = gErrorCount = gMildCount = 0;
    gErrorMessages.clear();
}
}

extern "C" {
__attribute__((visibility("default"))) GFileInfo* g_file_query_info(GFile* file, const char* attributes, GFileQueryInfoFlags flags,
                             GCancellable* cancellable, GError** error) {
    using Fn = GFileInfo* (*)(GFile*, const char*, GFileQueryInfoFlags, GCancellable*, GError**);
    static Fn real = reinterpret_cast<Fn>(dlsym(RTLD_NEXT, "g_file_query_info"));
    // Only the job's "does the destination exist?" probe (type only, no-follow) on the armed name.
    if(gQueryFaultArmed.load() && attributes && std::strcmp(attributes, "standard::type") == 0 &&
       (flags & G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS)) {
        char* base = g_file_get_basename(file);
        const bool hit = base && gQueryFaultName == base && gAskCount >= gQueryFaultMinAsks.load();
        g_free(base);
        if(hit) {
            ++gQueryFaults;
            if(gQueryFaultNotFound.load()) {
                g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "injected: destination reported absent");
            }
            else {
                g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT, "injected transient query failure");
            }
            return nullptr;
        }
    }
    return real(file, attributes, flags, cancellable, error);
}

__attribute__((visibility("default"))) gboolean g_file_move(GFile* source, GFile* destination, GFileCopyFlags flags, GCancellable* cancellable,
                     GFileProgressCallback cb, gpointer data, GError** error) {
    using Fn = gboolean (*)(GFile*, GFile*, GFileCopyFlags, GCancellable*, GFileProgressCallback, gpointer, GError**);
    static Fn real = reinterpret_cast<Fn>(dlsym(RTLD_NEXT, "g_file_move"));
    if(gMoveExistsArmed.load()) {
        char* base = g_file_get_basename(source);
        const bool temp = base && base[0] == '.' && g_str_has_suffix(base, ".partial");
        g_free(base);
        if(temp) {
            ++gMoveFaults;
            g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_EXISTS, "injected EXISTS from the final rename");
            return FALSE;
        }
    }
    return real(source, destination, flags, cancellable, cb, data, error);
}
}

class FileOpsSafetyTest final : public QObject {
    Q_OBJECT
    QTemporaryDir work_;
    QTemporaryDir shm_{QStringLiteral("/dev/shm/fileops-safety-XXXXXX")};
    QString dir(const QString& name) {
        const QString path = work_.filePath(name);
        QDir().mkpath(path);
        return path;
    }
private Q_SLOTS:
    void initTestCase() {
        QVERIFY(work_.isValid());
        Fm::FileOperation::setDialogFactory(&scriptDialogFactory);
    }
    void cleanup() { QApplication::clipboard()->clear(); }

    // FO-01
    void overwriteWriteFailureKeepsOriginal() {
        const QString src = dir(QStringLiteral("fo01-src")), dst = dir(QStringLiteral("fo01-dst"));
        const QByteArray original = pattern(5000, 'O');
        QVERIFY(writeFile(src + QStringLiteral("/big.bin"), pattern(3 << 20, 'N')));
        QVERIFY(writeFile(dst + QStringLiteral("/big.bin"), original));
        resetCounters(Ask::Overwrite);
        {
            WriteLimit limit(1 << 20);
            QVERIFY(waitFinished(Fm::FileOperation::copyFiles({P(src + QStringLiteral("/big.bin"))}, P(dst))));
        }
        QVERIFY2(gErrorCount >= 1, "the write failure must be reported to the user");
        QCOMPARE(readFile(dst + QStringLiteral("/big.bin")), original);
        QCOMPARE(names(dst), QStringList{QStringLiteral("big.bin")});  // temp file removed
    }
    void overwriteSuccessReplacesContentAndKeepsMode() {
        const QString src = dir(QStringLiteral("fo01b-src")), dst = dir(QStringLiteral("fo01b-dst"));
        const QByteArray fresh = pattern(200000, 'F');
        QVERIFY(writeFile(src + QStringLiteral("/f.bin"), fresh));
        QVERIFY(writeFile(dst + QStringLiteral("/f.bin"), pattern(10, 'x')));
        QVERIFY(QFile::setPermissions(src + QStringLiteral("/f.bin"), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
        const QFileDevice::Permissions originalMode = QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadGroup;
        QVERIFY(QFile::setPermissions(dst + QStringLiteral("/f.bin"), originalMode));
        resetCounters(Ask::Overwrite);
        QVERIFY(waitFinished(Fm::FileOperation::copyFiles({P(src + QStringLiteral("/f.bin"))}, P(dst))));
        QCOMPARE(gAskCount, 1);
        QCOMPARE(gErrorCount, 0);
        QCOMPARE(readFile(dst + QStringLiteral("/f.bin")), fresh);
        // The overwritten file keeps ITS permissions (0640), not the source's (0700).
        struct stat st;
        QCOMPARE(::stat(QFile::encodeName(dst + QStringLiteral("/f.bin")).constData(), &st), 0);
        QCOMPARE(int(st.st_mode & 07777), 0640);
        QCOMPARE(names(dst), QStringList{QStringLiteral("f.bin")});
    }
    // B1: an existence probe that fails for any reason other than NOT_FOUND is "unknown": never delete.
    void overwriteWithUnreliableExistenceQueryNeverDeletesOriginal() {
        const QString src = dir(QStringLiteral("b1-src")), dst = dir(QStringLiteral("b1-dst"));
        const QByteArray original = pattern(5000, 'O');
        QVERIFY(writeFile(src + QStringLiteral("/big.bin"), pattern(3 << 20, 'N')));
        QVERIFY(writeFile(dst + QStringLiteral("/big.bin"), original));
        resetCounters(Ask::Overwrite);
        gQueryFaultName = "big.bin";
        gQueryFaults = 0;
        gQueryFaultArmed = true;
        {
            WriteLimit limit(1 << 20);
            QVERIFY(waitFinished(Fm::FileOperation::copyFiles({P(src + QStringLiteral("/big.bin"))}, P(dst))));
        }
        gQueryFaultArmed = false;
        QVERIFY2(gQueryFaults >= 1, "the fault hook was never reached: the test would be vacuous");
        QVERIFY2(QFile::exists(dst + QStringLiteral("/big.bin")), "original destination was deleted");
        QCOMPARE(readFile(dst + QStringLiteral("/big.bin")), original);
        QCOMPARE(names(dst), QStringList{QStringLiteral("big.bin")});
    }
    // Regression: temp name must not push a long destination name over NAME_MAX.
    void overwriteOfVeryLongFileNameWorks() {
        const QString src = dir(QStringLiteral("long-src")), dst = dir(QStringLiteral("long-dst"));
        const QString name = QString(250, QLatin1Char('n')) + QStringLiteral(".bin");  // 254 bytes
        QVERIFY(writeFile(src + QLatin1Char('/') + name, "new"));
        QVERIFY(writeFile(dst + QLatin1Char('/') + name, "old"));
        resetCounters(Ask::Overwrite);
        QVERIFY(waitFinished(Fm::FileOperation::copyFiles({P(src + QLatin1Char('/') + name)}, P(dst))));
        QCOMPARE(gAskCount, 1);
        QCOMPARE(gErrorCount, 0);
        QCOMPARE(readFile(dst + QLatin1Char('/') + name), QByteArray("new"));
        QCOMPARE(names(dst), QStringList{name});
    }
    // Regression: the temp is removed on EVERY exit path, including an EXISTS from the final rename.
    void tempFileIsRemovedWhenFinalRenameReportsExists() {
        const QString src = dir(QStringLiteral("mv-src")), dst = dir(QStringLiteral("mv-dst"));
        QVERIFY(writeFile(src + QStringLiteral("/a.txt"), "new"));
        QVERIFY(writeFile(dst + QStringLiteral("/a.txt"), "old"));
        resetCounters(Ask::Overwrite);
        gMoveFaults = 0;
        gMoveExistsArmed = true;
        QVERIFY(waitFinished(Fm::FileOperation::copyFiles({P(src + QStringLiteral("/a.txt"))}, P(dst))));
        gMoveExistsArmed = false;
        QVERIFY2(gMoveFaults >= 1, "the rename fault hook was never reached");
        QCOMPARE(readFile(dst + QStringLiteral("/a.txt")), QByteArray("old"));
        QCOMPARE(names(dst), QStringList{QStringLiteral("a.txt")});  // no .fm-*.partial left behind
    }
    // Behaviour of GIO before the overlay ("como hoje"): a destination symlink is REPLACED by a regular
    // file; its target (also outside the folder / on another filesystem) is never written.
    void overwriteOverDestinationSymlinkReplacesTheLinkAndLeavesTheTargetIntact_data() {
        QTest::addColumn<bool>("otherFilesystem");
        QTest::addColumn<bool>("relativeLink");
        QTest::newRow("outside-folder-absolute") << false << false;
        QTest::newRow("outside-folder-relative") << false << true;
        QTest::newRow("other-filesystem") << true << false;
    }
    void overwriteOverDestinationSymlinkReplacesTheLinkAndLeavesTheTargetIntact() {
        QFETCH(bool, otherFilesystem);
        QFETCH(bool, relativeLink);
        QVERIFY2(!otherFilesystem || shm_.isValid(), "needs a second filesystem (/dev/shm)");
        const QString tag = QString::fromLatin1(QTest::currentDataTag());
        const QString src = dir(QStringLiteral("sl-src-") + tag), dst = dir(QStringLiteral("sl-dst-") + tag);
        const QString outside = otherFilesystem ? shm_.path() : dir(QStringLiteral("sl-outside-") + tag);
        const QString target = outside + QStringLiteral("/important.txt");
        const auto removeTarget = qScopeGuard([&] { QFile::remove(target); });  // /dev/shm is shared with other tests
        QVERIFY(writeFile(src + QStringLiteral("/link.txt"), "new"));
        QVERIFY(writeFile(target, "precious"));
        QVERIFY(QFile::setPermissions(target, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadGroup));
        const QString linkTo = relativeLink ? QDir(dst).relativeFilePath(target) : target;
        QVERIFY(QFile::link(linkTo, dst + QStringLiteral("/link.txt")));
        resetCounters(Ask::Overwrite);
        QVERIFY(waitFinished(Fm::FileOperation::copyFiles({P(src + QStringLiteral("/link.txt"))}, P(dst))));
        QCOMPARE(gAskCount, 1);
        QCOMPARE(gErrorCount, 0);
        QVERIFY2(readFile(target) == QByteArray("precious"), "the unrelated link TARGET was overwritten");
        struct stat st;
        QCOMPARE(::stat(QFile::encodeName(target).constData(), &st), 0);
        QCOMPARE(int(st.st_mode & 07777), 0640);
        const QFileInfo link(dst + QStringLiteral("/link.txt"));
        QVERIFY2(!link.isSymLink(), "the destination symlink was kept instead of replaced");
        QCOMPARE(readFile(dst + QStringLiteral("/link.txt")), QByteArray("new"));
        QCOMPARE(names(dst), QStringList{QStringLiteral("link.txt")});  // no temp left, no extra file
    }
    // A SYMLINK source is copied as a link; the temp file is then a symlink too, and chmod(2) on it
    // would change the mode of the link's (unrelated) target. The sentinel's mode must not move.
    void symlinkSourceOverwriteNeverChangesTheModeOfTheLinkTarget() {
        const QString src = dir(QStringLiteral("ss-src")), dst = dir(QStringLiteral("ss-dst")), sentinelDir = dir(QStringLiteral("ss-sentinel"));
        const QString sentinel = sentinelDir + QStringLiteral("/secret.txt");
        QVERIFY(writeFile(sentinel, "S"));
        QVERIFY(QFile::setPermissions(sentinel, QFileDevice::ReadOwner | QFileDevice::WriteOwner));  // 0600
        QVERIFY(QFile::link(sentinel, src + QStringLiteral("/link.txt")));
        QVERIFY(writeFile(dst + QStringLiteral("/link.txt"), "old"));
        QVERIFY(QFile::setPermissions(dst + QStringLiteral("/link.txt"), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadGroup |
                                                                           QFileDevice::ReadOther));  // 0644: what the buggy chmod would apply
        resetCounters(Ask::Overwrite);
        QVERIFY(waitFinished(Fm::FileOperation::copyFiles({P(src + QStringLiteral("/link.txt"))}, P(dst))));
        QCOMPARE(gAskCount, 1);
        struct stat st;
        QCOMPARE(::stat(QFile::encodeName(sentinel).constData(), &st), 0);
        QVERIFY2(int(st.st_mode & 07777) == 0600, qPrintable(QStringLiteral("sentinel mode changed to %1").arg(int(st.st_mode & 07777), 0, 8)));
        QCOMPARE(readFile(sentinel), QByteArray("S"));
        QCOMPARE(names(dst), QStringList{QStringLiteral("link.txt")});
        QVERIFY2(QFileInfo(dst + QStringLiteral("/link.txt")).isSymLink(), "a symlink source is copied as a symlink");
    }
    // The link kinds a destination symlink can have besides "points at a file": broken, and pointing at a
    // directory. The link is replaced by the new regular file; nothing it pointed at is created or touched.
    void overwriteOverBrokenDestinationSymlinkReplacesTheLinkAndCreatesNothingAtTheTarget() {
        const QString src = dir(QStringLiteral("bl-src")), dst = dir(QStringLiteral("bl-dst")), outside = dir(QStringLiteral("bl-outside"));
        const QString missing = outside + QStringLiteral("/does-not-exist.txt");
        QVERIFY(writeFile(src + QStringLiteral("/link.txt"), "new"));
        QVERIFY(QFile::link(missing, dst + QStringLiteral("/link.txt")));
        QVERIFY(QFileInfo(dst + QStringLiteral("/link.txt")).isSymLink());
        QVERIFY(!QFileInfo::exists(missing));
        resetCounters(Ask::Overwrite);
        QVERIFY(waitFinished(Fm::FileOperation::copyFiles({P(src + QStringLiteral("/link.txt"))}, P(dst))));
        QCOMPARE(gAskCount, 1);
        QCOMPARE(gErrorCount, 0);
        QVERIFY2(!QFileInfo(dst + QStringLiteral("/link.txt")).isSymLink(), "the broken link was kept instead of replaced");
        QCOMPARE(readFile(dst + QStringLiteral("/link.txt")), QByteArray("new"));
        QVERIFY2(!QFileInfo::exists(missing) && !QFileInfo(missing).isSymLink(), "something was created at the link's target");
        QCOMPARE(names(outside), QStringList{});
        QCOMPARE(names(dst), QStringList{QStringLiteral("link.txt")});
    }
    void overwriteOverDestinationSymlinkToDirectoryReplacesTheLinkAndLeavesTheDirectoryIntact() {
        const QString src = dir(QStringLiteral("dl-src")), dst = dir(QStringLiteral("dl-dst")), pointed = dir(QStringLiteral("dl-pointed"));
        QVERIFY(writeFile(pointed + QStringLiteral("/keep.txt"), "keep"));
        QVERIFY(writeFile(src + QStringLiteral("/link.txt"), "new"));
        QVERIFY(QFile::link(pointed, dst + QStringLiteral("/link.txt")));
        QVERIFY(QFileInfo(dst + QStringLiteral("/link.txt")).isDir());  // through the link
        resetCounters(Ask::Overwrite);
        QVERIFY(waitFinished(Fm::FileOperation::copyFiles({P(src + QStringLiteral("/link.txt"))}, P(dst))));
        QCOMPARE(gErrorCount, 0);
        QVERIFY2(!QFileInfo(dst + QStringLiteral("/link.txt")).isSymLink(), "the link was kept instead of replaced");
        QCOMPARE(readFile(dst + QStringLiteral("/link.txt")), QByteArray("new"));
        QVERIFY(QFileInfo(pointed).isDir());
        QCOMPARE(names(pointed), QStringList{QStringLiteral("keep.txt")});  // no file dropped INTO the directory
        QCOMPARE(readFile(pointed + QStringLiteral("/keep.txt")), QByteArray("keep"));
        QCOMPARE(names(dst), QStringList{QStringLiteral("link.txt")});
    }
    // A destination symlink in a READ-ONLY folder: no temp sibling is possible, the GIO fallback refuses (it
    // cannot unlink the link). Error reported; link, its target (file / directory / nothing) and no .fm-*.partial.
    void overwriteOverDestinationSymlinkInReadOnlyFolderReportsErrorAndChangesNothing_data() {
        QTest::addColumn<int>("kind");  // 0 file, 1 directory, 2 broken
        QTest::newRow("link-to-file") << 0;
        QTest::newRow("link-to-directory") << 1;
        QTest::newRow("broken-link") << 2;
    }
    void overwriteOverDestinationSymlinkInReadOnlyFolderReportsErrorAndChangesNothing() {
        if(geteuid() == 0) QSKIP("root ignores directory permissions");
        QFETCH(int, kind);
        const QString tag = QString::fromLatin1(QTest::currentDataTag());
        const QString src = dir(QStringLiteral("rl-src-") + tag), dst = dir(QStringLiteral("rl-dst-") + tag), outside = dir(QStringLiteral("rl-out-") + tag);
        const QString target = outside + QStringLiteral("/target");
        if(kind == 0) QVERIFY(writeFile(target, "precious"));
        if(kind == 1) { QVERIFY(QDir().mkpath(target)); QVERIFY(writeFile(target + QStringLiteral("/keep.txt"), "keep")); }
        QVERIFY(writeFile(src + QStringLiteral("/link.txt"), "new"));
        QVERIFY(QFile::link(target, dst + QStringLiteral("/link.txt")));
        QVERIFY(QFile::setPermissions(dst, QFileDevice::ReadOwner | QFileDevice::ExeOwner));
        const auto restore = qScopeGuard([&] { QFile::setPermissions(dst, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner); });
        resetCounters(Ask::Overwrite);
        const bool finished = waitFinished(Fm::FileOperation::copyFiles({P(src + QStringLiteral("/link.txt"))}, P(dst)));
        QVERIFY(finished);
        QVERIFY2(gErrorCount >= 1, "the refusal must be reported");
        const QFileInfo link(dst + QStringLiteral("/link.txt"));
        QVERIFY2(link.isSymLink() && link.symLinkTarget() == target, "the destination link was modified or replaced");
        if(kind == 0) QCOMPARE(readFile(target), QByteArray("precious"));
        if(kind == 1) { QCOMPARE(names(target), QStringList{QStringLiteral("keep.txt")}); QCOMPARE(readFile(target + QStringLiteral("/keep.txt")), QByteArray("keep")); }
        if(kind == 2) QCOMPARE(names(outside), QStringList{});
        QCOMPARE(names(dst), QStringList{QStringLiteral("link.txt")});  // no .fm-*.partial left
    }
    // The destination's rwx bits are reapplied but never its setuid/setgid/sticky bits.
    void overwriteDoesNotPropagateSpecialModeBitsOfTheOldFile() {
        const QString src = dir(QStringLiteral("sb-src")), dst = dir(QStringLiteral("sb-dst"));
        QVERIFY(writeFile(src + QStringLiteral("/f.bin"), "new"));
        QVERIFY(writeFile(dst + QStringLiteral("/f.bin"), "old"));
        QCOMPARE(::chmod(QFile::encodeName(dst + QStringLiteral("/f.bin")).constData(), 02755), 0);  // setgid + rwxr-xr-x
        resetCounters(Ask::Overwrite);
        QVERIFY(waitFinished(Fm::FileOperation::copyFiles({P(src + QStringLiteral("/f.bin"))}, P(dst))));
        QCOMPARE(readFile(dst + QStringLiteral("/f.bin")), QByteArray("new"));
        struct stat st;
        QCOMPARE(::stat(QFile::encodeName(dst + QStringLiteral("/f.bin")).constData(), &st), 0);
        QVERIFY2((st.st_mode & 07000) == 0, qPrintable(QStringLiteral("special bits propagated: %1").arg(int(st.st_mode & 07777), 0, 8)));
        QCOMPARE(int(st.st_mode & 0777), 0755);  // the destination's mode prevails over the source's
    }
    // Simple branch: an OVERWRITE retry whose probe says "absent" does not prove we created the file
    // (someone may have re-created it): a failed copy must NOT delete the destination then.
    void failedOverwriteRetryWithAbsentProbeDoesNotDeleteTheDestination() {
        const QString src = dir(QStringLiteral("sg-src")), dst = dir(QStringLiteral("sg-dst"));
        QVERIFY(writeFile(src + QStringLiteral("/big.bin"), pattern(3 << 20, 'N')));
        QVERIFY(writeFile(dst + QStringLiteral("/big.bin"), pattern(5000, 'O')));
        resetCounters(Ask::Overwrite);
        gQueryFaultName = "big.bin";
        gQueryFaults = 0;
        gQueryFaultNotFound = true;
        gQueryFaultMinAsks = 1;  // the first (conflict-detecting) probe is real; the post-prompt retry probe lies
        gQueryFaultArmed = true;
        {
            WriteLimit limit(1 << 20);
            QVERIFY(waitFinished(Fm::FileOperation::copyFiles({P(src + QStringLiteral("/big.bin"))}, P(dst))));
        }
        gQueryFaultArmed = false;
        gQueryFaultNotFound = false;
        gQueryFaultMinAsks = 0;
        QVERIFY2(gQueryFaults >= 1, "the fault hook was never reached: the test would be vacuous");
        QVERIFY2(QFile::exists(dst + QStringLiteral("/big.bin")), "destination deleted although the OVERWRITE flag was set");
    }
    // A read-only folder (no sibling temp possible) falls back to the previous GIO in-place replace,
    // which refuses (it cannot remove the old name): clear error, original content untouched, no temp.
    void overwriteInReadOnlyFolderReportsErrorAndKeepsOriginal() {
        if(geteuid() == 0) QSKIP("root ignores directory permissions");
        const QString src = dir(QStringLiteral("ro-src")), dst = dir(QStringLiteral("ro-dst"));
        QVERIFY(writeFile(src + QStringLiteral("/f.txt"), "new"));
        QVERIFY(writeFile(dst + QStringLiteral("/f.txt"), "old"));
        QVERIFY(QFile::setPermissions(dst, QFileDevice::ReadOwner | QFileDevice::ExeOwner));
        resetCounters(Ask::Overwrite);
        const bool finished = waitFinished(Fm::FileOperation::copyFiles({P(src + QStringLiteral("/f.txt"))}, P(dst)));
        QFile::setPermissions(dst, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        QVERIFY(finished);
        QVERIFY2(gErrorCount >= 1, "the refusal must be reported");
        QCOMPARE(readFile(dst + QStringLiteral("/f.txt")), QByteArray("old"));
        QCOMPARE(names(dst), QStringList{QStringLiteral("f.txt")});
    }

    // FO-02
    void copyWriteFailureLeavesNoPartialFile() {
        const QString src = dir(QStringLiteral("fo02-src")), dst = dir(QStringLiteral("fo02-dst"));
        QVERIFY(writeFile(src + QStringLiteral("/big.bin"), pattern(3 << 20, 'N')));
        QVERIFY(writeFile(dst + QStringLiteral("/other.txt"), "keep"));
        resetCounters(Ask::Cancel);
        {
            WriteLimit limit(1 << 20);
            QVERIFY(waitFinished(Fm::FileOperation::copyFiles({P(src + QStringLiteral("/big.bin"))}, P(dst))));
        }
        QVERIFY(gErrorCount >= 1);
        QCOMPARE(names(dst), QStringList{QStringLiteral("other.txt")});
        QCOMPARE(readFile(dst + QStringLiteral("/other.txt")), QByteArray("keep"));
    }
    void crossFilesystemMoveWriteFailureKeepsSourceAndLeavesNoPartial() {
        QVERIFY2(shm_.isValid(), "needs a second filesystem (/dev/shm)");
        const QString src = dir(QStringLiteral("fo02m-src"));
        const QByteArray data = pattern(3 << 20, 'M');
        QVERIFY(writeFile(src + QStringLiteral("/big.bin"), data));
        resetCounters(Ask::Cancel);
        {
            WriteLimit limit(1 << 20);
            QVERIFY(waitFinished(Fm::FileOperation::moveFiles({P(src + QStringLiteral("/big.bin"))}, P(shm_.path()))));
        }
        QVERIFY(gErrorCount >= 1);
        QCOMPARE(readFile(src + QStringLiteral("/big.bin")), data);
        QCOMPARE(names(shm_.path()), QStringList{});
    }
    void skippedConflictDoesNotRemoveExistingDestination() {
        const QString src = dir(QStringLiteral("fo02s-src")), dst = dir(QStringLiteral("fo02s-dst"));
        QVERIFY(writeFile(src + QStringLiteral("/a.txt"), "new"));
        QVERIFY(writeFile(dst + QStringLiteral("/a.txt"), "old"));
        resetCounters(Ask::Skip);
        QVERIFY(waitFinished(Fm::FileOperation::copyFiles({P(src + QStringLiteral("/a.txt"))}, P(dst))));
        QCOMPARE(gAskCount, 1);
        QCOMPARE(readFile(dst + QStringLiteral("/a.txt")), QByteArray("old"));
    }

    // FO-03: Enter on the "delete instead?" prompt must never delete.
    void unsupportedTrashPromptDefaultsToNo() {
        QVERIFY2(shm_.isValid(), "needs /dev/shm (no trash support there)");
        const QString victim = shm_.filePath(QStringLiteral("victim.txt"));
        QVERIFY(writeFile(victim, "precious"));
        resetCounters(Ask::Cancel);
        bool sawPrompt = false;
        auto* poll = new QTimer(this);
        poll->setInterval(50);
        connect(poll, &QTimer::timeout, this, [&] {
            if(auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
                sawPrompt = true;
                QTest::keyClick(box, Qt::Key_Return);  // what a user does on reflex
            }
        });
        poll->start();
        auto* op = Fm::FileOperation::trashFiles({P(victim)}, false);
        QVERIFY(waitFinished(op));
        QTest::qWait(500);  // let the prompt (modal, after finished) appear and be answered
        poll->stop();
        QVERIFY2(sawPrompt, "trash on /dev/shm should be unsupported and raise the prompt");
        QVERIFY2(QFile::exists(victim), "Enter on the prompt deleted the file permanently");
        QCOMPARE(readFile(victim), QByteArray("precious"));
    }

    // FO-04
    void copyPasteInSameFolderCreatesNumberedCopies() {
        const QString d = dir(QStringLiteral("fo04"));
        QVERIFY(writeFile(d + QStringLiteral("/Relatório.txt"), "R"));
        QVERIFY(writeFile(d + QStringLiteral("/noext"), "N"));
        QDir().mkpath(d + QStringLiteral("/Pasta/sub"));
        QVERIFY(writeFile(d + QStringLiteral("/Pasta/sub/x"), "X"));
        const Fm::FilePathList sources{P(d + QStringLiteral("/Relatório.txt")), P(d + QStringLiteral("/noext")),
                                       P(d + QStringLiteral("/Pasta"))};
        for(int round = 0; round < 2; ++round) {
            resetCounters(Ask::Cancel);
            QVERIFY(waitFinished(Fm::FileOperation::copyFiles(sources, P(d))));
            QCOMPARE(gAskCount, 0);
            QCOMPARE(gErrorCount, 0);
        }
        const QStringList expected{
            QStringLiteral("Pasta"), QStringLiteral("Pasta - Cópia"), QStringLiteral("Pasta - Cópia (2)"),
            QStringLiteral("Relatório - Cópia (2).txt"), QStringLiteral("Relatório - Cópia.txt"),
            QStringLiteral("Relatório.txt"), QStringLiteral("noext"), QStringLiteral("noext - Cópia"),
            QStringLiteral("noext - Cópia (2)")};
        QCOMPARE(names(d), expected);
        QCOMPARE(readFile(d + QStringLiteral("/Relatório - Cópia (2).txt")), QByteArray("R"));
        QCOMPARE(readFile(d + QStringLiteral("/Pasta - Cópia/sub/x")), QByteArray("X"));
        QCOMPARE(readFile(d + QStringLiteral("/Relatório.txt")), QByteArray("R"));
    }
    void cutPasteAndMoveInSameFolderAreSilentNoOps() {
        const QString d = dir(QStringLiteral("fo04m"));
        QVERIFY(writeFile(d + QStringLiteral("/a.txt"), "A"));
        QDir().mkpath(d + QStringLiteral("/dir"));
        resetCounters(Ask::Cancel);
        QVERIFY(waitFinished(Fm::FileOperation::moveFiles({P(d + QStringLiteral("/a.txt")), P(d + QStringLiteral("/dir"))}, P(d))));
        QCOMPARE(gAskCount, 0);
        QCOMPARE(gErrorCount, 0);
        QCOMPARE(names(d), (QStringList{QStringLiteral("a.txt"), QStringLiteral("dir")}));
        QCOMPARE(readFile(d + QStringLiteral("/a.txt")), QByteArray("A"));
    }

    // FO-07
    void copyAndMoveOfVanishedSourceReportErrors() {
        const QString d = dir(QStringLiteral("fo07"));
        const QString gone = d + QStringLiteral("/gone.txt");
        const QString dst = dir(QStringLiteral("fo07-dst"));
        for(const bool move : {false, true}) {
            resetCounters(Ask::Cancel);
            auto* op = move ? Fm::FileOperation::moveFiles({P(gone)}, P(dst)) : Fm::FileOperation::copyFiles({P(gone)}, P(dst));
            QVERIFY(waitFinished(op));
            // The TotalSizeJob already emits a MILD error (ignored by the real dialog): only the new
            // MODERATE error from processPath is what the user sees.
            QVERIFY2(gErrorCount >= 1, move ? "move of vanished source was silent" : "copy of vanished source was silent");
            QVERIFY2(gErrorMessages.join(QLatin1Char('|')).contains(QStringLiteral("gone.txt")),
                     qPrintable(gErrorMessages.join(QLatin1Char('|'))));
        }
        QCOMPARE(names(dst), QStringList{});
    }
    void failedCutPasteKeepsClipboardAndSuccessfulOneClearsIt() {
        const QString d = dir(QStringLiteral("fo07c"));
        const QString dst = dir(QStringLiteral("fo07c-dst"));
        QVERIFY(writeFile(d + QStringLiteral("/real.txt"), "R"));
        // 1. source vanished -> operation fails -> clipboard must still offer the cut.
        Fm::cutFilesToClipboard({P(d + QStringLiteral("/gone.txt"))});
        resetCounters(Ask::Cancel);
        Fm::pasteFilesFromClipboard(P(dst));
        QTRY_VERIFY_WITH_TIMEOUT(gErrorCount >= 1, 10000);
        QVERIFY(gErrorMessages.join(QLatin1Char('|')).contains(QStringLiteral("gone.txt")));
        QTest::qWait(300);
        QVERIFY2(clipboardHasFiles(), "clipboard was cleared although the move failed");
        // 2. real file -> success -> clipboard cleared, file moved.
        Fm::cutFilesToClipboard({P(d + QStringLiteral("/real.txt"))});
        resetCounters(Ask::Cancel);
        Fm::pasteFilesFromClipboard(P(dst));
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(dst + QStringLiteral("/real.txt")), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(!clipboardHasFiles(), 5000);
        QVERIFY(!QFile::exists(d + QStringLiteral("/real.txt")));
    }

    // FO-06
    void restoreFromTrashWithMissingOriginalFolderReportsError() {
        const QString d = dir(QStringLiteral("fo06"));
        const QString folder = d + QStringLiteral("/gone-folder");
        QDir().mkpath(folder);
        QVERIFY(writeFile(folder + QStringLiteral("/doc.txt"), "D"));
        resetCounters(Ask::Cancel);
        QVERIFY(waitFinished(Fm::FileOperation::trashFiles({P(folder + QStringLiteral("/doc.txt"))}, false)));
        QVERIFY2(gErrorCount == 0, qPrintable(gErrorMessages.join(QLatin1Char('|'))));
        QVERIFY(!QFile::exists(folder + QStringLiteral("/doc.txt")));
        // B3: gvfsd-trash must be using the TEMPORARY trash, never the user's real one.
        const QString tempTrashed = QString::fromLocal8Bit(qgetenv("XDG_DATA_HOME")) + QStringLiteral("/Trash/files/doc.txt");
        QVERIFY2(QFile::exists(tempTrashed), qPrintable(tempTrashed + QStringLiteral(" missing: gvfsd-trash is not using the isolated HOME")));
        QVERIFY(QDir(folder).removeRecursively());
        QVERIFY(waitFinished(Fm::FileOperation::unTrashFiles({U(QStringLiteral("trash:///doc.txt"))})));
        QVERIFY2(gErrorCount >= 1, "restore into a missing folder reported success silently");
        QVERIFY2(gErrorMessages.join(QLatin1Char('|')).contains(QString::fromUtf8("pasta de destino")),
                 qPrintable(gErrorMessages.join(QLatin1Char('|'))));
        QVERIFY(!QFile::exists(folder + QStringLiteral("/doc.txt")));
    }
};

int main(int argc, char** argv) {
    // The isolation is created by run-fileops-safety.sh BEFORE dbus-run-session (so gvfsd-trash inherits
    // it). Setting HOME here would be too late for the daemons: refuse to run if it is not in place or
    // if HOME is the user's real home.
    const QByteArray iso = qgetenv("FILEOPS_SAFETY_HOME");
    const struct passwd* pw = getpwuid(getuid());
    // The bus must be the one dbus-run-session started for THIS run (wrapper: FILEOPS_SAFETY_BUS=1 injected
    // by `dbus-run-session -- env ...`, parent process = dbus-run-session, address != the inherited bus).
    const QByteArray bus = qgetenv("DBUS_SESSION_BUS_ADDRESS");
    bool privateBus = qgetenv("FILEOPS_SAFETY_BUS") == "1" && !bus.isEmpty() && bus != qgetenv("FILEOPS_SAFETY_INHERITED_BUS");
    if(privateBus) {
        char comm[64] = {};
        if(FILE* f = std::fopen(QByteArray(QByteArrayLiteral("/proc/") % QByteArray::number(qlonglong(getppid())) % QByteArrayLiteral("/comm")).constData(), "r")) {
            if(!std::fgets(comm, sizeof comm, f)) comm[0] = 0;
            std::fclose(f);
        }
        privateBus = std::strncmp(comm, "dbus-run-sessio", 15) == 0;  // comm is truncated to 15 chars
    }
    const bool ok = privateBus && !iso.isEmpty() && qgetenv("HOME") == iso && qgetenv("XDG_DATA_HOME").startsWith(QByteArray(iso).append('/')) &&
                    qgetenv("XDG_RUNTIME_DIR").startsWith(QByteArray(iso).append('/')) && (!pw || iso != QByteArray(pw->pw_dir)) &&
                    iso.contains("fileops-safety-home-");
    if(!ok) {
        std::fprintf(stderr, "tst_fileops_safety: refusing to run outside run-fileops-safety.sh (HOME and the D-Bus session must be the private ones)\n");
        return 2;
    }
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    FileOpsSafetyTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_fileops_safety.moc"
