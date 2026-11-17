/****************************************************************************
** Asmon — fork of SASM (GPL-3.0). Copyright © 2013 Dmitriy Manushin.
** Modifications © 2026 Asmon contributors.
**
** RegExp: a thin wrapper over QRegularExpression that keeps the small part
** of the old QRegExp API used by SASM (indexIn/matchedLength/cap/exactMatch/
** setMinimal/setCaseSensitivity). It lets the code base build on Qt 6
** without the Core5Compat module and without rewriting ~170 call sites.
** It converts implicitly to QRegularExpression, so QString::indexOf(),
** contains() and remove() keep working.
****************************************************************************/
#ifndef ASMON_REGEXP_H
#define ASMON_REGEXP_H

#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <QString>

class RegExp
{
public:
    RegExp() {}
    explicit RegExp(const QString &pattern, Qt::CaseSensitivity cs = Qt::CaseSensitive)
        : m_pattern(pattern), m_cs(cs) { rebuild(); }

    void setPattern(const QString &p) { m_pattern = p; rebuild(); }
    QString pattern() const { return m_pattern; }
    bool isEmpty() const { return m_pattern.isEmpty(); }

    void setCaseSensitivity(Qt::CaseSensitivity cs) { m_cs = cs; rebuild(); }
    Qt::CaseSensitivity caseSensitivity() const { return m_cs; }
    void setMinimal(bool minimal) { m_minimal = minimal; rebuild(); }
    bool isMinimal() const { return m_minimal; }

    // Returns the position of the first match at or after `from`, or -1.
    int indexIn(const QString &str, int from = 0) const
    {
        if (m_pattern.isEmpty() || from < 0 || from > str.size()) {
            m_match = QRegularExpressionMatch();
            return -1;
        }
        m_match = m_re.match(str, from);
        return m_match.hasMatch() ? int(m_match.capturedStart(0)) : -1;
    }

    int matchedLength() const
    {
        return m_match.hasMatch() ? int(m_match.capturedLength(0)) : -1;
    }

    QString cap(int n = 0) const { return m_match.hasMatch() ? m_match.captured(n) : QString(); }

    bool exactMatch(const QString &str) const
    {
        QRegularExpression full(QRegularExpression::anchoredPattern(m_pattern), m_re.patternOptions());
        m_match = full.match(str);
        return m_match.hasMatch();
    }

    operator QRegularExpression() const { return m_re; }
    const QRegularExpression &regularExpression() const { return m_re; }

private:
    void rebuild()
    {
        QRegularExpression::PatternOptions o = QRegularExpression::NoPatternOption;
        if (m_cs == Qt::CaseInsensitive)
            o |= QRegularExpression::CaseInsensitiveOption;
        if (m_minimal)
            o |= QRegularExpression::InvertedGreedinessOption;
        m_re = QRegularExpression(m_pattern, o);
    }

    QString m_pattern;
    Qt::CaseSensitivity m_cs = Qt::CaseSensitive;
    bool m_minimal = false;
    QRegularExpression m_re;
    mutable QRegularExpressionMatch m_match;
};

#endif // ASMON_REGEXP_H
