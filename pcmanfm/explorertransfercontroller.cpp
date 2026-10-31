#include "explorertransfercontroller.h"

#include <QString>
#include <QMutexLocker>
#include <QMetaObject>
#include "core/fileoperationjob.h"
#include "core/filetransferjob.h"

ExplorerTransferController::ExplorerTransferController(Fm::FileOperationJob* job, QObject* parent)
    : QObject(parent), job_(job) {
    timer_.setInterval(100);
    connect(&timer_, &QTimer::timeout, this, &ExplorerTransferController::sample);
    if(!job_) {
        state_.store(State::Detached);
        return;
    }
    connect(job_, &Fm::FileOperationJob::preparedToRun,
            this, &ExplorerTransferController::onPrepared, Qt::QueuedConnection);
    // Capture final counters synchronously before libfm-qt's auto-delete can destroy the job.
    connect(job_, &Fm::Job::finished, this, [this] {
        Snapshot final = readJobSnapshot();
        final.state = State::Finished;
        final.running = false;
        final.cancellationRequested = cancellationRequested_.load();
        final.cancelled = final.cancellationRequested;
        {
            QMutexLocker lock(&finalSnapshotMutex_);
            finalSnapshot_ = final;
            hasFinalSnapshot_ = true;
        }
        state_.store(State::Finished);
        QMetaObject::invokeMethod(this, &ExplorerTransferController::onFinishedObserved, Qt::QueuedConnection);
    }, Qt::DirectConnection);
    connect(job_, &Fm::Job::cancelled, this, [this] {
        if(!cancellationRequested_.exchange(true)) {
            if(state_.load() != State::Finished && state_.load() != State::Detached)
                state_.store(State::Cancelling);
            Q_EMIT cancellationRequested();
        }
    }, Qt::QueuedConnection);
    connect(job_, &QObject::destroyed, this, &ExplorerTransferController::onJobDestroyed, Qt::QueuedConnection);
}

ExplorerTransferController::~ExplorerTransferController() {
    timer_.stop();
}

ExplorerTransferController::Snapshot ExplorerTransferController::readJobSnapshot() const {
    Snapshot result;
    result.state = state_.load();
    result.running = result.state == State::Preparing || result.state == State::Running ||
                     result.state == State::Pausing || result.state == State::Paused || result.state == State::Cancelling;
    result.cancellationRequested = cancellationRequested_.load();
    result.cancelled = result.cancellationRequested;
    if(started_.load())
        result.elapsedSeconds = elapsed_.elapsed() / 1000.0;
    if(!job_)
        return result;

    if(auto* transfer = dynamic_cast<Fm::FileTransferJob*>(job_.data())) {
        result.pauseRequested = transfer->pauseRequested();
        result.paused = transfer->isPauseAcknowledged();
    }

    std::uint64_t total = 0, count = 0, done = 0, doneCount = 0;
    result.hasTotals = job_->totalAmount(total, count);
    if(result.hasTotals) {
        result.totalBytes = total;
        job_->finishedAmount(done, doneCount);
        result.finishedBytes = done;
    }
    Fm::FilePath current;
    std::uint64_t currentTotal = 0, currentDone = 0;
    if(job_->currentFileProgress(current, currentTotal, currentDone)) {
        const auto path = current.toString();
        result.source = QString::fromUtf8(path.get());
        result.currentFileTotalBytes = currentTotal;
        result.currentFileBytes = currentDone;
    }
    return result;
}

ExplorerTransferController::Snapshot ExplorerTransferController::snapshot() const {
    if(state_.load() == State::Finished) {
        QMutexLocker lock(&finalSnapshotMutex_);
        if(hasFinalSnapshot_)
            return finalSnapshot_;
    }
    if(state_.load() == State::Detached) {
        Snapshot result;
        result.state = State::Detached;
        result.cancellationRequested = cancellationRequested_.load();
        result.cancelled = result.cancellationRequested;
        return result;
    }
    return readJobSnapshot();
}

bool ExplorerTransferController::markStarted() {
    State expected = State::Idle;
    if(!job_ || !state_.compare_exchange_strong(expected, State::Preparing))
        return false;
    started_ = true;
    elapsed_.start();
    timer_.start();
    return true;
}

void ExplorerTransferController::cancel() {
    const auto current = state_.load();
    if(!job_ || (current != State::Preparing && current != State::Running &&
                 current != State::Pausing && current != State::Paused && current != State::Cancelling))
        return;
    if(cancellationRequested_.exchange(true)) return;
    state_.store(State::Cancelling);
    job_->cancel();
    Q_EMIT cancellationRequested();
}

bool ExplorerTransferController::supportsPause() const {
    auto* transfer = dynamic_cast<Fm::FileTransferJob*>(job_.data());
    const auto current = state_.load();
    return transfer && transfer->supportsCooperativePause() &&
           current != State::Finished && current != State::Detached && current != State::Cancelling;
}

bool ExplorerTransferController::pause() {
    const auto current = state_.load();
    if(!supportsPause() || (current != State::Preparing && current != State::Running))
        return false;
    auto* transfer = dynamic_cast<Fm::FileTransferJob*>(job_.data());
    if(!transfer || !transfer->pause()) return false;
    state_.store(State::Pausing);
    return true;
}

bool ExplorerTransferController::resume() {
    const auto current = state_.load();
    if(current != State::Pausing && current != State::Paused) return false;
    auto* transfer = dynamic_cast<Fm::FileTransferJob*>(job_.data());
    if(!transfer || !transfer->pauseRequested()) return false;
    transfer->resume();
    state_.store(State::Running);
    return true;
}

void ExplorerTransferController::sample() {
    if(state_.load() == State::Detached || state_.load() == State::Finished) {
        timer_.stop();
        return;
    }
    if(auto* transfer = dynamic_cast<Fm::FileTransferJob*>(job_.data())) {
        if(transfer->isPauseAcknowledged() && state_.load() == State::Pausing)
            state_.store(State::Paused);
        else if(!transfer->pauseRequested() && state_.load() == State::Pausing)
            state_.store(State::Running);
    }
    Q_EMIT telemetryUpdated();
}

void ExplorerTransferController::onPrepared() {
    if(state_.load() == State::Preparing)
        state_.store(State::Running);
    Q_EMIT telemetryUpdated();
}

void ExplorerTransferController::onFinishedObserved() {
    timer_.stop();
    Q_EMIT telemetryUpdated();
    Q_EMIT finished();
}

void ExplorerTransferController::onJobDestroyed() {
    if(state_.load() != State::Finished)
        state_.store(State::Detached);
    timer_.stop();
}
