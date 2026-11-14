// SPDX-License-Identifier: GPL-2.0-or-later
#include "explorertransferdialog.h"
#include "explorertransfercontroller.h"
#include <fileoperation.h>

#include <QFileInfo>
#include <QLocale>
#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>
#include <algorithm>

// Geometry/colours measured from the Windows 11 copy dialog; see
// docs/fidelity/transfer-dialog-measurements.md. 96 DPI, device pixel ratio 1.
namespace {
constexpr int kWidth = 449;
constexpr int kCollapsedHeight = 165;
constexpr int kExpandedHeight = 287;
constexpr int kCollapsedSeparatorY = 120;
constexpr int kExpandedSeparatorY = 242;
constexpr int kContentX = 27;          // bar/graph outer left edge
constexpr int kContentWidth = 395;     // bar/graph outer width (with border)
constexpr int kBarY = 90;
constexpr int kBarHeight = 15;
constexpr int kGraphHeight = 80;
const QColor kBackground(44, 44, 44);        // #2C2C2C
const QColor kAccentRunning(96, 205, 255);   // #60CDFF
const QColor kAccentPaused(252, 225, 0);     // #FCE100
const QColor kGlyph(255, 255, 255);
const QColor kGlyphDim(121, 121, 121);       // #797979 (dimmed ▶ while cancelling)
const QColor kBorder(87, 87, 87);            // #575757
const QColor kTrack(56, 56, 56);             // #383838

QColor blend(const QColor& base, const QColor& over, double amount) {
    const auto mix = [amount](int a, int b) { return int(a + (b - a) * amount + 0.5); };
    return QColor(mix(base.red(), over.red()), mix(base.green(), over.green()), mix(base.blue(), over.blue()));
}

// Glyphs are drawn, not font characters: no Microsoft font/asset is bundled.
class GlyphButton final : public QPushButton {
public:
    explicit GlyphButton(const QString& glyph, QWidget* parent) : QPushButton(parent) {
        setProperty("glyph", glyph);
        setFlat(true);
        setFocusPolicy(Qt::TabFocus);
    }
    void setGlyph(const QString& glyph) { setProperty("glyph", glyph); update(); }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        if(isEnabled() && (underMouse() || isDown())) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(255, 255, 255, isDown() ? 10 : 16));
            p.drawRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), 4, 4);
        }
        const QColor colour = isEnabled() ? kGlyph : kGlyphDim;
        const QPointF c(width() / 2.0, height() / 2.0);
        const QString glyph = property("glyph").toString();
        p.setPen(Qt::NoPen);
        p.setBrush(colour);
        if(glyph == QLatin1String("pause")) {           // measured box 9x8
            p.drawRect(QRectF(c.x() - 4.5, c.y() - 4, 3, 8));
            p.drawRect(QRectF(c.x() + 1.5, c.y() - 4, 3, 8));
        } else if(glyph == QLatin1String("resume")) {   // measured box ~8x9
            QPainterPath tri;
            tri.moveTo(c.x() - 3.5, c.y() - 4.5);
            tri.lineTo(c.x() - 3.5, c.y() + 4.5);
            tri.lineTo(c.x() + 4.0, c.y());
            tri.closeSubpath();
            p.drawPath(tri);
        } else {                                        // cancel: 9x8 cross
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(colour, 1.1));
            p.drawLine(QPointF(c.x() - 4, c.y() - 4), QPointF(c.x() + 4, c.y() + 4));
            p.drawLine(QPointF(c.x() - 4, c.y() + 4), QPointF(c.x() + 4, c.y() - 4));
        }
    }
};

// Footer row "⌄ Mais detalhes": chevron at x 32..40 and text at x 52 (dialog coordinates).
class FooterButton final : public QPushButton {
public:
    explicit FooterButton(QWidget* parent) : QPushButton(parent) { setFlat(true); setCursor(Qt::PointingHandCursor); }
    bool expanded = false;
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        if(underMouse() || isDown()) p.fillRect(rect().adjusted(2, 2, -2, -2), QColor(255, 255, 255, isDown() ? 6 : 10));
        const double cy = height() / 2.0;
        p.setPen(QPen(kGlyph, 1.1));
        p.setBrush(Qt::NoBrush);
        QPainterPath chevron;
        if(expanded) { chevron.moveTo(31, cy + 2); chevron.lineTo(35.5, cy - 2); chevron.lineTo(40, cy + 2); }
        else         { chevron.moveTo(31, cy - 2); chevron.lineTo(35.5, cy + 2); chevron.lineTo(40, cy - 2); }
        p.drawPath(chevron);
        QFont f = font();
        f.setPixelSize(12);
        p.setFont(f);
        p.drawText(QRectF(51, 0, width() - 51, height()), Qt::AlignVCenter | Qt::AlignLeft, text());
    }
};

// Speed graph: only ever plots rates computed from real byte observations.
class SpeedGraph final : public QWidget {
public:
    explicit SpeedGraph(QWidget* parent) : QWidget(parent) {}
    void setData(const QVector<ExplorerTransferDialog::SpeedSample>& samples, double progress, bool yellow) {
        samples_ = samples; progress_ = progress; yellow_ = yellow; update();
    }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        const QColor accent = yellow_ ? kAccentPaused : kAccentRunning;
        const int w = width(), h = height();
        p.fillRect(rect(), kBackground);
        const bool hasData = !samples_.isEmpty();
        const int plotRight = hasData ? 1 + int(std::clamp(progress_, 0.0, 1.0) * (w - 2) + 0.5) : 1;
        if(hasData) p.fillRect(QRect(1, 1, plotRight - 1, h - 2), blend(kBackground, accent, 0.15));
        const QColor gridOut = blend(kBackground, accent, 0.08), gridIn = blend(kBackground, accent, 0.22);
        for(int k = 1; k < 10; ++k) {
            const int x = int(w * k / 10.0 + 0.5);
            p.fillRect(QRect(x, 1, 1, h - 2), x < plotRight ? gridIn : gridOut);
        }
        for(int y = 2; y < h - 1; y += 15) {
            p.fillRect(QRect(1, y, plotRight - 1, 1), gridIn);
            p.fillRect(QRect(plotRight, y, w - 1 - plotRight, 1), gridOut);
        }
        if(hasData) {
            double peak = 0;
            for(const auto& s : samples_) peak = std::max(peak, s.bytesPerSecond);
            const double plotH = (h - 4) * 0.85;
            const int bottom = h - 2;
            QPolygonF poly;
            poly << QPointF(1, bottom);
            bool first = true;
            for(const auto& s : samples_) {
                const double x = 1 + std::clamp(s.fraction, 0.0, 1.0) * (w - 2);
                const double y = bottom - (peak > 0 ? s.bytesPerSecond / peak : 0) * plotH;
                if(first) { poly << QPointF(1, y); first = false; }
                poly << QPointF(x, y);
            }
            poly << QPointF(poly.last().x(), bottom);
            p.setPen(Qt::NoPen);
            p.setBrush(accent);
            p.drawPolygon(poly);
        }
        // Measured: a 1 px #383838 ring sits just inside the border, over tint and curve alike.
        p.setPen(kTrack);
        p.setBrush(Qt::NoBrush);
        p.drawRect(1, 1, w - 3, h - 3);
        p.setPen(kBorder);
        p.setBrush(Qt::NoBrush);
        p.drawRect(0, 0, w - 1, h - 1);
    }
private:
    QVector<ExplorerTransferDialog::SpeedSample> samples_;
    double progress_ = 0;
    bool yellow_ = false;
};

QString formatRemaining(unsigned int seconds) {
    if(seconds < 60) return QObject::tr("%1 s").arg(seconds);
    if(seconds < 3600) return QObject::tr("%1 min %2 s").arg(seconds / 60).arg(seconds % 60);
    return QObject::tr("%1 h %2 min").arg(seconds / 3600).arg((seconds % 3600) / 60);
}
}  // namespace

ExplorerTransferDialog::ExplorerTransferDialog(Fm::FileOperation* operation)
    : Fm::FileOperationDialog(operation), content_(new QWidget(this)),
      summary_(new QLabel(content_)), progressText_(new QLabel(content_)),
      detailName_(new QLabel(content_)), detailRemaining_(new QLabel(content_)), detailItems_(new QLabel(content_)),
      progress_(new QProgressBar(content_)), separator_(new QWidget(content_)), graph_(new SpeedGraph(content_)),
      detailsButton_(new FooterButton(content_)),
      pauseButton_(new GlyphButton(QStringLiteral("pause"), content_)),
      cancelButton_(new GlyphButton(QStringLiteral("cancel"), content_)),
      operation_(operation) {
    clock_.start();
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
    for(QWidget* child : findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly))
        if(child != content_) child->hide();
    if(layout()) {
        // The base dialog's own (now hidden) layout must not manage or squash content_:
        // its spacers/nested layouts gave content_ height 0 in the collapsed state.
        layout()->setContentsMargins(0, 0, 0, 0);
        layout()->setSpacing(0);
        layout()->setEnabled(false);
    }
    content_->setObjectName(QStringLiteral("transferContent"));
    content_->setAttribute(Qt::WA_StyledBackground, true);
    content_->setStyleSheet(QStringLiteral("#transferContent{background:#2C2C2C;border:1px solid #3D3D3D;}"
                                           "#transferContent QLabel{background:transparent;color:#FFFFFF;}"));
    summary_->setObjectName(QStringLiteral("transferSummary"));
    summary_->setTextFormat(Qt::RichText);
    summary_->setStyleSheet(QStringLiteral("QLabel{color:#D0D0D0;font-size:12px;}"));
    summary_->setText(tr("Copiando 1 item"));
    progressText_->setObjectName(QStringLiteral("transferPercent"));
    progressText_->setStyleSheet(QStringLiteral("QLabel{font-size:14px;}"));
    progressText_->setText(tr("0% concluído"));
    for(QLabel* l : {detailName_, detailRemaining_, detailItems_}) {
        l->setStyleSheet(QStringLiteral("QLabel{font-size:12px;}"));
        l->hide();
    }
    detailName_->setObjectName(QStringLiteral("transferDetailName"));
    detailRemaining_->setObjectName(QStringLiteral("transferDetailRemaining"));
    detailItems_->setObjectName(QStringLiteral("transferDetailItems"));
    progress_->setObjectName(QStringLiteral("transferProgress"));
    progress_->setRange(0, 100);
    progress_->setValue(0);
    progress_->setTextVisible(false);
    separator_->setObjectName(QStringLiteral("transferSeparator"));
    separator_->setAttribute(Qt::WA_StyledBackground, true);
    separator_->setStyleSheet(QStringLiteral("#transferSeparator{background:#4D4D4D;}"));
    graph_->setObjectName(QStringLiteral("transferSpeedGraph"));
    graph_->hide();
    detailsButton_->setObjectName(QStringLiteral("transferDetailsButton"));
    detailsButton_->setText(tr("Mais detalhes"));
    pauseButton_->setObjectName(QStringLiteral("pauseButton"));
    pauseButton_->setAccessibleName(tr("Pausar"));
    pauseButton_->setToolTip(tr("Pausar"));
    pauseButton_->hide();
    cancelButton_->setObjectName(QStringLiteral("transferCancelButton"));
    cancelButton_->setAccessibleName(tr("Cancelar"));
    cancelButton_->setToolTip(tr("Cancelar"));

    updateDetailTexts();
    updatePresentation();
    relayout();
    updateTitle();
    connect(detailsButton_, &QPushButton::clicked, this, &ExplorerTransferDialog::toggleDetails);
    connect(pauseButton_, &QPushButton::clicked, this, &ExplorerTransferDialog::togglePause);
    connect(cancelButton_, &QPushButton::clicked, this, &ExplorerTransferDialog::reject);
    if(operation && operation->isRunning() && operation->job()) {
        controller_ = new ExplorerTransferController(operation->job(), this);
        if(controller_->attachLate() && controller_->supportsPause()) {
            setPauseAvailable(true);
            connect(controller_, &ExplorerTransferController::telemetryUpdated,
                    this, &ExplorerTransferDialog::refreshPauseState);
            refreshPauseState();
        } else {
            delete controller_;
            controller_ = nullptr;
        }
    }
}

void ExplorerTransferDialog::relayout() {
    const int totalHeight = expanded_ ? kExpandedHeight : kCollapsedHeight;
    setFixedSize(kWidth, totalHeight);
    content_->setGeometry(0, 0, kWidth, totalHeight);   // height() is not yet updated for a never-shown dialog
    const int sepY = expanded_ ? kExpandedSeparatorY : kCollapsedSeparatorY;
    summary_->setGeometry(28, 43, 393, 18);
    progressText_->setGeometry(28, 62, 320, 20);
    pauseButton_->setGeometry(350, 57, 32, 31);   // centre (366, 72.5) in the measured window
    cancelButton_->setGeometry(397, 57, 32, 31);  // centre (413, 72.5)
    progress_->setGeometry(kContentX, kBarY, kContentWidth, kBarHeight);
    graph_->setGeometry(kContentX, kBarY, kContentWidth, kGraphHeight);
    detailName_->setGeometry(28, 181, 393, 16);
    detailRemaining_->setGeometry(28, 197, 393, 16);
    detailItems_->setGeometry(28, 213, 393, 16);
    separator_->setGeometry(3, sepY, kWidth - 6, 1);
    detailsButton_->setGeometry(1, sepY + 1, kWidth - 2, totalHeight - sepY - 3);
}

void ExplorerTransferDialog::setPauseAvailable(bool available) {
    pauseAvailable_ = available;
    pauseButton_->setVisible(available);
    if(available) applyTransferState(ExplorerTransferController::State::Running, false);
}

bool ExplorerTransferDialog::detailsExpanded() const { return expanded_; }

void ExplorerTransferDialog::setSourceFiles(const Fm::FilePathList& files) {
    sourceCount_ = std::max<std::size_t>(files.size(), 1);
    if(!files.empty()) {
        const QString path = QString::fromUtf8(files.front().toString().get());
        sourceName_ = QFileInfo(QFileInfo(path).absolutePath()).fileName();
        if(sourceName_.isEmpty()) sourceName_ = QFileInfo(path).fileName();
    }
    updateTitle();
    setCurFile(currentFileName_);
}
void ExplorerTransferDialog::setDestPath(const Fm::FilePath& destination) {
    const QString path = QString::fromUtf8(destination.toString().get());
    destinationName_ = QFileInfo(path).fileName();
    if(destinationName_.isEmpty()) destinationName_ = path;
}
void ExplorerTransferDialog::setPrepared() {
    const QString count = QString::number(filesTotal_ ? filesTotal_ : sourceCount_);
    const QString link = QStringLiteral("<a href=\"#\" style=\"color:#60CDFF;text-decoration:none\">%1</a>");
    if(showsDestination_ && !sourceName_.isEmpty() && !destinationName_.isEmpty())
        summary_->setText(tr("%1 %2 item de %3 para %4")
                          .arg(verb_, count, link.arg(sourceName_.toHtmlEscaped()), link.arg(destinationName_.toHtmlEscaped())));
    else if(!sourceName_.isEmpty())
        summary_->setText(tr("%1 %2 item: %3").arg(verb_, count, sourceName_.toHtmlEscaped()));
}
void ExplorerTransferDialog::setCurFile(QString file) {
    currentFileName_ = QFileInfo(file).fileName();
    if(currentFileName_.isEmpty()) currentFileName_ = file;
    updateDetailTexts();
}
void ExplorerTransferDialog::setPercent(unsigned int percent) {
    percent_ = qMin(percent, 100u);
    progress_->setValue(static_cast<int>(percent_));
    updateTitle();
    updatePresentation();
}
void ExplorerTransferDialog::setDataTransferred(std::uint64_t finished, std::uint64_t total) {
    transferred_ = finished; total_ = total;
    recordSpeedSample(finished, total, clock_.elapsed());
    updateDetailTexts();
}
void ExplorerTransferDialog::setFilesProcessed(std::uint64_t finished, std::uint64_t total) {
    filesDone_ = finished; filesTotal_ = total;
    setPrepared();
    updateDetailTexts();
}
void ExplorerTransferDialog::setRemainingTime(unsigned int seconds) {
    // Only libfm's own estimate is shown; until it has one the row says "Calculando…".
    remainingSeconds_ = seconds;
    updateDetailTexts();
}

bool ExplorerTransferDialog::recordSpeedSample(std::uint64_t bytes, std::uint64_t total, qint64 atMs) {
    if(!haveLastObservation_) {
        haveLastObservation_ = true;
        lastBytes_ = bytes;
        lastMs_ = atMs;
        return false;                       // a rate needs an interval
    }
    if(atMs <= lastMs_ || bytes < lastBytes_ || total == 0) return false;
    if(bytes == lastBytes_) {               // stalled/paused: record no invented zero, keep the clock current
        lastMs_ = atMs;
        return false;
    }
    SpeedSample sample;
    sample.bytesPerSecond = double(bytes - lastBytes_) * 1000.0 / double(atMs - lastMs_);
    sample.fraction = std::min(1.0, double(bytes) / double(total));
    lastBytes_ = bytes;
    lastMs_ = atMs;
    constexpr int kMaxSamples = 4096;
    if(samples_.size() >= kMaxSamples) samples_.remove(0);
    samples_.append(sample);
    updatePresentation();
    return true;
}

QVector<ExplorerTransferDialog::SpeedSample> ExplorerTransferDialog::speedSamples() const { return samples_; }

void ExplorerTransferDialog::updateDetailTexts() {
    detailName_->setText(tr("Nome: %1").arg(currentFileName_));
    detailRemaining_->setText(remainingSeconds_ ? tr("Tempo restante: %1").arg(formatRemaining(remainingSeconds_))
                                                : tr("Tempo restante: Calculando…"));
    const std::uint64_t items = filesTotal_ ? (filesTotal_ > filesDone_ ? filesTotal_ - filesDone_ : 0) : sourceCount_;
    if(total_)
        detailItems_->setText(tr("Itens restantes: %1 (%2)").arg(items).arg(
            QLocale().formattedDataSize(qint64(total_ > transferred_ ? total_ - transferred_ : 0), 2, QLocale::DataSizeSIFormat)));
    else
        detailItems_->setText(tr("Itens restantes: %1").arg(items));
}

void ExplorerTransferDialog::updateTitle() {
    const QString base = tr("%1% concluído").arg(percent_);
    const QString text = cancelRequested_ ? tr("Cancelando - %1").arg(base)
                       : paused_ ? tr("Pausado - %1").arg(base) : base;
    setWindowTitle(text);
    progressText_->setText(text);
}

void ExplorerTransferDialog::updatePresentation() {
    const bool yellow = paused_ || cancelRequested_;
    const QColor accent = yellow ? kAccentPaused : kAccentRunning;
    progress_->setStyleSheet(QStringLiteral("QProgressBar{border:1px solid %1;background:%2;}"
                                            "QProgressBar::chunk{background:%3;margin:0;}")
                             .arg(kBorder.name(), kTrack.name(), accent.name()));
    static_cast<SpeedGraph*>(graph_)->setData(samples_, percent_ / 100.0, yellow);
}

void ExplorerTransferDialog::applyTransferState(ExplorerTransferController::State state, bool pauseAcknowledged) {
    using State = ExplorerTransferController::State;
    if(state == State::Cancelling) cancelRequested_ = true;
    // Paused only after a real backend ACK; Cancelling keeps the last known pause state.
    if(state == State::Paused || (state == State::Pausing && pauseAcknowledged)) paused_ = true;
    else if(state == State::Running || state == State::Preparing || state == State::Pausing) paused_ = false;
    auto* pause = static_cast<GlyphButton*>(pauseButton_);
    if(cancelRequested_) {
        pause->setGlyph(paused_ ? QStringLiteral("resume") : QStringLiteral("pause"));
        pause->setEnabled(false);
        cancelButton_->setEnabled(false);
    } else if(state == State::Pausing && !pauseAcknowledged) {
        // Keep the UI honest: no Resume until the backend acknowledges the pause.
        pause->setGlyph(QStringLiteral("pause"));
        pause->setEnabled(false);
        pause->setAccessibleName(tr("Pausando…"));
        pause->setToolTip(tr("Pausando…"));
    } else if(paused_) {
        pause->setGlyph(QStringLiteral("resume"));
        pause->setEnabled(true);
        pause->setAccessibleName(tr("Retomar"));
        pause->setToolTip(tr("Retomar"));
    } else if(state == State::Running || state == State::Preparing) {
        pause->setGlyph(QStringLiteral("pause"));
        pause->setEnabled(true);
        pause->setAccessibleName(tr("Pausar"));
        pause->setToolTip(tr("Pausar"));
    } else {
        pause->setEnabled(false);
    }
    updateTitle();
    updatePresentation();
}

void ExplorerTransferDialog::reject() {
    // No confirmation and no hiding: libfm's base reject() would hide the dialog
    // at once, but Windows keeps it as "Cancelando" until the job really ends
    // (FileOperation destroys it on completion).
    if(cancelRequested_) return;
    if(controller_) controller_->cancel();
    else if(operation_) operation_->cancel();
    applyTransferState(ExplorerTransferController::State::Cancelling, false);
}

void ExplorerTransferDialog::toggleDetails() {
    expanded_ = !expanded_;
    progress_->setVisible(!expanded_);
    graph_->setVisible(expanded_);
    for(QLabel* l : {detailName_, detailRemaining_, detailItems_}) l->setVisible(expanded_);
    detailsButton_->setText(expanded_ ? tr("Menos detalhes") : tr("Mais detalhes"));
    static_cast<FooterButton*>(detailsButton_)->expanded = expanded_;
    relayout();
    detailsButton_->update();
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
    applyTransferState(snapshot.state, snapshot.paused);
}

Fm::FileOperationDialog* createExplorerTransferDialog(Fm::FileOperation* operation) {
    return new ExplorerTransferDialog(operation);
}
