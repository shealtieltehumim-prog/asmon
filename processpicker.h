/****************************************************************************
** Asmon — fork of SASM (GPL-3.0). Copyright © 2013 Dmitriy Manushin.
** Modifications © 2026 Asmon contributors.
**
** ProcessPicker: choose a running process to attach the debugger to
** (Debug > Attach to process). Programs built by Asmon are listed first.
****************************************************************************/
#ifndef ASMON_PROCESSPICKER_H
#define ASMON_PROCESSPICKER_H

#include <QDialog>

class QLineEdit;
class QTableWidget;

class ProcessPicker : public QDialog
{
    Q_OBJECT
public:
    struct Process {
        qint64 pid;
        QString name;
        QString path;   //!< full path of the executable (may be empty)
    };
    explicit ProcessPicker(const QString &highlightPath, QWidget *parent = nullptr);
    static QList<Process> processes();

    qint64 selectedPid() const { return pid; }
    QString selectedPath() const { return path; }

private slots:
    void reload();
    void refilter(const QString &text);
    void choose();

private:
    QLineEdit *filter;
    QTableWidget *table;
    QString highlight;
    qint64 pid = 0;
    QString path;
};

#endif // ASMON_PROCESSPICKER_H
