#include "explorertransferdialog.h"
#include "explorertransfercontroller.h"
#include <fileoperation.h>

#include <QFileInfo>
#include <QHBoxLayout>
#include <QLocale>
#include <QVBoxLayout>

ExplorerTransferDialog::ExplorerTransferDialog(Fm::FileOperation* operation)
    : Fm::FileOperationDialog(operation),
      summary_(new QLabel(this)), currentFile_(new QLabel(this)),
      progressText_(new QLabel(this)), detail_(new QLabel(this)), progress_(new QProgressBar(this)),
      detailsButton_(new QPushButton(tr("⌄  Mais detalhes"), this)),
      pauseButton_(new QPushButton(tr("Pausar"), this)),
      cancelButton_(new QPushButton(tr("Cancelar"), this)) {
    // Match the verb to the actual operation; don't label moves/deletes as copies.
    switch(operation->type()) {
    case Fm::FileOperation::Copy: verb_ = tr("Copiando"); break;
    case Fm::FileOperation::Move: verb_ = tr("Movendo"); break;
    case Fm::FileOperation::Link: verb_ = tr("Criando links para"); break;
    case Fm::FileOperation::Trash: verb_ = tr("Movendo para a lixeira"); showsDestination_ = false; break;
    case Fm::FileOperation::Delete: verb_ = tr("Excluindo"); showsDestination_ = false; break;
    case Fm::FileOperation::UnTrash: verb_ = tr("Restaurando"); showsDestination_ = false; break;
    case Fm::FileOperation::ChangeAttr: verb_ = tr("Alterando atributos de"); showsDestination_ = false; break;
    }
    // Retain libfm-qt's original widgets and their private state: merely hide
    // its presentation. Base error/conflict methods remain fully functional.
    for(QWidget* child : findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly)) {
        if(child != summary_ && child != currentFile_ && child != progressText_ && child != detail_
           && child != progress_ && child != detailsButton_ && child != pauseButton_ && child != cancelButton_)
            child->hide();
    }
    auto* content = new QWidget(this);
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(7);
    summary_->setParent(content);
    currentFile_->setParent(content);
    progressText_->setParent(content);
    detail_->setParent(content);
    progress_->setParent(content);
    detailsButton_->setParent(content);
    pauseButton_->setParent(content);
    cancelButton_->setParent(content);
    summary_->setObjectName(QStringLiteral("transferSummary"));
    layout->addWidget(summary_);
    layout->addWidget(currentFile_);
    layout->addWidget(progressText_);
    layout->addWidget(progress_);
    layout->addWidget(detail_);
    auto* buttons = new QHBoxLayout;
    buttons->addWidget(detailsButton_, 0, Qt::AlignLeft);
    buttons->addStretch(1);
    buttons->addWidget(pauseButton_);
    buttons->addWidget(cancelButton_);
    layout->addLayout(buttons);
    if(this->layout()) this->layout()->addWidget(content);
    else setLayout(layout);
    currentFile_->setObjectName(QStringLiteral("transferCurrentFile"));
    detail_->setObjectName(QStringLiteral("transferDetails"));
    progress_->setObjectName(QStringLiteral("transferProgress"));
    detailsButton_->setObjectName(QStringLiteral("transferDetailsButton"));
    pauseButton_->setObjectName(QStringLiteral("pauseButton"));
    cancelButton_->setObjectName(QStringLiteral("transferCancelButton"));
    pauseButton_->hide();
    summary_->setText(tr("Copiando 1 item"));
    summary_->setTextFormat(Qt::RichText);
    summary_->setWordWrap(true);
    currentFile_->setText(tr("Preparando…"));
    progressText_->setObjectName(QStringLiteral("transferPercent"));
    progressText_->setText(tr("0% concluído"));
    progress_->setRange(0, 100);
    progress_->setValue(0);
    progress_->setTextVisible(false);
    progress_->setFixedHeight(14);
    progress_->setStyleSheet(QStringLiteral("QProgressBar{border:0;background:#e5e5e5;} QProgressBar::chunk{background:#16833b;}"));
    detailsButton_->setFlat(true);
    detailsButton_->setCursor(Qt::PointingHandCursor);
    detail_->setWordWrap(true);
    detail_->hide();
    setMinimumSize(440, 166);
    resize(448, 166);
    connect(detailsButton_, &QPushButton::clicked, this, &ExplorerTransferDialog::toggleDetails);
    connect(pauseButton_, &QPushButton::clicked, this, &ExplorerTransferDialog::togglePause);
    connect(cancelButton_, &QPushButton::clicked, this, &ExplorerTransferDialog::reject);
    if(operation && operation->isRunning() && operation->job()) {
        controller_ = new ExplorerTransferController(operation->job(), this);
        if(controller_->attachLate() && controller_->supportsPause()) {
            pauseButton_->show();
            connect(controller_, &ExplorerTransferController::telemetryUpdated,
                    this, &ExplorerTransferDialog::refreshPauseState);
            refreshPauseState();
        } else {
            delete controller_;
            controller_ = nullptr;
        }
    }
}

void ExplorerTransferDialog::setSourceFiles(const Fm::FilePathList& files) {
    if(!files.empty()) {
        const QString path = QString::fromUtf8(files.front().toString().get());
        sourceName_ = QFileInfo(QFileInfo(path).absolutePath()).fileName();
        if(sourceName_.isEmpty()) sourceName_ = QFileInfo(path).fileName();
    }
    setWindowTitle(tr("%1 concluído").arg(progress_->value()));
    setCurFile(currentFileName_);
}
void ExplorerTransferDialog::setDestPath(const Fm::FilePath& destination) {
    const QString path = QString::fromUtf8(destination.toString().get());
    destinationName_ = QFileInfo(path).fileName();
    if(destinationName_.isEmpty()) destinationName_ = path;
}
void ExplorerTransferDialog::setPrepared() {
    const QString count = QString::number(filesTotal_ ? filesTotal_ : 1);
    if(showsDestination_ && !sourceName_.isEmpty() && !destinationName_.isEmpty())
        summary_->setText(tr("%1 %2 item de <a href=\"\">%3</a> para <a href=\"\">%4</a>")
                          .arg(verb_, count, sourceName_.toHtmlEscaped(), destinationName_.toHtmlEscaped()));
    else if(!sourceName_.isEmpty())
        summary_->setText(tr("%1 %2 item: %3").arg(verb_, count, sourceName_.toHtmlEscaped()));
    if(currentFile_->text() == tr("Preparando…")) currentFile_->clear();
}
void ExplorerTransferDialog::setCurFile(QString file) {
    currentFileName_ = QFileInfo(file).fileName();
    if(currentFileName_.isEmpty()) currentFileName_ = file;
    if(!currentFileName_.isEmpty()) currentFile_->setText(currentFileName_);
}
void ExplorerTransferDialog::setPercent(unsigned int percent) {
    const auto clamped = qMin(percent, 100u);
    progress_->setValue(static_cast<int>(clamped));
    progressText_->setText(tr("%1% concluído").arg(clamped));
    setWindowTitle(tr("%1% concluído").arg(clamped));
}
void ExplorerTransferDialog::setDataTransferred(std::uint64_t finished, std::uint64_t total) {
    transferred_ = finished; total_ = total;
    detail_->setText(tr("%1 de %2 transferidos").arg(QLocale().formattedDataSize(qint64(finished)), QLocale().formattedDataSize(qint64(total))));
}
void ExplorerTransferDialog::setFilesProcessed(std::uint64_t finished, std::uint64_t total) {
    filesDone_ = finished; filesTotal_ = total;
    setPrepared();
}
void ExplorerTransferDialog::setRemainingTime(unsigned int seconds) {
    // Do not invent a velocity/ETA; libfm's real estimate can be disclosed in details.
    if(seconds && detail_->text().isEmpty()) detail_->setText(tr("Tempo restante estimado: %1 s").arg(seconds));
}
void ExplorerTransferDialog::toggleDetails() {
    const bool show = !detail_->isVisible();
    detail_->setVisible(show);
    detailsButton_->setText(show ? tr("⌃  Menos detalhes") : tr("⌄  Mais detalhes"));
    adjustSize();
}

void ExplorerTransferDialog::togglePause() {
    if(!controller_) return;
    const auto state = controller_->snapshot().state;
    if(state == ExplorerTransferController::State::Paused)
        controller_->resume();
    else if(state == ExplorerTransferController::State::Running)
        controller_->pause();
    refreshPauseState();
}

void ExplorerTransferDialog::refreshPauseState() {
    if(!controller_) return;
    const auto snapshot = controller_->snapshot();
    switch(snapshot.state) {
    case ExplorerTransferController::State::Running:
        pauseButton_->setText(tr("Pausar"));
        pauseButton_->setEnabled(true);
        break;
    case ExplorerTransferController::State::Pausing:
        // Keep the UI honest: do not offer Resume until the backend ACKs pause.
        pauseButton_->setText(snapshot.paused ? tr("Retomar") : tr("Pausando…"));
        pauseButton_->setEnabled(snapshot.paused);
        break;
    case ExplorerTransferController::State::Paused:
        pauseButton_->setText(tr("Retomar"));
        pauseButton_->setEnabled(true);
        break;
    default:
        pauseButton_->setEnabled(false);
        break;
    }
}

bool ExplorerTransferDialog::recordSpeedSample(std::uint64_t, std::uint64_t, qint64) { return false; }
QVector<ExplorerTransferDialog::SpeedSample> ExplorerTransferDialog::speedSamples() const { return {}; }
void ExplorerTransferDialog::applyTransferState(ExplorerTransferController::State, bool) {}
void ExplorerTransferDialog::setPauseAvailable(bool) {}
bool ExplorerTransferDialog::detailsExpanded() const { return detail_->isVisible(); }

Fm::FileOperationDialog* createExplorerTransferDialog(Fm::FileOperation* operation) {
    return new ExplorerTransferDialog(operation);
}
