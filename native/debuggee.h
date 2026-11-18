/****************************************************************************
** Asmon — GPL-3.0.
**
** Debuggee: a program being debugged natively. Builds on Engine and adds what
** a source-level debugger of assembly needs: the map from addresses to source
** lines, stepping by line ("step over" runs a whole call, a macro body runs at
** full speed), step into / out, breakpoints with conditions, memory write stops,
** expressions, disassembly with symbol names and a call stack read from the
** stack. Blocking and single-threaded, like Engine. No Qt.
****************************************************************************/
#ifndef ASMON_DEBUGGEE_H
#define ASMON_DEBUGGEE_H

#if defined(_WIN32) || defined(ASMON_NATIVE_CHECK)

#include "disasm.h"
#include "engine.h"
#include "expr.h"
#include "pe.h"

#include <map>
#include <set>
#include <string>
#include <vector>

namespace wdbg {

struct Stop {
    enum Kind { Stopped, Crashed, Finished, MemoryWritten } kind = Stopped;
    uint64_t pc = 0;
    int exitCode = 0;
    std::string exceptionName, meaning;    //!< Crashed
    std::string watchName, oldValue, newValue;   //!< MemoryWritten
    std::string note;                       //!< something worth telling the user (condition errors...)
};

struct Instruction {
    uint64_t address = 0;
    std::vector<uint8_t> bytes;
    std::string text;      //!< Intel syntax, with "; comment" when there is one
    std::string symbol;    //!< "<main+4>" or ""
};

class Debuggee {
public:
    bool launch(const std::wstring &exe, const std::wstring &cwd, const std::wstring &stdinFile,
                Engine::OutputFn out, std::string &err);
    bool attach(uint32_t pid, const std::wstring &imagePath, std::string &err);

    Engine &engine() { return eng_; }
    const pe::Info &image() const { return pe_; }
    bool alive() const { return eng_.alive(); }
    uint64_t imageBase() const { return eng_.imageBase(); }

    //! runtime address of a symbol of the program (0: not found)
    uint64_t symbolAddress(const std::string &name) const;
    //! "main+0x4" / "msg" for an address in the program image; "" elsewhere
    std::string symbolize(uint64_t address) const;

    //! Source lines: first instruction address of each line -> line id. Call once the runtime
    //! address of main is known (after the first stop).
    void setLines(const std::map<uint64_t, int> &lineOfAddress);
    int lineAt(uint64_t address) const;                //!< exact start of a line, else 0
    int lineContaining(uint64_t address) const;        //!< the nearest line at or before address, else 0
    bool inUserCode(uint64_t address) const { return address >= userLow_ && address < userHigh_; }

    //! the set of breakpoint addresses; a condition ("rcx == 5") makes one conditional
    void setBreakpoints(const std::set<uint64_t> &addresses) { userBps_ = addresses; }
    void setCondition(uint64_t address, const std::string &condition);
    void setConditions(const std::map<uint64_t, std::string> &c) { conditions_ = c; }

    Stop cont();
    //! runs until address is reached (or something else stops the program first)
    Stop runTo(uint64_t address);
    Stop stepOver();
    Stop stepInto();
    Stop stepOut();
    Stop stepInstruction();
    void pause() { eng_.requestPause(); }
    void kill() { eng_.kill(); }          //!< any thread; the debugging thread then sees Finished
    void terminate() { eng_.terminate(); }
    void detach() { eng_.detach(); }

    // memory write stops (hardware); text is a label / register / [rbp-8] / number
    bool watchMemory(const std::string &text, int size, const std::string &shownName, std::string &message);
    void clearWatches();

    // data
    bool regs(nd::Regs &r) { return eng_.getRegs(r); }
    bool setRegister(const std::string &name, const std::string &valueText, std::string &err);
    bool evalAddress(const std::string &text, uint64_t &out, std::string &err);
    std::vector<Instruction> disassemble(uint64_t start, int byteCount);
    struct Frame { uint64_t address; int line; };
    std::vector<Frame> callStack();

private:
    expr::Context context();
    Stop makeStop(const Event &e);
    Stop stopHere();
    //! temps: address -> smallest rsp for which reaching it counts (0: any)
    Stop runUntilStop(const std::map<uint64_t, uint64_t> &temps);
    Stop stepLine(bool into);
    bool stepOnce(Stop &out);
    bool watchTriggered(const Event &e, Stop &out);
    bool conditionHolds(uint64_t pc, std::string &note);
    size_t readCode(uint64_t address, uint8_t *buf, size_t size);
    bool isReturnAddress(uint64_t v) const;

    Engine eng_;
    pe::Info pe_;
    std::map<uint64_t, int> lineOfAddress_;
    uint64_t userLow_ = 0, userHigh_ = 0;
    std::vector<uint8_t> userCode_;
    std::set<uint64_t> userBps_;
    std::map<uint64_t, std::string> conditions_;
    struct Watch { bool used = false; uint64_t address = 0; int size = 0; std::string name; std::vector<uint8_t> last; };
    Watch watches_[4];
};

} // namespace wdbg

#endif // _WIN32
#endif // ASMON_DEBUGGEE_H
