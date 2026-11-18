/****************************************************************************
** Asmon — GPL-3.0.
**
** NativeDebugger: the debug backend that talks to Windows directly (Debug API)
** instead of gdb. 64-bit programs started by Asmon on native Windows.
** The program is debugged on a worker thread (the Debug API ties a program
** to the thread that started it); results come back to the window's thread.
****************************************************************************/
#ifndef ASMON_NATIVEDEBUGGER_H
#define ASMON_NATIVEDEBUGGER_H

#include <QtGlobal>

#if defined(Q_OS_WIN) || defined(ASMON_NATIVE_CHECK)

#include "debugbackend.h"
#include "native/debuggee.h"

#include <QMap>
#include <QSet>
#include <QStringDecoder>
#include <QThread>
#include <atomic>
#include <functional>

class NativeDebugger : public DebugBackend
{
    Q_OBJECT
public:
    explicit NativeDebugger(const Config &config, QObject *parent = nullptr);
    ~NativeDebugger() override;

    //! Can this engine debug the program? Not for attach, WSL, 32-bit programs.
    static bool canDebug(const Config &config, QString *reason = nullptr);

    void start(const QList<int> &breakpointLines) override;
    bool isActive() const override { return state_ != Off && state_ != Ended; }
    bool isStopped() const override { return state_ == Stopped; }
    bool isAttached() const override { return false; }

    void continueRun() override;
    void pause() override;
    void stepOver() override;
    void stepInto() override;
    void stepOut() override;
    void stepInstruction() override;
    void stop() override;

    void setBreakpoint(int line, bool enabled) override;
    void setBreakpointCondition(int line, const QString &condition) override;
    void watchMemory(const QString &addressText, int size, const QString &shownName,
                     std::function<void(bool, const QString &)> cb) override;
    void clearMemoryWatches() override;
    void callStack(std::function<void(const QList<Frame> &)> cb) override;
    int lineContaining(quint64 address) const override;

    void readRegisters(const QStringList &names, std::function<void(const QList<Register> &)> cb) override;
    void writeRegister(const QString &name, const QString &valueText,
                       std::function<void(bool, const QString &)> cb) override;
    void readMemory(quint64 address, int length, std::function<void(bool, const QByteArray &)> cb) override;
    void writeMemory(quint64 address, const QByteArray &bytes, std::function<void(bool)> cb) override;
    void evaluateAddress(const QString &text, std::function<void(bool, quint64)> cb) override;
    void disassemble(quint64 start, int byteCount, std::function<void(const QList<Instruction> &)> cb) override;
    QString engineName() const override { return tr("Native (Windows Debug API)"); }

    int lineForAddress(quint64 address) const override { return lineOfAddress_.value(address, 0); }
    quint64 addressForLine(int line) const override { return addressOfLine_.value(line, 0); }
    quint64 currentPc() const override { return pc_; }
    bool is64Bit() const override { return true; }

private:
    enum State { Off, Starting, Stopped, Running, Ended };
    struct Mapping {
        QMap<quint64, int> lineOfAddress;
        QMap<int, quint64> addressOfLine;
        QList<QPair<QString, bool>> messages;   //!< text, is an error
    };

    void post(std::function<void()> job);       //!< run on the debugging thread
    void toUi(std::function<void()> job);       //!< run on the window's thread (from any thread)
    void startJob();
    Mapping buildMapping(quint64 mainAddress);
    void runJob(std::function<wdbg::Stop()> job);
    void handleStop(const wdbg::Stop &stop);
    void applyBreakpoints();
    void endSession();

    Config cfg_;
    QThread thread_;
    QObject *worker_ = nullptr;      //!< lives in thread_: the target of post()
    wdbg::Debuggee dbg_;             //!< used by the debugging thread only (kill() and pause() aside)
    State state_ = Off;
    std::atomic<bool> stopping_{ false };
    QSet<int> wantedBreakpoints_;
    QMap<int, QString> conditions_;
    QMap<quint64, int> lineOfAddress_;
    QMap<int, quint64> addressOfLine_;
    bool hasLines_ = false;
    quint64 pc_ = 0;
    QStringDecoder outputDecoder_{ QStringConverter::Utf8 };
};

#endif // Q_OS_WIN
#endif // ASMON_NATIVEDEBUGGER_H
