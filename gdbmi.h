/****************************************************************************
** Asmon — fork of SASM (GPL-3.0). Copyright © 2013 Dmitriy Manushin.
** Modifications © 2026 Asmon contributors.
**
** Parser for GDB/MI output records (GDB manual, "GDB/MI Output Syntax").
**   ^done,bkpt={number="1",addr="0x401000"}          result record
**   *stopped,reason="breakpoint-hit",frame={...}      exec async record
**   =thread-group-started,id="i1",pid="1234"         notify async record
**   ~"text\n"  @"target output"  &"log"             stream records
****************************************************************************/
#ifndef ASMON_GDBMI_H
#define ASMON_GDBMI_H

#include <QList>
#include <QPair>
#include <QString>

struct MiValue {
    enum Type { None, String, Tuple, List };
    Type type = None;
    QString str;                                 //!< String
    QList<QPair<QString, MiValue>> fields;       //!< Tuple, or List of results
    QList<MiValue> items;                        //!< List of plain values

    bool isValid() const { return type != None; }
    //! Field of a tuple (or of a list of results); invalid value if missing.
    const MiValue &operator[](const QString &key) const;
    //! Convenience: string of a field, or def.
    QString get(const QString &key, const QString &def = QString()) const;
    //! Elements of a list, whether written as values or as name=value results.
    QList<MiValue> elements() const;
};

struct MiRecord {
    enum Kind { Invalid, Result, ExecAsync, StatusAsync, NotifyAsync,
                ConsoleStream, TargetStream, LogStream, Prompt };
    Kind kind = Invalid;
    int token = -1;          //!< -1 when absent
    QString cls;             //!< "done", "error", "stopped", "thread-group-started"...
    MiValue results;         //!< Tuple with the record's results
    QString text;            //!< decoded C string for stream records
};

namespace GdbMi {
//! Parses one line of gdb's MI output. Lines that are not MI (e.g. output of
//! the debugged program sharing gdb's stdout) give Kind::Invalid.
MiRecord parseLine(const QString &line);
//! Decodes a C string starting at s[pos] == '"'; pos is moved past it.
QString parseCString(const QString &s, int &pos, bool *ok = nullptr);
//! Quotes a string for use as an MI command argument.
QString quote(const QString &s);
}

#endif // ASMON_GDBMI_H
