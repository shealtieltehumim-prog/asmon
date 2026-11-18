/****************************************************************************
** Asmon — fork of SASM (GPL-3.0). Copyright © 2013 Dmitriy Manushin.
** Modifications © 2026 Asmon contributors.
**
** Symbols: a light scanner for assembly sources (NASM, GAS, FASM syntax):
** labels, local labels (.name, scoped to the label before them), data
** labels (msg db ...), constants (equ / %define / %assign / .equ / .set)
** and macros (%macro / .macro / macro). Used by go to definition, find
** references, go to symbol and auto-completion.
****************************************************************************/
#ifndef ASMON_SYMBOLS_H
#define ASMON_SYMBOLS_H

#include <QList>
#include <QString>
#include <QStringList>

namespace Symbols {

enum Kind { Label, LocalLabel, Data, Constant, Macro };

struct Symbol {
    QString name;     //!< as written (".base" for a local label)
    QString parent;   //!< for a local label: the label it belongs to
    QString file;     //!< absolute path, or empty for an unsaved buffer
    int line = 0;     //!< 1-based
    Kind kind = Label;
};

struct Source {
    QString file;     //!< absolute path ("" = unsaved)
    QString text;
};

//! Every symbol defined in text.
QList<Symbol> scan(const QString &text, const QString &file);

//! The code part of a line: without the comment and with strings blanked
//! (same length), so positions stay valid.
QString codePart(const QString &line);

//! The identifier at position pos of line ("" if none).
QString wordAt(const QString &line, int pos, int *start = nullptr);

//! The global label a line belongs to (for local labels), scanning upwards.
QString parentLabel(const QStringList &lines, int lineIndex);

//! Where name (used at line lineIndex of sources[sourceIndex]) is defined.
//! Returns false if no definition is found.
bool findDefinition(const QList<Source> &sources, int sourceIndex, int lineIndex,
                    const QString &name, Symbol *result);

struct Reference {
    QString file;
    int line;     //!< 1-based
    int column;   //!< 0-based
    QString text; //!< the line
};

//! Whole-word uses of name in all sources (comments excluded).
QList<Reference> findReferences(const QList<Source> &sources, const QString &name);

//! %include "x" / .include "x" / include 'x' file names in text.
QStringList includes(const QString &text);

QString kindName(Kind k);

} // namespace Symbols

#endif // ASMON_SYMBOLS_H
