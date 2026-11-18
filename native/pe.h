/****************************************************************************
** Asmon — GPL-3.0.
**
** PE / COFF reader: headers, sections, the COFF symbol table (labels), imports
** and exports. Pure C++17, no Qt and no Windows API, so it is unit-tested on
** any platform. Used by the native debugger (where is main / a label?) and by
** the reverse-engineering view.
****************************************************************************/
#ifndef ASMON_PE_H
#define ASMON_PE_H

#include <cstdint>
#include <string>
#include <vector>

namespace pe {

struct Section {
    std::string name;
    uint32_t rva = 0, virtualSize = 0, rawOffset = 0, rawSize = 0, characteristics = 0;
    bool executable() const { return (characteristics & 0x20000000u) != 0; }
    bool writable() const { return (characteristics & 0x80000000u) != 0; }
    bool readable() const { return (characteristics & 0x40000000u) != 0; }
};

struct Symbol {
    std::string name;
    uint32_t rva = 0;       //!< relative to the image base
    int section = 0;        //!< 1-based section index (0: none)
    bool external = false;  //!< global
    bool function = false;  //!< COFF type says "function"
};

struct Import {
    std::string dll, name;
    uint16_t ordinal = 0;
    uint32_t iatRva = 0;    //!< where the loader writes the address
};

struct Export {
    std::string name;
    uint32_t rva = 0;
    uint16_t ordinal = 0;
};

struct Info {
    bool is64 = false;
    bool dll = false;
    uint16_t machine = 0, subsystem = 0;
    uint64_t imageBase = 0;
    uint32_t entryRva = 0, sizeOfImage = 0, timeStamp = 0, checksum = 0;
    std::vector<Section> sections;
    std::vector<Symbol> symbols;    //!< COFF symbols that survive linking (labels, functions)
    std::vector<Import> imports;
    std::vector<Export> exports;

    //! symbol by exact name (nullptr if there is none)
    const Symbol *findSymbol(const std::string &name) const;
    //! the section an RVA falls into (nullptr: headers or outside the image)
    const Section *sectionOfRva(uint32_t rva) const;
    //! file offset of an RVA (-1: not backed by the file)
    int64_t rvaToOffset(uint32_t rva) const;
    //! "name+0x10" for the nearest symbol at or below the RVA in the same section (empty: none)
    std::string symbolize(uint32_t rva) const;
};

//! Parses a PE image held in memory. Returns false and sets err for a damaged or non-PE file.
bool parse(const uint8_t *data, size_t size, Info &out, std::string &err);
bool parseFile(const std::string &path, Info &out, std::string &err);

} // namespace pe

#endif // ASMON_PE_H
