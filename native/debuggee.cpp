#ifdef _WIN32

#include "debuggee.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>

namespace wdbg {

namespace {

std::string hex(uint64_t v)
{
    char b[24];
    std::snprintf(b, sizeof b, "0x%llx", (unsigned long long)v);
    return b;
}

constexpr int kStepsBeforeFastRun = 2000;   // a long loop inside one line runs at full speed after this
constexpr int kStackScanSlots = 1024;

} // namespace

// ----------------------------------------------------------------- start

static bool readWholeFile(const std::wstring &path, std::vector<uint8_t> &data)
{
    FILE *f = _wfopen(path.c_str(), L"rb");
    if (!f)
        return false;
    uint8_t chunk[65536];
    size_t got;
    while ((got = std::fread(chunk, 1, sizeof chunk, f)) > 0)
        data.insert(data.end(), chunk, chunk + got);
    std::fclose(f);
    return true;
}

bool Debuggee::launch(const std::wstring &exe, const std::wstring &cwd, const std::wstring &stdinFile,
                      Engine::OutputFn out, std::string &err)
{
    // the program's symbols (labels): without them there is no "main", so read them first
    std::vector<uint8_t> data;
    if (!readWholeFile(exe, data)) {
        err = "cannot read the program file";
        return false;
    }
    std::string perr;
    if (!pe::parse(data.data(), data.size(), pe_, perr)) {
        err = "not a program Windows can run: " + perr;
        return false;
    }
    if (!pe_.is64) {
        err = "this is a 32-bit program; the native debugger supports 64-bit programs";
        return false;
    }
    return eng_.launch(exe, cwd, stdinFile, std::move(out), err);
}

bool Debuggee::attach(uint32_t pid, const std::wstring &imagePath, std::string &err)
{
    std::vector<uint8_t> data;
    std::string perr;
    if (!imagePath.empty() && readWholeFile(imagePath, data))
        pe::parse(data.data(), data.size(), pe_, perr);   // no symbols if it fails: still debuggable
    return eng_.attach(pid, err);
}

uint64_t Debuggee::symbolAddress(const std::string &name) const
{
    const pe::Symbol *s = pe_.findSymbol(name);
    return s ? eng_.imageBase() + s->rva : 0;
}

std::string Debuggee::symbolize(uint64_t address) const
{
    const uint64_t base = eng_.imageBase();
    if (!base || address < base || address >= base + pe_.sizeOfImage)
        return "";
    return pe_.symbolize(uint32_t(address - base));
}

// ----------------------------------------------------------------- lines

void Debuggee::setLines(const std::map<uint64_t, int> &lineOfAddress)
{
    lineOfAddress_ = lineOfAddress;
    userLow_ = userHigh_ = 0;
    userCode_.clear();
    if (lineOfAddress_.empty())
        return;
    userLow_ = lineOfAddress_.begin()->first;
    const uint64_t last = lineOfAddress_.rbegin()->first;
    // the end of the last line: the next symbol of the program after it (the next object file's
    // code: the C runtime, library stubs), else the end of its section
    const uint64_t base = eng_.imageBase();
    uint64_t high = last + 1;
    const pe::Section *sec = base && last >= base ? pe_.sectionOfRva(uint32_t(last - base)) : nullptr;
    if (sec) {
        high = base + sec->rva + sec->virtualSize;
        for (const pe::Symbol &s : pe_.symbols)
            if (s.section && base + s.rva > last && base + s.rva < high)
                high = base + s.rva;
    }
    userHigh_ = std::max(high, last + 1);
    if (userHigh_ - userLow_ > (64u << 20))
        userHigh_ = userLow_ + (64u << 20);
    userCode_.resize(size_t(userHigh_ - userLow_));
    if (!eng_.read(userLow_, userCode_.data(), userCode_.size()))
        userCode_.clear();
}

int Debuggee::lineAt(uint64_t address) const
{
    auto it = lineOfAddress_.find(address);
    return it == lineOfAddress_.end() ? 0 : it->second;
}

int Debuggee::lineContaining(uint64_t address) const
{
    if (!inUserCode(address))
        return 0;
    auto it = lineOfAddress_.upper_bound(address);
    if (it == lineOfAddress_.begin())
        return 0;
    return std::prev(it)->second;
}

void Debuggee::setCondition(uint64_t address, const std::string &condition)
{
    if (condition.empty())
        conditions_.erase(address);
    else
        conditions_[address] = condition;
}

// ----------------------------------------------------------------- helpers

size_t Debuggee::readCode(uint64_t address, uint8_t *buf, size_t size)
{
    for (size_t n = size; n > 0; --n)
        if (eng_.read(address, buf, n))
            return n;
    return 0;
}

expr::Context Debuggee::context()
{
    auto regs = std::make_shared<nd::Regs>();
    const bool have = eng_.getRegs(*regs);
    expr::Context c;
    c.reg = [regs, have](const std::string &name, uint64_t &v) { return have && nd::getRegister(*regs, name, v); };
    c.symbol = [this](const std::string &name, uint64_t &v) {
        v = symbolAddress(name);
        return v != 0;
    };
    c.mem = [this](uint64_t address, int size, uint64_t &v) {
        v = 0;
        return size >= 1 && size <= 8 && eng_.read(address, &v, size_t(size));
    };
    return c;
}

bool Debuggee::conditionHolds(uint64_t pc, std::string &note)
{
    auto it = conditions_.find(pc);
    if (it == conditions_.end())
        return true;
    uint64_t v = 0;
    std::string err;
    if (!expr::eval(it->second, context(), v, err)) {
        note = "The condition \"" + it->second + "\" could not be evaluated (" + err + "); stopping.";
        return true;
    }
    return v != 0;
}

Stop Debuggee::stopHere()
{
    Stop s;
    nd::Regs r;
    if (eng_.getRegs(r))
        s.pc = r.rip;
    return s;
}

bool Debuggee::watchTriggered(const Event &e, Stop &out)
{
    if (e.watchSlot < 0 || e.watchSlot > 3 || !watches_[e.watchSlot].used)
        return false;
    Watch &w = watches_[e.watchSlot];
    std::vector<uint8_t> now(size_t(w.size));
    if (!eng_.read(w.address, now.data(), now.size()))
        return false;
    if (now == w.last)
        return false;       // written with the same value: not a change
    auto num = [](const std::vector<uint8_t> &b) {
        uint64_t v = 0;
        std::memcpy(&v, b.data(), std::min<size_t>(8, b.size()));
        return v;
    };
    out = Stop();
    out.kind = Stop::MemoryWritten;
    out.pc = e.pc;
    out.watchName = w.name;
    out.oldValue = hex(num(w.last));
    out.newValue = hex(num(now));
    w.last = now;
    return true;
}

Stop Debuggee::makeStop(const Event &e)
{
    Stop s;
    s.pc = e.pc;
    switch (e.kind) {
    case StopKind::Exited:
        s.kind = Stop::Finished;
        s.exitCode = e.exitCode;
        break;
    case StopKind::Exception:
        if (e.exceptionCode == 0x80000003u) {       // int3 written in the program
            s.note = "The program executed an int3 instruction.";
        } else {
            s.kind = Stop::Crashed;
            s.exceptionName = exceptionName(e.exceptionCode);
            s.meaning = exceptionMeaning(e);
        }
        break;
    case StopKind::Watch: {
        Stop w;
        if (watchTriggered(e, w))
            return w;
        break;
    }
    default:
        break;
    }
    return s;
}

// ----------------------------------------------------------------- running

Stop Debuggee::runUntilStop(const std::map<uint64_t, uint64_t> &temps)
{
    for (;;) {
        std::set<uint64_t> all = userBps_;
        for (const auto &t : temps)
            all.insert(t.first);
        eng_.setBreakpoints(all);
        const Event e = eng_.cont();
        if (e.kind == StopKind::Watch) {
            Stop w;
            if (watchTriggered(e, w))
                return w;
            continue;           // same value written again: keep going
        }
        if (e.kind != StopKind::Breakpoint)
            return makeStop(e);
        nd::Regs r;
        eng_.getRegs(r);
        auto t = temps.find(e.pc);
        if (t != temps.end() && r.rsp >= t->second)
            return stopHere();
        if (userBps_.count(e.pc)) {
            std::string note;
            if (conditionHolds(e.pc, note)) {
                Stop s = stopHere();
                s.note = note;
                return s;
            }
        }
        // a conditional breakpoint that did not hold, or a temp in a deeper recursion: go on
    }
}

Stop Debuggee::cont()
{
    return runUntilStop({});
}

Stop Debuggee::runTo(uint64_t address)
{
    return runUntilStop({ { address, 0 } });
}

bool Debuggee::stepOnce(Stop &out)
{
    nd::Regs r;
    bool pushf = false;
    if (eng_.getRegs(r)) {
        uint8_t buf[16];
        const size_t n = readCode(r.rip, buf, sizeof buf);
        dis::Insn in;
        if (n && dis::decode(buf, n, r.rip, true, in))
            pushf = in.pushf;
    }
    Event e = eng_.step();
    if (e.kind == StopKind::Watch) {
        Stop w;
        if (watchTriggered(e, w)) {
            out = w;
            return false;
        }
        e.kind = StopKind::Step;
    }
    if (e.kind != StopKind::Step) {
        out = makeStop(e);
        return false;
    }
    if (pushf) {
        // the trap flag we set to step went onto the stack with the flags: take it out again
        nd::Regs a;
        uint64_t flags = 0;
        if (eng_.getRegs(a) && eng_.read(a.rsp, &flags, 8) && (flags & 0x100)) {
            flags &= ~uint64_t(0x100);
            eng_.write(a.rsp, &flags, 8);
        }
    }
    return true;
}

Stop Debuggee::stepInstruction()
{
    Stop out;
    if (!stepOnce(out))
        return out;
    return stopHere();
}

Stop Debuggee::stepLine(bool into)
{
    int steps = 0;
    for (;;) {
        nd::Regs r;
        if (!eng_.getRegs(r))
            return stopHere();
        uint8_t buf[16];
        const size_t n = readCode(r.rip, buf, sizeof buf);
        dis::Insn in;
        const bool decoded = n && dis::decode(buf, n, r.rip, true, in);

        if (decoded && in.call) {
            const uint64_t ret = r.rip + uint64_t(in.length);
            bool over = !into;
            if (into) {
                Stop out;
                if (!stepOnce(out))
                    return out;
                nd::Regs a;
                eng_.getRegs(a);
                if (inUserCode(a.rip)) {
                    if (lineAt(a.rip))
                        return stopHere();
                    continue;                // a function entry in the middle of a line: keep stepping
                }
                over = true;                 // into the system / a library: run until it returns
                // we are already past the call instruction
            }
            if (over) {
                Stop s = runUntilStop({ { ret, r.rsp } });
                if (!(s.kind == Stop::Stopped && s.pc == ret && s.note.empty()))
                    return s;                // a breakpoint elsewhere, a crash, the end...
                // this was our own stop: carry on in the line
            }
        } else {
            Stop out;
            if (!stepOnce(out))
                return out;
        }

        nd::Regs a;
        if (!eng_.getRegs(a))
            return stopHere();
        if (!inUserCode(a.rip))
            return runUntilStop({});         // left the program's own code (main returned...)
        if (lineAt(a.rip))
            return stopHere();
        if (++steps >= kStepsBeforeFastRun) {
            // a long loop inside one line: stop single-stepping, run to the next line start
            std::map<uint64_t, uint64_t> temps;
            for (const auto &l : lineOfAddress_)
                temps[l.first] = 0;
            return runUntilStop(temps);
        }
    }
}

Stop Debuggee::stepOver() { return stepLine(false); }
Stop Debuggee::stepInto() { return stepLine(true); }

bool Debuggee::isReturnAddress(uint64_t v) const
{
    if (!inUserCode(v) || v == userLow_ || userCode_.empty())
        return false;
    const size_t off = size_t(v - userLow_);
    const size_t have = std::min<size_t>(off, 16);
    return dis::endsWithCall(userCode_.data() + off - have, have, v, true);
}

Stop Debuggee::stepOut()
{
    nd::Regs r;
    if (!eng_.getRegs(r))
        return stopHere();
    for (int i = 0; i < kStackScanSlots; ++i) {
        const uint64_t slot = r.rsp + uint64_t(i) * 8;
        uint64_t v = 0;
        if (!eng_.read(slot, &v, 8))
            break;
        if (isReturnAddress(v))
            return runUntilStop({ { v, slot + 8 } });
    }
    // no caller of ours is known: this is main (called by the C runtime) or code entered with jmp
    Stop s = runUntilStop({});
    s.note = "Step out: no caller in your code (this is main), so the program runs on until it ends or reaches a breakpoint.";
    return s;
}

// ----------------------------------------------------------------- watch

bool Debuggee::watchMemory(const std::string &text, int size, const std::string &shownName, std::string &message)
{
    uint64_t addr = 0;
    if (!evalAddress(text, addr, message))
        return false;
    int slot = -1;
    for (int i = 0; i < 4; ++i)
        if (!watches_[i].used) { slot = i; break; }
    if (slot < 0) {
        message = "only 4 memory stops are possible at once";
        return false;
    }
    if (!eng_.setWatch(slot, addr, size, message))
        return false;
    Watch &w = watches_[slot];
    w.used = true;
    w.address = addr;
    w.size = size;
    w.name = shownName.empty() ? text : shownName;
    w.last.assign(size_t(size), 0);
    eng_.read(addr, w.last.data(), w.last.size());
    return true;
}

void Debuggee::clearWatches()
{
    for (Watch &w : watches_)
        w = Watch();
    eng_.clearWatches();
}

// ----------------------------------------------------------------- data

bool Debuggee::setRegister(const std::string &name, const std::string &valueText, std::string &err)
{
    if (!nd::isRegisterName(name)) {
        err = "no register named " + name;
        return false;
    }
    uint64_t v = 0;
    if (!expr::eval(valueText, context(), v, err))
        return false;
    nd::Regs r;
    if (!eng_.getRegs(r)) {
        err = "cannot read the registers";
        return false;
    }
    nd::setRegister(r, name, v);
    if (!eng_.setRegs(r)) {
        err = "cannot write the registers";
        return false;
    }
    return true;
}

bool Debuggee::evalAddress(const std::string &text, uint64_t &out, std::string &err)
{
    return expr::evalAddress(text, context(), out, err);
}

std::vector<Instruction> Debuggee::disassemble(uint64_t start, int byteCount)
{
    std::vector<Instruction> result;
    if (byteCount <= 0)
        return result;
    std::vector<uint8_t> mem(size_t(byteCount) + 16);
    size_t have = readCode(start, mem.data(), mem.size());
    if (!have)
        return result;
    const dis::Symbolizer sym = [this](uint64_t a) { return symbolize(a); };
    size_t off = 0;
    while (off < size_t(byteCount) && off < have) {
        dis::Insn in;
        dis::decode(mem.data() + off, have - off, start + off, true, in, &sym);
        Instruction i;
        i.address = in.address;
        const size_t len = std::max(1, in.length);
        i.bytes.assign(mem.begin() + long(off), mem.begin() + long(std::min(have, off + len)));
        i.text = in.text;
        if (!in.comment.empty())
            i.text += "  ; " + in.comment;
        const std::string s = symbolize(in.address);
        if (!s.empty())
            i.symbol = "<" + s + ">";
        result.push_back(std::move(i));
        off += len;
    }
    return result;
}

std::vector<Debuggee::Frame> Debuggee::callStack()
{
    std::vector<Frame> frames;
    nd::Regs r;
    if (!eng_.getRegs(r))
        return frames;
    frames.push_back({ r.rip, lineContaining(r.rip) });
    for (int i = 0; i < kStackScanSlots && frames.size() < 32; ++i) {
        uint64_t v = 0;
        if (!eng_.read(r.rsp + uint64_t(i) * 8, &v, 8))
            break;
        if (isReturnAddress(v))
            frames.push_back({ v, lineContaining(v - 1) });
    }
    return frames;
}

} // namespace wdbg

#endif // _WIN32
