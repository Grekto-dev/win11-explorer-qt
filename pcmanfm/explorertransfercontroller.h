#pragma once

#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QElapsedTimer>
#include "core/fileoperationjob.h"
#include <chrono>
#include <cstdint>

namespace Fm { class FileOperationJob; }

class ExplorerTransferController final : public QObject {
    Q_OBJECT
public:
    enum class State { Idle, Preparing, Running, Pausing, Paused, Cancelling, Finished, Detached };
    Q_ENUM(State)

    struct Snapshot {
        QString source;
        quint64 totalBytes = 0;
        quint64 finishedBytes = 0;
        quint64 currentFileBytes = 0;
        quint64 currentFileTotalBytes = 0;
        double elapsedSeconds = 0.0;
        State state = State::Idle;
        bool hasTotals = false;
        bool running = false;
        bool cancellationRequested = false;
        bool pauseRequested = false;
        bool paused = false;
        // Compatibility field; true means cancellation was requested, not finished.
        bool cancelled = false;
    };

    explicit ExplorerTransferController(Fm::FileOperationJob* job, QObject* parent = nullptr);
    ~ExplorerTransferController() override;

    Snapshot snapshot() const;
    // Called only after the owner confirms FileOperation::isRunning() when
    // the delayed progress dialog is constructed. This is explicit late attach,
    // not a synthetic started event.
    bool attachLate();
    // The owner must call immediately before runAsync(); Fm::Job exposes no started signal.
    bool markStarted();
    void cancel();
    // Supported only for one local regular-file COPY with GIO progress callbacks.
    bool supportsPause() const;
    bool pause();
    bool resume();

Q_SIGNALS:
    void telemetryUpdated();
    void finished();
    void cancellationRequested();

private Q_SLOTS:
    void sample();
    void onPrepared();
    void onFinishedObserved();
    void onJobDestroyed();

private:
    Snapshot readJobSnapshot() const;
    bool terminalSnapshot(Snapshot& result) const;
    QPointer<Fm::FileOperationJob> job_;
    QTimer timer_;
    QElapsedTimer elapsed_;
    // All controller state is confined to its QObject (GUI) thread. Workers
    // publish only the backend-owned, independently shared Completion value.
    State state_ = State::Idle;
    bool cancellationRequested_ = false;
    bool started_ = false;
    std::chrono::steady_clock::time_point startedAt_;
    std::shared_ptr<const Fm::FileOperationJob::Completion> completion_;
};
