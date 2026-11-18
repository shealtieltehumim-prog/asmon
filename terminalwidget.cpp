/****************************************************************************
** Asmon — fork of SASM (GPL-3.0). Copyright © 2013 Dmitriy Manushin.
** Modifications © 2026 Asmon contributors.
****************************************************************************/
#include "terminalwidget.h"
#include "common.h"
#include "theme.h"

#include <QDir>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QScrollBar>
#include <QSettings>
#include <QVBoxLayout>

TerminalWidget::TerminalWidget(QWidget *parent) : QWidget(parent)
{
    setLayoutDirection(Qt::LeftToRight); // a terminal reads left to right
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    view = new QPlainTextEdit;
    view->setReadOnly(true);
    view->setMaximumBlockCount(5000);
    QFont f(QSettings().value("fontfamily", Theme::defaultCodeFont()).toString());
    f.setStyleHint(QFont::Monospace);
    f.setPointSize(QSettings().value("fontsize", 11).toInt());
    view->setFont(f);
    layout->addWidget(view, 1);

    auto *bar = new QHBoxLayout;
    bar->setContentsMargins(6, 4, 6, 4);
    cwdLabel = new QLabel;
    cwdLabel->setFont(f);
    cwdLabel->setForegroundRole(QPalette::PlaceholderText);
    input = new QLineEdit;
    input->setFont(f);
    input->setPlaceholderText(tr("Type a command, e.g. nasm -v"));
    input->installEventFilter(this);
    auto *restart = new QPushButton(tr("Restart"));
    restart->setToolTip(tr("Stops the running command and starts a new shell"));
    bar->addWidget(cwdLabel);
    bar->addWidget(input, 1);
    bar->addWidget(restart);
    layout->addLayout(bar);

    connect(input, &QLineEdit::returnPressed, this, &TerminalWidget::submit);
    connect(restart, &QPushButton::clicked, this, &TerminalWidget::restartShell);
    workingDir = QDir::homePath();
}

TerminalWidget::~TerminalWidget()
{
    if (shell) {
        shell->disconnect(this);
        shell->kill();
        shell->waitForFinished(1000);
    }
}

QStringList TerminalWidget::toolPaths()
{
    QStringList p;
#ifdef Q_OS_WIN32
    const QString base = Common::applicationDataPath();
    for (const QString &d : { QString("/NASM"), QString("/MinGW64/bin"), QString("/LLVM/bin") })
        if (QDir(base + d).exists())
            p << QDir::toNativeSeparators(base + d);
#endif
    return p;
}

void TerminalWidget::setWorkingDirectory(const QString &dir)
{
    if (dir.isEmpty() || !QDir(dir).exists())
        return;
    const bool changed = QDir(dir) != QDir(workingDir);
    workingDir = dir;
    cwdLabel->setText(QDir::toNativeSeparators(dir) + ">");
    if (changed && shell && shell->state() == QProcess::Running) {
#ifdef Q_OS_WIN32
        shell->write(QString("cd /d \"%1\"\r\n").arg(QDir::toNativeSeparators(dir)).toUtf8());
#else
        shell->write(QString("cd '%1'\n").arg(QString(dir).replace("'", "'\\''")).toUtf8());
#endif
    }
}

void TerminalWidget::startShell()
{
    shell = new QProcess(this);
    shell->setProcessChannelMode(QProcess::MergedChannels);
    shell->setWorkingDirectory(workingDir);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QStringList extra = toolPaths();
#ifdef Q_OS_WIN32
    if (!extra.isEmpty())
        env.insert("PATH", extra.join(';') + ";" + env.value("PATH"));
    shell->setProcessEnvironment(env);
    connect(shell, &QProcess::readyRead, this, &TerminalWidget::readOutput);
    //! /Q: no echo of commands; chcp 65001: UTF-8 output (Hebrew paths and text)
    shell->start("cmd.exe", QStringList() << "/Q" << "/K" << "chcp 65001 >nul");
#else
    if (!extra.isEmpty())
        env.insert("PATH", extra.join(':') + ":" + env.value("PATH"));
    env.insert("TERM", "dumb");
    shell->setProcessEnvironment(env);
    connect(shell, &QProcess::readyRead, this, &TerminalWidget::readOutput);
    shell->start("bash", QStringList() << "--noprofile" << "--norc");
#endif
    cwdLabel->setText(QDir::toNativeSeparators(workingDir) + ">");
    if (!shell->waitForStarted(5000))
        append(tr("Could not start the shell.") + "\n");
}

void TerminalWidget::restartShell()
{
    if (shell) {
        shell->disconnect(this);
        shell->kill();
        shell->waitForFinished(1000);
        shell->deleteLater();
        shell = nullptr;
    }
    append(tr("[new shell]") + "\n");
    startShell();
}

void TerminalWidget::clearView()
{
    view->clear();
}

void TerminalWidget::focusInput()
{
    if (!shell)
        startShell();
    input->setFocus();
}

void TerminalWidget::append(const QString &text, bool isCommand)
{
    QTextCursor c(view->document());
    c.movePosition(QTextCursor::End);
    QTextCharFormat fmt;
    fmt.setForeground(isCommand ? Theme::color("keywordscolor") : Theme::color("fontcolor"));
    c.insertText(text, fmt);
    view->verticalScrollBar()->setValue(view->verticalScrollBar()->maximum());
}

void TerminalWidget::readOutput()
{
    QString text = QString::fromUtf8(shell->readAll());
    text.remove('\r');
    append(text);
}

void TerminalWidget::runCommand(const QString &command)
{
    if (!shell || shell->state() != QProcess::Running)
        startShell();
    append(QDir::toNativeSeparators(workingDir) + "> " + command + "\n", true);
#ifdef Q_OS_WIN32
    shell->write((command + "\r\n").toUtf8());
#else
    shell->write((command + "\n").toUtf8());
#endif
}

void TerminalWidget::submit()
{
    const QString cmd = input->text();
    input->clear();
    if (cmd.trimmed() == "cls" || cmd.trimmed() == "clear") {
        view->clear();
        return;
    }
    if (!cmd.trimmed().isEmpty() && (history.isEmpty() || history.last() != cmd))
        history << cmd;
    historyPos = history.size();
    runCommand(cmd);
}

bool TerminalWidget::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == input && event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        if (ke->key() == Qt::Key_Up && historyPos > 0) {
            input->setText(history.value(--historyPos));
            return true;
        }
        if (ke->key() == Qt::Key_Down && historyPos < history.size()) {
            ++historyPos;
            input->setText(history.value(historyPos));
            return true;
        }
    }
    return QWidget::eventFilter(obj, event);
}
