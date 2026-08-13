#include "Json.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace genesis
{
    namespace
    {
        const Json &nullJson()
        {
            static const Json n;
            return n;
        }

        void escapeTo(std::string &out, const std::string &s)
        {
            out += '"';
            for (char c : s)
            {
                switch (c)
                {
                case '"':  out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default:
                    if ((unsigned char)c < 0x20)
                    {
                        char buf[8];
                        std::snprintf(buf, sizeof buf, "\\u%04x", (unsigned char)c);
                        out += buf;
                    }
                    else
                        out += c;
                }
            }
            out += '"';
        }

        /** Shortest round-trippable form, and never scientific/locale-dependent for the
         *  small magnitudes a design document holds — so a saved file is byte-stable. */
        std::string numberToString(double d)
        {
            if (d == (long long)d && std::fabs(d) < 1e15)
                return std::to_string((long long)d);
            char buf[40];
            std::snprintf(buf, sizeof buf, "%.10g", d);
            return buf;
        }

        struct Parser
        {
            const std::string &src;
            size_t i = 0;
            int line = 1;
            std::string err;

            explicit Parser(const std::string &s) : src(s) {}

            bool fail(const std::string &m)
            {
                if (err.empty())
                    err = "line " + std::to_string(line) + ": " + m;
                return false;
            }
            void skipWs()
            {
                while (i < src.size())
                {
                    char c = src[i];
                    if (c == '\n') { ++line; ++i; }
                    else if (c == ' ' || c == '\t' || c == '\r') ++i;
                    else if (c == '/' && i + 1 < src.size() && src[i + 1] == '/')
                    {   // line comments: a hand-edited document may carry notes
                        while (i < src.size() && src[i] != '\n') ++i;
                    }
                    else break;
                }
            }
            bool literal(const char *word)
            {
                size_t n = 0;
                while (word[n]) ++n;
                if (src.compare(i, n, word) != 0)
                    return fail(std::string("expected ") + word);
                i += n;
                return true;
            }
            bool parseString(std::string &out)
            {
                if (i >= src.size() || src[i] != '"')
                    return fail("expected a string");
                ++i;
                while (i < src.size() && src[i] != '"')
                {
                    char c = src[i++];
                    if (c != '\\') { if (c == '\n') ++line; out += c; continue; }
                    if (i >= src.size())
                        return fail("unterminated escape");
                    char e = src[i++];
                    switch (e)
                    {
                    case 'n': out += '\n'; break;
                    case 't': out += '\t'; break;
                    case 'r': out += '\r'; break;
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case '/': out += '/'; break;
                    case '"': out += '"'; break;
                    case '\\': out += '\\'; break;
                    case 'u':
                    {
                        if (i + 4 > src.size())
                            return fail("truncated \\u escape");
                        unsigned code = (unsigned)std::strtoul(src.substr(i, 4).c_str(), nullptr, 16);
                        i += 4;
                        // Minimal UTF-8 encode (BMP only; surrogate pairs are passed through
                        // as replacement, which a design document never needs).
                        if (code < 0x80) out += (char)code;
                        else if (code < 0x800)
                        {
                            out += (char)(0xC0 | (code >> 6));
                            out += (char)(0x80 | (code & 0x3F));
                        }
                        else
                        {
                            out += (char)(0xE0 | (code >> 12));
                            out += (char)(0x80 | ((code >> 6) & 0x3F));
                            out += (char)(0x80 | (code & 0x3F));
                        }
                        break;
                    }
                    default: return fail("bad escape");
                    }
                }
                if (i >= src.size())
                    return fail("unterminated string");
                ++i;
                return true;
            }
            bool parseValue(Json &out)
            {
                skipWs();
                if (i >= src.size())
                    return fail("unexpected end of input");
                char c = src[i];
                if (c == '{')
                {
                    ++i;
                    out = Json::object();
                    skipWs();
                    if (i < src.size() && src[i] == '}') { ++i; return true; }
                    while (true)
                    {
                        skipWs();
                        std::string key;
                        if (!parseString(key)) return false;
                        skipWs();
                        if (i >= src.size() || src[i] != ':') return fail("expected ':'");
                        ++i;
                        Json v;
                        if (!parseValue(v)) return false;
                        out.set(key, v);
                        skipWs();
                        if (i < src.size() && src[i] == ',') { ++i; continue; }
                        if (i < src.size() && src[i] == '}') { ++i; return true; }
                        return fail("expected ',' or '}'");
                    }
                }
                if (c == '[')
                {
                    ++i;
                    out = Json::array();
                    skipWs();
                    if (i < src.size() && src[i] == ']') { ++i; return true; }
                    while (true)
                    {
                        Json v;
                        if (!parseValue(v)) return false;
                        out.push(v);
                        skipWs();
                        if (i < src.size() && src[i] == ',') { ++i; continue; }
                        if (i < src.size() && src[i] == ']') { ++i; return true; }
                        return fail("expected ',' or ']'");
                    }
                }
                if (c == '"')
                {
                    std::string s;
                    if (!parseString(s)) return false;
                    out = Json::string(s);
                    return true;
                }
                if (c == 't') { if (!literal("true")) return false; out = Json::boolean(true); return true; }
                if (c == 'f') { if (!literal("false")) return false; out = Json::boolean(false); return true; }
                if (c == 'n') { if (!literal("null")) return false; out = Json::makeNull(); return true; }
                {
                    const char *start = src.c_str() + i;
                    char *end = nullptr;
                    double d = std::strtod(start, &end);
                    if (end == start)
                        return fail("expected a value");
                    i += (size_t)(end - start);
                    out = Json::number(d);
                    return true;
                }
            }
        };
    }

    Json Json::boolean(bool b) { Json j; j.mType = Type::Bool; j.mBool = b; return j; }
    Json Json::number(double d) { Json j; j.mType = Type::Number; j.mNumber = d; return j; }
    Json Json::string(std::string s) { Json j; j.mType = Type::String; j.mString = std::move(s); return j; }
    Json Json::array() { Json j; j.mType = Type::Array; return j; }
    Json Json::object() { Json j; j.mType = Type::Object; return j; }

    void Json::push(Json v)
    {
        if (mType != Type::Array) { mType = Type::Array; mArray.clear(); }
        mArray.push_back(std::move(v));
    }

    const Json &Json::at(int i) const
    {
        if (i < 0 || i >= (int)mArray.size())
            return nullJson();
        return mArray[(size_t)i];
    }

    void Json::set(const std::string &key, Json v)
    {
        if (mType != Type::Object) { mType = Type::Object; mObject.clear(); }
        for (auto &kv : mObject)
            if (kv.first == key) { kv.second = std::move(v); return; }
        mObject.emplace_back(key, std::move(v));
    }

    bool Json::has(const std::string &key) const
    {
        for (const auto &kv : mObject)
            if (kv.first == key) return true;
        return false;
    }

    const Json &Json::operator[](const std::string &key) const
    {
        for (const auto &kv : mObject)
            if (kv.first == key) return kv.second;
        return nullJson();
    }

    std::string Json::dump(int indent) const
    {
        const std::string pad(indent * 2, ' ');
        const std::string pad2((indent + 1) * 2, ' ');
        switch (mType)
        {
        case Type::Null: return "null";
        case Type::Bool: return mBool ? "true" : "false";
        case Type::Number: return numberToString(mNumber);
        case Type::String: { std::string o; escapeTo(o, mString); return o; }
        case Type::Array:
        {
            if (mArray.empty()) return "[]";
            std::string o = "[\n";
            for (size_t k = 0; k < mArray.size(); ++k)
            {
                o += pad2 + mArray[k].dump(indent + 1);
                o += (k + 1 < mArray.size()) ? ",\n" : "\n";
            }
            return o + pad + "]";
        }
        case Type::Object:
        {
            if (mObject.empty()) return "{}";
            std::string o = "{\n";
            for (size_t k = 0; k < mObject.size(); ++k)
            {
                o += pad2;
                escapeTo(o, mObject[k].first);
                o += ": " + mObject[k].second.dump(indent + 1);
                o += (k + 1 < mObject.size()) ? ",\n" : "\n";
            }
            return o + pad + "}";
        }
        }
        return "null";
    }

    Json Json::parse(const std::string &text, std::string *error)
    {
        Parser p(text);
        Json out;
        if (!p.parseValue(out))
        {
            if (error) *error = p.err;
            return Json();
        }
        p.skipWs();
        if (p.i != text.size())
        {
            if (error) *error = "line " + std::to_string(p.line) + ": trailing content after the document";
            return Json();
        }
        if (error) error->clear();
        return out;
    }
}
