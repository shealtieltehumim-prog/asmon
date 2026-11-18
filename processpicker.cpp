/****************************************************************************
** Asmon — fork of SASM (GPL-3.0). Copyright © 2013 Dmitriy Manushin.
** Modifications © 2026 Asmon contributors.
****************************************************************************/
#include "processpicker.h"

#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#ifdef Q_OS_WIN32
#include <windows.h>
#include <tlhelp32.h>
#endif

QList<ProcessPicker::Process> ProcessPicker::processes()
{
    QList<Process> out;
    const qint64 self = QCoreApplication::applicationPid();
#ifdef Q_OS_WIN32
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return out;
    PROCESSENTRY32W e;
    e.dwSize = sizeof(e);
    for (BOOL ok = Process32FirstW(snap, &e); ok; ok = Process32NextW(snap, &e)) {
        if (e.th32ProcessID == 0 || qint64(e.th32ProcessID) == self)
            continue;
        Process p{ qint64(e.th32ProcessID), QString::fromWCharArray(e.szExeFile), QString() };
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, e.th32ProcessID);
        if (h) {
            wchar_t buf[MAX_PATH * 2];
            DWORD size = DWORD(sizeof(buf) / sizeof(buf[0]));
            if (QueryFullProcessImageNameW(h, 0, buf, &size))
                p.path = QDir::fromNativeSeparators(QString::fromWCharArray(buf, int(size)));
            CloseHandle(h);
        }
        out << p;
    }
    CloseHandle(snap);
#else
    const QStringList pids = QDir("/proc").entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &d : pids) {
        bool ok = false;
        const qint64 pid = d.toLongLong(&ok);
        if (!ok || pid == self)
            continue;
        QFile comm("/proc/" + d + "/comm");
        if (!comm.open(QIODevice::ReadOnly))
            continue;
        const QString name = QString::fromUtf8(comm.readAll()).trimmed();
        const QString exe = QFileInfo("/proc/" + d + "/exe").symLinkTarget();
        out << Process{ pid, name, exe };
    }
#endif
    return out;
}

ProcessPicker::ProcessPicker(const QString &highlightPath, QWidget *parent)
    : QDialog(parent), highlight(QDir::fromNativeSeparators(highlightPath))
{
    setWindowTitle(tr("Attach to a running process"));
    resize(720, 480);
    auto *layout = new QVBoxLayout(this);
    auto *hint = new QLabel(tr("The debugger stops the process where it is. Programs you built in Asmon "
                               "are shown first: with them you also get lines, breakpoints and steps. "
                               "When you stop debugging, the process keeps running."));
    hint->setWordWrap(true);
    layout->addWidget(hint);
    filter = new QLineEdit;
    filter->setPlaceholderText(tr("Filter by name, path or PID"));
    filter->setClearButtonEnabled(true);
    layout->addWidget(filter);
    table = new QTableWidget(0, 3);
    table->setHorizontalHeaderLabels(QStringList() << "PID" << tr("Name") << tr("Path"));
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    table->verticalHeader()->hide();
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setLayoutDirection(Qt::LeftToRight);
    layout->addWidget(table, 1);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Attach"));
    QPushButton *refresh = buttons->addButton(tr("Refresh"), QDialogButtonBox::ActionRole);
    layout->addWidget(buttons);

    connect(filter, &QLineEdit::textChanged, this, &ProcessPicker::refilter);
    connect(refresh, &QPushButton::clicked, this, &ProcessPicker::reload);
    connect(buttons, &QDialogButtonBox::accepted, this, &ProcessPicker::choose);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(table, &QTableWidget::cellDoubleClicked, this, &ProcessPicker::choose);
    reload();
    filter->setFocus();
}

void ProcessPicker::reload()
{
    QList<Process> list = processes();
    //! Asmon's programs first, then by name
    std::stable_sort(list.begin(), list.end(), [this](const Process &a, const Process &b) {
        const bool am = !highlight.isEmpty() && a.path == highlight;
        const bool bm = !highlight.isEmpty() && b.path == highlight;
        if (am != bm)
            return am;
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });
    table->setRowCount(list.size());
    for (int i = 0; i < list.size(); ++i) {
        const Process &p = list.at(i);
        auto *pidItem = new QTableWidgetItem(QString::number(p.pid));
        pidItem->setData(Qt::UserRole, p.pid);
        pidItem->setData(Qt::UserRole + 1, p.path);
        table->setItem(i, 0, pidItem);
        table->setItem(i, 1, new QTableWidgetItem(p.name));
        table->setItem(i, 2, new QTableWidgetItem(QDir::toNativeSeparators(p.path)));
        if (!highlight.isEmpty() && p.path == highlight) {
            for (int c = 0; c < 3; ++c) {
                QFont f = table->item(i, c)->font();
                f.setBold(true);
                table->item(i, c)->setFont(f);
            }
        }
    }
    table->resizeColumnToContents(0);
    table->resizeColumnToContents(1);
    refilter(filter->text());
    for (int i = 0; i < table->rowCount(); ++i)
        if (!table->isRowHidden(i)) { table->selectRow(i); break; }
}

void ProcessPicker::refilter(const QString &text)
{
    for (int i = 0; i < table->rowCount(); ++i) {
        bool show = text.isEmpty();
        for (int c = 0; c < 3 && !show; ++c)
            show = table->item(i, c)->text().contains(text, Qt::CaseInsensitive);
        table->setRowHidden(i, !show);
    }
}

void ProcessPicker::choose()
{
    const int row = table->currentRow();
    if (row < 0 || table->isRowHidden(row))
        return;
    pid = table->item(row, 0)->data(Qt::UserRole).toLongLong();
    path = table->item(row, 0)->data(Qt::UserRole + 1).toString();
    accept();
}
