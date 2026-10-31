// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Grekto
// Original window chrome and hand-drawn icons; no Microsoft assets.
#include "mainwindow.h"
#include "application.h"
#include "tabpage.h"
#include <QToolBar>
#include <QToolButton>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QIconEngine>
#include <QWindow>
#include <QComboBox>
#include <QMenuBar>
#include <QSignalBlocker>
#include <libfm-qt6/pathbar.h>

namespace PCManFM {
namespace {
class LineIcon : public QIconEngine {
public:
    explicit LineIcon(QString name) : name_(std::move(name)) {}
    QIconEngine* clone() const override { return new LineIcon(name_); }
    QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override {
        QPixmap image(size); image.fill(Qt::transparent);
        QPainter painter(&image); paint(&painter, QRect(QPoint(), size), mode, state);
        return image;
    }
    void paint(QPainter* p, const QRect& rect, QIcon::Mode mode, QIcon::State) override {
        p->save(); p->setRenderHint(QPainter::Antialiasing);
        p->translate(rect.x(), rect.y()); p->scale(rect.width()/24.0,rect.height()/24.0);
        const QColor ink = mode == QIcon::Disabled ? QColor(160,160,160) : QColor(45,45,45);
        p->setPen(QPen(ink,1.35,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin)); p->setBrush(Qt::NoBrush);
        auto line = [p](qreal a,qreal b,qreal c,qreal d) { p->drawLine(QPointF(a,b),QPointF(c,d)); };
        if(name_ == QLatin1String("back") || name_ == QLatin1String("forward")) {
            if(name_ == QLatin1String("forward")) { p->translate(24,0); p->scale(-1,1); }
            line(5,12,19,12); line(5,12,11,6); line(5,12,11,18);
        } else if(name_ == QLatin1String("up")) {
            line(12,5,12,19); line(12,5,6,11); line(12,5,18,11);
        } else if(name_ == QLatin1String("refresh")) {
            p->drawArc(QRectF(5,5,14,14),40*16,300*16); line(19,4,19,9); line(19,9,14,9);
        } else if(name_ == QLatin1String("plus")) {
            line(12,5,12,19); line(5,12,19,12);
        } else if(name_ == QLatin1String("minimize")) {
            line(7,12,17,12);
        } else if(name_ == QLatin1String("maximize")) {
            p->drawRect(QRectF(7,7,10,10));
        } else if(name_ == QLatin1String("restore")) {
            p->drawRect(QRectF(6,9,9,9)); line(9,9,9,6); line(9,6,18,6); line(18,6,18,15); line(18,15,15,15);
        } else if(name_ == QLatin1String("close")) {
            line(7,7,17,17); line(17,7,7,17);
        } else if(name_ == QLatin1String("cut")) {
            p->drawEllipse(QRectF(4,14,5,5)); p->drawEllipse(QRectF(15,14,5,5)); line(8,15,17,4); line(16,15,7,4);
        } else if(name_ == QLatin1String("copy")) {
            if(mode != QIcon::Disabled) p->setPen(QPen(QColor(0,103,192),1.35));
            p->drawRoundedRect(QRectF(8,4,11,13),1.5,1.5);
            QPainterPath path; path.moveTo(5,8); path.lineTo(5,19); path.lineTo(15,19); p->drawPath(path);
        } else if(name_ == QLatin1String("paste")) {
            p->drawRoundedRect(QRectF(6,6,12,14),1.5,1.5); p->drawRoundedRect(QRectF(9,3,6,5),1,1);
        } else if(name_ == QLatin1String("rename")) {
            p->drawRoundedRect(QRectF(3,7,18,11),1.5,1.5);
            if(mode != QIcon::Disabled) p->setPen(QPen(QColor(0,103,192),1.35));
            line(14,4,14,21); line(12,4,16,4); line(12,21,16,21); line(6,12,10,12);
        } else if(name_ == QLatin1String("delete")) {
            if(mode != QIcon::Disabled) p->setPen(QPen(QColor(190,43,43),1.35));
            line(4,7,20,7); line(9,4,15,4); line(6,7,7,20); line(7,20,17,20); line(17,20,18,7); line(10,10,10,17); line(14,10,14,17);
        } else if(name_ == QLatin1String("sort")) {
            line(7,4,7,20); line(4,17,7,20); line(7,20,10,17); line(13,6,20,6); line(13,12,18,12); line(13,18,16,18);
        } else if(name_ == QLatin1String("view")) {
            p->drawRoundedRect(QRectF(3,5,18,14),1,1); line(3,10,21,10); line(9,10,9,19); line(15,10,15,19);
        } else if(name_ == QLatin1String("more")) {
            p->setBrush(ink); p->setPen(Qt::NoPen);
            for(int x : {5,12,19}) p->drawEllipse(QPointF(x,12),1.2,1.2);
        } else if(name_ == QLatin1String("search")) {
            p->drawEllipse(QRectF(4,4,11,11)); line(14,14,20,20);
        } else if(name_ == QLatin1String("folder")) {
            p->setPen(Qt::NoPen); p->setBrush(mode == QIcon::Disabled ? QColor(180,180,180) : QColor(241,170,38));
            QPainterPath back; back.moveTo(2,6); back.quadTo(2,4,4,4); back.lineTo(10,4); back.lineTo(12,7); back.lineTo(20,7); back.quadTo(22,7,22,9); back.lineTo(22,19); back.lineTo(2,19); back.closeSubpath(); p->drawPath(back);
            p->setBrush(mode == QIcon::Disabled ? QColor(200,200,200) : QColor(255,207,82)); p->drawRoundedRect(QRectF(2,9,20,11),2,2);
        }
        p->restore();
    }
private:
    QString name_;
};
QIcon icon(const char* name) { return QIcon(new LineIcon(QString::fromLatin1(name))); }

class TitleBar : public QWidget {
public:
    TitleBar(QWidget* parent, TabBar* tabs, QAction* newTab) : QWidget(parent) {
        setObjectName(QStringLiteral("explorerTitleBar")); setFixedHeight(40);
        auto* row = new QHBoxLayout(this); row->setContentsMargins(8,0,0,0); row->setSpacing(0);
        tabs->setProperty("explorerTabs",true); tabs->setObjectName(QStringLiteral("explorerTabs"));
        tabs->setSizePolicy(QSizePolicy::Maximum,QSizePolicy::Preferred);
        tabs->setAutoHide(false); tabs->setTabsClosable(true); tabs->setIconSize(QSize(16,16));
        row->addWidget(tabs,0,Qt::AlignBottom);
        auto* add = new QToolButton(this); add->setObjectName(QStringLiteral("explorerNewTab"));
        newTab->setIcon(icon("plus")); add->setDefaultAction(newTab); add->setAccessibleName(tr("Nova guia")); add->setFixedSize(36,32);
        row->addWidget(add,0,Qt::AlignBottom); row->addStretch(1);
        auto button = [this,row](const char* name,const char* glyph,const QString& label) {
            auto* b = new QToolButton(this); b->setObjectName(QString::fromLatin1(name));
            b->setIcon(icon(glyph)); b->setIconSize(QSize(20,20)); b->setToolTip(label); b->setAccessibleName(label); b->setFixedSize(46,32);
            row->addWidget(b,0,Qt::AlignTop); return b;
        };
        auto* minimize = button("explorerMinimize","minimize",tr("Minimizar"));
        connect(minimize,&QToolButton::clicked,this,[this] { window()->showMinimized(); });
        maximize_ = button("explorerMaximize","maximize",tr("Maximizar"));
        connect(maximize_,&QToolButton::clicked,this,[this] { toggleMaximized(); });
        auto* close = button("explorerClose","close",tr("Fechar"));
        connect(close,&QToolButton::clicked,this,[this] { window()->close(); });
        window()->installEventFilter(this);
    }
protected:
    bool eventFilter(QObject* watched,QEvent* event) override {
        if(watched == window() && event->type() == QEvent::WindowStateChange) {
            const bool maximized = window()->isMaximized();
            maximize_->setIcon(icon(maximized ? "restore" : "maximize"));
            maximize_->setToolTip(maximized ? tr("Restaurar") : tr("Maximizar")); maximize_->setAccessibleName(maximize_->toolTip());
        }
        return QWidget::eventFilter(watched,event);
    }
    void mousePressEvent(QMouseEvent* event) override {
        if(event->button() == Qt::LeftButton && window()->windowHandle()) { window()->windowHandle()->startSystemMove(); event->accept(); }
        else QWidget::mousePressEvent(event);
    }
    void mouseDoubleClickEvent(QMouseEvent* event) override {
        if(event->button() == Qt::LeftButton) { toggleMaximized(); event->accept(); }
        else QWidget::mouseDoubleClickEvent(event);
    }
private:
    void toggleMaximized() { if(window()->isMaximized()) window()->showNormal(); else window()->showMaximized(); }
    QToolButton* maximize_;
};

// Native WM resizing is retained with a client-drawn title strip.
class FrameController : public QObject {
public:
    explicit FrameController(QWidget* window) : QObject(window),window_(window) { window_->setMouseTracking(true); qApp->installEventFilter(this); }
protected:
    bool eventFilter(QObject* watched,QEvent* event) override {
        auto* w = qobject_cast<QWidget*>(watched);
        if(!w || w->window() != window_ || window_->isMaximized()) return false;
        if(event->type() != QEvent::MouseButtonPress && event->type() != QEvent::MouseMove) return false;
        auto* mouse = static_cast<QMouseEvent*>(event);
        const QPoint p = window_->mapFromGlobal(mouse->globalPosition().toPoint());
        Qt::Edges edges;
        if(p.x() < 5) edges |= Qt::LeftEdge;
        if(p.x() >= window_->width()-5) edges |= Qt::RightEdge;
        if(p.y() < 5) edges |= Qt::TopEdge;
        if(p.y() >= window_->height()-5) edges |= Qt::BottomEdge;
        if(event->type() == QEvent::MouseButtonPress && mouse->button() == Qt::LeftButton && edges && window_->windowHandle()) return window_->windowHandle()->startSystemResize(edges);
        if(event->type() == QEvent::MouseMove) {
            if(cursorWidget_ && (cursorWidget_ != w || !edges)) cursorWidget_->unsetCursor();
            cursorWidget_.clear();
            if(edges) {
                const bool h = edges.testFlag(Qt::LeftEdge) || edges.testFlag(Qt::RightEdge);
                const bool v = edges.testFlag(Qt::TopEdge) || edges.testFlag(Qt::BottomEdge);
                Qt::CursorShape shape = h ? Qt::SizeHorCursor : Qt::SizeVerCursor;
                if(h && v) shape = (edges.testFlag(Qt::TopEdge) == edges.testFlag(Qt::LeftEdge)) ? Qt::SizeFDiagCursor : Qt::SizeBDiagCursor;
                w->setCursor(shape); cursorWidget_ = w;
            }
        }
        return false;
    }
private:
    QWidget* window_;
    QPointer<QWidget> cursorWidget_;
};
}

ViewFrame* MainWindow::viewFrameForTabBar(TabBar* tabBar) {
    for(int i=0;i<ui.viewSplitter->count();++i) {
        auto* frame = qobject_cast<ViewFrame*>(ui.viewSplitter->widget(i));
        if(frame && frame->getTabBar() == tabBar) return frame;
    }
    return nullptr;
}

void MainWindow::updateExplorerSearch() {
    if(explorerSearch_ && currentPage()) {
        const QSignalBlocker blocker(explorerSearch_);
        explorerSearch_->setText(currentPage()->getFilterStr());
    }
}

void MainWindow::setupExplorerChrome() {
    setWindowFlag(Qt::FramelessWindowHint); setContentsMargins(1,1,1,1); setMinimumSize(760,420);
    setFont(QFont(QStringLiteral("Noto Sans"),9)); setWindowIcon(icon("folder"));
    setStyleSheet(QStringLiteral(R"CSS(
QMainWindow#MainWindow { background:#f8f8f8; border:1px solid #cfcfcf; }
QWidget { color:#202020; }
QToolBar { border:0; spacing:4px; background:#f8f8f8; }
QToolBar#explorerTitleToolbar, QWidget#explorerTitleBar { background:#e8e8e8; padding:0; margin:0; }
QToolBar#explorerNavigation { min-height:48px; max-height:48px; padding:0 8px; border-bottom:1px solid #e7e7e7; }
QToolBar#explorerCommands { min-height:46px; max-height:46px; background:white; padding:0 12px; border-bottom:1px solid #d6d6d6; }
QToolButton { border:0; background:transparent; border-radius:4px; padding:5px; }
QToolButton:hover { background:#eaeaea; }
QToolButton:pressed, QToolButton:checked { background:#dedede; }
QToolButton:disabled { color:#a0a0a0; }
QToolButton#explorerClose:hover { background:#e81123; }
QToolBar::separator { background:#e5e5e5; width:1px; margin:12px 8px; }
QTabBar::tab { background:transparent; border:0; border-top-left-radius:7px; border-top-right-radius:7px; padding:0 12px; margin-top:8px; }
QTabBar::tab:selected { background:#f8f8f8; }
QTabBar::tab:!selected:hover { background:#eeeeee; }
QTabBar::close-button { image:url(:/icons/win11-explorer-qt/scalable/places/window-close.svg); width:16px; height:16px; }
QTabBar::close-button:hover { background:#dddddd; border-radius:3px; }
QWidget#explorerAddress, QLineEdit#explorerSearch { background:#fdfdfd; border:1px solid #e1e1e1; border-radius:4px; }
QWidget#explorerAddress QToolButton { padding:3px 7px; border:0; }
QLineEdit { background:#fdfdfd; border:1px solid #dfdfdf; padding:4px 8px; border-radius:4px; selection-background-color:#cce8ff; }
QLineEdit:focus { border-bottom:2px solid #0067c0; }
QSplitter::handle { background:#e5e5e5; width:1px; }
QWidget#sidePane { background:white; }
QWidget#sidePane QTreeView { background:white; border:0; outline:0; }
QWidget#sidePane QTreeView::item { min-height:30px; padding-left:8px; }
QAbstractItemView { background:white; border:0; selection-background-color:#cce8ff; selection-color:#202020; outline:0; }
QAbstractItemView::item:hover { background:#f1f1f1; }
QAbstractItemView::item:selected { background:#cce8ff; color:#202020; }
QHeaderView { background:white; }
QHeaderView::section { background:white; color:#555; border:0; border-right:1px solid #ededed; border-bottom:1px solid #ededed; padding:6px 8px; }
QStatusBar { background:white; border-top:1px solid #eeeeee; min-height:24px; }
QStatusBar::item { border:0; }
QMenu { background:#fafafa; color:#202020; border:1px solid #d8d8d8; padding:5px; }
QMenu::item { padding:7px 28px 7px 12px; border-radius:4px; }
QMenu::item:selected { background:#e9e9e9; }
QMenu::item:disabled { color:#a0a0a0; }
QMenu::separator { height:1px; background:#dedede; margin:4px; }
QToolTip { background:#fafafa; color:#202020; border:1px solid #d8d8d8; padding:6px; }
)CSS"));
    new FrameController(this);
    ui.menubar->hide(); ui.actionMenu_bar->setVisible(false);
    ui.toolBar->clear(); ui.toolBar->setObjectName(QStringLiteral("explorerNavigation"));
    ui.toolBar->setFixedHeight(49); ui.toolBar->setIconSize(QSize(18,18)); ui.toolBar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    auto nav = [this](QAction* action,const char* name,const char* glyph,const QString& label) {
        action->setIcon(icon(glyph)); action->setToolTip(label); action->setText(label); ui.toolBar->addAction(action);
        auto* b = qobject_cast<QToolButton*>(ui.toolBar->widgetForAction(action));
        b->setObjectName(QString::fromLatin1(name)); b->setAccessibleName(label); b->setFixedSize(44,32);
        if(action == ui.actionGoBack || action == ui.actionGoForward) {
            b->setContextMenuPolicy(Qt::CustomContextMenu);
            connect(b,&QToolButton::customContextMenuRequested,this,&MainWindow::onBackForwardContextMenu);
        }
    };
    nav(ui.actionGoBack,"explorerBack","back",tr("Voltar")); nav(ui.actionGoForward,"explorerForward","forward",tr("Avançar"));
    nav(ui.actionGoUp,"explorerUp","up",tr("Uma pasta acima")); nav(ui.actionReload,"explorerRefresh","refresh",tr("Atualizar"));
    ui.toolBar->addAction(ui.actionGo); ui.actionGo->setVisible(false);
    explorerSearch_ = new QLineEdit(this); explorerSearch_->setObjectName(QStringLiteral("explorerSearch"));
    explorerSearch_->setPlaceholderText(tr("Filtrar esta pasta")); explorerSearch_->setAccessibleName(tr("Filtrar arquivos nesta pasta"));
    explorerSearch_->setToolTip(tr("Filtro local por nome. Para pesquisa recursiva, use Buscar arquivos no menu Mais."));
    explorerSearch_->setClearButtonEnabled(true); explorerSearch_->setFixedSize(230,32);
    explorerSearch_->addAction(icon("search"),QLineEdit::TrailingPosition); ui.toolBar->addWidget(explorerSearch_);
    connect(explorerSearch_, &QLineEdit::textChanged, this, [this](const QString& text) {
        if(auto* page = currentPage()) {
            page->setFilterStr(text);
            page->applyFilter();
        }
    });
    auto* titleToolbar = new QToolBar(this); titleToolbar->setObjectName(QStringLiteral("explorerTitleToolbar"));
    titleToolbar->setMovable(false); titleToolbar->setFloatable(false); titleToolbar->setFixedHeight(40);
    auto* tabs = activeViewFrame_->getTabBar(); activeViewFrame_->layout()->removeWidget(tabs);
    explorerTitle_ = new TitleBar(titleToolbar,tabs,ui.actionNewTab); explorerTitle_->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);
    titleToolbar->addWidget(explorerTitle_); insertToolBar(ui.toolBar,titleToolbar); insertToolBarBreak(ui.toolBar);
    explorerCommands_ = new QToolBar(this); explorerCommands_->setObjectName(QStringLiteral("explorerCommands"));
    explorerCommands_->setMovable(false); explorerCommands_->setFloatable(false); explorerCommands_->setFixedHeight(47); explorerCommands_->setIconSize(QSize(20,20));
    auto command = [this](QAction* action,const char* name,const char* glyph,const QString& label) {
        action->setIcon(icon(glyph)); action->setToolTip(label); explorerCommands_->addAction(action);
        auto* b = qobject_cast<QToolButton*>(explorerCommands_->widgetForAction(action)); b->setObjectName(QString::fromLatin1(name)); b->setAccessibleName(label); b->setFixedSize(40,32);
    };
    auto menuButton = [this](QMenu* menu,const char* name,const char* glyph,const QString& label) {
        auto* b = new QToolButton(explorerCommands_); b->setObjectName(QString::fromLatin1(name)); b->setIcon(icon(glyph)); b->setIconSize(QSize(20,20));
        b->setText(label); b->setAccessibleName(label); b->setToolTip(label); b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon); b->setPopupMode(QToolButton::InstantPopup);
        b->setMenu(menu); b->setFixedHeight(32); explorerCommands_->addWidget(b); return b;
    };
    menuButton(ui.menuCreateNew,"explorerNew","plus",tr("Novo")); explorerCommands_->addSeparator();
    command(ui.actionCut,"explorerCut","cut",tr("Recortar")); command(ui.actionCopy,"explorerCopy","copy",tr("Copiar")); command(ui.actionPaste,"explorerPaste","paste",tr("Colar"));
    command(ui.actionRename,"explorerRename","rename",tr("Renomear")); command(ui.actionDelete,"explorerDelete","delete",tr("Mover para a lixeira")); explorerCommands_->addSeparator();
    menuButton(ui.menuSorting,"explorerSort","sort",tr("Classificar")); ui.menu_View_2->addSeparator(); ui.menu_View_2->addAction(ui.actionShowHidden); ui.menu_View_2->addAction(ui.actionShowThumbnails);
    menuButton(ui.menu_View_2,"explorerView","view",tr("Visualizar"));
    auto* more = menuButton(ui.actionMenu->menu(),"explorerMore","more",tr("Mais")); more->setToolButtonStyle(Qt::ToolButtonIconOnly); more->setFixedWidth(40);
    auto* spacer = new QWidget(explorerCommands_); spacer->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Preferred); explorerCommands_->addWidget(spacer);
    command(ui.actionFolderProperties,"explorerProperties","view",tr("Propriedades da pasta"));
    addToolBarBreak(Qt::TopToolBarArea); addToolBar(Qt::TopToolBarArea,explorerCommands_);
    ui.statusbar->setSizeGripEnabled(false); ui.splitter->setHandleWidth(1); ui.splitter->setSizes({180,924}); ui.sidePane->setIconSize(QSize(16,16));
    if(auto* combo = ui.sidePane->findChild<QComboBox*>()) combo->hide();
    if(auto* layout = ui.sidePane->layout()) layout->setContentsMargins(4,8,4,0);
}
}
