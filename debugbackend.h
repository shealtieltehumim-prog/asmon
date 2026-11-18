/****************************************************************************
** Asmon — fork of SASM (GPL-3.0). Copyright © 2013 Dmitriy Manushin.
** Modifications © 2026 Asmon contributors.
**
** DebugBackend: what the window and the debug panels need from a debugger,
** independent of how it works. Two implementations:
**   MiDebugger       — drives gdb through its machine interface (every target)
**   NativeDebugger   — Windows Debug API (Windows x64 programs)
** Everything is asynchronous: results arrive through callbacks, in the thread
** of the backend object.
****************************************************************************/
#ifndef ASMON_DEBUGBACKEND_H
#define ASMON_DEBUGBACKEND_H

#include "assembler.h"

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>
#include <functional>

class DebugBackend : public QObject
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
        qint64 attachPid = 0;      //!< attach to this running process instead of starting one
        Assembler *assembler = nullptr;
        //! the project's other files: module N (from 1) has line ids N * kModuleLineBase + line
        struct Module { QString source; QString listing; };
        QList<Module> modules;
    };
    //! Line ids: the main file's lines are 1.., module N's are N * kModuleLineBase + line.
    static constexpr int kModuleLineBase = 10000000;

    //! Call stack: the current instruction, then each return address found on
    //! the stack (no frame info in assembly). line = the line of the call.
    struct Frame { quint64 address; int line; };
    struct Register { QString name; quint64 value; };
    struct Instruction {
        quint64 address = 0;
        QByteArray bytes;   //!< machine code (may be empty)
        QString text;       //!< Intel syntax
        QString symbol;     //!< "<main+4>" or empty
    };

    explicit DebugBackend(QObject *parent = nullptr) : QObject(parent) {}

    virtual void start(const QList<int> &breakpointLines) = 0;
    virtual bool isActive() const = 0;
    virtual bool isStopped() const = 0;
    virtual bool isAttached() const = 0;

    // execution control
    virtual void continueRun() = 0;
    virtual void pause() = 0;
    virtual void stepOver() = 0;        //!< next source line, calls/macros run through
    virtual void stepInto() = 0;        //!< next source line, entering calls into your code
    virtual void stepOut() = 0;         //!< run until the current function returns
    virtual void stepInstruction() = 0; //!< exactly one machine instruction
    virtual void stop() = 0;

    virtual void setBreakpoint(int line, bool enabled) = 0;
    //! "rcx == 5" (register names may be written without $). Empty: none.
    virtual void setBreakpointCondition(int line, const QString &condition) = 0;
    //! Stop when the program writes to size bytes at the address of addressText
    //! (a label, register, [rbp-8] or number). cb(ok, message).
    virtual void watchMemory(const QString &addressText, int size, const QString &shownName,
                             std::function<void(bool, const QString &)> cb) = 0;
    virtual void clearMemoryWatches() = 0;
    virtual void callStack(std::function<void(const QList<Frame> &)> cb) = 0;
    virtual int lineContaining(quint64 address) const = 0;

    // data access for the panels (the program must be stopped)
    //! values of the named registers (rax…, rip, eflags; lower case); unknown names are left out
    virtual void readRegisters(const QStringList &names, std::function<void(const QList<Register> &)> cb) = 0;
    virtual void writeRegister(const QString &name, const QString &valueText,
                               std::function<void(bool, const QString &)> cb) = 0;
    virtual void readMemory(quint64 address, int length, std::function<void(bool, const QByteArray &)> cb) = 0;
    virtual void writeMemory(quint64 address, const QByteArray &bytes, std::function<void(bool)> cb) = 0;
    //! the address a label / register / "[rbp-8]" / number / expression stands for
    virtual void evaluateAddress(const QString &text, std::function<void(bool, quint64)> cb) = 0;
    virtual void disassemble(quint64 start, int byteCount, std::function<void(const QList<Instruction> &)> cb) = 0;
    //! the debugger's own command line (gdb: any gdb command). Without one: false.
    virtual bool hasConsole() const { return false; }
    virtual void consoleCommand(const QString &) {}
    virtual QString engineName() const = 0;

    virtual int lineForAddress(quint64 address) const = 0;
    virtual quint64 addressForLine(int line) const = 0;
    virtual quint64 currentPc() const = 0;
    virtual int currentLine() const { return lineForAddress(currentPc()); }
    virtual bool is64Bit() const = 0;

signals:
    void started();
    void stopped(quint64 pc, int line);   //!< paused (line 0: outside your code)
    void running();
    void finished(int exitCode);
    void crashed(const QString &signal, const QString &meaning, quint64 pc, int line);
    void programOutput(const QString &text);
    void consoleOutput(const QString &text);
    void log(const QString &text, bool error);
    void ended();                         //!< the debugger has exited
};

#endif // ASMON_DEBUGBACKEND_H
