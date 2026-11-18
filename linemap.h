/****************************************************************************
** Asmon — GPL-3.0.
**
** Shared by the debug backends: which labels of a module's source can be used to
** find where the module's code ended up in the running program.
****************************************************************************/
#ifndef ASMON_LINEMAP_H
#define ASMON_LINEMAP_H

#include "assembler.h"

#include <QPair>
#include <QString>
#include <QVector>

//! Labels in code followed by a mapped line (no section switch between), at most six, global
//! ones first (they are unique in the program). second = address in the module's listing.
QVector<QPair<QString, quint64>> moduleLabelCandidates(const QString &sourceFile,
                                                       const QVector<Assembler::LineNum> &lines);

#endif // ASMON_LINEMAP_H
