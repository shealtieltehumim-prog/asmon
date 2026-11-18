/****************************************************************************
** Asmon — GPL-3.0.
**
** Expression evaluator for the native debugger: breakpoint conditions
** ("rcx == 5 && [rbp-8] > 0x10"), addresses ("msg", "rsp", "[rbp-8]",
** "msg+4") and register assignments ("rax+1"). Pure C++17, no Qt.
**
** Grammar, lowest precedence first:  || && | ^ & ==,!= <,>,<=,>= <<,>> +,- *,/,%
**   unary -,~,!   primary: number | register | label | (expr) | [expr] |
**   byte/word/dword/qword [expr]
** "[expr]" is the content of memory (8 bytes by default). Numbers: 123, 0x7f, 0FFh, 101b.
** Values are unsigned 64-bit; comparisons are unsigned except for < > <= >= on values
** that look negative (the sign bit set in the 64-bit result): they compare signed.
****************************************************************************/
#ifndef ASMON_EXPR_H
#define ASMON_EXPR_H

#include <cstdint>
#include <functional>
#include <string>

namespace expr {

struct Context {
    std::function<bool(const std::string &name, uint64_t &value)> reg;     //!< register value
    std::function<bool(const std::string &name, uint64_t &value)> symbol;  //!< label address
    std::function<bool(uint64_t address, int size, uint64_t &value)> mem;  //!< memory content
};

bool eval(const std::string &text, const Context &ctx, uint64_t &out, std::string &err);

//! The address a text stands for: "msg" (label address), "rsp"/"rax" (the register's value),
//! "[rbp-8]" (the address inside the brackets, not its content), "msg+4", "0x404000".
bool evalAddress(const std::string &text, const Context &ctx, uint64_t &out, std::string &err);

} // namespace expr

#endif // ASMON_EXPR_H
