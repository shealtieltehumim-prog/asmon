#include "disasm.h"

#include <Zycore/Format.h>
#include <Zydis/Zydis.h>

#include <cstdio>
#include <cstring>

namespace dis {

namespace {

struct Ctx {
    const Symbolizer *symbolizer;
    ZydisFormatterFunc defaultPrintAddress;
};

//! Intel syntax with symbol names for branch targets ("call main+0x10").
ZyanStatus printAddressAbs(const ZydisFormatter *formatter, ZydisFormatterBuffer *buffer,
                           ZydisFormatterContext *context)
{
    Ctx *c = static_cast<Ctx *>(context->user_data);
    ZyanU64 address = 0;
    ZYAN_CHECK(ZydisCalcAbsoluteAddress(context->instruction, context->operand, context->runtime_address, &address));
    if (c && c->symbolizer && *c->symbolizer) {
        const std::string name = (*c->symbolizer)(address);
        if (!name.empty()) {
            ZYAN_CHECK(ZydisFormatterBufferAppend(buffer, ZYDIS_TOKEN_SYMBOL));
            ZyanString *s = nullptr;
            ZYAN_CHECK(ZydisFormatterBufferGetString(buffer, &s));
            return ZyanStringAppendFormat(s, "%s", name.c_str());
        }
    }
    return c->defaultPrintAddress(formatter, buffer, context);
}

struct Tables {
    ZydisDecoder dec64, dec32;
    ZydisFormatter fmt;
    ZydisFormatterFunc defaultPrintAddress = nullptr;
    Tables() {
        ZydisDecoderInit(&dec64, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64);
        ZydisDecoderInit(&dec32, ZYDIS_MACHINE_MODE_LEGACY_32, ZYDIS_STACK_WIDTH_32);
        ZydisFormatterInit(&fmt, ZYDIS_FORMATTER_STYLE_INTEL);
        ZydisFormatterSetProperty(&fmt, ZYDIS_FORMATTER_PROP_FORCE_SIZE, ZYAN_TRUE);
        ZydisFormatterSetProperty(&fmt, ZYDIS_FORMATTER_PROP_HEX_UPPERCASE, ZYAN_FALSE);
        ZydisFormatterSetProperty(&fmt, ZYDIS_FORMATTER_PROP_ADDR_PADDING_ABSOLUTE, ZYDIS_PADDING_DISABLED);
        ZydisFormatterSetProperty(&fmt, ZYDIS_FORMATTER_PROP_ADDR_PADDING_RELATIVE, ZYDIS_PADDING_DISABLED);
        ZydisFormatterSetProperty(&fmt, ZYDIS_FORMATTER_PROP_DISP_PADDING, ZYDIS_PADDING_DISABLED);
        ZydisFormatterSetProperty(&fmt, ZYDIS_FORMATTER_PROP_IMM_PADDING, ZYDIS_PADDING_DISABLED);
        const void *hook = reinterpret_cast<const void *>(printAddressAbs);
        ZydisFormatterSetHook(&fmt, ZYDIS_FORMATTER_FUNC_PRINT_ADDRESS_ABS, &hook);
        defaultPrintAddress = reinterpret_cast<ZydisFormatterFunc>(const_cast<void *>(hook));
    }
};

Tables &tables()
{
    static thread_local Tables t;
    return t;
}

} // namespace

bool decode(const uint8_t *code, size_t size, uint64_t address, bool is64, Insn &out,
            const Symbolizer *symbolizer)
{
    out = Insn();
    out.address = address;
    Tables &t = tables();
    ZydisDecodedInstruction ins;
    ZydisDecodedOperand ops[ZYDIS_MAX_OPERAND_COUNT];
    const ZydisDecoder &dec = is64 ? t.dec64 : t.dec32;
    if (!ZYAN_SUCCESS(ZydisDecoderDecodeFull(&dec, code, size, &ins, ops))) {
        out.length = 1;
        char buf[24];
        std::snprintf(buf, sizeof buf, "db 0x%02x", size ? code[0] : 0);
        out.text = buf;
        return false;
    }
    out.length = ins.length;
    char buf[256];
    Ctx ctx{ symbolizer, t.defaultPrintAddress };
    if (ZYAN_SUCCESS(ZydisFormatterFormatInstruction(&t.fmt, &ins, ops, ins.operand_count_visible, buf, sizeof buf,
                                                     address, &ctx))) {
        out.text = buf;
    } else {
        out.text = "?";
    }
    switch (ins.meta.category) {
    case ZYDIS_CATEGORY_CALL: out.call = true; break;
    case ZYDIS_CATEGORY_RET: out.ret = true; break;
    case ZYDIS_CATEGORY_UNCOND_BR: out.jump = true; break;
    case ZYDIS_CATEGORY_COND_BR: out.jump = true; out.conditional = true; break;
    case ZYDIS_CATEGORY_SYSCALL: out.syscall = true; break;
    default: break;
    }
    out.pushf = ins.mnemonic == ZYDIS_MNEMONIC_PUSHF || ins.mnemonic == ZYDIS_MNEMONIC_PUSHFD
                || ins.mnemonic == ZYDIS_MNEMONIC_PUSHFQ;
    // direct branch target / rip-relative operand
    for (int i = 0; i < ins.operand_count_visible; ++i) {
        const ZydisDecodedOperand &op = ops[i];
        ZyanU64 a = 0;
        if ((out.call || out.jump) && op.type == ZYDIS_OPERAND_TYPE_IMMEDIATE && op.imm.is_relative) {
            if (ZYAN_SUCCESS(ZydisCalcAbsoluteAddress(&ins, &op, address, &a))) {
                out.hasTarget = true;
                out.target = a;
            }
        } else if (op.type == ZYDIS_OPERAND_TYPE_MEMORY && (op.mem.base == ZYDIS_REGISTER_RIP
                                                             || op.mem.base == ZYDIS_REGISTER_EIP)) {
            if (ZYAN_SUCCESS(ZydisCalcAbsoluteAddress(&ins, &op, address, &a))) {
                char c[32];
                std::snprintf(c, sizeof c, "0x%llx", (unsigned long long)a);
                out.comment = c;
                if (symbolizer && *symbolizer) {
                    const std::string name = (*symbolizer)(a);
                    if (!name.empty())
                        out.comment += " <" + name + ">";
                }
            }
        }
    }
    return true;
}

bool endsWithCall(const uint8_t *before, size_t size, uint64_t address, bool is64)
{
    // a call is 2..7 bytes (E8 rel32 = 5, FF /2 forms 2..7; with prefixes up to 8): try each start
    for (size_t len = 2; len <= 8 && len <= size; ++len) {
        Insn i;
        if (decode(before + (size - len), len, address - len, is64, i) && i.length == int(len) && i.call)
            return true;
    }
    return false;
}

} // namespace dis
