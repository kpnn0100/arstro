/*
 *  Arstro Ntwb — Json implementation. Recursive-descent parser with a depth cap (a hostile
 *  peer must not be able to overflow the stack with "[[[[..."), UTF-8 passed through, \u
 *  escapes decoded to UTF-8 (surrogate pairs included), numbers formatted locale-free.
 */
#include "ntwb/Json.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace arstro
{
namespace ntwb
{
    namespace
    {
        const Json kNull;
        const std::string kEmpty;
        constexpr int kMaxDepth = 64;

        void appendUtf8(std::string &out, unsigned cp)
        {
            if (cp < 0x80) out += (char)cp;
            else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
            else if (cp < 0x10000)
            {
                out += (char)(0xE0 | (cp >> 12));
                out += (char)(0x80 | ((cp >> 6) & 0x3F));
                out += (char)(0x80 | (cp & 0x3F));
            }
            else
            {
                out += (char)(0xF0 | (cp >> 18));
                out += (char)(0x80 | ((cp >> 12) & 0x3F));
                out += (char)(0x80 | ((cp >> 6) & 0x3F));
                out += (char)(0x80 | (cp & 0x3F));
            }
        }

        void dumpString(std::string &out, const std::string &s)
        {
            out += '"';
            for (unsigned char c : s)
            {
                switch (c)
                {
                case '"': out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                case '\b': out += "\\b"; break;
                case '\f': out += "\\f"; break;
                default:
                    if (c < 0x20)
                    {
                        char buf[8];
                        std::snprintf(buf, sizeof buf, "\\u%04x", c);
                        out += buf;
                    }
                    else out += (char)c;
                }
            }
            out += '"';
        }

        struct Parser
        {
            const std::string &s;
            size_t i = 0;
            std::string err;

            void ws() { while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i; }
            bool fail(const std::string &why)
            {
                if (err.empty()) err = why + " at offset " + std::to_string(i);
                return false;
            }
            bool literal(const char *lit)
            {
                size_t n = 0;
                while (lit[n]) ++n;
                if (s.compare(i, n, lit) != 0) return fail(std::string("expected ") + lit);
                i += n;
                return true;
            }
            bool hex4(unsigned &out)
            {
                if (i + 4 > s.size()) return fail("short \\u escape");
                out = 0;
                for (int k = 0; k < 4; ++k)
                {
                    const char c = s[i++];
                    out <<= 4;
                    if (c >= '0' && c <= '9') out |= (unsigned)(c - '0');
                    else if (c >= 'a' && c <= 'f') out |= (unsigned)(c - 'a' + 10);
                    else if (c >= 'A' && c <= 'F') out |= (unsigned)(c - 'A' + 10);
                    else return fail("bad \\u escape");
                }
                return true;
            }
            bool string(std::string &out)
            {
                if (i >= s.size() || s[i] != '"') return fail("expected string");
                ++i;
                while (i < s.size())
                {
                    const char c = s[i++];
                    if (c == '"') return true;
                    if ((unsigned char)c < 0x20) return fail("control character in string");
                    if (c != '\\') { out += c; continue; }
                    if (i >= s.size()) break;
                    const char e = s[i++];
                    switch (e)
                    {
                    case '"': out += '"'; break;
                    case '\\': out += '\\'; break;
                    case '/': out += '/'; break;
                    case 'n': out += '\n'; break;
                    case 'r': out += '\r'; break;
                    case 't': out += '\t'; break;
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case 'u':
                    {
                        unsigned cp;
                        if (!hex4(cp)) return false;
                        if (cp >= 0xD800 && cp < 0xDC00 && i + 6 <= s.size() && s[i] == '\\' && s[i + 1] == 'u')
                        {
                            i += 2;
                            unsigned lo;
                            if (!hex4(lo)) return false;
                            if (lo < 0xDC00 || lo > 0xDFFF) return fail("bad surrogate pair");
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        }
                        appendUtf8(out, cp);
                        break;
                    }
                    default: return fail("bad escape");
                    }
                }
                return fail("unterminated string");
            }
            bool number(Json &out)
            {
                const size_t start = i;
                if (s[i] == '-') ++i;
                bool isInt = true;
                while (i < s.size() && ((s[i] >= '0' && s[i] <= '9') || s[i] == '.' || s[i] == 'e' || s[i] == 'E' ||
                                        s[i] == '+' || s[i] == '-'))
                {
                    if (s[i] == '.' || s[i] == 'e' || s[i] == 'E') isInt = false;
                    ++i;
                }
                const std::string tok = s.substr(start, i - start);
                if (tok.empty() || tok == "-") return fail("bad number");
                // strtod honours the C locale's decimal point; parse by hand for '.' safety
                double v = 0.0;
                {
                    size_t k = 0;
                    bool neg = false;
                    if (tok[k] == '-') { neg = true; ++k; }
                    if (k >= tok.size() || tok[k] < '0' || tok[k] > '9') return fail("bad number");
                    while (k < tok.size() && tok[k] >= '0' && tok[k] <= '9') v = v * 10.0 + (tok[k++] - '0');
                    if (k < tok.size() && tok[k] == '.')
                    {
                        ++k;
                        double scale = 0.1;
                        if (k >= tok.size() || tok[k] < '0' || tok[k] > '9') return fail("bad fraction");
                        while (k < tok.size() && tok[k] >= '0' && tok[k] <= '9') { v += (tok[k++] - '0') * scale; scale *= 0.1; }
                    }
                    if (k < tok.size() && (tok[k] == 'e' || tok[k] == 'E'))
                    {
                        ++k;
                        bool eneg = false;
                        if (k < tok.size() && (tok[k] == '+' || tok[k] == '-')) eneg = tok[k++] == '-';
                        if (k >= tok.size()) return fail("bad exponent");
                        int e = 0;
                        while (k < tok.size() && tok[k] >= '0' && tok[k] <= '9') e = e * 10 + (tok[k++] - '0');
                        v *= std::pow(10.0, eneg ? -e : e);
                    }
                    if (k != tok.size()) return fail("bad number");
                    if (neg) v = -v;
                }
                out = isInt ? Json((long long)v) : Json(v);
                return true;
            }
            bool value(Json &out, int depth)
            {
                if (depth > kMaxDepth) return fail("nested too deeply");
                ws();
                if (i >= s.size()) return fail("unexpected end");
                const char c = s[i];
                if (c == '{')
                {
                    ++i;
                    out = Json::object();
                    ws();
                    if (i < s.size() && s[i] == '}') { ++i; return true; }
                    for (;;)
                    {
                        ws();
                        std::string key;
                        if (!string(key)) return false;
                        ws();
                        if (i >= s.size() || s[i] != ':') return fail("expected ':'");
                        ++i;
                        Json v;
                        if (!value(v, depth + 1)) return false;
                        out.set(key, std::move(v));
                        ws();
                        if (i < s.size() && s[i] == ',') { ++i; continue; }
                        if (i < s.size() && s[i] == '}') { ++i; return true; }
                        return fail("expected ',' or '}'");
                    }
                }
                if (c == '[')
                {
                    ++i;
                    out = Json::array();
                    ws();
                    if (i < s.size() && s[i] == ']') { ++i; return true; }
                    for (;;)
                    {
                        Json v;
                        if (!value(v, depth + 1)) return false;
                        out.push(std::move(v));
                        ws();
                        if (i < s.size() && s[i] == ',') { ++i; continue; }
                        if (i < s.size() && s[i] == ']') { ++i; return true; }
                        return fail("expected ',' or ']'");
                    }
                }
                if (c == '"')
                {
                    std::string str;
                    if (!string(str)) return false;
                    out = Json(std::move(str));
                    return true;
                }
                if (c == 't') { out = Json(true); return literal("true"); }
                if (c == 'f') { out = Json(false); return literal("false"); }
                if (c == 'n') { out = Json(); return literal("null"); }
                if (c == '-' || (c >= '0' && c <= '9')) return number(out);
                return fail("unexpected character");
            }
        };
    }

    const std::string &Json::asString() const { return mType == Type::String ? mStr : kEmpty; }

    size_t Json::size() const
    {
        return mType == Type::Array ? mArr.size() : mType == Type::Object ? mObj.size() : 0;
    }
    const Json &Json::at(size_t i) const { return (mType == Type::Array && i < mArr.size()) ? mArr[i] : kNull; }
    Json &Json::push(Json v)
    {
        if (mType != Type::Array) { *this = array(); }
        mArr.push_back(std::move(v));
        return mArr.back();
    }

    bool Json::has(const std::string &key) const
    {
        if (mType != Type::Object) return false;
        for (const auto &m : mObj) if (m.first == key) return true;
        return false;
    }
    const Json &Json::operator[](const std::string &key) const
    {
        if (mType == Type::Object)
            for (const auto &m : mObj) if (m.first == key) return m.second;
        return kNull;
    }
    Json &Json::set(const std::string &key, Json v)
    {
        if (mType != Type::Object) *this = object();
        for (auto &m : mObj)
            if (m.first == key) { m.second = std::move(v); return m.second; }
        mObj.emplace_back(key, std::move(v));
        return mObj.back().second;
    }
    bool Json::erase(const std::string &key)
    {
        for (size_t k = 0; k < mObj.size(); ++k)
            if (mObj[k].first == key) { mObj.erase(mObj.begin() + (long)k); return true; }
        return false;
    }

    void Json::dumpTo(std::string &out) const
    {
        switch (mType)
        {
        case Type::Null: out += "null"; break;
        case Type::Bool: out += mBool ? "true" : "false"; break;
        case Type::Number:
        {
            if (!std::isfinite(mNum)) { out += "null"; break; }   // JSON has no NaN / inf
            char buf[40];
            if (mInt)
                std::snprintf(buf, sizeof buf, "%lld", (long long)mNum);
            else
            {
                // the shortest of %.15g / %.17g that reads back exactly: 0.1 stays "0.1"
                std::snprintf(buf, sizeof buf, "%.15g", mNum);
                if (std::strtod(buf, nullptr) != mNum) std::snprintf(buf, sizeof buf, "%.17g", mNum);
                for (char *p = buf; *p; ++p) if (*p == ',') *p = '.';     // a comma locale
            }
            out += buf;
            break;
        }
        case Type::String: dumpString(out, mStr); break;
        case Type::Array:
            out += '[';
            for (size_t k = 0; k < mArr.size(); ++k) { if (k) out += ','; mArr[k].dumpTo(out); }
            out += ']';
            break;
        case Type::Object:
            out += '{';
            for (size_t k = 0; k < mObj.size(); ++k)
            {
                if (k) out += ',';
                dumpString(out, mObj[k].first);
                out += ':';
                mObj[k].second.dumpTo(out);
            }
            out += '}';
            break;
        }
    }

    std::string Json::dump() const
    {
        std::string out;
        dumpTo(out);
        return out;
    }

    void Json::prettyTo(std::string &out, int indent, int level) const
    {
        const std::string pad((size_t)(indent * (level + 1)), ' '), end((size_t)(indent * level), ' ');
        if (mType == Type::Array && !mArr.empty())
        {
            out += "[\n";
            for (size_t k = 0; k < mArr.size(); ++k)
            {
                out += pad;
                mArr[k].prettyTo(out, indent, level + 1);
                out += k + 1 < mArr.size() ? ",\n" : "\n";
            }
            out += end + "]";
        }
        else if (mType == Type::Object && !mObj.empty())
        {
            out += "{\n";
            for (size_t k = 0; k < mObj.size(); ++k)
            {
                out += pad;
                dumpString(out, mObj[k].first);
                out += ": ";
                mObj[k].second.prettyTo(out, indent, level + 1);
                out += k + 1 < mObj.size() ? ",\n" : "\n";
            }
            out += end + "}";
        }
        else dumpTo(out);
    }

    std::string Json::dumpPretty(int indent) const
    {
        std::string out;
        prettyTo(out, indent, 0);
        out += "\n";
        return out;
    }

    Json Json::parse(const std::string &text, std::string &err)
    {
        Parser p{text};
        Json v;
        if (!p.value(v, 0)) { err = p.err; return Json(); }
        p.ws();
        if (p.i != text.size()) { err = "trailing characters at offset " + std::to_string(p.i); return Json(); }
        err.clear();
        return v;
    }

    bool Json::operator==(const Json &o) const
    {
        if (mType != o.mType) return false;
        switch (mType)
        {
        case Type::Null: return true;
        case Type::Bool: return mBool == o.mBool;
        case Type::Number: return mNum == o.mNum;
        case Type::String: return mStr == o.mStr;
        case Type::Array: return mArr == o.mArr;
        case Type::Object:
            if (mObj.size() != o.mObj.size()) return false;
            for (const auto &m : mObj) if (!o.has(m.first) || o[m.first] != m.second) return false;
            return true;
        }
        return false;
    }
}
}
