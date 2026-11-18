// Unit test for native/disasm.cpp
#include "../disasm.h"
#include <cstdio>
#include <cstring>

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

int main()
{
    dis::Insn i;
    const uint8_t mov[] = { 0x48, 0x89, 0xe5 };
    CHECK(dis::decode(mov, sizeof mov, 0x401000, true, i));
    std::printf("[%s] len=%d\n", i.text.c_str(), i.length);
    CHECK(i.length == 3 && i.text == "mov rbp, rsp");

    // call rel32 to 0x401018 (0x401003 + 5 + 0x10)
    const uint8_t call[] = { 0xe8, 0x10, 0, 0, 0 };
    dis::Symbolizer sym = [](uint64_t a) { return a == 0x401018 ? std::string("helper") : std::string(); };
    CHECK(dis::decode(call, sizeof call, 0x401003, true, i, &sym));
    std::printf("[%s] call=%d target=0x%llx\n", i.text.c_str(), i.call, (unsigned long long)i.target);
    CHECK(i.call && i.hasTarget && i.target == 0x401018 && i.text == "call helper");
    CHECK(dis::decode(call, sizeof call, 0x401003, true, i));
    CHECK(i.text.find("0x401018") != std::string::npos);

    const uint8_t ret[] = { 0xc3 };
    CHECK(dis::decode(ret, 1, 0, true, i) && i.ret && !i.call);
    const uint8_t pushf[] = { 0x9c };
    CHECK(dis::decode(pushf, 1, 0, true, i) && i.pushf);
    const uint8_t jnz[] = { 0x75, 0xfe };
    CHECK(dis::decode(jnz, 2, 0x401020, true, i) && i.jump && i.conditional && i.target == 0x401020);

    // lea rax, [rip+0x10] at 0x401000: rip-relative operand -> comment with the target
    const uint8_t lea[] = { 0x48, 0x8d, 0x05, 0x10, 0, 0, 0 };
    dis::Symbolizer msg = [](uint64_t a) { return a == 0x401017 ? std::string("msg") : std::string(); };
    CHECK(dis::decode(lea, sizeof lea, 0x401000, true, i, &msg));
    std::printf("[%s] ; %s\n", i.text.c_str(), i.comment.c_str());
    CHECK(i.comment == "0x401017 <msg>");

    // bad bytes
    const uint8_t bad[] = { 0x06 };   // invalid in 64-bit mode
    CHECK(!dis::decode(bad, 1, 0, true, i) && i.length == 1);

    // return address detection
    const uint8_t before[] = { 0x90, 0x90, 0xe8, 0x10, 0, 0, 0 };   // address = after the call
    CHECK(dis::endsWithCall(before, sizeof before, 0x401007, true));
    const uint8_t notcall[] = { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };
    CHECK(!dis::endsWithCall(notcall, sizeof notcall, 0x401007, true));
    const uint8_t callrax[] = { 0x90, 0xff, 0xd0 };   // call rax (2 bytes)
    CHECK(dis::endsWithCall(callrax, sizeof callrax, 0x401003, true));

    std::puts(failures ? "DISASM TEST FAILED" : "DISASM TEST PASSED");
    return failures ? 1 : 0;
}
