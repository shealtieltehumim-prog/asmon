/****************************************************************************
** Asmon — GPL-3.0.
**
** Engine: Windows Debug API (CreateProcess with DEBUG_ONLY_THIS_PROCESS,
** WaitForDebugEvent, int3 breakpoints, trap-flag single steps, hardware write
** watchpoints). x86-64 programs only. No Qt.
**
** Blocking and single-threaded: every method except requestPause() must be
** called from the thread that called launch()/attach() (the Debug API ties a
** debugged process to its thread). While the program runs, step()/cont() block
** until it stops; requestPause() may be called from any thread to stop it.
**
** The program's breakpoints are only in memory while it runs: when it is
** stopped, read() shows the original bytes.
****************************************************************************/
#ifndef ASMON_ENGINE_H
#define ASMON_ENGINE_H

#if defined(_WIN32) || defined(ASMON_NATIVE_CHECK)

#include "regs.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <thread>

namespace wdbg {

enum class StopKind {
    Breakpoint, //!< an int3 of setBreakpoints() was reached; pc is its address
    Step,       //!< one instruction was executed
    Exception,  //!< an exception (crash or int3 in the program); see Event
    Paused,     //!< requestPause()
    Watch,      //!< a hardware watchpoint fired; pc is after the writing instruction
    Exited      //!< the process ended (exitCode)
};

struct Event {
    StopKind kind = StopKind::Step;
    uint32_t threadId = 0;
    uint64_t pc = 0;
    uint32_t exceptionCode = 0;
    bool firstChance = true;
    uint64_t faultAddress = 0;    //!< access violation: the address
    int faultKind = -1;           //!< access violation: 0 read, 1 write, 8 execute
    int exitCode = 0;
    int watchSlot = -1;
};

//! "EXCEPTION_ACCESS_VIOLATION" and the like; code as hex for unknown ones
std::string exceptionName(uint32_t code);
//! "Access violation: the program tried to read address 0x0"
std::string exceptionMeaning(const Event &e);

class Engine {
public:
    using OutputFn = std::function<void(const std::string &)>;
    Engine();
    ~Engine();
    Engine(const Engine &) = delete;
    Engine &operator=(const Engine &) = delete;

    //! Starts the program under the debugger and returns when the loader has finished
    //! its initial breakpoint (the program is stopped before its entry point).
    //! Its stdout/stderr go to out (from another thread), stdin comes from stdinFile ("" = empty).
    bool launch(const std::wstring &exe, const std::wstring &cwd, const std::wstring &stdinFile,
                OutputFn out, std::string &err);
    //! Attaches to a running 64-bit process; returns when it is stopped.
    bool attach(uint32_t pid, std::string &err);

    bool alive() const { return alive_; }
    bool attached() const { return attached_; }
    uint32_t pid() const { return pid_; }
    uint64_t imageBase() const { return imageBase_; }
    uint32_t threadId() const { return curTid_; }

    bool read(uint64_t address, void *buffer, size_t size);
    bool write(uint64_t address, const void *buffer, size_t size);
    bool getRegs(nd::Regs &regs);
    bool setRegs(const nd::Regs &regs);

    //! The full set of int3 addresses to insert while the program runs.
    void setBreakpoints(const std::set<uint64_t> &addresses) { breakpoints_ = addresses; }
    //! Hardware write watchpoint in slot 0..3 (size 1, 2, 4 or 8; the address must be aligned).
    bool setWatch(int slot, uint64_t address, int size, std::string &err);
    void clearWatches();

    //! Executes one instruction.
    Event step();
    //! Runs until a breakpoint, watchpoint, exception, pause or exit. A breakpoint at the
    //! current pc is stepped over first. After an exception the program gets it back (crash).
    Event cont();
    //! Stops a running program (any thread).
    void requestPause();
    //! Ends the process from any thread (the thread that debugs it sees the exit event).
    void kill();

    void terminate();   //!< ends the program (and the session)
    void detach();      //!< lets an attached program run on

private:
    struct Impl;
    Event pump(bool expectStep);
    void continueEvent();
    void insertBreakpoints();
    void removeBreakpoints();
    bool setTrapFlag(bool on);
    void applyWatches(void *threadHandle);
    void closeAll();
    Impl *d;
    std::atomic<bool> alive_{ false };
    bool attached_ = false;
    uint32_t pid_ = 0, curTid_ = 0;
    uint64_t imageBase_ = 0;
    std::set<uint64_t> breakpoints_;
};

} // namespace wdbg

#endif // _WIN32
#endif // ASMON_ENGINE_H
