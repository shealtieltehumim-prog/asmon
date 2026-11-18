/****************************************************************************
** Asmon — fork of SASM (GPL-3.0). Copyright © 2013 Dmitriy Manushin.
** Modifications © 2026 Asmon contributors.
**
** TerminalWidget: a simple integrated terminal (bottom panel).
** Runs one persistent shell — cmd.exe (UTF-8 code page) on Windows, bash on
** Linux — with Asmon's NASM and MinGW tools first in PATH. Commands are typed
** in a line edit (history with Up/Down); "Restart" kills a stuck command.
** It is not a full terminal emulator: interactive full-screen programs
** (vim, less) do not work here.
****************************************************************************/
#ifndef ASMON_TERMINALWIDGET_H
#define ASMON_TERMINALWIDGET_H

#include <QProcess>
#include <QStringList>
#include <QWidget>

class QLabel;
class QLineEdit;
class QPlainTextEdit;

class TerminalWidget : public QWidget
{
    Q_OBJECT
public:
    explicit TerminalWidget(QWidget *parent = nullptr);
    ~TerminalWidget() override;

    void setWorkingDirectory(const QString &dir); //!< used when the shell (re)starts
    void focusInput();
    void clearView();
    //! PATH entries put in front of the system PATH (bundled tools).
    static QStringList toolPaths();

public slots:
    void restartShell();
    void runCommand(const QString &command);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private slots:
    void readOutput();
    void submit();

private:
    void startShell();
    void append(const QString &text, bool isCommand = false);

    QProcess *shell = nullptr;
    QPlainTextEdit *view;
    QLineEdit *input;
    QLabel *cwdLabel;
    QString workingDir;
    QStringList history;
    int historyPos = 0;
};

#endif // ASMON_TERMINALWIDGET_H
