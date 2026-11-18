/****************************************************************************
** Asmon — GPL-3.0.
**
** Reverse-engineering core: a PE image in memory, function discovery
** (symbols, exports, entry point, the x64 exception table, recursive descent
** from calls and jumps, and a linear sweep for what is left), cross-references,
** strings (ASCII and UTF-16), disassembly with names, and byte patches that are
** saved to a NEW file. Pure C++17, no Qt and no Windows API.
****************************************************************************/
#ifndef ASMON_ANALYSIS_H
#define ASMON_ANALYSIS_H

#include "disasm.h"
#include "pe.h"

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace ra {

class Image {
public:
    bool load(const std::string &path, std::string &err);
    bool loadBytes(std::vector<uint8_t> data, const std::string &path, std::string &err);

    const pe::Info &info() const { return pe_; }
    const std::string &path() const { return path_; }
    uint64_t base() const { return pe_.imageBase; }
    bool is64() const { return pe_.is64; }
    size_t fileSize() const { return bytes_.size(); }

    int64_t vaToOffset(uint64_t va) const;            //!< -1: the address is not backed by the file
    int64_t offsetToVa(uint64_t offset) const;        //!< -1: the offset is not inside a section
    bool inImage(uint64_t va) const { return va >= base() && va < base() + pe_.sizeOfImage; }
    const pe::Section *sectionOfVa(uint64_t va) const;
    bool isCode(uint64_t va) const;                   //!< inside a section marked executable

    //! Reads at most size bytes at an address (patches included); returns how many (0: none).
    size_t read(uint64_t va, uint8_t *buf, size_t size) const;
    //! Reads from the file by offset (patches included).
    size_t readFile(uint64_t offset, uint8_t *buf, size_t size) const;

    //! The program as it is laid out in memory (headers and sections at their addresses), patches included.
    std::vector<uint8_t> mapped() const;

    // patches: byte changes that are kept in memory and written to a new file
    bool patch(uint64_t offset, const std::vector<uint8_t> &bytes);
    bool patchVa(uint64_t va, const std::vector<uint8_t> &bytes);
    void clearPatches() { patches_.clear(); }
    void revertPatch(uint64_t offset) { patches_.erase(offset); }
    const std::map<uint64_t, uint8_t> &patches() const { return patches_; }
    bool original(uint64_t offset, uint8_t &value) const;
    //! Writes the file with the patches into newPath. Refuses to overwrite the original file.
    bool saveAs(const std::string &newPath, std::string &err) const;

private:
    std::vector<uint8_t> bytes_;
    pe::Info pe_;
    std::string path_;
    std::map<uint64_t, uint8_t> patches_;
};

struct Function {
    enum Source { Entry, Export, Symbol, Unwind, Call, Sweep };
    uint64_t va = 0;
    uint32_t size = 0;
    std::string name;
    Source source = Call;
    bool thunk = false;      //!< jmp [import]: a stub that calls a function of a DLL
};

struct Xref {
    enum Kind { Call, Jump, Data };
    uint64_t from = 0, to = 0;
    Kind kind = Call;
};

struct String {
    uint64_t va = 0;
    uint64_t offset = 0;
    bool wide = false;
    std::string text;        //!< UTF-8, cut at 400 characters
};

struct Result {
    std::vector<Function> functions;                 //!< sorted by address
    std::vector<Xref> xrefs;                         //!< sorted by target, then by source
    std::vector<String> strings;                     //!< sorted by address
    std::map<uint64_t, std::string> imports;         //!< import slot address -> "kernel32.dll!ExitProcess"
    std::map<uint64_t, std::string> labels;          //!< names from the symbol table and exports (any address)
    std::map<uint64_t, std::string> userNames;       //!< names the user chose (win over everything)
    std::map<uint64_t, std::string> comments;        //!< the user's comments

    const Function *functionAt(uint64_t va) const;   //!< the function that contains the address
    const Function *functionStartingAt(uint64_t va) const;
    std::vector<Xref> xrefsTo(uint64_t va) const;
    std::vector<Xref> xrefsFrom(uint64_t va) const;
    const String *stringAt(uint64_t va) const;
    //! "main", "main+0x14", "kernel32.dll!ExitProcess" or "" (nothing is known about the address)
    std::string nameAt(uint64_t va) const;
    //! the name of a function (user name, symbol, or sub_<address>)
    std::string functionName(const Function &f) const;
};

//! Finds functions, cross-references and strings. progress(percent) returns false to cancel.
bool analyze(const Image &image, Result &out, std::string &err,
             const std::function<bool(int)> &progress = std::function<bool(int)>());

struct Line {
    uint64_t va = 0;
    int length = 0;
    std::string bytes;       //!< "48 89 e5"
    std::string text;        //!< Intel syntax, branch targets and calls by name
    std::string comment;     //!< where a memory operand points, string previews, the user's comment
    bool functionStart = false;
    bool patched = false;    //!< one of its bytes was changed by a patch
};

//! Disassembles up to maxInsns instructions starting at an address (stops at the end of the code).
std::vector<Line> disassemble(const Image &image, const Result &result, uint64_t va, size_t maxInsns);

//! "48 89 e5" for a byte range
std::string hexBytes(const uint8_t *data, size_t n);

} // namespace ra

#endif // ASMON_ANALYSIS_H
