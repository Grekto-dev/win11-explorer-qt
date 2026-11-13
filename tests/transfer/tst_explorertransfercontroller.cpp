#include <QtTest>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStorageInfo>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QDeadlineTimer>
#include <QScopeGuard>
#include <atomic>
#include <memory>
#include <QDateTime>
#include <QApplication>
#include <QMessageBox>
#include <QTimer>
#include <sys/stat.h>
#include <thread>
#include <future>
#include "../../pcmanfm/explorertransfercontroller.h"
#include "../../pcmanfm/explorertransferdialog.h"
#include "core/filetransferjob.h"
#include "core/job.h"
#include "core/jobpausegate_p.h"
#include <fileoperation.h>
#include <fileoperationdialog.h>

namespace {
int dialogFactoryCalls = 0;
int conflictDialogCalls = 0;
int errorDialogCalls = 0;
QPointer<ExplorerTransferDialog> capturedTransferDialog;

Fm::FileOperationDialog* captureExplorerTransferDialog(Fm::FileOperation* operation) {
    capturedTransferDialog = qobject_cast<ExplorerTransferDialog*>(createExplorerTransferDialog(operation));
    return capturedTransferDialog;
}

class ProbeTransferDialog final : public Fm::FileOperationDialog {
public:
    explicit ProbeTransferDialog(Fm::FileOperation* operation) : FileOperationDialog(operation) {}
    Fm::FileOperationJob::FileExistsAction askRename(const Fm::FileInfo&, const Fm::FileInfo&, Fm::FilePath&) override {
        ++conflictDialogCalls;
        return Fm::FileOperationJob::SKIP;
    }
    Fm::Job::ErrorAction error(GError*, Fm::Job::ErrorSeverity) override {
        ++errorDialogCalls;
        return Fm::Job::ErrorAction::CONTINUE;
    }
};

Fm::FileOperationDialog* createProbeDialog(Fm::FileOperation* operation) {
    ++dialogFactoryCalls;
    return new ProbeTransferDialog(operation);
}
}

class ExplorerTransferControllerTest final : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void backendControlsCannotReopenCompletion();
    void cancellationCannotPassPredicateWaitGap();
    void completionPrecedesDestructiveFinishedHandlers();
    void cancellationBeforeGuiDispatchIsInTerminalSnapshot_data();
    void cancellationBeforeGuiDispatchIsInTerminalSnapshot();
    void cancellationForwardsToLiveLibfmJob();
    void observerLifecycleTracksRealJobSignals();
    void lateAttachStartsObservingAnAlreadyRunningJob();
    void observerDetachesWhenBorrowedJobIsDestroyedEarly();
    void destroyingObserverDoesNotDestroyBorrowedJob();
    void destroyingObserverDuringIoLeavesJobRunning();
    void optInDialogFactoryKeepsConflictRoutingOnRealFileOperation();
    void optInDialogFactoryKeepsErrorRoutingOnRealFileOperation();
    void defaultDialogFactoryReceivesPointerBasedErrorWithoutTestRegistration();
    void transferDialogFactoryBuildsCollapsedRealProgressUi();
    void liveOperationPauseButtonWaitsForBackendAck();
    void preservesExplicitConflictResponseOnLiveJob();
    void reportsRealTwoGiBCopyProgressAndIntegrity();
    void cooperativePauseCapabilityIsNotAdvertisedByUnsupportedJobs();
    void externalCancellationNotifiesControllerExactlyOnce();
    void directCancellableCancellationWakesPausedCopy();
};

void ExplorerTransferControllerTest::backendControlsCannotReopenCompletion() {
    class TerminalJob final : public Fm::FileOperationJob {
    public:
        bool supportsCooperativePause() const override { return true; }
        void exec() override { setTotalAmount(7, 1); setFinishedAmount(7, 1); }
    } job;
    job.setAutoDelete(false);
    ExplorerTransferController controller(&job);
    QVERIFY(controller.markStarted());
    std::thread worker([&] { job.run(); });
    worker.join();
    const bool accepted = job.pause();
    job.resume();
    job.cancel();
    QCOMPARE(controller.snapshot().state, ExplorerTransferController::State::Finished);
    QVERIFY(!accepted);
    QVERIFY(!job.pauseRequested());
    QVERIFY(!job.isCancelled());
    QVERIFY(!controller.snapshot().cancellationRequested);
}

void ExplorerTransferControllerTest::cancellationCannotPassPredicateWaitGap() {
    Fm::JobPauseGate gate;
    gate.requested = true;
    std::promise<void> inGap, cancelStarted, cancelDone, workerDone;
    auto cancellationDone = cancelDone.get_future();
    auto completed = workerDone.get_future();
    std::atomic_bool cancelledBeforeWait{false};
    std::thread worker([&] {
        gate.wait([&] {
            inGap.set_value();
            cancelStarted.get_future().wait();
            // Deadline is only for observing blocked cancellation. The old
            // implementation is forced to finish its notify BEFORE wait().
            cancelledBeforeWait = cancellationDone.wait_for(std::chrono::milliseconds(200)) == std::future_status::ready;
        });
        workerDone.set_value();
    });
    inGap.get_future().wait();
    std::thread canceller([&] {
        cancelStarted.set_value();
        gate.cancel();
        cancelDone.set_value();
    });
    const bool woke = completed.wait_for(std::chrono::seconds(2)) == std::future_status::ready;
    if(!woke) gate.resume(); // deterministic rescue: never leave a hung test thread
    worker.join();
    canceller.join();
    QVERIFY2(!cancelledBeforeWait, "cancel publication escaped the predicate mutex");
    QVERIFY2(woke, "cancellation notification was lost in the predicate/wait gap");
}

// Connect the destructive owner first, as FileOperation does. Hold the
// worker inside that handler while the GUI reads terminal data and deletes
// its child observer. No queued observer notification has run yet.
void ExplorerTransferControllerTest::completionPrecedesDestructiveFinishedHandlers() {
    class CountingJob final : public Fm::FileOperationJob {
        void exec() override { setTotalAmount(29, 1); setFinishedAmount(29, 1); }
    } job;
    job.setAutoDelete(false);
    std::promise<void> entered, release;
    auto released = release.get_future();
    QObject owner;
    QObject::connect(&job, &Fm::Job::finished, &owner, [&] {
        entered.set_value();
        released.wait();
    }, Qt::DirectConnection);
    auto* controller = new ExplorerTransferController(&job);
    QVERIFY(controller->markStarted());
    std::thread worker([&] { job.run(); });
    entered.get_future().wait();
    const auto terminal = controller->snapshot();
    controller->cancel();
    const bool pauseAccepted = controller->pause();
    const bool resumeAccepted = controller->resume();
    const auto afterControls = controller->snapshot();
    delete controller;
    release.set_value();
    worker.join();
    QCOMPARE(terminal.state, ExplorerTransferController::State::Finished);
    QCOMPARE(terminal.finishedBytes, quint64(29));
    QCOMPARE(afterControls.state, ExplorerTransferController::State::Finished);
    QVERIFY(!afterControls.cancellationRequested);
    QVERIFY(!pauseAccepted);
    QVERIFY(!resumeAccepted);
    QVERIFY(!job.isCancelled());
}

void ExplorerTransferControllerTest::cancellationBeforeGuiDispatchIsInTerminalSnapshot_data() {
    QTest::addColumn<bool>("directCancellable");
    QTest::newRow("Job-cancel") << false;
    QTest::newRow("external-GCancellable") << true;
}

// Real Job::run(), no event loop dispatch until the worker has returned.
void ExplorerTransferControllerTest::cancellationBeforeGuiDispatchIsInTerminalSnapshot() {
    QFETCH(bool, directCancellable);
    class CancelJob final : public Fm::FileOperationJob {
    public:
        bool direct = false;
        void exec() override {
            setTotalAmount(17, 1);
            if(direct) g_cancellable_cancel(cancellable().get());
            else cancel();
        }
    } job;
    job.direct = directCancellable;
    job.setAutoDelete(false);
    ExplorerTransferController controller(&job);
    QSignalSpy notices(&controller, &ExplorerTransferController::cancellationRequested);
    QVERIFY(controller.markStarted());
    std::thread worker([&] { job.run(); });
    worker.join();
    QCOMPARE(notices.count(), 0);
    const auto terminal = controller.snapshot();
    QCOMPARE(terminal.state, ExplorerTransferController::State::Finished);
    QVERIFY(terminal.cancellationRequested);
    QVERIFY(terminal.cancelled);
    QCOMPARE(terminal.totalBytes, quint64(17));
    QCoreApplication::processEvents();
    QCOMPARE(notices.count(), 1);
}

void ExplorerTransferControllerTest::observerLifecycleTracksRealJobSignals() {
    ExplorerTransferController missingJob(nullptr);
    QCOMPARE(missingJob.snapshot().state, ExplorerTransferController::State::Detached);
    QVERIFY(!missingJob.snapshot().running);
    QVERIFY(!missingJob.markStarted());

    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString source = root.filePath(QStringLiteral("lifecycle.txt"));
    QFile file(source);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write("ok"), qint64(2));
    file.close();
    const QString destination = root.filePath(QStringLiteral("dest"));
    QVERIFY(QDir().mkpath(destination));
    Fm::FilePathList sources{Fm::FilePath::fromLocalPath(source.toLocal8Bit().constData())};
    auto* job = new Fm::FileTransferJob(sources,
        Fm::FilePath::fromLocalPath(destination.toLocal8Bit().constData()), Fm::FileTransferJob::Mode::COPY);
    ExplorerTransferController controller(job);
    QSignalSpy telemetry(&controller, &ExplorerTransferController::telemetryUpdated);
    QCOMPARE(controller.snapshot().state, ExplorerTransferController::State::Idle);
    QVERIFY(!controller.snapshot().running);
    QTest::qWait(150);
    QCOMPARE(telemetry.count(), 0);
    QVERIFY(controller.markStarted());
    QCOMPARE(controller.snapshot().state, ExplorerTransferController::State::Preparing);
    QVERIFY(controller.snapshot().running);
    QTRY_VERIFY_WITH_TIMEOUT(telemetry.count() > 0, 2000);
    QSignalSpy finished(&controller, &ExplorerTransferController::finished);
    job->runAsync();
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);
    const auto final = controller.snapshot();
    QCOMPARE(final.state, ExplorerTransferController::State::Finished);
    QVERIFY(!final.running);
    QVERIFY(final.hasTotals);
    QCOMPARE(final.totalBytes, quint64(2));
    const int terminalSamples = telemetry.count();
    QTest::qWait(250);
    QCOMPARE(telemetry.count(), terminalSamples);
}

void ExplorerTransferControllerTest::lateAttachStartsObservingAnAlreadyRunningJob() {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString source = root.filePath(QStringLiteral("late-attach.bin"));
    QFile file(source);
    QVERIFY(file.open(QIODevice::WriteOnly));
    const QByteArray block(1024 * 1024, 'L');
    for(int i = 0; i < 64; ++i) QCOMPARE(file.write(block), qint64(block.size()));
    file.close();
    const QString destination = root.filePath(QStringLiteral("dest"));
    QVERIFY(QDir().mkpath(destination));
    Fm::FilePathList sources{Fm::FilePath::fromLocalPath(source.toLocal8Bit().constData())};
    Fm::FileOperation operation(Fm::FileOperation::Copy, sources);
    operation.setAutoDestroy(false);
    operation.setDestination(Fm::FilePath::fromLocalPath(destination.toLocal8Bit().constData()));
    QSignalSpy operationFinished(&operation, &Fm::FileOperation::finished);
    QVERIFY(operation.run());
    QVERIFY(operation.isRunning());

    ExplorerTransferController controller(operation.job());
    QVERIFY(controller.attachLate());
    QCOMPARE(controller.snapshot().state, ExplorerTransferController::State::Running);
    QVERIFY(controller.snapshot().running);
    QSignalSpy telemetry(&controller, &ExplorerTransferController::telemetryUpdated);
    QTRY_VERIFY_WITH_TIMEOUT(telemetry.count() > 0, 2000);
    QTRY_COMPARE_WITH_TIMEOUT(operationFinished.count(), 1, 10000);
}

void ExplorerTransferControllerTest::observerDetachesWhenBorrowedJobIsDestroyedEarly() {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    Fm::FilePathList sources;
    auto* job = new Fm::FileTransferJob(sources, Fm::FilePath::fromLocalPath(root.path().toLocal8Bit().constData()), Fm::FileTransferJob::Mode::COPY);
    ExplorerTransferController controller(job);
    delete job;
    QTRY_COMPARE_WITH_TIMEOUT(controller.snapshot().state, ExplorerTransferController::State::Detached, 1000);
    QVERIFY(!controller.snapshot().running);
}

void ExplorerTransferControllerTest::destroyingObserverDoesNotDestroyBorrowedJob() {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    Fm::FilePathList sources;
    auto* job = new Fm::FileTransferJob(sources, Fm::FilePath::fromLocalPath(root.path().toLocal8Bit().constData()), Fm::FileTransferJob::Mode::COPY);
    QPointer<Fm::FileOperationJob> borrowed(job);
    {
        ExplorerTransferController controller(job);
        QCOMPARE(controller.snapshot().state, ExplorerTransferController::State::Idle);
    }
    QVERIFY(borrowed);
    delete job;
    QVERIFY(borrowed.isNull());
}

void ExplorerTransferControllerTest::destroyingObserverDuringIoLeavesJobRunning() {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString source = root.filePath(QStringLiteral("observer-destroy.bin"));
    QFile file(source);
    QVERIFY(file.open(QIODevice::WriteOnly));
    const QByteArray block(1024 * 1024, 'X');
    for(int i = 0; i < 8; ++i)
        QCOMPARE(file.write(block), qint64(block.size()));
    file.close();
    const QString destination = root.filePath(QStringLiteral("dest"));
    QVERIFY(QDir().mkpath(destination));
    Fm::FilePathList sources{Fm::FilePath::fromLocalPath(source.toLocal8Bit().constData())};
    auto* job = new Fm::FileTransferJob(sources,
        Fm::FilePath::fromLocalPath(destination.toLocal8Bit().constData()), Fm::FileTransferJob::Mode::COPY);
    job->setAutoDelete(false);
    QPointer<Fm::FileOperationJob> borrowed(job);
    auto* controller = new ExplorerTransferController(job);
    QVERIFY(controller->markStarted());
    QSignalSpy finished(job, &Fm::Job::finished);
    job->runAsync();
    delete controller;
    QVERIFY(borrowed);
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);
    QVERIFY(QFileInfo(QDir(destination).filePath(QStringLiteral("observer-destroy.bin"))).exists());
    delete job;
}

void ExplorerTransferControllerTest::optInDialogFactoryKeepsConflictRoutingOnRealFileOperation() {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString source = root.filePath(QStringLiteral("conflict.txt"));
    QFile src(source);
    QVERIFY(src.open(QIODevice::WriteOnly));
    QCOMPARE(src.write("new"), qint64(3));
    src.close();
    const QString destinationDir = root.filePath(QStringLiteral("dest"));
    QVERIFY(QDir().mkpath(destinationDir));
    QFile dest(QDir(destinationDir).filePath(QStringLiteral("conflict.txt")));
    QVERIFY(dest.open(QIODevice::WriteOnly));
    QCOMPARE(dest.write("old"), qint64(3));
    dest.close();

    dialogFactoryCalls = conflictDialogCalls = errorDialogCalls = 0;
    Fm::FileOperation::setDialogFactory(&createProbeDialog);
    Fm::FilePathList sources{Fm::FilePath::fromLocalPath(source.toLocal8Bit().constData())};
    Fm::FileOperation operation(Fm::FileOperation::Copy, sources);
    operation.setAutoDestroy(false);
    operation.setDestination(Fm::FilePath::fromLocalPath(destinationDir.toLocal8Bit().constData()));
    QSignalSpy finished(&operation, &Fm::FileOperation::finished);
    QVERIFY(operation.run());
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);
    Fm::FileOperation::setDialogFactory(nullptr);

    QCOMPARE(dialogFactoryCalls, 1);
    QCOMPARE(conflictDialogCalls, 1);
    QCOMPARE(errorDialogCalls, 0);
    QVERIFY(src.exists());
    QVERIFY(dest.open(QIODevice::ReadOnly));
    QCOMPARE(dest.readAll(), QByteArray("old"));
}

void ExplorerTransferControllerTest::optInDialogFactoryKeepsErrorRoutingOnRealFileOperation() {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString sourcePath = root.filePath(QStringLiteral("existing-source.txt"));
    QFile sourceFile(sourcePath);
    QVERIFY(sourceFile.open(QIODevice::WriteOnly));
    QCOMPARE(sourceFile.write("data"), qint64(4));
    sourceFile.close();
    const QString invalidDestination = root.filePath(QStringLiteral("not-a-directory"));
    QFile destinationFile(invalidDestination);
    QVERIFY(destinationFile.open(QIODevice::WriteOnly));
    QCOMPARE(destinationFile.write("destination"), qint64(11));
    destinationFile.close();
    Fm::FilePathList sources{Fm::FilePath::fromLocalPath(sourcePath.toLocal8Bit().constData())};
    dialogFactoryCalls = conflictDialogCalls = errorDialogCalls = 0;
    Fm::FileOperation::setDialogFactory(&createProbeDialog);
    Fm::FileOperation operation(Fm::FileOperation::Copy, sources);
    operation.setAutoDestroy(false);
    operation.setDestination(Fm::FilePath::fromLocalPath(invalidDestination.toLocal8Bit().constData()));
    QSignalSpy finished(&operation, &Fm::FileOperation::finished);
    QVERIFY(operation.run());
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);
    Fm::FileOperation::setDialogFactory(nullptr);

    QCOMPARE(dialogFactoryCalls, 1);
    QCOMPARE(conflictDialogCalls, 0);
    QCOMPARE(errorDialogCalls, 1);
}

void ExplorerTransferControllerTest::transferDialogFactoryBuildsCollapsedRealProgressUi() {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString source = root.filePath(QStringLiteral("visible-source.bin"));
    QFile file(source);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write("live-bytes"), qint64(10));
    file.close();
    const QString destination = root.filePath(QStringLiteral("destination"));
    QVERIFY(QDir().mkpath(destination));
    Fm::FilePathList sources{Fm::FilePath::fromLocalPath(source.toLocal8Bit().constData())};
    Fm::FileOperation operation(Fm::FileOperation::Copy, sources);
    auto* dialog = qobject_cast<ExplorerTransferDialog*>(createExplorerTransferDialog(&operation));
    QVERIFY(dialog);
    dialog->setSourceFiles(sources);
    dialog->setDestPath(Fm::FilePath::fromLocalPath(destination.toLocal8Bit().constData()));
    dialog->setFilesProcessed(0, 1);
    dialog->setCurFile(source);
    dialog->setDataTransferred(5, 10);
    dialog->setPercent(50);
    dialog->setPrepared();
    auto* progress = dialog->findChild<QProgressBar*>(QStringLiteral("transferProgress"));
    QVERIFY(progress);
    QCOMPARE(progress->value(), 50);
    auto* progressText = dialog->findChild<QLabel*>(QStringLiteral("transferPercent"));
    QVERIFY(progressText);
    QCOMPARE(progressText->text(), QStringLiteral("50% concluído"));
    auto* summary = dialog->findChild<QLabel*>(QStringLiteral("transferSummary"));
    QVERIFY(summary);
    QVERIFY(summary->text().contains(QFileInfo(root.path()).fileName()));
    QVERIFY(!summary->text().contains(root.path()));
    QVERIFY(dialog->findChild<QPushButton*>(QStringLiteral("transferDetailsButton")));
    QVERIFY(dialog->findChild<QPushButton*>(QStringLiteral("transferCancelButton")));
    QVERIFY(dialog->findChild<QPushButton*>(QStringLiteral("pauseButton")));
    QVERIFY(!dialog->findChild<QPushButton*>(QStringLiteral("pauseButton"))->isVisible());
    QVERIFY(dialog->minimumWidth() >= 440);
    dialog->show();
    QCoreApplication::processEvents();
    const QString capture = qEnvironmentVariable("EXPLORER_TRANSFER_UI_SCREENSHOT");
    if(!capture.isEmpty()) QVERIFY(dialog->grab().save(capture));
    delete dialog;

    Fm::FileOperation::setDialogFactory(&createExplorerTransferDialog);
    Fm::FileOperation liveOperation(Fm::FileOperation::Copy, sources);
    liveOperation.setAutoDestroy(false);
    liveOperation.setDestination(Fm::FilePath::fromLocalPath(destination.toLocal8Bit().constData()));
    QSignalSpy operationFinished(&liveOperation, &Fm::FileOperation::finished);
    QVERIFY(liveOperation.run());
    QTRY_COMPARE_WITH_TIMEOUT(operationFinished.count(), 1, 10000);
    Fm::FileOperation::setDialogFactory(nullptr);
    QFile copied(QDir(destination).filePath(QStringLiteral("visible-source.bin")));
    QVERIFY(copied.open(QIODevice::ReadOnly));
    QCOMPARE(copied.readAll(), QByteArray("live-bytes"));
}

void ExplorerTransferControllerTest::liveOperationPauseButtonWaitsForBackendAck() {
    constexpr quint64 fixtureBytes = 4ULL * 1024 * 1024 * 1024;
    constexpr qsizetype chunkBytes = 4 * 1024 * 1024;
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QStorageInfo storage(root.path());
    QVERIFY2(storage.isValid() && storage.bytesAvailable() >= qint64(3 * fixtureBytes),
             "live pause UI test requires >= 12 GiB free for a materialized 4 GiB source, destination, and safety margin");
    const QString source = root.filePath(QStringLiteral("ui-live-source-4GiB.bin"));
    const QString destination = root.filePath(QStringLiteral("destination"));
    QVERIFY(QDir().mkpath(destination));
    QFile file(source);
    QVERIFY(file.open(QIODevice::WriteOnly));
    const QByteArray block(chunkBytes, 'U');
    for(quint64 written = 0; written < fixtureBytes; written += chunkBytes)
        QCOMPARE(file.write(block), qint64(block.size()));
    QVERIFY(file.flush());
    file.close();

    const Fm::FilePathList sources{Fm::FilePath::fromLocalPath(source.toLocal8Bit().constData())};
    Fm::FileOperation::setDialogFactory(&captureExplorerTransferDialog);
    capturedTransferDialog = nullptr;
    Fm::FileOperation operation(Fm::FileOperation::Copy, sources);
    operation.setAutoDestroy(false);
    operation.setDestination(Fm::FilePath::fromLocalPath(destination.toLocal8Bit().constData()));
    QSignalSpy finished(&operation, &Fm::FileOperation::finished);
    QPointer<Fm::FileOperationJob> job(operation.job());

    // Rendezvous in preparation, not in a replacement copy implementation.
    // FileOperation's earlier blocking prepared handler still runs normally.
    // Keep the real worker here until its delayed dialog has requested pause:
    // otherwise a fast copy may finish before the 1 s dialog factory timer.
    std::promise<void> releaseWorker;
    const auto released = releaseWorker.get_future().share();
    const auto preparationReached = std::make_shared<std::atomic_bool>(false);
    const auto rendezvousExpired = std::make_shared<std::atomic_bool>(false);
    connect(job, &Fm::FileOperationJob::preparedToRun, &operation,
        [released, preparationReached, rendezvousExpired] {
            *preparationReached = true;
            *rendezvousExpired = released.wait_for(std::chrono::seconds(15)) != std::future_status::ready;
        }, Qt::DirectConnection);
    bool workerReleased = false;
    const auto release = [&] {
        if(!workerReleased) {
            workerReleased = true;
            releaseWorker.set_value();
        }
    };

    QElapsedTimer pauseClock;
    pauseClock.start();
    // One finite I/O budget, shared by checkpoint ACK and completion, rather
    // than a 5 s pause SLA or QTRY's implicit extended-timeout second attempt.
    // GIO may spend seconds inside one copy_file_range before the callback.
    QDeadlineTimer ioDeadline(180000);
    QPointer<ExplorerTransferController> controller;
    QPointer<QPushButton> pause;
    const QString copiedPath = QDir(destination).filePath(QFileInfo(source).fileName());
    const char* phase = "preparing";
    int responsiveTicks = 0;
    const auto trace = [&] {
        const auto s = controller ? controller->snapshot() : ExplorerTransferController::Snapshot{};
        qInfo().noquote() << "PAUSE_TRACE" << "ms=" << pauseClock.elapsed() << "phase=" << phase
            << "controller=" << bool(controller) << "state=" << int(s.state)
            << "request=" << (job && job->pauseRequested()) << "ack=" << (job && job->isPauseAcknowledged())
            << "current=" << s.currentFileBytes << "finished=" << s.finishedBytes << "total=" << s.totalBytes
            << "disk=" << QFileInfo(copiedPath).size() << "gui_ticks=" << responsiveTicks
            << "caption=" << (pause ? pause->text() : QStringLiteral("no-dialog"));
    };
    QTimer diagnosticTimer;
    diagnosticTimer.setInterval(500);
    connect(&diagnosticTimer, &QTimer::timeout, &operation, trace);
    QTimer responsivenessProbe;
    responsivenessProbe.setInterval(25);
    connect(&responsivenessProbe, &QTimer::timeout, &operation, [&] { ++responsiveTicks; });
    const auto cleanup = qScopeGuard([&] {
        diagnosticTimer.stop();
        responsivenessProbe.stop();
        release();
        Fm::FileOperation::setDialogFactory(nullptr);
        if(job && finished.isEmpty()) {
            job->cancel();
            // Do not leave a paused worker accessing an already removed fixture
            // when an assertion fails. Cancellation still needs a GIO checkpoint.
            if(!QTest::qWaitFor([&] { return !finished.isEmpty(); }, 180000)) {
                root.setAutoRemove(false);
                qCritical().noquote() << "pause test cleanup timed out; retained active fixture" << root.path();
            }
        }
    });
    diagnosticTimer.start();
    responsivenessProbe.start();
    trace();
    QVERIFY(operation.run());
    QVERIFY2(QTest::qWaitFor([&] { return capturedTransferDialog && preparationReached->load(); }, 10000),
             "real delayed dialog / preparation rendezvous not reached");
    controller = capturedTransferDialog->findChild<ExplorerTransferController*>();
    pause = capturedTransferDialog->findChild<QPushButton*>(QStringLiteral("pauseButton"));
    QVERIFY(controller);
    QVERIFY(pause);
    QVERIFY(pause->isVisible());
    QVERIFY(pause->isEnabled());
    QVERIFY(!job->isPauseAcknowledged());
    phase = "factory-visible-worker-held";
    trace();
    QTest::mouseClick(pause, Qt::LeftButton);
    const qint64 requestedAt = pauseClock.elapsed();
    QVERIFY(job->pauseRequested());
    QVERIFY(!job->isPauseAcknowledged());
    QCOMPARE(controller->snapshot().state, ExplorerTransferController::State::Pausing);
    QCOMPARE(pause->text(), QStringLiteral("Pausando…"));
    QVERIFY(!pause->isEnabled());
    phase = "request-without-ack-worker-held";
    trace();
    // Exercise an actual GUI dispatch while the worker cannot ACK. This makes
    // the test deterministically reject an optimistic/placebo Resume caption.
    const int ticksBeforeAck = responsiveTicks;
    QTest::qWait(75);
    QVERIFY(responsiveTicks > ticksBeforeAck);
    QVERIFY(!job->isPauseAcknowledged());
    QCOMPARE(pause->text(), QStringLiteral("Pausando…"));
    const qint64 releasedAt = pauseClock.elapsed();
    release();
    phase = "awaiting-gio-checkpoint";
    QVERIFY2(QTest::qWaitFor([&] {
        return !finished.isEmpty() || (controller && controller->snapshot().paused
            && pause && pause->text() == QStringLiteral("Retomar") && pause->isEnabled());
    }, int(ioDeadline.remainingTime())), "real pause ACK / UI not observed within the shared 180 s I/O budget");
    QVERIFY2(finished.isEmpty(), "copy completed before a partial-copy pause ACK");
    QVERIFY(controller);
    QVERIFY(job->pauseRequested());
    QVERIFY(job->isPauseAcknowledged());
    QVERIFY(controller->snapshot().paused);
    QCOMPARE(pause->text(), QStringLiteral("Retomar"));
    const qint64 ackObservedAt = pauseClock.elapsed();
    const qint64 ackLatencyMs = ackObservedAt - requestedAt;
    const qint64 pausedBytes = QFileInfo(copiedPath).size();
    QVERIFY(pausedBytes > 0);
    QVERIFY(pausedBytes < qint64(fixtureBytes));
    QVERIFY(!rendezvousExpired->load());
    phase = "ack-partial-copy";
    trace();
    const int ticksWhilePaused = responsiveTicks;
    // Check throughout the interval, not just two equal endpoint samples.
    QElapsedTimer stableClock;
    stableClock.start();
    while(stableClock.elapsed() < 300) {
        QTest::qWait(10);
        QCOMPARE(QFileInfo(copiedPath).size(), pausedBytes);
        QVERIFY(job->isPauseAcknowledged());
    }
    QVERIFY(responsiveTicks >= ticksWhilePaused + 5);
    const qint64 stableMs = stableClock.elapsed();
    const int pausedGuiTicks = responsiveTicks - ticksWhilePaused;
    const QString capture = qEnvironmentVariable("EXPLORER_TRANSFER_UI_SCREENSHOT");
    if(!capture.isEmpty()) QVERIFY(capturedTransferDialog->grab().save(capture));
    QTest::mouseClick(pause, Qt::LeftButton);
    QVERIFY(!job->pauseRequested());
    phase = "resumed";
    trace();
    QVERIFY2(QTest::qWaitFor([&] { return finished.count() == 1; }, int(ioDeadline.remainingTime())),
             "resume did not complete within the shared 180 s I/O budget");
    QCOMPARE(QFileInfo(copiedPath).size(), qint64(fixtureBytes));
    qInfo().noquote() << "LIVE_TRANSFER_UI"
                      << "fixture_bytes=" << fixtureBytes
                      << "paused_bytes=" << pausedBytes
                      << "ack_latency_ms=" << ackLatencyMs
                      << "checkpoint_wait_ms=" << ackObservedAt - releasedAt
                      << "elapsed_ms=" << pauseClock.elapsed()
                      << "stable_ms=" << stableMs
                      << "paused_gui_ticks=" << pausedGuiTicks
                      << "pause_button_ack_label=" << QStringLiteral("Retomar")
                      << "completed_bytes=" << QFileInfo(copiedPath).size()
                      << "screenshot=" << (capture.isEmpty() ? QStringLiteral("not-requested") : capture);
}

void ExplorerTransferControllerTest::defaultDialogFactoryReceivesPointerBasedErrorWithoutTestRegistration() {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString sourcePath = root.filePath(QStringLiteral("default-error-source.txt"));
    QFile sourceFile(sourcePath);
    QVERIFY(sourceFile.open(QIODevice::WriteOnly));
    QCOMPARE(sourceFile.write("data"), qint64(4));
    sourceFile.close();
    const QString invalidDestination = root.filePath(QStringLiteral("not-a-directory"));
    QFile destinationFile(invalidDestination);
    QVERIFY(destinationFile.open(QIODevice::WriteOnly));
    QCOMPARE(destinationFile.write("destination"), qint64(11));
    destinationFile.close();

    Fm::FileOperation::setDialogFactory(nullptr);
    Fm::FilePathList sources{Fm::FilePath::fromLocalPath(sourcePath.toLocal8Bit().constData())};
    Fm::FileOperation operation(Fm::FileOperation::Copy, sources);
    operation.setAutoDestroy(false);
    operation.setDestination(Fm::FilePath::fromLocalPath(invalidDestination.toLocal8Bit().constData()));
    QSignalSpy finished(&operation, &Fm::FileOperation::finished);
    int defaultErrorDialogs = 0;
    QTimer closeDefaultError;
    closeDefaultError.setInterval(10);
    QObject::connect(&closeDefaultError, &QTimer::timeout, &operation, [&] {
        if(auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
            ++defaultErrorDialogs;
            box->accept();
        }
    });
    closeDefaultError.start();
    QVERIFY(operation.run());
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);
    closeDefaultError.stop();
    QVERIFY(defaultErrorDialogs > 0);
    QVERIFY(!operation.isRunning());
}

void ExplorerTransferControllerTest::preservesExplicitConflictResponseOnLiveJob() {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString source = root.filePath(QStringLiteral("same-name.txt"));
    QFile sourceFile(source);
    QVERIFY(sourceFile.open(QIODevice::WriteOnly));
    QCOMPARE(sourceFile.write("new"), qint64(3));
    sourceFile.close();
    const QString destinationDir = root.filePath(QStringLiteral("dest"));
    QVERIFY(QDir().mkpath(destinationDir));
    QFile destinationFile(QDir(destinationDir).filePath(QStringLiteral("same-name.txt")));
    QVERIFY(destinationFile.open(QIODevice::WriteOnly));
    QCOMPARE(destinationFile.write("old"), qint64(3));
    destinationFile.close();

    Fm::FilePathList sources{Fm::FilePath::fromLocalPath(source.toLocal8Bit().constData())};
    Fm::FilePath destination = Fm::FilePath::fromLocalPath(destinationDir.toLocal8Bit().constData());
    auto* job = new Fm::FileTransferJob(sources, destination, Fm::FileTransferJob::Mode::COPY);
    job->setAutoDelete(false);
    ExplorerTransferController controller(job);
    int conflictPrompts = 0;
    QObject::connect(job, &Fm::FileOperationJob::fileExists, &controller,
        [&conflictPrompts](const Fm::FileInfo&, const Fm::FileInfo&,
                           Fm::FileOperationJob::FileExistsAction& response, Fm::FilePath&) {
            ++conflictPrompts;
            response = Fm::FileOperationJob::SKIP;
        }, Qt::BlockingQueuedConnection);
    QSignalSpy finished(job, &Fm::Job::finished);
    job->runAsync();
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);
    QCOMPARE(conflictPrompts, 1);
    QVERIFY(destinationFile.open(QIODevice::ReadOnly));
    QCOMPARE(destinationFile.readAll(), QByteArray("old"));
    QVERIFY(QFileInfo(source).exists());
    delete job;
}

void ExplorerTransferControllerTest::cancellationForwardsToLiveLibfmJob() {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString source = root.filePath(QStringLiteral("cancel-source.bin"));
    QFile sourceFile(source);
    QVERIFY(sourceFile.open(QIODevice::WriteOnly));
    const QByteArray block(1024 * 1024, 'C');
    for(int i = 0; i < 64; ++i)
        QCOMPARE(sourceFile.write(block), qint64(block.size()));
    sourceFile.close();
    QVERIFY(QDir().mkpath(root.filePath(QStringLiteral("dest"))));

    Fm::FilePathList sources{Fm::FilePath::fromLocalPath(source.toLocal8Bit().constData())};
    Fm::FilePath destination = Fm::FilePath::fromLocalPath(root.filePath(QStringLiteral("dest")).toLocal8Bit().constData());
    auto* job = new Fm::FileTransferJob(sources, destination, Fm::FileTransferJob::Mode::COPY);
    job->setAutoDelete(false);
    ExplorerTransferController controller(job);
    QSignalSpy cancellationRequested(&controller, &ExplorerTransferController::cancellationRequested);
    QSignalSpy finished(job, &Fm::Job::finished);
    QVERIFY(controller.markStarted());
    job->runAsync();
    QVERIFY(controller.pause());
    QTRY_VERIFY_WITH_TIMEOUT(controller.snapshot().paused, 5000);
    controller.cancel();
    controller.cancel();
    QCOMPARE(cancellationRequested.count(), 1);
    QVERIFY(controller.snapshot().state == ExplorerTransferController::State::Cancelling ||
            controller.snapshot().state == ExplorerTransferController::State::Finished);
    QCOMPARE(controller.snapshot().running,
             controller.snapshot().state == ExplorerTransferController::State::Cancelling);
    QVERIFY(controller.snapshot().cancellationRequested);
    QTRY_VERIFY_WITH_TIMEOUT(cancellationRequested.count() > 0, 10000);
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);
    QCOMPARE(controller.snapshot().state, ExplorerTransferController::State::Finished);
    QVERIFY(controller.snapshot().cancellationRequested);
    QVERIFY(!controller.snapshot().running);
    QVERIFY(QFileInfo(source).exists());
    delete job;
}

void ExplorerTransferControllerTest::reportsRealTwoGiBCopyProgressAndIntegrity() {
    constexpr quint64 fixtureBytes = 2ULL * 1024 * 1024 * 1024;
    constexpr qsizetype chunkBytes = 1024 * 1024;
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QStorageInfo storage(root.path());
    QVERIFY2(storage.isValid() && storage.bytesAvailable() >= qint64(3 * fixtureBytes),
             "need >= 6 GiB free for materialized source, destination, and safety margin");

    const QString source = root.filePath(QStringLiteral("source-2GiB.bin"));
    const QString destinationDir = root.filePath(QStringLiteral("dest"));
    QVERIFY(QDir().mkpath(destinationDir));
    QFile sourceFile(source);
    QVERIFY(sourceFile.open(QIODevice::WriteOnly));
    QByteArray block(chunkBytes, Qt::Uninitialized);
    for(qsizetype i = 0; i < chunkBytes; ++i)
        block[i] = char((i * 37 + (i >> 8) * 13 + 91) & 0xff);
    QCryptographicHash sourceHash(QCryptographicHash::Sha256);
    for(quint64 written = 0; written < fixtureBytes; written += chunkBytes) {
        QCOMPARE(sourceFile.write(block), qint64(chunkBytes));
        sourceHash.addData(block);
    }
    QVERIFY(sourceFile.flush());
    sourceFile.close();
    struct stat sourceStat {};
    QVERIFY(::stat(source.toLocal8Bit().constData(), &sourceStat) == 0);
    QVERIFY2(quint64(sourceStat.st_blocks) * 512 >= fixtureBytes * 99 / 100,
             "fixture is sparse or not materially allocated");

    Fm::FilePathList sources{Fm::FilePath::fromLocalPath(source.toLocal8Bit().constData())};
    Fm::FilePath destination = Fm::FilePath::fromLocalPath(destinationDir.toLocal8Bit().constData());
    auto* job = new Fm::FileTransferJob(sources, destination, Fm::FileTransferJob::Mode::COPY);
    job->setAutoDelete(false);
    ExplorerTransferController controller(job);
    QSignalSpy finished(job, &Fm::Job::finished);
    QSignalSpy controllerFinished(&controller, &ExplorerTransferController::finished);
    quint64 maxObservedBytes = 0;
    int telemetrySamples = 0;
    QObject::connect(&controller, &ExplorerTransferController::telemetryUpdated, &controller, [&] {
        ++telemetrySamples;
        const auto observed = controller.snapshot();
        maxObservedBytes = qMax(maxObservedBytes, observed.finishedBytes + observed.currentFileBytes);
    });
    const auto startedAt = QDateTime::currentDateTimeUtc();
    bool pauseAccepted = false;
    int responsiveTicks = 0;
    QTimer pauseWatcher;
    pauseWatcher.setInterval(2);
    QObject::connect(&pauseWatcher, &QTimer::timeout, &controller, [&] { ++responsiveTicks; });
    QTimer responsivenessProbe;
    responsivenessProbe.setInterval(25);
    QObject::connect(&responsivenessProbe, &QTimer::timeout, &controller, [&] { ++responsiveTicks; });
    QElapsedTimer elapsed;
    elapsed.start();
    QVERIFY(controller.markStarted());
    pauseWatcher.start();
    responsivenessProbe.start();
    pauseAccepted = controller.pause();
    QVERIFY(pauseAccepted);
    job->runAsync();
    QTRY_VERIFY_WITH_TIMEOUT(pauseAccepted, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(controller.snapshot().paused, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(controller.snapshot().state, ExplorerTransferController::State::Paused, 5000);
    const QString copied = QDir(destinationDir).filePath(QStringLiteral("source-2GiB.bin"));
    const qint64 pausedSize = QFileInfo(copied).size();
    QVERIFY(pausedSize > 0);
    QVERIFY(pausedSize < qint64(fixtureBytes));
    const int ticksBeforeWait = responsiveTicks;
    QTest::qWait(350);
    QCOMPARE(QFileInfo(copied).size(), pausedSize);
    QVERIFY(responsiveTicks >= ticksBeforeWait + 5);
    QVERIFY(controller.resume());
    pauseWatcher.stop();
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 180000);
    QTRY_COMPARE_WITH_TIMEOUT(controllerFinished.count(), 1, 10000);
    const auto endedAt = QDateTime::currentDateTimeUtc();
    const auto elapsedMs = elapsed.elapsed();
    const auto sample = controller.snapshot();
    QVERIFY(!controller.pause());
    QVERIFY(!controller.resume());
    QFile copiedFile(copied);
    QVERIFY(copiedFile.open(QIODevice::ReadOnly));
    QCryptographicHash copiedHash(QCryptographicHash::Sha256);
    while(!copiedFile.atEnd())
        copiedHash.addData(copiedFile.read(chunkBytes));

    QCOMPARE(sample.totalBytes, fixtureBytes);
    QCOMPARE(sample.finishedBytes, fixtureBytes);
    QVERIFY(maxObservedBytes > 0);
    QVERIFY(maxObservedBytes <= fixtureBytes);
    QCOMPARE(QFileInfo(source).size(), qint64(fixtureBytes));
    QCOMPARE(QFileInfo(copied).size(), qint64(fixtureBytes));
    QVERIFY(QFileInfo(source).exists());
    QCOMPARE(sourceHash.result(), copiedHash.result());
    QVERIFY(telemetrySamples > 0);
    qInfo().noquote() << "TRANSFER_METRICS"
                      << "bytes=" << fixtureBytes
                      << "started_utc=" << startedAt.toString(Qt::ISODateWithMs)
                      << "finished_utc=" << endedAt.toString(Qt::ISODateWithMs)
                      << "elapsed_ms=" << elapsedMs
                      << "sha256_source=" << sourceHash.result().toHex()
                      << "sha256_destination=" << copiedHash.result().toHex()
                      << "source_allocated_bytes=" << quint64(sourceStat.st_blocks) * 512
                      << "telemetry_samples=" << telemetrySamples
                      << "max_observed_bytes=" << maxObservedBytes;
    delete job;
}

void ExplorerTransferControllerTest::cooperativePauseCapabilityIsNotAdvertisedByUnsupportedJobs() {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString source = root.filePath(QStringLiteral("link-source.txt"));
    QFile file(source);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write("data"), qint64(4));
    file.close();
    const QString destination = root.filePath(QStringLiteral("dest"));
    QVERIFY(QDir().mkpath(destination));
    Fm::FilePathList sources{Fm::FilePath::fromLocalPath(source.toLocal8Bit().constData())};
    auto* job = new Fm::FileTransferJob(sources,
        Fm::FilePath::fromLocalPath(destination.toLocal8Bit().constData()), Fm::FileTransferJob::Mode::LINK);
    job->setAutoDelete(false);
    ExplorerTransferController controller(job);
    QVERIFY(!controller.supportsPause());
    QVERIFY(!controller.pause());
    QVERIFY(!controller.resume());
    delete job;
}

void ExplorerTransferControllerTest::externalCancellationNotifiesControllerExactlyOnce() {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString source = root.filePath(QStringLiteral("external-cancel.bin"));
    QFile file(source);
    QVERIFY(file.open(QIODevice::WriteOnly));
    const QByteArray block(1024 * 1024, 'E');
    for(int i = 0; i < 64; ++i) QCOMPARE(file.write(block), qint64(block.size()));
    file.close();
    const QString destination = root.filePath(QStringLiteral("dest"));
    QVERIFY(QDir().mkpath(destination));
    Fm::FilePathList sources{Fm::FilePath::fromLocalPath(source.toLocal8Bit().constData())};
    auto* job = new Fm::FileTransferJob(sources,
        Fm::FilePath::fromLocalPath(destination.toLocal8Bit().constData()), Fm::FileTransferJob::Mode::COPY);
    job->setAutoDelete(false);
    ExplorerTransferController controller(job);
    QSignalSpy notices(&controller, &ExplorerTransferController::cancellationRequested);
    QSignalSpy finished(job, &Fm::Job::finished);
    QVERIFY(controller.markStarted());
    job->runAsync();
    job->cancel();
    job->cancel();
    QTRY_COMPARE_WITH_TIMEOUT(notices.count(), 1, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);
    QCOMPARE(notices.count(), 1);
    QCOMPARE(controller.snapshot().state, ExplorerTransferController::State::Finished);
    delete job;
}

void ExplorerTransferControllerTest::directCancellableCancellationWakesPausedCopy() {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString source = root.filePath(QStringLiteral("direct-cancel.bin"));
    QFile file(source);
    QVERIFY(file.open(QIODevice::WriteOnly));
    const QByteArray block(1024 * 1024, 'D');
    for(int i = 0; i < 128; ++i) QCOMPARE(file.write(block), qint64(block.size()));
    file.close();
    const QString destination = root.filePath(QStringLiteral("dest"));
    QVERIFY(QDir().mkpath(destination));
    Fm::FilePathList sources{Fm::FilePath::fromLocalPath(source.toLocal8Bit().constData())};
    auto* job = new Fm::FileTransferJob(sources,
        Fm::FilePath::fromLocalPath(destination.toLocal8Bit().constData()), Fm::FileTransferJob::Mode::COPY);
    job->setAutoDelete(false);
    ExplorerTransferController controller(job);
    QSignalSpy finished(job, &Fm::Job::finished);
    int responsiveTicks = 0;
    QTimer responsivenessProbe;
    responsivenessProbe.setInterval(10);
    QObject::connect(&responsivenessProbe, &QTimer::timeout, &controller, [&] { ++responsiveTicks; });
    QVERIFY(controller.markStarted());
    responsivenessProbe.start();
    job->runAsync();
    QVERIFY(controller.pause());
    QTRY_VERIFY_WITH_TIMEOUT(controller.snapshot().paused, 5000);
    const int ticksBeforeCancel = responsiveTicks;

    // Exercise the public GCancellable directly, not Job::cancel().
    g_cancellable_cancel(job->cancellable().get());

    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
    QVERIFY(responsiveTicks > ticksBeforeCancel);
    QCOMPARE(controller.snapshot().state, ExplorerTransferController::State::Finished);
    responsivenessProbe.stop();
    delete job;
}

QTEST_MAIN(ExplorerTransferControllerTest)
#include "tst_explorertransfercontroller.moc"
