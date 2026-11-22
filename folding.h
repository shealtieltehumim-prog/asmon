/****************************************************************************
** Asmon — fork of SASM (GPL-3.0).
** Modifications © 2026 Asmon contributors.
****************************************************************************/
#ifndef FOLDING_H
#define FOLDING_H

#include <QString>
#include <QStringList>
#include <QTextBlockUserData>
#include <QVector>

/**
 * @file folding.h
 * Code folding for assembly (NASM, YASM, FASM, GAS, MASM). The regions are found from the text alone,
 * so they need no assembler and work while the code does not build:
 *   - a section (section/segment, .text/.data/.bss, .code/.data) up to the next section;
 *   - a global label (`name:` at the start of a line, not a local `.name:`) up to the next global label
 *     or section: the "functions" of an assembly file;
 *   - blocks with an end keyword: %macro/%endmacro, %if.../%endif, %rep/%endrep, struc/endstruc,
 *     istruc/iend, .macro/.endm, MASM proc/endp, macro/endm, struct/ends, FASM proc/endp, { };
 *   - two or more comment lines in a row.
 * A region is a range of lines [start, end] (block numbers, end > start); the start line stays visible
 * when the region is folded.
 */
namespace Folding {

struct Range {
    int start = 0;
    int end = 0;
    bool operator==(const Range &o) const { return start == o.start && end == o.end; }
};

//! all foldable regions of the text (one line per element), sorted by start; nested regions are kept,
//! one region per start line (the longest)
QVector<Range> compute(const QStringList &lines);

//! the folded flag of a start line travels with its text block when lines are inserted above it
class Mark : public QTextBlockUserData
{
public:
    bool folded = false;
};

} // namespace Folding

#endif
