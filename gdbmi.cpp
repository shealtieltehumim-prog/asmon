/****************************************************************************
** Asmon — fork of SASM (GPL-3.0). Copyright © 2013 Dmitriy Manushin.
** Modifications © 2026 Asmon contributors.
****************************************************************************/
#include "gdbmi.h"

static const MiValue kInvalid;

const MiValue &MiValue::operator[](const QString &key) const
{
    for (const auto &f : fields)
        if (f.first == key)
            return f.second;
    return kInvalid;
}

QString MiValue::get(const QString &key, const QString &def) const
{
    const MiValue &v = (*this)[key];
    return v.type == String ? v.str : def;
}

QList<MiValue> MiValue::elements() const
{
    if (!items.isEmpty())
        return items;
    QList<MiValue> out;
    for (const auto &f : fields)
        out << f.second;
    return out;
}

namespace GdbMi {

QString parseCString(const QString &s, int &pos, bool *ok)
{
    if (ok)
        *ok = false;
    if (pos >= s.size() || s[pos] != '"')
        return QString();
    ++pos;
    QByteArray bytes; // gdb escapes non-ASCII as octal UTF-8 bytes
    while (pos < s.size()) {
        const QChar c = s[pos];
        if (c == '"') {
            ++pos;
            if (ok)
                *ok = true;
            return QString::fromUtf8(bytes);
        }
        if (c == '\\' && pos + 1 < s.size()) {
            const QChar e = s[pos + 1];
            pos += 2;
            switch (e.unicode()) {
            case 'n': bytes += '\n'; break;
            case 't': bytes += '\t'; break;
            case 'r': bytes += '\r'; break;
            case 'a': bytes += '\a'; break;
            case 'b': bytes += '\b'; break;
            case 'f': bytes += '\f'; break;
            case 'v': bytes += '\v'; break;
            case 'e': bytes += '\x1b'; break;
            case '"': bytes += '"'; break;
            case '\\': bytes += '\\'; break;
            default:
                if (e >= '0' && e <= '7') {
                    int v = e.unicode() - '0';
                    for (int k = 0; k < 2 && pos < s.size() && s[pos] >= '0' && s[pos] <= '7'; ++k, ++pos)
                        v = v * 8 + (s[pos].unicode() - '0');
                    bytes += char(v);
                } else {
                    bytes += QString(e).toUtf8();
                }
            }
            continue;
        }
        bytes += QString(c).toUtf8();
        ++pos;
    }
    return QString::fromUtf8(bytes); // unterminated
}

static MiValue parseValue(const QString &s, int &pos);

static QString parseName(const QString &s, int &pos)
{
    const int start = pos;
    while (pos < s.size() && s[pos] != '=' && s[pos] != ',' && s[pos] != '}' && s[pos] != ']')
        ++pos;
    return s.mid(start, pos - start);
}

static MiValue parseTupleOrList(const QString &s, int &pos, QChar close, bool isList)
{
    MiValue v;
    v.type = isList ? MiValue::List : MiValue::Tuple;
    ++pos; // skip { or [
    while (pos < s.size() && s[pos] != close) {
        if (s[pos] == ',') {
            ++pos;
            continue;
        }
        if (isList && (s[pos] == '"' || s[pos] == '{' || s[pos] == '[')) {
            v.items << parseValue(s, pos);
        } else {
            const QString name = parseName(s, pos);
            if (pos < s.size() && s[pos] == '=') {
                ++pos;
                v.fields << qMakePair(name, parseValue(s, pos));
            } else {
                break; // malformed
            }
        }
    }
    if (pos < s.size())
        ++pos; // skip close
    return v;
}

static MiValue parseValue(const QString &s, int &pos)
{
    if (pos >= s.size())
        return MiValue();
    if (s[pos] == '"') {
        MiValue v;
        v.type = MiValue::String;
        v.str = parseCString(s, pos);
        return v;
    }
    if (s[pos] == '{')
        return parseTupleOrList(s, pos, '}', false);
    if (s[pos] == '[')
        return parseTupleOrList(s, pos, ']', true);
    return MiValue();
}

MiRecord parseLine(const QString &rawLine)
{
    MiRecord r;
    QString line = rawLine;
    while (line.endsWith('\r') || line.endsWith('\n'))
        line.chop(1);
    if (line.isEmpty())
        return r;
    if (line.trimmed() == "(gdb)") {
        r.kind = MiRecord::Prompt;
        return r;
    }
    int pos = 0;
    // stream records
    const QChar first = line[0];
    if ((first == '~' || first == '@' || first == '&') && line.size() > 1 && line[1] == '"') {
        r.kind = first == '~' ? MiRecord::ConsoleStream
               : first == '@' ? MiRecord::TargetStream : MiRecord::LogStream;
        pos = 1;
        bool ok = false;
        r.text = parseCString(line, pos, &ok);
        if (!ok || pos != line.size())
            r.kind = MiRecord::Invalid;
        return r;
    }
    // optional token
    while (pos < line.size() && line[pos].isDigit())
        ++pos;
    if (pos > 0)
        r.token = line.left(pos).toInt();
    if (pos >= line.size())
        return MiRecord();
    const QChar k = line[pos];
    if (k == '^') r.kind = MiRecord::Result;
    else if (k == '*') r.kind = MiRecord::ExecAsync;
    else if (k == '+') r.kind = MiRecord::StatusAsync;
    else if (k == '=') r.kind = MiRecord::NotifyAsync;
    else return MiRecord();
    ++pos;
    const int clsStart = pos;
    while (pos < line.size() && (line[pos].isLetterOrNumber() || line[pos] == '-' || line[pos] == '_'))
        ++pos;
    r.cls = line.mid(clsStart, pos - clsStart);
    if (r.cls.isEmpty())
        return MiRecord();
    // results: ,name=value,...
    r.results.type = MiValue::Tuple;
    while (pos < line.size()) {
        if (line[pos] != ',')
            return MiRecord(); // not MI after all
        ++pos;
        const QString name = parseName(line, pos);
        if (pos >= line.size() || line[pos] != '=')
            return MiRecord();
        ++pos;
        MiValue v = parseValue(line, pos);
        if (!v.isValid())
            return MiRecord();
        r.results.fields << qMakePair(name, v);
    }
    return r;
}

QString quote(const QString &s)
{
    QString out = "\"";
    for (const QChar c : s) {
        if (c == '"' || c == '\\')
            out += '\\';
        if (c == '\n') { out += "\\n"; continue; }
        out += c;
    }
    return out + "\"";
}

} // namespace GdbMi
