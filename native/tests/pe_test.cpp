// Unit test for native/pe.cpp. Usage: pe_test <program.exe>
// The program must be a 64-bit PE built from tests/debug_crash_test.asm (labels main, helper, loop1, msg).
#include "../pe.h"
#include <cstdio>
#include <cstdlib>

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

int main(int argc, char **argv)
{
    if (argc < 2) { std::puts("usage: pe_test program.exe"); return 2; }
    pe::Info info; std::string err;
    if (!pe::parseFile(argv[1], info, err)) { std::printf("parse failed: %s\n", err.c_str()); return 1; }
    std::printf("64-bit=%d base=0x%llx entry=0x%x sections=%zu symbols=%zu imports=%zu exports=%zu\n",
                info.is64, (unsigned long long)info.imageBase, info.entryRva, info.sections.size(),
                info.symbols.size(), info.imports.size(), info.exports.size());
    CHECK(info.is64);
    CHECK(info.imageBase == 0x400000);
    CHECK(info.sections.size() >= 3);
    CHECK(info.sections[0].name == ".text" && info.sections[0].executable());
    const pe::Symbol *m = info.findSymbol("main");
    CHECK(m != nullptr);
    if (m) {
        std::printf("main: rva=0x%x section=%d external=%d function=%d\n", m->rva, m->section, m->external, m->function);
        CHECK(m->external);
        std::printf("  symbolize(main+4) = %s\n", info.symbolize(m->rva + 4).c_str());
        CHECK(info.symbolize(m->rva) == "main");
        CHECK(info.symbolize(m->rva + 4) == "main+0x4");
    }
    CHECK(info.findSymbol("helper") != nullptr);
    CHECK(info.findSymbol("msg") != nullptr);
    bool sawFflush = false, sawKernel32 = false;
    for (const pe::Import &i : info.imports) {
        if (i.name == "fflush") sawFflush = true;
        if (i.dll == "KERNEL32.dll" || i.dll == "kernel32.dll") sawKernel32 = true;
    }
    CHECK(sawFflush);
    CHECK(sawKernel32);
    CHECK(info.rvaToOffset(info.entryRva) > 0);
    // damaged input must not crash
    pe::Info bad; std::string e2;
    CHECK(!pe::parse(reinterpret_cast<const uint8_t *>("MZ"), 2, bad, e2));
    std::puts(failures ? "PE TEST FAILED" : "PE TEST PASSED");
    return failures ? 1 : 0;
}
