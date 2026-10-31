#include "explorertransfercontroller.h"

#include <QString>
#include "core/fileoperationjob.h"

ExplorerTransferController::ExplorerTransferController(Fm::FileOperationJob* job, QObject* parent)
    : QObject(parent), job_(job) {
    timer_.setInterval(100);
    connect(&timer_, &QTimer::timeout, this, &ExplorerTransferController::sample);
    if(job_) {
        connect(job_, &Fm::Job::finished, this, [this] {
            running_ = false;
            timer_.stop();
            Q_EMIT telemetryUpdated();
            Q_EMIT finished();
        }, Qt::QueuedConnection);
        connect(job_, &Fm::Job::cancelled, this, [this] {
            cancelled_ = true;
            Q_EMIT cancelled();
        }, Qt::QueuedConnection);
        timer_.start();
    }
}

ExplorerTransferController::~ExplorerTransferController() {
    timer_.stop();
}

ExplorerTransferController::Snapshot ExplorerTransferController::snapshot() const {
    Snapshot result;
    result.running = running_;
    result.cancelled = cancelled_;
    if(started_)
        result.elapsedSeconds = elapsed_.elapsed() / 1000.0;
    if(!job_)
        return result;

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

void ExplorerTransferController::cancel() {
    if(job_ && running_)
        job_->cancel();
}

bool ExplorerTransferController::pause() { return false; }
bool ExplorerTransferController::resume() { return false; }

void ExplorerTransferController::sample() {
    if(!job_) {
        timer_.stop();
        return;
    }
    std::uint64_t total = 0, count = 0;
    if(!started_ && job_->totalAmount(total, count)) {
        elapsed_.start();
        started_ = true;
    }
    Q_EMIT telemetryUpdated();
}
