// SPDX-License-Identifier: GPL-2.0-or-later
// Behavioral tests use the real PCManFM-Qt window and libfm-qt folder model.
#include "application.h"
#include "mainwindow.h"
#include "tabpage.h"
#include "preferencesdialog.h"
#include "explorericons.h"
#include "explorertransferdialog.h"
#include <fileoperation.h>
#include <QMessageBox>
#include <QProgressBar>
#include <QLabel>
#include <QCheckBox>
#include <QStandardItemModel>
#include <QStyledItemDelegate>
#include <QIconEngine>
#include <QPainter>
#include <QStyleOptionViewItem>
#include <QProxyStyle>
#include "view.h"
#include "launcher.h"
#include <QtTest>
#include <QTemporaryDir>
#include <QToolBar>
#include <QMenuBar>
#include <QToolButton>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTreeView>
#include <QHeaderView>
#include <QClipboard>
#include <QMimeData>
#include <libfm-qt6/pathbar.h>
#include <libfm-qt6/pathedit.h>
#include <libfm-qt6/folderitemdelegate.h>
#include <QDir>
#include <QFile>
#include <QScopedPointer>
#include <QCryptographicHash>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <QTimer>
#include <QScopeGuard>
#include <QJsonArray>
#include <libfm-qt6/core/folderconfig.h>
#include <QColor>
#include <QAbstractItemView>
#include <QLibrary>
#include <QImage>
#include <QStringList>
#include <memory>
#include <stdexcept>
#include <functional>
#include <QDragMoveEvent>
#include <QTextDocument>
#include <QFontMetrics>
#include <QProcess>
#include <sys/stat.h>
#include "selection_x11_ownership.h"

namespace {
template<typename Body, typename Release>
bool checkedInputGesture(Body body, Release release) {
    const auto cleanup = qScopeGuard(release);
    bool completed = false;
    body(completed);
    return completed;
}
template<typename Gesture>
bool selectionModes(Gesture gesture) {
    for(const auto mode : {Fm::FolderView::DetailedListMode, Fm::FolderView::IconMode}) {
        if(!gesture(mode)) return false;
    }
    return true;
}
// Static (no running job) dialog fixture for the Windows copy-dialog contract
// tests. devicePixelRatio is asserted to be 1 where sizes are compared.
struct TransferDialogFixture {
    QTemporaryDir root;
    Fm::FilePathList sources;
    std::unique_ptr<Fm::FileOperation> operation;
    std::unique_ptr<ExplorerTransferDialog> dialog;
    explicit TransferDialogFixture(Fm::FileOperation::Type type = Fm::FileOperation::Copy,
                                   const QString& fileName = QStringLiteral("fixture3g.bin"),
                                   const QString& parentDir = QString(), bool setOperationDestination = false) {
        if(!parentDir.isEmpty()) QDir().mkpath(root.filePath(parentDir));
        const QString src = parentDir.isEmpty() ? root.filePath(fileName) : root.filePath(parentDir + QLatin1Char('/') + fileName);
        QFile f(src);
        if(f.open(QIODevice::WriteOnly)) { f.write("x"); f.close(); }
        QDir().mkpath(root.filePath(QStringLiteral("dst1")));
        sources.push_back(Fm::FilePath::fromLocalPath(src.toLocal8Bit().constData()));
        operation = std::make_unique<Fm::FileOperation>(type, sources);
        // A destination makes a single-regular-file COPY job advertise cooperative pause (real capability gate).
        if(setOperationDestination)
            operation->setDestination(Fm::FilePath::fromLocalPath(root.filePath(QStringLiteral("dst1")).toLocal8Bit().constData()));
        dialog.reset(qobject_cast<ExplorerTransferDialog*>(createExplorerTransferDialog(operation.get())));
        if(!dialog) return;
        dialog->setSourceFiles(sources);
        dialog->setDestPath(Fm::FilePath::fromLocalPath(root.filePath(QStringLiteral("dst1")).toLocal8Bit().constData()));
        dialog->setCurFile(src);
        dialog->setPrepared();
    }
    template<typename T> T* child(const char* name) const {
        return dialog ? dialog->findChild<T*>(QString::fromLatin1(name)) : nullptr;
    }
    // Centre of a child widget in dialog coordinates (x, y as doubles: even-sized
    // widgets have half-pixel centres, like the measured 72.5 px).
    QPointF centreOf(QWidget* w) const {
        const QPoint tl = w->mapTo(dialog.get(), QPoint(0, 0));
        return QPointF(tl.x() + w->width() / 2.0, tl.y() + w->height() / 2.0);
    }
};
QColor barPixel(QWidget* progress, int xFraction256) {
    const QImage img = progress->grab().toImage();
    return QColor(img.pixel(img.width() * xFraction256 / 256, img.height() / 2));
}
}  // namespace

class ExplorerTest : public QObject {
    Q_OBJECT
private Q_SLOTS:

    // ---- Windows copy-dialog contract (docs/fidelity/transfer-dialog-measurements.md) ----
    void transferDialogTitleFollowsStateAfterRealAck() {
        TransferDialogFixture fx;
        QVERIFY(fx.dialog);
        fx.dialog->setPauseAvailable(true);
        fx.dialog->setPercent(32);
        QCOMPARE(fx.dialog->windowTitle(), QStringLiteral("32% concluído"));
        // Requested but not acknowledged: the title must not claim "Pausado".
        fx.dialog->applyTransferState(ExplorerTransferController::State::Pausing, false);
        QCOMPARE(fx.dialog->windowTitle(), QStringLiteral("32% concluído"));
        fx.dialog->applyTransferState(ExplorerTransferController::State::Pausing, true);
        QCOMPARE(fx.dialog->windowTitle(), QStringLiteral("Pausado - 32% concluído"));
        QCOMPARE(fx.child<QLabel>("transferPercent")->text(), QStringLiteral("Pausado - 32% concluído"));
        fx.dialog->setPercent(33);
        QCOMPARE(fx.dialog->windowTitle(), QStringLiteral("Pausado - 33% concluído"));
        fx.dialog->applyTransferState(ExplorerTransferController::State::Running, false);
        QCOMPARE(fx.dialog->windowTitle(), QStringLiteral("33% concluído"));
        fx.dialog->applyTransferState(ExplorerTransferController::State::Cancelling, false);
        QCOMPARE(fx.dialog->windowTitle(), QStringLiteral("Cancelando - 33% concluído"));
        QCOMPARE(fx.child<QLabel>("transferPercent")->text(), QStringLiteral("Cancelando - 33% concluído"));
    }

    void transferDialogBarIsLightBlueRunningAndYellowPausedOrCancelling() {
        TransferDialogFixture fx;
        QVERIFY(fx.dialog);
        fx.dialog->setPauseAvailable(true);
        fx.dialog->show();
        fx.dialog->setPercent(60);
        auto* bar = fx.child<QProgressBar>("transferProgress");
        QVERIFY(bar);
        QCOMPARE(barPixel(bar, 64), QColor(96, 205, 255));          // measured #60CDFF
        fx.dialog->applyTransferState(ExplorerTransferController::State::Paused, true);
        QCOMPARE(barPixel(bar, 64), QColor(252, 225, 0));           // measured #FCE100
        fx.dialog->applyTransferState(ExplorerTransferController::State::Running, false);
        QCOMPARE(barPixel(bar, 64), QColor(96, 205, 255));
        fx.dialog->applyTransferState(ExplorerTransferController::State::Cancelling, false);
        QCOMPARE(barPixel(bar, 64), QColor(252, 225, 0));
        QCOMPARE(bar->width(), 395);
        QCOMPARE(bar->height(), 15);
    }

    void transferDialogPauseGlyphButtonIsInlineAndHonestAboutAck() {
        TransferDialogFixture fx;
        QVERIFY(fx.dialog);
        fx.dialog->setPauseAvailable(true);
        fx.dialog->show();
        auto* pause = fx.child<QPushButton>("pauseButton");
        auto* cancel = fx.child<QPushButton>("transferCancelButton");
        QVERIFY(pause && cancel);
        QVERIFY(pause->isVisible());
        QCOMPARE(pause->property("glyph").toString(), QStringLiteral("pause"));
        QCOMPARE(pause->accessibleName(), QStringLiteral("Pausar"));
        QVERIFY(pause->text().isEmpty());
        QCOMPARE(cancel->property("glyph").toString(), QStringLiteral("cancel"));
        QVERIFY(cancel->text().isEmpty());
        // Same row as the cancel glyph, to its left (measured centres (366, 72.5) and (413, 72.5)).
        QCOMPARE(fx.centreOf(pause), QPointF(366.0, 72.5));
        QCOMPARE(fx.centreOf(cancel), QPointF(413.0, 72.5));
        fx.dialog->applyTransferState(ExplorerTransferController::State::Pausing, false);
        QCOMPARE(pause->property("glyph").toString(), QStringLiteral("pause"));
        QVERIFY(!pause->isEnabled());                               // no Resume before the ACK
        QCOMPARE(pause->accessibleName(), QStringLiteral("Pausando…"));
        fx.dialog->applyTransferState(ExplorerTransferController::State::Paused, true);
        QCOMPARE(pause->property("glyph").toString(), QStringLiteral("resume"));
        QCOMPARE(pause->accessibleName(), QStringLiteral("Retomar"));
        QVERIFY(pause->isEnabled());
        fx.dialog->applyTransferState(ExplorerTransferController::State::Cancelling, false);
        QCOMPARE(pause->property("glyph").toString(), QStringLiteral("resume"));
        QVERIFY(!pause->isEnabled());                               // dimmed ▶ while cancelling
        // The glyph is drawn, not a font character: the centre of the ▶ is white when active and
        // the measured dim grey #797979 while cancelling.
        const auto centrePixel = [&] {
            const QImage img = pause->grab().toImage();
            return QColor(img.pixel(img.width() / 2, img.height() / 2));
        };
        QCOMPARE(centrePixel(), QColor(121, 121, 121));
        TransferDialogFixture active;
        active.dialog->setPauseAvailable(true);
        active.dialog->applyTransferState(ExplorerTransferController::State::Paused, true);
        const QImage lit = active.child<QPushButton>("pauseButton")->grab().toImage();
        QCOMPARE(QColor(lit.pixel(lit.width() / 2, lit.height() / 2)), QColor(255, 255, 255));
    }

    void transferDialogHasNoPauseButtonWithoutBackendCapability() {
        TransferDialogFixture fx;
        QVERIFY(fx.dialog);
        fx.dialog->show();
        auto* pause = fx.child<QPushButton>("pauseButton");
        QVERIFY(pause);
        QVERIFY(!pause->isVisible());
        const QPoint where = pause->geometry().center();
        QWidget* hit = fx.dialog->childAt(where);
        QVERIFY(!qobject_cast<QPushButton*>(hit) || hit == fx.child<QPushButton>("transferCancelButton"));
        QVERIFY(hit != pause);
    }

    // Review G1: exercises the real supportsPause() gate (controller created from a real job with a destination),
    // not a dialog that never had a controller.
    void transferDialogPauseGateFollowsBackendCapability() {
        // Positive: single regular local file COPY advertises pause -> visible, enabled, focusable.
        {
            TransferDialogFixture fx(Fm::FileOperation::Copy, QStringLiteral("fixture3g.bin"), QString(), true);
            QVERIFY(fx.dialog);
            fx.dialog->show();
            auto* pause = fx.child<QPushButton>("pauseButton");
            QVERIFY(fx.dialog->findChild<ExplorerTransferController*>());
            QVERIFY(pause->isVisible() && pause->isEnabled());
            QVERIFY(pause->focusPolicy() != Qt::NoFocus);
        }
        for(auto type : {Fm::FileOperation::Move, Fm::FileOperation::Trash, Fm::FileOperation::Delete}) {
            TransferDialogFixture other(type, QStringLiteral("fixture3g.bin"), QString(), true);
            QVERIFY(other.dialog);
            QVERIFY2(other.operation->job(), "real job expected so the controller gate is actually evaluated");
            other.dialog->show();
            auto* pause = other.child<QPushButton>("pauseButton");
            QVERIFY(!pause->isVisibleTo(other.dialog.get()));
            QVERIFY(!pause->isVisible());
            QVERIFY(!other.dialog->findChild<ExplorerTransferController*>());   // gate rejected -> controller dropped
            QVERIFY(!pause->hasFocus());
            // Tab through the whole chain: focus never lands on the hidden pause button.
            for(int i = 0; i < 6; ++i) {
                QTest::keyClick(other.dialog.get(), Qt::Key_Tab);
                QVERIFY(QApplication::focusWidget() != pause);
            }
        }
    }

    void transferDialogCancelAsksNoConfirmationAndStaysVisibleAsCancelling() {
        TransferDialogFixture fx;
        QVERIFY(fx.dialog);
        fx.dialog->show();
        fx.dialog->setPercent(53);
        auto* cancel = fx.child<QPushButton>("transferCancelButton");
        QVERIFY(cancel);
        QTest::mouseClick(cancel, Qt::LeftButton);
        for(QWidget* w : QApplication::topLevelWidgets())
            QVERIFY2(!(qobject_cast<QMessageBox*>(w) && w->isVisible()), "cancel opened a confirmation box");
        QVERIFY(!QApplication::activeModalWidget());
        // Cancelling is not Finished: the dialog stays until the job really ends.
        QVERIFY(fx.dialog->isVisible());
        QCOMPARE(fx.dialog->windowTitle(), QStringLiteral("Cancelando - 53% concluído"));
        QVERIFY(!cancel->isEnabled());
    }

    // ---- Review A: cancel must reflect what the backend really accepted (never a permanent "Cancelando") ----
    // Controller whose job is already gone (Detached): nothing to cancel, the dialog must stay closable.
    void transferDialogCancelWithDetachedControllerDoesNotLatchCancelling() {
        TransferDialogFixture fx(Fm::FileOperation::Copy, QStringLiteral("fixture3g.bin"), QString(), true);
        QVERIFY(fx.dialog);
        fx.dialog->show();
        fx.dialog->setPercent(40);
        auto* pause = fx.child<QPushButton>("pauseButton");
        auto* cancel = fx.child<QPushButton>("transferCancelButton");
        QVERIFY2(pause->isVisible(), "controller with pause capability expected for a single-file COPY with destination");
        auto* controller = fx.dialog->findChild<ExplorerTransferController*>();
        QVERIFY(controller);
        delete fx.operation->job();
        QTRY_COMPARE(controller->snapshot().state, ExplorerTransferController::State::Detached);
        QTest::mouseClick(cancel, Qt::LeftButton);
        QVERIFY2(!fx.dialog->windowTitle().startsWith(QStringLiteral("Cancelando")), qPrintable(fx.dialog->windowTitle()));
        QVERIFY(!fx.dialog->isVisible());                           // closable: left to FileOperation::onJobFinish
        QVERIFY(cancel->isEnabled());
    }

    // The job really completed (its Finished notification may still be queued): the controller already reports
    // Finished, cancel is not accepted, so the dialog does not enter "Cancelando" and can be closed.
    void transferDialogCancelAfterJobCompletedDoesNotLatchCancelling() {
        TransferDialogFixture fx(Fm::FileOperation::Copy, QStringLiteral("fixture3g.bin"), QString(), true);
        QVERIFY(fx.dialog);
        fx.operation->setAutoDestroy(false);
        fx.dialog->show();
        fx.dialog->setPercent(40);
        auto* cancel = fx.child<QPushButton>("transferCancelButton");
        auto* controller = fx.dialog->findChild<ExplorerTransferController*>();
        QVERIFY(controller && fx.child<QPushButton>("pauseButton")->isVisible());
        // The BlockingQueued FileOperation slots would deadlock a same-thread run(); the dialog under test only
        // observes the job through its controller, so detach the operation and run the real job synchronously.
        QObject::disconnect(fx.operation->job(), nullptr, fx.operation.get(), nullptr);
        fx.operation->job()->run();
        QCOMPARE(controller->snapshot().state, ExplorerTransferController::State::Finished);
        QTest::mouseClick(cancel, Qt::LeftButton);
        QVERIFY2(!fx.dialog->windowTitle().startsWith(QStringLiteral("Cancelando")), qPrintable(fx.dialog->windowTitle()));
        QVERIFY(cancel->isEnabled());
        QVERIFY(!fx.dialog->isVisible());
    }

    // Without a controller (Move never advertises pause) the live job is cancelled exactly once and the repeated
    // gestures (✕ twice, Esc, reject()) are idempotent.
    void transferDialogCancelWithoutControllerIsOneIdempotentBackendRequest() {
        TransferDialogFixture fx(Fm::FileOperation::Move);
        QVERIFY(fx.dialog);
        QVERIFY(!fx.dialog->findChild<ExplorerTransferController*>());
        fx.dialog->show();
        fx.dialog->setPercent(10);
        QPointer<Fm::FileOperationJob> job = fx.operation->job();
        QVERIFY(job);
        QSignalSpy cancelled(job.data(), &Fm::Job::cancelled);
        auto* cancel = fx.child<QPushButton>("transferCancelButton");
        QTest::mouseClick(cancel, Qt::LeftButton);
        QCOMPARE(fx.dialog->windowTitle(), QStringLiteral("Cancelando - 10% concluído"));
        QVERIFY(!cancel->isEnabled());
        fx.dialog->reject();
        QTest::keyClick(fx.dialog.get(), Qt::Key_Escape);
        QCOMPARE(cancelled.count(), 1);
        QVERIFY(job->isCancelled());
        QVERIFY(fx.dialog->isVisible());                            // accepted -> stays as Cancelando until the job ends
    }

    // Repeated clicks with a live controller reach the controller/backend only once.
    void transferDialogRepeatedCancelReachesControllerOnce() {
        TransferDialogFixture fx(Fm::FileOperation::Copy, QStringLiteral("fixture3g.bin"), QString(), true);
        QVERIFY(fx.dialog);
        fx.dialog->show();
        auto* controller = fx.dialog->findChild<ExplorerTransferController*>();
        QVERIFY(controller);
        QSignalSpy requested(controller, &ExplorerTransferController::cancellationRequested);
        for(int i = 0; i < 3; ++i) fx.dialog->reject();
        QCOMPARE(requested.count(), 1);
        QVERIFY(fx.dialog->windowTitle().startsWith(QStringLiteral("Cancelando")));
        QVERIFY(fx.dialog->isVisible());
    }

    // No controller and no live job/operation: base behaviour (close), not a permanent "Cancelando".
    void transferDialogCancelWithoutControllerOrOperationClosesInsteadOfLatching() {
        TransferDialogFixture fx(Fm::FileOperation::Move);
        QVERIFY(fx.dialog);
        fx.dialog->show();
        delete fx.operation->job();
        fx.operation.reset();
        QTest::mouseClick(fx.child<QPushButton>("transferCancelButton"), Qt::LeftButton);
        QVERIFY2(!fx.dialog->windowTitle().startsWith(QStringLiteral("Cancelando")), qPrintable(fx.dialog->windowTitle()));
        QVERIFY(!fx.dialog->isVisible());
    }

    // Live operation whose job already went away (no controller): cancel must not touch the dead job.
    void transferDialogCancelWithoutControllerAfterJobDestroyedCloses() {
        TransferDialogFixture fx(Fm::FileOperation::Move);
        QVERIFY(fx.dialog);
        fx.dialog->show();
        delete fx.operation->job();
        QTest::mouseClick(fx.child<QPushButton>("transferCancelButton"), Qt::LeftButton);
        QVERIFY2(!fx.dialog->windowTitle().startsWith(QStringLiteral("Cancelando")), qPrintable(fx.dialog->windowTitle()));
        QVERIFY(!fx.dialog->isVisible());
    }

    // Review D: ▶ shown after the pause ACK (controller still "Pausing" until its next tick) must resume on click.
    void transferDialogResumeClickWorksDuringPausingWithAck() {
        using State = ExplorerTransferController::State;
        TransferDialogFixture fx(Fm::FileOperation::Copy, QStringLiteral("fixture3g.bin"), QString(), true);
        QVERIFY(fx.dialog);
        fx.dialog->show();
        auto* pause = fx.child<QPushButton>("pauseButton");
        auto* controller = fx.dialog->findChild<ExplorerTransferController*>();
        QVERIFY(controller && pause->isVisible());
        QTest::mouseClick(pause, Qt::LeftButton);
        QCOMPARE(controller->snapshot().state, State::Pausing);
        QVERIFY(controller->snapshot().pauseRequested);
        QVERIFY(!pause->isEnabled());                               // no ACK yet
        fx.dialog->applyTransferState(State::Pausing, true);        // the ACK telemetry
        QCOMPARE(pause->accessibleName(), QStringLiteral("Retomar"));
        QVERIFY(pause->isEnabled());
        QCOMPARE(controller->snapshot().state, State::Pausing);     // controller itself has not ticked yet
        QTest::mouseClick(pause, Qt::LeftButton);
        QCOMPARE(controller->snapshot().state, State::Running);     // the click was not swallowed
        QVERIFY(!controller->snapshot().pauseRequested);
        QCOMPARE(pause->accessibleName(), QStringLiteral("Pausar"));
        QVERIFY(!fx.dialog->windowTitle().startsWith(QStringLiteral("Pausado")));
    }

    void transferDialogHasMeasuredCollapsedAndExpandedSizes() {
        TransferDialogFixture fx;
        QVERIFY(fx.dialog);
        fx.dialog->setPauseAvailable(true);
        fx.dialog->show();
        QCOMPARE(fx.dialog->devicePixelRatioF(), 1.0);              // test scale: 96 DPI, DPR 1
        QCOMPARE(fx.dialog->size(), QSize(449, 165));
        auto* more = fx.child<QPushButton>("transferDetailsButton");
        auto* pause = fx.child<QPushButton>("pauseButton");
        auto* cancel = fx.child<QPushButton>("transferCancelButton");
        QVERIFY(more && more->isVisible());
        const QPointF pauseCentre = fx.centreOf(pause), cancelCentre = fx.centreOf(cancel);
        QVERIFY(!fx.dialog->detailsExpanded());
        QTest::mouseClick(more, Qt::LeftButton);
        QTRY_VERIFY(fx.dialog->detailsExpanded());
        QCOMPARE(fx.dialog->size(), QSize(449, 287));
        QCOMPARE(fx.centreOf(pause), pauseCentre);                  // controls do not move
        QCOMPARE(fx.centreOf(cancel), cancelCentre);
        QCOMPARE(more->text(), QStringLiteral("Menos detalhes"));
        QTest::mouseClick(more, Qt::LeftButton);
        QCOMPARE(fx.dialog->size(), QSize(449, 165));
        QCOMPARE(more->text(), QStringLiteral("Mais detalhes"));
        QCOMPARE(fx.centreOf(pause), pauseCentre);
    }

    void transferDialogDetailsShowNameEtaAndRemainingItemsFromRealCounters() {
        TransferDialogFixture fx;
        QVERIFY(fx.dialog);
        fx.dialog->show();
        auto* name = fx.child<QLabel>("transferDetailName");
        auto* eta = fx.child<QLabel>("transferDetailRemaining");
        auto* items = fx.child<QLabel>("transferDetailItems");
        QVERIFY(name && eta && items);
        QCOMPARE(name->text(), QStringLiteral("Nome: fixture3g.bin"));
        QCOMPARE(eta->text(), QStringLiteral("Tempo restante: Calculando…"));
        fx.dialog->setRemainingTime(0);                             // libfm has no estimate yet
        QCOMPARE(eta->text(), QStringLiteral("Tempo restante: Calculando…"));
        constexpr std::uint64_t total = 3ULL << 30, done = 1ULL << 30;
        fx.dialog->setDataTransferred(done, total);
        QCOMPARE(items->text(), QStringLiteral("Itens restantes: 1 (%1)")
                     .arg(QLocale().formattedDataSize(qint64(total - done), 2, QLocale::DataSizeSIFormat)));
        fx.dialog->setRemainingTime(75);
        QCOMPARE(eta->text(), QStringLiteral("Tempo restante: 1 min 15 s"));
    }

    // Review B: file names are attacker-controlled; the detail labels must show them literally,
    // never as rich text (<a>/<b>/<img> spoofing). summary_ builds its own links and escapes names.
    void transferDialogDetailLabelsShowHtmlInNamesLiterally() {
        const QString evil = QStringLiteral("<b>bold<a href=\"#x\">click<img src=\"logo\">.bin");
        TransferDialogFixture fx(Fm::FileOperation::Copy, evil, QStringLiteral("<i>dir"));
        QVERIFY(fx.dialog);
        fx.dialog->show();
        for(const char* name : {"transferDetailName", "transferDetailRemaining", "transferDetailItems", "transferPercent"}) {
            auto* label = fx.child<QLabel>(name);
            QVERIFY2(label, name);
            QCOMPARE(label->textFormat(), Qt::PlainText);
        }
        auto* nameLabel = fx.child<QLabel>("transferDetailName");
        QCOMPARE(nameLabel->text(), QStringLiteral("Nome: ") + evil);
        // Rendered as plain text the label is as wide as the literal markup; rich text would strip the tags.
        QVERIFY(nameLabel->sizeHint().width() >= nameLabel->fontMetrics().horizontalAdvance(nameLabel->text()) - 2);
        QVERIFY(nameLabel->text().contains(QLatin1String("<a href")));
        // The summary keeps escaping names and only contains the two links generated by the code.
        auto* summary = fx.child<QLabel>("transferSummary");
        QCOMPARE(summary->textFormat(), Qt::RichText);
        QVERIFY2(summary->text().contains(QStringLiteral("&lt;i&gt;dir")), qPrintable(summary->text()));
        QVERIFY(!summary->text().contains(QLatin1String("<i>")));
        QCOMPARE(summary->text().count(QLatin1String("<a ")), 2);
    }

    void transferDialogSpeedGraphOnlyContainsRatesFromRealBytes() {
        TransferDialogFixture fx;
        QVERIFY(fx.dialog);
        QVERIFY(fx.dialog->speedSamples().isEmpty());
        // Not byte observations: no sample may appear from percent/ETA/file name.
        fx.dialog->setPercent(50);
        fx.dialog->setRemainingTime(10);
        fx.dialog->setCurFile(QStringLiteral("/x/y.bin"));
        QVERIFY(fx.dialog->speedSamples().isEmpty());
        constexpr std::uint64_t total = 10'000'000;
        QVERIFY(fx.dialog->recordSpeedSample(0, total, 0) == false);          // first point has no interval
        QVERIFY(fx.dialog->recordSpeedSample(1'000'000, total, 1000));
        QVERIFY(fx.dialog->recordSpeedSample(3'000'000, total, 2000));
        QVERIFY(!fx.dialog->recordSpeedSample(3'000'000, total, 3000));       // no bytes moved: paused, no invented zero
        QVERIFY(!fx.dialog->recordSpeedSample(4'000'000, total, 2000));       // time did not advance
        QVERIFY(!fx.dialog->recordSpeedSample(2'000'000, total, 4000));       // bytes went backwards
        const auto samples = fx.dialog->speedSamples();
        QCOMPARE(samples.size(), 2);
        QCOMPARE(samples[0].fraction, 0.1);
        QCOMPARE(samples[0].bytesPerSecond, 1'000'000.0);
        QCOMPARE(samples[1].fraction, 0.3);
        QCOMPARE(samples[1].bytesPerSecond, 2'000'000.0);
        // setDataTransferred (the libfm feed) records through the same real-byte path.
        // Deterministic: the dialog's clock is injected, no real time passes (no qWait).
        TransferDialogFixture live;
        qint64 nowMs = 0;
        live.dialog->setClockForTesting([&nowMs] { return nowMs; });
        live.dialog->setDataTransferred(1'000, 10'000);
        nowMs = 500;
        live.dialog->setDataTransferred(5'000, 10'000);
        QCOMPARE(live.dialog->speedSamples().size(), 1);
        QCOMPARE(live.dialog->speedSamples().first().bytesPerSecond, 8'000.0);   // 4000 B in 0.5 s
        QCOMPARE(live.dialog->speedSamples().first().fraction, 0.5);
    }

    // Review C1: after Retomar the baseline restarts, so the first observation after a pause is not turned
    // into a falsely low rate averaged over the whole pause.
    void transferDialogSpeedBaselineRestartsOnResume() {
        TransferDialogFixture fx;
        QVERIFY(fx.dialog);
        fx.dialog->setPauseAvailable(true);
        constexpr std::uint64_t total = 10'000'000;
        fx.dialog->recordSpeedSample(0, total, 0);
        QVERIFY(fx.dialog->recordSpeedSample(1'000'000, total, 1000));
        fx.dialog->applyTransferState(ExplorerTransferController::State::Paused, true);
        fx.dialog->applyTransferState(ExplorerTransferController::State::Running, false);   // Retomar, 60 s later
        QVERIFY(!fx.dialog->recordSpeedSample(1'100'000, total, 61'000));                  // new baseline, no sample
        QVERIFY(fx.dialog->recordSpeedSample(2'100'000, total, 62'000));
        const auto samples = fx.dialog->speedSamples();
        QCOMPARE(samples.size(), 2);
        QCOMPARE(samples[1].bytesPerSecond, 1'000'000.0);
        QVERIFY(samples[1].fraction > samples[0].fraction);
    }

    // Review C2: a stall (no new bytes) must not advance the baseline clock; the next rate is the honest
    // average over the whole interval, not the inflated rate of the last tick alone.
    void transferDialogSpeedAfterStallIsAveragedOverTheStall() {
        TransferDialogFixture fx;
        QVERIFY(fx.dialog);
        constexpr std::uint64_t total = 10'000'000;
        fx.dialog->recordSpeedSample(0, total, 0);
        QVERIFY(fx.dialog->recordSpeedSample(1'000'000, total, 1000));
        QVERIFY(!fx.dialog->recordSpeedSample(1'000'000, total, 5000));   // 4 s without a byte
        QVERIFY(fx.dialog->recordSpeedSample(2'000'000, total, 6000));    // 1 MB over the 5 s since the last advance
        const auto samples = fx.dialog->speedSamples();
        QCOMPARE(samples.size(), 2);
        QCOMPARE(samples[1].bytesPerSecond, 200'000.0);
    }

    // Review C3: counters that go backwards or a total that changes never produce a non-monotonic series.
    void transferDialogSpeedSeriesStaysMonotonicWhenCountersMisbehave() {
        TransferDialogFixture fx;
        QVERIFY(fx.dialog);
        constexpr std::uint64_t total = 10'000'000;
        fx.dialog->recordSpeedSample(0, total, 0);
        QVERIFY(fx.dialog->recordSpeedSample(4'000'000, total, 1000));
        QVERIFY(fx.dialog->recordSpeedSample(5'000'000, total, 2000));
        QVERIFY(!fx.dialog->recordSpeedSample(1'000'000, total, 3000));   // counter restarted: rebase, no sample
        QVERIFY(!fx.dialog->recordSpeedSample(2'000'000, total, 4000));   // below the plotted fraction: dropped
        QVERIFY(fx.dialog->recordSpeedSample(6'000'000, total, 5000));    // advances past it again
        auto samples = fx.dialog->speedSamples();
        for(int i = 1; i < samples.size(); ++i)
            QVERIFY2(samples[i].fraction >= samples[i - 1].fraction, "fraction went backwards");
        QCOMPARE(samples.last().fraction, 0.6);
        QCOMPARE(samples.last().bytesPerSecond, 4'000'000.0);             // 1 MB -> ... measured from the rebase point
        // A different total changes the x scale: the old series is discarded instead of mixed in.
        QVERIFY(!fx.dialog->recordSpeedSample(7'000'000, 20'000'000, 6000));
        QVERIFY(fx.dialog->speedSamples().isEmpty());
        QVERIFY(fx.dialog->recordSpeedSample(9'000'000, 20'000'000, 7000));
        samples = fx.dialog->speedSamples();
        QCOMPARE(samples.size(), 1);
        QCOMPARE(samples[0].fraction, 0.45);
    }

    // Review C5: the sample vector is bounded; compaction keeps the shape (monotonic x, bytes conserved).
    void transferDialogSpeedSamplesAreBoundedByDecimation() {
        TransferDialogFixture fx;
        QVERIFY(fx.dialog);
        constexpr std::uint64_t total = 3'000'000;
        fx.dialog->recordSpeedSample(0, total, 0);
        for(int i = 1; i <= 3000; ++i)
            fx.dialog->recordSpeedSample(std::uint64_t(i) * 1000, total, qint64(i) * 100);   // 10 kB/s steadily
        const auto samples = fx.dialog->speedSamples();
        QVERIFY2(samples.size() <= 512, qPrintable(QString::number(samples.size())));
        QVERIFY(samples.size() > 100);
        double bytes = 0;
        for(int i = 0; i < samples.size(); ++i) {
            if(i) QVERIFY(samples[i].fraction >= samples[i - 1].fraction);
            QVERIFY(qAbs(samples[i].bytesPerSecond - 10'000.0) < 1.0);
            bytes += samples[i].bytesPerSecond * samples[i].intervalSeconds;
        }
        QCOMPARE(samples.last().fraction, 1.0);
        QVERIFY(qAbs(bytes - 3'000'000.0) < 1.0);                        // compaction conserves the bytes
    }

    // Review C4: the curve starts at the first REAL sample; nothing is drawn to its left (no invented plateau).
    void transferDialogSpeedGraphDoesNotInventSeriesLeftOfFirstSample() {
        TransferDialogFixture fx;
        QVERIFY(fx.dialog);
        fx.dialog->setPauseAvailable(true);
        fx.dialog->show();
        QTest::mouseClick(fx.child<QPushButton>("transferDetailsButton"), Qt::LeftButton);
        constexpr std::uint64_t total = 10'000'000;
        fx.dialog->setPercent(30);
        fx.dialog->recordSpeedSample(0, total, 0);
        fx.dialog->recordSpeedSample(1'000'000, total, 1000);           // first real sample at 10 % -> x ~ 40
        fx.dialog->recordSpeedSample(3'000'000, total, 2000);
        fx.dialog->applyTransferState(ExplorerTransferController::State::Paused, true);
        auto* graph = fx.child<QWidget>("transferSpeedGraph");
        const QImage img = graph->grab().toImage();
        int left = 0, right = 0;
        for(int y = 0; y < img.height(); ++y)
            for(int x = 0; x < img.width(); ++x)
                if(QColor(img.pixel(x, y)) == QColor(252, 225, 0)) (x < 38 ? left : right)++;
        QCOMPARE(left, 0);
        QVERIFY(right > 200);
    }

    void transferDialogSpeedGraphRendersEmptyBaselineThenYellowAreaWhenPaused() {
        TransferDialogFixture fx;
        QVERIFY(fx.dialog);
        fx.dialog->setPauseAvailable(true);
        fx.dialog->show();
        auto* more = fx.child<QPushButton>("transferDetailsButton");
        QTest::mouseClick(more, Qt::LeftButton);
        auto* graph = fx.child<QWidget>("transferSpeedGraph");
        QVERIFY(graph && graph->isVisible());
        QCOMPARE(graph->size(), QSize(395, 80));
        const auto countColour = [&](const QColor& c) {
            const QImage img = graph->grab().toImage();
            int n = 0;
            for(int y = 0; y < img.height(); ++y)
                for(int x = 0; x < img.width(); ++x)
                    if(QColor(img.pixel(x, y)) == c) ++n;
            return n;
        };
        QCOMPARE(countColour(QColor(252, 225, 0)), 0);              // no data -> no curve at all
        QCOMPARE(countColour(QColor(96, 205, 255)), 0);
        constexpr std::uint64_t total = 10'000'000;
        fx.dialog->setPercent(30);
        fx.dialog->recordSpeedSample(0, total, 0);
        fx.dialog->recordSpeedSample(1'000'000, total, 1000);
        fx.dialog->recordSpeedSample(2'000'000, total, 2000);
        fx.dialog->recordSpeedSample(3'000'000, total, 3000);
        fx.dialog->applyTransferState(ExplorerTransferController::State::Paused, true);
        QVERIFY(countColour(QColor(252, 225, 0)) > 500);            // yellow curve while paused
        QCOMPARE(countColour(QColor(96, 205, 255)), 0);
        const QImage img = graph->grab().toImage();
        QCOMPARE(QColor(img.pixel(img.width() - 3, img.height() / 2)), QColor(44, 44, 44));   // beyond progress: plain background
    }

    // Measured in frame B_pre_cmd2 (paused 32 %, expanded): the 1 px #575757 graph border has a 1 px
    // #383838 inner ring on all four sides (graph-local x=1, x=w-2, y=1, y=h-2), also over the tinted area.
    void transferDialogGraphHasMeasuredInnerRing() {
        TransferDialogFixture fx;
        QVERIFY(fx.dialog);
        fx.dialog->setPauseAvailable(true);
        fx.dialog->show();
        QTest::mouseClick(fx.child<QPushButton>("transferDetailsButton"), Qt::LeftButton);
        constexpr std::uint64_t total = 10'000'000;
        fx.dialog->setPercent(30);
        fx.dialog->recordSpeedSample(0, total, 0);
        fx.dialog->recordSpeedSample(1'000'000, total, 1000);
        fx.dialog->recordSpeedSample(3'000'000, total, 2000);
        fx.dialog->applyTransferState(ExplorerTransferController::State::Paused, true);
        auto* graph = fx.child<QWidget>("transferSpeedGraph");
        QVERIFY(graph && graph->isVisible());
        const QImage img = graph->grab().toImage();
        const QColor ring(56, 56, 56);
        QCOMPARE(QColor(img.pixel(0, 40)), QColor(87, 87, 87));                 // outer border unchanged
        QCOMPARE(QColor(img.pixel(1, 40)), ring);                               // left ring, over the tinted area
        QCOMPARE(QColor(img.pixel(img.width() - 2, 40)), ring);                 // right ring
        QCOMPARE(QColor(img.pixel(300, 1)), ring);                              // top ring
        QCOMPARE(QColor(img.pixel(300, img.height() - 2)), ring);               // bottom ring
    }

    // Measured (B_pre_cmd2): four interior horizontal grid lines at graph-local y 17, 32, 48, 63 (outside
    // the progress tint), none directly under the top/bottom rings (the old layout drew 6 lines every 15 px
    // starting at y=2, i.e. extra lines at y=2 and y=77 and a 1 px shifted 3rd/4th line).
    void transferDialogGraphHorizontalGridMatchesMeasuredRows() {
        TransferDialogFixture fx;
        QVERIFY(fx.dialog);
        fx.dialog->setPauseAvailable(true);
        fx.dialog->show();
        QTest::mouseClick(fx.child<QPushButton>("transferDetailsButton"), Qt::LeftButton);
        constexpr std::uint64_t total = 10'000'000;
        fx.dialog->setPercent(30);
        fx.dialog->recordSpeedSample(0, total, 0);
        fx.dialog->recordSpeedSample(1'000'000, total, 1000);
        fx.dialog->applyTransferState(ExplorerTransferController::State::Paused, true);
        auto* graph = fx.child<QWidget>("transferSpeedGraph");
        QVERIFY(graph && graph->isVisible());
        const QImage img = graph->grab().toImage();
        QList<int> rows;                                    // column 300: right of the tint, between vertical lines
        for(int y = 2; y < img.height() - 2; ++y)
            if(QColor(img.pixel(300, y)) != QColor(44, 44, 44)) rows << y;
        QCOMPARE(rows, (QList<int>{17, 32, 48, 63}));
    }

    // Qt renders ToolTip text as rich text whenever it looks like HTML (QLabel::setTextFormat does not apply),
    // so a tooltip carrying a raw file name is an injection vector (second review, B).
    static QString tooltipPlainText(const QString& tip) {
        QTextDocument doc;
        doc.setHtml(tip);
        return doc.toPlainText();
    }

    // Second-review blocking finding: ZWJ/ZWNJ are Cf but are part of legitimate names (composed emoji, Persian
    // and Indic scripts) and cannot reorder text, so they must survive; accents and CJK are untouched.
    void transferDialogKeepsLegitimateFileNamesIntact() {
        TransferDialogFixture fx;
        QVERIFY(fx.dialog);
        const QString legit = QStringLiteral("caf\u00e9-\u65e5\u672c\u8a9e-")
                              + QStringLiteral("\U0001F468\u200D\U0001F469\u200D\U0001F467")   // family emoji (ZWJ)
                              + QStringLiteral("-\u0645\u06cc\u200C\u062e\u0648\u0627\u0647\u0645.txt");   // Persian (ZWNJ)
        fx.dialog->setCurFile(QStringLiteral("/tmp/") + legit);
        auto* label = fx.child<QLabel>("transferDetailName");
        QVERIFY(label);
        QVERIFY2(label->text() == QStringLiteral("Nome: ") + legit, qPrintable(label->text()));
        QVERIFY(!label->text().contains(QChar(0xFFFD)));
        QVERIFY(Qt::mightBeRichText(label->toolTip()));
    }

    void transferDialogToolTipsAndLabelsNeutraliseHostileFileNames() {
        const QString hostile = QStringLiteral("x<img src=\"foto.png\"><b>y&amp;")
                                + QChar(0x202E) + QStringLiteral("cod.exe") + QChar(0x2066) + QStringLiteral("a\nb\tc");
        TransferDialogFixture fx(Fm::FileOperation::Copy, QStringLiteral("f.bin"), hostile, true);
        QVERIFY(fx.dialog);
        fx.dialog->setDestPath(Fm::FilePath::fromLocalPath(QByteArray(QByteArray("/tmp/") + hostile.toUtf8()).constData()));
        fx.dialog->setCurFile(QStringLiteral("/tmp/") + hostile);
        fx.dialog->setPrepared();
        const QString literal = QStringLiteral("x<img src=\"foto.png\"><b>y&amp;");
        for(const char* name : {"transferSummary", "transferDetailName"}) {
            auto* label = fx.child<QLabel>(name);
            QVERIFY(label);
            const QString tip = label->toolTip();
            QVERIFY2(!tip.isEmpty(), name);
            // The tooltip is rich text whose RENDERING is the literal name: no <img>, no <b> element.
            QTextDocument doc;
            doc.setHtml(tip);
            QVERIFY2(tip.contains(QStringLiteral("&lt;img")), qPrintable(QString::fromLatin1(name) + QStringLiteral(" tip=") + tip));
            QVERIFY2(doc.toPlainText().contains(literal), qPrintable(QString::fromLatin1(name) + doc.toPlainText()));
            QVERIFY2(!tip.contains(QStringLiteral("<img")), name);
            // No bidi/format/control characters survive in either the tooltip or the label text.
            const QString shown = tooltipPlainText(tip) + label->text();
            for(const QChar c : shown)
                QVERIFY2(!(c == QLatin1Char('\n') || c == QLatin1Char('\t') || c == QChar(0x202E) || c == QChar(0x2066)),
                         qPrintable(QString::fromLatin1(name) + QStringLiteral(" has U+%1").arg(c.unicode(), 4, 16)));
        }
    }

    // Review E (partial): a very long current file name is elided to the label width; tooltip has the full text.
    void transferDialogLongFileNameIsElidedWithFullTooltip() {
        TransferDialogFixture fx;
        QVERIFY(fx.dialog);
        const QString longName = QString(300, QLatin1Char('x')) + QStringLiteral(".bin");
        fx.dialog->setCurFile(longName);
        auto* label = fx.child<QLabel>("transferDetailName");
        QVERIFY(label);
        QVERIFY(label->text().contains(QChar(0x2026)));
        QVERIFY(label->fontMetrics().horizontalAdvance(label->text()) <= 393);
        QVERIFY(tooltipPlainText(label->toolTip()).endsWith(longName));
        QCOMPARE(label->geometry().width(), 393);
        fx.dialog->setCurFile(QStringLiteral("curto.txt"));
        QCOMPARE(label->text(), QStringLiteral("Nome: curto.txt"));
    }

    // Review E (final): the HTML summary elides the NAMES (escaped afterwards) and keeps the full text in the tooltip.
    void transferDialogSummaryElidesLongNamesKeepingHtmlEscapeAndFullTooltip() {
        const QString longDir = QStringLiteral("<b>&") + QString(200, QLatin1Char('y'));
        TransferDialogFixture fx(Fm::FileOperation::Copy, QStringLiteral("f.bin"), longDir, true);
        QVERIFY(fx.dialog);
        const QString longDest = QStringLiteral("<i>") + QString(220, QLatin1Char('d'));
        fx.dialog->setDestPath(Fm::FilePath::fromLocalPath(QByteArray(QByteArray("/tmp/") + longDest.toUtf8()).constData()));
        fx.dialog->setPrepared();
        auto* summary = fx.child<QLabel>("transferSummary");
        QVERIFY(summary);
        QTextDocument doc;
        doc.setHtml(summary->text());
        QVERIFY2(!summary->text().contains(QStringLiteral("<b>&")), "name must be HTML-escaped");
        QVERIFY(summary->text().contains(QStringLiteral("&lt;b&gt;&amp;")));
        QVERIFY(doc.toPlainText().contains(QChar(0x2026)));
        QFont f = summary->font();
        f.setPixelSize(12);
        QVERIFY2(QFontMetrics(f).horizontalAdvance(doc.toPlainText()) <= 393, qPrintable(doc.toPlainText()));
        QVERIFY(tooltipPlainText(summary->toolTip()).contains(longDir));
        QVERIFY(tooltipPlainText(summary->toolTip()).contains(longDest));
        QCOMPARE(summary->geometry().width(), 393);
    }

    // Review F: drawn buttons stay out of the default-button logic; the focus ring is drawn only for keyboard focus.
    void transferDialogKeyboardFocusOrderAndRingOnlyForKeyboard() {
        TransferDialogFixture fx(Fm::FileOperation::Copy, QStringLiteral("fixture3g.bin"), QString(), true);
        QVERIFY(fx.dialog);
        fx.dialog->show();
        QVERIFY(QTest::qWaitForWindowExposed(fx.dialog.get()));
        fx.dialog->activateWindow();
        auto* pause = fx.child<QPushButton>("pauseButton");
        auto* cancel = fx.child<QPushButton>("transferCancelButton");
        auto* more = fx.child<QPushButton>("transferDetailsButton");
        QVERIFY(pause->isVisible());
        for(auto* b : {pause, cancel, more}) {
            QVERIFY(!b->autoDefault());
            QVERIFY(!b->isDefault());
            QCOMPARE(b->focusPolicy(), Qt::TabFocus);
        }
        const QImage restPause = pause->grab().toImage(), restCancel = cancel->grab().toImage(),
                     restMore = more->grab().toImage();
        QVERIFY(QApplication::focusWidget() != pause);
        QList<QWidget*> order;
        pause->setFocus(Qt::TabFocusReason);
        order << QApplication::focusWidget();
        for(int i = 0; i < 2; ++i) {
            QTest::keyClick(QApplication::focusWidget(), Qt::Key_Tab);   // real Tab navigation
            order << QApplication::focusWidget();
        }
        QCOMPARE(order, (QList<QWidget*>{pause, cancel, more}));     // pausa -> ✕ -> detalhes
        // Focus ring present with keyboard focus (differs from rest), absent again after focus is lost.
        QVERIFY(more->hasFocus());
        QVERIFY(more->grab().toImage() != restMore);
        cancel->setFocus(Qt::TabFocusReason);
        QVERIFY(cancel->grab().toImage() != restCancel);
        QVERIFY(more->grab().toImage() == restMore);
        pause->setFocus(Qt::TabFocusReason);
        QVERIFY(pause->grab().toImage() != restPause);
        pause->clearFocus();
        cancel->clearFocus();
        QCOMPARE(pause->grab().toImage(), restPause);
        QCOMPARE(cancel->grab().toImage(), restCancel);
        // Mouse/other focus reasons never draw the ring.
        cancel->setFocus(Qt::MouseFocusReason);
        QCOMPARE(cancel->grab().toImage(), restCancel);
        cancel->clearFocus();
    }

    // Esc (and Enter on a focused drawn button) behave: Esc cancels exactly once without confirmation, Enter is inert.
    void transferDialogEscCancelsOnceAndEnterDoesNotActivateDrawnButtons() {
        TransferDialogFixture fx(Fm::FileOperation::Copy, QStringLiteral("fixture3g.bin"), QString(), true);
        QVERIFY(fx.dialog);
        fx.dialog->show();
        QVERIFY(QTest::qWaitForWindowExposed(fx.dialog.get()));
        auto* controller = fx.dialog->findChild<ExplorerTransferController*>();
        QVERIFY(controller);
        QSignalSpy requested(controller, &ExplorerTransferController::cancellationRequested);
        auto* cancel = fx.child<QPushButton>("transferCancelButton");
        cancel->setFocus(Qt::TabFocusReason);
        QTest::keyClick(cancel, Qt::Key_Return);
        QTest::keyClick(cancel, Qt::Key_Enter);
        QCOMPARE(requested.count(), 0);
        QTest::keyClick(fx.dialog.get(), Qt::Key_Escape);
        QTest::keyClick(fx.dialog.get(), Qt::Key_Escape);
        QCOMPARE(requested.count(), 1);
        for(QWidget* w : QApplication::topLevelWidgets())
            QVERIFY2(!(qobject_cast<QMessageBox*>(w) && w->isVisible()), "Esc opened a confirmation box");
        QVERIFY(fx.dialog->windowTitle().startsWith(QStringLiteral("Cancelando")));
    }

    // Review H: measured grid colours (paused): vertical line outside the tint 60,58,40; horizontal line over
    // the tint 89,83,34 (the old blend gave 61,59,41 / 90,84,34).
    void transferDialogGraphGridColoursMatchMeasuredWhenPaused() {
        TransferDialogFixture fx;
        QVERIFY(fx.dialog);
        fx.dialog->setPauseAvailable(true);
        fx.dialog->show();
        QTest::mouseClick(fx.child<QPushButton>("transferDetailsButton"), Qt::LeftButton);
        constexpr std::uint64_t total = 10'000'000;
        fx.dialog->setPercent(30);
        fx.dialog->recordSpeedSample(0, total, 0);
        fx.dialog->recordSpeedSample(1'000'000, total, 1000);
        fx.dialog->applyTransferState(ExplorerTransferController::State::Paused, true);
        const QImage img = fx.child<QWidget>("transferSpeedGraph")->grab().toImage();
        QCOMPARE(QColor(img.pixel(5, 17)), QColor(89, 83, 34));                   // horizontal, over the tint
        const int vx = int(img.width() * 6 / 10.0 + 0.5);                          // vertical, past the tint (30 %)
        QCOMPARE(QColor(img.pixel(vx, 3)), QColor(60, 58, 40));
    }

    void transferDialogKeepsVerbsAndNoPauseForNonCopyOperations() {
        const struct { Fm::FileOperation::Type type; const char* verb; } cases[] = {
            {Fm::FileOperation::Move, "Movendo"}, {Fm::FileOperation::Trash, "Movendo para a lixeira"},
            {Fm::FileOperation::Delete, "Excluindo"}, {Fm::FileOperation::Copy, "Copiando"}};
        for(const auto& c : cases) {
            TransferDialogFixture fx(c.type);
            QVERIFY(fx.dialog);
            QVERIFY2(fx.child<QLabel>("transferSummary")->text().startsWith(QString::fromUtf8(c.verb)),
                     qPrintable(fx.child<QLabel>("transferSummary")->text()));
            QVERIFY(!fx.child<QPushButton>("pauseButton")->isVisibleTo(fx.dialog.get()));
        }
    }
    // Found by the capture test: the content widget was laid out with height()==0 in the
    // collapsed state (dialog blank until "Mais detalhes" was toggled once).
    void transferDialogContentFillsDialogWhenFirstShownCollapsed() {
        TransferDialogFixture fx;
        QVERIFY(fx.dialog);
        fx.dialog->setPauseAvailable(true);
        fx.dialog->show();
        QVERIFY(QTest::qWaitForWindowExposed(fx.dialog.get()));
        QCoreApplication::processEvents();
        QCOMPARE(fx.child<QWidget>("transferContent")->geometry(), QRect(0, 0, 449, 165));
        const QImage img = fx.dialog->grab().toImage();
        QCOMPARE(QColor(img.pixel(10, 10)), QColor(44, 44, 44));    // measured body #2C2C2C, not the default window grey
        QCOMPARE(QColor(img.pixel(224, 120)), QColor(77, 77, 77));  // footer separator row (y 120)
    }

    // Capture only: with EXPLORER_TEST_ARTIFACTS set, renders the dialog (grab, DPR 1)
    // in the states of the measured Windows frames into <dir>/transfer-dialog/*.png;
    // without it nothing is written. Consumed by tools/fidelity/compare_transfer_dialog.py.
    void transferDialogCapturesMeasuredStatesWhenArtifactsRequested() {
        const QString artifacts = qEnvironmentVariable("EXPLORER_TEST_ARTIFACTS");
        if(artifacts.isEmpty())
            return;   // capture disabled: writes nothing, still passes
        const QString dir = QDir(artifacts).filePath(QStringLiteral("transfer-dialog"));
        QVERIFY(QDir().mkpath(dir));
        const auto save = [&](TransferDialogFixture& fx, const QString& name) {
            QCOMPARE(fx.dialog->devicePixelRatioF(), 1.0);
            QVERIFY(QTest::qWaitForWindowExposed(fx.dialog.get()));
            QCoreApplication::processEvents();   // let layout/stylesheet polish settle before grab()
            QImage img = fx.dialog->grab().toImage();
            QVERIFY(!img.isNull());
            QVERIFY2(img.save(QDir(dir).filePath(name)), qPrintable(name));
        };
        using State = ExplorerTransferController::State;
        constexpr std::uint64_t total = 3ULL << 30;   // 3 GiB, like the measured fixture
        {   // A: running, collapsed, 79 %
            TransferDialogFixture fx;
            QVERIFY(fx.dialog);
            fx.dialog->setPauseAvailable(true);
            fx.dialog->show();
            fx.dialog->setPercent(79);
            fx.dialog->applyTransferState(State::Running, false);
            save(fx, QStringLiteral("running-79.png"));
        }
        {   // B: paused 32 %, collapsed then expanded with real-counter speed samples
            TransferDialogFixture fx;
            QVERIFY(fx.dialog);
            fx.dialog->setPauseAvailable(true);
            fx.dialog->show();
            fx.dialog->setPercent(32);
            fx.dialog->applyTransferState(State::Paused, true);
            save(fx, QStringLiteral("paused-32.png"));
            // Byte observations 0 -> 32 % of 3 GiB at varying rates (recordSpeedSample = same path as libfm's feed).
            const double steps[] = {0.02, 0.05, 0.09, 0.14, 0.19, 0.23, 0.27, 0.30, 0.32};
            const qint64 dt[] = {0, 1000, 1000, 1000, 1000, 1000, 1000, 1000, 1000};
            qint64 t = 0;
            for(int i = 0; i < 9; ++i) {
                t += dt[i];
                fx.dialog->recordSpeedSample(std::uint64_t(steps[i] * double(total)), total, t);
            }
            QVERIFY(fx.dialog->speedSamples().size() >= 5);
            QTest::mouseClick(fx.child<QPushButton>("transferDetailsButton"), Qt::LeftButton);
            QVERIFY(fx.dialog->detailsExpanded());
            fx.dialog->setDataTransferred(std::uint64_t(0.32 * double(total)), total);
            save(fx, QStringLiteral("paused-32-expanded.png"));
        }
        {   // C: cancelling at 53 % (paused first, as in the measured sequence)
            TransferDialogFixture fx;
            QVERIFY(fx.dialog);
            fx.dialog->setPauseAvailable(true);
            fx.dialog->show();
            fx.dialog->setPercent(53);
            fx.dialog->applyTransferState(State::Paused, true);
            fx.dialog->applyTransferState(State::Cancelling, false);
            QCOMPARE(fx.dialog->windowTitle(), QStringLiteral("Cancelando - 53% concluído"));
            save(fx, QStringLiteral("cancelling-53.png"));
        }
    }

    void defaultSettingsMatchExplorerDensity() {
        PCManFM::Settings settings;
        settings.load(QStringLiteral("defaults-test"));
        QCOMPARE(settings.windowWidth(), 1120);
        QCOMPARE(settings.windowHeight(), 650);
        QCOMPARE(settings.smallIconSize(), 16);
        QCOMPARE(settings.sidePaneIconSize(), 16);
        QCOMPARE(settings.viewMode(), Fm::FolderView::DetailedListMode);
        QVERIFY(settings.switchToNewTab());
        QVERIFY(!settings.showMenuBar());
        QVERIFY(!settings.mountOnStartup());
        QVERIFY(!settings.mountRemovable());
        QVERIFY(!settings.autoRun());
    }
    void localFilterUsesTheRealFolderModel() {
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        for(const auto& name : {QStringLiteral("alpha.txt"), QStringLiteral("beta.txt")}) {
            QFile file(folder.filePath(name));
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("test\n");
        }
        QScopedPointer<PCManFM::MainWindow> window(new PCManFM::MainWindow(Fm::FilePath::fromLocalPath(folder.path().toUtf8().constData())));
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window.data()));
        auto* model = window->currentPage()->folderView()->model();
        QTRY_COMPARE(model->rowCount(), 2);
        auto* search = window->findChild<QLineEdit*>(QStringLiteral("explorerSearch"));
        QVERIFY(search);
        search->setFocus();
        QTest::keyClicks(search, QStringLiteral("alpha"));
        QTRY_COMPARE_WITH_TIMEOUT(model->rowCount(), 1, 1500);
        QCOMPARE(model->index(0,0).data().toString(), QStringLiteral("alpha.txt"));
        search->clear();
        QTRY_COMPARE(model->rowCount(), 2);
    }
    void unsupportedVolumeAutomationIsNotOffered() {
        PCManFM::PreferencesDialog preferences;
        for(const auto& name : {QStringLiteral("mountOnStartup"),QStringLiteral("mountRemovable"),QStringLiteral("autoRun")}) {
            auto* checkbox = preferences.findChild<QCheckBox*>(name);
            QVERIFY(checkbox);
            QVERIFY(!checkbox->isEnabled());
            QVERIFY(!checkbox->toolTip().isEmpty());
        }
    }
    void escapeClearsTheVisibleLocalFilter() {
        QTemporaryDir folder;
        QScopedPointer<PCManFM::MainWindow> window(new PCManFM::MainWindow(Fm::FilePath::fromLocalPath(folder.path().toUtf8().constData())));
        window->show(); QVERIFY(QTest::qWaitForWindowExposed(window.data()));
        auto* search = window->findChild<QLineEdit*>(QStringLiteral("explorerSearch"));
        search->setFocus(); search->setText(QStringLiteral("alpha"));
        QTest::keyClick(search,Qt::Key_Escape);
        QTRY_COMPARE(search->text(),QString());
        QCOMPARE(window->currentPage()->getFilterStr(),QString());
    }
    void filterStateFollowsTheActiveTab() {
        QTemporaryDir folder;
        auto path = Fm::FilePath::fromLocalPath(folder.path().toUtf8().constData());
        QScopedPointer<PCManFM::MainWindow> window(new PCManFM::MainWindow(path));
        window->show(); QVERIFY(QTest::qWaitForWindowExposed(window.data()));
        auto* search = window->findChild<QLineEdit*>(QStringLiteral("explorerSearch"));
        search->setText(QStringLiteral("alpha"));
        window->addTab(path);
        QCOMPARE(window->currentPage()->getFilterStr(),QString());
        QCOMPARE(search->text(),QString());
        auto* tabs = window->findChild<QTabBar*>(QStringLiteral("explorerTabs"));
        tabs->setCurrentIndex(0);
        QCOMPARE(search->text(),QStringLiteral("alpha"));
    }
    void nativeWindowIdentityIsDistinct() {
        QCOMPARE(qApp->applicationName(), QStringLiteral("win11-explorer-qt"));
        QCOMPARE(QGuiApplication::desktopFileName(), QStringLiteral("win11-explorer-qt"));
        QCOMPARE(QGuiApplication::applicationDisplayName(), QStringLiteral("Win11 Explorer Qt"));
    }
    void navigationAndTabsStayWiredAfterReparenting() {
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        QVERIFY(QDir(folder.path()).mkdir(QStringLiteral("child")));
        auto root = Fm::FilePath::fromLocalPath(folder.path().toUtf8().constData());
        auto child = Fm::FilePath::fromLocalPath(folder.filePath(QStringLiteral("child")).toUtf8().constData());
        QScopedPointer<PCManFM::MainWindow> window(new PCManFM::MainWindow(root));
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window.data()));
        auto* tabs = window->findChild<QTabBar*>(QStringLiteral("explorerTabs"));
        auto* add = window->findChild<QToolButton*>(QStringLiteral("explorerNewTab"));
        QVERIFY(tabs && add);
        QTest::mouseClick(add, Qt::LeftButton);
        QTRY_COMPARE(tabs->count(), 2);
        QCOMPARE(tabs->currentIndex(), 1);
        window->chdir(child);
        QTRY_VERIFY(window->currentPage()->path() == child);
        QTest::mouseClick(window->findChild<QToolButton*>(QStringLiteral("explorerUp")), Qt::LeftButton);
        QTRY_VERIFY(window->currentPage()->path() == root);
        QTest::mouseClick(window->findChild<QToolButton*>(QStringLiteral("explorerBack")), Qt::LeftButton);
        QTRY_VERIFY(window->currentPage()->path() == child);
        QTest::mouseClick(window->findChild<QToolButton*>(QStringLiteral("explorerForward")), Qt::LeftButton);
        QTRY_VERIFY(window->currentPage()->path() == root);
        tabs->setCurrentIndex(0);
        QCOMPARE(window->currentPage()->path(), root);
        auto* close = tabs->tabButton(1,QTabBar::RightSide);
        if(!close) close = tabs->tabButton(1,QTabBar::LeftSide);
        QVERIFY(close);
        QTest::mouseClick(close, Qt::LeftButton);
        QTRY_COMPARE(tabs->count(), 1);
        auto* maximize = window->findChild<QToolButton*>(QStringLiteral("explorerMaximize"));
        QTest::mouseClick(maximize, Qt::LeftButton);
        QTRY_VERIFY(window->isMaximized());
        QTest::mouseClick(maximize, Qt::LeftButton);
        QTRY_VERIFY(!window->isMaximized());
    }
    // D-02: opening a file with no associated application used to abort the whole
    // process (libmenu-cache g_error -> SIGTRAP) when the app chooser was built.
    void openingFileWithoutAssociatedAppDoesNotAbort() {
        GAppInfo* existing = g_app_info_get_default_for_type("application/octet-stream", FALSE);
        if(existing) {
            g_object_unref(existing);
            QSKIP("this environment has a default handler for application/octet-stream");
        }
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        const QString file = folder.filePath(QStringLiteral("x.zzqqunknown"));
        QFile f(file);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QByteArray("\x01\x02\x03\x04", 4));
        f.close();
        Fm::FilePathList list;
        list.push_back(Fm::FilePath::fromLocalPath(file.toUtf8().constData()));
        QTimer closer;
        connect(&closer, &QTimer::timeout, &closer, [] {
            for(QWidget* w : QApplication::topLevelWidgets()) {
                if(w->isVisible() && qobject_cast<QDialog*>(w)) w->close();
            }
        });
        closer.start(500);
        PCManFM::Launcher launcher(nullptr);
        launcher.launchPaths(nullptr, list);
        QVERIFY(true); // reaching this line means the process did not abort
    }
    // D-02 (deterministic, any environment): with the app chooser unavailable the launch is
    // reported with a non-blocking message instead of building the libmenu-cache view.
    void noAssociatedAppShowsNonBlockingMessageWhenChooserUnavailable() {
        GAppInfo* existing = g_app_info_get_default_for_type("application/octet-stream", FALSE);
        if(existing) {
            g_object_unref(existing);
            QSKIP("this environment has a default handler for application/octet-stream");
        }
        const auto restore = qScopeGuard([] { PCManFM::Launcher::setAppChooserAvailableForTest(-1); });
        PCManFM::Launcher::setAppChooserAvailableForTest(0);
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        const QString file = folder.filePath(QStringLiteral("y.zzqqunknown"));
        QFile f(file);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QByteArray("\x01\x02\x03\x04", 4));
        f.close();
        Fm::FilePathList list;
        list.push_back(Fm::FilePath::fromLocalPath(file.toUtf8().constData()));
        PCManFM::Launcher launcher(nullptr);
        QElapsedTimer t;
        t.start();
        launcher.launchPaths(nullptr, list);
        QVERIFY2(t.elapsed() < 3000, "launch must not wait for the user");
        QMessageBox* box = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((box = [] {
            for(QWidget* w : QApplication::topLevelWidgets())
                if(w->isVisible() && w->objectName() == QLatin1String("explorerNoAppMessage")) return qobject_cast<QMessageBox*>(w);
            return static_cast<QMessageBox*>(nullptr);
        }()) != nullptr, 3000);
        QVERIFY(box->text().contains(QStringLiteral("No application")));
        QVERIFY(!box->isModal() || box->windowModality() == Qt::NonModal);
        box->close();
    }
    void appChooserAvailabilityMatchesTheMenuCacheHelper() {
        // In this SDK libmenu-cache hardcodes /usr/lib/menu-cache/menu-cached.
        const bool helperPresent = QFileInfo(QStringLiteral("/usr/lib/menu-cache/menu-cached")).isExecutable();
        QCOMPARE(PCManFM::Launcher::appChooserAvailable(), helperPresent);
        PCManFM::Launcher::setAppChooserAvailableForTest(1);
        QVERIFY(PCManFM::Launcher::appChooserAvailable());
        PCManFM::Launcher::setAppChooserAvailableForTest(0);
        QVERIFY(!PCManFM::Launcher::appChooserAvailable());
        PCManFM::Launcher::setAppChooserAvailableForTest(-1);
    }
    // D-05 / C04
    void userPathsExpandTildeAndSimpleVariables() {
        const QString home = QDir::homePath();
        QCOMPARE(PCManFM::Application::expandUserPath(QStringLiteral("~")), home);
        QCOMPARE(PCManFM::Application::expandUserPath(QStringLiteral("~/Documentos")), home + QStringLiteral("/Documentos"));
        qputenv("P4_TEST_DIR", "/tmp/p4dir");
        QCOMPARE(PCManFM::Application::expandUserPath(QStringLiteral("$P4_TEST_DIR/x")), QStringLiteral("/tmp/p4dir/x"));
        QCOMPARE(PCManFM::Application::expandUserPath(QStringLiteral("${P4_TEST_DIR}")), QStringLiteral("/tmp/p4dir"));
        QCOMPARE(PCManFM::Application::expandUserPath(QStringLiteral("$P4_NOT_SET/x")), QStringLiteral("$P4_NOT_SET/x"));
        QCOMPARE(PCManFM::Application::expandUserPath(QStringLiteral("~other/x")), QStringLiteral("~other/x"));
        QCOMPARE(PCManFM::Application::expandUserPath(QStringLiteral("/a/~/b")), QStringLiteral("/a/~/b"));
        QCOMPARE(PCManFM::Application::expandUserPath(QStringLiteral("rel/$HOME")), QStringLiteral("rel/$HOME"));
        qunsetenv("P4_TEST_DIR");
    }
    void breadcrumbAddressEditExpandsTilde() {
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        auto root = Fm::FilePath::fromLocalPath(folder.path().toUtf8().constData());
        QScopedPointer<PCManFM::MainWindow> window(new PCManFM::MainWindow(root));
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window.data()));
        auto* bar = window->findChild<Fm::PathBar*>();
        QVERIFY(bar);
        QMetaObject::invokeMethod(bar, "openEditor");
        Fm::PathEdit* edit = nullptr;
        QTRY_VERIFY((edit = bar->findChild<Fm::PathEdit*>()) != nullptr);
        QTest::qWait(50);
        edit->setText(QStringLiteral("~"));
        QTest::keyClick(edit, Qt::Key_Return);
        QTRY_VERIFY(window->currentPage()->path() == Fm::FilePath::homeDir());
    }
    // D-11
    void settingsArePersistedSoonWithoutWaitingForQuit() {
        auto& app = *static_cast<PCManFM::Application*>(qApp);
        if(!app.isPrimary()) QSKIP("another instance owns the D-Bus service; only the primary persists settings");
        app.settings().setLastWindowWidth(1234);
        app.scheduleSettingsSave();
        const QString file = app.settings().profileDir(app.settings().profileName()) + QStringLiteral("/settings.conf");
        auto contains = [&file] {
            QFile f(file);
            return f.open(QIODevice::ReadOnly) && f.readAll().contains("LastWindowWidth=1234");
        };
        QTRY_VERIFY_WITH_TIMEOUT(contains(), 4000);
    }
    // C05: a failed address-bar navigation must leave the tab on the previous folder.
    void invalidAddressRestoresPreviousFolder() {
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        auto root = Fm::FilePath::fromLocalPath(folder.path().toUtf8().constData());
        QScopedPointer<PCManFM::MainWindow> window(new PCManFM::MainWindow(root));
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window.data()));
        QTRY_VERIFY(window->currentPage()->path() == root);
        bool sawErrorDialog = false;
        QTimer closer;
        connect(&closer, &QTimer::timeout, &closer, [&sawErrorDialog] {
            for(QWidget* w : QApplication::topLevelWidgets()) {
                if(w->isVisible() && qobject_cast<QMessageBox*>(w)) {
                    sawErrorDialog = true;
                    w->close();
                }
            }
        });
        closer.start(300);
        window->chdir(Fm::FilePath::fromPathStr("/nonexistent/zz-p4-crash-nav"));
        QTRY_VERIFY_WITH_TIMEOUT(sawErrorDialog, 5000); // the navigation really failed
        QTRY_VERIFY_WITH_TIMEOUT(window->currentPage()->path() == root, 3000);
        QVERIFY(!window->currentPage()->canBackward());
    }
    void toolbarCopyPasteAndRenameOperateOnRealFiles() {
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        QDir dir(folder.path());
        QVERIFY(dir.mkdir(QStringLiteral("source"))); QVERIFY(dir.mkdir(QStringLiteral("destination")));
        const auto sourceDir = folder.filePath(QStringLiteral("source"));
        const auto destinationDir = folder.filePath(QStringLiteral("destination"));
        QFile source(QDir(sourceDir).filePath(QStringLiteral("original.txt")));
        QVERIFY(source.open(QIODevice::WriteOnly)); source.write("engine-roundtrip"); source.close();
        QScopedPointer<PCManFM::MainWindow> window(new PCManFM::MainWindow(Fm::FilePath::fromLocalPath(sourceDir.toUtf8().constData())));
        window->show(); QVERIFY(QTest::qWaitForWindowExposed(window.data()));
        auto* view = window->currentPage()->folderView();
        QTRY_COMPARE(view->model()->rowCount(),1);
        view->childView()->setCurrentIndex(view->model()->index(0,0));
        QTRY_VERIFY(window->findChild<QToolButton*>(QStringLiteral("explorerCopy"))->isEnabled());
        QTest::mouseClick(window->findChild<QToolButton*>(QStringLiteral("explorerCopy")),Qt::LeftButton);
        const auto* clipboard = QApplication::clipboard()->mimeData();
        QVERIFY(clipboard && clipboard->hasUrls());
        QCOMPARE(clipboard->urls().first().toLocalFile(),source.fileName());
        auto destinationPath = Fm::FilePath::fromLocalPath(destinationDir.toUtf8().constData());
        window->chdir(destinationPath);
        QTRY_VERIFY(window->currentPage()->path() == destinationPath);
        auto* paste = window->findChild<QToolButton*>(QStringLiteral("explorerPaste"));
        QTRY_VERIFY(paste->isEnabled());
        QTest::mouseClick(paste,Qt::LeftButton);
        QFile copied(QDir(destinationDir).filePath(QStringLiteral("original.txt")));
        QTRY_VERIFY_WITH_TIMEOUT(copied.exists(),5000);
        QVERIFY(copied.open(QIODevice::ReadOnly)); QCOMPARE(copied.readAll(),QByteArray("engine-roundtrip")); copied.close();
        view = window->currentPage()->folderView();
        QTRY_COMPARE(view->model()->rowCount(),1);
        view->childView()->setCurrentIndex(view->model()->index(0,0));
        auto* rename = window->findChild<QToolButton*>(QStringLiteral("explorerRename"));
        QTRY_VERIFY(rename->isEnabled()); QTest::mouseClick(rename,Qt::LeftButton);
        QLineEdit* editor = nullptr;
        QTRY_VERIFY((editor = view->childView()->findChild<QLineEdit*>()));
        editor->setText(QStringLiteral("renamed.txt")); QTest::keyClick(editor,Qt::Key_Return);
        QTRY_VERIFY(QFile::exists(QDir(destinationDir).filePath(QStringLiteral("renamed.txt"))));
        QVERIFY(!QFile::exists(copied.fileName())); QVERIFY(QFile::exists(source.fileName()));
        QApplication::clipboard()->clear();
    }
    void placesDelegatePaintsReplacementNotModelArtwork() {
        class NamedIcon final : public QIconEngine {
        public:
            explicit NamedIcon(QString name) : name_(std::move(name)) {}
            QIconEngine* clone() const override { return new NamedIcon(name_); }
            QString iconName() override { return name_; }
            QString name_;
            void paint(QPainter* p, const QRect& r, QIcon::Mode, QIcon::State) override {
                p->fillRect(r, Qt::magenta);
            }
        };
        QTemporaryDir fixture;
        PCManFM::MainWindow window(Fm::FilePath::fromLocalPath(fixture.path().toUtf8().constData()));
        auto* side = window.findChild<QWidget*>(QStringLiteral("sidePane"));
        QVERIFY(side);
        auto trees = side->findChildren<QTreeView*>();
        QVERIFY(!trees.isEmpty());
        auto* tree = trees.first();
        auto* delegate = tree->itemDelegate();
        QStandardItemModel model(1, 1);
        model.setData(model.index(0,0), QUrl::fromLocalFile(fixture.path()), Qt::UserRole);
        const auto unchangedPath = model.index(0,0).data(Qt::UserRole);
        QStyleOptionViewItem option;
        option.initFrom(tree);
        option.rect = QRect(0,0,160,32);
        option.decorationSize = QSize(16,16);
        auto render = [&](QAbstractItemDelegate* d) {
            QImage image(160,32,QImage::Format_ARGB32);
            image.fill(Qt::white);
            QPainter painter(&image);
            d->paint(&painter, option, model.index(0,0));
            return image;
        };
        QStyledItemDelegate upstream;
        const QList<QPair<QString,QString>> places{
            {QStringLiteral("folder"), QStringLiteral("folder")},
            {QStringLiteral("user-home"), QStringLiteral("home")},
            {QStringLiteral("folder-download"), QStringLiteral("downloads")},
            {QStringLiteral("folder-documents"), QStringLiteral("documents")},
            {QStringLiteral("folder-pictures"), QStringLiteral("pictures")},
            {QStringLiteral("folder-music"), QStringLiteral("music")},
            {QStringLiteral("folder-videos"), QStringLiteral("videos")},
            {QStringLiteral("user-desktop"), QStringLiteral("desktop")},
            {QStringLiteral("computer"), QStringLiteral("computer")},
            {QStringLiteral("network-workgroup"), QStringLiteral("network")},
            {QStringLiteral("drive-optical"), QStringLiteral("optical-drive")},
            {QStringLiteral("user-trash"), QStringLiteral("trash")}};
        for(const auto& [sourceName, semanticRole] : places) {
            model.setData(model.index(0,0), QIcon(new NamedIcon(sourceName)), Qt::DecorationRole);
            const auto actual = render(delegate);
            QVERIFY2(actual != render(&upstream), qPrintable(sourceName));
            QCOMPARE(model.index(0,0).data(Qt::DecorationRole).value<QIcon>().name(), sourceName);
            QCOMPARE(model.index(0,0).data(Qt::UserRole), unchangedPath);
            const auto replacement = PCManFM::ExplorerIcons::icon(semanticRole);
            QVERIFY(!replacement.isNull());
            model.setData(model.index(0,0), replacement, Qt::DecorationRole);
            QCOMPARE(actual, render(&upstream));
        }
        model.setData(model.index(0,0), QIcon(new NamedIcon(QStringLiteral("unobserved-service"))), Qt::DecorationRole);
        QCOMPARE(render(delegate), render(&upstream));
    }
    void repeatedModeAndNavigationReuseOneLocalStyle() {
        QTemporaryDir fixture;
        QVERIFY(QDir(fixture.path()).mkdir(QStringLiteral("child")));
        const auto root = Fm::FilePath::fromLocalPath(fixture.path().toUtf8().constData());
        const auto nested = Fm::FilePath::fromLocalPath(fixture.filePath(QStringLiteral("child")).toUtf8().constData());
        QPointer<QStyle> shared = qApp->style();
        {
            PCManFM::MainWindow window(root);
            window.show();
            QVERIFY(QTest::qWaitForWindowExposed(&window));
            for(const auto mode : {Fm::FolderView::DetailedListMode, Fm::FolderView::IconMode,
                                   Fm::FolderView::DetailedListMode}) {
                window.currentPage()->setViewMode(mode);
                auto* view = window.currentPage()->folderView();
                auto* child = view->childView();
                QCOMPARE(child->findChildren<QProxyStyle*>(QString(), Qt::FindDirectChildrenOnly).size(), 1);
                QPointer<QProxyStyle> first = child->findChildren<QProxyStyle*>(QString(), Qt::FindDirectChildrenOnly).first();
                QVERIFY(child->style() != shared);
                for(int i=0; i<8; ++i) {
                    window.currentPage()->setViewMode(mode);
                    QCOMPARE(view->childView(), child);
                    QVERIFY(first);
                    QVERIFY(child->findChildren<QProxyStyle*>(QString(), Qt::FindDirectChildrenOnly).contains(first.data()));
                    QCOMPARE(child->findChildren<QProxyStyle*>(QString(), Qt::FindDirectChildrenOnly).size(), 1);
                    const auto destination = i % 2 ? root : nested;
                    window.chdir(destination);
                    QTRY_VERIFY(window.currentPage()->path() == destination);
                    // Folder settings can legitimately switch mode/recreate the
                    // child on navigation. Bound ownership on that child too.
                    child = view->childView();
                    QCOMPARE(child->findChildren<QProxyStyle*>(QString(), Qt::FindDirectChildrenOnly).size(), 1);
                    window.currentPage()->setViewMode(mode);
                    child = view->childView();
                    QCOMPARE(child->findChildren<QProxyStyle*>(QString(), Qt::FindDirectChildrenOnly).size(), 1);
                    first = child->findChildren<QProxyStyle*>(QString(), Qt::FindDirectChildrenOnly).first();
                }
                QCOMPARE(qApp->style(), shared.data());
            }
        }
        QVERIFY(shared);
        QCOMPARE(qApp->style(), shared.data());
    }
    void syntheticInputFailureCleanup() {
        class InputProbe : public QWidget {
        public:
            int releases = 0;
            bool pressed = false, ctrl = false, shift = false;
            void mousePressEvent(QMouseEvent*) override { pressed = true; }
            void mouseReleaseEvent(QMouseEvent*) override { pressed = false; ++releases; }
            void keyPressEvent(QKeyEvent* e) override {
                if(e->key() == Qt::Key_Control) ctrl = true;
                if(e->key() == Qt::Key_Shift) shift = true;
            }
            void keyReleaseEvent(QKeyEvent* e) override {
                if(e->key() == Qt::Key_Control) ctrl = false;
                if(e->key() == Qt::Key_Shift) shift = false;
            }
        } probe;
        probe.show();
        for(bool throws : {false, true}) {
            int attempts = 0;
            bool completed = false;
            try {
                completed = selectionModes([&](auto) {
                    ++attempts;
                    return checkedInputGesture([&](bool&) {
                        QTest::keyPress(&probe, Qt::Key_Control);
                        QTest::keyPress(&probe, Qt::Key_Shift);
                        QTest::mousePress(&probe, Qt::LeftButton, Qt::ControlModifier | Qt::ShiftModifier);
                        if(throws) throw std::runtime_error("injected after press");
                        return; // Same return path as a Qt assertion in a lambda.
                    }, [&] {
                        QTest::mouseRelease(&probe, Qt::LeftButton);
                        QTest::keyRelease(&probe, Qt::Key_Shift);
                        QTest::keyRelease(&probe, Qt::Key_Control);
                    });
                });
            } catch(const std::runtime_error&) {
                QVERIFY(throws);
            }
            QVERIFY(!completed);
            QCOMPARE(attempts, 1);
            QVERIFY(!probe.pressed);
            QVERIFY(!probe.ctrl && !probe.shift);
            QCOMPARE(probe.releases, throws ? 2 : 1);
        }
    }
    void rubberBandUsesObservedSelectionPixels() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        for(const auto& name : {QStringLiteral("Nested Notes.txt"), QStringLiteral("Readme.txt")}) {
            QFile file(fixture.filePath(name));
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("selection fixture\n");
        }
        QScopedPointer<PCManFM::MainWindow> window(new PCManFM::MainWindow(Fm::FilePath::fromLocalPath(fixture.path().toUtf8().constData())));
        window->resize(1104, 642);
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window.data()));
        auto* folderView = window->currentPage()->folderView();
        QTRY_COMPARE(folderView->model()->rowCount(), 2);
        QStyle* sharedApplicationStyle = qApp->style();
        QVERIFY(selectionModes([&](auto mode) {
            QWidget* viewport = nullptr;
            QPoint end;
            bool pressed = false;
            return checkedInputGesture([&](bool& complete) {
            window->currentPage()->setViewMode(mode);
            auto* child = folderView->childView();
            viewport = child->viewport();
            QTRY_VERIFY(viewport->width() > 200 && viewport->height() > 100);
            QVERIFY(child->style() != sharedApplicationStyle);
            QCOMPARE(qApp->style(), sharedApplicationStyle);
            const QPoint start(12, viewport->height() - 32);
            end = QPoint(190, viewport->height() - 10);
            pressed = true;
            QTest::mousePress(viewport, Qt::LeftButton, Qt::NoModifier, start);
            QTest::mouseMove(viewport, end, 20);
            QCoreApplication::processEvents();
            const auto image = viewport->grab().toImage().convertToFormat(QImage::Format_ARGB32);
            QCOMPARE(image.pixelColor((start.x() + end.x()) / 2, (start.y() + end.y()) / 2), QColor(170, 204, 238));
            QCOMPARE(image.pixelColor(start.x(), (start.y() + end.y()) / 2), QColor(0, 120, 212));
            QTest::mouseRelease(viewport, Qt::LeftButton, Qt::NoModifier, end);
            pressed = false;
            complete = true;
            }, [&] {
                if(pressed && viewport) QTest::mouseRelease(viewport, Qt::LeftButton, Qt::NoModifier, end);
            });
        }));
    }

    void rubberBandCompositesOverContent_data() {
        QTest::addColumn<int>("mode");
        QTest::newRow("details") << int(Fm::FolderView::DetailedListMode);
        QTest::newRow("grid") << int(Fm::FolderView::IconMode);
    }
    void rubberBandCompositesOverContent() {
        QFETCH(int, mode);
        class Markers final : public Fm::FolderItemDelegate {
        public:
            using Fm::FolderItemDelegate::FolderItemDelegate;
            void paint(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& index) const override {
                Fm::FolderItemDelegate::paint(p, opt, index);
                if(index.column() != 0) return;
                // Fixed colors independent of selected/hover state: the same
                // backing content must survive both real painting paths.
                p->fillRect(QRect(opt.rect.topLeft() + QPoint(12,4), QSize(12,8)), Qt::black);
                p->fillRect(QRect(opt.rect.topLeft() + QPoint(28,4), QSize(12,8)), Qt::white);
                p->fillRect(QRect(opt.rect.topLeft() + QPoint(44,4), QSize(12,8)), QColor(60,150,210));
            }
        };
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QStringList expected{QStringLiteral("Nested Notes.txt"), QStringLiteral("Readme.txt")};
        for(const auto& name : expected) {
            QFile file(fixture.filePath(name));
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("alpha regression\n");
        }
        PCManFM::MainWindow window(Fm::FilePath::fromLocalPath(fixture.path().toUtf8().constData()));
        window.resize(1104,642);
        window.currentPage()->setViewMode(static_cast<Fm::FolderView::ViewMode>(mode));
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto* view = window.currentPage()->folderView();
        QTRY_COMPARE(view->model()->rowCount(), 2);
        auto* child = view->childView();
        auto* original = qobject_cast<Fm::FolderItemDelegate*>(child->itemDelegateForColumn(0));
        QVERIFY(original);
        auto* markers = new Markers(child, child);
        markers->setItemSize(original->itemSize());
        markers->setIconSize(original->iconSize());
        markers->setMargins(original->getMargins());
        child->setItemDelegateForColumn(0, markers);
        auto* viewport = child->viewport();
        const auto index = view->model()->index(0,0);
        QTRY_VERIFY(child->visualRect(index).isValid());
        const auto origin = child->visualRect(index).topLeft();
        const QPoint black = origin + QPoint(16,8), white = origin + QPoint(32,8), color = origin + QPoint(48,8);
        QTest::qWait(50);
        const auto before = viewport->grab().toImage();
        QCOMPARE(before.pixelColor(black), QColor(Qt::black));
        QCOMPARE(before.pixelColor(white), QColor(Qt::white));
        QCOMPARE(before.pixelColor(color), QColor(60,150,210));
        const QPoint start(viewport->width()-8, viewport->height()-8), end(2,2);
        QTest::mousePress(viewport, Qt::LeftButton, Qt::NoModifier, start);
        const auto release = qScopeGuard([&] { QTest::mouseRelease(viewport, Qt::LeftButton, Qt::NoModifier, end); });
        QTest::mouseMove(viewport, end, 20);
        QTest::qWait(250);
        const auto held = viewport->grab().toImage();
        const auto artifacts = qEnvironmentVariable("EXPLORER_TEST_ARTIFACTS");
        const auto suffix = QString::fromLatin1(QTest::currentDataTag());
        if(!artifacts.isEmpty()) {
            QVERIFY(QDir().mkpath(artifacts));
            QVERIFY(before.save(QDir(artifacts).filePath(QStringLiteral("alpha-%1-before.png").arg(suffix))));
            QVERIFY(held.save(QDir(artifacts).filePath(QStringLiteral("alpha-%1-held.png").arg(suffix))));
        }
        QCOMPARE(held.pixelColor(black), QColor(0,34,68));
        QCOMPARE(held.pixelColor(white), QColor(170,204,238));
        QCOMPARE(held.pixelColor(color), QColor(40,134,208));
        QCOMPARE(held.pixelColor(end.x()+1, viewport->height()/2), QColor(0,120,212));
        const QPoint outside(viewport->width()-3, viewport->height()/2);
        QCOMPARE(held.pixelColor(outside), before.pixelColor(outside));
        QTest::mouseRelease(viewport, Qt::LeftButton, Qt::NoModifier, end);
        QTest::qWait(250);
        const auto released = viewport->grab().toImage();
        QCOMPARE(released.pixelColor(black), before.pixelColor(black));
        QCOMPARE(released.pixelColor(white), before.pixelColor(white));
        QCOMPARE(released.pixelColor(color), before.pixelColor(color));
        QCOMPARE(released.pixelColor(end.x()+1, viewport->height()/2), before.pixelColor(end.x()+1, viewport->height()/2));
        QStringList names;
        for(const auto& selected : view->selectionModel()->selectedIndexes())
            if(selected.column() == 0) names << selected.data().toString();
        names.sort();
        QCOMPARE(names, expected);
        if(!artifacts.isEmpty()) QVERIFY(released.save(QDir(artifacts).filePath(QStringLiteral("alpha-%1-released.png").arg(suffix))));
    }

    void localMovePolicyDoesNotClaimUnobservedDrops() {
        QTemporaryDir fixture;
        QFile source(fixture.filePath(QStringLiteral("source.txt")));
        QVERIFY(source.open(QIODevice::WriteOnly));
        source.write("drop fixture"); source.close();
        PCManFM::MainWindow window(Fm::FilePath::fromLocalPath(fixture.path().toUtf8().constData()));
        QTRY_COMPARE(window.currentPage()->folderView()->model()->rowCount(), 1);
        class Probe : public PCManFM::View {
        public:
            using PCManFM::View::eventFilter;
        } probe;
        probe.setModel(window.currentPage()->folderView()->model());
        probe.resize(800,500);
        probe.show();
        QVERIFY(QTest::qWaitForWindowExposed(&probe));
        QMimeData mime;
        const auto localUrl = QUrl::fromLocalFile(source.fileName());
        auto consumed = [&](Qt::MouseButtons buttons, Qt::KeyboardModifiers mods,
                            Qt::DropActions actions, const QList<QUrl>& urls) {
            mime.setUrls(urls);
            QDragMoveEvent event(QPoint(500,400), actions, &mime, buttons, mods);
            event.ignore();
            return probe.eventFilter(probe.childView()->viewport(), &event);
        };
        QVERIFY(consumed(Qt::LeftButton, Qt::NoModifier, Qt::CopyAction | Qt::MoveAction, {localUrl}));
        QVERIFY(!consumed(Qt::RightButton, Qt::NoModifier, Qt::CopyAction | Qt::MoveAction, {localUrl}));
        QVERIFY(!consumed(Qt::NoButton, Qt::NoModifier, Qt::CopyAction | Qt::MoveAction, {localUrl}));
        for(auto mods : {Qt::ControlModifier, Qt::ShiftModifier, Qt::AltModifier})
            QVERIFY(!consumed(Qt::LeftButton, mods, Qt::CopyAction | Qt::MoveAction, {localUrl}));
        QVERIFY(!consumed(Qt::LeftButton, Qt::NoModifier, Qt::CopyAction, {localUrl}));
        QVERIFY(!consumed(Qt::LeftButton, Qt::NoModifier, Qt::CopyAction | Qt::MoveAction, {}));
        QVERIFY(!consumed(Qt::LeftButton, Qt::NoModifier, Qt::CopyAction | Qt::MoveAction,
                          {QUrl::fromLocalFile(fixture.filePath(QStringLiteral("missing")))}));
        QVERIFY(!consumed(Qt::LeftButton, Qt::NoModifier, Qt::CopyAction | Qt::MoveAction,
                          {QUrl(QStringLiteral("sftp://invalid.example/owned-test"))}));
        // Read-only own-process procfs file: no foreign fixture or operation.
        const QString foreign = QStringLiteral("/proc/self/comm");
        QFile ownComm(foreign);
        QVERIFY(ownComm.open(QIODevice::ReadOnly));
        QVERIFY(!ownComm.readAll().isEmpty());
        struct stat a{}, b{};
        QVERIFY(::stat(source.fileName().toUtf8().constData(), &a) == 0);
        QVERIFY(::stat(foreign.toUtf8().constData(), &b) == 0);
        QVERIFY2(a.st_dev != b.st_dev, "Cross-filesystem regression needs a distinct procfs device");
        QVERIFY(!consumed(Qt::LeftButton, Qt::NoModifier, Qt::CopyAction | Qt::MoveAction,
                          {QUrl::fromLocalFile(foreign)}));

        auto leftMove = [&] {
            return consumed(Qt::LeftButton, Qt::NoModifier, Qt::CopyAction | Qt::MoveAction, {localUrl});
        };
        auto drop = [&](const QList<QUrl>& urls, Qt::KeyboardModifiers mods = Qt::NoModifier) {
            mime.setUrls(urls);
            QDropEvent event(QPointF(500,400), Qt::CopyAction | Qt::MoveAction, &mime, Qt::NoButton, mods);
            event.ignore();
            const bool claimed = probe.eventFilter(probe.childView()->viewport(), &event);
            if(claimed && (!event.isAccepted() || event.dropAction() != Qt::MoveAction))
                QTest::qFail("Claimed drop must explicitly accept Move", __FILE__, __LINE__);
            return claimed;
        };
        QVERIFY(!drop({localUrl}));
        QVERIFY(leftMove());
        QVERIFY(!drop({QUrl::fromLocalFile(fixture.path())})); // Different URLs consume record.
        QVERIFY(!drop({localUrl}));
        for(auto type : {QEvent::DragLeave, QEvent::DragEnter}) {
            QVERIFY(leftMove());
            QEvent boundary(type);
            probe.eventFilter(probe.childView()->viewport(), &boundary);
            QVERIFY(!drop({localUrl}));
        }
        for(auto mods : {Qt::ControlModifier, Qt::ShiftModifier, Qt::AltModifier}) {
            QVERIFY(leftMove());
            QVERIFY(!consumed(Qt::LeftButton, mods, Qt::CopyAction | Qt::MoveAction, {localUrl}));
            QVERIFY(!drop({localUrl}));
            QVERIFY(leftMove());
            QVERIFY(!drop({localUrl}, mods));
            QVERIFY(!drop({localUrl}));
        }
        QVERIFY(leftMove());
        QVERIFY(!consumed(Qt::RightButton, Qt::NoModifier, Qt::CopyAction | Qt::MoveAction, {localUrl}));
        QVERIFY(!drop({localUrl}));
        QVERIFY(leftMove());
        QVERIFY(drop({localUrl}));
        QVERIFY(!drop({localUrl})); // Single use after an eligible NoButton drop.
        // No event dispatch after accepting: probe destruction cancels its queued
        // FileOperation callback. These are state-policy tests, never transfers.
    }
    void qtRoutingGuardDoesNotConsumeQtTestArguments() {
        const bool wasSet = qEnvironmentVariableIsSet("QT_QPA_PLATFORM");
        const auto saved = qgetenv("QT_QPA_PLATFORM");
        const auto restore = qScopeGuard([&] {
            if(wasSet) qputenv("QT_QPA_PLATFORM", saved);
            else qunsetenv("QT_QPA_PLATFORM");
        });
        qputenv("QT_QPA_PLATFORM", "xcb");
        auto allowed = [](QList<QByteArray> args) {
            QList<char*> argv;
            for(auto& arg : args) argv.append(arg.data());
            return SelectionOwnership::qtRoutingAllowed(argv.size(), argv.data());
        };
        // Pure routing policy: these strings never reach XOpenDisplay or Qt.
        for(const auto& option : {QByteArray("-display"), QByteArray("--display"),
                                 QByteArray("-platform"), QByteArray("--platform"),
                                 QByteArray("-platformpluginpath"), QByteArray("--platformpluginpath")}) {
            QVERIFY(!allowed({"test", option, "unowned-route"}));
            QVERIFY(!allowed({"test", option + "=unowned-route"}));
            QVERIFY(!allowed({"test", option}));
        }
        QVERIFY(allowed({"test", "-o", "report.xml,junitxml", "-v1", "case:row"}));
        QVERIFY(allowed({"test", "-functions"}));
        QVERIFY(allowed({"test", "-datatags"}));
        for(const auto& platform : {QByteArray("offscreen"), QByteArray("xcb;offscreen"), QByteArray("xcb:display=unowned-route"), QByteArray()}) {
            qputenv("QT_QPA_PLATFORM", platform);
            QVERIFY(!allowed({"test"}));
        }
        qunsetenv("QT_QPA_PLATFORM");
        QVERIFY(!allowed({"test"}));
    }
    void nativeQtRoutingRejectsOverrides() {
        if(!qEnvironmentVariableIsSet("EXPLORER_ENABLE_XTEST")) QSKIP("Owned Xephyr required");
        QVERIFY(SelectionOwnership::verify());
        const QString target = qEnvironmentVariable("EXPLORER_XTEST_DISPLAY");
        const QList<QStringList> routes = {
            {QStringLiteral("-platform"), QStringLiteral("offscreen")},
            {QStringLiteral("--platform"), QStringLiteral("offscreen")},
            {QStringLiteral("-display"), target},
            {QStringLiteral("--display"), target},
            {QStringLiteral("-platformpluginpath"), QStringLiteral("/nonexistent")}
        };
        for(const auto& route : routes) {
            QProcess child;
            auto arguments = route;
            arguments << QStringLiteral("defaultSettingsMatchExplorerDensity");
            child.start(QCoreApplication::applicationFilePath(), arguments);
            QVERIFY(child.waitForFinished(5000));
            QCOMPARE(child.exitCode(), 2);
            QVERIFY(child.readAllStandardError().contains("XTEST Qt routing rejected"));
            QVERIFY(!child.readAllStandardOutput().contains("Start testing"));
        }
        for(const auto& platform : {QStringLiteral("offscreen"), QStringLiteral("xcb;offscreen"), QString()}) {
            QProcess child;
            auto environment = QProcessEnvironment::systemEnvironment();
            environment.insert(QStringLiteral("QT_QPA_PLATFORM"), platform);
            child.setProcessEnvironment(environment);
            child.start(QCoreApplication::applicationFilePath(), {QStringLiteral("defaultSettingsMatchExplorerDensity")});
            QVERIFY(child.waitForFinished(5000));
            QCOMPARE(child.exitCode(), 2);
            QVERIFY(child.readAllStandardError().contains("XTEST Qt routing rejected"));
        }
        QTemporaryDir reports;
        QVERIFY(reports.isValid());
        QProcess child;
        const auto report = reports.filePath(QStringLiteral("result.xml"));
        child.start(QCoreApplication::applicationFilePath(), {
            QStringLiteral("defaultSettingsMatchExplorerDensity"), QStringLiteral("-o"), report + QStringLiteral(",junitxml")});
        QVERIFY(child.waitForFinished(5000));
        QCOMPARE(child.exitCode(), 0);
        QVERIFY(QFile::exists(report));
    }
    void nativeOwnershipRejectsMismatches() {
        if(!qEnvironmentVariableIsSet("EXPLORER_ENABLE_XTEST")) QSKIP("Owned Xephyr required");
        QVERIFY(SelectionOwnership::verify());
        for(const auto* name : {"EXPLORER_XSERVER_START", "EXPLORER_XOWNER_START",
                               "EXPLORER_XSERVER_PID", "EXPLORER_XOWNER_PID"}) {
            const QByteArray saved = qgetenv(name);
            const auto restore = qScopeGuard([&] { qputenv(name, saved); });
            qputenv(name, "1");
            QVERIFY(!SelectionOwnership::verify());
        }
        QVERIFY(SelectionOwnership::verify());
        QTemporaryDir rejected;
        QVERIFY(rejected.isValid());
        // Assert distinctness before even starting the runner: a matching target
        // would start another WM instead of exercising its rejection path.
        const QString target = qEnvironmentVariable("EXPLORER_XTEST_DISPLAY");
        const QString mismatchedTarget = target == QStringLiteral(":2") ? QStringLiteral(":3") : QStringLiteral(":2");
        QVERIFY(mismatchedTarget != qEnvironmentVariable("EXPLORER_XTEST_DISPLAY"));
        QProcess mismatched;
        mismatched.start(QStringLiteral("/usr/bin/python3"), {
            QDir(QFileInfo(QString::fromUtf8(__FILE__)).absolutePath()).filePath(QStringLiteral("run_selection_x11.py")),
            QStringLiteral("--session"), QStringLiteral("--display"), mismatchedTarget,
            QStringLiteral("--binary"), QCoreApplication::applicationFilePath(),
            QStringLiteral("--artifacts"), rejected.path()});
        QVERIFY(mismatched.waitForFinished(5000));
        QCOMPARE(mismatched.exitCode(), 2);
        QVERIFY(mismatched.readAllStandardError().contains("does not match validated target"));
        QVERIFY(!QFile::exists(rejected.filePath(QStringLiteral("wm.log"))));
    }
    void nativeInputFailureCleanup() {
        if(!qEnvironmentVariableIsSet("EXPLORER_ENABLE_XTEST")) QSKIP("Owned Xephyr required");
        QVERIFY(SelectionOwnership::verify());
        QLibrary xtst(QStringLiteral("libXtst.so.6"));
        QVERIFY(xtst.load());
        using Button = int(*)(Display*, unsigned int, int, unsigned long);
        auto button = reinterpret_cast<Button>(xtst.resolve("XTestFakeButtonEvent"));
        auto key = reinterpret_cast<Button>(xtst.resolve("XTestFakeKeyEvent"));
        QVERIFY(button && key);
        Display* d = XOpenDisplay(nullptr);
        QVERIFY(d);
        const auto close = qScopeGuard([&] { XCloseDisplay(d); });
        QVERIFY(SelectionOwnership::verify(d));
        const auto ctrl = XKeysymToKeycode(d, XK_Control_L), shift = XKeysymToKeycode(d, XK_Shift_L);
        auto release = [&] {
            button(d, 1, False, 0);
            key(d, ctrl, False, 0);
            key(d, shift, False, 0);
            XSync(d, False);
        };
        const auto emergencyRelease = qScopeGuard(release);
        auto state = [&] {
            ::Window root, child;
            int rx, ry, wx, wy;
            unsigned int mask = 0;
            XQueryPointer(d, DefaultRootWindow(d), &root, &child, &rx, &ry, &wx, &wy, &mask);
            return mask & (Button1Mask | ControlMask | ShiftMask);
        };
        QCOMPARE(state(), 0u);
        for(bool throws : {false, true}) {
            int attempts = 0;
            bool completed = false, observedPress = false;
            try {
                completed = selectionModes([&](auto) {
                    ++attempts;
                    return checkedInputGesture([&](bool&) {
                        key(d, ctrl, True, 0);
                        key(d, shift, True, 0);
                        button(d, 1, True, 0);
                        XSync(d, False);
                        observedPress = state() == (Button1Mask | ControlMask | ShiftMask);
                        if(throws) throw std::runtime_error("injected after XTEST press");
                        return;
                    }, release);
                });
            } catch(const std::runtime_error&) {
                QVERIFY(throws);
            }
            QVERIFY(observedPress);
            QVERIFY(!completed);
            QCOMPARE(attempts, 1);
            QCOMPARE(state(), 0u); // Read back real Xserver state, not callbacks.
        }
    }
    void nativeXTestSelectionGestures() {
        if(!qEnvironmentVariableIsSet("EXPLORER_ENABLE_XTEST")) {
            QSKIP("Set EXPLORER_ENABLE_XTEST=1 on an isolated owned Xephyr display to run native XTEST gestures");
        }
        QVERIFY2(SelectionOwnership::verify(), "XTEST ownership changed before selection");
        QLibrary xtst(QStringLiteral("libXtst.so.6"));
        QVERIFY2(xtst.load(), "libXtst is required for the native XTEST selection test");
        using FakeMotion = int (*)(Display*, int, int, int, unsigned long);
        using FakeButton = int (*)(Display*, unsigned int, int, unsigned long);
        auto fakeMotion = reinterpret_cast<FakeMotion>(xtst.resolve("XTestFakeMotionEvent"));
        auto fakeButton = reinterpret_cast<FakeButton>(xtst.resolve("XTestFakeButtonEvent"));
        QVERIFY(fakeMotion && fakeButton);
        Display* display = XOpenDisplay(nullptr);
        QVERIFY(display);
        QVERIFY(SelectionOwnership::verify(display));
        struct DisplayCloser { void operator()(Display* d) const { if(d) XCloseDisplay(d); } };
        std::unique_ptr<Display, DisplayCloser> xdisplay(display);
        const auto releaseInput = [&] {
            fakeButton(display, 1, False, 0);
            XSync(display, False);
        };

        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QStringList expected{QStringLiteral("Nested Notes.txt"), QStringLiteral("Readme.txt")};
        for(const auto& name : expected) {
            QFile file(fixture.filePath(name));
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("native XTEST selection fixture\n");
        }
        QScopedPointer<PCManFM::MainWindow> window(new PCManFM::MainWindow(Fm::FilePath::fromLocalPath(fixture.path().toUtf8().constData())));
        window->resize(1104, 642);
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window.data()));
        QVERIFY(QTest::qWaitForWindowActive(window.data()));
        QTest::qWait(200);
        auto* folderView = window->currentPage()->folderView();
        QTRY_COMPARE(folderView->model()->rowCount(), 2);
        QJsonObject observations;
        auto selectedNames = [&] {
            QStringList selected;
            for(const auto& index : folderView->selectionModel()->selectedIndexes()) {
                if(index.column() == 0) selected << index.data().toString();
            }
            selected.sort();
            return selected;
        };
        auto injectReverseMarquee = [&](Fm::FolderView::ViewMode mode) {
            return checkedInputGesture([&](bool& complete) {
            window->currentPage()->setViewMode(mode);
            auto* viewport = folderView->childView()->viewport();
            QTRY_VERIFY(viewport->width() > 200 && viewport->height() > 100);
            folderView->selectionModel()->clearSelection();
            const QPoint start = viewport->mapToGlobal(QPoint(viewport->width() - 8, viewport->height() - 8));
            const QPoint end = viewport->mapToGlobal(QPoint(8, 8));
            const int screen = DefaultScreen(display);
            QVERIFY(fakeMotion(display, screen, start.x(), start.y(), 0));
            QVERIFY(fakeButton(display, 1, True, 0));
            QVERIFY(fakeMotion(display, screen, end.x(), end.y(), 80));
            XFlush(display);
            QTest::qWait(250);
            const auto captureDir = qEnvironmentVariable("EXPLORER_TEST_ARTIFACTS");
            if(!captureDir.isEmpty()) {
                const auto label = mode == Fm::FolderView::DetailedListMode ? QStringLiteral("details") : QStringLiteral("grid");
                QVERIFY(window->grab().save(QDir(captureDir).filePath(QStringLiteral("selection-%1-attempt.png").arg(label))));
            }
            auto* selectionModel = folderView->selectionModel();
            QModelIndexList selectedIndexes = selectionModel->selectedRows(0);
            if(mode == Fm::FolderView::IconMode && selectedIndexes.isEmpty()) {
                for(const auto& index : selectionModel->selectedIndexes()) {
                    if(index.column() == 0) selectedIndexes << index;
                }
            }
            QCOMPARE(selectedIndexes.size(), 2);
            QStringList selected;
            for(const auto& index : selectedIndexes) selected << index.data().toString();
            selected.sort();
            QStringList sortedExpected = expected;
            sortedExpected.sort();
            QCOMPARE(selected, sortedExpected);
            const auto heldContent = viewport->grab().toImage();
            if(!qEnvironmentVariable("EXPLORER_TEST_ARTIFACTS").isEmpty()) {
                const QString suffix = mode == Fm::FolderView::DetailedListMode ? QStringLiteral("details") : QStringLiteral("grid");
                const auto dir = qEnvironmentVariable("EXPLORER_TEST_ARTIFACTS");
                QVERIFY(QDir().mkpath(dir));
                QVERIFY(window->grab().save(QDir(dir).filePath(QStringLiteral("selection-%1-held.png").arg(suffix))));
            }
            QVERIFY(fakeButton(display, 1, False, 0));
            XSync(display, False);
            QTest::qWait(250);
            QCOMPARE(selectedNames(), sortedExpected);
            const QString suffix = mode == Fm::FolderView::DetailedListMode ? QStringLiteral("details") : QStringLiteral("grid");
            observations.insert(suffix + QStringLiteral("-held"), QJsonArray::fromStringList(selected));
            observations.insert(suffix + QStringLiteral("-released"), QJsonArray::fromStringList(selectedNames()));
            if(!qEnvironmentVariable("EXPLORER_TEST_ARTIFACTS").isEmpty()) {
                const QString suffix = mode == Fm::FolderView::DetailedListMode ? QStringLiteral("details") : QStringLiteral("grid");
                const auto dir = qEnvironmentVariable("EXPLORER_TEST_ARTIFACTS");
                QVERIFY(window->grab().save(QDir(dir).filePath(QStringLiteral("selection-%1-released.png").arg(suffix))));
            }
            const auto releasedContent = viewport->grab().toImage();
            int darkPixels = 0;
            const QRect interior(12,12,viewport->width()-24,viewport->height()-24);
            for(const auto& index : selectedIndexes) {
                const auto area = folderView->childView()->visualRect(index).intersected(interior);
                for(int y=area.top(); y<=area.bottom(); ++y) {
                    for(int x=area.left(); x<=area.right(); ++x) {
                        const auto under = releasedContent.pixelColor(x,y);
                        if(qMax(under.red(), qMax(under.green(), under.blue())) > 100) continue;
                        const auto over = heldContent.pixelColor(x,y);
                        const QColor predicted(qRound(under.red()*2.0/3),
                                               qRound(under.green()*2.0/3+34),
                                               qRound(under.blue()*2.0/3+68));
                        QVERIFY2(qAbs(over.red()-predicted.red()) <= 1
                                 && qAbs(over.green()-predicted.green()) <= 1
                                 && qAbs(over.blue()-predicted.blue()) <= 1,
                                 "Native held marquee erased/changed backing text or icon");
                        ++darkPixels;
                    }
                }
            }
            QVERIFY2(darkPixels >= 20, "Need actual dark name/icon pixels under native marquee");
            observations.insert(suffix + QStringLiteral("-dark-pixels-preserved"), darkPixels);
            complete = true;
            }, releaseInput);
        };
        QVERIFY(selectionModes(injectReverseMarquee));
        const auto artifacts = qEnvironmentVariable("EXPLORER_TEST_ARTIFACTS");
        if(!artifacts.isEmpty()) {
            QFile file(QDir(artifacts).filePath(QStringLiteral("selection-names.json")));
            QVERIFY(file.open(QIODevice::WriteOnly));
            QVERIFY(file.write(QJsonDocument(observations).toJson()) > 0);
        }
    }

    void nativeExplorerGesturesMatchWindows() {
        if(!qEnvironmentVariableIsSet("EXPLORER_ENABLE_XTEST")) QSKIP("Run with EXPLORER_ENABLE_XTEST=1 on owned isolated Xephyr");
        QVERIFY2(SelectionOwnership::verify(), "XTEST ownership changed before native gestures");
        QLibrary xtst(QStringLiteral("libXtst.so.6"));
        QVERIFY(xtst.load());
        using Motion=int(*)(Display*,int,int,int,unsigned long);
        using Button=int(*)(Display*,unsigned int,int,unsigned long);
        using Key=int(*)(Display*,unsigned int,int,unsigned long);
        auto motion=reinterpret_cast<Motion>(xtst.resolve("XTestFakeMotionEvent"));
        auto button=reinterpret_cast<Button>(xtst.resolve("XTestFakeButtonEvent"));
        auto key=reinterpret_cast<Key>(xtst.resolve("XTestFakeKeyEvent"));
        QVERIFY(motion && button && key);
        Display* d=XOpenDisplay(nullptr);
        QVERIFY(d);
        QVERIFY(SelectionOwnership::verify(d));
        struct Close { void operator()(Display* p) const { XCloseDisplay(p);
            } };
        std::unique_ptr<Display,Close> xd(d);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        QDir root(fixture.path());
        QVERIFY(root.mkdir(QStringLiteral("Target")));
        const QMap<QString,QByteArray> content{{QStringLiteral("Alpha.txt"),"Alpha test bytes\n"},{QStringLiteral("Bravo.txt"),"Bravo test bytes\n"},{QStringLiteral("Charlie.txt"),"C"},{QStringLiteral("Delta.txt"),"D"},{QStringLiteral("Echo.txt"),"E"},{QStringLiteral("Gamma.txt"),"G"}};
        for(auto it=content.cbegin();it!=content.cend();++it){ QFile f(root.filePath(it.key()));
            QVERIFY(f.open(QIODevice::WriteOnly));
            QCOMPARE(f.write(it.value()),qint64(it.value().size()));
        }
        auto digest=[](const QByteArray& bytes){ return QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());
        };
        auto fileHash=[&](const QString& path){ QFile f(path);
            if(!f.open(QIODevice::ReadOnly)) return QString();
            return digest(f.readAll());
        };
        QScopedPointer<PCManFM::MainWindow> window(new PCManFM::MainWindow(Fm::FilePath::fromLocalPath(fixture.path().toUtf8().constData())));
        window->resize(1104,642);
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window.data()));
        auto* v=window->currentPage()->folderView();
        QTRY_COMPARE_WITH_TIMEOUT(v->model()->rowCount(),7,5000);
        auto names=[&](){ QStringList out;
            for(const auto& ix:v->selectionModel()->selectedRows(0)) out<<ix.data().toString();
            out.sort();
            return out;
        };
        auto point=[&](const QString& name){ for(int i=0;i<v->model()->rowCount();++i){ auto ix=v->model()->index(i,0);
                if(ix.data().toString()==name) return v->childView()->viewport()->mapToGlobal(v->childView()->visualRect(ix).center());
            } return QPoint();
        };
        const auto ctrl=XKeysymToKeycode(d,XK_Control_L), shift=XKeysymToKeycode(d,XK_Shift_L);
        const auto releaseInput = qScopeGuard([&] {
            button(d,1,False,0); key(d,ctrl,False,0); key(d,shift,False,0); XFlush(d);
        });
        QJsonObject observations;
        const QString artifacts=qEnvironmentVariable("EXPLORER_TEST_ARTIFACTS");
        auto record = [&](const QString& stage) {
            observations.insert(stage, QJsonArray::fromStringList(names()));
            if(!artifacts.isEmpty()) {
                QDir().mkpath(artifacts);
                if(!window->grab().save(QDir(artifacts).filePath(stage + QStringLiteral(".png")))) return false;
            }
            return true;
        };
        auto wait=[&](int ms){ QElapsedTimer t;
            t.start();
            while(t.elapsed()<ms){QCoreApplication::processEvents(QEventLoop::AllEvents,20);
                QThread::msleep(5);
            } };
        auto click=[&](const QString& name,bool c=false,bool s=false){
            return checkedInputGesture([&](bool& complete) {
            const auto p=point(name);
            QVERIFY(!p.isNull());
            if(c)key(d,ctrl,True,0);
            if(s)key(d,shift,True,0);
            motion(d,DefaultScreen(d),p.x(),p.y(),0);
            button(d,1,True,0);
            button(d,1,False,0);
            XFlush(d);
            wait(300);
            if(s)key(d,shift,False,0);
            if(c)key(d,ctrl,False,0);
            XSync(d, False);
            wait(250);
            complete = true;
            }, [&] { button(d,1,False,0); key(d,ctrl,False,0); key(d,shift,False,0); XSync(d,False); });
        };
        QVERIFY(click(QStringLiteral("Alpha.txt")));
        QVERIFY(record(QStringLiteral("ctrl-after-alpha")));
        QCOMPARE(names(),QStringList{QStringLiteral("Alpha.txt")});
        QVERIFY(click(QStringLiteral("Bravo.txt"),true));
        QVERIFY(record(QStringLiteral("ctrl-after-bravo")));
        QCOMPARE(names(),QStringList({QStringLiteral("Alpha.txt"),QStringLiteral("Bravo.txt")}));
        QVERIFY(click(QStringLiteral("Alpha.txt"),true));
        QVERIFY(record(QStringLiteral("ctrl-after-toggle-alpha")));
        QCOMPARE(names(),QStringList{QStringLiteral("Bravo.txt")});
        QVERIFY(click(QStringLiteral("Alpha.txt")));
        QVERIFY(click(QStringLiteral("Gamma.txt"),false,true));
        QVERIFY(record(QStringLiteral("shift-after-gamma")));
        QCOMPARE(names(),QStringList({QStringLiteral("Alpha.txt"),QStringLiteral("Bravo.txt"),QStringLiteral("Charlie.txt"),QStringLiteral("Delta.txt"),QStringLiteral("Echo.txt"),QStringLiteral("Gamma.txt")}));
        auto vp=v->childView()->viewport();
        auto p=vp->mapToGlobal(QPoint(100,vp->height()-60));
        motion(d,DefaultScreen(d),p.x(),p.y(),0);
        button(d,1,True,0);
        button(d,1,False,0);
        XFlush(d);
        wait(300);
        QVERIFY(record(QStringLiteral("empty-clears")));
        QVERIFY(names().isEmpty());
        // Queue the complete gesture BEFORE dispatching events: QDrag::exec()
        // enters a nested loop, so synchronous processEvents-driven input hangs.
        auto drag=[&](const QString& name,bool copy) {
            // Destroy pending timer callbacks before releasing input on any exit.
            QObject timers;
            return checkedInputGesture([&](bool& complete) {
            const auto a=point(name), b=point(QStringLiteral("Target"));
            QVERIFY(!a.isNull() && !b.isNull());
            if(copy) key(d,ctrl,True,0);
            motion(d,DefaultScreen(d),a.x(),a.y(),0);
            button(d,1,True,0);
            XFlush(d);
            for(int i=1;i<=12;++i) {
                QTimer::singleShot(200+i*50, &timers, [=] {
                    motion(d,DefaultScreen(d),a.x()+(b.x()-a.x())*i/12,a.y()+(b.y()-a.y())*i/12,0);
                    XFlush(d);
                });
            }
            QTimer::singleShot(1100, &timers, [=] { button(d,1,False,0); XFlush(d); });
            QTimer::singleShot(1250, &timers, [=] { key(d,ctrl,False,0); XFlush(d); });
            QTimer::singleShot(1400, &timers, [=] {
                // A regression to the upstream action menu must fail, not hang.
                if(QApplication::activePopupWidget()) {
                    qWarning("Unexpected popup after drop; cancelling for bounded failure");
                    const auto escape = XKeysymToKeycode(d, XK_Escape);
                    key(d,escape,True,0); key(d,escape,False,0); XFlush(d);
                }
            });
            wait(1600);
            complete = true;
            }, [&] { button(d,1,False,0); key(d,ctrl,False,0); key(d,shift,False,0); XSync(d,False); });
        };
        const auto alpha=digest(content.value(QStringLiteral("Alpha.txt"))), bravo=digest(content.value(QStringLiteral("Bravo.txt")));
        QVERIFY(drag(QStringLiteral("Alpha.txt"),false));
        QTRY_VERIFY_WITH_TIMEOUT(!QFile::exists(root.filePath(QStringLiteral("Alpha.txt"))),5000);
        QCOMPARE(fileHash(root.filePath(QStringLiteral("Target/Alpha.txt"))),alpha);
        QVERIFY(record(QStringLiteral("move-complete")));
        QVERIFY(drag(QStringLiteral("Bravo.txt"),true));
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(root.filePath(QStringLiteral("Target/Bravo.txt"))),5000);
        QVERIFY(QFile::exists(root.filePath(QStringLiteral("Bravo.txt"))));
        QCOMPARE(fileHash(root.filePath(QStringLiteral("Bravo.txt"))),bravo);
        QCOMPARE(fileHash(root.filePath(QStringLiteral("Target/Bravo.txt"))),bravo);
        QVERIFY(record(QStringLiteral("copy-complete")));
        if(!artifacts.isEmpty()){QDir().mkpath(artifacts);
            QJsonObject report;
            report.insert(QStringLiteral("reference"),QStringLiteral("20261031T0147-0300/INTERACTIONS_READY.json"));
            report.insert(QStringLiteral("cases"),QJsonObject{{QStringLiteral("ctrl-toggle"),true},{QStringLiteral("shift-range-empty"),true},{QStringLiteral("dnd-default-move"),true},{QStringLiteral("dnd-ctrl-copy"),true}});
            report.insert(QStringLiteral("selectedNames"),observations);
            report.insert(QStringLiteral("filesystem"), QJsonObject{{QStringLiteral("moveSourceExists"),QFile::exists(root.filePath(QStringLiteral("Alpha.txt")))},{QStringLiteral("moveDestinationSHA256"),fileHash(root.filePath(QStringLiteral("Target/Alpha.txt")))},{QStringLiteral("copySourceSHA256"),fileHash(root.filePath(QStringLiteral("Bravo.txt")))},{QStringLiteral("copyDestinationSHA256"),fileHash(root.filePath(QStringLiteral("Target/Bravo.txt")))}});
            report.insert(QStringLiteral("sha256"),QJsonObject{{QStringLiteral("Alpha.txt"),alpha},{QStringLiteral("Bravo.txt"),bravo}});
            QFile f(QDir(artifacts).filePath(QStringLiteral("native-gesture-comparison.json")));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(QJsonDocument(report).toJson(QJsonDocument::Indented));
        }
    }


    void chromeHasWindowsStructure() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString path = fixture.filePath(QStringLiteral("ExplorerReference"));
        QVERIFY(QDir().mkpath(path));
        for(const auto& name : {QStringLiteral("Documentos"),QStringLiteral("Imagens"),QStringLiteral("Projetos")}) QVERIFY(QDir(path).mkdir(name));
        for(const auto& name : {QStringLiteral("Leia-me.txt"),QStringLiteral("Notas.txt")}) {
            QFile file(QDir(path).filePath(name)); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("Referencia visual\n");
        }
        QScopedPointer<PCManFM::MainWindow> window(new PCManFM::MainWindow(Fm::FilePath::fromLocalPath(path.toUtf8().constData())));
        window->resize(1104, 642);
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window.data()));
        auto* title = window->findChild<QWidget*>(QStringLiteral("explorerTitleBar"));
        QVERIFY2(title, "Explorer requires a tabs/title row above navigation and commands");
        auto* navigation = window->findChild<QToolBar*>(QStringLiteral("explorerNavigation"));
        QVERIFY(navigation);
        auto* commands = window->findChild<QToolBar*>(QStringLiteral("explorerCommands"));
        QVERIFY(commands);
        const QList<QPair<QString,QString>> wiredIcons{
            {QStringLiteral("explorerNew"),QStringLiteral("new")}, {QStringLiteral("explorerCut"),QStringLiteral("cut")},
            {QStringLiteral("explorerCopy"),QStringLiteral("copy")}, {QStringLiteral("explorerPaste"),QStringLiteral("paste")},
            {QStringLiteral("explorerRename"),QStringLiteral("rename")}, {QStringLiteral("explorerDelete"),QStringLiteral("delete")},
            {QStringLiteral("explorerSort"),QStringLiteral("sort")}, {QStringLiteral("explorerView"),QStringLiteral("view")},
            {QStringLiteral("explorerMore"),QStringLiteral("more")}};
        for(const auto& [widgetName, iconName] : wiredIcons) {
            auto* button = window->findChild<QToolButton*>(widgetName);
            QVERIFY2(button, qPrintable(widgetName));
            QVERIFY2(button->icon().pixmap(20,20).toImage() == PCManFM::ExplorerIcons::icon(iconName).pixmap(20,20).toImage(), qPrintable(widgetName));
        }
        QCOMPARE(title->height(),40);
        QCOMPARE(navigation->height(),49);
        QCOMPARE(commands->height(),47);
        QVERIFY2(!QIcon::fromTheme(QStringLiteral("folder")).pixmap(16,16).isNull(), "Folder icons must render without a platform theme plugin");
        QVERIFY(title->mapTo(window.data(), QPoint()).y() < navigation->mapTo(window.data(), QPoint()).y());
        QVERIFY(navigation->mapTo(window.data(), QPoint()).y() < commands->mapTo(window.data(), QPoint()).y());
        auto* tabs = title->findChild<QTabBar*>();
        QVERIFY(tabs);
        QCOMPARE(tabs->count(), 1);
        QVERIFY(tabs->isVisible());
        QVERIFY(!tabs->tabIcon(0).isNull());
        auto* tree = qobject_cast<QTreeView*>(window->currentPage()->folderView()->childView());
        QVERIFY(tree);
        QCOMPARE(tree->header()->visualIndex(Fm::FolderModel::ColumnFileMTime),1);
        QVERIFY(tree->isColumnHidden(Fm::FolderModel::ColumnFileOwner));
        QVERIFY(tree->isColumnHidden(Fm::FolderModel::ColumnFileGroup));
        QVERIFY(!tree->alternatingRowColors());
        auto* search = window->findChild<QLineEdit*>(QStringLiteral("explorerSearch"));
        QVERIFY(search);
        QVERIFY(search->placeholderText().contains(QStringLiteral("Filtrar")));
        QVERIFY(!window->findChild<QMenuBar*>()->isVisible());
        QTRY_COMPARE(window->currentPage()->folderView()->model()->rowCount(), 5);
        const QString artifacts = qEnvironmentVariable("EXPLORER_TEST_ARTIFACTS");
        if(!artifacts.isEmpty()) {
            QVERIFY(QDir().mkpath(artifacts));
            QVERIFY(window->grab().save(QDir(artifacts).filePath(QStringLiteral("explorer-window.png"))));
            QJsonObject geometry;
            for(const auto* widget : window->findChildren<QWidget*>()) {
                if(widget->objectName().startsWith(QStringLiteral("explorer"))) {
                    const QPoint origin = widget->mapTo(window.data(),QPoint());
                    geometry.insert(widget->objectName(),QJsonObject{{QStringLiteral("x"),origin.x()},{QStringLiteral("y"),origin.y()},{QStringLiteral("w"),widget->width()},{QStringLiteral("h"),widget->height()},{QStringLiteral("visible"),widget->isVisible()}});
                }
            }
            QFile file(QDir(artifacts).filePath(QStringLiteral("layout.json")));
            QVERIFY(file.open(QIODevice::WriteOnly)); file.write(QJsonDocument(geometry).toJson());
        }
    }
};

int main(int argc, char** argv) {
    const bool native = qEnvironmentVariableIsSet("EXPLORER_ENABLE_XTEST");
    if(native && !SelectionOwnership::qtRoutingAllowed(argc, argv)) {
        fprintf(stderr, "XTEST Qt routing rejected: use runner xcb environment without routing overrides\n");
        return 2;
    }
    if(native && !SelectionOwnership::verify()) {
        fprintf(stderr, "XTEST ownership rejected: use run_selection_x11.py with a task-owned Xephyr\n");
        return 2;
    }
    QTemporaryDir config;
    if(!config.isValid()) return 2;
    qputenv("XDG_CONFIG_HOME", config.path().toUtf8());
    qputenv("XDG_CACHE_HOME", (config.path() + QStringLiteral("/cache")).toUtf8());
    PCManFM::Application app(argc, argv);
    if(native) {
        auto* x11 = app.nativeInterface<QNativeInterface::QX11Application>();
        Display* qtDisplay = x11 ? x11->display() : nullptr;
        if(!qtDisplay || !SelectionOwnership::verify(qtDisplay)) {
            fprintf(stderr, "XTEST Qt connection ownership rejected\n");
            return 2;
        }
    }
    app.init();
    app.settings().load(QStringLiteral("ui-test"));
    const auto folderConfig = (config.path() + QStringLiteral("/folders.conf")).toUtf8();
    Fm::FolderConfig::init(folderConfig.constData());
    app.setQuitOnLastWindowClosed(false);
    ExplorerTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "tst_explorer.moc"
