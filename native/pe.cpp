#include "pe.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>

namespace pe {

namespace {

struct Reader {
    const uint8_t *d;
    size_t n;
    bool ok(size_t off, size_t len) const { return off <= n && len <= n - off; }
    uint16_t u16(size_t o) const { uint16_t v = 0; if (ok(o, 2)) std::memcpy(&v, d + o, 2); return v; }
    uint32_t u32(size_t o) const { uint32_t v = 0; if (ok(o, 4)) std::memcpy(&v, d + o, 4); return v; }
    uint64_t u64(size_t o) const { uint64_t v = 0; if (ok(o, 8)) std::memcpy(&v, d + o, 8); return v; }
    //! NUL-terminated string at o (at most max bytes)
    std::string cstr(size_t o, size_t max = 512) const {
        std::string s;
        for (size_t i = o; i < n && s.size() < max && d[i]; ++i)
            s.push_back(char(d[i]));
        return s;
    }
};

} // namespace

const Symbol *Info::findSymbol(const std::string &name) const
{
    for (const Symbol &s : symbols)
        if (s.name == name)
            return &s;
    return nullptr;
}

const Section *Info::sectionOfRva(uint32_t rva) const
{
    for (const Section &s : sections) {
        const uint32_t size = std::max(s.virtualSize, s.rawSize);
        if (rva >= s.rva && rva < s.rva + size)
            return &s;
    }
    return nullptr;
}

int64_t Info::rvaToOffset(uint32_t rva) const
{
    const Section *s = sectionOfRva(rva);
    if (!s || rva - s->rva >= s->rawSize)
        return -1;
    return int64_t(s->rawOffset) + (rva - s->rva);
}

std::string Info::symbolize(uint32_t rva) const
{
    const Section *sec = sectionOfRva(rva);
    if (!sec)
        return std::string();
    const Symbol *best = nullptr;
    for (const Symbol &s : symbols) {
        if (s.rva > rva || sectionOfRva(s.rva) != sec)
            continue;
        if (!best || s.rva > best->rva)
            best = &s;
    }
    if (!best)
        return std::string();
    const uint32_t off = rva - best->rva;
    if (off == 0)
        return best->name;
    char buf[24];
    std::snprintf(buf, sizeof buf, "+0x%x", off);
    return best->name + buf;
}

bool parse(const uint8_t *data, size_t size, Info &out, std::string &err)
{
    out = Info();
    const Reader r{ data, size };
    if (size < 0x40 || data[0] != 'M' || data[1] != 'Z') {
        err = "not a PE file (no MZ header)";
        return false;
    }
    const size_t pe = r.u32(0x3c);
    if (!r.ok(pe, 24) || std::memcmp(data + pe, "PE\0\0", 4) != 0) {
        err = "not a PE file (no PE signature)";
        return false;
    }
    const size_t coff = pe + 4;
    out.machine = r.u16(coff);
    const unsigned nSections = r.u16(coff + 2);
    out.timeStamp = r.u32(coff + 4);
    const uint32_t symPtr = r.u32(coff + 8);
    const uint32_t nSyms = r.u32(coff + 12);
    const unsigned optSize = r.u16(coff + 16);
    const uint16_t characteristics = r.u16(coff + 18);
    out.dll = (characteristics & 0x2000) != 0;
    const size_t opt = coff + 20;
    if (!r.ok(opt, std::min<size_t>(optSize, 96)) || optSize < 96) {
        err = "damaged PE file (optional header)";
        return false;
    }
    const uint16_t magic = r.u16(opt);
    if (magic != 0x10b && magic != 0x20b) {
        err = "unknown PE optional header";
        return false;
    }
    out.is64 = magic == 0x20b;
    out.entryRva = r.u32(opt + 16);
    out.imageBase = out.is64 ? r.u64(opt + 24) : r.u32(opt + 28);
    out.sizeOfImage = r.u32(opt + 56);
    out.checksum = r.u32(opt + 64);
    out.subsystem = r.u16(opt + 68);
    const size_t dirCountAt = opt + (out.is64 ? 108 : 92);
    const uint32_t nDirs = r.u32(dirCountAt);
    const size_t dirs = dirCountAt + 4;
    auto dir = [&](unsigned i, uint32_t &rva, uint32_t &sz) {
        rva = sz = 0;
        if (i < nDirs && r.ok(dirs + i * 8, 8)) {
            rva = r.u32(dirs + i * 8);
            sz = r.u32(dirs + i * 8 + 4);
        }
    };

    size_t sh = opt + optSize;
    if (nSections > 96 || !r.ok(sh, size_t(nSections) * 40)) {
        err = "damaged PE file (section table)";
        return false;
    }
    for (unsigned i = 0; i < nSections; ++i, sh += 40) {
        Section s;
        char nm[9] = {};
        std::memcpy(nm, data + sh, 8);
        s.name = nm;
        s.virtualSize = r.u32(sh + 8);
        s.rva = r.u32(sh + 12);
        s.rawSize = r.u32(sh + 16);
        s.rawOffset = r.u32(sh + 20);
        s.characteristics = r.u32(sh + 36);
        out.sections.push_back(s);
    }

    // COFF symbols (what ld keeps unless the file was stripped)
    if (symPtr && nSyms && r.ok(symPtr, size_t(nSyms) * 18)) {
        const size_t strTab = size_t(symPtr) + size_t(nSyms) * 18;
        for (uint32_t i = 0; i < nSyms; ++i) {
            const size_t o = size_t(symPtr) + size_t(i) * 18;
            const int16_t section = int16_t(r.u16(o + 12));
            const uint16_t type = r.u16(o + 14);
            const uint8_t storage = data[o + 16];
            const uint8_t aux = data[o + 17];
            const uint32_t value = r.u32(o + 8);
            std::string name;
            if (r.u32(o) == 0) {
                const uint32_t off = r.u32(o + 4);
                name = r.cstr(strTab + off, 1024);
            } else {
                char nm[9] = {};
                std::memcpy(nm, data + o, 8);
                name = nm;
            }
            const uint32_t auxCount = aux;
            const uint32_t next = i + auxCount;
            if (section > 0 && (storage == 2 || storage == 3) && !name.empty() && name[0] != '.'
                    && size_t(section) <= out.sections.size()) {
                Symbol s;
                s.name = name;
                s.section = section;
                s.external = storage == 2;
                s.function = (type & 0xFFF0) == 0x20;
                //! the value is an offset inside the section
                s.rva = out.sections[size_t(section) - 1].rva + value;
                out.symbols.push_back(s);
            }
            i = next;
        }
    }

    // imports
    uint32_t rva, sz;
    dir(1, rva, sz);
    if (rva) {
        const int64_t base = out.rvaToOffset(rva);
        for (size_t k = 0; base >= 0 && r.ok(size_t(base) + k * 20, 20); ++k) {
            const size_t e = size_t(base) + k * 20;
            const uint32_t origThunk = r.u32(e), nameRva = r.u32(e + 12), firstThunk = r.u32(e + 16);
            if (!nameRva && !firstThunk)
                break;
            const int64_t nameOff = out.rvaToOffset(nameRva);
            const std::string dll = nameOff >= 0 ? r.cstr(size_t(nameOff)) : std::string();
            const uint32_t thunkRva = origThunk ? origThunk : firstThunk;
            const int64_t thunkOff = out.rvaToOffset(thunkRva);
            const unsigned w = out.is64 ? 8 : 4;
            for (size_t t = 0; thunkOff >= 0 && r.ok(size_t(thunkOff) + t * w, w) && t < 100000; ++t) {
                const uint64_t v = out.is64 ? r.u64(size_t(thunkOff) + t * w) : r.u32(size_t(thunkOff) + t * w);
                if (!v)
                    break;
                Import imp;
                imp.dll = dll;
                imp.iatRva = firstThunk + uint32_t(t * w);
                const uint64_t ordFlag = out.is64 ? (1ull << 63) : (1ull << 31);
                if (v & ordFlag) {
                    imp.ordinal = uint16_t(v & 0xffff);
                } else {
                    const int64_t hn = out.rvaToOffset(uint32_t(v));
                    if (hn >= 0) {
                        imp.ordinal = r.u16(size_t(hn));
                        imp.name = r.cstr(size_t(hn) + 2);
                    }
                }
                out.imports.push_back(imp);
            }
        }
    }

    // exports
    dir(0, rva, sz);
    if (rva) {
        const int64_t e = out.rvaToOffset(rva);
        if (e >= 0 && r.ok(size_t(e), 40)) {
            const uint32_t ordBase = r.u32(size_t(e) + 16), nFn = r.u32(size_t(e) + 20), nNames = r.u32(size_t(e) + 24);
            const int64_t fns = out.rvaToOffset(r.u32(size_t(e) + 28));
            const int64_t names = out.rvaToOffset(r.u32(size_t(e) + 32));
            const int64_t ords = out.rvaToOffset(r.u32(size_t(e) + 36));
            for (uint32_t i = 0; fns >= 0 && names >= 0 && ords >= 0 && i < nNames && i < 65536; ++i) {
                const uint16_t idx = r.u16(size_t(ords) + i * 2);
                if (idx >= nFn)
                    continue;
                const int64_t nameOff = out.rvaToOffset(r.u32(size_t(names) + i * 4));
                Export ex;
                ex.name = nameOff >= 0 ? r.cstr(size_t(nameOff)) : std::string();
                ex.rva = r.u32(size_t(fns) + idx * 4);
                ex.ordinal = uint16_t(ordBase + idx);
                out.exports.push_back(ex);
            }
        }
    }

    // exception table: function boundaries of 64-bit images
    dir(3, rva, sz);
    if (out.is64 && rva) {
        const int64_t e = out.rvaToOffset(rva);
        for (size_t k = 0; e >= 0 && k < sz / 12 && k < 4000000 && r.ok(size_t(e) + k * 12, 12); ++k) {
            const size_t at = size_t(e) + k * 12;
            RuntimeFunction f;
            f.begin = r.u32(at);
            f.end = r.u32(at + 4);
            if (f.begin && f.end > f.begin && f.end <= out.sizeOfImage)
                out.runtimeFunctions.push_back(f);
        }
        std::sort(out.runtimeFunctions.begin(), out.runtimeFunctions.end(),
                  [](const RuntimeFunction &a, const RuntimeFunction &b) { return a.begin < b.begin; });
    }
    return true;
}

bool parseFile(const std::string &path, Info &out, std::string &err)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        err = "cannot open " + path;
        return false;
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return parse(bytes.data(), bytes.size(), out, err);
}

} // namespace pe
