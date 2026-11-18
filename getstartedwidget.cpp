/****************************************************************************
** SASM - simple IDE for assembler development
** Copyright (C) 2013 Dmitriy Manushin
** Contact: site: http://dman95.github.io/SASM/
**          e-mail: Dman1095@gmail.com
**
** This file is part of SASM.
**
** SASM is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 3 of the License, or
** (at your option) any later version.
**
** SASM is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with SASM.  If not, see <http://www.gnu.org/licenses/>.
**
** Этот файл — часть SASM.
**
** SASM - свободная программа: вы можете перераспространять ее и/или
** изменять ее на условиях Стандартной общественной лицензии GNU в том виде,
** в каком она была опубликована Фондом свободного программного обеспечения;
** либо версии 3 лицензии, либо (по вашему выбору) любой более поздней
** версии.
**
** SASM распространяется в надежде, что она будет полезной,
** но БЕЗО ВСЯКИХ ГАРАНТИЙ; даже без неявной гарантии ТОВАРНОГО ВИДА
** или ПРИГОДНОСТИ ДЛЯ ОПРЕДЕЛЕННЫХ ЦЕЛЕЙ. Подробнее см. в Стандартной
** общественной лицензии GNU.
**
** Вы должны были получить копию Стандартной общественной лицензии GNU
** вместе с этой программой. Если это не так, см.
** <http://www.gnu.org/licenses/>.)
**
****************************************************************************/

#include "getstartedwidget.h"
#include "theme.h"
#include <QHBoxLayout>
#include <QIcon>
#include <QVBoxLayout>
#include <QDir>
#include <QFileInfo>
/**

 * @file getstartedwidget.cpp
 * Implements the opening window.
 */

GetStartedWidget::GetStartedWidget(QWidget *parent) :
    QWidget(parent)
{
    auto *outer = new QHBoxLayout(this);
    auto *column = new QVBoxLayout;
    column->setSpacing(6);
    outer->addStretch(1);
    outer->addLayout(column, 0);
    outer->addStretch(1);

    column->addStretch(2);

    auto *logo = new QLabel;
    logo->setPixmap(QIcon(":/icons/app.svg").pixmap(96, 96));
    logo->setAlignment(Qt::AlignCenter);
    column->addWidget(logo);
    column->addSpacing(8);

    auto *title = new QLabel(tr("Asmon"));
    QFont titleFont = title->font();
    titleFont.setPointSize(26);
    titleFont.setWeight(QFont::DemiBold);
    title->setFont(titleFont);
    title->setAlignment(Qt::AlignCenter);
    column->addWidget(title);

    auto *subtitle = new QLabel(tr("Assembly: write, run, debug and reverse engineer"));
    subtitle->setAlignment(Qt::AlignCenter);
    subtitle->setForegroundRole(QPalette::PlaceholderText);
    column->addWidget(subtitle);
    column->addSpacing(28);

    newButton = makeLink("new", tr("New file"), "Ctrl+N");
    openButton = makeLink("open", tr("Open file..."), "Ctrl+O");
    prevSessionButton = makeLink("explorer", tr("Restore last session"), QString());
    column->addWidget(newButton);
    column->addWidget(openButton);
    column->addWidget(prevSessionButton);
    column->addSpacing(16);

    recentTitle = new QLabel(tr("Recent"));
    recentTitle->setForegroundRole(QPalette::PlaceholderText);
    recentTitle->setContentsMargins(12, 0, 12, 2);
    column->addWidget(recentTitle);
    recentLayout = new QVBoxLayout;
    recentLayout->setSpacing(0);
    column->addLayout(recentLayout);
    recentTitle->hide();
    column->addSpacing(20);

    auto *hint = new QLabel(tr("All commands: Ctrl+Shift+P"));
    hint->setAlignment(Qt::AlignCenter);
    hint->setForegroundRole(QPalette::PlaceholderText);
    column->addWidget(hint);

    column->addStretch(3);
    setMinimumWidth(360);
}

QPushButton *GetStartedWidget::makeLink(const QString &iconName, const QString &text, const QString &hint)
{
    auto *b = new QPushButton(Theme::icon(iconName), hint.isEmpty() ? text : text + "      " + hint);
    b->setFlat(true);
    b->setCursor(Qt::PointingHandCursor);
    b->setMinimumWidth(320);
    b->setIconSize(QSize(18, 18));
    b->setStyleSheet("QPushButton { text-align: left; padding: 8px 12px; border: none; border-radius: 6px;"
                     " background: transparent; }" +
                     QString("QPushButton:hover { background: %1; }").arg(Theme::isDark() ? "#2a2d2e" : "#e8e8e8"));
    if (layoutDirection() == Qt::RightToLeft)
        b->setStyleSheet(b->styleSheet().replace("text-align: left", "text-align: right"));
    return b;
}

void GetStartedWidget::setRecentFiles(const QStringList &paths)
{
    qDeleteAll(recentButtons);
    recentButtons.clear();
    for (const QString &p : paths.mid(0, 6)) {
        const QFileInfo fi(p);
        QPushButton *b = makeLink("open", fi.fileName(), QDir::toNativeSeparators(fi.absolutePath()));
        b->setIcon(QIcon());
        b->setToolTip(QDir::toNativeSeparators(p));
        b->setStyleSheet(b->styleSheet() + "QPushButton { padding: 5px 12px; }");
        connect(b, &QPushButton::clicked, this, [this, p]() { emit openRecent(p); });
        recentLayout->addWidget(b);
        recentButtons << b;
    }
    recentTitle->setVisible(!recentButtons.isEmpty());
}
