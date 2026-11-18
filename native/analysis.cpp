#include "analysis.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <unordered_set>

namespace ra {

// ----------------------------------------------------------------- Image

bool Image::load(const std::string &path, std::string &err)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        err = "cannot open " + path;
        return false;
    }
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return loadBytes(std::move(data), path, err);
}

bool Image::loadBytes(std::vector<uint8_t> data, const std::string &path, std::string &err)
{
    pe::Info info;
    if (!pe::parse(data.data(), data.size(), info, err))
        return false;
    bytes_ = std::move(data);
    pe_ = std::move(info);
    path_ = path;
    patches_.clear();
    return true;
}

int64_t Image::vaToOffset(uint64_t va) const
{
    if (!inImage(va))
        return -1;
    return pe_.rvaToOffset(uint32_t(va - base()));
}

int64_t Image::offsetToVa(uint64_t offset) const
{
    for (const pe::Section &s : pe_.sections)
        if (s.rawSize && offset >= s.rawOffset && offset < uint64_t(s.rawOffset) + s.rawSize)
            return int64_t(base() + s.rva + (offset - s.rawOffset));
    return -1;
}

const pe::Section *Image::sectionOfVa(uint64_t va) const
{
    return inImage(va) ? pe_.sectionOfRva(uint32_t(va - base())) : nullptr;
}

bool Image::isCode(uint64_t va) const
{
    const pe::Section *s = sectionOfVa(va);
    return s && s->executable();
}

size_t Image::readFile(uint64_t offset, uint8_t *buf, size_t size) const
{
    if (offset >= bytes_.size())
        return 0;
    const size_t n = std::min<size_t>(size, bytes_.size() - size_t(offset));
    std::memcpy(buf, bytes_.data() + offset, n);
    if (!patches_.empty())
        for (auto it = patches_.lower_bound(offset); it != patches_.end() && it->first < offset + n; ++it)
            buf[it->first - offset] = it->second;
    return n;
}

size_t Image::read(uint64_t va, uint8_t *buf, size_t size) const
{
    const int64_t off = vaToOffset(va);
    if (off < 0)
        return 0;
    // never read past the raw data of the section
    const pe::Section *s = sectionOfVa(va);
    const uint64_t avail = uint64_t(s->rawOffset) + s->rawSize - uint64_t(off);
    return readFile(uint64_t(off), buf, size_t(std::min<uint64_t>(size, avail)));
}

std::vector<uint8_t> Image::mapped() const
{
    std::vector<uint8_t> out(pe_.sizeOfImage, 0);
    const size_t headers = std::min<size_t>(bytes_.size(), out.size());
    // headers: up to the first section's raw data
    uint32_t headerEnd = pe_.sizeOfImage;
    for (const pe::Section &sec : pe_.sections)
        if (sec.rawOffset)
            headerEnd = std::min(headerEnd, sec.rawOffset);
    readFile(0, out.data(), std::min<size_t>(headers, headerEnd));
    for (const pe::Section &sec : pe_.sections) {
        if (sec.rva >= out.size() || !sec.rawSize)
            continue;
        const uint64_t n = std::min<uint64_t>({ sec.rawSize, sec.virtualSize ? sec.virtualSize : sec.rawSize,
                                                out.size() - sec.rva });
        readFile(sec.rawOffset, out.data() + sec.rva, size_t(n));
    }
    return out;
}

bool Image::patch(uint64_t offset, const std::vector<uint8_t> &bytes)
{
    if (offset + bytes.size() > bytes_.size())
        return false;
    for (size_t i = 0; i < bytes.size(); ++i) {
        if (bytes_[offset + i] == bytes[i])
            patches_.erase(offset + i);
        else
            patches_[offset + i] = bytes[i];
    }
    return true;
}

bool Image::patchVa(uint64_t va, const std::vector<uint8_t> &bytes)
{
    const int64_t off = vaToOffset(va);
    if (off < 0)
        return false;
    const pe::Section *s = sectionOfVa(va);
    if (uint64_t(off) + bytes.size() > uint64_t(s->rawOffset) + s->rawSize)
        return false;
    return patch(uint64_t(off), bytes);
}

bool Image::original(uint64_t offset, uint8_t &value) const
{
    if (offset >= bytes_.size())
        return false;
    value = bytes_[offset];
    return true;
}

bool Image::saveAs(const std::string &newPath, std::string &err) const
{
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!path_.empty() && fs::exists(newPath, ec) && fs::equivalent(newPath, path_, ec)) {
        err = "the patched file must be a new file: the original is never overwritten";
        return false;
    }
    std::vector<uint8_t> out = bytes_;
    for (const auto &p : patches_)
        out[size_t(p.first)] = p.second;
    std::ofstream f(newPath, std::ios::binary | std::ios::trunc);
    if (!f) {
        err = "cannot write " + newPath;
        return false;
    }
    f.write(reinterpret_cast<const char *>(out.data()), std::streamsize(out.size()));
    if (!f) {
        err = "writing " + newPath + " failed";
        return false;
    }
    return true;
}

std::string hexBytes(const uint8_t *data, size_t n)
{
    std::string s;
    char b[4];
    for (size_t i = 0; i < n; ++i) {
        std::snprintf(b, sizeof b, "%02x", data[i]);
        if (i)
            s += ' ';
        s += b;
    }
    return s;
}

// ----------------------------------------------------------------- Result

const Function *Result::functionAt(uint64_t va) const
{
    auto it = std::upper_bound(functions.begin(), functions.end(), va,
                               [](uint64_t v, const Function &f) { return v < f.va; });
    if (it == functions.begin())
        return nullptr;
    --it;
    return (va < it->va + std::max<uint32_t>(it->size, 1)) ? &*it : nullptr;
}

const Function *Result::functionStartingAt(uint64_t va) const
{
    auto it = std::lower_bound(functions.begin(), functions.end(), va,
                               [](const Function &f, uint64_t v) { return f.va < v; });
    return (it != functions.end() && it->va == va) ? &*it : nullptr;
}

std::vector<Xref> Result::xrefsTo(uint64_t va) const
{
    auto lo = std::lower_bound(xrefs.begin(), xrefs.end(), va, [](const Xref &x, uint64_t v) { return x.to < v; });
    std::vector<Xref> r;
    for (; lo != xrefs.end() && lo->to == va; ++lo)
        r.push_back(*lo);
    return r;
}

std::vector<Xref> Result::xrefsFrom(uint64_t va) const
{
    std::vector<Xref> r;
    for (const Xref &x : xrefs)
        if (x.from == va)
            r.push_back(x);
    return r;
}

const String *Result::stringAt(uint64_t va) const
{
    auto it = std::lower_bound(strings.begin(), strings.end(), va, [](const String &s, uint64_t v) { return s.va < v; });
    return (it != strings.end() && it->va == va) ? &*it : nullptr;
}

std::string Result::functionName(const Function &f) const
{
    auto u = userNames.find(f.va);
    if (u != userNames.end())
        return u->second;
    if (!f.name.empty())
        return f.name;
    char b[32];
    std::snprintf(b, sizeof b, "sub_%llx", (unsigned long long)f.va);
    return b;
}

std::string Result::nameAt(uint64_t va) const
{
    auto u = userNames.find(va);
    if (u != userNames.end())
        return u->second;
    if (const Function *f = functionStartingAt(va))
        return functionName(*f);
    auto im = imports.find(va);
    if (im != imports.end())
        return im->second;
    auto l = labels.find(va);
    if (l != labels.end())
        return l->second;
    if (const Function *f = functionAt(va)) {
        char b[24];
        std::snprintf(b, sizeof b, "+0x%llx", (unsigned long long)(va - f->va));
        return functionName(*f) + b;
    }
    return std::string();
}

// ----------------------------------------------------------------- analysis

namespace {

bool noReturnName(const std::string &n)
{
    static const char *names[] = { "ExitProcess", "exit", "_exit", "abort", "TerminateProcess", "RtlExitUserProcess",
                                   "__fastfail", "ExitThread", "_Exit", "quick_exit", "terminate" };
    std::string base = n;
    const size_t bang = base.rfind('!');
    if (bang != std::string::npos)
        base = base.substr(bang + 1);
    for (const char *k : names)
        if (base == k)
            return true;
    return false;
}

struct Analyzer {
    const Image &img;
    Result &res;
    std::map<uint64_t, Function> funcs;                  // by start
    std::unordered_set<uint64_t> decoded;                // instruction addresses seen
    std::unordered_set<uint64_t> explored;               // function starts already followed
    std::map<uint64_t, uint64_t> pdataEnd;               // function start -> end (exact)
    std::deque<uint64_t> work;
    std::set<std::pair<std::pair<uint64_t, uint64_t>, int>> xrefSet;
    int steps = 0;

    Analyzer(const Image &i, Result &r) : img(i), res(r) {}

    void addXref(uint64_t from, uint64_t to, Xref::Kind kind)
    {
        if (xrefSet.insert({ { to, from }, kind }).second) {
            Xref x;
            x.from = from;
            x.to = to;
            x.kind = kind;
            res.xrefs.push_back(x);
        }
    }

    void seed(uint64_t va, Function::Source src, const std::string &name = std::string())
    {
        if (!img.isCode(va))
            return;
        auto it = funcs.find(va);
        if (it != funcs.end()) {
            if (it->second.name.empty() && !name.empty())
                it->second.name = name;
            return;
        }
        Function f;
        f.va = va;
        f.source = src;
        f.name = name;
        funcs[va] = f;
        work.push_back(va);
    }

    //! Follows the code of one function from its start. Returns its extent.
    void explore(uint64_t start)
    {
        std::deque<uint64_t> blocks{ start };
        std::unordered_set<uint64_t> seen;          // this function's own instructions
        uint64_t high = start;
        const auto pd = pdataEnd.find(start);
        while (!blocks.empty()) {
            uint64_t va = blocks.front();
            blocks.pop_front();
            for (;;) {
                if (!img.isCode(va) || !seen.insert(va).second)
                    break;
                // flowing into the start of another function (after a call that never returns...)
                if (va != start && funcs.count(va))
                    break;
                decoded.insert(va);
                uint8_t buf[16];
                const size_t n = img.read(va, buf, sizeof buf);
                dis::Insn in;
                if (!n || !dis::decode(buf, n, va, img.is64(), in))
                    break;
                high = std::max(high, va + uint64_t(in.length));
                ++steps;
                record(in);
                if (in.ret)
                    break;
                if (in.call) {
                    if (in.hasTarget && img.isCode(in.target)) {
                        seed(in.target, Function::Call);
                        if (noReturnTarget(in.target))
                            break;
                    } else if (in.hasRip && res.imports.count(in.ripTarget) && noReturnName(res.imports[in.ripTarget])) {
                        break;
                    }
                } else if (in.jump) {
                    if (in.hasTarget) {
                        if (isOtherFunction(in.target, start)) {
                            seed(in.target, Function::Call);       // a tail call
                        } else if (img.isCode(in.target)) {
                            blocks.push_back(in.target);
                        }
                    }
                    if (!in.conditional)
                        break;
                } else if (in.text == "int3" || in.text == "ud2" || in.text == "hlt") {
                    break;
                }
                va += uint64_t(in.length);
            }
        }
        Function &f = funcs[start];
        if (pd != pdataEnd.end())
            f.size = uint32_t(pd->second - start);
        else
            f.size = uint32_t(high - start);
    }

    //! a call to a stub "jmp [import]" of a function that never returns (ExitProcess...)
    bool noReturnTarget(uint64_t va)
    {
        uint8_t buf[16];
        const size_t n = img.read(va, buf, sizeof buf);
        dis::Insn in;
        if (!n || !dis::decode(buf, n, va, img.is64(), in) || !in.jump || !in.hasRip)
            return false;
        auto im = res.imports.find(in.ripTarget);
        return im != res.imports.end() && noReturnName(im->second);
    }

    bool isOtherFunction(uint64_t target, uint64_t current) const
    {
        if (target == current)
            return false;
        if (funcs.count(target))
            return true;
        auto it = pdataEnd.upper_bound(target);
        if (it != pdataEnd.begin()) {
            --it;
            if (it->first != current && target >= it->first && target < it->second)
                return true;
        }
        return false;
    }

    void record(const dis::Insn &in)
    {
        if (in.hasTarget && (in.call || in.jump))
            addXref(in.address, in.target, in.call ? Xref::Call : Xref::Jump);
        if (in.hasRip) {
            if (in.call || in.jump)
                addXref(in.address, in.ripTarget, in.call ? Xref::Call : Xref::Jump);
            else
                addXref(in.address, in.ripTarget, Xref::Data);
        }
        if (in.hasMemAbs && img.inImage(in.memAbs))
            addXref(in.address, in.memAbs, Xref::Data);
        if (in.hasImm && img.inImage(in.imm) && in.imm >= img.base())
            addXref(in.address, in.imm, Xref::Data);
    }

    // a stub "jmp [rip+iat]": named after the import it forwards to
    void nameThunks()
    {
        for (auto &kv : funcs) {
            Function &f = kv.second;
            uint8_t buf[16];
            const size_t n = img.read(f.va, buf, sizeof buf);
            dis::Insn in;
            if (n && dis::decode(buf, n, f.va, img.is64(), in) && in.jump && !in.conditional && in.hasRip) {
                auto im = res.imports.find(in.ripTarget);
                if (im != res.imports.end()) {
                    f.thunk = true;
                    const std::string full = im->second;
                    const size_t bang = full.rfind('!');
                    if (f.name.empty() || f.name.rfind("sub_", 0) == 0)
                        f.name = bang == std::string::npos ? full : full.substr(bang + 1);
                }
            }
        }
    }

    //! Can the bytes at va be the start of a function: a few valid instructions that end in a ret/jmp?
    bool plausible(uint64_t va) const
    {
        uint64_t p = va;
        int valid = 0;
        for (int i = 0; i < 64; ++i) {
            uint8_t buf[16];
            const size_t n = img.read(p, buf, sizeof buf);
            dis::Insn in;
            if (!n || !dis::decode(buf, n, p, img.is64(), in))
                return false;
            if (in.text == "int3" || in.text == "ud2")
                return false;
            ++valid;
            if (in.ret || (in.jump && !in.conditional))
                return valid >= 2 || in.ret;
            p += uint64_t(in.length);
        }
        return valid >= 8;
    }

    void sweep()
    {
        for (int pass = 0; pass < 3; ++pass) {
            bool any = false;
            for (const pe::Section &s : img.info().sections) {
                if (!s.executable() || !s.rawSize)
                    continue;
                uint64_t va = img.base() + s.rva;
                const uint64_t end = va + std::min(s.virtualSize ? s.virtualSize : s.rawSize, s.rawSize);
                bool afterTerminator = true;
                while (va < end) {
                    // inside a known function (its code, jump tables, alignment): skip it
                    {
                        auto it = funcs.upper_bound(va);
                        if (it != funcs.begin()) {
                            --it;
                            if (va < it->second.va + it->second.size) {
                                va = it->second.va + it->second.size;
                                afterTerminator = true;
                                continue;
                            }
                        }
                        if (decoded.count(va)) {
                            ++va;
                            afterTerminator = true;
                            continue;
                        }
                    }
                    uint8_t buf[16];
                    const size_t n = img.read(va, buf, sizeof buf);
                    dis::Insn in;
                    if (!n || !dis::decode(buf, n, va, img.is64(), in)) {
                        ++va;
                        afterTerminator = false;
                        continue;
                    }
                    const bool padding = in.text == "int3" || in.text == "nop" || (buf[0] == 0 && n > 1 && buf[1] == 0)
                                         || in.text.rfind("nop ", 0) == 0 || in.text.rfind("xchg ax, ax", 0) == 0;
                    if (padding) {
                        va += uint64_t(in.length);
                        afterTerminator = true;
                        continue;
                    }
                    if (afterTerminator && plausible(va)) {
                        seed(va, Function::Sweep);
                        drain();
                        any = true;
                        if (decoded.count(va))
                            continue;      // va is now known code: the next round skips over it
                    }
                    afterTerminator = in.ret || (in.jump && !in.conditional);
                    va += uint64_t(in.length);
                }
            }
            if (!any)
                break;
        }
    }

    void drain()
    {
        while (!work.empty()) {
            const uint64_t s = work.front();
            work.pop_front();
            if (!explored.insert(s).second)
                continue;
            explore(s);
        }
    }
};

void findStrings(const Image &img, std::vector<String> &out)
{
    const size_t kMin = 4, kMaxText = 400;
    const size_t minAsciiData = 4, minAsciiCode = 8;
    auto printable = [](uint8_t c) { return (c >= 0x20 && c < 0x7f) || c == '\t' || c == '\r' || c == '\n'; };
    std::vector<uint8_t> data;
    for (const pe::Section &s : img.info().sections) {
        if (!s.rawSize || s.name == ".reloc" || s.name == ".pdata" || s.name == ".xdata"
                || s.name == ".idata" || s.name == ".edata")
            continue;
        const uint32_t size = std::min(s.rawSize, s.virtualSize ? s.virtualSize : s.rawSize);
        const size_t minAscii = s.executable() ? minAsciiCode : minAsciiData;
        data.resize(size);
        if (img.readFile(s.rawOffset, data.data(), size) != size)
            continue;
        // ASCII
        for (size_t i = 0; i < size;) {
            if (!printable(data[i])) {
                ++i;
                continue;
            }
            size_t j = i;
            while (j < size && printable(data[j]))
                ++j;
            size_t visible = 0;
            for (size_t q = i; q < j; ++q)
                visible += data[q] > 0x20;
            if (j - i >= minAscii && visible >= 3) {
                String st;
                st.offset = s.rawOffset + i;
                st.va = img.base() + s.rva + i;
                st.text.assign(reinterpret_cast<const char *>(&data[i]), std::min(j - i, kMaxText));
                out.push_back(std::move(st));
            }
            i = j;
        }
        // UTF-16LE: (printable-or-BMP, 0) pairs; Latin and Hebrew etc. are kept as UTF-8
        for (size_t i = 0; !s.executable() && i + 1 < size;) {
            auto wideChar = [&](size_t k, uint32_t &c) {
                c = uint32_t(data[k]) | (uint32_t(data[k + 1]) << 8);
                return (c >= 0x20 && c < 0x7f) || c == '\t' || c == '\r' || c == '\n' || (c >= 0x00a0 && c < 0xd800);
            };
            uint32_t c;
            // an ASCII-range first character, so random bytes do not look like text
            if (!wideChar(i, c) || c >= 0x80) {
                i += 2;
                continue;
            }
            size_t j = i;
            std::string text;
            size_t chars = 0;
            while (j + 1 < size && wideChar(j, c)) {
                if (c < 0x80) {
                    text += char(c);
                } else if (c < 0x800) {
                    text += char(0xC0 | (c >> 6));
                    text += char(0x80 | (c & 0x3F));
                } else {
                    text += char(0xE0 | (c >> 12));
                    text += char(0x80 | ((c >> 6) & 0x3F));
                    text += char(0x80 | (c & 0x3F));
                }
                ++chars;
                j += 2;
            }
            if (chars >= kMin) {
                String st;
                st.wide = true;
                st.offset = s.rawOffset + i;
                st.va = img.base() + s.rva + i;
                st.text = text.substr(0, kMaxText);
                out.push_back(std::move(st));
            }
            i = std::max(j, i + 2);
        }
    }
    std::sort(out.begin(), out.end(), [](const String &a, const String &b) { return a.va < b.va; });
    out.erase(std::unique(out.begin(), out.end(), [](const String &a, const String &b) { return a.va == b.va; }),
              out.end());
}

} // namespace

bool analyze(const Image &image, Result &out, std::string &err, const std::function<bool(int)> &progress)
{
    out = Result();
    const pe::Info &pe = image.info();
    if (pe.sections.empty()) {
        err = "the file has no sections";
        return false;
    }
    auto report = [&](int p) { return !progress || progress(p); };
    Analyzer a(image, out);
    const uint64_t base = image.base();

    // names
    for (const pe::Import &im : pe.imports) {
        std::string n = im.dll + "!" + (im.name.empty() ? "#" + std::to_string(im.ordinal) : im.name);
        out.imports[base + im.iatRva] = n;
    }
    for (const pe::Symbol &s : pe.symbols)
        if (!out.labels.count(base + s.rva))
            out.labels[base + s.rva] = s.name;
    for (const pe::Export &e : pe.exports)
        if (e.rva && !e.name.empty())
            out.labels[base + e.rva] = e.name;

    // seeds: entry point, exports, exported/function symbols, unwind table
    for (const pe::RuntimeFunction &rf : pe.runtimeFunctions)
        a.pdataEnd[base + rf.begin] = base + rf.end;
    if (pe.entryRva)
        a.seed(base + pe.entryRva, Function::Entry, out.labels.count(base + pe.entryRva) ? out.labels[base + pe.entryRva] : "entry");
    for (const pe::Export &e : pe.exports)
        if (e.rva)
            a.seed(base + e.rva, Function::Export, e.name);
    for (const pe::Symbol &s : pe.symbols)
        if ((s.external || s.function) && image.isCode(base + s.rva))
            a.seed(base + s.rva, Function::Symbol, s.name);
    for (const auto &p : a.pdataEnd)
        a.seed(p.first, Function::Unwind, out.labels.count(p.first) ? out.labels[p.first] : std::string());
    if (!report(5))
        return false;

    a.drain();
    if (!report(60))
        return false;
    // names for the labels that turned out to be function starts (local labels that are called)
    for (auto &kv : a.funcs)
        if (kv.second.name.empty() && out.labels.count(kv.first))
            kv.second.name = out.labels[kv.first];
    a.nameThunks();
    a.sweep();
    a.nameThunks();
    if (!report(85))
        return false;

    for (auto &kv : a.funcs) {
        if (kv.second.size == 0)
            kv.second.size = 1;
        out.functions.push_back(kv.second);
    }
    // a function that flowed on into one found later ends where that one starts
    for (size_t i = 0; i + 1 < out.functions.size(); ++i) {
        Function &f = out.functions[i];
        const uint64_t next = out.functions[i + 1].va;
        if (f.source != Function::Unwind && f.va + f.size > next)
            f.size = uint32_t(next - f.va);
    }
    std::sort(out.xrefs.begin(), out.xrefs.end(), [](const Xref &x, const Xref &y) {
        return x.to != y.to ? x.to < y.to : x.from < y.from;
    });
    findStrings(image, out.strings);
    report(100);
    return true;
}

// ----------------------------------------------------------------- disassembly

std::vector<Line> disassemble(const Image &image, const Result &result, uint64_t va, size_t maxInsns)
{
    std::vector<Line> lines;
    const dis::Symbolizer sym = [&result](uint64_t a) { return result.nameAt(a); };
    for (size_t k = 0; k < maxInsns; ++k) {
        uint8_t buf[16];
        const size_t n = image.read(va, buf, sizeof buf);
        if (!n)
            break;
        dis::Insn in;
        dis::decode(buf, n, va, image.is64(), in, &sym);
        Line l;
        l.va = va;
        l.length = std::max(1, in.length);
        l.bytes = hexBytes(buf, std::min<size_t>(size_t(l.length), n));
        l.text = in.text;
        l.comment = in.comment;
        l.functionStart = result.functionStartingAt(va) != nullptr;
        // a string a memory operand or an immediate points to
        const uint64_t target = in.hasRip ? in.ripTarget : in.hasMemAbs ? in.memAbs : in.hasImm ? in.imm : 0;
        if (target) {
            if (const String *s = result.stringAt(target)) {
                const std::string shown = s->text.size() > 60 ? s->text.substr(0, 60) + "..." : s->text;
                l.comment += std::string(l.comment.empty() ? "" : "  ") + (s->wide ? "L\"" : "\"") + shown + "\"";
            }
        }
        auto c = result.comments.find(va);
        if (c != result.comments.end())
            l.comment += std::string(l.comment.empty() ? "" : "  ") + "; " + c->second;
        const int64_t off = image.vaToOffset(va);
        if (off >= 0)
            for (int b = 0; b < l.length; ++b)
                if (image.patches().count(uint64_t(off) + uint64_t(b)))
                    l.patched = true;
        lines.push_back(std::move(l));
        va += uint64_t(std::max(1, in.length));
    }
    return lines;
}

} // namespace ra
