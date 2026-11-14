// SPDX-License-Identifier: GPL-2.0-or-later
// Data-loss / file-operation regressions (audit FO-01..FO-07) against the REAL private libfm-qt overlay.
// Only the dialog is scripted (answers to conflict/error prompts); jobs, conflict logic, clipboard and
// trash are the real ones. All paths live inside test-owned temporary directories and HOME/XDG_* are
// redirected before any GIO call, so neither the real trash nor the user's configuration is touched.
#include <QtTest>
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QMimeData>
#include <QMessageBox>
#include <QPointer>
#include <QTemporaryDir>
#include <QTimer>
#include <csignal>
#include <sys/resource.h>
#include <glib.h>
#include <fileoperation.h>
#include <fileoperationdialog.h>
#include <utilities.h>
#include "core/filepath.h"
#include "core/fileinfo.h"

namespace {
enum class Ask { Cancel, Overwrite, Skip };
Ask gAsk = Ask::Cancel;
int gAskCount = 0;
int gErrorCount = 0;
QStringList gErrorMessages;

class ScriptDialog final : public Fm::FileOperationDialog {
public:
    explicit ScriptDialog(Fm::FileOperation* operation) : FileOperationDialog(operation) {}
    Fm::FileOperationJob::FileExistsAction askRename(const Fm::FileInfo&, const Fm::FileInfo&, Fm::FilePath&) override {
        ++gAskCount;
        switch(gAsk) {
        case Ask::Overwrite: return Fm::FileOperationJob::OVERWRITE;
        case Ask::Skip: return Fm::FileOperationJob::SKIP;
        default: return Fm::FileOperationJob::CANCEL;
        }
    }
    Fm::Job::ErrorAction error(GError* err, Fm::Job::ErrorSeverity) override {
        ++gErrorCount;
        gErrorMessages << QString::fromUtf8(err->message);
        return Fm::Job::ErrorAction::CONTINUE;
    }
};

Fm::FileOperationDialog* scriptDialogFactory(Fm::FileOperation* operation) {
    return new ScriptDialog(operation);
}

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
    return QDir(dir).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden, QDir::Name);
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
    gAskCount = gErrorCount = 0;
    gErrorMessages.clear();
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
        resetCounters(Ask::Overwrite);
        QVERIFY(waitFinished(Fm::FileOperation::copyFiles({P(src + QStringLiteral("/f.bin"))}, P(dst))));
        QCOMPARE(gAskCount, 1);
        QCOMPARE(gErrorCount, 0);
        QCOMPARE(readFile(dst + QStringLiteral("/f.bin")), fresh);
        QVERIFY(QFile::permissions(dst + QStringLiteral("/f.bin")) & QFileDevice::ExeOwner);
        QCOMPARE(names(dst), QStringList{QStringLiteral("f.bin")});
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
            QVERIFY2(gErrorCount >= 1, move ? "move of vanished source was silent" : "copy of vanished source was silent");
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
        QVERIFY(QDir(folder).removeRecursively());
        QVERIFY(waitFinished(Fm::FileOperation::unTrashFiles({U(QStringLiteral("trash:///doc.txt"))})));
        QVERIFY2(gErrorCount >= 1, "restore into a missing folder reported success silently");
        QVERIFY(!QFile::exists(folder + QStringLiteral("/doc.txt")));
    }
};

int main(int argc, char** argv) {
    // Redirect everything BEFORE GLib caches its directories and before QApplication exists.
    QTemporaryDir home(QDir::tempPath() + QStringLiteral("/fileops-safety-home-XXXXXX"));
    if(!home.isValid()) return 2;
    const QByteArray root = QFile::encodeName(home.path());
    qputenv("HOME", root);
    qputenv("XDG_DATA_HOME", root + "/data");
    qputenv("XDG_CONFIG_HOME", root + "/config");
    qputenv("XDG_CACHE_HOME", root + "/cache");
    qputenv("XDG_STATE_HOME", root + "/state");
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QDir().mkpath(home.path() + QStringLiteral("/data"));
    QApplication app(argc, argv);
    FileOpsSafetyTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_fileops_safety.moc"
