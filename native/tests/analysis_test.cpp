// Unit test for native/analysis.cpp. Usage: analysis_test <engine_test.exe> <scratch directory>
// engine_test.exe: native/tests/programs/engine_test.asm assembled with NASM and linked with gcc (labels main, helper, msg).
#include "../analysis.h"
#include <cstdio>
#include <cstdlib>

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

int main(int argc, char **argv)
{
    if (argc < 3) { std::puts("usage: analysis_test engine_test.exe scratch-dir"); return 2; }
    ra::Image img;
    std::string err;
    if (!img.load(argv[1], err)) { std::printf("load failed: %s\n", err.c_str()); return 1; }
    ra::Result res;
    CHECK(ra::analyze(img, res, err));
    std::printf("functions=%zu xrefs=%zu strings=%zu imports=%zu\n", res.functions.size(), res.xrefs.size(),
                res.strings.size(), res.imports.size());
    CHECK(res.functions.size() > 5);
    for (size_t i = 1; i < res.functions.size(); ++i)
        CHECK(res.functions[i - 1].va < res.functions[i].va);

    const pe::Symbol *m = img.info().findSymbol("main"), *h = img.info().findSymbol("helper"), *d = img.info().findSymbol("msg");
    CHECK(m && h && d);
    if (!m || !h || !d) return 1;
    const uint64_t main = img.base() + m->rva, helper = img.base() + h->rva, msg = img.base() + d->rva;

    const ra::Function *fm = res.functionStartingAt(main);
    CHECK(fm != nullptr);
    if (fm) {
        CHECK(res.functionName(*fm) == "main");
        CHECK(fm->size >= 0x4F && fm->size <= 0x60);
    }
    const ra::Function *fh = res.functionStartingAt(helper);
    CHECK(fh != nullptr);                     // found because main calls it
    if (fh) {
        CHECK(res.functionName(*fh) == "helper");
        CHECK(fh->source == ra::Function::Call);
    }
    const ra::Function *at = res.functionAt(main + 0x20);
    CHECK(at && at->va == main);
    CHECK(res.nameAt(main + 0x14) == "main+0x14");

    // the entry point and a library stub
    bool entry = false, puts = false;
    for (const ra::Function &f : res.functions) {
        entry |= f.source == ra::Function::Entry;
        if (f.thunk && f.name == "puts") puts = true;
    }
    CHECK(entry);
    CHECK(puts);

    // cross-references
    auto toHelper = res.xrefsTo(helper);
    CHECK(toHelper.size() == 1 && toHelper[0].kind == ra::Xref::Call && toHelper[0].from == main + 0x17);
    auto toMsg = res.xrefsTo(msg);
    CHECK(toMsg.size() == 1 && toMsg[0].kind == ra::Xref::Data && toMsg[0].from == main + 0x1C);

    // strings
    const ra::String *s = res.stringAt(msg);
    CHECK(s && s->text == "native engine test" && !s->wide);

    // disassembly with names
    auto lines = ra::disassemble(img, res, main, 12);
    CHECK(lines.size() == 12);
    if (lines.size() == 12) {
        CHECK(lines[0].text == "push rbp" && lines[0].functionStart && lines[0].bytes == "55");
        CHECK(lines[1].text == "mov rbp, rsp");
        bool call = false, lea = false;
        for (const auto &l : lines) {
            call |= l.text == "call helper";
            lea |= l.text.rfind("lea rcx", 0) == 0 && l.comment.find("native engine test") != std::string::npos;
        }
        CHECK(call);
        CHECK(lea);
    }

    // rename and comment show up
    res.userNames[helper] = "square_it";
    res.comments[main] = "entry of the test";
    lines = ra::disassemble(img, res, main, 12);
    bool renamed = false;
    for (const auto &l : lines) renamed |= l.text == "call square_it";
    CHECK(renamed);
    CHECK(lines[0].comment == "; entry of the test");

    // patch: a nop at main, saved to a new file; the original is untouched
    CHECK(img.patchVa(main, { 0x90 }));
    lines = ra::disassemble(img, res, main, 2);
    CHECK(lines[0].text == "nop" && lines[0].patched && !lines[1].patched);
    const std::string out = std::string(argv[2]) + "/patched.exe";
    CHECK(img.saveAs(out, err));
    CHECK(!img.saveAs(argv[1], err));          // never over the original
    ra::Image again;
    CHECK(again.load(out, err));
    uint8_t b = 0;
    again.read(main, &b, 1);
    CHECK(b == 0x90);
    ra::Image orig;
    CHECK(orig.load(argv[1], err));
    orig.read(main, &b, 1);
    CHECK(b == 0x55);
    img.revertPatch(uint64_t(img.vaToOffset(main)));
    img.read(main, &b, 1);
    CHECK(b == 0x55);

    // wide strings
    std::vector<uint8_t> raw;
    {
        FILE *f = std::fopen(argv[1], "rb");
        uint8_t chunk[4096]; size_t n;
        while ((n = std::fread(chunk, 1, sizeof chunk, f)) > 0) raw.insert(raw.end(), chunk, chunk + n);
        std::fclose(f);
    }
    const char *w = "W\0i\0d\0e\0 \0t\0e\0x\0t\0\0\0";
    const int64_t off = img.vaToOffset(msg);
    ra::Image wide;
    for (int i = 0; i < 20; ++i) raw[size_t(off) + size_t(i)] = uint8_t(w[i]);
    CHECK(wide.loadBytes(raw, "", err));
    ra::Result wr;
    CHECK(ra::analyze(wide, wr, err));
    const ra::String *ws = wr.stringAt(msg);
    CHECK(ws && ws->wide && ws->text == "Wide text");

    if (failures) { std::printf("%d failure(s)\n", failures); return 1; }
    std::puts("ANALYSIS TEST PASSED");
    return 0;
}
