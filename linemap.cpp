#include "linemap.h"

#include <QFile>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>

QVector<QPair<QString, quint64>> moduleLabelCandidates(const QString &sourceFile,
                                                       const QVector<Assembler::LineNum> &lines)
{
    QFile src(sourceFile);
    QStringList text;
    if (src.open(QIODevice::ReadOnly))
        text = QString::fromUtf8(src.readAll()).split('\n');
    //! labels in code followed by a mapped line (no section switch between):
    //! global ones first, they are unique in the program
    static const QRegularExpression labelRe("^\\s*([A-Za-z_?$][\\w$?@#~]*)\\s*:");
    static const QRegularExpression globalRe("^\\s*global\\s+(.+)$", QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression sectionRe("^\\s*\\[?\\s*(section|segment)\\b", QRegularExpression::CaseInsensitiveOption);
    QSet<QString> globals;
    for (const QString &l : text) {
        const QRegularExpressionMatch g = globalRe.match(l.section(';', 0, 0));
        if (g.hasMatch())
            for (QString n : g.captured(1).split(','))
                globals.insert(n.trimmed().section(':', 0, 0).trimmed());
    }
    QVector<QPair<QString, quint64>> globalCandidates, otherCandidates;
    for (int i = 0; i < text.size(); ++i) {
        const QRegularExpressionMatch lm = labelRe.match(text.at(i));
        if (!lm.hasMatch())
            continue;
        const int sourceLine = i + 1;
        const Assembler::LineNum *first = nullptr;
        for (const Assembler::LineNum &ln : lines)
            if (int(ln.numInCode) >= sourceLine) { first = &ln; break; }
        if (!first)
            continue;
        bool sectionBetween = false;
        for (int k = sourceLine; k < int(first->numInCode) - 1 && k < text.size(); ++k)
            if (sectionRe.match(text.at(k)).hasMatch()) { sectionBetween = true; break; }
        if (sectionBetween)
            continue;
        const QString label = lm.captured(1);
        (globals.contains(label) ? globalCandidates : otherCandidates) << qMakePair(label, first->numInMem);
    }
    QVector<QPair<QString, quint64>> candidates = globalCandidates + otherCandidates;
    if (candidates.size() > 6)
        candidates.resize(6);
    return candidates;
}
