/*
 *  Genesis — a minimal JSON value + parser/writer.
 *
 *  The `.genesis` document is JSON, and Genesis must stay dependency-free (it links
 *  artboard_core and nothing else), so it carries its own reader/writer rather than
 *  pulling in a library. Deliberately small: objects preserve INSERTION ORDER so a
 *  saved document is byte-stable and diffs cleanly in review, which is the whole point
 *  of storing the document as text.
 */
#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace genesis
{
    class Json
    {
    public:
        enum class Type { Null, Bool, Number, String, Array, Object };

        Json() = default;
        static Json makeNull() { return Json(); }
        static Json boolean(bool b);
        static Json number(double d);
        static Json string(std::string s);
        static Json array();
        static Json object();

        Type type() const { return mType; }
        bool isNull() const { return mType == Type::Null; }
        bool isObject() const { return mType == Type::Object; }
        bool isArray() const { return mType == Type::Array; }
        bool isNumber() const { return mType == Type::Number; }
        bool isString() const { return mType == Type::String; }
        bool isBool() const { return mType == Type::Bool; }

        bool asBool(bool fallback = false) const { return mType == Type::Bool ? mBool : fallback; }
        double asNumber(double fallback = 0.0) const { return mType == Type::Number ? mNumber : fallback; }
        const std::string &asString() const { return mString; }
        std::string asString(const std::string &fallback) const { return mType == Type::String ? mString : fallback; }

        // ---- array ----
        void push(Json v);
        int size() const { return (int)mArray.size(); }
        const Json &at(int i) const;

        // ---- object (insertion-ordered) ----
        void set(const std::string &key, Json v);
        bool has(const std::string &key) const;
        /** Missing keys return a shared null, so `doc["a"]["b"]` never throws. */
        const Json &operator[](const std::string &key) const;
        const std::vector<std::pair<std::string, Json>> &members() const { return mObject; }

        /** Pretty-print with 2-space indent and a trailing newline. */
        std::string dump(int indent = 0) const;

        /** Parse `text`. On failure returns null and fills `error` with "line N: message". */
        static Json parse(const std::string &text, std::string *error = nullptr);

    private:
        Type mType = Type::Null;
        bool mBool = false;
        double mNumber = 0.0;
        std::string mString;
        std::vector<Json> mArray;
        std::vector<std::pair<std::string, Json>> mObject;
    };
}
