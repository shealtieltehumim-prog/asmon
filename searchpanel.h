/****************************************************************************
** Asmon — fork of SASM (GPL-3.0). Copyright © 2013 Dmitriy Manushin.
** Modifications © 2026 Asmon contributors.
**
** SearchPanel: "Search" tab of the bottom panel. Searches every source file
** under a folder (Ctrl+Shift+F) and shows "find references" results; both
** grouped by file. Double-click (or Enter) opens the place.
****************************************************************************/
#ifndef ASMON_SEARCHPANEL_H
#define ASMON_SEARCHPANEL_H

#include "symbols.h"
#include <QHash>
#include <QWidget>

class QCheckBox;
class QLabel;
class QLineEdit;
class QTreeWidget;
class QTreeWidgetItem;

class SearchPanel : public QWidget
{
    Q_OBJECT
public:
    explicit SearchPanel(QWidget *parent = nullptr);

    void setRoot(const QString &folder);   //!< folder searched (the file's folder)
    //! Text of open, unsaved editors, used instead of the file on disk.
    void setOpenBuffers(const QHash<QString, QString> &buffers) { openBuffers = buffers; }
    void focusSearch(const QString &text = QString());
    void showReferences(const QString &name, const QList<Symbols::Reference> &refs);

signals:
    void openLocation(const QString &file, int line, int column);

private slots:
    void runSearch();
    void activate(QTreeWidgetItem *item);

private:
    void addResult(const QString &file, int line, int column, const QString &text, int length);
    QTreeWidgetItem *fileItem(const QString &file);

    QLineEdit *query;
    QCheckBox *caseBox, *wordBox, *regexBox;
    QLabel *summary;
    QTreeWidget *tree;
    QString root;
    QHash<QString, QString> openBuffers;
    QHash<QString, QTreeWidgetItem *> fileItems;
};

#endif // ASMON_SEARCHPANEL_H
