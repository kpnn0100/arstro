#include "Json.h"
#include "Format.h"
#include <cstdio>

namespace arstro
{
namespace solaris
{
    std::string jsonEscape(const std::string &s)
    {
        std::string o;
        for (unsigned char c : s)
        {
            switch (c)
            {
            case '"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            default:
                if (c < 0x20)
                {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", c);
                    o += buf;
                }
                else
                    o += (char)c;
            }
        }
        return o;
    }

    Json Json::boolean(bool b) { Json j; j.mType = Type::Bool; j.mBool = b; return j; }
    Json Json::number(double v) { Json j; j.mType = Type::Number; j.mText = canonicalNumber(v); return j; }
    Json Json::integer(long long v) { Json j; j.mType = Type::Number; j.mText = std::to_string(v); return j; }
    Json Json::string(const std::string &s) { Json j; j.mType = Type::String; j.mText = s; return j; }
    Json Json::array() { Json j; j.mType = Type::Array; return j; }
    Json Json::object() { Json j; j.mType = Type::Object; return j; }

    Json &Json::set(const std::string &key, Json v)
    {
        mType = Type::Object;
        mMembers.emplace_back(key, std::move(v));
        return *this;
    }

    Json &Json::push(Json v)
    {
        mType = Type::Array;
        mItems.push_back(std::move(v));
        return *this;
    }

    void Json::write(std::string &out, int indent) const
    {
        const std::string pad(size_t(indent) * 2, ' '), padIn(size_t(indent + 1) * 2, ' ');
        switch (mType)
        {
        case Type::Null: out += "null"; break;
        case Type::Bool: out += mBool ? "true" : "false"; break;
        case Type::Number:
            // JSON has no nan/inf; the model never stores them (it repairs), but a dump must not lie
            out += (mText == "nan" || mText == "inf" || mText == "-inf") ? "null" : mText;
            break;
        case Type::String: out += "\"" + jsonEscape(mText) + "\""; break;
        case Type::Array:
            if (mItems.empty()) { out += "[]"; break; }
            out += "[\n";
            for (size_t i = 0; i < mItems.size(); ++i)
            {
                out += padIn;
                mItems[i].write(out, indent + 1);
                out += i + 1 < mItems.size() ? ",\n" : "\n";
            }
            out += pad + "]";
            break;
        case Type::Object:
            if (mMembers.empty()) { out += "{}"; break; }
            out += "{\n";
            for (size_t i = 0; i < mMembers.size(); ++i)
            {
                out += padIn + "\"" + jsonEscape(mMembers[i].first) + "\": ";
                mMembers[i].second.write(out, indent + 1);
                out += i + 1 < mMembers.size() ? ",\n" : "\n";
            }
            out += pad + "}";
            break;
        }
    }

    std::string Json::dump() const
    {
        std::string out;
        write(out, 0);
        return out + "\n";
    }
}
}
