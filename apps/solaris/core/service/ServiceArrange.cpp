// SolarisService — the song's arrangement (R-ARR): markers — named places, a section each (R-ARR-1). Interstellar's
// marker (id, name, at, note) in the song's beats, with a length making it a section; `transport seek|loop <mk>`
// reach them. Every edit here runs inside `dispatch`'s all-or-nothing.
#include "Format.h"
#include "SolarisService.h"
#include <algorithm>
#include <cmath>

namespace arstro
{
namespace solaris
{
    using K = Command::Kind;

    namespace
    {
        void sortMarkers(Project &p)
        {
            std::stable_sort(p.markers.begin(), p.markers.end(), [](const Marker &a, const Marker &b) { return a.at < b.at; });
        }
        bool beatsFlag(const Command &c, const std::string &name, double fallback, double &out, std::string &err)
        {
            out = fallback;
            if (!c.has(name)) return true;
            if (!parseNumber(c.flag(name), out) || !std::isfinite(out) || out < 0) { err = "--" + name + " is a number of beats ≥ 0, got `" + c.flag(name) + "`"; return false; }
            out = toTick(out);
            return true;
        }
    }

    bool SolarisService::markerSpan(const std::string &id, double &from, double &to) const
    {
        const Marker *m = mProject.marker(id);
        if (!m) return false;
        from = m->at;
        to = mProject.sectionEnd(*m, songEndBeats());
        return true;
    }

    bool SolarisService::arrangeCommand(const Command &c, std::string &err)
    {
        Project &p = mProject;
        if (c.kind == K::MarkerAdd)
        {
            Marker m;
            m.id = p.nextId("mk");
            m.name = c.arg(0);
            m.note = c.flag("note");
            if (m.name.empty()) { err = "a marker needs a name"; return false; }
            if (!beatsFlag(c, "at", 0.0, m.at, err) || !beatsFlag(c, "length", 0.0, m.length, err)) return false;
            p.markers.push_back(m);
            sortMarkers(p);
            changed("marker.added", m.id);
            mOutput = m.id + "\n";
            return true;
        }
        Marker *m = p.marker(c.arg(0));
        if (!m)
        {
            std::vector<std::string> ids;
            for (const auto &x : p.markers) ids.push_back(x.id + " (" + x.name + ")");
            err = "no marker `" + c.arg(0) + "`";
            if (!ids.empty())
            {
                err += " — the song's:";
                for (const auto &x : ids) err += " " + x;
            }
            return false;
        }
        switch (c.kind)
        {
        case K::MarkerRename:
            if (c.arg(1).empty()) { err = "a marker needs a name"; return false; }
            m->name = c.arg(1);
            if (c.has("note")) m->note = c.flag("note");
            changed("marker.renamed", m->id);
            return true;
        case K::MarkerMove:
        {
            if (!c.has("at") && !c.has("length")) { err = "marker move needs --at and/or --length"; return false; }
            if (!beatsFlag(c, "at", m->at, m->at, err) || !beatsFlag(c, "length", m->length, m->length, err)) return false;
            const std::string id = m->id;
            sortMarkers(p);
            changed("marker.moved", id);
            return true;
        }
        case K::MarkerDelete:
        {
            const std::string id = m->id;
            p.markers.erase(std::remove_if(p.markers.begin(), p.markers.end(), [&](const Marker &x) { return x.id == id; }), p.markers.end());
            changed("marker.deleted", id);
            return true;
        }
        default:
            return false;
        }
    }
}
}
