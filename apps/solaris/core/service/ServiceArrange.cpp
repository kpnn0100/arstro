// SolarisService — the song's arrangement (R-ARR): markers — named places, a section each (R-ARR-1). Interstellar's
// marker (id, name, at, note) in the song's beats, with a length making it a section; `transport seek|loop <mk>`
// reach them. Time opened and taken out across the whole song (R-ARR-2). Every edit here runs inside `dispatch`'s
// all-or-nothing — and a time edit decides every refusal before it moves anything.
#include "Compile.h"
#include "Format.h"
#include "Player.h"
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

    bool SolarisService::timeCommand(const Command &c, std::string &err)
    {
        // R-ARR-2: the whole song at once — clips, automation points (a looping automation's span), markers, the loop
        Project &p = mProject;
        const double eps = 0.5 / kPpq;
        auto num = [&](const char *name, double &out) {
            if (!parseNumber(c.flag(name), out) || !std::isfinite(out) || out < 0) { err = std::string("--") + name + " is a number of beats ≥ 0, got `" + c.flag(name) + "`"; return false; }
            out = toTick(out);
            return true;
        };
        const bool insert = c.kind == K::TimeInsert;
        double a = 0, b = 0;
        if (insert ? (!num("at", a) || !num("length", b)) : (!num("from", a) || !num("to", b))) return false;
        if (insert ? !(b > 0) : !(b > a)) { err = insert ? "--length must be more than 0 beats" : "--to must be after --from"; return false; }
        const double span = insert ? b : b - a;
        size_t clips = 0, gone = 0, autos = 0, marks = 0;
        std::string notes;
        // what crosses an edge and what lies inside: decided before anything moves
        std::vector<std::string> crossing, shortened, inside;
        for (const auto &cl : p.clips)
        {
            const double end = cl.at + clipLengthBeats(p, cl);
            if (insert) { if (cl.at < a - eps && end > a + eps) crossing.push_back(cl.id); continue; }
            if (cl.at >= a - eps && cl.at < b - eps && end <= b + eps) { inside.push_back(cl.id); continue; }
            const bool crossesFrom = cl.at < a - eps && end > a + eps, headInside = cl.at >= a - eps && cl.at < b - eps && end > b + eps;
            if (crossesFrom && (!cl.isAudio() || cl.loop)) shortened.push_back(cl.id); // what repeats keeps repeating
            else if (crossesFrom || headInside) crossing.push_back(cl.id);
        }
        if (!insert && !crossing.empty())
        {
            err = "";
            for (const auto &id : crossing) err += (err.empty() ? "" : ", ") + id;
            err += (crossing.size() == 1 ? " crosses" : " cross") + std::string(" an edge of ") + canonicalBeats(a) + " … " + canonicalBeats(b) +
                   " — split it there first (R-EDIT-1) or move it; nothing changed";
            return false;
        }
        // clips
        for (auto &cl : p.clips)
        {
            const double end = cl.at + clipLengthBeats(p, cl);
            if (insert) { if (cl.at >= a - eps) { cl.at = toTick(cl.at + span); ++clips; } continue; }
            if (std::find(inside.begin(), inside.end(), cl.id) != inside.end()) continue;
            if (std::find(shortened.begin(), shortened.end(), cl.id) != shortened.end())
            {
                const double overlap = std::min(end, b) - a;
                cl.length = toTick(clipLengthBeats(p, cl) - overlap);
                ++clips;
            }
            else if (cl.at >= b - eps) { cl.at = toTick(cl.at - span); ++clips; }
        }
        if (!insert)
        {
            const size_t before = p.clips.size();
            p.clips.erase(std::remove_if(p.clips.begin(), p.clips.end(), [&](const Clip &cl) { return std::find(inside.begin(), inside.end(), cl.id) != inside.end(); }),
                          p.clips.end());
            gone = before - p.clips.size();
        }
        // automations: a curve's points in song beats; a looping one's span (its points are its cycle's)
        for (auto &au : p.automations)
        {
            bool moved = false;
            if (au.loop > 0)
            {
                const double s = au.at, e = au.length > 0 ? au.at + au.length : 1e300;
                if (insert)
                {
                    if (s >= a - eps) { au.at = toTick(s + span); moved = true; }
                    else if (au.length > 0 && e > a + eps) { au.length = toTick(au.length + span); moved = true; }
                }
                else if (s >= b - eps) { au.at = toTick(s - span); moved = true; }
                else if (s >= a - eps) { au.at = a; if (au.length > 0) au.length = toTick(std::max(1.0 / kPpq, e - b)); moved = true; }
                else if (au.length > 0 && e > a + eps) { au.length = toTick(au.length - (std::min(e, b) - a)); moved = true; }
            }
            else
            {
                const size_t n = au.points.size();
                if (!insert)
                    au.points.erase(std::remove_if(au.points.begin(), au.points.end(), [&](const AutoPoint &x) { return x.at >= a - eps && x.at < b - eps; }), au.points.end());
                moved |= n != au.points.size();
                for (auto &x : au.points)
                    if (x.at >= (insert ? a : b) - eps) { x.at = toTick(x.at + (insert ? span : -span)); moved = true; }
            }
            autos += moved ? 1 : 0;
        }
        // markers: a place after moves; one inside a deleted span goes; a section across the edit grows or shrinks
        {
            const size_t n = p.markers.size();
            if (!insert)
                p.markers.erase(std::remove_if(p.markers.begin(), p.markers.end(), [&](const Marker &m) { return m.at >= a - eps && m.at < b - eps && (m.length <= 0 || m.at + m.length <= b + eps); }),
                                p.markers.end());
            marks += n - p.markers.size();
            for (auto &m : p.markers)
            {
                const double e = m.at + m.length;
                if (insert)
                {
                    if (m.at >= a - eps) { m.at = toTick(m.at + span); ++marks; }
                    else if (m.length > 0 && e > a + eps) { m.length = toTick(m.length + span); ++marks; }
                }
                else if (m.at >= b - eps) { m.at = toTick(m.at - span); ++marks; }
                else if (m.at >= a - eps) { m.at = a; m.length = toTick(e - b); ++marks; }          // a section's head in the span
                else if (m.length > 0 && e > a + eps) { m.length = toTick(m.length - (std::min(e, b) - a)); ++marks; }
            }
        }
        // the loop (the session's): moved, grown or shrunk the same; a loop taken out entirely ends
        if (mLoopTo > mLoopFrom)
        {
            auto at = [&](double x) { return insert ? (x >= a - eps ? x + span : x) : (x >= b - eps ? x - span : x > a ? a : x); };
            const double f = at(mLoopFrom), t = insert && mLoopTo > a + eps && mLoopFrom < a - eps ? mLoopTo + span : at(mLoopTo);
            mLoopFrom = toTick(f);
            mLoopTo = toTick(t);
            if (!(mLoopTo > mLoopFrom + eps)) mLoopFrom = mLoopTo = 0;
            if (mPlayer)
            {
                const double spb = 60.0 / std::max(1.0, p.header.bpm) * p.header.sampleRate;
                mPlayer->setLoop(mLoopTo > mLoopFrom ? (long long)std::llround(mLoopFrom * spb) : 0, mLoopTo > mLoopFrom ? (long long)std::llround(mLoopTo * spb) : 0);
            }
        }
        sortMarkers(p);
        changed(insert ? "time.inserted" : "time.deleted", "song");
        auto count = [](size_t k, const char *one, const char *many) { return std::to_string(k) + " " + (k == 1 ? one : many); };
        mOutput = std::string(insert ? "opened " : "took out ") + canonicalBeats(span) + " beats at " + canonicalBeats(a) + ": " + count(clips, "clip", "clips") +
                  (insert ? " moved" : " moved or shortened") + (gone ? ", " + count(gone, "clip", "clips") + " gone" : std::string()) + ", " +
                  count(autos, "automation", "automations") + ", " + count(marks, "marker", "markers") + "\n";
        for (const auto &id : crossing) mOutput += id + " crosses beat " + canonicalBeats(a) + " — it stays (split it to open the gap through it, R-EDIT-1)\n";
        return true;
    }

    bool SolarisService::groupCommand(const Command &c, std::string &err)
    {
        // R-ARR-3: a group is organisation only; its lanes side by side (`gatherLaneGroups` keeps them so)
        Project &p = mProject;
        auto ordered = [&p] {
            std::vector<Lane *> v;
            for (auto &l : p.lanes) v.push_back(&l);
            std::stable_sort(v.begin(), v.end(), [](const Lane *a, const Lane *b) { return a->order < b->order; });
            return v;
        };
        switch (c.kind)
        {
        case K::LaneGroup:
        {
            std::vector<std::string> ids;
            for (const auto &a : c.args)
                for (const auto &w : tokenize(a))
                {
                    if (!p.lane(w)) { err = "no lane `" + w + "` — nothing was grouped"; return false; }
                    if (std::find(ids.begin(), ids.end(), w) == ids.end()) ids.push_back(w);
                }
            LaneGroup g;
            g.id = p.nextId("lg");
            g.name = c.flag("name", "Group " + std::to_string(p.groups.size() + 1));
            if (g.name.empty()) { err = "a group needs a name"; return false; }
            // gathered at the first one's place, in their order
            std::vector<Lane *> out;
            bool placed = false;
            for (Lane *l : ordered())
            {
                const bool named = std::find(ids.begin(), ids.end(), l->id) != ids.end();
                if (!named) { out.push_back(l); continue; }
                if (placed) continue;
                placed = true;
                for (Lane *m : ordered())
                    if (std::find(ids.begin(), ids.end(), m->id) != ids.end()) out.push_back(m);
            }
            for (size_t i = 0; i < out.size(); ++i) out[i]->order = (int)i;
            for (const auto &id : ids) p.lane(id)->group = g.id;
            p.groups.push_back(g);
            gatherLaneGroups(p); // another group a lane left stays whole; one left empty goes
            changed("group.added", g.id);
            mOutput = g.id + "\n";
            return true;
        }
        case K::LaneUngroup:
        {
            if (!p.group(c.arg(0))) { err = "no lane group `" + c.arg(0) + "`"; return false; }
            const std::string id = c.arg(0);
            for (auto &l : p.lanes)
                if (l.group == id) l.group.clear();
            p.groups.erase(std::remove_if(p.groups.begin(), p.groups.end(), [&](const LaneGroup &g) { return g.id == id; }), p.groups.end());
            changed("group.deleted", id);
            return true;
        }
        case K::LaneMove: // a group, whole: its first lane to --to, never inside another group's run
        {
            const std::string id = c.arg(0);
            if (!c.has("to")) { err = "lane move needs --to <index>"; return false; }
            double v = 0;
            if (!parseNumber(c.flag("to"), v) || v != std::floor(v) || v < 0 || v > (double)p.lanes.size() - 1)
            { err = "--to is a row from 0 to " + std::to_string(p.lanes.size() - 1); return false; }
            std::vector<Lane *> rest, block;
            for (Lane *l : ordered()) (l->group == id ? block : rest).push_back(l);
            size_t at = std::min((size_t)v, rest.size());
            while (at > 0 && at < rest.size() && !rest[at]->group.empty() && rest[at]->group == rest[at - 1]->group) ++at; // past a run it would split
            rest.insert(rest.begin() + (long)at, block.begin(), block.end());
            for (size_t i = 0; i < rest.size(); ++i) rest[i]->order = (int)i;
            changed("group.moved", id);
            return true;
        }
        case K::ClipsMove:
        {
            double by = 0;
            if (!parseNumber(c.flag("by"), by) || !std::isfinite(by) || std::fabs(by) > 1e6) { err = "--by is a number of beats (negative = earlier), got `" + c.flag("by") + "`"; return false; }
            std::vector<Clip *> which;
            for (const auto &a : c.args)
                for (const auto &w : tokenize(a))
                {
                    Clip *cl = p.clip(w);
                    if (!cl) { err = "no clip `" + w + "` — nothing moved"; return false; }
                    if (cl->at + by < -0.5 / kPpq) { err = w + " would start before beat 0 — nothing moved"; return false; }
                    if (std::find(which.begin(), which.end(), cl) == which.end()) which.push_back(cl);
                }
            for (Clip *cl : which) cl->at = toTick(std::max(0.0, cl->at + by));
            changed("clips.moved", which.empty() ? std::string() : which.front()->id);
            mOutput = std::to_string(which.size()) + (which.size() == 1 ? " clip" : " clips") + " moved\n";
            return true;
        }
        default:
            return false;
        }
    }

    bool SolarisService::arrangeCommand(const Command &c, std::string &err)
    {
        Project &p = mProject;
        if (c.kind == K::TimeInsert || c.kind == K::TimeDelete) return timeCommand(c, err);
        if (c.kind == K::LaneGroup || c.kind == K::LaneUngroup || c.kind == K::ClipsMove || c.kind == K::LaneMove) return groupCommand(c, err);
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
