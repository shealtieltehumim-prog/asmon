/****************************************************************************
** Asmon — fork of SASM (GPL-3.0). Copyright © 2013 Dmitriy Manushin.
** Modifications © 2026 Asmon contributors.
**
** MiDebugger: drives gdb through its machine interface (--interpreter=mi3).
** Replaces SASM's Debugger, which parsed gdb's human-readable CLI output.
**
** Source lines <-> addresses come from the assembler's listing (as in SASM),
** because NASM writes no DWARF for win32/win64; this works everywhere:
**   runtime address of a line = listing address + (runtime main - listing main)
**
** Stepping is by source line: nexti/stepi repeat until the program counter
** reaches the first instruction of a line of your code. A macro such as
** PRINT_STRING (dozens of instructions) is therefore one step.
****************************************************************************/
#ifndef ASMON_MIDEBUGGER_H
#define ASMON_MIDEBUGGER_H

#include "assembler.h"
#include "gdbmi.h"

#include <QHash>
#include <QMap>
#include <QObject>
#include <QProcess>
#include <QSet>
#include <functional>

class MiDebugger : public QObject
{
    Q_OBJECT
public:
    struct Config {
        QString gdb;          //!< gdb executable
        QString executable;   //!< program to debug
        QString workingDir;   //!< program's current directory
        QString inputFile;    //!< stdin for the program
        QString listing;      //!< assembler listing (.lst)
        bool windows = false; //!< target is a Windows program
        QStringList gdbPrefixArgs; //!< e.g. WSL: gdb = "wsl.exe", prefix = --cd dir --exec gdb
        Assembler *assembler = nullptr;
    };
    using Callback = std::function<void(const MiRecord &)>;

    explicit MiDebugger(const Config &config, QObject *parent = nullptr);
    ~MiDebugger() override;

    void start(const QList<int> &breakpointLines);
    bool isActive() const { return state != Off && state != Ended; }
    bool isStopped() const { return state == Stopped; }

    // execution control
    void continueRun();
    void pause();
    void stepOver();        //!< next source line, calls/macros run through
    void stepInto();        //!< next source line, entering calls into your code
    void stepOut();         //!< run until the current function returns
    void stepInstruction(); //!< exactly one machine instruction
    void stop();

    void setBreakpoint(int line, bool enabled);
    //! Asmon: "rcx == 5" (register names may be written without $). Empty: none.
    void setBreakpointCondition(int line, const QString &condition);
    static QString gdbCondition(const QString &condition);
    //! Stop when the program writes to size bytes at addressExpr (a gdb
    //! address expression, e.g. "&msg" or "$rbp-8"). cb(ok, message).
    void watchMemory(const QString &addressExpr, int size, const QString &shownName,
                     std::function<void(bool, const QString &)> cb);
    void clearMemoryWatches();
    //! Call stack: the current instruction, then each return address found on
    //! the stack (no frame info in assembly). line = the line of the call.
    struct Frame { quint64 address; int line; };
    void callStack(std::function<void(const QList<Frame> &)> cb);
    int lineContaining(quint64 address) const;

    // data access for the panels
    void command(const QString &cmd, Callback cb = nullptr);
    void consoleCommand(const QString &cliCommand);
    int lineForAddress(quint64 address) const;
    quint64 addressForLine(int line) const;
    quint64 currentPc() const { return pc; }
    int currentLine() const { return lineForAddress(pc); }
    bool is64Bit() const { return arch64; }

signals:
    void started();
    void stopped(quint64 pc, int line);   //!< paused (line 0: outside your code)
    void running();
    void finished(int exitCode);
    void crashed(const QString &signal, const QString &meaning, quint64 pc, int line);
    void programOutput(const QString &text);
    void consoleOutput(const QString &text);
    void log(const QString &text, bool error);
    void ended();                         //!< gdb has exited

private slots:
    void onReadyRead();
    void onProcessFinished();

private:
    enum State { Off, Starting, Running, Stopped, Ended };
    enum StepMode { NoStep, StepOver, StepInto, StepOut, StepInsn };
    enum StepPhase { PhaseNone, PhaseInsn, PhaseAfterCall, PhaseIntoCall, PhaseRunToLine, PhaseFinish, PhaseOut };

    void handleLine(const QString &line);
    void handleRecord(const MiRecord &r);
    void handleStopped(const MiValue &res);
    void setupAfterFirstStop(quint64 mainAddress);
    void beginStep(StepMode mode);
    void instructionAt(quint64 address, std::function<void(const QString &, quint64)> cb);
    void runToNextLine();
    void stepStopped(const QString &reason);
    void finishStepping(quint64 address);
    void applyPendingBreakpoints();
    bool inUserCode(quint64 address) const;
    void endSession();

    Config cfg;
    QProcess *gdb = nullptr;
    State state = Off;
    StepMode stepMode = NoStep;
    StepPhase stepPhase = PhaseNone;
    bool fixPushedTrapFlag = false;
    QString tempBreakpoint;
    QStringList lineBreakpoints; //!< gdb numbers, one per line start (disabled)
    quint64 pc = 0;
    quint64 stepStartPc = 0;
    bool arch64 = true;
    bool pauseRequested = false;
    bool firstStop = true;
    qint64 inferiorPid = 0;
    int nextToken = 1;
    int consoleCommandsPending = 0;
    QHash<int, Callback> callbacks;
    QByteArray buffer;

    QMap<quint64, int> lineOfAddress; //!< first instruction of a line -> line
    QMap<int, quint64> addressOfLine;
    quint64 userLow = 0, userHigh = 0;
    QByteArray userCode;          //!< bytes of [userLow, userHigh): finds return addresses
    bool isReturnAddress(quint64 v) const;
    void stepOutUsingStack();
    void readUserCode();

    QSet<int> wantedBreakpoints;      //!< lines
    QHash<int, QString> breakpointIds; //!< line -> gdb breakpoint number
    QHash<int, QString> breakpointConditions; //!< line -> gdb condition
    QStringList watchpointIds;
    QHash<QString, QString> watchpointNames; //!< gdb number -> name shown to the user
};

#endif // ASMON_MIDEBUGGER_H
