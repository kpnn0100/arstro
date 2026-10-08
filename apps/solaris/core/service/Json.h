/*
 *  solaris_core — Json: a tiny ORDERED JSON value, for the model dump and the API document.
 *
 *  Ordered because both outputs are committed or diffed (`docs/api.json` byte for byte, R-API-1;
 *  `state print --json --stable` by equality tests). Numbers go through the model's
 *  `canonicalNumber`, so a value prints identically in the file, the log and the dump.
 *
 *  The same shape as Interstellar's `interstellar::Json` — written here rather than linked because
 *  that one lives inside `interstellar_core`, which hosts all of Cosmo (decision logged in
 *  docs/PROGRESS.md: a shared utility library would be an Interstellar refactor, not a Solaris task).
 */
#pragma once
#include <string>
#include <utility>
#include <vector>

namespace arstro
{
namespace solaris
{
    class Json
    {
    public:
        enum class Type { Null, Bool, Number, String, Array, Object };

        Json() = default;
        static Json boolean(bool b);
        static Json number(double v);
        static Json integer(long long v);
        static Json string(const std::string &s);
        static Json array();
        static Json object();

        Json &set(const std::string &key, Json v);
        Json &set(const std::string &key, const std::string &s) { return set(key, string(s)); }
        Json &set(const std::string &key, const char *s) { return set(key, string(s)); }
        Json &set(const std::string &key, double v) { return set(key, number(v)); }
        Json &set(const std::string &key, int v) { return set(key, integer(v)); }
        Json &set(const std::string &key, long long v) { return set(key, integer(v)); }
        Json &set(const std::string &key, bool b) { return set(key, boolean(b)); }
        Json &push(Json v);

        Type type() const { return mType; }
        /** Pretty-printed, two-space indent, a trailing newline. */
        std::string dump() const;

    private:
        void write(std::string &out, int indent) const;
        Type mType = Type::Null;
        bool mBool = false;
        std::string mText;
        std::vector<Json> mItems;
        std::vector<std::pair<std::string, Json>> mMembers;
    };

    std::string jsonEscape(const std::string &s);
}
}
