/****************************************************************************
** Asmon — fork of SASM (GPL-3.0). Copyright © 2013 Dmitriy Manushin.
** Modifications © 2026 Asmon contributors.
****************************************************************************/
#include "modebar.h"

#include <QAction>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

#include "cardframe.h"

ModeBar::ModeBar(QWidget *parent) : QWidget(parent)
{
    setObjectName("modeBar");
    setAttribute(Qt::WA_StyledBackground, true);   // the style sheet draws its bottom separator
    QHBoxLayout *row = new QHBoxLayout(this);
    row->setContentsMargins(6, 0, 8, 0);
    row->setSpacing(2);

    const QString names[3] = { tr("Write"), tr("Debug"), tr("Reverse engineering") };
    for (int i = 0; i < 3; ++i) {
        QToolButton *tab = new QToolButton;
        tab->setObjectName("modeTab");
        tab->setText(names[i]);
        tab->setCheckable(true);
        tab->setAutoRaise(false);
        tab->setToolButtonStyle(Qt::ToolButtonTextOnly);
        tab->setFocusPolicy(Qt::NoFocus);
        const Mode mode = Mode(i);
        connect(tab, &QToolButton::clicked, this, [this, mode]() { setMode(mode); });
        row->addWidget(tab);
        m_tabs[i] = tab;
    }
    m_tabs[m_mode]->setChecked(true);

    row->addStretch(1);
    m_buttons = new QHBoxLayout;
    m_buttons->setContentsMargins(0, 0, 0, 0);
    m_buttons->setSpacing(4);
    row->addLayout(m_buttons);
    // the bar never forces the window wider than the screen: it switches to icons instead
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
}

void ModeBar::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateCompact();
}

bool ModeBar::event(QEvent *e)
{
    const bool r = QWidget::event(e);
    // a button changed its text or visibility, or the language changed
    if (e->type() == QEvent::LayoutRequest || e->type() == QEvent::LanguageChange)
        updateCompact();
    return r;
}

void ModeBar::updateCompact()
{
    if (m_updating || !layout())
        return;
    m_updating = true;
    QList<QToolButton *> buttons;
    for (const auto &item : m_items) {
        QToolButton *b = qobject_cast<QToolButton *>(item.second);
        if (b && item.first == m_mode && b->objectName() == "modeAction" && !b->icon().isNull())
            buttons << b;
    }
    // the width the bar needs with full labels
    auto apply = [&buttons](bool compact) {
        for (QToolButton *b : buttons) {
            b->setToolButtonStyle(compact ? Qt::ToolButtonIconOnly : Qt::ToolButtonTextBesideIcon);
            if (b->property("compact").toBool() != compact) {
                b->setProperty("compact", compact);   // smaller padding, see the style sheet
                b->style()->unpolish(b);
                b->style()->polish(b);
            }
        }
    };
    apply(false);
    // the width needed with full labels, summed by hand (nested layouts keep stale size caches until
    // the next activation, so the layout's own hint cannot be trusted right after a style change)
    const QMargins mg = layout()->contentsMargins();
    int need = mg.left() + mg.right();
    int shown = 0;
    for (QToolButton *t : m_tabs) {
        need += t->sizeHint().width();
        ++shown;
    }
    for (const auto &item : m_items)
        if (item.first == m_mode) {
            need += item.second->sizeHint().width();
            ++shown;
        }
    need += 4 * shown;
    const bool compact = need > width();
    if (compact)
        apply(true);
    m_compact = compact;
    layout()->invalidate();
    layout()->activate();
    m_updating = false;
}

void ModeBar::setMode(Mode mode)
{
    for (int i = 0; i < 3; ++i)
        m_tabs[i]->setChecked(i == mode);   // a click on the checked tab must not uncheck it
    if (mode == m_mode)
        return;
    m_mode = mode;
    showButtonsOf(mode);
    updateCompact();
    emit modeChanged(int(mode));
}

void ModeBar::addAction(Mode mode, QAction *action)
{
    QToolButton *button = new QToolButton;
    button->setObjectName("modeAction");
    button->setDefaultAction(action);
    button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    button->setIconSize(QSize(16, 16));
    button->setAutoRaise(false);
    button->setFocusPolicy(Qt::NoFocus);
    addWidget(mode, button);
}

void ModeBar::addWidget(Mode mode, QWidget *widget)
{
    m_buttons->addWidget(widget);
    m_items.append({ mode, widget });
    widget->setVisible(mode == m_mode);
}

void ModeBar::showButtonsOf(Mode mode)
{
    for (const auto &item : m_items)
        item.second->setVisible(item.first == mode);
}
