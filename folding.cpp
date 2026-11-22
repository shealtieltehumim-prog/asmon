/****************************************************************************
** Asmon — fork of SASM (GPL-3.0).
** Modifications © 2026 Asmon contributors.
****************************************************************************/
#include "folding.h"

#include <QHash>
#include <QRegularExpression>
#include <algorithm>

namespace Folding {

namespace {

//! the code of a line without its comment (';' outside quotes)
QString codePart(const QString &line)
{
    QChar quote;
    for (int i = 0; i < line.size(); ++i) {
        const QChar c = line.at(i);
        if (!quote.isNull()) {
            if (c == quote)
                quote = QChar();
            continue;
        }
        if (c == '"' || c == '\'' || c == '`')
            quote = c;
        else if (c == ';')
            return line.left(i);
    }
    return line;
}

bool isCommentOnly(const QString &trimmed)
{
    return trimmed.startsWith(';') || trimmed.startsWith('#') || trimmed.startsWith("//");
}

struct Pair {
    QRegularExpression open, close;
};

const QVector<Pair> &pairs()
{
    static const auto ci = QRegularExpression::CaseInsensitiveOption;
    static const QVector<Pair> p = {
        { QRegularExpression("^\\s*%i?macro\\b", ci), QRegularExpression("^\\s*%endmacro\\b", ci) },
        { QRegularExpression("^\\s*%if\\w*\\b", ci), QRegularExpression("^\\s*%endif\\b", ci) },
        { QRegularExpression("^\\s*%rep\\b", ci), QRegularExpression("^\\s*%endrep\\b", ci) },
        { QRegularExpression("^\\s*struc\\b", ci), QRegularExpression("^\\s*endstruc\\b", ci) },
        { QRegularExpression("^\\s*istruc\\b", ci), QRegularExpression("^\\s*iend\\b", ci) },
        { QRegularExpression("^\\s*\\.macro\\b", ci), QRegularExpression("^\\s*\\.endm(acro)?\\b", ci) },
        { QRegularExpression("^\\s*\\.if\\w*\\b", ci), QRegularExpression("^\\s*\\.endif\\b", ci) },
        { QRegularExpression("^\\s*\\.(rept?|irpc?)\\b", ci), QRegularExpression("^\\s*\\.endr\\b", ci) },
        { QRegularExpression("^\\s*[\\w.$?@]+\\s+proc\\b", ci), QRegularExpression("^\\s*[\\w.$?@]+\\s+endp\\b", ci) },
        { QRegularExpression("^\\s*proc\\b", ci), QRegularExpression("^\\s*endp\\b", ci) },
        { QRegularExpression("^\\s*[\\w.$?@]+\\s+macro\\b", ci), QRegularExpression("^\\s*endm\\b", ci) },
        { QRegularExpression("^\\s*[\\w.$?@]+\\s+struct?\\b", ci), QRegularExpression("^\\s*[\\w.$?@]+\\s+ends\\b", ci) },
    };
    return p;
}

} // namespace

QVector<Range> compute(const QStringList &lines)
{
    static const auto ci = QRegularExpression::CaseInsensitiveOption;
    static const QRegularExpression sectionRe(
        "^\\s*(section|segment)\\b"
        "|^\\s*\\.(section|text|data|bss|rodata|code|const)\\b"
        "|^\\s*[\\w.$?@]+\\s+segment\\b", ci);
    //! a global label at the start of the line; local (.x), macro-local (%%x) and "x::" are not functions
    static const QRegularExpression labelRe("^[A-Za-z_?@$][\\w$?@]*\\s*:(?!:)");

    const int n = lines.size();
    QVector<Range> out;
    QVector<QString> code(n);
    QVector<bool> blank(n), comment(n);
    for (int i = 0; i < n; ++i) {
        const QString t = lines.at(i).trimmed();
        blank[i] = t.isEmpty();
        comment[i] = !blank[i] && isCommentOnly(t);
        code[i] = comment[i] ? QString() : codePart(lines.at(i));
    }

    //! a section / function ends before the next one; blank lines and the comment header of the
    //! next one are not part of it
    auto trimmedEnd = [&](int from, int nextStart) {
        int e = nextStart - 1;
        while (e > from && (blank[e] || comment[e]))
            --e;
        return e;
    };

    //! sections and global labels
    QVector<int> sections, labels;
    for (int i = 0; i < n; ++i) {
        if (comment[i] || blank[i])
            continue;
        if (sectionRe.match(code[i]).hasMatch())
            sections << i;
        else if (labelRe.match(code[i]).hasMatch())
            labels << i;
    }
    for (int k = 0; k < sections.size(); ++k) {
        const int s = sections[k];
        const int next = k + 1 < sections.size() ? sections[k + 1] : n;
        const int e = trimmedEnd(s, next);
        if (e > s)
            out << Range{ s, e };
    }
    for (int k = 0; k < labels.size(); ++k) {
        const int s = labels[k];
        int next = k + 1 < labels.size() ? labels[k + 1] : n;
        for (int sec : sections)
            if (sec > s && sec < next) {
                next = sec;
                break;
            }
        const int e = trimmedEnd(s, next);
        if (e > s)
            out << Range{ s, e };
    }

    //! keyword pairs and { } braces
    struct Open { int kind; int line; };
    QVector<Open> stack;
    const QVector<Pair> &pp = pairs();
    for (int i = 0; i < n; ++i) {
        if (comment[i] || blank[i])
            continue;
        const QString &c = code[i];
        bool matched = false;
        for (int k = 0; k < pp.size() && !matched; ++k) {
            if (pp[k].close.match(c).hasMatch()) {
                for (int j = stack.size() - 1; j >= 0; --j)
                    if (stack[j].kind == k) {
                        if (i > stack[j].line)
                            out << Range{ stack[j].line, i };
                        stack.remove(j, stack.size() - j);
                        break;
                    }
                matched = true;
            }
        }
        for (int k = 0; k < pp.size() && !matched; ++k)
            if (pp[k].open.match(c).hasMatch()) {
                stack << Open{ k, i };
                matched = true;
            }
        if (matched)
            continue;
        QChar quote;
        for (const QChar ch : c) {
            if (!quote.isNull()) {
                if (ch == quote)
                    quote = QChar();
                continue;
            }
            if (ch == '"' || ch == '\'' || ch == '`')
                quote = ch;
            else if (ch == '{')
                stack << Open{ -1, i };
            else if (ch == '}')
                for (int j = stack.size() - 1; j >= 0; --j)
                    if (stack[j].kind == -1) {
                        if (i > stack[j].line)
                            out << Range{ stack[j].line, i };
                        stack.remove(j, stack.size() - j);
                        break;
                    }
        }
    }

    //! comment blocks: two or more comment lines in a row
    for (int i = 0; i < n;) {
        if (!comment[i]) {
            ++i;
            continue;
        }
        int j = i;
        while (j + 1 < n && comment[j + 1])
            ++j;
        if (j > i)
            out << Range{ i, j };
        i = j + 1;
    }

    //! one region per start line: the longest
    std::sort(out.begin(), out.end(), [](const Range &a, const Range &b) {
        return a.start != b.start ? a.start < b.start : a.end > b.end;
    });
    QVector<Range> unique;
    for (const Range &r : out)
        if (unique.isEmpty() || unique.last().start != r.start)
            unique << r;
    return unique;
}

} // namespace Folding
