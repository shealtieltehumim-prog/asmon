#include "expr.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace expr {

namespace {

enum class T { End, Num, Ident, Op, LBracket, RBracket, LParen, RParen };

struct Tok {
    T t = T::End;
    std::string s;       // identifier or operator
    uint64_t n = 0;
};

bool isIdentStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_' || c == '.' || c == '?' || c == '@' || c == '$'; }
bool isIdentChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.' || c == '?' || c == '@' || c == '$' || c == '#' || c == '~'; }

bool parseNumber(const std::string &w, uint64_t &out)
{
    if (w.empty())
        return false;
    auto digits = [](const std::string &s, int base, uint64_t &v) {
        if (s.empty())
            return false;
        v = 0;
        for (char c : s) {
            int d;
            if (c >= '0' && c <= '9') d = c - '0';
            else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
            else return false;
            if (d >= base)
                return false;
            v = v * uint64_t(base) + uint64_t(d);
        }
        return true;
    };
    std::string lower;
    for (char c : w) lower.push_back(char(std::tolower(static_cast<unsigned char>(c))));
    if (lower.size() > 2 && lower[0] == '0' && lower[1] == 'x')
        return digits(lower.substr(2), 16, out);
    if (lower.size() > 2 && lower[0] == '0' && lower[1] == 'b')
        return digits(lower.substr(2), 2, out);
    if (lower.back() == 'h')
        return digits(lower.substr(0, lower.size() - 1), 16, out);
    if (lower.back() == 'b' && lower.size() > 1 && lower.find_first_not_of("01b") == std::string::npos)
        return digits(lower.substr(0, lower.size() - 1), 2, out);
    return digits(lower, 10, out);
}

class Parser {
public:
    Parser(const std::string &text, const Context &ctx) : ctx_(ctx) { tokenize(text); }
    bool run(uint64_t &out, std::string &err)
    {
        if (!err_.empty()) { err = err_; return false; }
        out = orExpr();
        if (err_.empty() && toks_[pos_].t != T::End)
            fail("unexpected '" + describe(toks_[pos_]) + "'");
        if (!err_.empty()) { err = err_; return false; }
        return true;
    }

private:
    const Context &ctx_;
    std::vector<Tok> toks_;
    size_t pos_ = 0;
    std::string err_;

    void fail(const std::string &m) { if (err_.empty()) err_ = m; }
    static std::string describe(const Tok &t) { return t.t == T::End ? "end" : t.s.empty() ? std::to_string(t.n) : t.s; }

    void tokenize(const std::string &text)
    {
        size_t i = 0;
        while (i < text.size()) {
            const char c = text[i];
            if (std::isspace(static_cast<unsigned char>(c))) { ++i; continue; }
            Tok t;
            if (std::isdigit(static_cast<unsigned char>(c))) {
                size_t j = i;
                while (j < text.size() && std::isalnum(static_cast<unsigned char>(text[j]))) ++j;
                if (!parseNumber(text.substr(i, j - i), t.n)) { fail("bad number '" + text.substr(i, j - i) + "'"); return; }
                t.t = T::Num;
                i = j;
            } else if (isIdentStart(c)) {
                size_t j = i + 1;
                while (j < text.size() && isIdentChar(text[j])) ++j;
                t.t = T::Ident;
                t.s = text.substr(i, j - i);
                if (t.s[0] == '$' && t.s.size() > 1) t.s.erase(0, 1);
                i = j;
            } else if (c == '[') { t.t = T::LBracket; t.s = "["; ++i; }
            else if (c == ']') { t.t = T::RBracket; t.s = "]"; ++i; }
            else if (c == '(') { t.t = T::LParen; t.s = "("; ++i; }
            else if (c == ')') { t.t = T::RParen; t.s = ")"; ++i; }
            else if (c == '\'' && i + 2 < text.size() && text[i + 2] == '\'') { t.t = T::Num; t.n = uint8_t(text[i + 1]); i += 3; }
            else {
                static const char *ops[] = { "<<", ">>", "==", "!=", "<=", ">=", "&&", "||",
                                             "+", "-", "*", "/", "%", "&", "|", "^", "~", "!", "<", ">" };
                bool found = false;
                for (const char *o : ops) {
                    const std::string os = o;
                    if (text.compare(i, os.size(), os) == 0) { t.t = T::Op; t.s = os; i += os.size(); found = true; break; }
                }
                if (!found) { fail(std::string("unexpected character '") + c + "'"); return; }
            }
            toks_.push_back(t);
        }
        toks_.push_back(Tok());
    }

    bool isOp(const char *o) const { return toks_[pos_].t == T::Op && toks_[pos_].s == o; }

    uint64_t orExpr()
    {
        uint64_t v = andExpr();
        while (err_.empty() && isOp("||")) { ++pos_; const uint64_t r = andExpr(); v = (v || r) ? 1 : 0; }
        return v;
    }
    uint64_t andExpr()
    {
        uint64_t v = bitOr();
        while (err_.empty() && isOp("&&")) { ++pos_; const uint64_t r = bitOr(); v = (v && r) ? 1 : 0; }
        return v;
    }
    uint64_t bitOr()
    {
        uint64_t v = bitXor();
        while (err_.empty() && isOp("|")) { ++pos_; v |= bitXor(); }
        return v;
    }
    uint64_t bitXor()
    {
        uint64_t v = bitAnd();
        while (err_.empty() && isOp("^")) { ++pos_; v ^= bitAnd(); }
        return v;
    }
    uint64_t bitAnd()
    {
        uint64_t v = equality();
        while (err_.empty() && isOp("&")) { ++pos_; v &= equality(); }
        return v;
    }
    uint64_t equality()
    {
        uint64_t v = relational();
        while (err_.empty() && (isOp("==") || isOp("!="))) {
            const bool eq = isOp("==");
            ++pos_;
            const uint64_t r = relational();
            v = ((v == r) == eq) ? 1 : 0;
        }
        return v;
    }
    uint64_t relational()
    {
        uint64_t v = shift();
        while (err_.empty() && (isOp("<") || isOp(">") || isOp("<=") || isOp(">="))) {
            const std::string o = toks_[pos_++].s;
            const uint64_t r = shift();
            const int64_t a = int64_t(v), b = int64_t(r);
            bool res = false;
            if (o == "<") res = a < b; else if (o == ">") res = a > b; else if (o == "<=") res = a <= b; else res = a >= b;
            v = res ? 1 : 0;
        }
        return v;
    }
    uint64_t shift()
    {
        uint64_t v = additive();
        while (err_.empty() && (isOp("<<") || isOp(">>"))) {
            const bool left = isOp("<<");
            ++pos_;
            const uint64_t r = additive();
            v = r >= 64 ? 0 : (left ? v << r : v >> r);
        }
        return v;
    }
    uint64_t additive()
    {
        uint64_t v = term();
        while (err_.empty() && (isOp("+") || isOp("-"))) {
            const bool plus = isOp("+");
            ++pos_;
            const uint64_t r = term();
            v = plus ? v + r : v - r;
        }
        return v;
    }
    uint64_t term()
    {
        uint64_t v = unary();
        while (err_.empty() && (isOp("*") || isOp("/") || isOp("%"))) {
            const std::string o = toks_[pos_++].s;
            const uint64_t r = unary();
            if (o == "*") v *= r;
            else if (r == 0) { fail("division by zero"); return 0; }
            else v = o == "/" ? v / r : v % r;
        }
        return v;
    }
    uint64_t unary()
    {
        if (isOp("-")) { ++pos_; return uint64_t(0) - unary(); }
        if (isOp("+")) { ++pos_; return unary(); }
        if (isOp("~")) { ++pos_; return ~unary(); }
        if (isOp("!")) { ++pos_; return unary() ? 0 : 1; }
        return primary();
    }
    uint64_t deref(int size)
    {
        // current token is '['
        ++pos_;
        const uint64_t addr = orExpr();
        if (toks_[pos_].t != T::RBracket) { fail("missing ']'"); return 0; }
        ++pos_;
        uint64_t v = 0;
        if (!err_.empty())
            return 0;
        if (!ctx_.mem || !ctx_.mem(addr, size, v)) {
            char b[40];
            std::snprintf(b, sizeof b, "cannot read memory at 0x%llx", (unsigned long long)addr);
            fail(b);
            return 0;
        }
        return v;
    }
    uint64_t primary()
    {
        const Tok t = toks_[pos_];
        switch (t.t) {
        case T::Num: ++pos_; return t.n;
        case T::LParen: {
            ++pos_;
            const uint64_t v = orExpr();
            if (toks_[pos_].t != T::RParen) { fail("missing ')'"); return 0; }
            ++pos_;
            return v;
        }
        case T::LBracket: return deref(8);
        case T::Ident: {
            ++pos_;
            std::string lower;
            for (char c : t.s) lower.push_back(char(std::tolower(static_cast<unsigned char>(c))));
            int size = lower == "byte" ? 1 : lower == "word" ? 2 : lower == "dword" ? 4 : lower == "qword" ? 8 : 0;
            if (size) {
                if (toks_[pos_].t == T::Ident) {   // "dword ptr [..]"
                    std::string nx;
                    for (char c : toks_[pos_].s) nx.push_back(char(std::tolower(static_cast<unsigned char>(c))));
                    if (nx == "ptr") ++pos_;
                }
                if (toks_[pos_].t != T::LBracket) { fail("'[' expected after " + lower); return 0; }
                return deref(size);
            }
            uint64_t v = 0;
            if (ctx_.reg && ctx_.reg(lower, v))
                return v;
            if (ctx_.symbol && ctx_.symbol(t.s, v))
                return v;
            fail("unknown name '" + t.s + "'");
            return 0;
        }
        default:
            fail(t.t == T::End ? "the expression ends too early" : "unexpected '" + describe(t) + "'");
            return 0;
        }
    }
};

} // namespace

bool eval(const std::string &text, const Context &ctx, uint64_t &out, std::string &err)
{
    Parser p(text, ctx);
    return p.run(out, err);
}

bool evalAddress(const std::string &text, const Context &ctx, uint64_t &out, std::string &err)
{
    size_t b = 0, e = text.size();
    while (b < e && std::isspace(static_cast<unsigned char>(text[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(text[e - 1]))) --e;
    std::string t = text.substr(b, e - b);
    if (!t.empty() && t[0] == '&')
        t.erase(0, 1);
    if (t.empty()) {
        err = "empty expression";
        return false;
    }
    // "[expr]" as a whole is the memory operand's address, like in NASM
    if (t.front() == '[' && t.back() == ']') {
        int depth = 0;
        bool whole = true;
        for (size_t i = 0; i < t.size(); ++i) {
            if (t[i] == '[') ++depth;
            else if (t[i] == ']' && --depth == 0 && i + 1 != t.size()) { whole = false; break; }
        }
        if (whole)
            t = t.substr(1, t.size() - 2);
    }
    return eval(t, ctx, out, err);
}

} // namespace expr
