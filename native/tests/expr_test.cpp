// Unit test for native/expr.cpp and native/regs.cpp
#include "../expr.h"
#include "../regs.h"
#include <cstdio>
#include <cstring>
#include <map>

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

static uint64_t ev(const std::string &text, const expr::Context &c, bool address = false, bool *ok = nullptr)
{
    uint64_t v = 0; std::string err;
    const bool good = address ? expr::evalAddress(text, c, v, err) : expr::eval(text, c, v, err);
    if (ok) *ok = good;
    if (!good && !ok) std::printf("  (error for '%s': %s)\n", text.c_str(), err.c_str());
    return v;
}

int main()
{
    nd::Regs r; r.rax = 5; r.rbx = 0x1122334455667788ull; r.rcx = 3; r.rsp = 0x7000; r.rbp = 0x7010; r.r8 = 0xffffffffffffffffull;
    std::map<std::string, uint64_t> syms = { { "msg", 0x404000 }, { "main", 0x401480 } };
    uint8_t memory[64]; for (int i = 0; i < 64; ++i) memory[i] = uint8_t(i + 1);   // at 0x7000..
    expr::Context c;
    c.reg = [&](const std::string &n, uint64_t &v) { return nd::getRegister(r, n, v); };
    c.symbol = [&](const std::string &n, uint64_t &v) { auto it = syms.find(n); if (it == syms.end()) return false; v = it->second; return true; };
    c.mem = [&](uint64_t a, int size, uint64_t &v) {
        if (a < 0x7000 || a + size > 0x7000 + 64) return false;
        v = 0; for (int i = size - 1; i >= 0; --i) v = (v << 8) | memory[a - 0x7000 + i]; return true; };

    CHECK(ev("rax", c) == 5);
    CHECK(ev("$rax + 1", c) == 6);
    CHECK(ev("rax == 5", c) == 1);
    CHECK(ev("rax != 5", c) == 0);
    CHECK(ev("rax == 5 && rcx == 3", c) == 1);
    CHECK(ev("rax == 4 || rcx == 3", c) == 1);
    CHECK(ev("rcx > 2 && rcx < 4", c) == 1);
    CHECK(ev("0x10 + 2*3", c) == 22);
    CHECK(ev("(1+2)*3", c) == 9);
    CHECK(ev("10h", c) == 16);
    CHECK(ev("101b", c) == 5);
    CHECK(ev("1 << 4", c) == 16);
    CHECK(ev("0xff & 0x0f", c) == 15);
    CHECK(ev("-1 < 0", c) == 1);                 // signed compare
    CHECK(ev("r8 == -1", c) == 1);
    CHECK(ev("eax", c) == 5 && ev("ax", c) == 5 && ev("al", c) == 5);
    CHECK(ev("ebx", c) == 0x55667788);
    CHECK(ev("bx", c) == 0x7788);
    CHECK(ev("bl", c) == 0x88);
    CHECK(ev("bh", c) == 0x77);
    CHECK(ev("r8d", c) == 0xffffffffull);
    CHECK(ev("[rsp]", c) == 0x0807060504030201ull);
    CHECK(ev("dword [rsp]", c) == 0x04030201);
    CHECK(ev("byte ptr [rsp+2]", c) == 3);
    CHECK(ev("[rbp-16] == 0x0807060504030201", c) == 1);   // rbp-8 = 0x7008
    CHECK(ev("main + 4", c) == 0x401484);
    // addresses
    CHECK(ev("msg", c, true) == 0x404000);
    CHECK(ev("&msg", c, true) == 0x404000);
    CHECK(ev("rsp", c, true) == 0x7000);
    CHECK(ev("[rbp-8]", c, true) == 0x7008);          // NASM operand: the address, not the content
    CHECK(ev("[rbp - 8] ", c, true) == 0x7008);
    CHECK(ev("msg+4", c, true) == 0x404004);
    CHECK(ev("0x404000", c, true) == 0x404000);
    CHECK(ev("[rsp]+[rbp]", c, true) != 0);           // not one bracket pair: evaluated as an expression
    // errors
    bool ok;
    ev("nosuch + 1", c, false, &ok); CHECK(!ok);
    ev("rax +", c, false, &ok); CHECK(!ok);
    ev("(rax", c, false, &ok); CHECK(!ok);
    ev("[0x10]", c, false, &ok); CHECK(!ok);          // unreadable memory
    ev("1 / 0", c, false, &ok); CHECK(!ok);
    ev("", c, false, &ok); CHECK(!ok);

    // registers: write
    nd::Regs w; w.rax = 0xffffffffffffffffull;
    CHECK(nd::setRegister(w, "eax", 1) && w.rax == 1);
    w.rax = 0x1122334455667788ull;
    CHECK(nd::setRegister(w, "ax", 0xaaaa) && w.rax == 0x112233445566aaaaull);
    CHECK(nd::setRegister(w, "al", 0xbb) && w.rax == 0x112233445566aabbull);
    CHECK(nd::setRegister(w, "ah", 0xcc) && w.rax == 0x112233445566ccbbull);
    CHECK(nd::setRegister(w, "r9d", 7) && w.r9 == 7);
    CHECK(nd::setRegister(w, "$rip", 0x401000) && w.rip == 0x401000);
    CHECK(!nd::setRegister(w, "xmm0", 1));
    CHECK(nd::isRegisterName("RSP") && nd::isRegisterName("eflags") && !nd::isRegisterName("msg"));

    std::puts(failures ? "EXPR TEST FAILED" : "EXPR TEST PASSED");
    return failures ? 1 : 0;
}
