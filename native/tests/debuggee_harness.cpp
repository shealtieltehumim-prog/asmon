// Test of the native debug engine on real programs (Windows only; run by CI).
// Usage: debuggee_harness <dir with engine_test.exe/.lst, crash_test.exe, loop_test.exe>
#include "../debuggee.h"

#include <windows.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>

using namespace wdbg;

static int failures = 0, checks = 0;
#define CHECK(c) do { ++checks; if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
#define CHECK_EQ(a, b) do { ++checks; const auto va = (a); const auto vb = (b); if (!(va == vb)) { \
    std::printf("FAIL line %d: %s == %s (%llx vs %llx)\n", __LINE__, #a, #b, (unsigned long long)va, (unsigned long long)vb); ++failures; } } while (0)

static std::wstring widen(const std::string &s) { return std::wstring(s.begin(), s.end()); }

struct Output {
    std::mutex m;
    std::string text;
    Engine::OutputFn fn() { return [this](const std::string &s) { std::lock_guard<std::mutex> l(m); text += s; }; }
    std::string get() { std::lock_guard<std::mutex> l(m); return text; }
};

// source line -> address from a NASM listing (the .text lines only), relative to main's first instruction
static std::map<uint64_t, int> readListing(const std::string &path, uint64_t mainAddress)
{
    std::map<uint64_t, int> lines;
    std::ifstream f(path);
    std::string l;
    bool text = false;
    int previousNumber = -1;
    while (std::getline(f, l)) {
        int number = 0;
        unsigned address = 0;
        char bytes[64] = { 0 };
        if (l.find("section .text") != std::string::npos) { text = true; continue; }
        if (l.find("section .data") != std::string::npos) { text = false; continue; }
        if (!text || std::sscanf(l.c_str(), "%d %x %63s", &number, &address, bytes) < 3 || number == previousNumber)
            continue;
        previousNumber = number;
        lines[mainAddress + address] = number;
    }
    return lines;
}

static void testProgram(const std::string &dir)
{
    std::puts("== engine_test: stepping, lines, breakpoints, watch, conditions");
    Output out;
    Debuggee d;
    std::string err;
    CHECK(d.launch(widen(dir + "\\engine_test.exe"), L"", L"", out.fn(), err));
    if (!err.empty())
        std::printf("launch error: %s\n", err.c_str());
    CHECK(d.alive());
    CHECK(d.imageBase() != 0);
    const uint64_t main = d.symbolAddress("main"), helper = d.symbolAddress("helper");
    CHECK(main != 0 && helper == main + 0x4F);
    Stop s = d.runTo(main);
    CHECK(s.kind == Stop::Stopped);
    CHECK_EQ(s.pc, main);
    d.setLines(readListing(dir + "\\engine_test.lst", main));
    CHECK_EQ(d.lineAt(main), 14);
    CHECK(d.inUserCode(main + 0x20));
    CHECK(!d.inUserCode(main + 0x1000));

    // single instructions: push, mov, sub, mov, mov, add
    for (int i = 0; i < 6; ++i)
        s = d.stepInstruction();
    nd::Regs r;
    CHECK(d.regs(r));
    CHECK_EQ(r.rax, 12u);
    CHECK_EQ(r.rbx, 7u);
    CHECK_EQ(s.pc, main + 0x15);
    // pushfq must not leave the trap flag on the stack
    s = d.stepInstruction();
    CHECK_EQ(s.pc, main + 0x16);
    d.regs(r);
    uint64_t flags = 0;
    CHECK(d.engine().read(r.rsp, &flags, 8));
    CHECK((flags & 0x100) == 0);
    s = d.stepInstruction();   // popfq
    d.regs(r);
    CHECK((r.eflags & 0x100) == 0);
    CHECK_EQ(s.pc, main + 0x17);

    // disassembly
    std::vector<Instruction> ins = d.disassemble(main, 0x30);
    CHECK(ins.size() > 8);
    if (ins.size() > 8) {
        CHECK(ins[0].text == "push rbp");
        CHECK(ins[0].bytes.size() == 1 && ins[0].bytes[0] == 0x55);
        CHECK(ins[0].symbol == "<main>");
        CHECK(ins[1].text == "mov rbp, rsp");
        bool foundCall = false;
        for (const Instruction &i : ins)
            if (i.text.rfind("call", 0) == 0 && i.text.find("helper") != std::string::npos)
                foundCall = true;
        if (!foundCall)
            for (const Instruction &i : ins)
                std::printf("   %llx  %s\n", (unsigned long long)i.address, i.text.c_str());
        CHECK(foundCall);
    }

    // registers, expressions
    CHECK(d.setRegister("rax", "rbx+100", err));
    d.regs(r);
    CHECK_EQ(r.rax, 107u);
    uint64_t a = 0;
    CHECK(d.evalAddress("counter", a, err) && a != 0);
    CHECK(d.evalAddress("main+4", a, err) && a == main + 4);
    CHECK(d.evalAddress("rsp", a, err) && a == r.rsp);

    // stepping by line: we are on the "call helper" line
    CHECK_EQ(d.lineContaining(main + 0x17), 22);
    s = d.stepInto();
    CHECK_EQ(s.pc, helper);
    CHECK_EQ(d.lineAt(s.pc), 36);
    s = d.stepOver();            // mov rdx, 42
    CHECK_EQ(s.pc, helper + 5);
    d.regs(r);
    CHECK_EQ(r.rdx, 42u);
    auto frames = d.callStack();
    CHECK(frames.size() >= 2);
    if (frames.size() >= 2) {
        CHECK_EQ(frames[0].line, 37);
        CHECK_EQ(frames[1].address, main + 0x1C);
        CHECK_EQ(frames[1].line, 22);
    }
    s = d.stepOut();             // back after the call
    CHECK(s.kind == Stop::Stopped);
    CHECK_EQ(s.pc, main + 0x1C);
    s = d.stepOver();            // lea
    CHECK_EQ(s.pc, main + 0x23);
    s = d.stepOver();            // call puts: a system function, run through
    CHECK_EQ(s.pc, main + 0x28);

    // stop on write
    CHECK(d.watchMemory("counter", 8, "counter", err));
    s = d.cont();
    CHECK(s.kind == Stop::MemoryWritten);
    CHECK(s.watchName == "counter" && s.oldValue == "0x0" && s.newValue == "0x1");
    CHECK_EQ(s.pc, main + 0x33);
    s = d.cont();
    CHECK(s.kind == Stop::MemoryWritten);
    CHECK(s.newValue == "0x2" && s.oldValue == "0x1");
    d.clearWatches();

    // conditional breakpoint: loop1 is reached with rcx = 3, 2, 1
    d.setBreakpoints({ main + 0x43 });
    d.setCondition(main + 0x43, "rcx == 1");
    s = d.cont();
    CHECK(s.kind == Stop::Stopped);
    CHECK_EQ(s.pc, main + 0x43);
    d.regs(r);
    CHECK_EQ(r.rcx, 1u);
    // a long loop inside one line runs at full speed
    d.setBreakpoints({});
    d.setCondition(main + 0x43, "");

    // run to the end
    s = d.cont();
    CHECK(s.kind == Stop::Finished);
    CHECK_EQ(s.exitCode, 7);
    d.terminate();
    CHECK(out.get().find("native engine test") != std::string::npos);
}

static void testCrash(const std::string &dir)
{
    std::puts("== crash_test: access violation");
    Output out;
    Debuggee d;
    std::string err;
    CHECK(d.launch(widen(dir + "\\crash_test.exe"), L"", L"", out.fn(), err));
    const uint64_t main = d.symbolAddress("main");
    Stop s = d.runTo(main);
    CHECK(s.kind == Stop::Stopped);
    s = d.cont();
    CHECK(s.kind == Stop::Crashed);
    CHECK(s.exceptionName == "EXCEPTION_ACCESS_VIOLATION");
    CHECK(s.meaning.find("write") != std::string::npos && s.meaning.find("0x0") != std::string::npos);
    CHECK_EQ(s.pc, main + 2);
    nd::Regs r;
    CHECK(d.regs(r));
    CHECK_EQ(r.rip, main + 2);
    s = d.cont();    // the program gets its exception back and ends
    CHECK(s.kind == Stop::Finished);
    CHECK_EQ(uint32_t(s.exitCode), 0xC0000005u);
    d.terminate();
}

static void testPauseAndKill(const std::string &dir)
{
    std::puts("== loop_test: pause, step while paused, kill");
    Output out;
    Debuggee d;
    std::string err;
    CHECK(d.launch(widen(dir + "\\loop_test.exe"), L"", L"", out.fn(), err));
    const uint64_t main = d.symbolAddress("main");
    Stop s = d.runTo(main);
    CHECK(s.kind == Stop::Stopped);
    std::thread pauser([&d]() { std::this_thread::sleep_for(std::chrono::milliseconds(400)); d.pause(); });
    s = d.cont();
    pauser.join();
    CHECK(s.kind == Stop::Stopped);
    CHECK(s.pc >= main && s.pc < main + 16);
    nd::Regs r;
    CHECK(d.regs(r));
    CHECK(r.rax > 1000);       // it ran for a while
    const uint64_t before = r.rax;
    for (int i = 0; i < 6; ++i)
        s = d.stepInstruction();
    d.regs(r);
    CHECK(r.rax >= before + 2 && r.rax <= before + 3);
    // kill while running
    std::thread killer([&d]() { std::this_thread::sleep_for(std::chrono::milliseconds(300)); d.kill(); });
    s = d.cont();
    killer.join();
    CHECK(s.kind == Stop::Finished);
    d.terminate();
    CHECK(!d.alive());
}

static void testAttach(const std::string &dir)
{
    std::puts("== loop_test: attach to a running program, detach");
    const std::wstring exe = widen(dir + "\\loop_test.exe");
    STARTUPINFOW si{};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    std::wstring cmd = L"\"" + exe + L"\"";
    CHECK(CreateProcessW(exe.c_str(), &cmd[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi));
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    {
        Debuggee d;
        std::string err;
        const bool ok = d.attach(pi.dwProcessId, exe, err);
        if (!ok)
            std::printf("attach error: %s\n", err.c_str());
        CHECK(ok);
        if (ok) {
            nd::Regs r;
            CHECK(d.regs(r));
            CHECK(r.rsp != 0);
            Stop s = d.stepInstruction();
            CHECK(s.kind == Stop::Stopped);
            d.detach();
        }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    CHECK(WaitForSingleObject(pi.hProcess, 0) == WAIT_TIMEOUT);   // still running: not killed by detaching
    TerminateProcess(pi.hProcess, 0);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        std::puts("usage: debuggee_harness <directory>");
        return 2;
    }
    const std::string dir = argv[1];
    testProgram(dir);
    testCrash(dir);
    testPauseAndKill(dir);
    testAttach(dir);
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
