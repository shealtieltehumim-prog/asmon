#ifndef FRAMELESSHELPER_H
#define FRAMELESSHELPER_H

#include <QAbstractButton>
#include <QMainWindow>
#include <QObject>

/**
 * @file framelesshelper.h
 * Asmon: window with the minimize / maximize / close buttons on the LEFT, the way Hebrew
 * software is usually laid out. The native title bar is replaced by the menu bar row:
 * the empty part of the menu bar drags the window, double click maximizes, and a thin
 * border around the window resizes it.
 */

//! One flat, painted title-bar button (no stylesheet needed, follows the palette).
class WindowButton : public QAbstractButton
{
    Q_OBJECT
public:
    enum Kind { Minimize, Maximize, Close };
    WindowButton(Kind kind, QWidget *parent = nullptr);
    void setRestoreIcon(bool restore);
    QSize sizeHint() const override { return QSize(40, 28); }

protected:
    void paintEvent(QPaintEvent *) override;
    void enterEvent(QEnterEvent *) override { update(); }
    void leaveEvent(QEvent *) override { update(); }

private:
    Kind kind;
    bool restore = false;
};

class FramelessHelper : public QObject
{
    Q_OBJECT
public:
    //! Makes `window` frameless; menu bar row becomes the title bar. Call before show().
    FramelessHelper(QMainWindow *window, QWidget *cornerContainerLayoutOwner);

    //! Adds the three buttons to `layout` so that Close ends up at the physical left.
    void addButtons(QLayout *layout);
    //! Widgets that should drag the window when pressed on an empty spot.
    void addDragArea(QWidget *w);

    static constexpr int kBorder = 4;

protected:
    bool eventFilter(QObject *obj, QEvent *e) override;

private:
    Qt::Edges edgesAt(const QPoint &p) const;
    void updateState();

    QMainWindow *win;
    WindowButton *minBtn, *maxBtn, *closeBtn;
    QList<QWidget *> dragAreas;
    bool dragPending = false;
    QPoint dragStart;
};

#endif
