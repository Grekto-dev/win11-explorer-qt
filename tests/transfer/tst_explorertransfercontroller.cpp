#include <QtTest>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStorageInfo>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QDateTime>
#include <sys/stat.h>
#include "../../pcmanfm/explorertransfercontroller.h"
#include "core/filetransferjob.h"
#include "core/job.h"

class ExplorerTransferControllerTest final : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void cancellationForwardsToLiveLibfmJob();
    void preservesExplicitConflictResponseOnLiveJob();
    void reportsRealTwoGiBCopyProgressAndIntegrity();
};

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
    QSignalSpy cancelled(&controller, &ExplorerTransferController::cancelled);
    QSignalSpy finished(job, &Fm::Job::finished);
    job->runAsync();
    controller.cancel();
    QTRY_COMPARE_WITH_TIMEOUT(cancelled.count(), 1, 10000);
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);
    QVERIFY(controller.snapshot().cancelled);
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
    quint64 maxObservedBytes = 0;
    int telemetrySamples = 0;
    QObject::connect(&controller, &ExplorerTransferController::telemetryUpdated, &controller, [&] {
        ++telemetrySamples;
        const auto observed = controller.snapshot();
        maxObservedBytes = qMax(maxObservedBytes, observed.finishedBytes + observed.currentFileBytes);
    });
    const auto startedAt = QDateTime::currentDateTimeUtc();
    QElapsedTimer elapsed;
    elapsed.start();
    job->runAsync();
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 180000);
    const auto endedAt = QDateTime::currentDateTimeUtc();
    const auto elapsedMs = elapsed.elapsed();
    const auto sample = controller.snapshot();
    const QString copied = QDir(destinationDir).filePath(QStringLiteral("source-2GiB.bin"));
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

QTEST_GUILESS_MAIN(ExplorerTransferControllerTest)
#include "tst_explorertransfercontroller.moc"
