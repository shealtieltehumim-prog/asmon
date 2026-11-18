/****************************************************************************
** Asmon — fork of SASM (GPL-3.0). Copyright © 2013 Dmitriy Manushin.
** Modifications © 2026 Asmon contributors.
****************************************************************************/
#include "symbols.h"

#include <QCoreApplication>
#include <QRegularExpression>

namespace Symbols {

static bool isIdentChar(QChar c)
{
    return c.isLetterOrNumber() || c == '_' || c == '.' || c == '$' || c == '?' || c == '@' || c == '#'
           || c == '~';
}

QString codePart(const QString &line)
{
    QString out = line;
    QChar quote;
    for (int i = 0; i < out.size(); ++i) {
        const QChar c = out.at(i);
        if (!quote.isNull()) {
            if (c == quote)
                quote = QChar();
            else
                out[i] = ' ';
        } else if (c == '\'' || c == '"' || c == '`') {
            quote = c;
        } else if (c == ';' || (c == '#' && (i == 0 || out.at(i - 1).isSpace()))) {
            //! ';' comments (NASM/FASM); '#' at a word start: GAS comments
            out.truncate(i);
            break;
        }
    }
    return out;
}

QString wordAt(const QString &line, int pos, int *start)
{
    if (pos > line.size())
        pos = line.size();
    int b = pos, e = pos;
    while (b > 0 && isIdentChar(line.at(b - 1)))
        --b;
    while (e < line.size() && isIdentChar(line.at(e)))
        ++e;
    if (start)
        *start = b;
    QString w = line.mid(b, e - b);
    if (!w.isEmpty() && w.at(0).isDigit())
        return QString(); // a number
    return w;
}

static const QRegularExpression &labelRe()
{
    static const QRegularExpression re("^\\s*([A-Za-z_.?$@][\\w.?$@#~]*)\\s*:(?!:)");
    return re;
}
static const QRegularExpression &dataRe()
{
    static const QRegularExpression re(
        "^\\s*([A-Za-z_?$@][\\w.?$@#~]*)\\s+(db|dw|dd|dq|dt|do|dy|dz|resb|resw|resd|resq|rest|reso|resy|"
        "times|rb|rw|rd|rq|du)\\b", QRegularExpression::CaseInsensitiveOption);
    return re;
}
static const QRegularExpression &constRe()
{
    static const QRegularExpression re(
        "^\\s*(?:([A-Za-z_.?$@][\\w.?$@#~]*)\\s*:?\\s+equ\\b|%(?:i?x?define|assign|i?define)\\s+([A-Za-z_.?$@][\\w.?$@#~]*)"
        "|\\.(?:equ|set|equiv)\\s+([A-Za-z_.][\\w.$]*)\\s*,|([A-Za-z_][\\w]*)\\s*=)",
        QRegularExpression::CaseInsensitiveOption);
    return re;
}
static const QRegularExpression &macroRe()
{
    static const QRegularExpression re(
        "^\\s*(?:%i?macro|\\.macro|macro)\\s+([A-Za-z_.?$@][\\w.?$@#~]*)",
        QRegularExpression::CaseInsensitiveOption);
    return re;
}

QList<Symbol> scan(const QString &text, const QString &file)
{
    QList<Symbol> out;
    const QStringList lines = text.split('\n');
    QString parent;
    for (int i = 0; i < lines.size(); ++i) {
        const QString code = codePart(lines.at(i));
        if (code.trimmed().isEmpty())
            continue;
        QRegularExpressionMatch m = macroRe().match(code);
        if (m.hasMatch()) {
            out << Symbol{ m.captured(1), QString(), file, i + 1, Macro };
            continue;
        }
        m = constRe().match(code);
        if (m.hasMatch()) {
            QString n;
            for (int g = 1; g <= 4 && n.isEmpty(); ++g)
                n = m.captured(g);
            if (!n.isEmpty()) {
                out << Symbol{ n, QString(), file, i + 1, Constant };
                continue;
            }
        }
        m = labelRe().match(code);
        if (m.hasMatch()) {
            const QString n = m.captured(1);
            if (n.startsWith('.') && !n.startsWith("..")) {
                out << Symbol{ n, parent, file, i + 1, LocalLabel };
            } else {
                parent = n;
                out << Symbol{ n, QString(), file, i + 1, Label };
            }
            continue;
        }
        m = dataRe().match(code);
        if (m.hasMatch()) {
            const QString n = m.captured(1);
            if (n.startsWith('.')) {
                out << Symbol{ n, parent, file, i + 1, LocalLabel };
            } else {
                parent = n;
                out << Symbol{ n, QString(), file, i + 1, Data };
            }
        }
    }
    return out;
}

QString parentLabel(const QStringList &lines, int lineIndex)
{
    for (int i = qMin(lineIndex, int(lines.size()) - 1); i >= 0; --i) {
        const QString code = codePart(lines.at(i));
        QRegularExpressionMatch m = labelRe().match(code);
        if (!m.hasMatch())
            m = dataRe().match(code);
        if (m.hasMatch() && !m.captured(1).startsWith('.'))
            return m.captured(1);
    }
    return QString();
}

bool findDefinition(const QList<Source> &sources, int sourceIndex, int lineIndex,
                    const QString &name, Symbol *result)
{
    if (name.isEmpty())
        return false;
    //! "fact.base" (full name of a local label) or ".base" used under fact
    QString wantParent, wantLocal = name;
    if (name.startsWith('.') && !name.startsWith("..")) {
        if (sourceIndex >= 0 && sourceIndex < sources.size())
            wantParent = parentLabel(sources.at(sourceIndex).text.split('\n'), lineIndex);
    } else if (name.contains('.') && !name.startsWith('.')) {
        const int dot = name.indexOf('.');
        wantParent = name.left(dot);
        wantLocal = name.mid(dot);
    }
    //! the current file first, then the others
    QList<int> order;
    if (sourceIndex >= 0)
        order << sourceIndex;
    for (int i = 0; i < sources.size(); ++i)
        if (i != sourceIndex)
            order << i;
    for (bool caseSensitive : { true, false }) {
        for (int idx : order) {
            const QList<Symbol> syms = scan(sources.at(idx).text, sources.at(idx).file);
            for (const Symbol &s : syms) {
                const Qt::CaseSensitivity cs = caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
                if (s.kind == LocalLabel) {
                    if (wantLocal.startsWith('.') && s.name.compare(wantLocal, cs) == 0
                            && (wantParent.isEmpty() || s.parent.compare(wantParent, cs) == 0)) {
                        *result = s;
                        return true;
                    }
                } else if (s.name.compare(name, cs) == 0) {
                    *result = s;
                    return true;
                }
            }
        }
    }
    return false;
}

QList<Reference> findReferences(const QList<Source> &sources, const QString &name)
{
    QList<Reference> out;
    if (name.isEmpty())
        return out;
    const QRegularExpression re("(?<![\\w.?$@#~])" + QRegularExpression::escape(name) + "(?![\\w?$@#~])");
    for (const Source &src : sources) {
        const QStringList lines = src.text.split('\n');
        for (int i = 0; i < lines.size(); ++i) {
            const QString code = codePart(lines.at(i));
            auto it = re.globalMatch(code);
            while (it.hasNext()) {
                const QRegularExpressionMatch m = it.next();
                out << Reference{ src.file, i + 1, int(m.capturedStart()), lines.at(i) };
            }
        }
    }
    return out;
}

QStringList includes(const QString &text)
{
    static const QRegularExpression re(
        "^\\s*(?:%include|\\.include|include)\\s+[\"'<]([^\"'>]+)[\"'>]",
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::MultilineOption);
    QStringList out;
    auto it = re.globalMatch(text);
    while (it.hasNext())
        out << it.next().captured(1);
    return out;
}

QString kindName(Kind k)
{
    switch (k) {
    case Label: return QCoreApplication::translate("Symbols", "label");
    case LocalLabel: return QCoreApplication::translate("Symbols", "local label");
    case Data: return QCoreApplication::translate("Symbols", "data");
    case Constant: return QCoreApplication::translate("Symbols", "constant");
    case Macro: return QCoreApplication::translate("Symbols", "macro");
    }
    return QString();
}

} // namespace Symbols
