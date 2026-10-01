#include "Colour.h"
#include "core/EditSession.h"
#include "engine/EditParamsIO.h"
#include <cmath>
#include <cstdlib>
#include <sstream>

namespace arstro
{
namespace interstellar
{
    namespace
    {
        EditParams foldUp(const ColourTree &t, int index, EditParams e)
        {
            int guard = (int)t.size();
            for (int p = t[(size_t)index].parent; p >= 0 && p < (int)t.size() && guard-- > 0; p = t[(size_t)p].parent)
                if (!t[(size_t)p].bypass) e = composeParams(e, t[(size_t)p].own);
            return e;
        }
    }

    EditParams foldRender(const ColourTree &t, int index)
    {
        if (index < 0 || index >= (int)t.size()) return EditParams{};
        return foldUp(t, index, t[(size_t)index].bypass ? EditParams{} : t[(size_t)index].own);
    }

    EditParams foldEditTarget(const ColourTree &t, int index)
    {
        if (index < 0 || index >= (int)t.size()) return EditParams{};
        return foldUp(t, index, t[(size_t)index].own);
    }

    bool colourTreeFromCmp(const std::string &cmpPath, ColourTree &out, std::string &err)
    {
        std::vector<cosmo::EditSession::WorkspaceEntry> entries;
        if (!cosmo::EditSession::readWorkspaceFile(cmpPath, entries))
        {
            err = "cannot read the rack snapshot " + cmpPath;
            return false;
        }
        out.clear();
        for (const auto &e : entries)
        {
            ColourNode n;
            n.parent = e.parent;   // the file's parent is the ENTRY index (saveWorkspaceAs' DFS ids)
            n.group = e.group;
            n.bypass = e.bypass;
            n.name = e.name;
            n.imagePath = e.imagePath;
            n.own = e.params;
            out.push_back(std::move(n));
        }
        return true;
    }

    bool paramText(const EditParams &p, const std::string &key, std::string &out)
    {
        std::istringstream in(serializeParams(p));
        std::string line;
        while (std::getline(in, line))
        {
            const auto eq = line.find('=');
            if (eq != std::string::npos && line.compare(0, eq, key) == 0 && eq == key.size())
            {
                out = line.substr(eq + 1);
                return true;
            }
        }
        return false;
    }

    bool paramScalar(const EditParams &p, const std::string &key, double &out)
    {
        std::string t;
        if (!paramText(p, key, t)) return false;
        char *end = nullptr;
        out = std::strtod(t.c_str(), &end);
        return end && *end == '\0' && std::isfinite(out);
    }

    bool setParamText(EditParams &p, const std::string &key, const std::string &value)
    {
        std::string probe;
        if (!paramText(p, key, probe)) return false;   // not a key Cosmo's codec writes
        return deserializeParams(key + "=" + value + "\n", p);
    }

    void applyDeltas(EditParams &p, const std::vector<std::pair<std::string, double>> &deltas)
    {
        for (const auto &d : deltas)
        {
            double v = 0;
            if (!paramScalar(p, d.first, v)) continue;
            std::ostringstream s;
            s.precision(9);
            s << v + d.second;
            setParamText(p, d.first, s.str());
        }
    }
}
}
