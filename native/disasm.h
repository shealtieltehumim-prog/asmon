/****************************************************************************
** Asmon — GPL-3.0.
**
** x86 / x86-64 disassembler (Intel syntax) on top of Zydis (MIT, bundled in
** third_party/zydis). Pure C++17, no Qt.
****************************************************************************/
#ifndef ASMON_DISASM_H
#define ASMON_DISASM_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace dis {

struct Insn {
    uint64_t address = 0;
    int length = 0;
    std::string text;        //!< "mov rax, 0x5"
    std::string comment;     //!< "0x404010 <msg>" for rip-relative operands, "" if none
    bool call = false, ret = false, jump = false, conditional = false, pushf = false, syscall = false;
    bool hasTarget = false;  //!< a direct branch: target is known
    uint64_t target = 0;
};

//! name for an address ("main+0x4"), or "" when unknown; used inside the text of branches
using Symbolizer = std::function<std::string(uint64_t)>;

//! Decodes the instruction at the start of code. False for bytes that are not an instruction
//! (out.length is then 1, out.text "db 0x..").
bool decode(const uint8_t *code, size_t size, uint64_t address, bool is64, Insn &out,
            const Symbolizer *symbolizer = nullptr);

//! True if a "call" instruction ends exactly at address (so address is a return address):
//! code[0..size) are the bytes just before address (up to 16 are used).
bool endsWithCall(const uint8_t *before, size_t size, uint64_t address, bool is64);

} // namespace dis

#endif // ASMON_DISASM_H
