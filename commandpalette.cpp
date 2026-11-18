/****************************************************************************
** Asmon — fork of SASM (GPL-3.0). Copyright © 2013 Dmitriy Manushin.
** Modifications © 2026 Asmon contributors.
****************************************************************************/
#include "commandpalette.h"

#include <QAction>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QVBoxLayout>

CommandPalette::CommandPalette(QWidget *parent)
    : QDialog(parent, Qt::Popup | Qt::FramelessWindowHint)
{
    setObjectName("commandPalette");
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(4);

    input = new QLineEdit(this);
    input->setPlaceholderText(tr("Type a command..."));
    input->installEventFilter(this);
    layout->addWidget(input);

    list = new QListWidget(this);
    list->setUniformItemSizes(true);
    list->setFocusPolicy(Qt::NoFocus);
    layout->addWidget(list);

    connect(input, &QLineEdit::textChanged, this, &CommandPalette::refilter);
    connect(input, &QLineEdit::returnPressed, this, &CommandPalette::runSelected);
    connect(list, &QListWidget::itemActivated, this, &CommandPalette::runSelected);
    connect(list, &QListWidget::itemClicked, this, &CommandPalette::runSelected);

    setStyleSheet("#commandPalette { border: 1px solid palette(mid); border-radius: 8px; }"
                  "QListWidget::item { padding: 6px 8px; border-radius: 4px; }");
}

void CommandPalette::collect(const QList<QAction *> &actions, const QString &prefix)
{
    for (QAction *a : actions) {
        if (a->isSeparator())
            continue;
        QString text = a->text();
        text.remove('&');
        if (a->menu()) {
            collect(a->menu()->actions(), prefix.isEmpty() ? text : prefix + " › " + text);
            continue;
        }
        if (text.isEmpty())
            continue;
        Entry e;
        e.action = a;
        e.label = prefix.isEmpty() ? text : prefix + ": " + text;
        e.searchText = e.label + " " + a->property("en").toString() + " " + a->objectName();
        entries.append(e);
    }
}

void CommandPalette::popupItems(const QList<Item> &items, const QString &placeholder)
{
    entries.clear();
    for (const Item &it : items) {
        Entry e;
        e.run = it.run;
        e.label = it.detail.isEmpty() ? it.label : it.label + "      " + it.detail;
        e.searchText = it.label;
        entries.append(e);
    }
    input->setPlaceholderText(placeholder);
    QWidget *host = parentWidget() ? parentWidget()->window() : nullptr;
    const int w = host ? qMin(640, host->width() - 80) : 600;
    resize(w, 380);
    if (host) {
        const QPoint topCenter = host->mapToGlobal(QPoint(host->width() / 2, 0));
        move(topCenter.x() - w / 2, topCenter.y() + 60);
    }
    input->clear();
    refilter(QString());
    show();
    input->setFocus();
}

void CommandPalette::popup(QMenuBar *menuBar, const QList<QAction *> &extra)
{
    entries.clear();
    input->setPlaceholderText(tr("Type a command..."));
    collect(menuBar->actions(), QString());
    collect(extra, QString());

    QWidget *host = parentWidget() ? parentWidget()->window() : nullptr;
    const int w = host ? qMin(640, host->width() - 80) : 600;
    resize(w, 380);
    if (host) {
        const QPoint topCenter = host->mapToGlobal(QPoint(host->width() / 2, 0));
        move(topCenter.x() - w / 2, topCenter.y() + 60);
    }
    input->clear();
    refilter(QString());
    show();
    input->setFocus();
}

void CommandPalette::refilter(const QString &text)
{
    list->clear();
    const QStringList words = text.split(' ', Qt::SkipEmptyParts);
    for (int idx = 0; idx < entries.size(); ++idx) {
        const Entry &e = entries.at(idx);
        if (e.run) {
            bool ok = true;
            for (const QString &w : words)
                if (!e.searchText.contains(w, Qt::CaseInsensitive)) { ok = false; break; }
            if (ok) {
                auto *item = new QListWidgetItem(e.label);
                item->setData(Qt::UserRole + 1, idx);
                list->addItem(item);
            }
            continue;
        }
        if (!e.action || !e.action->isVisible())
            continue;
        bool ok = true;
        for (const QString &w : words)
            if (!e.searchText.contains(w, Qt::CaseInsensitive)) { ok = false; break; }
        if (!ok)
            continue;
        QString label = e.label;
        const QString key = e.action->shortcut().toString(QKeySequence::NativeText);
        auto *item = new QListWidgetItem(e.action->icon(), key.isEmpty() ? label : label + "    " + key);
        item->setData(Qt::UserRole, QVariant::fromValue<void *>(e.action.data()));
        if (!e.action->isEnabled())
            item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
        list->addItem(item);
    }
    for (int i = 0; i < list->count(); ++i) {
        if (list->item(i)->flags() & Qt::ItemIsEnabled) {
            list->setCurrentRow(i);
            break;
        }
    }
}

void CommandPalette::runSelected()
{
    QListWidgetItem *item = list->currentItem();
    if (!item || !(item->flags() & Qt::ItemIsEnabled))
        return;
    if (item->data(Qt::UserRole + 1).isValid()) {
        const int idx = item->data(Qt::UserRole + 1).toInt();
        hide();
        if (idx >= 0 && idx < entries.size() && entries.at(idx).run)
            entries.at(idx).run();
        return;
    }
    QAction *a = static_cast<QAction *>(item->data(Qt::UserRole).value<void *>());
    hide();
    if (a)
        a->trigger();
}

bool CommandPalette::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == input && event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        int row = list->currentRow();
        if (ke->key() == Qt::Key_Down || ke->key() == Qt::Key_Up) {
            const int step = (ke->key() == Qt::Key_Down) ? 1 : -1;
            for (int r = row + step; r >= 0 && r < list->count(); r += step) {
                if (list->item(r)->flags() & Qt::ItemIsEnabled) {
                    list->setCurrentRow(r);
                    break;
                }
            }
            return true;
        }
        if (ke->key() == Qt::Key_Escape) {
            hide();
            return true;
        }
    }
    return QDialog::eventFilter(obj, event);
}
