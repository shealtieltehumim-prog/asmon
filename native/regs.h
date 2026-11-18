/****************************************************************************
** Asmon — GPL-3.0.
**
** The x86-64 register file as the debugger shows it, with access by name
** (rax, eax, ax, al, ah, r8d, rip, eflags...). Pure C++17.
****************************************************************************/
#ifndef ASMON_REGS_H
#define ASMON_REGS_H

#include <cstdint>
#include <string>

namespace nd {

struct Regs {
    uint64_t rax = 0, rbx = 0, rcx = 0, rdx = 0, rsi = 0, rdi = 0, rbp = 0, rsp = 0;
    uint64_t r8 = 0, r9 = 0, r10 = 0, r11 = 0, r12 = 0, r13 = 0, r14 = 0, r15 = 0;
    uint64_t rip = 0, eflags = 0;
    uint16_t cs = 0, ss = 0, ds = 0, es = 0, fs = 0, gs = 0;
};

//! Reads a register by (case-insensitive, optional leading $) name. False: no such register.
bool getRegister(const Regs &r, const std::string &name, uint64_t &value);
//! Writes a register; 32-bit names zero-extend, 16/8-bit names keep the other bits.
bool setRegister(Regs &r, const std::string &name, uint64_t value);
bool isRegisterName(const std::string &name);

} // namespace nd

#endif // ASMON_REGS_H
