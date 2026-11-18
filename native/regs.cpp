#include "regs.h"

#include <algorithm>
#include <cctype>

namespace nd {

namespace {

enum Width { W64, W32, W16, W8L, W8H };

struct Slot {
    uint64_t *p;
    Width w;
};

std::string normalize(const std::string &name)
{
    std::string n;
    for (char c : name)
        if (c != '$' && c != ' ')
            n.push_back(char(std::tolower(static_cast<unsigned char>(c))));
    return n;
}

bool locate(Regs &r, const std::string &raw, Slot &s, uint16_t *&seg)
{
    seg = nullptr;
    const std::string n = normalize(raw);
    struct Named { const char *name; uint64_t *p; };
    const Named full[] = { { "rax", &r.rax }, { "rbx", &r.rbx }, { "rcx", &r.rcx }, { "rdx", &r.rdx },
                           { "rsi", &r.rsi }, { "rdi", &r.rdi }, { "rbp", &r.rbp }, { "rsp", &r.rsp } };
    for (const Named &f : full)
        if (n == f.name) { s = { f.p, W64 }; return true; }
    // legacy 32/16/8-bit names of the first eight registers
    static const char *base[] = { "ax", "bx", "cx", "dx", "si", "di", "bp", "sp" };
    for (int i = 0; i < 8; ++i) {
        const std::string b = base[i];
        if (n == "e" + b) { s = { full[i].p, W32 }; return true; }
        if (n == b) { s = { full[i].p, W16 }; return true; }
    }
    if (n == "al") { s = { &r.rax, W8L }; return true; }
    if (n == "bl") { s = { &r.rbx, W8L }; return true; }
    if (n == "cl") { s = { &r.rcx, W8L }; return true; }
    if (n == "dl") { s = { &r.rdx, W8L }; return true; }
    if (n == "ah") { s = { &r.rax, W8H }; return true; }
    if (n == "bh") { s = { &r.rbx, W8H }; return true; }
    if (n == "ch") { s = { &r.rcx, W8H }; return true; }
    if (n == "dh") { s = { &r.rdx, W8H }; return true; }
    if (n == "sil") { s = { &r.rsi, W8L }; return true; }
    if (n == "dil") { s = { &r.rdi, W8L }; return true; }
    if (n == "bpl") { s = { &r.rbp, W8L }; return true; }
    if (n == "spl") { s = { &r.rsp, W8L }; return true; }
    // r8..r15 with d/w/b suffixes
    if (n.size() >= 2 && n[0] == 'r' && std::isdigit(static_cast<unsigned char>(n[1]))) {
        size_t i = 1;
        int num = 0;
        while (i < n.size() && std::isdigit(static_cast<unsigned char>(n[i])))
            num = num * 10 + (n[i++] - '0');
        if (num >= 8 && num <= 15) {
            uint64_t *regs[] = { &r.r8, &r.r9, &r.r10, &r.r11, &r.r12, &r.r13, &r.r14, &r.r15 };
            const std::string suffix = n.substr(i);
            uint64_t *p = regs[num - 8];
            if (suffix.empty()) { s = { p, W64 }; return true; }
            if (suffix == "d") { s = { p, W32 }; return true; }
            if (suffix == "w") { s = { p, W16 }; return true; }
            if (suffix == "b") { s = { p, W8L }; return true; }
        }
        return false;
    }
    if (n == "rip" || n == "eip") { s = { &r.rip, n == "rip" ? W64 : W32 }; return true; }
    if (n == "eflags" || n == "rflags" || n == "flags") { s = { &r.eflags, W64 }; return true; }
    if (n == "cs") { seg = &r.cs; return true; }
    if (n == "ss") { seg = &r.ss; return true; }
    if (n == "ds") { seg = &r.ds; return true; }
    if (n == "es") { seg = &r.es; return true; }
    if (n == "fs") { seg = &r.fs; return true; }
    if (n == "gs") { seg = &r.gs; return true; }
    return false;
}

} // namespace

bool getRegister(const Regs &rc, const std::string &name, uint64_t &value)
{
    Regs &r = const_cast<Regs &>(rc);
    Slot s{};
    uint16_t *seg = nullptr;
    if (!locate(r, name, s, seg))
        return false;
    if (seg) {
        value = *seg;
        return true;
    }
    switch (s.w) {
    case W64: value = *s.p; break;
    case W32: value = *s.p & 0xffffffffull; break;
    case W16: value = *s.p & 0xffffull; break;
    case W8L: value = *s.p & 0xffull; break;
    case W8H: value = (*s.p >> 8) & 0xffull; break;
    }
    return true;
}

bool setRegister(Regs &r, const std::string &name, uint64_t value)
{
    Slot s{};
    uint16_t *seg = nullptr;
    if (!locate(r, name, s, seg))
        return false;
    if (seg) {
        *seg = uint16_t(value);
        return true;
    }
    switch (s.w) {
    case W64: *s.p = value; break;
    case W32: *s.p = value & 0xffffffffull; break;       // a 32-bit write clears the upper half
    case W16: *s.p = (*s.p & ~0xffffull) | (value & 0xffff); break;
    case W8L: *s.p = (*s.p & ~0xffull) | (value & 0xff); break;
    case W8H: *s.p = (*s.p & ~0xff00ull) | ((value & 0xff) << 8); break;
    }
    return true;
}

bool isRegisterName(const std::string &name)
{
    Regs r;
    uint64_t v;
    return getRegister(r, name, v);
}

} // namespace nd
