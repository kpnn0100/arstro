/*
 *  Arstro Preset File (.apf) — a GENERIC, engine-independent preset envelope.
 *
 *  An .apf carries clear metadata (format version + target ENGINE domain + app +
 *  human name) and a set of named CATEGORIES, each a list of key=value lines. The
 *  envelope knows nothing about any specific engine's parameters: a front end maps
 *  its own params to/from categories. On load, an engine reads `engine` and REJECTS
 *  a file for a different domain; it then applies only the categories it understands
 *  (and that the user selected), ignoring unknown ones (forward-compatible).
 *
 *  This is deliberately dependency-free (only std::string/vector) so any Arstro
 *  engine — image, audio, video — can reuse it.
 *
 *  Text form:
 *      apf 1
 *      engine=image
 *      app=cosmo
 *      name=Golden Hour
 *      [basic]
 *      exposure=0.7
 *      contrast=15
 *      [color]
 *      temp=7000
 */
#pragma once
#include <string>
#include <utility>
#include <vector>

namespace arstro
{
namespace apf
{
    struct Category
    {
        std::string name;
        std::vector<std::pair<std::string, std::string>> values;  // ordered key=value

        void set(const std::string &k, const std::string &v) { values.push_back({k, v}); }
        const std::string *get(const std::string &k) const
        {
            for (const auto &kv : values) if (kv.first == k) return &kv.second;
            return nullptr;
        }
    };

    struct Document
    {
        static constexpr int kVersion = 1;
        int version = kVersion;
        std::string engine;  // target domain: "image" | "audio" | "video" | ...
        std::string app;     // producing app (informational): "cosmo" | ...
        std::string name;    // human-readable preset name
        std::vector<Category> categories;

        Category &category(const std::string &name);          // get-or-create
        const Category *find(const std::string &name) const;  // nullptr if absent
        bool has(const std::string &name) const { return find(name) != nullptr; }
        std::vector<std::string> categoryNames() const;
    };

    /** Serialize a document to the .apf text form. */
    std::string serialize(const Document &doc);
    /** Parse .apf text; returns false only if it is not an apf file at all
     *  (missing/!= "apf" magic). A partial/garbage body still parses tolerantly. */
    bool parse(const std::string &text, Document &out);
}
}
