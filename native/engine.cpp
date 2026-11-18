#ifdef _WIN32

#include "engine.h"

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <vector>

namespace wdbg {

#ifndef STATUS_WX86_BREAKPOINT
#define STATUS_WX86_BREAKPOINT 0x4000001FL
#endif
#ifndef STATUS_WX86_SINGLE_STEP
#define STATUS_WX86_SINGLE_STEP 0x4000001EL
#endif

struct Engine::Impl {
    HANDLE hProcess = nullptr;
    std::map<DWORD, HANDLE> threads;      //!< our own duplicated handles
    DWORD mainTid = 0;
    bool haveEvent = false;               //!< stopped inside a debug event not yet continued
    DWORD evPid = 0, evTid = 0, contStatus = DBG_CONTINUE;
    bool wantInitialBreak = false;
    bool initialBreakSeen = false;
    std::atomic<bool> pausePending{ false };
    int exitCode = 0;
    bool crashReported = false;           //!< a first-chance crash was shown: its second chance just ends the program
    HANDLE outRead = nullptr;
    std::thread reader;
    struct Watch { bool active = false; uint64_t addr = 0; int size = 0; } watches[4];
    std::map<uint64_t, uint8_t> inserted; //!< address -> original byte, while the program runs
};

namespace {

HANDLE duplicate(HANDLE h)
{
    HANDLE out = nullptr;
    if (!h || !DuplicateHandle(GetCurrentProcess(), h, GetCurrentProcess(), &out, 0, FALSE, DUPLICATE_SAME_ACCESS))
        return nullptr;
    return out;
}

constexpr DWORD kCtxFlags = CONTEXT_CONTROL | CONTEXT_INTEGER | CONTEXT_SEGMENTS;

bool fatalFirstChance(DWORD code)
{
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
    case EXCEPTION_ILLEGAL_INSTRUCTION:
    case EXCEPTION_PRIV_INSTRUCTION:
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
    case EXCEPTION_INT_OVERFLOW:
    case EXCEPTION_STACK_OVERFLOW:
    case EXCEPTION_IN_PAGE_ERROR:
    case EXCEPTION_DATATYPE_MISALIGNMENT:
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:
    case EXCEPTION_FLT_INVALID_OPERATION:
    case EXCEPTION_NONCONTINUABLE_EXCEPTION:
        return true;
    default:
        return false;
    }
}

} // namespace

std::string exceptionName(uint32_t code)
{
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION: return "EXCEPTION_ACCESS_VIOLATION";
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "EXCEPTION_ARRAY_BOUNDS_EXCEEDED";
    case EXCEPTION_BREAKPOINT: return "EXCEPTION_BREAKPOINT";
    case EXCEPTION_DATATYPE_MISALIGNMENT: return "EXCEPTION_DATATYPE_MISALIGNMENT";
    case EXCEPTION_FLT_DIVIDE_BY_ZERO: return "EXCEPTION_FLT_DIVIDE_BY_ZERO";
    case EXCEPTION_FLT_INVALID_OPERATION: return "EXCEPTION_FLT_INVALID_OPERATION";
    case EXCEPTION_ILLEGAL_INSTRUCTION: return "EXCEPTION_ILLEGAL_INSTRUCTION";
    case EXCEPTION_IN_PAGE_ERROR: return "EXCEPTION_IN_PAGE_ERROR";
    case EXCEPTION_INT_DIVIDE_BY_ZERO: return "EXCEPTION_INT_DIVIDE_BY_ZERO";
    case EXCEPTION_INT_OVERFLOW: return "EXCEPTION_INT_OVERFLOW";
    case EXCEPTION_NONCONTINUABLE_EXCEPTION: return "EXCEPTION_NONCONTINUABLE_EXCEPTION";
    case EXCEPTION_PRIV_INSTRUCTION: return "EXCEPTION_PRIV_INSTRUCTION";
    case EXCEPTION_STACK_OVERFLOW: return "EXCEPTION_STACK_OVERFLOW";
    default: {
        char b[32];
        std::snprintf(b, sizeof b, "exception 0x%08X", code);
        return b;
    }
    }
}

std::string exceptionMeaning(const Event &e)
{
    char b[160];
    switch (e.exceptionCode) {
    case EXCEPTION_ACCESS_VIOLATION: {
        const char *what = e.faultKind == 0 ? "read" : e.faultKind == 1 ? "write to" : e.faultKind == 8 ? "run code at" : "access";
        std::snprintf(b, sizeof b, "access violation: the program tried to %s address 0x%llx", what,
                      (unsigned long long)e.faultAddress);
        return b;
    }
    case EXCEPTION_INT_DIVIDE_BY_ZERO: return "integer division by zero (or the quotient does not fit)";
    case EXCEPTION_ILLEGAL_INSTRUCTION: return "illegal instruction";
    case EXCEPTION_PRIV_INSTRUCTION: return "privileged instruction";
    case EXCEPTION_STACK_OVERFLOW: return "stack overflow";
    case EXCEPTION_BREAKPOINT: return "int3 instruction in the program";
    case EXCEPTION_DATATYPE_MISALIGNMENT: return "misaligned data access";
    case EXCEPTION_INT_OVERFLOW: return "integer overflow";
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "array bounds exceeded";
    case EXCEPTION_IN_PAGE_ERROR: return "the page could not be read in";
    default: return exceptionName(e.exceptionCode);
    }
}

Engine::Engine() : d(new Impl) {}

Engine::~Engine()
{
    if (alive_)
        terminate();
    closeAll();
    delete d;
}

void Engine::closeAll()
{
    for (auto &t : d->threads)
        CloseHandle(t.second);
    d->threads.clear();
    if (d->hProcess) {
        CloseHandle(d->hProcess);
        d->hProcess = nullptr;
    }
    if (d->reader.joinable())
        d->reader.join();
    if (d->outRead) {
        CloseHandle(d->outRead);
        d->outRead = nullptr;
    }
}

// ----------------------------------------------------------------- start / attach

bool Engine::launch(const std::wstring &exe, const std::wstring &cwd, const std::wstring &stdinFile,
                    OutputFn out, std::string &err)
{
    SECURITY_ATTRIBUTES sa{ sizeof sa, nullptr, TRUE };
    HANDLE outR = nullptr, outW = nullptr;
    if (!CreatePipe(&outR, &outW, &sa, 0)) {
        err = "CreatePipe failed";
        return false;
    }
    SetHandleInformation(outR, HANDLE_FLAG_INHERIT, 0);
    HANDLE in = CreateFileW(stdinFile.empty() ? L"NUL" : stdinFile.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                            &sa, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (in == INVALID_HANDLE_VALUE)
        in = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);

    // only these two handles are passed to the program
    HANDLE inherit[2] = { in, outW };
    SIZE_T attrSize = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attrSize);
    std::vector<char> attrBuf(attrSize);
    auto *attrs = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attrBuf.data());
    InitializeProcThreadAttributeList(attrs, 1, 0, &attrSize);
    UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherit, sizeof inherit, nullptr, nullptr);

    STARTUPINFOEXW si{};
    si.StartupInfo.cb = sizeof si;
    si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    si.StartupInfo.hStdInput = in;
    si.StartupInfo.hStdOutput = outW;
    si.StartupInfo.hStdError = outW;
    si.lpAttributeList = attrs;
    std::wstring cmd = L"\"" + exe + L"\"";
    PROCESS_INFORMATION pi{};
    const BOOL ok = CreateProcessW(exe.c_str(), &cmd[0], nullptr, nullptr, TRUE,
                                   DEBUG_ONLY_THIS_PROCESS | CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT,
                                   nullptr, cwd.empty() ? nullptr : cwd.c_str(), &si.StartupInfo, &pi);
    const DWORD createError = GetLastError();
    DeleteProcThreadAttributeList(attrs);
    CloseHandle(outW);
    if (in != INVALID_HANDLE_VALUE)
        CloseHandle(in);
    if (!ok) {
        CloseHandle(outR);
        char b[96];
        std::snprintf(b, sizeof b, "CreateProcess failed (error %lu)", (unsigned long)createError);
        err = b;
        return false;
    }
    BOOL wow = FALSE;
    IsWow64Process(pi.hProcess, &wow);
    if (wow) {
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        CloseHandle(outR);
        err = "this is a 32-bit program; the native debugger supports 64-bit programs";
        return false;
    }
    d->hProcess = pi.hProcess;
    CloseHandle(pi.hThread);
    pid_ = pi.dwProcessId;
    d->outRead = outR;
    if (out)
        d->reader = std::thread([outR, out]() {
            char buf[4096];
            DWORD n = 0;
            while (ReadFile(outR, buf, sizeof buf, &n, nullptr) && n)
                out(std::string(buf, n));
        });
    alive_ = true;
    d->wantInitialBreak = true;
    const Event e = pump(false);
    if (e.kind != StopKind::Paused) {
        err = "the program ended before it started";
        return false;
    }
    return true;
}

bool Engine::attach(uint32_t pid, std::string &err)
{
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) {
        err = "cannot open the process (access denied?)";
        return false;
    }
    BOOL wow = FALSE;
    IsWow64Process(h, &wow);
    CloseHandle(h);
    if (wow) {
        err = "this is a 32-bit program; the native debugger supports 64-bit programs";
        return false;
    }
    DebugSetProcessKillOnExit(FALSE);
    if (!DebugActiveProcess(pid)) {
        char b[96];
        std::snprintf(b, sizeof b, "DebugActiveProcess failed (error %lu)", (unsigned long)GetLastError());
        err = b;
        return false;
    }
    pid_ = pid;
    attached_ = true;
    alive_ = true;
    d->wantInitialBreak = true;
    const Event e = pump(false);
    if (e.kind != StopKind::Paused) {
        err = "the process ended while attaching";
        return false;
    }
    return true;
}

// ----------------------------------------------------------------- state access

bool Engine::read(uint64_t address, void *buffer, size_t size)
{
    SIZE_T got = 0;
    return d->hProcess && ReadProcessMemory(d->hProcess, reinterpret_cast<LPCVOID>(address), buffer, size, &got)
           && got == size;
}

bool Engine::write(uint64_t address, const void *buffer, size_t size)
{
    SIZE_T put = 0;
    if (!d->hProcess || !WriteProcessMemory(d->hProcess, reinterpret_cast<LPVOID>(address), buffer, size, &put)
            || put != size)
        return false;
    FlushInstructionCache(d->hProcess, reinterpret_cast<LPCVOID>(address), size);
    return true;
}

bool Engine::getRegs(nd::Regs &r)
{
    auto it = d->threads.find(curTid_);
    if (it == d->threads.end())
        return false;
    alignas(16) CONTEXT c;
    std::memset(&c, 0, sizeof c);
    c.ContextFlags = kCtxFlags;
    if (!GetThreadContext(it->second, &c))
        return false;
    r.rax = c.Rax; r.rbx = c.Rbx; r.rcx = c.Rcx; r.rdx = c.Rdx; r.rsi = c.Rsi; r.rdi = c.Rdi;
    r.rbp = c.Rbp; r.rsp = c.Rsp;
    r.r8 = c.R8; r.r9 = c.R9; r.r10 = c.R10; r.r11 = c.R11; r.r12 = c.R12; r.r13 = c.R13; r.r14 = c.R14; r.r15 = c.R15;
    r.rip = c.Rip;
    r.eflags = c.EFlags;
    r.cs = c.SegCs; r.ss = c.SegSs; r.ds = c.SegDs; r.es = c.SegEs; r.fs = c.SegFs; r.gs = c.SegGs;
    return true;
}

bool Engine::setRegs(const nd::Regs &r)
{
    auto it = d->threads.find(curTid_);
    if (it == d->threads.end())
        return false;
    alignas(16) CONTEXT c;
    std::memset(&c, 0, sizeof c);
    c.ContextFlags = kCtxFlags;
    if (!GetThreadContext(it->second, &c))
        return false;
    c.Rax = r.rax; c.Rbx = r.rbx; c.Rcx = r.rcx; c.Rdx = r.rdx; c.Rsi = r.rsi; c.Rdi = r.rdi;
    c.Rbp = r.rbp; c.Rsp = r.rsp;
    c.R8 = r.r8; c.R9 = r.r9; c.R10 = r.r10; c.R11 = r.r11; c.R12 = r.r12; c.R13 = r.r13; c.R14 = r.r14; c.R15 = r.r15;
    c.Rip = r.rip;
    c.EFlags = DWORD(r.eflags);
    c.SegCs = r.cs; c.SegSs = r.ss; c.SegDs = r.ds; c.SegEs = r.es; c.SegFs = r.fs; c.SegGs = r.gs;
    return SetThreadContext(it->second, &c) != 0;
}

bool Engine::setTrapFlag(bool on)
{
    auto it = d->threads.find(curTid_);
    if (it == d->threads.end())
        return false;
    alignas(16) CONTEXT c;
    std::memset(&c, 0, sizeof c);
    c.ContextFlags = CONTEXT_CONTROL;
    if (!GetThreadContext(it->second, &c))
        return false;
    const bool has = (c.EFlags & 0x100) != 0;
    if (has == on)
        return true;
    c.EFlags = on ? (c.EFlags | 0x100) : (c.EFlags & ~DWORD(0x100));
    return SetThreadContext(it->second, &c) != 0;
}

// ----------------------------------------------------------------- watchpoints

void Engine::applyWatches(void *threadHandle)
{
    HANDLE h = static_cast<HANDLE>(threadHandle);
    alignas(16) CONTEXT c;
    std::memset(&c, 0, sizeof c);
    c.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    if (!GetThreadContext(h, &c))
        return;
    DWORD64 dr7 = 0;
    DWORD64 *dr[4] = { &c.Dr0, &c.Dr1, &c.Dr2, &c.Dr3 };
    for (int i = 0; i < 4; ++i) {
        const Impl::Watch &w = d->watches[i];
        if (!w.active) {
            *dr[i] = 0;
            continue;
        }
        *dr[i] = w.addr;
        const DWORD64 len = w.size == 1 ? 0 : w.size == 2 ? 1 : w.size == 4 ? 3 : 2;
        dr7 |= (DWORD64(1) << (i * 2)) | (DWORD64(1) << (16 + i * 4)) | (len << (18 + i * 4));
    }
    c.Dr7 = dr7;
    c.Dr6 = 0;
    SetThreadContext(h, &c);
}

bool Engine::setWatch(int slot, uint64_t address, int size, std::string &err)
{
    if (slot < 0 || slot > 3) {
        err = "only 4 memory stops are possible at once";
        return false;
    }
    if (size != 1 && size != 2 && size != 4 && size != 8) {
        err = "the size must be 1, 2, 4 or 8 bytes";
        return false;
    }
    if (address % uint64_t(size) != 0) {
        err = "the address must be a multiple of the size";
        return false;
    }
    d->watches[slot] = { true, address, size };
    for (auto &t : d->threads)
        applyWatches(t.second);
    return true;
}

void Engine::clearWatches()
{
    for (auto &w : d->watches)
        w.active = false;
    for (auto &t : d->threads)
        applyWatches(t.second);
}

// ----------------------------------------------------------------- running

void Engine::insertBreakpoints()
{
    for (uint64_t a : breakpoints_) {
        if (d->inserted.count(a))
            continue;
        uint8_t orig = 0;
        const uint8_t int3 = 0xCC;
        SIZE_T n = 0;
        if (ReadProcessMemory(d->hProcess, reinterpret_cast<LPCVOID>(a), &orig, 1, &n) && n == 1
                && WriteProcessMemory(d->hProcess, reinterpret_cast<LPVOID>(a), &int3, 1, &n) && n == 1) {
            d->inserted[a] = orig;
            FlushInstructionCache(d->hProcess, reinterpret_cast<LPCVOID>(a), 1);
        }
    }
}

void Engine::removeBreakpoints()
{
    for (auto &b : d->inserted) {
        SIZE_T n = 0;
        WriteProcessMemory(d->hProcess, reinterpret_cast<LPVOID>(b.first), &b.second, 1, &n);
        FlushInstructionCache(d->hProcess, reinterpret_cast<LPCVOID>(b.first), 1);
    }
    d->inserted.clear();
}

void Engine::continueEvent()
{
    if (d->haveEvent) {
        ContinueDebugEvent(d->evPid, d->evTid, d->contStatus);
        d->haveEvent = false;
    }
}

Event Engine::pump(bool expectStep)
{
    for (;;) {
        DEBUG_EVENT de;
        if (!WaitForDebugEvent(&de, INFINITE)) {
            alive_ = false;
            Event e;
            e.kind = StopKind::Exited;
            e.exitCode = -1;
            return e;
        }
        d->evPid = de.dwProcessId;
        d->evTid = de.dwThreadId;
        d->contStatus = DBG_CONTINUE;
        d->haveEvent = true;
        switch (de.dwDebugEventCode) {
        case CREATE_PROCESS_DEBUG_EVENT: {
            const CREATE_PROCESS_DEBUG_INFO &i = de.u.CreateProcessInfo;
            if (i.hFile)
                CloseHandle(i.hFile);
            imageBase_ = reinterpret_cast<uint64_t>(i.lpBaseOfImage);
            if (!d->hProcess)
                d->hProcess = duplicate(i.hProcess);
            d->mainTid = de.dwThreadId;
            d->threads[de.dwThreadId] = duplicate(i.hThread);
            curTid_ = de.dwThreadId;
            break;
        }
        case CREATE_THREAD_DEBUG_EVENT: {
            HANDLE h = duplicate(de.u.CreateThread.hThread);
            d->threads[de.dwThreadId] = h;
            applyWatches(h);
            break;
        }
        case EXIT_THREAD_DEBUG_EVENT: {
            auto it = d->threads.find(de.dwThreadId);
            if (it != d->threads.end() && de.dwThreadId != d->mainTid) {
                CloseHandle(it->second);
                d->threads.erase(it);
            }
            break;
        }
        case EXIT_PROCESS_DEBUG_EVENT: {
            alive_ = false;
            d->inserted.clear();
            continueEvent();
            Event e;
            e.kind = StopKind::Exited;
            e.exitCode = int(de.u.ExitProcess.dwExitCode);
            e.threadId = de.dwThreadId;
            return e;
        }
        case LOAD_DLL_DEBUG_EVENT:
            if (de.u.LoadDll.hFile)
                CloseHandle(de.u.LoadDll.hFile);
            break;
        case EXCEPTION_DEBUG_EVENT: {
            const EXCEPTION_RECORD &er = de.u.Exception.ExceptionRecord;
            const DWORD code = er.ExceptionCode;
            const bool first = de.u.Exception.dwFirstChance != 0;
            const uint64_t where = reinterpret_cast<uint64_t>(er.ExceptionAddress);
            const uint32_t savedCur = curTid_;
            curTid_ = de.dwThreadId;
            Event e;
            e.threadId = de.dwThreadId;
            e.exceptionCode = code;
            e.firstChance = first;
            e.pc = where;
            if (code == EXCEPTION_BREAKPOINT) {
                if (!d->initialBreakSeen) {
                    d->initialBreakSeen = true;
                    if (d->wantInitialBreak) {
                        d->wantInitialBreak = false;
                        e.kind = StopKind::Paused;
                        curTid_ = attached_ ? d->mainTid : de.dwThreadId;
                        return e;
                    }
                    curTid_ = savedCur;
                    break;
                }
                if (d->inserted.count(where)) {
                    nd::Regs r;
                    if (getRegs(r)) {
                        r.rip = where;           // the int3 was executed: back to its address
                        setRegs(r);
                    }
                    e.kind = StopKind::Breakpoint;
                    return e;
                }
                if (d->pausePending.exchange(false)) {
                    e.kind = StopKind::Paused;
                    curTid_ = d->threads.count(d->mainTid) ? d->mainTid : de.dwThreadId;
                    nd::Regs r;
                    if (getRegs(r))
                        e.pc = r.rip;
                    return e;
                }
                // an int3 written in the program itself
                nd::Regs r;
                if (getRegs(r))
                    e.pc = r.rip;
                e.kind = StopKind::Exception;
                return e;
            }
            if (code == EXCEPTION_SINGLE_STEP || code == STATUS_WX86_SINGLE_STEP) {
                // hardware watchpoint?
                alignas(16) CONTEXT c;
                std::memset(&c, 0, sizeof c);
                c.ContextFlags = CONTEXT_DEBUG_REGISTERS;
                int slot = -1;
                auto it = d->threads.find(de.dwThreadId);
                if (it != d->threads.end() && GetThreadContext(it->second, &c)) {
                    for (int i = 0; i < 4; ++i)
                        if ((c.Dr6 & (DWORD64(1) << i)) && d->watches[i].active) { slot = i; break; }
                    if (c.Dr6 & 0xF) {
                        c.Dr6 = 0;
                        SetThreadContext(it->second, &c);
                    }
                }
                setTrapFlag(false);
                nd::Regs r;
                if (getRegs(r))
                    e.pc = r.rip;
                if (slot >= 0) {
                    e.kind = StopKind::Watch;
                    e.watchSlot = slot;
                    return e;
                }
                if (expectStep) {
                    e.kind = StopKind::Step;
                    return e;
                }
                curTid_ = savedCur;       // a stray trap: ignore it
                break;
            }
            if (!first && d->crashReported) {
                // the program was handed its crash after we showed it: let it end
                d->contStatus = DBG_EXCEPTION_NOT_HANDLED;
                curTid_ = savedCur;
                break;
            }
            if (!first || fatalFirstChance(code)) {
                d->crashReported = true;
                e.kind = StopKind::Exception;
                if (code == EXCEPTION_ACCESS_VIOLATION && er.NumberParameters >= 2) {
                    e.faultKind = int(er.ExceptionInformation[0]);
                    e.faultAddress = er.ExceptionInformation[1];
                }
                d->contStatus = DBG_EXCEPTION_NOT_HANDLED;   // a continue after it ends the program
                return e;
            }
            // other first-chance exceptions (C++ throw, thread names...): the program handles them
            d->contStatus = DBG_EXCEPTION_NOT_HANDLED;
            curTid_ = savedCur;
            break;
        }
        default:
            break;
        }
        continueEvent();
    }
}

Event Engine::step()
{
    if (!alive_) {
        Event e;
        e.kind = StopKind::Exited;
        return e;
    }
    setTrapFlag(true);
    continueEvent();
    return pump(true);
}

Event Engine::cont()
{
    if (!alive_) {
        Event e;
        e.kind = StopKind::Exited;
        return e;
    }
    nd::Regs r;
    if (getRegs(r) && breakpoints_.count(r.rip)) {
        // leave the breakpoint we are standing on
        Event e = step();
        if (e.kind != StopKind::Step)
            return e;
        if (breakpoints_.count(e.pc)) {
            e.kind = StopKind::Breakpoint;
            return e;
        }
    }
    insertBreakpoints();
    continueEvent();
    Event e = pump(false);
    if (alive_)
        removeBreakpoints();
    return e;
}

void Engine::requestPause()
{
    if (!alive_ || !d->hProcess)
        return;
    d->pausePending = true;
    DebugBreakProcess(d->hProcess);
}

void Engine::kill()
{
    HANDLE h = d->hProcess;
    if (alive_ && h && !attached_)
        TerminateProcess(h, 1);
}

void Engine::terminate()
{
    if (!alive_) {
        closeAll();
        return;
    }
    if (attached_) {
        // never kill a program we only looked at
        detach();
        closeAll();
        return;
    }
    TerminateProcess(d->hProcess, 1);
    continueEvent();
    for (int i = 0; i < 40 && alive_; ++i) {
        DEBUG_EVENT de;
        if (!WaitForDebugEvent(&de, 250))
            continue;
        if (de.dwDebugEventCode == EXIT_PROCESS_DEBUG_EVENT)
            alive_ = false;
        if (de.dwDebugEventCode == LOAD_DLL_DEBUG_EVENT && de.u.LoadDll.hFile)
            CloseHandle(de.u.LoadDll.hFile);
        if (de.dwDebugEventCode == CREATE_PROCESS_DEBUG_EVENT && de.u.CreateProcessInfo.hFile)
            CloseHandle(de.u.CreateProcessInfo.hFile);
        ContinueDebugEvent(de.dwProcessId, de.dwThreadId, DBG_CONTINUE);
    }
    alive_ = false;
    closeAll();
}

void Engine::detach()
{
    if (!alive_)
        return;
    removeBreakpoints();
    clearWatches();
    setTrapFlag(false);
    continueEvent();
    DebugActiveProcessStop(pid_);
    alive_ = false;
}

} // namespace wdbg

#endif // _WIN32
