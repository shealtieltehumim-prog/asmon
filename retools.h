/****************************************************************************
** Asmon — fork of SASM (GPL-3.0). Copyright © 2013 Dmitriy Manushin.
** Modifications © 2026 Asmon contributors.
**
** ReTools: external reverse-engineering tools Asmon can open a binary in.
** On Windows they are bundled under <Asmon>/tools/ (optional installer
** components); on Linux they are looked up in PATH. Every path can be
** overridden in Asmon.ini ([re] ghidra=..., cutter=..., x64dbg=..., imhex=...).
****************************************************************************/
#ifndef ASMON_RETOOLS_H
#define ASMON_RETOOLS_H

#include <QString>
#include <QStringList>

namespace ReTools {

struct Tool {
    QString id;        //!< "ghidra", "cutter", "x64dbg", "imhex"
    QString name;      //!< shown in the menu
    bool opensFiles;   //!< false: the tool cannot open a binary from the command line
};

QList<Tool> all();

//! Full path of the tool's launcher, or empty if it is not installed.
QString find(const QString &id, bool target32bit = false);

//! Starts the tool on a file (or without one if file is empty).
//! Returns false and sets *error when it cannot be started.
bool launch(const QString &id, const QString &file, bool target32bit, QString *error);

} // namespace ReTools

#endif // ASMON_RETOOLS_H
