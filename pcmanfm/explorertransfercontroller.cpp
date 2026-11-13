#include "explorertransfercontroller.h"

#include <QString>
#include <QMetaObject>
#include "core/fileoperationjob.h"
#include "core/filetransferjob.h"

ExplorerTransferController::ExplorerTransferController(Fm::FileOperationJob* job, QObject* parent)
    : QObject(parent), job_(job) {
    timer_.setInterval(100);
    connect(&timer_, &QTimer::timeout, this, &ExplorerTransferController::sample);
    if(!job_) {
        state_ = State::Detached;
        return;
    }
    connect(job_, &Fm::FileOperationJob::preparedToRun,
            this, &ExplorerTransferController::onPrepared, Qt::QueuedConnection);
    completion_ = job->completion();
    // Only queued QObject deliveries. No worker callback captures this, even
    // while an earlier FileOperation finished handler destroys the dialog.
    connect(job_, &Fm::Job::finished, this,
            &ExplorerTransferController::onFinishedObserved, Qt::QueuedConnection);
    connect(job_, &Fm::Job::cancelled, this, [this] {
        if(!cancellationRequested_) {
            cancellationRequested_ = true;
            const auto current = snapshot().state;
            if(current != State::Finished && current != State::Detached)
                state_ = State::Cancelling;
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
    result.state = state_;
    result.running = result.state == State::Preparing || result.state == State::Running ||
                     result.state == State::Pausing || result.state == State::Paused || result.state == State::Cancelling;
    result.cancellationRequested = cancellationRequested_;
    result.cancelled = result.cancellationRequested;
    if(started_)
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

bool ExplorerTransferController::terminalSnapshot(Snapshot& result) const {
    if(!completion_) return false;
    std::lock_guard<std::mutex> lock{completion_->mutex};
    if(!completion_->ready) return false;
    result.state = State::Finished;
    result.hasTotals = completion_->hasTotals;
    result.totalBytes = completion_->totalBytes;
    result.finishedBytes = completion_->finishedBytes;
    result.currentFileTotalBytes = completion_->currentTotal;
    result.currentFileBytes = completion_->currentDone;
    if(completion_->currentFile) {
        const auto path = completion_->currentFile.toString();
        result.source = QString::fromUtf8(path.get());
    }
    result.cancellationRequested = completion_->cancellationRequested;
    result.cancelled = result.cancellationRequested;
    if(started_)
        result.elapsedSeconds = qMax(0.0, std::chrono::duration<double>(completion_->completedAt - startedAt_).count());
    return true;
}

ExplorerTransferController::Snapshot ExplorerTransferController::snapshot() const {
    Snapshot result;
    if(terminalSnapshot(result)) return result;
    if(state_ == State::Detached || !job_) {
        result.state = State::Detached;
        result.cancellationRequested = cancellationRequested_;
        result.cancelled = result.cancellationRequested;
        return result;
    }
    return readJobSnapshot();
}

bool ExplorerTransferController::attachLate() {
    if(!job_ || snapshot().state != State::Idle)
        return false;
    state_ = State::Running;
    startedAt_ = std::chrono::steady_clock::now();
    started_ = true;
    elapsed_.start();
    timer_.start();
    Q_EMIT telemetryUpdated();
    return true;
}

bool ExplorerTransferController::markStarted() {
    if(!job_ || snapshot().state != State::Idle)
        return false;
    state_ = State::Preparing;
    startedAt_ = std::chrono::steady_clock::now();
    started_ = true;
    elapsed_.start();
    timer_.start();
    return true;
}

void ExplorerTransferController::cancel() {
    const auto current = snapshot().state;
    if(!job_ || (current != State::Preparing && current != State::Running &&
                 current != State::Pausing && current != State::Paused && current != State::Cancelling))
        return;
    if(cancellationRequested_) return;
    // Reserve the intent before entering GLib: cancelled handlers may call
    // cancel() again synchronously (or dispatch the queued notification).
    cancellationRequested_ = true;
    QPointer<ExplorerTransferController> alive(this);
    const bool accepted = job_->requestCancellation();
    if(!alive) return;
    if(!accepted) {
        // Completion won the backend cut; no cancellation was performed.
        cancellationRequested_ = false;
        return;
    }
    if(snapshot().state != State::Finished) state_ = State::Cancelling;
    Q_EMIT cancellationRequested();
}

bool ExplorerTransferController::supportsPause() const {
    auto* transfer = dynamic_cast<Fm::FileTransferJob*>(job_.data());
    const auto current = snapshot().state;
    return current != State::Finished && current != State::Detached && current != State::Cancelling &&
           transfer && transfer->supportsCooperativePause() && snapshot().state != State::Finished;
}

bool ExplorerTransferController::pause() {
    const auto current = snapshot().state;
    if(!supportsPause() || (current != State::Preparing && current != State::Running))
        return false;
    auto* transfer = dynamic_cast<Fm::FileTransferJob*>(job_.data());
    if(!transfer || !transfer->pause() || snapshot().state == State::Finished) return false;
    state_ = State::Pausing;
    return true;
}

bool ExplorerTransferController::resume() {
    const auto current = snapshot().state;
    if(current != State::Pausing && current != State::Paused) return false;
    auto* transfer = dynamic_cast<Fm::FileTransferJob*>(job_.data());
    if(!transfer || !transfer->pauseRequested()) return false;
    transfer->resume();
    if(snapshot().state == State::Finished) return false;
    state_ = State::Running;
    return true;
}

void ExplorerTransferController::sample() {
    const auto current = snapshot().state;
    if(current == State::Detached || current == State::Finished) {
        timer_.stop();
        return;
    }
    if(auto* transfer = dynamic_cast<Fm::FileTransferJob*>(job_.data())) {
        if(transfer->isPauseAcknowledged() && state_ == State::Pausing)
            state_ = State::Paused;
        else if(!transfer->pauseRequested() && state_ == State::Pausing)
            state_ = State::Running;
    }
    Q_EMIT telemetryUpdated();
}

void ExplorerTransferController::onPrepared() {
    if(snapshot().state == State::Preparing)
        state_ = State::Running;
    Q_EMIT telemetryUpdated();
}

void ExplorerTransferController::onFinishedObserved() {
    state_ = State::Finished;
    timer_.stop();
    QPointer<ExplorerTransferController> alive(this);
    Q_EMIT telemetryUpdated();
    if(alive) Q_EMIT finished();
}

void ExplorerTransferController::onJobDestroyed() {
    state_ = snapshot().state == State::Finished ? State::Finished : State::Detached;
    timer_.stop();
}
