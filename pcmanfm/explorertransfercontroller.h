#pragma once

#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QElapsedTimer>
#include <cstdint>

namespace Fm { class FileOperationJob; }

class ExplorerTransferController final : public QObject {
    Q_OBJECT
public:
    struct Snapshot {
        QString source;
        quint64 totalBytes = 0;
        quint64 finishedBytes = 0;
        quint64 currentFileBytes = 0;
        quint64 currentFileTotalBytes = 0;
        double elapsedSeconds = 0.0;
        bool hasTotals = false;
        bool running = false;
        bool cancelled = false;
    };

    explicit ExplorerTransferController(Fm::FileOperationJob* job, QObject* parent = nullptr);
    ~ExplorerTransferController() override;

    Snapshot snapshot() const;
    void cancel();
    // libfm-qt 2.4 declares Job::pause/resume but ships no definitions;
    // FileTransferJob also has no pause checkpoints. Never pretend it paused.
    bool pause();
    bool resume();

Q_SIGNALS:
    void telemetryUpdated();
    void finished();
    void cancelled();

private Q_SLOTS:
    void sample();

private:
    QPointer<Fm::FileOperationJob> job_;
    QTimer timer_;
    QElapsedTimer elapsed_;
    bool started_ = false;
    bool running_ = true;
    bool cancelled_ = false;
};
