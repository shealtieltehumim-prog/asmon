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

#include "debugbackend.h"
#include "gdbmi.h"

#include <QHash>
#include <QMap>
#include <QObject>
#include <QProcess>
#include <QSet>
#include <functional>

class MiDebugger : public DebugBackend
{
    Q_OBJECT
public:
    using Callback = std::function<void(const MiRecord &)>;

    explicit MiDebugger(const Config &config, QObject *parent = nullptr);
    ~MiDebugger() override;

    void start(const QList<int> &breakpointLines) override;
    bool isActive() const override { return state != Off && state != Ended; }
    bool isStopped() const override { return state == Stopped; }
    bool isAttached() const override { return cfg.attachPid > 0; }

    // execution control
    void continueRun() override;
    void pause() override;
    void stepOver() override;
    void stepInto() override;
    void stepOut() override;
    void stepInstruction() override;
    void stop() override;

    void setBreakpoint(int line, bool enabled) override;
    void setBreakpointCondition(int line, const QString &condition) override;
    static QString gdbCondition(const QString &condition);
    //! "msg" -> "&msg", "rsp" -> "$rsp", "[rbp-8]" -> "($rbp-8)", "0x40" stays (gdb syntax).
    static QString gdbAddressExpression(const QString &text);
    void watchMemory(const QString &addressText, int size, const QString &shownName,
                     std::function<void(bool, const QString &)> cb) override;
    void clearMemoryWatches() override;
    void callStack(std::function<void(const QList<Frame> &)> cb) override;
    int lineContaining(quint64 address) const override;

    // data access for the panels
    void readRegisters(const QStringList &names, std::function<void(const QList<Register> &)> cb) override;
    void writeRegister(const QString &name, const QString &valueText,
                       std::function<void(bool, const QString &)> cb) override;
    void readMemory(quint64 address, int length, std::function<void(bool, const QByteArray &)> cb) override;
    void writeMemory(quint64 address, const QByteArray &bytes, std::function<void(bool)> cb) override;
    void evaluateAddress(const QString &text, std::function<void(bool, quint64)> cb) override;
    void disassemble(quint64 start, int byteCount, std::function<void(const QList<Instruction> &)> cb) override;
    bool hasConsole() const override { return true; }
    void consoleCommand(const QString &cliCommand) override;
    QString engineName() const override { return QStringLiteral("gdb"); }
    void command(const QString &cmd, Callback cb = nullptr);
    int lineForAddress(quint64 address) const override;
    quint64 addressForLine(int line) const override;
    quint64 currentPc() const override { return pc; }
    int currentLine() const override { return lineForAddress(pc); }
    bool is64Bit() const override { return arch64; }

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
    void mapModule(int index, quint64 mainAddress);
    void tryModuleLabel(int index, QVector<QPair<QString, quint64>> candidates, int at,
                        QVector<Assembler::LineNum> lines, quint64 mainAddress);
    void finishSetup(quint64 mainAddress);
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

    QStringList gdbRegisterNames;     //!< gdb's register numbering, read once
    QSet<int> wantedBreakpoints;      //!< lines
    QHash<int, QString> breakpointIds; //!< line -> gdb breakpoint number
    QHash<int, QString> breakpointConditions; //!< line -> gdb condition
    QStringList watchpointIds;
    QHash<QString, QString> watchpointNames; //!< gdb number -> name shown to the user
};

#endif // ASMON_MIDEBUGGER_H
