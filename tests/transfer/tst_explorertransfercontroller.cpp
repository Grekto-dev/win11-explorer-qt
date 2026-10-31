#include <QtTest>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStorageInfo>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QDateTime>
#include <QApplication>
#include <QMessageBox>
#include <QTimer>
#include <sys/stat.h>
#include "../../pcmanfm/explorertransfercontroller.h"
#include "core/filetransferjob.h"
#include "core/job.h"
#include <fileoperation.h>
#include <fileoperationdialog.h>

namespace {
int dialogFactoryCalls = 0;
int conflictDialogCalls = 0;
int errorDialogCalls = 0;

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
    void cancellationForwardsToLiveLibfmJob();
    void observerLifecycleTracksRealJobSignals();
    void observerDetachesWhenBorrowedJobIsDestroyedEarly();
    void destroyingObserverDoesNotDestroyBorrowedJob();
    void destroyingObserverDuringIoLeavesJobRunning();
    void optInDialogFactoryKeepsConflictRoutingOnRealFileOperation();
    void optInDialogFactoryKeepsErrorRoutingOnRealFileOperation();
    void defaultDialogFactoryReceivesPointerBasedErrorWithoutTestRegistration();
    void preservesExplicitConflictResponseOnLiveJob();
    void reportsRealTwoGiBCopyProgressAndIntegrity();
    void cooperativePauseCapabilityIsNotAdvertisedByUnsupportedJobs();
    void externalCancellationNotifiesControllerExactlyOnce();
};

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
    QVERIFY(!operation.isRunning());
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
    QCOMPARE(controller.snapshot().state, ExplorerTransferController::State::Cancelling);
    QVERIFY(controller.snapshot().running);
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

QTEST_MAIN(ExplorerTransferControllerTest)
#include "tst_explorertransfercontroller.moc"
