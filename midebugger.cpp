/****************************************************************************
** Asmon — fork of SASM (GPL-3.0). Copyright © 2013 Dmitriy Manushin.
** Modifications © 2026 Asmon contributors.
****************************************************************************/
#include "midebugger.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QTimer>
#include <cstdio>

#ifdef Q_OS_WIN32
#include <windows.h>
#else
#include <signal.h>
#endif

namespace {
const quint64 kUserCodeSlack = 0x1000;  // macro code after the last mapped line

quint64 parseAddress(const QString &s)
{
    bool ok = false;
    const quint64 v = s.toULongLong(&ok, 16);
    return ok ? v : 0;
}
} // namespace

MiDebugger::MiDebugger(const Config &config, QObject *parent)
    : QObject(parent), cfg(config)
{
}

MiDebugger::~MiDebugger()
{
    if (gdb) {
        gdb->disconnect(this);
        if (gdb->state() != QProcess::NotRunning) {
            gdb->write("-gdb-exit\n");
            if (!gdb->waitForFinished(1500))
                gdb->kill();
        }
        delete gdb;
    }
}

// ---------------------------------------------------------------- start / stop

void MiDebugger::start(const QList<int> &breakpointLines)
{
    for (int l : breakpointLines)
        wantedBreakpoints.insert(l);

    gdb = new QProcess(this);
    gdb->setProcessChannelMode(QProcess::MergedChannels);
    connect(gdb, &QProcess::readyReadStandardOutput, this, &MiDebugger::onReadyRead);
    connect(gdb, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &MiDebugger::onProcessFinished);
    //! WinLibs' gdb embeds Python; without PYTHONHOME it prints "Could not find
    //! platform independent libraries" into the program's output.
    const QDir gdbPrefix(QFileInfo(cfg.gdb).absolutePath() + "/..");
    if (QFileInfo(cfg.gdb).isAbsolute() && !gdbPrefix.entryList({ "python3*" }, QDir::Dirs).isEmpty() == false
            && !QDir(gdbPrefix.absolutePath() + "/lib").entryList({ "python3*" }, QDir::Dirs).isEmpty()) {
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert("PYTHONHOME", QDir::toNativeSeparators(gdbPrefix.absolutePath()));
        gdb->setProcessEnvironment(env);
    }
    gdb->start(cfg.gdb, QStringList() << "--interpreter=mi3" << "-nx" << "-q" << cfg.executable);
    if (!gdb->waitForStarted(10000)) {
        emit log(tr("Could not start gdb (%1).").arg(cfg.gdb), true);
        state = Ended;
        emit ended();
        return;
    }
    state = Starting;
    auto ignore = [](const MiRecord &) {};
    command("-gdb-set confirm off", ignore);
    command("-gdb-set pagination off", ignore);
    command("-gdb-set width 0", ignore);
    command("-gdb-set print pretty off", ignore);
    command("-gdb-set disassembly-flavor intel", ignore);
    command("-gdb-set mi-async on", ignore); // lets -exec-interrupt work while running
    command("-environment-cd " + GdbMi::quote(cfg.workingDir), ignore);
    if (cfg.windows) {
        command("-gdb-set new-console off", ignore);
    } else if (!cfg.inputFile.isEmpty()) {
        //! gdb starts the program through a shell on Unix: redirection works.
        command("-exec-arguments < " + cfg.inputFile, ignore);
    }
    command("-break-insert -t *main", [this](const MiRecord &r) {
        if (r.cls == "error") {
            emit log(tr("Entry point \"main\" was not found. Debugging needs a global label \"main\".") + '\n', true);
            stop();
        }
    });
    command("-exec-run", [this](const MiRecord &r) {
        if (r.cls == "error") {
            emit log(tr("gdb could not start the program: %1").arg(r.results.get("msg")) + '\n', true);
            stop();
        }
    });
}

void MiDebugger::stop()
{
    if (!gdb || gdb->state() == QProcess::NotRunning) {
        endSession();
        return;
    }
    if (state == Running)
        pause();
    gdb->write("-gdb-exit\n");
    QTimer::singleShot(2000, this, [this]() {
        if (gdb && gdb->state() != QProcess::NotRunning)
            gdb->kill();
    });
}

void MiDebugger::endSession()
{
    if (state == Ended)
        return;
    state = Ended;
    emit ended();
}

void MiDebugger::onProcessFinished()
{
    if (!buffer.isEmpty()) {
        handleLine(QString::fromUtf8(buffer));
        buffer.clear();
    }
    endSession();
}

// ---------------------------------------------------------------- commands

void MiDebugger::command(const QString &cmd, Callback cb)
{
    if (!gdb || gdb->state() != QProcess::Running)
        return;
    const int token = nextToken++;
    if (cb)
        callbacks.insert(token, cb);
    static const bool trace = qEnvironmentVariableIsSet("ASMON_MI_TRACE");
    if (trace)
        fprintf(stderr, "gdb< %d%s\n", token, qPrintable(cmd));
    gdb->write((QString::number(token) + cmd + "\n").toUtf8());
}

void MiDebugger::consoleCommand(const QString &cliCommand)
{
    //! gdb's console text is shown only for commands the user typed; the
    //! internal line breakpoints would otherwise print "Breakpoint 3, ..."
    ++consoleCommandsPending;
    command("-interpreter-exec console " + GdbMi::quote(cliCommand), [this](const MiRecord &r) {
        --consoleCommandsPending;
        if (r.cls == "error")
            emit consoleOutput(r.results.get("msg") + '\n');
    });
}

// ---------------------------------------------------------------- output parsing

void MiDebugger::onReadyRead()
{
    buffer += gdb->readAllStandardOutput();
    int nl;
    while ((nl = buffer.indexOf('\n')) >= 0) {
        QByteArray raw = buffer.left(nl);
        buffer.remove(0, nl + 1);
        if (raw.endsWith('\r'))
            raw.chop(1);
        handleLine(QString::fromUtf8(raw));
    }
}

void MiDebugger::handleLine(const QString &line)
{
    static const bool trace = qEnvironmentVariableIsSet("ASMON_MI_TRACE");
    if (trace)
        fprintf(stderr, "gdb> %s\n", qPrintable(line));
    MiRecord r = GdbMi::parseLine(line);
    if (r.kind != MiRecord::Invalid) {
        handleRecord(r);
        return;
    }
    //! Not MI: output of the debugged program, which shares gdb's stdout.
    //! A program that prints without a final newline makes gdb's next record
    //! start in the middle of the line ("Hello*stopped,reason=..."): split it.
    static const QRegularExpression miStart(
        "(\\d*[\\^*=+][a-z][a-z-]*(,|$)|[~@&]\"|\\(gdb\\)\\s*$)");
    auto it = miStart.globalMatch(line);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        if (m.capturedStart() == 0)
            continue;
        const MiRecord rest = GdbMi::parseLine(line.mid(m.capturedStart()));
        if (rest.kind != MiRecord::Invalid) {
            emit programOutput(line.left(m.capturedStart()));
            handleRecord(rest);
            return;
        }
    }
    emit programOutput(line + '\n');
}

void MiDebugger::handleRecord(const MiRecord &r)
{
    switch (r.kind) {
    case MiRecord::Result: {
        Callback cb = callbacks.take(r.token);
        if (cb)
            cb(r);
        else if (r.cls == "error")
            emit log(r.results.get("msg") + '\n', true);
        break;
    }
    case MiRecord::ExecAsync:
        if (r.cls == "stopped") {
            handleStopped(r.results);
        } else if (r.cls == "running") {
            if (state == Stopped)
                state = Running;
        }
        break;
    case MiRecord::NotifyAsync:
        if (r.cls == "thread-group-started")
            inferiorPid = r.results.get("pid").toLongLong();
        break;
    case MiRecord::ConsoleStream:
        if (consoleCommandsPending > 0)
            emit consoleOutput(r.text);
        break;
    case MiRecord::TargetStream:
        emit programOutput(r.text);
        break;
    default:
        break;
    }
}

// ---------------------------------------------------------------- stops

void MiDebugger::handleStopped(const MiValue &res)
{
    const QString reason = res.get("reason");
    const quint64 addr = parseAddress(res["frame"].get("addr"));
    if (addr)
        pc = addr;

    if (reason == "exited-normally" || reason == "exited") {
        stepMode = NoStep;
        state = Stopped;
        emit finished(res.get("exit-code", "0").toInt(nullptr, 8));
        stop();
        return;
    }
    if (reason == "exited-signalled") {
        stepMode = NoStep;
        emit crashed(res.get("signal-name"), res.get("signal-meaning"), pc, currentLine());
        stop();
        return;
    }

    const QString signal = res.get("signal-name");
    if (stepPhase == PhaseRunToLine && (reason == "signal-received" || pauseRequested))
        command("-break-disable " + lineBreakpoints.join(' '));
    if (pauseRequested && (reason.isEmpty() || signal == "SIGINT" || signal == "SIGTRAP" || signal == "0")) {
        pauseRequested = false;
        stepPhase = PhaseNone;
        stepMode = NoStep;
        state = Stopped;
        applyPendingBreakpoints();
        emit stopped(pc, currentLine());
        return;
    }
    if (reason == "signal-received") {
        //! The program crashed (or got a signal): stay stopped so registers,
        //! memory and the stack can be inspected at the faulting instruction.
        stepMode = NoStep;
        stepPhase = PhaseNone;
        state = Stopped;
        emit crashed(signal, res.get("signal-meaning"), pc, currentLine());
        return;
    }

    if (firstStop) {
        firstStop = false;
        setupAfterFirstStop(pc);
        return;
    }

    if (stepMode != NoStep) {
        stepStopped(reason);
        return;
    }
    finishStepping(pc);
}

void MiDebugger::setupAfterFirstStop(quint64 mainAddress)
{
    //! Map listing addresses to runtime addresses.
    QFile lst(cfg.listing);
    if (cfg.assembler && lst.open(QIODevice::ReadOnly)) {
        const quint64 mainInListing = cfg.assembler->getMainOffset(lst, "main");
        lst.close();
        if ((qint64) mainInListing != -1 && lst.open(QIODevice::ReadOnly)) {
            QVector<Assembler::LineNum> lines;
            cfg.assembler->parseLstFile(lst, lines, mainAddress - mainInListing);
            lst.close();
            for (const Assembler::LineNum &l : lines) {
                const quint64 a = l.numInMem;
                const int line = int(l.numInCode);
                if (!lineOfAddress.contains(a))
                    lineOfAddress.insert(a, line);
                if (!addressOfLine.contains(line))
                    addressOfLine.insert(line, a);
            }
        }
    }
    if (lineOfAddress.isEmpty()) {
        emit log(tr("Could not match the listing to the program: stepping by line is not available, "
                    "single instructions still work.") + '\n', true);
        userLow = mainAddress;
        userHigh = mainAddress + kUserCodeSlack;
    } else {
        userLow = lineOfAddress.firstKey();
        userHigh = lineOfAddress.lastKey() + kUserCodeSlack;
    }

    command("-data-evaluate-expression " + GdbMi::quote("sizeof(void*)"), [this](const MiRecord &r) {
        if (r.cls == "done")
            arch64 = r.results.get("value") == "8";
    });

    if (cfg.windows && !cfg.inputFile.isEmpty()) {
        //! gdb cannot redirect stdin on Windows: ask the program's helper
        //! (macro_win.c, linked into every program) to reopen it.
        QString path = cfg.inputFile;
        path.replace('\\', '/');
        command("-data-evaluate-expression "
                    + GdbMi::quote(QString("(int)asmon_redirect_stdin(\"%1\")").arg(path)),
                [](const MiRecord &) {});
    }

    //! One disabled breakpoint at the start of every line: a "step" enables
    //! them all and continues, so macro bodies run at full speed instead of
    //! being single-stepped (which also leaked the trap flag through pushf).
    QSet<quint64> seen;
    for (auto it = lineOfAddress.constBegin(); it != lineOfAddress.constEnd(); ++it) {
        if (seen.contains(it.key()))
            continue;
        seen.insert(it.key());
        command(QString("-break-insert -d *0x%1").arg(it.key(), 0, 16), [this](const MiRecord &r) {
            if (r.cls == "done")
                lineBreakpoints << r.results["bkpt"].get("number");
        });
    }

    applyPendingBreakpoints();
    state = Stopped;
    emit started();
    emit stopped(pc, currentLine());
}

void MiDebugger::instructionAt(quint64 address, std::function<void(const QString &, quint64)> cb)
{
    command(QString("-data-disassemble -s 0x%1 -e 0x%2 -- 0").arg(address, 0, 16).arg(address + 32, 0, 16),
            [cb](const MiRecord &r) {
        const QList<MiValue> insns = r.results["asm_insns"].elements();
        if (r.cls != "done" || insns.isEmpty()) {
            cb(QString(), 0);
            return;
        }
        const QString inst = insns[0].get("inst").trimmed().toLower();
        const quint64 next = insns.size() > 1 ? parseAddress(insns[1].get("address")) : 0;
        cb(inst, next);
    });
}

void MiDebugger::beginStep(StepMode mode)
{
    if (state != Stopped)
        return;
    stepMode = mode;
    stepPhase = PhaseNone;
    stepStartPc = pc;
    state = Running;
    emit running();
    instructionAt(pc, [this, mode](const QString &inst, quint64 next) {
        if (mode == StepInsn) {
            //! Single-stepping "pushf" pushes the CPU's trap flag, and the
            //! matching "popf" would then make every instruction trap. Clear it.
            fixPushedTrapFlag = inst.startsWith("pushf");
            stepPhase = PhaseInsn;
            command("-exec-step-instruction");
            return;
        }
        if (mode == StepOut) {
            stepPhase = PhaseFinish;
            command("-exec-finish", [this](const MiRecord &r) {
                if (r.cls == "error") {
                    //! e.g. in main, which has no caller frame gdb can see
                    emit log(tr("Step out: %1").arg(r.results.get("msg")) + '\n', true);
                    finishStepping(pc);
                }
            });
            return;
        }
        const bool isCall = inst.startsWith("call");
        if (isCall && mode == StepOver && next) {
            //! Run the whole call: stop at the instruction after it.
            stepPhase = PhaseAfterCall;
            command(QString("-break-insert -t *0x%1").arg(next, 0, 16), [this](const MiRecord &r) {
                tempBreakpoint = r.results["bkpt"].get("number");
            });
            command("-exec-continue");
        } else if (isCall && mode == StepInto) {
            stepPhase = PhaseIntoCall;
            command("-exec-step-instruction");
        } else {
            runToNextLine();
        }
    });
}

void MiDebugger::runToNextLine()
{
    if (lineBreakpoints.isEmpty()) {
        //! no listing mapping: fall back to one instruction
        stepPhase = PhaseInsn;
        command("-exec-step-instruction");
        return;
    }
    stepPhase = PhaseRunToLine;
    command("-break-enable " + lineBreakpoints.join(' '));
    command("-exec-continue");
}

void MiDebugger::stepStopped(const QString &reason)
{
    //! Undo what this step set up.
    if (stepPhase == PhaseRunToLine)
        command("-break-disable " + lineBreakpoints.join(' '));
    if (!tempBreakpoint.isEmpty()) {
        if (reason != "breakpoint-hit" || stepPhase != PhaseAfterCall)
            command("-break-delete " + tempBreakpoint, [](const MiRecord &) {});
        tempBreakpoint.clear();
    }

    const int line = lineForAddress(pc);
    if (stepPhase == PhaseInsn) {
        if (fixPushedTrapFlag) {
            fixPushedTrapFlag = false;
            command("-data-evaluate-expression " + GdbMi::quote(arch64
                ? "*(unsigned long long *)$rsp &= ~0x100ULL"
                : "*(unsigned int *)$esp &= ~0x100U"), [](const MiRecord &) {});
        }
        finishStepping(pc);
        return;
    }
    if (line > 0 || reason == "breakpoint-hit" && stepPhase != PhaseAfterCall) {
        finishStepping(pc);
        return;
    }
    if (stepPhase == PhaseIntoCall && !inUserCode(pc)) {
        //! Stepped into a library function (printf...): come back out.
        stepPhase = PhaseFinish;
        command("-exec-finish", [this](const MiRecord &r) {
            if (r.cls == "error")
                runToNextLine();
        });
        return;
    }
    if (inUserCode(pc)) {
        runToNextLine(); // e.g. in the middle of a macro's expansion
        return;
    }
    //! Outside your program (e.g. after "ret" from main): let it run on.
    stepMode = NoStep;
    stepPhase = PhaseNone;
    command("-exec-continue");
}

void MiDebugger::finishStepping(quint64 address)
{
    stepMode = NoStep;
    stepPhase = PhaseNone;
    state = Stopped;
    applyPendingBreakpoints();
    emit stopped(address, lineForAddress(address));
}

// ---------------------------------------------------------------- control

void MiDebugger::continueRun()
{
    if (state != Stopped)
        return;
    stepMode = NoStep;
    state = Running;
    emit running();
    command("-exec-continue");
}

void MiDebugger::pause()
{
    if (state != Running)
        return;
    pauseRequested = true;
#ifdef Q_OS_WIN32
    //! Native Windows gdb: break into the process from outside (like SASM).
    if (inferiorPid > 0) {
        HANDLE h = OpenProcess(PROCESS_ALL_ACCESS, FALSE, DWORD(inferiorPid));
        if (h) {
            DebugBreakProcess(h);
            CloseHandle(h);
            return;
        }
    }
#endif
    command("-exec-interrupt", [this](const MiRecord &r) {
#ifndef Q_OS_WIN32
        if (r.cls == "error" && inferiorPid > 0)
            ::kill(pid_t(inferiorPid), SIGINT);
#else
        Q_UNUSED(r);
#endif
    });
}

void MiDebugger::stepOver() { beginStep(StepOver); }
void MiDebugger::stepInto() { beginStep(StepInto); }
void MiDebugger::stepInstruction() { beginStep(StepInsn); }
void MiDebugger::stepOut() { beginStep(StepOut); }

// ---------------------------------------------------------------- breakpoints

void MiDebugger::setBreakpoint(int line, bool enabled)
{
    if (enabled)
        wantedBreakpoints.insert(line);
    else
        wantedBreakpoints.remove(line);
    if (state == Stopped)
        applyPendingBreakpoints();
}

void MiDebugger::applyPendingBreakpoints()
{
    if (addressOfLine.isEmpty())
        return;
    for (int line : wantedBreakpoints) {
        if (breakpointIds.contains(line))
            continue;
        //! A line without an instruction (label, comment): use the next one.
        auto it = addressOfLine.lowerBound(line);
        if (it == addressOfLine.end())
            continue;
        breakpointIds.insert(line, QString()); // pending
        command(QString("-break-insert *0x%1").arg(it.value(), 0, 16), [this, line](const MiRecord &r) {
            if (r.cls == "done")
                breakpointIds.insert(line, r.results["bkpt"].get("number"));
            else
                breakpointIds.remove(line);
        });
    }
    for (auto it = breakpointIds.begin(); it != breakpointIds.end();) {
        if (!wantedBreakpoints.contains(it.key()) && !it.value().isEmpty()) {
            command("-break-delete " + it.value(), [](const MiRecord &) {});
            it = breakpointIds.erase(it);
        } else {
            ++it;
        }
    }
}

// ---------------------------------------------------------------- mapping

int MiDebugger::lineForAddress(quint64 address) const
{
    return lineOfAddress.value(address, 0);
}

quint64 MiDebugger::addressForLine(int line) const
{
    return addressOfLine.value(line, 0);
}

bool MiDebugger::inUserCode(quint64 address) const
{
    return address >= userLow && address < userHigh;
}
