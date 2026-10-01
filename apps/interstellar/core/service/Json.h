/*
 *  interstellar_core — Json: a tiny ORDERED JSON value, for the model dump and the API document.
 *
 *  Ordered because both outputs are committed or diffed: `docs/api.json` is drift-tested byte for
 *  byte (R-API-1) and `state print --json --stable` is what an equality test compares. A map that
 *  sorted or hashed its keys would make the order an accident of the container.
 *  Numbers go through `canonicalNumber`, so a value prints identically on every machine.
 */
#pragma once
#include <string>
#include <utility>
#include <vector>

namespace arstro
{
namespace interstellar
{
    class Json
    {
    public:
        enum class Type { Null, Bool, Number, String, Array, Object };

        Json() = default;
        static Json null() { return Json(); }
        static Json boolean(bool b);
        static Json number(double v);
        static Json integer(long long v);
        static Json string(const std::string &s);
        static Json array();
        static Json object();

        /** Object: append `key` (no de-duplication — the writer owns the order). Returns *this. */
        Json &set(const std::string &key, Json v);
        Json &set(const std::string &key, const std::string &s) { return set(key, string(s)); }
        Json &set(const std::string &key, const char *s) { return set(key, string(s)); }
        Json &set(const std::string &key, double v) { return set(key, number(v)); }
        Json &set(const std::string &key, int v) { return set(key, integer(v)); }
        Json &set(const std::string &key, long long v) { return set(key, integer(v)); }
        Json &set(const std::string &key, unsigned v) { return set(key, integer((long long)v)); }
        Json &set(const std::string &key, bool b) { return set(key, boolean(b)); }
        /** Array: append. */
        Json &push(Json v);

        Type type() const { return mType; }
        const std::vector<std::pair<std::string, Json>> &members() const { return mMembers; }
        const std::vector<Json> &items() const { return mItems; }

        /** Pretty-printed, two-space indent, trailing newline only at the top level. */
        std::string dump() const;

    private:
        void write(std::string &out, int indent) const;
        Type mType = Type::Null;
        bool mBool = false;
        std::string mText;   // number (already canonical) or string
        std::vector<Json> mItems;
        std::vector<std::pair<std::string, Json>> mMembers;
    };

    std::string jsonEscape(const std::string &s);
}
}
