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
#include <functional>
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
    //! Asmon: pick one of arbitrary items (e.g. go to symbol).
    struct Item {
        QString label;
        QString detail;   //!< shown after the label, dimmed (kind, line...)
        std::function<void()> run;
    };
    void popupItems(const QList<Item> &items, const QString &placeholder);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private slots:
    void refilter(const QString &text);
    void runSelected();

private:
    void collect(const QList<QAction *> &actions, const QString &prefix);

    struct Entry {
        QPointer<QAction> action;
        std::function<void()> run; //!< items mode
        QString label;
        QString searchText; //!< label + English names, for matching
    };
    QList<Entry> entries;
    QLineEdit *input;
    QListWidget *list;
};

#endif // ASMON_COMMANDPALETTE_H
