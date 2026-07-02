#include "Apf.h"
#include <sstream>

namespace arstro
{
namespace apf
{
    Category &Document::category(const std::string &n)
    {
        for (auto &c : categories) if (c.name == n) return c;
        categories.push_back(Category{n, {}});
        return categories.back();
    }

    const Category *Document::find(const std::string &n) const
    {
        for (const auto &c : categories) if (c.name == n) return &c;
        return nullptr;
    }

    std::vector<std::string> Document::categoryNames() const
    {
        std::vector<std::string> out;
        for (const auto &c : categories) out.push_back(c.name);
        return out;
    }

    std::string serialize(const Document &doc)
    {
        std::ostringstream o;
        o << "apf " << doc.version << "\n";
        o << "engine=" << doc.engine << "\n";
        o << "app=" << doc.app << "\n";
        o << "name=" << doc.name << "\n";
        for (const auto &c : doc.categories)
        {
            o << "[" << c.name << "]\n";
            for (const auto &kv : c.values) o << kv.first << "=" << kv.second << "\n";
        }
        return o.str();
    }

    bool parse(const std::string &text, Document &out)
    {
        out = Document{};
        std::stringstream ss(text);
        std::string line;
        if (!std::getline(ss, line)) return false;
        // header magic: "apf <version>"
        if (line.rfind("apf", 0) != 0) return false;
        try { out.version = std::stoi(line.substr(3)); } catch (...) { out.version = Document::kVersion; }

        Category *cur = nullptr;  // current category (nullptr = still in the header)
        while (std::getline(ss, line))
        {
            if (line.empty()) continue;
            if (line.front() == '[' && line.back() == ']')
            {
                cur = &out.category(line.substr(1, line.size() - 2));
                continue;
            }
            const auto eq = line.find('=');
            if (eq == std::string::npos) continue;
            const std::string k = line.substr(0, eq), v = line.substr(eq + 1);
            if (cur)
                cur->set(k, v);
            else if (k == "engine") out.engine = v;   // header fields
            else if (k == "app") out.app = v;
            else if (k == "name") out.name = v;
        }
        return true;
    }
}
}
