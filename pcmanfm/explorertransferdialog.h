#pragma once

#include <fileoperationdialog.h>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QWidget>

class ExplorerTransferController;

// Presentation-only subclass: libfm-qt still owns the operation and its
// conflict/error handlers. Pause is exposed only when the live job confirms support.
class ExplorerTransferDialog final : public Fm::FileOperationDialog {
    Q_OBJECT
public:
    explicit ExplorerTransferDialog(Fm::FileOperation* operation);
    void setSourceFiles(const Fm::FilePathList& files) override;
    void setDestPath(const Fm::FilePath& destination) override;
    void setPrepared() override;
    void setCurFile(QString file) override;
    void setPercent(unsigned int percent) override;
    void setDataTransferred(std::uint64_t finished, std::uint64_t total) override;
    void setFilesProcessed(std::uint64_t finished, std::uint64_t total) override;
    void setRemainingTime(unsigned int seconds) override;

private Q_SLOTS:
    void toggleDetails();
    void togglePause();
    void refreshPauseState();

private:
    QLabel* summary_;
    QLabel* currentFile_;
    QLabel* progressText_;
    QLabel* detail_;
    QProgressBar* progress_;
    QPushButton* detailsButton_;
    QPushButton* pauseButton_;
    QPushButton* cancelButton_;
    ExplorerTransferController* controller_ = nullptr;
    QString sourceName_;
    QString destinationName_;
    QString currentFileName_;
    QString verb_ = tr("Copiando");
    bool showsDestination_ = true;
    std::uint64_t transferred_ = 0;
    std::uint64_t total_ = 0;
    std::uint64_t filesDone_ = 0;
    std::uint64_t filesTotal_ = 0;
};

Fm::FileOperationDialog* createExplorerTransferDialog(Fm::FileOperation* operation);
