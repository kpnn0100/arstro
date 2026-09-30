/*
 *  Arstro Ntwb — Json: the one value type NTWB messages are made of.
 *
 *  Small on purpose. NTWB (the native-to-web bridge, docs of record in Arstro Remote's
 *  docs/ntwb/) carries JSON objects, and the apps that speak it are C++ cores that must
 *  stay dependency-free and WASM/Android-portable — so a 300-line value type beats pulling
 *  a JSON library into every app. Objects keep insertion order (a dump reads the way it
 *  was built, and tests can compare text), numbers remember whether they were integers
 *  (a call id or a node index must round-trip without a ".0").
 *
 *  Platform-free: no I/O, no locale (numbers are formatted and parsed in the "C" way by
 *  hand), no exceptions thrown out of parse().
 */
#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace arstro
{
namespace ntwb
{
    class Json
    {
    public:
        enum class Type { Null, Bool, Number, String, Array, Object };

        Json() = default;
        Json(std::nullptr_t) {}
        Json(bool b) : mType(Type::Bool), mBool(b) {}
        Json(int v) : mType(Type::Number), mNum((double)v), mInt(true) {}
        Json(long v) : mType(Type::Number), mNum((double)v), mInt(true) {}
        Json(long long v) : mType(Type::Number), mNum((double)v), mInt(true) {}
        Json(unsigned v) : mType(Type::Number), mNum((double)v), mInt(true) {}
        Json(unsigned long v) : mType(Type::Number), mNum((double)v), mInt(true) {}
        Json(unsigned long long v) : mType(Type::Number), mNum((double)v), mInt(true) {}
        Json(double v) : mType(Type::Number), mNum(v), mInt(false) {}
        Json(const char *s) : mType(Type::String), mStr(s) {}
        Json(std::string s) : mType(Type::String), mStr(std::move(s)) {}

        static Json array() { Json j; j.mType = Type::Array; return j; }
        static Json object() { Json j; j.mType = Type::Object; return j; }

        Type type() const { return mType; }
        bool isNull() const { return mType == Type::Null; }
        bool isBool() const { return mType == Type::Bool; }
        bool isNumber() const { return mType == Type::Number; }
        bool isInt() const { return mType == Type::Number && mInt; }
        bool isString() const { return mType == Type::String; }
        bool isArray() const { return mType == Type::Array; }
        bool isObject() const { return mType == Type::Object; }

        bool asBool(bool fallback = false) const { return mType == Type::Bool ? mBool : fallback; }
        double asNumber(double fallback = 0.0) const { return mType == Type::Number ? mNum : fallback; }
        long long asInt(long long fallback = 0) const { return mType == Type::Number ? (long long)mNum : fallback; }
        const std::string &asString() const;

        // ── arrays ──
        size_t size() const;
        const Json &at(size_t i) const;
        Json &push(Json v);
        const std::vector<Json> &items() const { return mArr; }

        // ── objects (insertion-ordered) ──
        bool has(const std::string &key) const;
        /** The member, or a shared null when absent. */
        const Json &operator[](const std::string &key) const;
        /** Insert or replace; returns the stored value. */
        Json &set(const std::string &key, Json v);
        bool erase(const std::string &key);
        const std::vector<std::pair<std::string, Json>> &members() const { return mObj; }

        /** Compact text (no whitespace). */
        std::string dump() const;
        /** Indented text (`indent` spaces per level), for files people read and diff. */
        std::string dumpPretty(int indent = 2) const;
        /** Parse one JSON value; on failure returns null and sets `err` (never throws). */
        static Json parse(const std::string &text, std::string &err);

        bool operator==(const Json &o) const;
        bool operator!=(const Json &o) const { return !(*this == o); }

    private:
        void dumpTo(std::string &out) const;
        void prettyTo(std::string &out, int indent, int level) const;

        Type mType = Type::Null;
        bool mBool = false;
        double mNum = 0.0;
        bool mInt = false;
        std::string mStr;
        std::vector<Json> mArr;
        std::vector<std::pair<std::string, Json>> mObj;
    };
}
}
