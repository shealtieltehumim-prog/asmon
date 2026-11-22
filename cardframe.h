#ifndef CARDFRAME_H
#define CARDFRAME_H

#include <QApplication>
#include <QFrame>
#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>
#include <QTabWidget>
#include <QTabBar>
#include "theme.h"

/**
 * @file cardframe.h
 * A rounded "card" that holds one widget (the code editor, the bottom panel): the card is
 * painted slightly lighter than the window with a thin border, so the panels look separate
 * from each other and from the window, like the floating panels of modern editors.
 */
class CardFrame : public QFrame
{
public:
    //! flat (VS Code): no rounded outline, the panels meet edge to edge with 1 px separators
    static constexpr int kRadius = 0;
    static constexpr int kInset = 0;

    explicit CardFrame(QWidget *content, QWidget *parent = nullptr) : QFrame(parent), m_content(content)
    {
        setAttribute(Qt::WA_StyledBackground, false);
        QVBoxLayout *l = new QVBoxLayout(this);
        l->setContentsMargins(kInset, kInset, kInset, kInset);
        l->setSpacing(0);
        l->addWidget(content);
        //! the strip behind the tab bar follows the tab bar (tabs opened, closed, the mode changed)
        if (auto *tw = qobject_cast<QTabWidget *>(content)) {
            tw->tabBar()->installEventFilter(this);
            connect(tw, &QTabWidget::currentChanged, this, [this](int) { update(); });
        }
    }

    bool eventFilter(QObject *o, QEvent *e) override
    {
        if (e->type() == QEvent::Resize || e->type() == QEvent::Move || e->type() == QEvent::Show
                || e->type() == QEvent::Hide)
            update();
        return QFrame::eventFilter(o, e);
    }

private:
    QWidget *m_content = nullptr;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), Theme::color("backgroundcolor")); // follows the selected colour theme
        //! the strip behind a tab bar is the window colour across the whole width (VS Code), not
        //! the editor background beside the last tab
        if (auto *tw = qobject_cast<QTabWidget *>(m_content)) {
            const QRect bar = tw->tabBar()->geometry();
            if (tw->count() > 0 && bar.height() > 0) {
                const int bottom = tw->mapTo(this, QPoint(0, bar.bottom())).y();
                p.fillRect(QRect(0, 0, width(), bottom + 1), QApplication::palette().color(QPalette::Window));
            }
        }
    }
};

#endif
