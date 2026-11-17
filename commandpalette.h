/****************************************************************************
** Asmon — fork of SASM (GPL-3.0). Copyright © 2013 Dmitriy Manushin.
** Modifications © 2026 Asmon contributors.
**
** CommandPalette: Ctrl+Shift+P. Lists every menu action (with its
** shortcut), filters as you type, Enter runs the selected one.
****************************************************************************/
#ifndef ASMON_COMMANDPALETTE_H
#define ASMON_COMMANDPALETTE_H

#include <QDialog>
#include <QList>
#include <QPointer>

class QAction;
class QLineEdit;
class QListWidget;
class QMenuBar;

class CommandPalette : public QDialog
{
    Q_OBJECT
public:
    explicit CommandPalette(QWidget *parent = nullptr);
    //! Collect actions from the menu bar and show the palette.
    void popup(QMenuBar *menuBar, const QList<QAction *> &extra = {});

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private slots:
    void refilter(const QString &text);
    void runSelected();

private:
    void collect(const QList<QAction *> &actions, const QString &prefix);

    struct Entry {
        QPointer<QAction> action;
        QString label;
        QString searchText; //!< label + English names, for matching
    };
    QList<Entry> entries;
    QLineEdit *input;
    QListWidget *list;
};

#endif // ASMON_COMMANDPALETTE_H
