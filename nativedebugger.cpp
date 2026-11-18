#include "nativedebugger.h"

#if defined(Q_OS_WIN) || defined(ASMON_NATIVE_CHECK)

#include "assembler.h"
#include "linemap.h"
#include "native/pe.h"
#include "native/regs.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>

namespace {

std::wstring nativePath(const QString &p)
{
    return p.isEmpty() ? std::wstring() : QDir::toNativeSeparators(p).toStdWString();
}

QString fromStd(const std::string &s) { return QString::fromUtf8(s.c_str(), int(s.size())); }

} // namespace

NativeDebugger::NativeDebugger(const Config &config, QObject *parent)
    : DebugBackend(parent), cfg_(config)
{
    worker_ = new QObject;
    worker_->moveToThread(&thread_);
}

NativeDebugger::~NativeDebugger()
{
    if (thread_.isRunning()) {
        stopping_ = true;
        dbg_.kill();
        // let the debugging thread finish what it is doing, end the program and release it
        QMetaObject::invokeMethod(worker_, [this]() { dbg_.terminate(); }, Qt::BlockingQueuedConnection);
        thread_.quit();
        thread_.wait(10000);
    }
    delete worker_;
}

bool NativeDebugger::canDebug(const Config &config, QString *reason)
{
    auto no = [&](const QString &why) {
        if (reason)
            *reason = why;
        return false;
    };
    if (!config.windows || config.attachPid > 0 || !config.gdbPrefixArgs.isEmpty())
        return no(tr("not a native Windows program"));
    pe::Info info;
    std::string err;
    if (!pe::parseFile(QDir::toNativeSeparators(config.executable).toStdString(), info, err))
        return no(fromStd(err));
    if (!info.is64)
        return no(tr("32-bit program"));
    return true;
}

void NativeDebugger::post(std::function<void()> job)
{
    QMetaObject::invokeMethod(worker_, std::move(job), Qt::QueuedConnection);
}

void NativeDebugger::toUi(std::function<void()> job)
{
    QMetaObject::invokeMethod(this, std::move(job), Qt::QueuedConnection);
}

// ---------------------------------------------------------------- start

void NativeDebugger::start(const QList<int> &breakpointLines)
{
    for (int l : breakpointLines)
        wantedBreakpoints_.insert(l);
    state_ = Starting;
    thread_.start();
    post([this]() { startJob(); });
}

void NativeDebugger::startJob()
{
    std::string err;
    wdbg::Engine::OutputFn out = [this](const std::string &bytes) {
        const QByteArray data(bytes.data(), int(bytes.size()));
        toUi([this, data]() { const QString text = outputDecoder_(data);
            emit programOutput(text); });
    };
    if (!dbg_.launch(nativePath(cfg_.executable), nativePath(cfg_.workingDir), nativePath(cfg_.inputFile),
                     out, err)) {
        const QString msg = fromStd(err);
        toUi([this, msg]() {
            emit log(tr("Could not start the program: %1").arg(msg) + '\n', true);
            endSession();
        });
        return;
    }
    const quint64 mainAddress = dbg_.symbolAddress("main");
    if (!mainAddress) {
        dbg_.terminate();
        toUi([this]() {
            emit log(tr("Entry point \"main\" was not found. Debugging needs a global label \"main\".") + '\n', true);
            endSession();
        });
        return;
    }
    wdbg::Stop first = dbg_.runTo(mainAddress);
    if (stopping_) {
        dbg_.terminate();
        toUi([this]() { endSession(); });
        return;
    }
    if (first.kind != wdbg::Stop::Stopped) {
        // it ended or crashed before reaching main
        if (first.kind == wdbg::Stop::Finished)
            dbg_.terminate();
        toUi([this, first]() { handleStop(first); });
        return;
    }
    const Mapping m = buildMapping(mainAddress);
    std::map<uint64_t, int> lines;
    for (auto it = m.lineOfAddress.constBegin(); it != m.lineOfAddress.constEnd(); ++it)
        lines[it.key()] = it.value();
    dbg_.setLines(lines);
    toUi([this, m, first]() {
        for (const auto &msg : m.messages)
            emit log(msg.first + '\n', msg.second);
        lineOfAddress_ = m.lineOfAddress;
        addressOfLine_ = m.addressOfLine;
        hasLines_ = !lineOfAddress_.isEmpty();
        applyBreakpoints();
        pc_ = first.pc;
        state_ = Stopped;
        emit started();
        emit stopped(pc_, lineContaining(pc_));
    });
}

//! Where the listing's lines are in the running program: main's listing address vs. its real
//! address; for the other files of a project, a label found in both.
NativeDebugger::Mapping NativeDebugger::buildMapping(quint64 mainAddress)
{
    Mapping m;
    auto add = [&m](quint64 address, int line) {
        if (!m.lineOfAddress.contains(address))
            m.lineOfAddress.insert(address, line);
        if (!m.addressOfLine.contains(line))
            m.addressOfLine.insert(line, address);
    };
    if (cfg_.assembler) {
        QFile lst(cfg_.listing);
        if (lst.open(QIODevice::ReadOnly)) {
            const quint64 mainInListing = cfg_.assembler->getMainOffset(lst, "main");
            lst.close();
            if (qint64(mainInListing) != -1 && lst.open(QIODevice::ReadOnly)) {
                QVector<Assembler::LineNum> lines;
                cfg_.assembler->parseLstFile(lst, lines, mainAddress - mainInListing);
                lst.close();
                for (const Assembler::LineNum &l : lines)
                    add(l.numInMem, int(l.numInCode));
            }
        }
        for (int i = 0; i < cfg_.modules.size(); ++i) {
            const Config::Module &mod = cfg_.modules.at(i);
            const QString name = QFileInfo(mod.source).fileName();
            QVector<Assembler::LineNum> lines;
            QFile mlst(mod.listing);
            if (!mlst.open(QIODevice::ReadOnly)
                    || !cfg_.assembler->parseModuleLstFile(mlst, lines, 0, mod.source) || lines.isEmpty()) {
                m.messages << qMakePair(tr("%1: no line information (debugging it by instructions only).").arg(name), true);
                continue;
            }
            bool found = false;
            for (const auto &cand : moduleLabelCandidates(mod.source, lines)) {
                const quint64 runtime = dbg_.symbolAddress(cand.first.toStdString());
                if (!runtime)
                    continue;
                const quint64 offset = runtime - cand.second;
                const int base = (i + 1) * kModuleLineBase;
                for (const Assembler::LineNum &l : lines)
                    add(l.numInMem + offset, base + int(l.numInCode));
                found = true;
                break;
            }
            if (!found)
                m.messages << qMakePair(tr("%1: could not find its code in the program (debugging it by "
                                           "instructions only).").arg(name), true);
        }
    }
    if (m.lineOfAddress.isEmpty())
        m.messages << qMakePair(tr("Could not match the listing to the program: stepping by line is not available, "
                                   "single instructions still work."), true);
    return m;
}

// ---------------------------------------------------------------- stops

void NativeDebugger::runJob(std::function<wdbg::Stop()> job)
{
    state_ = Running;
    post([this, job]() {
        wdbg::Stop s = job();
        if (s.kind == wdbg::Stop::Finished)
            dbg_.terminate();            // also collects the program's last output before we report the end
        toUi([this, s]() { handleStop(s); });
    });
}

void NativeDebugger::handleStop(const wdbg::Stop &s)
{
    if (stopping_ || state_ == Ended) {
        endSession();
        return;
    }
    if (!s.note.empty())
        emit log(fromStd(s.note) + '\n', false);
    switch (s.kind) {
    case wdbg::Stop::Finished:
        state_ = Stopped;
        emit finished(s.exitCode);
        endSession();
        return;
    case wdbg::Stop::Crashed:
        pc_ = s.pc;
        state_ = Stopped;
        emit crashed(fromStd(s.exceptionName), fromStd(s.meaning), pc_, lineContaining(pc_));
        return;
    case wdbg::Stop::MemoryWritten:
        emit log(tr("Memory written: %1  %2 → %3").arg(fromStd(s.watchName), fromStd(s.oldValue),
                                                       fromStd(s.newValue)) + '\n', false);
        break;
    case wdbg::Stop::Stopped:
        break;
    }
    pc_ = s.pc;
    state_ = Stopped;
    emit stopped(pc_, lineContaining(pc_));
}

void NativeDebugger::endSession()
{
    if (state_ == Ended)
        return;
    state_ = Ended;
    post([this]() { dbg_.terminate(); });
    emit ended();
}

// ---------------------------------------------------------------- control

void NativeDebugger::continueRun()
{
    if (state_ != Stopped)
        return;
    runJob([this]() { return dbg_.cont(); });
    emit running();
}

void NativeDebugger::pause()
{
    if (state_ == Running)
        dbg_.pause();
}

void NativeDebugger::stepOver()
{
    if (state_ != Stopped)
        return;
    const bool lines = hasLines_;
    runJob([this, lines]() { return lines ? dbg_.stepOver() : dbg_.stepInstruction(); });
}

void NativeDebugger::stepInto()
{
    if (state_ != Stopped)
        return;
    const bool lines = hasLines_;
    runJob([this, lines]() { return lines ? dbg_.stepInto() : dbg_.stepInstruction(); });
}

void NativeDebugger::stepOut()
{
    if (state_ != Stopped)
        return;
    runJob([this]() { return dbg_.stepOut(); });
}

void NativeDebugger::stepInstruction()
{
    if (state_ != Stopped)
        return;
    runJob([this]() { return dbg_.stepInstruction(); });
}

void NativeDebugger::stop()
{
    if (state_ == Off || state_ == Ended) {
        endSession();
        return;
    }
    stopping_ = true;
    if (state_ == Running || state_ == Starting) {
        dbg_.kill();       // the debugging thread sees the end and reports it; handleStop() closes the session
        return;
    }
    toUi([this]() { endSession(); });
}

// ---------------------------------------------------------------- breakpoints

void NativeDebugger::setBreakpoint(int line, bool enabled)
{
    if (enabled)
        wantedBreakpoints_.insert(line);
    else
        wantedBreakpoints_.remove(line);
    applyBreakpoints();
}

void NativeDebugger::setBreakpointCondition(int line, const QString &condition)
{
    if (condition.trimmed().isEmpty())
        conditions_.remove(line);
    else
        conditions_.insert(line, condition.trimmed());
    applyBreakpoints();
}

//! Sends the breakpoints that have an address to the debugging thread. It applies them when it
//! is next free (a program that is running keeps the old set until it stops).
void NativeDebugger::applyBreakpoints()
{
    if (!hasLines_)
        return;
    std::set<uint64_t> addresses;
    std::map<uint64_t, std::string> conditions;
    for (int line : wantedBreakpoints_) {
        const quint64 a = addressOfLine_.value(line, 0);
        if (!a)
            continue;
        addresses.insert(a);
        if (conditions_.contains(line))
            conditions[a] = conditions_.value(line).toStdString();
    }
    post([this, addresses, conditions]() {
        dbg_.setBreakpoints(addresses);
        dbg_.setConditions(conditions);
    });
}

void NativeDebugger::watchMemory(const QString &addressText, int size, const QString &shownName,
                                 std::function<void(bool, const QString &)> cb)
{
    if (state_ != Stopped) {
        cb(false, tr("The program must be paused."));
        return;
    }
    const std::string text = addressText.toStdString(), name = shownName.toStdString();
    post([this, text, size, name, cb]() {
        std::string message;
        const bool ok = dbg_.watchMemory(text, size, name, message);
        toUi([cb, ok, message]() { cb(ok, fromStd(message)); });
    });
}

void NativeDebugger::clearMemoryWatches()
{
    post([this]() { dbg_.clearWatches(); });
}

// ---------------------------------------------------------------- data

int NativeDebugger::lineContaining(quint64 address) const
{
    if (!hasLines_ || !dbg_.inUserCode(address))
        return 0;
    auto it = lineOfAddress_.upperBound(address);
    if (it == lineOfAddress_.constBegin())
        return 0;
    --it;
    return it.value();
}

void NativeDebugger::callStack(std::function<void(const QList<Frame> &)> cb)
{
    if (state_ != Stopped)
        return;
    post([this, cb]() {
        QList<Frame> frames;
        for (const auto &f : dbg_.callStack())
            frames << Frame{ f.address, f.line };
        toUi([cb, frames]() { cb(frames); });
    });
}

void NativeDebugger::readRegisters(const QStringList &names, std::function<void(const QList<Register> &)> cb)
{
    if (state_ != Stopped)
        return;
    post([this, names, cb]() {
        QList<Register> out;
        nd::Regs r;
        if (dbg_.regs(r)) {
            for (const QString &n : names) {
                uint64_t v = 0;
                if (nd::getRegister(r, n.toStdString(), v))
                    out << Register{ n, v };
            }
        }
        toUi([cb, out]() { cb(out); });
    });
}

void NativeDebugger::writeRegister(const QString &name, const QString &valueText,
                                   std::function<void(bool, const QString &)> cb)
{
    if (state_ != Stopped) {
        cb(false, tr("The program must be paused."));
        return;
    }
    const std::string n = name.toStdString(), v = valueText.toStdString();
    post([this, n, v, cb]() {
        std::string err;
        const bool ok = dbg_.setRegister(n, v, err);
        toUi([cb, ok, err]() { cb(ok, fromStd(err)); });
    });
}

void NativeDebugger::readMemory(quint64 address, int length, std::function<void(bool, const QByteArray &)> cb)
{
    if (state_ != Stopped || length <= 0) {
        cb(false, QByteArray());
        return;
    }
    post([this, address, length, cb]() {
        QByteArray data(length, '\0');
        const bool ok = dbg_.engine().read(address, data.data(), size_t(length));
        toUi([cb, ok, data]() { cb(ok, ok ? data : QByteArray()); });
    });
}

void NativeDebugger::writeMemory(quint64 address, const QByteArray &bytes, std::function<void(bool)> cb)
{
    if (state_ != Stopped) {
        cb(false);
        return;
    }
    post([this, address, bytes, cb]() {
        const bool ok = dbg_.engine().write(address, bytes.constData(), size_t(bytes.size()));
        toUi([cb, ok]() { cb(ok); });
    });
}

void NativeDebugger::evaluateAddress(const QString &text, std::function<void(bool, quint64)> cb)
{
    if (state_ != Stopped) {
        cb(false, 0);
        return;
    }
    const std::string t = text.toStdString();
    post([this, t, cb]() {
        uint64_t a = 0;
        std::string err;
        const bool ok = dbg_.evalAddress(t, a, err);
        toUi([cb, ok, a]() { cb(ok, a); });
    });
}

void NativeDebugger::disassemble(quint64 start, int byteCount, std::function<void(const QList<Instruction> &)> cb)
{
    if (state_ != Stopped) {
        cb(QList<Instruction>());
        return;
    }
    post([this, start, byteCount, cb]() {
        QList<Instruction> out;
        for (const wdbg::Instruction &i : dbg_.disassemble(start, byteCount)) {
            Instruction x;
            x.address = i.address;
            x.bytes = QByteArray(reinterpret_cast<const char *>(i.bytes.data()), int(i.bytes.size()));
            x.text = fromStd(i.text);
            x.symbol = fromStd(i.symbol);
            out << x;
        }
        toUi([cb, out]() { cb(out); });
    });
}

#endif // Q_OS_WIN
