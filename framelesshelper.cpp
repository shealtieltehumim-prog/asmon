#include "framelesshelper.h"
#include <QBoxLayout>
#include <QCursor>
#include <QApplication>
#include <QElapsedTimer>
#include <QEvent>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPainter>
#include <QToolBar>
#include <QWindow>

WindowButton::WindowButton(Kind k, QWidget *parent)
    : QAbstractButton(parent), kind(k)
{
    setFocusPolicy(Qt::NoFocus);
    setCursor(Qt::ArrowCursor);
    setAttribute(Qt::WA_Hover);
    setToolTip(k == Minimize ? tr("Minimize") : k == Maximize ? tr("Maximize") : tr("Close"));
}

void WindowButton::setRestoreIcon(bool r)
{
    restore = r;
    setToolTip(r ? tr("Restore") : tr("Maximize"));
    update();
}

void WindowButton::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);
    const bool hot = underMouse();
    QColor fg = palette().color(QPalette::ButtonText);
    if (hot) {
        if (kind == Close) {
            p.fillRect(rect(), QColor("#e81123"));
            fg = Qt::white;
        } else {
            QColor h = palette().color(QPalette::ButtonText);
            h.setAlpha(isDown() ? 45 : 28);
            p.fillRect(rect(), h);
        }
    }
    QPen pen(fg, 1);
    p.setPen(pen);
    const QPoint c = rect().center();
    const int s = 5; // half size of the glyph
    if (kind == Minimize) {
        p.drawLine(c.x() - s, c.y() + 1, c.x() + s, c.y() + 1);
    } else if (kind == Maximize) {
        if (!restore) {
            p.drawRect(QRect(c.x() - s, c.y() - s, 2 * s, 2 * s));
        } else {
            p.drawRect(QRect(c.x() - s, c.y() - s + 2, 2 * s - 2, 2 * s - 2));
            p.drawLine(c.x() - s + 2, c.y() - s, c.x() + s, c.y() - s);
            p.drawLine(c.x() + s, c.y() - s, c.x() + s, c.y() + s - 2);
        }
    } else {
        p.setRenderHint(QPainter::Antialiasing, true);
        p.drawLine(QPointF(c.x() - s + 0.5, c.y() - s + 0.5), QPointF(c.x() + s + 0.5, c.y() + s + 0.5));
        p.drawLine(QPointF(c.x() - s + 0.5, c.y() + s + 0.5), QPointF(c.x() + s + 0.5, c.y() - s + 0.5));
    }
}

FramelessHelper::FramelessHelper(QMainWindow *window, QWidget *)
    : QObject(window), win(window)
{
    win->setWindowFlag(Qt::FramelessWindowHint, true);
    win->setMouseTracking(true);
    win->setContentsMargins(kBorder, kBorder, kBorder, kBorder);
    win->installEventFilter(this);

    minBtn = new WindowButton(WindowButton::Minimize);
    maxBtn = new WindowButton(WindowButton::Maximize);
    closeBtn = new WindowButton(WindowButton::Close);
    connect(minBtn, &QAbstractButton::clicked, win, &QWidget::showMinimized);
    connect(maxBtn, &QAbstractButton::clicked, this, [this] {
        win->isMaximized() ? win->showNormal() : win->showMaximized();
    });
    connect(closeBtn, &QAbstractButton::clicked, win, &QWidget::close);
}

void FramelessHelper::addButtons(QLayout *layout)
{
    //! The corner widget is mirrored in a right-to-left interface, so the widget added last
    //! is the leftmost one: minimize, maximize, then close at the far left.
    QBoxLayout *box = static_cast<QBoxLayout *>(layout);
    box->addWidget(minBtn);
    box->addWidget(maxBtn);
    box->addWidget(closeBtn);
}

void FramelessHelper::addDragArea(QWidget *w)
{
    dragAreas << w;
    w->installEventFilter(this);
}

Qt::Edges FramelessHelper::edgesAt(const QPoint &p) const
{
    if (win->isMaximized() || win->isFullScreen())
        return {};
    Qt::Edges e;
    if (p.x() < kBorder) e |= Qt::LeftEdge;
    if (p.x() >= win->width() - kBorder) e |= Qt::RightEdge;
    if (p.y() < kBorder) e |= Qt::TopEdge;
    if (p.y() >= win->height() - kBorder) e |= Qt::BottomEdge;
    return e;
}

void FramelessHelper::updateState()
{
    const bool max = win->isMaximized() || win->isFullScreen();
    maxBtn->setRestoreIcon(max);
    const int m = max ? 0 : kBorder;
    win->setContentsMargins(m, m, m, m);
}

bool FramelessHelper::eventFilter(QObject *obj, QEvent *e)
{
    if (obj == win) {
        switch (e->type()) {
        case QEvent::WindowStateChange:
            updateState();
            break;
        case QEvent::MouseMove: {
            const Qt::Edges ed = edgesAt(static_cast<QMouseEvent *>(e)->position().toPoint());
            if (ed == (Qt::LeftEdge | Qt::TopEdge) || ed == (Qt::RightEdge | Qt::BottomEdge))
                win->setCursor(Qt::SizeFDiagCursor);
            else if (ed == (Qt::RightEdge | Qt::TopEdge) || ed == (Qt::LeftEdge | Qt::BottomEdge))
                win->setCursor(Qt::SizeBDiagCursor);
            else if (ed & (Qt::LeftEdge | Qt::RightEdge))
                win->setCursor(Qt::SizeHorCursor);
            else if (ed & (Qt::TopEdge | Qt::BottomEdge))
                win->setCursor(Qt::SizeVerCursor);
            else
                win->unsetCursor();
            break;
        }
        case QEvent::MouseButtonPress: {
            auto *me = static_cast<QMouseEvent *>(e);
            const Qt::Edges ed = edgesAt(me->position().toPoint());
            if (me->button() == Qt::LeftButton && ed && win->windowHandle()) {
                win->windowHandle()->startSystemResize(ed);
                return true;
            }
            break;
        }
        default:
            break;
        }
        return false;
    }

    //! Title bar behaviour on the empty part of the menu bar / corner widget: press + drag moves
    //! the window, double click maximizes / restores.
    QWidget *w = qobject_cast<QWidget *>(obj);
    if (!w)
        return false;
    const QEvent::Type t = e->type();
    if (t == QEvent::MouseButtonPress || t == QEvent::MouseButtonDblClick) {
        auto *me = static_cast<QMouseEvent *>(e);
        if (me->button() != Qt::LeftButton)
            return false;
        if (auto *mb = qobject_cast<QMenuBar *>(w)) {
            if (mb->actionAt(me->position().toPoint()))
                return false; // a real menu title
        }
        if (auto *tb = qobject_cast<QToolBar *>(w)) {
            if (tb->actionAt(me->position().toPoint()))
                return false;
        }
        if (t == QEvent::MouseButtonDblClick) {
            dragPending = false;
            win->isMaximized() ? win->showNormal() : win->showMaximized();
            return true;
        }
        dragPending = true;
        dragStart = me->globalPosition().toPoint();
        return true;
    }
    if (t == QEvent::MouseMove && dragPending) {
        auto *me = static_cast<QMouseEvent *>(e);
        if ((me->globalPosition().toPoint() - dragStart).manhattanLength() >= QApplication::startDragDistance()) {
            dragPending = false;
            if (win->windowHandle())
                win->windowHandle()->startSystemMove();
            return true;
        }
    }
    if (t == QEvent::MouseButtonRelease && dragPending) {
        dragPending = false;
        return true;
    }
    return false;
}
