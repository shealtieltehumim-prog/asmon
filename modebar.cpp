/****************************************************************************
** Asmon — fork of SASM (GPL-3.0). Copyright © 2013 Dmitriy Manushin.
** Modifications © 2026 Asmon contributors.
****************************************************************************/
#include "modebar.h"

#include <QAction>
#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>
#include <QVBoxLayout>

#include "cardframe.h"

ModeBar::ModeBar(QWidget *parent) : QWidget(parent)
{
    setObjectName("modeBar");
    QHBoxLayout *row = new QHBoxLayout(this);
    row->setContentsMargins(2, 0, 2, 4);
    row->setSpacing(4);

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
}

void ModeBar::setMode(Mode mode)
{
    for (int i = 0; i < 3; ++i)
        m_tabs[i]->setChecked(i == mode);   // a click on the checked tab must not uncheck it
    if (mode == m_mode)
        return;
    m_mode = mode;
    showButtonsOf(mode);
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
