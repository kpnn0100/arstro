#include "Project.h"
#include "Format.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <set>
#include <sstream>

namespace arstro
{
namespace solaris
{
    // ── lookups ──────────────────────────────────────────────────────────────────────────────

    namespace
    {
        template <class V> auto *findId(V &v, const std::string &id)
        {
            for (auto &x : v)
                if (x.id == id) return &x;
            return (decltype(&v[0]))nullptr;
        }
        std::string trim(const std::string &s)
        {
            const auto a = s.find_first_not_of(" \t\r\n");
            if (a == std::string::npos) return std::string();
            return s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
        }
        std::vector<std::string> splitComma(const std::string &s)
        {
            std::vector<std::string> out;
            std::string cur;
            for (char c : s)
            {
                if (c == ',') { if (!trim(cur).empty()) out.push_back(trim(cur)); cur.clear(); }
                else cur += c;
            }
            if (!trim(cur).empty()) out.push_back(trim(cur));
            return out;
        }
    }

    Port *Project::port(const std::string &id) { return findId(ports, id); }
    Mixer *Project::mixer(const std::string &id) { return findId(mixers, id); }
    Strip *Project::strip(const std::string &id) { return findId(strips, id); }
    Send *Project::send(const std::string &id) { return findId(sends, id); }
    Lane *Project::lane(const std::string &id) { return findId(lanes, id); }
    std::string laneTitle(const Project &p, const Lane &l)
    {
        if (!l.name.empty() || l.track.empty()) return l.name;
        const Strip *s = p.strip(l.track);
        return s ? s->name : l.name;
    }
    Pattern *Project::pattern(const std::string &id) { return findId(patterns, id); }
    Clip *Project::clip(const std::string &id) { return findId(clips, id); }
    const Strip *Project::strip(const std::string &id) const { return findId(strips, id); }
    const Mixer *Project::mixer(const std::string &id) const { return findId(mixers, id); }
    const Port *Project::port(const std::string &id) const { return findId(ports, id); }
    const Lane *Project::lane(const std::string &id) const { return findId(lanes, id); }
    const Pattern *Project::pattern(const std::string &id) const { return findId(patterns, id); }
    const Clip *Project::clip(const std::string &id) const { return findId(clips, id); }
    Automation *Project::automation(const std::string &id) { return findId(automations, id); }
    int Clip::takeAt(double beat) const
    {
        if (takes.empty()) return 0;
        for (auto it = comp.rbegin(); it != comp.rend(); ++it) // the latest range written wins where they meet
            if (beat >= it->from - 1e-9 && beat < it->to - 1e-9) return it->take;
        return (int)takes.size();
    }
    Marker *Project::marker(const std::string &id) { return findId(markers, id); }
    TempoPoint *Project::tempo(const std::string &id) { return findId(tempos, id); }
    const TempoPoint *Project::tempo(const std::string &id) const { return findId(tempos, id); }
    LaneGroup *Project::group(const std::string &id) { return findId(groups, id); }
    const LaneGroup *Project::group(const std::string &id) const { return findId(groups, id); }
    const Marker *Project::marker(const std::string &id) const { return findId(markers, id); }
    Scene *Project::scene(const std::string &id) { return findId(scenes, id); }
    const Scene *Project::scene(const std::string &id) const { return findId(scenes, id); }
    Slot *Project::slot(const std::string &id) { return findId(slots, id); }
    const Slot *Project::slot(const std::string &id) const { return findId(slots, id); }
    std::vector<const Scene *> Project::scenesInOrder() const
    {
        std::vector<const Scene *> v;
        for (const auto &x : scenes) v.push_back(&x);
        std::stable_sort(v.begin(), v.end(), [](const Scene *a, const Scene *b) { return a->order < b->order; });
        return v;
    }
    double Project::sectionEnd(const Marker &m, double songEnd) const
    {
        if (m.length > 0) return m.at + m.length;
        double next = std::max(songEnd, m.at);
        for (const auto &x : markers)
            if (x.at > m.at + 0.5 / kPpq) next = std::min(next, x.at);
        return std::max(next, m.at);
    }
    const Automation *Project::automation(const std::string &id) const { return findId(automations, id); }
    Binding *Project::binding(const std::string &address)
    {
        for (auto &x : bindings)
            if (x.address == address) return &x;
        return nullptr;
    }
    const Binding *Project::binding(const std::string &address) const { return const_cast<Project *>(this)->binding(address); }

    Rack *Project::rack(const std::string &track)
    {
        for (auto &r : racks)
            if (r.track == track) return &r;
        return nullptr;
    }

    const Rack *Project::rack(const std::string &track) const
    {
        for (const auto &r : racks)
            if (r.track == track) return &r;
        return nullptr;
    }

    DeviceNode *Project::device(const std::string &id, Rack **owner)
    {
        for (auto &r : racks)
            for (auto &d : r.devices)
                if (d.id == id)
                {
                    if (owner) *owner = &r;
                    return &d;
                }
        return nullptr;
    }

    std::vector<const Mixer *> Project::mixersInOrder() const
    {
        std::vector<const Mixer *> v;
        for (const auto &m : mixers) v.push_back(&m);
        std::stable_sort(v.begin(), v.end(), [](const Mixer *a, const Mixer *b) { return a->order < b->order; });
        return v;
    }

    const Mixer *Project::mixerOf(const Strip &s) const
    {
        if (!s.mixer.empty()) return mixer(s.mixer);
        const auto all = mixersInOrder();
        return all.empty() ? nullptr : all.front();
    }

    std::pair<int, int> Project::rankOf(const Strip &s) const
    {
        const Mixer *m = mixerOf(s);
        return {m ? m->order : 0, s.order};
    }

    std::vector<const Strip *> Project::stripsInOrder() const
    {
        std::vector<const Strip *> v;
        for (const auto &s : strips) v.push_back(&s);
        std::stable_sort(v.begin(), v.end(), [this](const Strip *a, const Strip *b) { return rankOf(*a) < rankOf(*b); });
        return v;
    }

    std::vector<std::string> Project::allIds() const
    {
        std::vector<std::string> ids;
        for (const auto &x : ports) ids.push_back(x.id);
        for (const auto &x : mixers) ids.push_back(x.id);
        for (const auto &x : strips) ids.push_back(x.id);
        for (const auto &x : sends) ids.push_back(x.id);
        for (const auto &r : racks)
            for (const auto &d : r.devices) ids.push_back(d.id);
        for (const auto &x : lanes) ids.push_back(x.id);
        for (const auto &x : groups) ids.push_back(x.id);
        for (const auto &x : patterns) ids.push_back(x.id);
        for (const auto &x : clips) ids.push_back(x.id);
        for (const auto &x : automations) ids.push_back(x.id);
        for (const auto &x : markers) ids.push_back(x.id);
        for (const auto &x : scenes) ids.push_back(x.id); // R-LAUNCH-1
        for (const auto &x : slots) ids.push_back(x.id);
        for (const auto &x : videos) ids.push_back(x.id); // R-VIDEO-1
        for (const auto &x : tempos) ids.push_back(x.id);
        for (const auto &x : versions) ids.push_back(x.id); // R-VER
        return ids;
    }

    const Version *Project::versionOf(const std::string &idOrName) const
    {
        for (const auto &v : versions)
            if (v.id == idOrName) return &v;
        for (const auto &v : versions)
            if (v.name == idOrName) return &v;
        return nullptr;
    }

    Version *Project::versionOf(const std::string &idOrName)
    {
        return const_cast<Version *>(static_cast<const Project &>(*this).versionOf(idOrName));
    }

    std::string Project::nextId(const std::string &prefix) const
    {
        long best = 0;
        const std::string p = prefix + "_";
        auto see = [&](const std::string &id) {
            if (id.compare(0, p.size(), p) != 0) return;
            const std::string tail = id.substr(p.size());
            if (tail.empty() || tail.find_first_not_of("0123456789") != std::string::npos) return;
            best = std::max(best, std::strtol(tail.c_str(), nullptr, 10));
        };
        for (const auto &id : allIds()) see(id);
        // R-VER: an id main or a version holds out of sight (a node the open version dropped, one only another version
        // has, one in a snapshot) is still taken — so a node one version adds never meets one its base adds later
        auto scan = [&](const std::string &text) {
            const std::string key = "id=" + p;
            for (size_t at = text.find(key); at != std::string::npos; at = text.find(key, at + 1))
            {
                if (at > 0 && text[at - 1] != ' ') continue;
                size_t end = at + 3;
                while (end < text.size() && text[end] != ' ' && text[end] != '\n') ++end;
                see(text.substr(at + 3, end - at - 3));
            }
        };
        scan(mainText);
        for (const auto &v : versions)
            for (const auto &l : v.lines) scan(l);
        return p + std::to_string(best + 1);
    }

    Project newProject(const std::string &id, const std::string &name, double bpm, const std::string &sig, int sampleRate)
    {
        Project p;
        p.header.id = id;
        p.header.name = name;
        p.header.bpm = bpm;
        p.header.sig = sig;
        p.header.sampleRate = sampleRate;
        p.ports.push_back(Port{"prt_1", "Main", "out", 2, 0, {}, {}});
        p.header.masterOut = {"prt_1"};
        p.mixers.push_back(Mixer{"mx_1", "Sources", 0, {}, {}});
        p.mixers.push_back(Mixer{"mx_2", "Buses", 1, {}, {}});
        Strip main;
        main.id = "ch_1";
        main.name = "Main";
        main.kind = "bus";
        main.mixer = "mx_2";
        p.strips.push_back(main);
        return p;
    }

    // ── parse ────────────────────────────────────────────────────────────────────────────────

    namespace
    {
        struct Reader
        {
            ParseReport *report;
            std::string err;

            // A number field: a non-finite or unreadable value is repaired to `def` and counted.
            double num(const std::string &key, const std::string &v, double def)
            {
                double x = 0;
                if (parseNumber(v, x) && std::isfinite(x)) return x;
                if (report)
                {
                    ++report->repaired;
                    report->notes.push_back(key + "=" + v + " repaired to " + canonicalNumber(def));
                }
                return def;
            }
            int integer(const std::string &key, const std::string &v, int def) { return (int)std::lround(num(key, v, def)); }
            bool boolean(const std::string &key, const std::string &v, bool def)
            {
                bool b = def;
                if (parseBool(v, b)) return b;
                if (report)
                {
                    ++report->repaired;
                    report->notes.push_back(key + "=" + v + " repaired to " + boolText(def));
                }
                return def;
            }
        };

        void apply(Reader &r, Port &n, const std::string &k, const std::string &v)
        {
            if (k == "id") n.id = v;
            else if (k == "name") n.name = v;
            else if (k == "dir") n.dir = v;
            else if (k == "channels") n.channels = r.integer(k, v, 2);
            else if (k == "order") n.order = r.integer(k, v, 0);
            else n.unknown.emplace_back(k, v);
        }
        void apply(Reader &r, Mixer &n, const std::string &k, const std::string &v)
        {
            if (k == "id") n.id = v;
            else if (k == "name") n.name = v;
            else if (k == "order") n.order = r.integer(k, v, 0);
            else n.unknown.emplace_back(k, v);
        }
        void apply(Reader &r, Strip &n, const std::string &k, const std::string &v)
        {
            if (k == "id") n.id = v;
            else if (k == "name") n.name = v;
            else if (k == "kind") n.kind = v;
            else if (k == "mixer") n.mixer = v;
            else if (k == "out") n.out = v;
            else if (k == "order") n.order = r.integer(k, v, 0);
            else if (k == "colour") n.colour = r.integer(k, v, -1);
            else if (k == "gain") n.gain = r.num(k, v, 0.0);
            else if (k == "pan") n.pan = std::clamp(r.num(k, v, 0.0), -1.0, 1.0);
            else if (k == "mute") n.mute = r.boolean(k, v, false);
            else if (k == "solo") n.solo = r.boolean(k, v, false);
            else if (k == "input") n.input = v;     // R-REC-1 — checked by validateProject
            else if (k == "monitor") n.monitor = v;
            else if (k == "frozen") n.frozen = v; // R-PERF-3
            else if (k == "vca") n.vca = v;       // R-MIX-19
            else if (k == "phase") n.phase = v == "invert" || v == "true" || v == "on"; // R-MIX-20
            else if (k == "width") n.width = r.num(k, v, 100.0);
            else if (k == "soloSafe") n.soloSafe = r.boolean(k, v, false);
            else n.unknown.emplace_back(k, v);
        }
        void apply(Reader &r, Send &n, const std::string &k, const std::string &v)
        {
            if (k == "id") n.id = v;
            else if (k == "from") n.from = v;
            else if (k == "to") n.to = v;
            else if (k == "gain") n.gain = r.num(k, v, 0.0);
            else if (k == "pre") n.pre = r.boolean(k, v, false);
            else if (k == "sidechain") n.sidechain = r.boolean(k, v, false);
            else n.unknown.emplace_back(k, v);
        }
        void apply(Reader &, Rack &n, const std::string &k, const std::string &v)
        {
            if (k == "track") n.track = v;
            else n.unknown.emplace_back(k, v);
        }
        void apply(Reader &r, DeviceNode &n, const std::string &k, const std::string &v)
        {
            if (k == "id") n.id = v;
            else if (k == "type") n.type = v;
            else if (k == "bypass") n.bypass = r.boolean(k, v, false);
            else if (k == "sample") n.sample = v;
            else if (n.type == "vst3" && k == "plugin") n.plugin = v; // R-VST-6
            else if (n.type == "vst3" && k == "bundle") n.bundle = v;
            else if (n.type == "vst3" && k == "name") n.pluginName = v;
            else if (n.type == "vst3" && k == "kind") n.pluginKind = v;
            else if (n.type == "vst3" && k == "state") n.state = v;
            else n.params.emplace_back(k, v); // the registry's (a plugin's p<id>), checked by the core
        }
        void apply(Reader &r, Lane &n, const std::string &k, const std::string &v)
        {
            if (k == "id") n.id = v;
            else if (k == "name") n.name = v;
            else if (k == "track") n.track = v;
            else if (k == "group") n.group = v; // R-ARR-3
            else if (k == "order") n.order = r.integer(k, v, 0);
            else if (k == "colour") n.colour = r.integer(k, v, -1);
            else n.unknown.emplace_back(k, v);
        }
        void apply(Reader &, Take &n, const std::string &k, const std::string &v)
        {
            if (k == "src") n.src = v;
            else n.unknown.emplace_back(k, v);
        }
        void apply(Reader &r, CompRange &n, const std::string &k, const std::string &v)
        {
            if (k == "take") n.take = r.integer(k, v, 1);
            else if (k == "from") n.from = toTick(r.num(k, v, 0.0));
            else if (k == "to") n.to = toTick(r.num(k, v, 0.0));
            else n.unknown.emplace_back(k, v);
        }
        void apply(Reader &, LaneGroup &n, const std::string &k, const std::string &v)
        {
            if (k == "id") n.id = v;
            else if (k == "name") n.name = v;
            else if (k == "folded") n.folded = v == "on" || v == "true" || v == "1";
            else n.unknown.emplace_back(k, v);
        }
        void apply(Reader &r, Pattern &n, const std::string &k, const std::string &v)
        {
            if (k == "id") n.id = v;
            else if (k == "name") n.name = v;
            else if (k == "length") n.length = toTick(r.num(k, v, 4.0));
            else if (k == "scale") n.scale = v; // checked by validateProject (R-ROLL-11)
            else n.unknown.emplace_back(k, v);
        }
        void apply(Reader &r, Note &n, const std::string &k, const std::string &v)
        {
            if (k == "pitch") n.pitch = std::clamp(r.integer(k, v, 60), 0, 127);
            else if (k == "vel") n.vel = std::clamp(r.integer(k, v, 100), 1, 127);
            else if (k == "at") n.at = toTick(r.num(k, v, 0.0));
            else if (k == "length") n.length = toTick(r.num(k, v, 0.25));
            else if (k == "chance") n.chance = std::clamp(r.num(k, v, 1.0), 0.0, 1.0); // R-ROLL-11
            else n.unknown.emplace_back(k, v);
        }
        void apply(Reader &r, Version &n, const std::string &k, const std::string &v) // R-VER
        {
            if (k == "id") n.id = v;
            else if (k == "name") n.name = v;
            else if (k == "base") n.base = v;
            else if (k == "frozen") n.frozen = r.boolean(k, v, false);
            else n.unknown.emplace_back(k, v);
        }
        void apply(Reader &r, TempoPoint &n, const std::string &k, const std::string &v)
        {
            if (k == "id") n.id = v;
            else if (k == "at") n.at = toTick(r.num(k, v, 0.0));
            else if (k == "bpm") n.bpm = r.num(k, v, 120.0);
            else if (k == "ramp") n.ramp = r.boolean(k, v, false);
            else n.unknown.emplace_back(k, v);
        }
        void apply(Reader &r, Video &n, const std::string &k, const std::string &v) // R-VIDEO-1
        {
            if (k == "id") n.id = v;
            else if (k == "src") n.src = v;
            else if (k == "offset") n.offset = r.num(k, v, 0.0);
            else n.unknown.emplace_back(k, v);
        }
        void apply(Reader &r, Scene &n, const std::string &k, const std::string &v) // R-LAUNCH-1
        {
            if (k == "id") n.id = v;
            else if (k == "name") n.name = v;
            else if (k == "order") n.order = (int)r.num(k, v, 0.0);
            else n.unknown.emplace_back(k, v);
        }
        void apply(Reader &r, Slot &n, const std::string &k, const std::string &v)
        {
            if (k == "id") n.id = v;
            else if (k == "name") n.name = v;
            else if (k == "scene") n.scene = v;
            else if (k == "strip") n.strip = v;
            else if (k == "pattern") n.pattern = v;
            else if (k == "src") n.src = v;
            else if (k == "in") n.in = r.num(k, v, 0.0);
            else if (k == "out") n.out = r.num(k, v, 0.0);
            else if (k == "length") n.length = toTick(r.num(k, v, 0.0));
            else if (k == "gain") n.gain = r.num(k, v, 0.0);
            else n.unknown.emplace_back(k, v);
        }
        void apply(Reader &r, Marker &n, const std::string &k, const std::string &v)
        {
            if (k == "id") n.id = v;
            else if (k == "name") n.name = v;
            else if (k == "note") n.note = v;
            else if (k == "at") n.at = toTick(r.num(k, v, 0.0));
            else if (k == "length") n.length = toTick(r.num(k, v, 0.0));
            else n.unknown.emplace_back(k, v);
        }
        void apply(Reader &r, ExprPoint &n, const std::string &k, const std::string &v)
        {
            if (k == "lane") n.lane = v;
            else if (k == "at") n.at = toTick(r.num(k, v, 0.0));
            else if (k == "value") n.value = r.num(k, v, 0.0);
            else n.unknown.emplace_back(k, v);
        }
        void apply(Reader &r, Clip &n, const std::string &k, const std::string &v)
        {
            if (k == "id") n.id = v;
            else if (k == "name") n.name = v;
            else if (k == "track") n.track = v;
            else if (k == "lane") n.lane = v;
            else if (k == "src") n.src = v;
            else if (k == "pattern") n.pattern = v;
            else if (k == "at") n.at = toTick(r.num(k, v, 0.0));
            else if (k == "in") n.in = r.num(k, v, 0.0);
            else if (k == "out") n.out = r.num(k, v, 0.0);
            else if (k == "length") n.length = toTick(r.num(k, v, 0.0));
            else if (k == "offset") n.offset = toTick(r.num(k, v, 0.0)); // R-LAUNCH-1
            else if (k == "gain") n.gain = r.num(k, v, 0.0);
            else if (k == "fadeIn") n.fadeIn = toTick(r.num(k, v, 0.0));
            else if (k == "fadeOut") n.fadeOut = toTick(r.num(k, v, 0.0));
            else if (k == "loop") n.loop = r.boolean(k, v, false);
            else if (k == "reverse") n.reverse = r.boolean(k, v, false); // R-EDIT-1
            else if (k == "warp") n.warp = r.boolean(k, v, false);       // R-WARP-1
            else if (k == "bpm") n.bpm = r.num(k, v, 0.0);
            else if (k == "pitch") n.pitch = r.num(k, v, 0.0);
            else n.unknown.emplace_back(k, v);
        }

        void apply(Reader &r, Automation &n, const std::string &k, const std::string &v)
        {
            if (k == "id") n.id = v;
            else if (k == "name") n.name = v;
            else if (k == "unit") n.unit = v;
            else if (k == "from") n.from = v;
            else if (k == "min") n.min = r.num(k, v, 0.0);
            else if (k == "max") n.max = r.num(k, v, 1.0);
            else if (k == "loop") n.loop = toTick(r.num(k, v, 0.0)); // R-AUTO-12
            else if (k == "at") n.at = toTick(r.num(k, v, 0.0));
            else if (k == "length") n.length = toTick(r.num(k, v, 0.0));
            else n.unknown.emplace_back(k, v);
        }
        void apply(Reader &r, AutoPoint &n, const std::string &k, const std::string &v)
        {
            if (k == "at") n.at = toTick(r.num(k, v, 0.0));
            else if (k == "value") n.value = r.num(k, v, 0.0);
            else if (k == "shape") n.shape = v;
            else if (k == "speedIn") n.speedIn = r.num(k, v, 0.0);       // R-AUTO-10: Interstellar's spelling
            else if (k == "inflIn") n.inflIn = r.num(k, v, 33.333);
            else if (k == "speedOut") n.speedOut = r.num(k, v, 0.0);
            else if (k == "inflOut") n.inflOut = r.num(k, v, 33.333);
            else n.unknown.emplace_back(k, v);
        }
        void apply(Reader &, Binding &n, const std::string &k, const std::string &v)
        {
            if (k == "address") n.address = v;
            else if (k == "formula") n.formula = v;
            else n.unknown.emplace_back(k, v);
        }

        void sortNotes(Pattern &p)
        {
            std::stable_sort(p.notes.begin(), p.notes.end(), [](const Note &a, const Note &b) {
                return a.at != b.at ? a.at < b.at : a.pitch < b.pitch;
            });
        }
    }

    bool parseProject(const std::string &text, Project &out, std::string &err, ParseReport *report)
    {
        Project p;
        p.header.masterOut.clear();
        ParseReport localReport;
        Reader r{report ? report : &localReport, {}};

        // What a continuation line or a comment attaches to.
        enum class Cur { None, Port, Mixer, Strip, Send, Rack, Device, Lane, Group, Pattern, Note, Expr, Clip, Auto, Point, Bind, Marker, Tempo, Raw, Version, Scene, Slot, Video };
        Cur cur = Cur::None;
        bool seenNode = false, sawMagic = false, sawTracks = false;
        std::map<std::string, std::vector<Note>> inlineNotes; // note clips written the suite's inline way

        auto remarksOfCurrent = [&]() -> Remarks * {
            switch (cur)
            {
            case Cur::Port: return &p.ports.back().remarks;
            case Cur::Mixer: return &p.mixers.back().remarks;
            case Cur::Strip: return &p.strips.back().remarks;
            case Cur::Send: return &p.sends.back().remarks;
            case Cur::Rack: return &p.racks.back().remarks;
            case Cur::Device: return &p.racks.back().devices.back().remarks;
            case Cur::Lane: return &p.lanes.back().remarks;
            case Cur::Group: return &p.groups.back().remarks;
            case Cur::Pattern:
            case Cur::Note: return &p.patterns.back().remarks;
            case Cur::Clip: return &p.clips.back().remarks;
            case Cur::Auto:
            case Cur::Point: return &p.automations.back().remarks;
            case Cur::Bind: return &p.bindings.back().remarks;
            case Cur::Marker: return &p.markers.back().remarks;
            case Cur::Scene: return &p.scenes.back().remarks;
            case Cur::Slot: return &p.slots.back().remarks;
            case Cur::Video: return &p.videos.back().remarks;
            case Cur::Tempo: return &p.tempos.back().remarks;
            case Cur::Version: return &p.versions.back().remarks;
            default: return nullptr;
            }
        };

        std::istringstream in(text);
        std::string line;
        int lineNo = 0;
        while (std::getline(in, line))
        {
            ++lineNo;
            if (!line.empty() && line.back() == '\r') line.pop_back();
            const std::string t = trim(line);
            const bool indented = !line.empty() && (line[0] == ' ' || line[0] == '\t');
            if (cur == Cur::Raw && indented) { p.raw.back().lines.push_back(line); continue; }
            if (cur == Cur::Version && indented) // R-VER: its deltas and nodes, kept as text — Versions.h reads them
            {
                p.versions.back().lines.push_back(line.compare(0, 2, "  ") == 0 ? line.substr(2) : trim(line));
                continue;
            }
            if (t.empty()) continue;
            if (t[0] == ';')
            {
                if (Remarks *rm = remarksOfCurrent()) rm->after.push_back(t);
                else if (cur == Cur::Raw) p.raw.back().lines.push_back(line);
                else p.header.comments.push_back(t);
                continue;
            }
            if (t[0] == '#')
            {
                seenNode = true;
                const LineTokens tok = tokenizeLine(t);
                auto fill = [&](auto &node) {
                    for (const auto &f : tok.fields) apply(r, node, f.first, f.second);
                    node.remarks.inlineComment = tok.comment;
                };
                if (tok.type == "aport") { p.ports.emplace_back(); fill(p.ports.back()); cur = Cur::Port; }
                else if (tok.type == "amixer") { p.mixers.emplace_back(); fill(p.mixers.back()); cur = Cur::Mixer; }
                else if (tok.type == "atrack") { p.strips.emplace_back(); fill(p.strips.back()); cur = Cur::Strip; }
                else if (tok.type == "asend") { p.sends.emplace_back(); fill(p.sends.back()); cur = Cur::Send; }
                else if (tok.type == "arack") { p.racks.emplace_back(); fill(p.racks.back()); cur = Cur::Rack; }
                else if (tok.type == "aeffect")
                {
                    if (p.racks.empty() || !(cur == Cur::Rack || cur == Cur::Device))
                    {
                        err = "line " + std::to_string(lineNo) + ": #aeffect outside an #arack";
                        return false;
                    }
                    p.racks.back().devices.emplace_back();
                    fill(p.racks.back().devices.back());
                    cur = Cur::Device;
                }
                else if (tok.type == "alane") { p.lanes.emplace_back(); fill(p.lanes.back()); cur = Cur::Lane; }
                else if (tok.type == "alanegroup") { p.groups.emplace_back(); fill(p.groups.back()); cur = Cur::Group; } // R-ARR-3
                else if (tok.type == "apattern") { p.patterns.emplace_back(); fill(p.patterns.back()); cur = Cur::Pattern; }
                else if (tok.type == "take" || tok.type == "comp") // R-REC-2: a clip's takes and its comp
                {
                    if (cur != Cur::Clip)
                    {
                        err = "line " + std::to_string(lineNo) + ": #" + tok.type + " outside an #aclip";
                        return false;
                    }
                    if (tok.type == "take") { Take x; for (const auto &f : tok.fields) apply(r, x, f.first, f.second); p.clips.back().takes.push_back(x); }
                    else { CompRange x; for (const auto &f : tok.fields) apply(r, x, f.first, f.second); p.clips.back().comp.push_back(x); }
                }
                else if (tok.type == "note")
                {
                    Note n;
                    for (const auto &f : tok.fields) apply(r, n, f.first, f.second);
                    if (cur == Cur::Pattern || cur == Cur::Note || cur == Cur::Expr) { p.patterns.back().notes.push_back(n); cur = Cur::Note; }
                    else if (cur == Cur::Clip) inlineNotes[p.clips.back().id].push_back(n);
                    else
                    {
                        err = "line " + std::to_string(lineNo) + ": #note outside an #apattern or a note #aclip";
                        return false;
                    }
                }
                else if (tok.type == "expr")
                {
                    // R-MIDI-3: a point of one of the pattern's expression lanes
                    if (!(cur == Cur::Pattern || cur == Cur::Note || cur == Cur::Expr))
                    {
                        err = "line " + std::to_string(lineNo) + ": #expr outside an #apattern";
                        return false;
                    }
                    ExprPoint x;
                    for (const auto &f : tok.fields) apply(r, x, f.first, f.second);
                    std::string canon;
                    if (exprLane(x.lane, canon)) x.lane = canon;
                    p.patterns.back().expr.push_back(x);
                    cur = Cur::Expr;
                }
                else if (tok.type == "aclip") { p.clips.emplace_back(); fill(p.clips.back()); cur = Cur::Clip; }
                else if (tok.type == "aauto") { p.automations.emplace_back(); fill(p.automations.back()); cur = Cur::Auto; }
                else if (tok.type == "point")
                {
                    if (!(cur == Cur::Auto || cur == Cur::Point))
                    {
                        err = "line " + std::to_string(lineNo) + ": #point outside an #aauto";
                        return false;
                    }
                    AutoPoint pt;
                    for (const auto &f : tok.fields) apply(r, pt, f.first, f.second);
                    p.automations.back().points.push_back(pt);
                    cur = Cur::Point;
                }
                else if (tok.type == "abind") { p.bindings.emplace_back(); fill(p.bindings.back()); cur = Cur::Bind; }
                else if (tok.type == "amarker") { p.markers.emplace_back(); fill(p.markers.back()); cur = Cur::Marker; } // R-ARR-1
                else if (tok.type == "atempo") { p.tempos.emplace_back(); fill(p.tempos.back()); cur = Cur::Tempo; }    // R-TEMPO-1
                else if (tok.type == "aversion") { p.versions.emplace_back(); fill(p.versions.back()); cur = Cur::Version; } // R-VER
                else if (tok.type == "ascene") { p.scenes.emplace_back(); fill(p.scenes.back()); cur = Cur::Scene; } // R-LAUNCH-1
                else if (tok.type == "aslot") { p.slots.emplace_back(); fill(p.slots.back()); cur = Cur::Slot; }
                else if (tok.type == "avideo") { p.videos.emplace_back(); fill(p.videos.back()); cur = Cur::Video; } // R-VIDEO-1
                else { p.raw.push_back(RawNode{{line}}); cur = Cur::Raw; }
                continue;
            }
            if (!seenNode)
            {
                const auto eq = t.find('=');
                if (eq == std::string::npos)
                {
                    err = "line " + std::to_string(lineNo) + ": expected `key = value` in the header, got `" + t + "`";
                    return false;
                }
                const std::string key = trim(t.substr(0, eq));
                LineTokens vt = tokenizeLine("v=" + trim(t.substr(eq + 1)));
                const std::string value = vt.fields.empty() ? std::string() : vt.fields[0].second;
                if (key == "arstro-project")
                {
                    if (value != "1") { err = "unsupported arstro-project version `" + value + "`"; return false; }
                    sawMagic = true;
                }
                else if (key == "app")
                {
                    if (value != "solaris") { err = "this is a `" + value + "` project, not a Solaris one"; return false; }
                }
                else if (key == "timebase")
                {
                    if (value != "beats") { err = "timebase `" + value + "`: a Solaris project counts in beats"; return false; }
                }
                else if (key == "ppq")
                {
                    if (value != std::to_string(kPpq)) { err = "ppq `" + value + "`: Solaris counts 960 ticks a beat"; return false; }
                }
                else if (key == "id") p.header.id = value;
                else if (key == "name") p.header.name = value;
                else if (key == "bpm") p.header.bpm = r.num(key, value, 120.0);
                else if (key == "sig") p.header.sig = value;
                else if (key == "sampleRate") p.header.sampleRate = r.integer(key, value, 48000);
                else if (key == "masterGain") p.header.masterGain = r.num(key, value, 0.0);
                else if (key == "masterOut") p.header.masterOut = splitComma(value);
                else if (key == "launchQuantize") // R-LAUNCH-1
                {
                    if (value != "bar" && value != "beat" && value != "none") { err = "launchQuantize `" + value + "`: bar, beat or none"; return false; }
                    p.header.launchQuantize = value;
                }
                else if (key == "tracks")
                {
                    if (value != "on") { err = "tracks `" + value + "`: the only value is `on` (R-LANE-3)"; return false; }
                    sawTracks = true;
                }
                else p.header.unknown.emplace_back(key, value);
                continue;
            }
            if (indented && (cur == Cur::Auto || cur == Cur::Point))
            {
                // the suite schema's breakpoint sketch, `<beats> = <value>` (audio-format §2.5 before
                // R-AUTO): read as a linear #point, written back as one
                const auto eq = t.find('=');
                double at = 0, v = 0;
                if (eq != std::string::npos && parseNumber(trim(t.substr(0, eq)), at) && parseNumber(trim(t.substr(eq + 1)), v))
                {
                    AutoPoint pt;
                    pt.at = toTick(at);
                    pt.value = v;
                    p.automations.back().points.push_back(pt);
                    cur = Cur::Point;
                    continue;
                }
            }
            if (indented && cur != Cur::None && cur != Cur::Raw)
            {
                // a continuation of the node above: more key=value fields
                const LineTokens tok = tokenizeLine(t);
                for (const auto &f : tok.fields)
                {
                    switch (cur)
                    {
                    case Cur::Port: apply(r, p.ports.back(), f.first, f.second); break;
                    case Cur::Mixer: apply(r, p.mixers.back(), f.first, f.second); break;
                    case Cur::Strip: apply(r, p.strips.back(), f.first, f.second); break;
                    case Cur::Send: apply(r, p.sends.back(), f.first, f.second); break;
                    case Cur::Rack: apply(r, p.racks.back(), f.first, f.second); break;
                    case Cur::Device: apply(r, p.racks.back().devices.back(), f.first, f.second); break;
                    case Cur::Lane: apply(r, p.lanes.back(), f.first, f.second); break;
                    case Cur::Pattern: apply(r, p.patterns.back(), f.first, f.second); break;
                    case Cur::Note: apply(r, p.patterns.back().notes.back(), f.first, f.second); break;
                    case Cur::Clip: apply(r, p.clips.back(), f.first, f.second); break;
                    case Cur::Auto: apply(r, p.automations.back(), f.first, f.second); break;
                    case Cur::Point: apply(r, p.automations.back().points.back(), f.first, f.second); break;
                    case Cur::Bind: apply(r, p.bindings.back(), f.first, f.second); break;
                    default: break;
                    }
                }
                continue;
            }
            err = "line " + std::to_string(lineNo) + ": cannot read `" + t + "`";
            return false;
        }
        if (!sawMagic)
        {
            err = "not an arstro project (no `arstro-project = 1` header)";
            return false;
        }

        // The suite's inline notes become a pattern of their own (the only normalisation).
        for (auto &c : p.clips)
        {
            auto it = inlineNotes.find(c.id);
            if (it == inlineNotes.end() || !c.pattern.empty()) continue;
            Pattern pt;
            pt.id = p.nextId("pt");
            pt.name = c.name.empty() ? c.id : c.name;
            pt.notes = it->second;
            double end = 0;
            for (const auto &n : pt.notes) end = std::max(end, n.at + n.length);
            pt.length = c.length > 0 ? c.length : std::max(1.0, std::ceil(end));
            c.pattern = pt.id;
            r.report->notes.push_back("the inline notes of " + c.id + " became pattern " + pt.id);
            p.patterns.push_back(pt);
        }
        for (auto &pt : p.patterns) sortNotes(pt);
        p.header.tracks = sawTracks; // R-LANE-3: without the line the service adopts the lanes that are tracks in all but name
        for (auto &a : p.automations)
        {
            // the sketch's `node=… param=… interp=…` become `from` and the points' shape
            std::string node, param, interp;
            Fields keep;
            for (const auto &f : a.unknown)
            {
                if (f.first == "node") node = f.second;
                else if (f.first == "param") param = f.second;
                else if (f.first == "interp") interp = f.second;
                else keep.push_back(f);
            }
            if (!node.empty() || !param.empty() || !interp.empty())
            {
                a.unknown = keep;
                if (a.from.empty() && !node.empty()) a.from = node + (param.empty() ? "" : "." + param);
                if (a.name.empty()) a.name = a.from.empty() ? a.id : a.from;
                if (interp == "hold" || interp == "smooth")
                    for (auto &pt : a.points) pt.shape = interp;
                r.report->notes.push_back(a.id + ": the schema sketch's node/param/interp became from=" + a.from);
            }
            std::stable_sort(a.points.begin(), a.points.end(), [](const AutoPoint &x, const AutoPoint &y) { return x.at < y.at; });
        }

        const auto errors = validateProject(p);
        if (!errors.empty())
        {
            err = errors.front();
            if (errors.size() > 1) err += " (and " + std::to_string(errors.size() - 1) + " more)";
            return false;
        }
        out = std::move(p);
        return true;
    }

    // ── serialize ────────────────────────────────────────────────────────────────────────────

    namespace
    {
        struct Line
        {
            std::string s;
            Line(const std::string &type) : s("#" + type) {}
            Line &kv(const std::string &k, const std::string &v) { s += " " + k + "=" + v; return *this; }
            Line &str(const std::string &k, const std::string &v) { return kv(k, quoteIfNeeded(v)); }
            Line &strIf(const std::string &k, const std::string &v) { return v.empty() ? *this : str(k, v); }
            Line &kvIf(bool when, const std::string &k, const std::string &v) { return when ? kv(k, v) : *this; }
            Line &unknown(const Fields &f)
            {
                for (const auto &x : f) str(x.first, x.second);
                return *this;
            }
        };

        void emit(std::string &out, const std::string &indent, const Line &l, const Remarks &rm)
        {
            out += indent + l.s;
            if (!rm.inlineComment.empty()) out += "  ; " + rm.inlineComment;
            out += "\n";
            for (const auto &c : rm.after) out += indent + c + "\n";
        }

        std::string headerLine(const std::string &key, const std::string &value)
        {
            std::string k = key;
            if (k.size() < 11) k.append(11 - k.size(), ' ');
            else k += " ";
            return k + "= " + value + "\n";
        }
    }

    std::string serializeProject(const Project &p)
    {
        // R-VER: the song is main's — the open version's nodes are its resolution, written by its deltas below
        std::string out = p.version.empty() ? songText(p) : p.mainText;
        for (const auto &v : p.versions)
        {
            Line l("aversion");
            l.kv("id", v.id).str("name", v.name).strIf("base", v.base).kvIf(v.frozen, "frozen", "true").unknown(v.unknown);
            out += "\n";
            emit(out, "", l, Remarks{v.remarks.inlineComment, {}});
            for (const auto &x : v.lines) out += "  " + x + "\n";
            for (const auto &c : v.remarks.after) out += c + "\n";
        }
        return out;
    }

    std::string songText(const Project &p)
    {
        std::string out;
        const Header &h = p.header;
        out += headerLine("arstro-project", "1");
        out += headerLine("app", "solaris");
        out += headerLine("id", quoteIfNeeded(h.id));
        if (!h.name.empty()) out += headerLine("name", quoteIfNeeded(h.name));
        out += headerLine("timebase", "beats");
        out += headerLine("bpm", canonicalNumber(h.bpm));
        out += headerLine("sig", h.sig);
        out += headerLine("ppq", std::to_string(kPpq));
        out += headerLine("sampleRate", std::to_string(h.sampleRate));
        out += headerLine("masterGain", canonicalNumber(h.masterGain));
        if (!h.masterOut.empty())
        {
            std::string v;
            for (size_t i = 0; i < h.masterOut.size(); ++i) v += (i ? "," : "") + h.masterOut[i];
            out += headerLine("masterOut", v);
        }
        out += headerLine("tracks", "on"); // R-LANE-3: lanes may be tracks — a file without the line predates them
        if (h.launchQuantize != "bar") out += headerLine("launchQuantize", h.launchQuantize); // R-LAUNCH-1: only when not a bar
        for (const auto &u : h.unknown) out += headerLine(u.first, quoteIfNeeded(u.second));
        for (const auto &c : h.comments) out += c + "\n";

        auto group = [&out](bool any) { if (any) out += "\n"; };

        group(!p.ports.empty());
        for (const auto &n : p.ports)
            emit(out, "", Line("aport").kv("id", n.id).str("name", n.name).kv("dir", n.dir)
                              .kv("channels", std::to_string(n.channels)).kv("order", std::to_string(n.order)).unknown(n.unknown), n.remarks);
        group(!p.mixers.empty());
        for (const auto &n : p.mixers)
            emit(out, "", Line("amixer").kv("id", n.id).str("name", n.name).kv("order", std::to_string(n.order)).unknown(n.unknown), n.remarks);
        group(!p.strips.empty());
        for (const auto &n : p.strips)
        {
            Line l("atrack");
            l.kv("id", n.id).str("name", n.name).kv("kind", n.kind).strIf("mixer", n.mixer).kv("order", std::to_string(n.order))
                .kv("gain", canonicalNumber(n.gain)).kv("pan", canonicalNumber(n.pan));
            if (n.mute) l.kv("mute", "true");
            if (n.solo) l.kv("solo", "true");
            l.strIf("out", n.out);
            if (n.colour >= 0) l.kv("colour", std::to_string(n.colour));
            l.strIf("input", n.input).kvIf(n.monitor != "auto", "monitor", n.monitor); // R-REC-1: only when set
            l.strIf("frozen", n.frozen);                                              // R-PERF-3
            l.strIf("vca", n.vca);                                                    // R-MIX-19
            l.kvIf(n.phase, "phase", "invert");                                       // R-MIX-20: only when set
            if (n.width != 100.0) l.kv("width", canonicalNumber(n.width));
            l.kvIf(n.soloSafe, "soloSafe", "true");
            emit(out, "", l.unknown(n.unknown), n.remarks);
        }
        group(!p.sends.empty());
        for (const auto &n : p.sends)
            emit(out, "", Line("asend").kv("id", n.id).kv("from", n.from).kv("to", n.to).kv("gain", canonicalNumber(n.gain))
                              .kv("pre", boolText(n.pre)).kvIf(n.sidechain, "sidechain", "true").unknown(n.unknown), n.remarks);
        group(!p.racks.empty());
        for (const auto &r : p.racks)
        {
            emit(out, "", Line("arack").kv("track", r.track).unknown(r.unknown), r.remarks);
            for (const auto &d : r.devices)
            {
                Line l("aeffect");
                l.kv("id", d.id).kv("type", d.type);
                if (d.bypass) l.kv("bypass", "true");
                l.strIf("sample", d.sample);
                l.strIf("plugin", d.plugin).strIf("name", d.pluginName).strIf("kind", d.pluginKind).strIf("bundle", d.bundle);
                for (const auto &kv : d.params) l.str(kv.first, kv.second);
                l.strIf("state", d.state); // last: the long one
                emit(out, "  ", l, d.remarks);
            }
        }
        group(!p.groups.empty()); // R-ARR-3: the groups before the lanes that name them
        for (const auto &n : p.groups)
            emit(out, "", Line("alanegroup").kv("id", n.id).str("name", n.name).kvIf(n.folded, "folded", "on").unknown(n.unknown), n.remarks);
        group(!p.lanes.empty());
        for (const auto &n : p.lanes)
        {
            Line l("alane");
            l.kv("id", n.id).str("name", n.name).kvIf(!n.track.empty(), "track", n.track).kvIf(!n.group.empty(), "group", n.group).kv("order", std::to_string(n.order));
            if (n.colour >= 0) l.kv("colour", std::to_string(n.colour));
            emit(out, "", l.unknown(n.unknown), n.remarks);
        }
        group(!p.patterns.empty());
        for (const auto &n : p.patterns)
        {
            emit(out, "", Line("apattern").kv("id", n.id).strIf("name", n.name).kv("length", canonicalBeats(n.length)).strIf("scale", n.scale)
                              .unknown(n.unknown), n.remarks);
            for (const auto &x : n.notes)
                out += "  " + Line("note").kv("pitch", std::to_string(x.pitch)).kv("at", canonicalBeats(x.at))
                                  .kv("length", canonicalBeats(x.length)).kv("vel", std::to_string(x.vel))
                                  .kvIf(x.chance < 1.0, "chance", canonicalNumber(x.chance)).unknown(x.unknown).s + "\n";
            for (const auto &x : n.expr) // R-MIDI-3
                out += "  " + Line("expr").kv("lane", x.lane).kv("at", canonicalBeats(x.at)).kv("value", canonicalNumber(x.value)).unknown(x.unknown).s + "\n";
        }
        group(!p.clips.empty());
        for (const auto &n : p.clips)
        {
            Line l("aclip");
            l.kv("id", n.id).strIf("name", n.name).kv("track", n.track).strIf("lane", n.lane);
            if (n.isAudio())
            {
                l.str("src", n.src).kv("at", canonicalBeats(n.at)).kv("in", canonicalSeconds(n.in)).kv("out", canonicalSeconds(n.out));
                if (n.length > 0) l.kv("length", canonicalBeats(n.length));
                if (n.gain != 0) l.kv("gain", canonicalNumber(n.gain));
                if (n.fadeIn > 0) l.kv("fadeIn", canonicalBeats(n.fadeIn));
                if (n.fadeOut > 0) l.kv("fadeOut", canonicalBeats(n.fadeOut));
                if (n.loop) l.kv("loop", "true");
                if (n.reverse) l.kv("reverse", "true"); // R-EDIT-1: written only when set
                if (n.warp) l.kv("warp", "true");       // R-WARP-1: likewise
                if (n.bpm > 0) l.kv("bpm", canonicalNumber(n.bpm));
                if (n.pitch != 0) l.kv("pitch", canonicalNumber(n.pitch));
            }
            else
            {
                l.strIf("pattern", n.pattern).kv("at", canonicalBeats(n.at));
                if (n.length > 0) l.kv("length", canonicalBeats(n.length));
                if (n.offset > 0) l.kv("offset", canonicalBeats(n.offset)); // R-LAUNCH-1: written only when set
                if (n.gain != 0) l.kv("gain", canonicalNumber(n.gain));
            }
            emit(out, "", l.unknown(n.unknown), n.remarks);
            for (const auto &x : n.takes) // R-REC-2
                out += "  " + Line("take").str("src", x.src).unknown(x.unknown).s + "\n";
            for (const auto &x : n.comp)
                out += "  " + Line("comp").kv("take", std::to_string(x.take)).kv("from", canonicalBeats(x.from)).kv("to", canonicalBeats(x.to)).unknown(x.unknown).s + "\n";
        }
        group(!p.automations.empty());
        for (const auto &n : p.automations)
        {
            Line l("aauto");
            l.kv("id", n.id).str("name", n.name).strIf("unit", n.unit).kv("min", canonicalNumber(n.min)).kv("max", canonicalNumber(n.max))
                .strIf("from", n.from);
            if (n.loop > 0) // R-AUTO-12: written only when it loops — a song that never loops reads as it always did
            {
                l.kv("loop", canonicalBeats(n.loop));
                if (n.at > 0) l.kv("at", canonicalBeats(n.at));
                if (n.length > 0) l.kv("length", canonicalBeats(n.length));
            }
            emit(out, "", l.unknown(n.unknown), n.remarks);
            for (const auto &x : n.points)
            {
                Line pl("point");
                pl.kv("at", canonicalBeats(x.at)).kv("value", canonicalNumber(x.value));
                if (x.shape != "linear") pl.kv("shape", x.shape);
                if (x.shape == "bezier")
                    pl.kv("speedIn", canonicalNumber(x.speedIn)).kv("inflIn", canonicalNumber(x.inflIn))
                        .kv("speedOut", canonicalNumber(x.speedOut)).kv("inflOut", canonicalNumber(x.inflOut));
                out += "  " + pl.unknown(x.unknown).s + "\n";
            }
        }
        group(!p.bindings.empty());
        for (const auto &n : p.bindings)
            emit(out, "", Line("abind").kv("address", n.address).str("formula", n.formula).unknown(n.unknown), n.remarks);
        group(!p.markers.empty()); // R-ARR-1
        for (const auto &n : p.markers)
            emit(out, "", Line("amarker").kv("id", n.id).str("name", n.name).kv("at", canonicalBeats(n.at)).kvIf(n.length > 0, "length", canonicalBeats(n.length))
                              .strIf("note", n.note).unknown(n.unknown), n.remarks);
        group(!p.tempos.empty()); // R-TEMPO-1
        for (const auto &n : p.tempos)
            emit(out, "", Line("atempo").kv("id", n.id).kv("at", canonicalBeats(n.at)).kv("bpm", canonicalNumber(n.bpm)).kvIf(n.ramp, "ramp", "true").unknown(n.unknown),
                 n.remarks);
        group(!p.scenes.empty()); // R-LAUNCH-1
        for (const auto &n : p.scenes)
            emit(out, "", Line("ascene").kv("id", n.id).str("name", n.name).kv("order", std::to_string(n.order)).unknown(n.unknown), n.remarks);
        group(!p.slots.empty());
        for (const auto &n : p.slots)
            emit(out, "",
                 Line("aslot").kv("id", n.id).strIf("name", n.name).kv("scene", n.scene).kv("strip", n.strip).strIf("pattern", n.pattern).strIf("src", n.src)
                     .kvIf(n.in != 0, "in", canonicalSeconds(n.in)).kvIf(n.out != 0, "out", canonicalSeconds(n.out))
                     .kvIf(n.length > 0, "length", canonicalBeats(n.length)).kvIf(n.gain != 0, "gain", canonicalNumber(n.gain)).unknown(n.unknown),
                 n.remarks);
        group(!p.videos.empty()); // R-VIDEO-1
        for (const auto &n : p.videos)
            emit(out, "", Line("avideo").kv("id", n.id).str("src", n.src).kv("offset", canonicalNumber(n.offset)).unknown(n.unknown), n.remarks);
        group(!p.raw.empty());
        for (const auto &r : p.raw)
            for (const auto &l : r.lines) out += l + "\n";
        return out;
    }

    // ── validate ─────────────────────────────────────────────────────────────────────────────

    bool feedsForward(const Project &p, const Strip &from, const Strip &to)
    {
        // later in processing order = later in stripsInOrder(): (mixer order, strip order), a tie kept in
        // the file's order by its stable sort
        if (&from == &to) return false;
        const auto a = p.rankOf(from), b = p.rankOf(to);
        if (a != b) return a < b;
        const Strip *first = p.strips.data(), *last = first + p.strips.size();
        return &from >= first && &from < last && &to >= first && &to < last && &from < &to;
    }

    std::vector<std::string> keyTargetsOf(const Project &p, const Strip &s)
    {
        std::vector<std::string> out;
        for (const auto &id : targetsOf(p, s))
            if (p.strip(id)) out.push_back(id);
        return out;
    }

    namespace
    {
        std::vector<const Port *> outPortsInOrder(const Project &p)
        {
            std::vector<const Port *> ports;
            for (const auto &pt : p.ports)
                if (pt.dir == "out") ports.push_back(&pt);
            std::stable_sort(ports.begin(), ports.end(), [](const Port *x, const Port *y) { return x->order < y->order; });
            return ports;
        }
    }

    std::vector<std::string> targetsOf(const Project &p, const Strip &s)
    {
        std::vector<std::string> out;
        if (s.kind == "vca") return out; // R-MIX-19: a VCA carries no sound — it goes nowhere
        for (const Strip *t : p.stripsInOrder())
            if (t->kind != "vca" && feedsForward(p, s, *t)) out.push_back(t->id); // … and nothing goes to one
        out.push_back("master");
        for (const Port *pt : outPortsInOrder(p)) out.push_back(pt->id);
        return out;
    }

    std::vector<std::string> matrixColumnsOf(const Project &p)
    {
        std::set<std::string> reached;
        for (const auto &s : p.strips)
        {
            for (const auto &t : targetsOf(p, s)) reached.insert(t);
            reached.insert(s.out); // a route the rule refuses still shows (a file edited by hand)
        }
        for (const auto &sd : p.sends) reached.insert(sd.to);
        std::vector<std::string> cols;
        for (const Strip *s : p.stripsInOrder())
            if (reached.count(s->id)) cols.push_back(s->id);
        cols.push_back("master");
        for (const Port *pt : outPortsInOrder(p)) cols.push_back(pt->id);
        return cols;
    }

    std::vector<std::string> validateProject(const Project &p)
    {
        std::vector<std::string> e;
        {
            std::set<std::string> seen;
            for (const auto &id : p.allIds())
            {
                if (id.empty()) e.push_back("a node has no id");
                else if (!seen.insert(id).second) e.push_back("two nodes share the id `" + id + "`");
            }
            if (seen.count("master")) e.push_back("`master` is reserved and cannot be a node id");
        }
        // R-VER: a version is named, once, never `main`; its base is main or an EARLIER version (so no chain loops); the
        // open one is one of them; a frozen one holds its snapshot
        {
            std::set<std::string> ids, names;
            for (const auto &v : p.versions)
            {
                if (v.name.empty()) e.push_back("version " + v.id + " has no name");
                else if (v.name == "main") e.push_back("version " + v.id + " cannot be called `main` — that is the song itself");
                else if (!names.insert(v.name).second) e.push_back("two versions are called `" + v.name + "`");
                if (!v.base.empty() && !ids.count(v.base))
                    e.push_back("version " + v.name + "'s base `" + v.base + "` is no version before it" +
                                (p.versionOf(v.base) ? " (a base comes first, so no chain loops)" : ""));
                if (v.frozen && std::find(v.lines.begin(), v.lines.end(), "#vsnapshot") == v.lines.end())
                    e.push_back("version " + v.name + " is frozen but holds no #vsnapshot");
                ids.insert(v.id);
            }
            if (!p.version.empty() && !ids.count(p.version)) e.push_back("the open version `" + p.version + "` is no version");
        }
        auto describe = [&p](const Strip &s) {
            const Mixer *m = p.mixerOf(s);
            return s.id + " (" + s.name + ", on " + (m ? m->name : std::string("no mixer")) + ")";
        };
        // R-MIX-4: a target is "master", an output port, or a strip LATER in processing order.
        auto checkTarget = [&](const Strip &from, const std::string &to, const std::string &what) {
            if (to.empty() || to == "master") return;
            if (const Port *pt = p.port(to))
            {
                if (pt->dir != "out") e.push_back(from.id + "'s " + what + " goes to " + to + ", an INPUT port");
                return;
            }
            const Strip *t = p.strip(to);
            if (!t) { e.push_back(from.id + "'s " + what + " names `" + to + "`, which is no strip, port or `master`"); return; }
            if (t == &from) { e.push_back(from.id + "'s " + what + " goes to itself"); return; }
            if (t->kind == "vca") { e.push_back(from.id + "'s " + what + " goes to " + t->id + ", a VCA — it carries no sound: it moves its members' faders (R-MIX-19)"); return; }
            if (!feedsForward(p, from, *t))
                e.push_back(describe(from) + " " + what + " → " + describe(*t) +
                            ": a strip can only feed a strip LATER in processing order (a later mixer, or after it on its own), the master or a port (R-MIX-4)");
        };
        for (const auto &s : p.strips)
        {
            if (s.kind != "audio" && s.kind != "instrument" && s.kind != "bus" && s.kind != "vca")
                e.push_back(s.id + " has kind `" + s.kind + "` (audio | instrument | bus | vca)");
            if (!s.mixer.empty() && !p.mixer(s.mixer)) e.push_back(s.id + " lives on mixer `" + s.mixer + "`, which does not exist");
            if (s.width < 0.0 || s.width > 200.0) e.push_back(s.id + " has width " + canonicalNumber(s.width) + " % (0 … 200, R-MIX-20)");
            if (s.soloSafe && s.kind != "bus") e.push_back(s.id + " is solo-safe but a " + s.kind + " strip — solo-safe is a bus's (R-MIX-20)");
            if (s.kind == "vca" && (s.phase || s.width != 100.0)) e.push_back(s.id + " is a VCA — it carries no sound: no phase or width (R-MIX-20)");
            if (s.kind == "vca")
            {
                // R-MIX-19: a VCA carries no sound — no output of its own; it follows no VCA (not yet)
                if (!s.out.empty() && s.out != "master") e.push_back(s.id + " is a VCA — it carries no sound, so it has no output (R-MIX-19)");
                if (!s.vca.empty()) e.push_back(s.id + " is a VCA — a VCA follows no VCA (not yet, R-MIX-19)");
            }
            else checkTarget(s, s.out, "output");
            if (!s.vca.empty() && s.kind != "vca")
            {
                const Strip *v = p.strip(s.vca);
                if (!v) e.push_back(s.id + " follows VCA `" + s.vca + "`, which is no strip");
                else if (v->kind != "vca") e.push_back(s.id + " follows " + v->id + ", a " + v->kind + " strip — only a VCA moves others (R-MIX-19)");
            }
        }
        for (const auto &sd : p.sends)
        {
            const Strip *from = p.strip(sd.from);
            if (!from) { e.push_back(sd.id + " sends from `" + sd.from + "`, which is no strip"); continue; }
            if (from->kind == "vca") { e.push_back(sd.id + " sends from " + from->id + ", a VCA — it carries no sound (R-MIX-19)"); continue; }
            if (sd.to.empty()) e.push_back(sd.id + " has no target");
            if (sd.sidechain)
            {
                // a key goes to a strip's detector: a strip later in processing order, any mixer (R-MIX-15)
                const Strip *t = p.strip(sd.to);
                if (!t) e.push_back(sd.id + " is a sidechain key, so it must go to a strip — `" + sd.to + "` is not one");
                else if (t->kind == "vca") e.push_back(sd.id + " keys " + t->id + ", a VCA — it carries no sound (R-MIX-19)");
                else if (!feedsForward(p, *from, *t))
                    e.push_back(describe(*from) + " key " + sd.id + " → " + describe(*t) +
                                ": a sidechain key can only go to a strip LATER in processing order (R-MIX-15)");
            }
            else
                checkTarget(*from, sd.to, "send " + sd.id);
        }
        {
            std::set<std::string> tracks;
            for (const auto &r : p.racks)
            {
                if (r.track != "master" && !p.strip(r.track)) e.push_back("a rack belongs to `" + r.track + "`, which is no strip");
                else if (r.track != "master" && p.strip(r.track)->kind == "vca" && !r.devices.empty())
                    e.push_back(r.track + " is a VCA — it carries no sound, so it holds no device (R-MIX-19)");
                if (!tracks.insert(r.track).second) e.push_back("two racks belong to `" + r.track + "`");
                for (const auto &d : r.devices)
                    if (d.type.empty()) e.push_back("device " + d.id + " has no type");
            }
        }
        for (const auto &pt : p.patterns)
        {
            if (!(pt.length > 0)) e.push_back("pattern " + pt.id + " has no length");
            std::string canon, why;
            int mask = 0;
            if (!pt.scale.empty() && !parseScale(pt.scale, canon, mask, why)) e.push_back("pattern " + pt.id + "'s scale: " + why); // R-ROLL-11
            for (const auto &x : pt.expr)
            {
                // R-MIDI-3: a lane we know, a value in its range
                std::string canon;
                double lo = 0, hi = 1;
                if (!exprLane(x.lane, canon)) { e.push_back("pattern " + pt.id + " has an expression lane `" + x.lane + "` (bend, mod or cc0…cc127)"); continue; }
                exprRange(canon, lo, hi);
                if (x.value < lo - 1e-9 || x.value > hi + 1e-9 || x.at < 0)
                    e.push_back("pattern " + pt.id + "'s " + canon + " point at " + canonicalBeats(x.at) + " is out of range (" + canonicalNumber(lo) + " … " + canonicalNumber(hi) + ")");
            }
        }
        for (const auto &c : p.clips) // R-REC-2: takes are an audio clip's; a comp names one of them, forwards
        {
            if (!c.takes.empty() && !c.isAudio()) e.push_back("clip " + c.id + " has takes but is not audio");
            for (const auto &x : c.comp)
                if (x.take < 1 || x.take > (int)c.takes.size() || !(x.to > x.from))
                    e.push_back("clip " + c.id + "'s comp names take " + std::to_string(x.take) + " over " + canonicalBeats(x.from) + " … " + canonicalBeats(x.to) +
                                " (it has " + std::to_string(c.takes.size()) + ")");
        }
        for (const auto &s : p.strips) // R-REC-1: an input is an input port; monitoring one of three
        {
            if (!s.input.empty())
            {
                const Port *pt = nullptr;
                for (const auto &x : p.ports)
                    if (x.id == s.input) pt = &x;
                if (!pt) e.push_back("strip " + s.id + "'s input `" + s.input + "` is not a port of the song");
                else if (pt->dir != "in") e.push_back("strip " + s.id + "'s input " + s.input + " is an output port");
            }
            if (s.monitor != "auto" && s.monitor != "on" && s.monitor != "off")
                e.push_back("strip " + s.id + "'s monitor is `" + s.monitor + "` (auto, on or off)");
        }
        for (const auto &l : p.lanes) // R-ARR-3: a lane's group is one the song has
            if (!l.group.empty() && !p.group(l.group)) e.push_back("lane " + l.id + " is in group `" + l.group + "`, which the song does not have");
        for (size_t i = 0; i < p.tempos.size(); ++i) // R-TEMPO-1: after the start, each at its own beat, in order, a tempo
        {
            const auto &t = p.tempos[i];
            if (t.at <= 0) e.push_back("tempo " + t.id + " is at beat " + canonicalBeats(t.at) + " — the tempo at the start is the song's bpm");
            if (i > 0 && t.at <= p.tempos[i - 1].at) e.push_back("tempo " + t.id + " is not after " + p.tempos[i - 1].id);
            if (!(t.bpm >= 20 && t.bpm <= 999)) e.push_back("tempo " + t.id + " is " + canonicalNumber(t.bpm) + " bpm (20 … 999)");
        }
        auto aKind = [](const std::string &k) { return (k == "instrument" || k == "audio" ? "an " : "a ") + k + " strip"; };
        for (const auto &c : p.clips) // R-LAUNCH-1: a note clip may start into its pattern; an audio clip starts into its file (`in`)
            if (c.offset < 0 || (c.offset > 0 && c.isAudio())) e.push_back("clip " + c.id + ": offset is a note clip's, beats ≥ 0 into its pattern");
        if (p.videos.size() > 1) e.push_back("the song has " + std::to_string(p.videos.size()) + " videos — one picture a song"); // R-VIDEO-1
        for (const auto &v : p.videos)
            if (v.src.empty()) e.push_back("video " + v.id + " names no file");
        for (const auto &sl : p.slots) // R-LAUNCH-1: a slot waits in a scene for a strip that can play it
        {
            if (!p.scene(sl.scene)) e.push_back("slot " + sl.id + " is in `" + sl.scene + "`, which is no scene");
            const Strip *st = p.strip(sl.strip);
            if (!st) { e.push_back("slot " + sl.id + " is for `" + sl.strip + "`, which is no strip"); continue; }
            if (sl.pattern.empty() == sl.src.empty()) e.push_back("slot " + sl.id + " needs a pattern or a file — one of them");
            else if (!sl.pattern.empty())
            {
                if (st->kind != "instrument") e.push_back("slot " + sl.id + " plays notes, but " + st->id + " is " + aKind(st->kind));
                if (!p.pattern(sl.pattern)) e.push_back("slot " + sl.id + " plays `" + sl.pattern + "`, which is no pattern");
            }
            else if (st->kind != "audio") e.push_back("slot " + sl.id + " plays a file, but " + st->id + " is " + aKind(st->kind));
            if (sl.length < 0 || (sl.out != 0 && sl.out <= sl.in)) e.push_back("slot " + sl.id + " lasts nothing");
            for (const auto &o : p.slots)
                if (&o != &sl && o.scene == sl.scene && o.strip == sl.strip && &o < &sl)
                    e.push_back("slots " + o.id + " and " + sl.id + " are both " + sl.strip + "'s in scene " + sl.scene + " — a cell holds one");
        }
        for (const auto &m : p.markers) // R-ARR-1: a place in the song, named
        {
            if (m.name.empty()) e.push_back("marker " + m.id + " has no name");
            if (m.at < 0 || m.length < 0) e.push_back("marker " + m.id + " is before the song's start or shorter than nothing");
        }
        for (const auto &l : p.lanes)
        {
            // R-LANE-3: a track belongs to an instrument
            if (l.track.empty()) continue;
            const Strip *s = p.strip(l.track);
            if (!s) e.push_back("lane " + l.id + " is the track of `" + l.track + "`, which is no strip");
            else if (s->kind != "instrument") e.push_back("lane " + l.id + " is the track of " + s->id + ", a " + s->kind + " strip — a track belongs to an instrument");
        }
        for (const auto &c : p.clips)
        {
            const Strip *s = p.strip(c.track);
            if (!s) { e.push_back("clip " + c.id + " plays through `" + c.track + "`, which is no strip"); continue; }
            if (!c.lane.empty() && !p.lane(c.lane)) e.push_back("clip " + c.id + " is on lane `" + c.lane + "`, which does not exist");
            if (const Lane *l = c.lane.empty() ? nullptr : p.lane(c.lane); l && !l->track.empty() && l->track != c.track)
                e.push_back((c.isAudio() ? "audio clip " : "note clip ") + c.id + " is on " + l->id + ", the track of " + l->track +
                            " — " + (c.isAudio() ? "audio cannot play through an instrument; put it on a lane of its own" : "but plays through " + c.track) + " (R-LANE-3)");
            if (c.at < 0) e.push_back("clip " + c.id + " starts before the song (at " + canonicalBeats(c.at) + ")");
            if (c.isAudio())
            {
                if (s->kind != "audio") e.push_back("audio clip " + c.id + " is on " + s->id + ", a " + s->kind + " strip — it needs an audio strip");
                if (!(c.in < c.out)) e.push_back("audio clip " + c.id + " has in " + canonicalSeconds(c.in) + " ≥ out " + canonicalSeconds(c.out));
            }
            else
            {
                if (s->kind != "instrument") e.push_back("note clip " + c.id + " is on " + s->id + ", a " + s->kind + " strip — it needs an instrument strip");
                if (c.pattern.empty()) e.push_back("clip " + c.id + " has neither a src nor a pattern");
                else if (!p.pattern(c.pattern)) e.push_back("clip " + c.id + " plays pattern `" + c.pattern + "`, which does not exist");
            }
        }
        for (const auto &po : p.ports)
        {
            if (po.dir != "out" && po.dir != "in") e.push_back("port " + po.id + " has dir `" + po.dir + "` (in | out)");
            if (po.channels < 1 || po.channels > 64) e.push_back("port " + po.id + " has " + std::to_string(po.channels) + " channels (1..64)");
        }
        for (const auto &o : p.header.masterOut)
        {
            const Port *po = p.port(o);
            if (!po) e.push_back("the master feeds `" + o + "`, which is no port");
            else if (po->dir != "out") e.push_back("the master feeds " + o + ", an INPUT port");
        }
        // R-AUTO: one binding per address, on something that exists; automations well formed
        {
            std::set<std::string> bound;
            for (const auto &b : p.bindings)
            {
                if (b.address.empty()) { e.push_back("an #abind has no address"); continue; }
                if (!bound.insert(b.address).second) e.push_back("two bindings drive " + b.address + " (one per address, R-AUTO-1)");
                if (b.formula.empty() || b.formula[0] != '=')
                    e.push_back(b.address + "'s binding `" + b.formula + "` is not a formula — a formula starts with `=`");
                const auto dot = b.address.find('.');
                const std::string node = b.address.substr(0, dot);
                bool exists = node == "project" || p.strip(node) != nullptr;
                for (const auto &sd : p.sends) exists |= sd.id == node;
                for (const auto &r : p.racks)
                    for (const auto &d : r.devices) exists |= d.id == node;
                if (dot == std::string::npos || dot + 1 >= b.address.size() || !exists)
                    e.push_back("a binding drives `" + b.address + "`, which is no strip, send, device or project field");
            }
            for (const auto &a : p.automations)
            {
                if (!(a.min < a.max)) e.push_back(a.id + "'s range " + canonicalNumber(a.min) + "…" + canonicalNumber(a.max) + " is empty");
                if (!(a.loop >= 0 && a.at >= 0 && a.length >= 0 && std::isfinite(a.loop + a.at + a.length)))
                    e.push_back(a.id + "'s loop, at and length are beats ≥ 0");
                for (const auto &pt : a.points)
                {
                    if (pt.shape != "linear" && pt.shape != "hold" && pt.shape != "smooth" && pt.shape != "bezier")
                        e.push_back(a.id + " has a point of shape `" + pt.shape + "` (linear | hold | smooth | bezier)");
                    if (pt.shape == "bezier" && !(pt.inflIn > 0 && pt.inflIn <= 100 && pt.inflOut > 0 && pt.inflOut <= 100))
                        e.push_back(a.id + "'s bezier point at beat " + canonicalBeats(pt.at) + " has an influence outside 0 < x ≤ 100 %");
                    if (pt.shape == "bezier" && !(std::isfinite(pt.speedIn) && std::isfinite(pt.speedOut)))
                        e.push_back(a.id + "'s bezier point at beat " + canonicalBeats(pt.at) + " has a speed that is not a number");
                }
            }
        }
        return e;
    }

    bool exprLane(const std::string &name, std::string &canonical)
    {
        if (name == "bend" || name == "mod") { canonical = name == "mod" ? "cc1" : "bend"; return true; }
        if (name.size() < 3 || name.compare(0, 2, "cc") != 0) return false;
        for (size_t i = 2; i < name.size(); ++i)
            if (name[i] < '0' || name[i] > '9') return false;
        const int n = std::atoi(name.c_str() + 2);
        if (n < 0 || n > 127 || name.size() > 5) return false;
        canonical = "cc" + std::to_string(n);
        return true;
    }

    void exprRange(const std::string &lane, double &lo, double &hi)
    {
        lo = lane == "bend" ? -1.0 : 0.0;
        hi = 1.0;
    }

    double exprAt(const std::vector<ExprPoint> &expr, const std::string &lane, double at, double rest)
    {
        const ExprPoint *before = nullptr, *after = nullptr;
        for (const auto &x : expr)
        {
            if (x.lane != lane) continue;
            if (x.at <= at) { if (!before || x.at >= before->at) before = &x; }
            else if (!after || x.at < after->at) after = &x;
        }
        if (!before && !after) return rest;
        if (!before) return after->value;  // held before the first
        if (!after) return before->value;  // … and after the last
        const double t = (at - before->at) / std::max(1e-12, after->at - before->at);
        return before->value + (after->value - before->value) * t;
    }

    const std::vector<ScaleKind> &scaleKinds()
    {
        static const std::vector<ScaleKind> k = {
            {{"major", "ionian"}, {0, 2, 4, 5, 7, 9, 11}, "major"},
            {{"minor", "aeolian", "natural-minor"}, {0, 2, 3, 5, 7, 8, 10}, "natural minor"},
            {{"harmonic-minor"}, {0, 2, 3, 5, 7, 8, 11}, "harmonic minor"},
            {{"melodic-minor"}, {0, 2, 3, 5, 7, 9, 11}, "melodic minor (rising)"},
            {{"dorian"}, {0, 2, 3, 5, 7, 9, 10}, "dorian"},
            {{"phrygian"}, {0, 1, 3, 5, 7, 8, 10}, "phrygian"},
            {{"lydian"}, {0, 2, 4, 6, 7, 9, 11}, "lydian"},
            {{"mixolydian"}, {0, 2, 4, 5, 7, 9, 10}, "mixolydian"},
            {{"locrian"}, {0, 1, 3, 5, 6, 8, 10}, "locrian"},
            {{"major-pentatonic", "pentatonic"}, {0, 2, 4, 7, 9}, "major pentatonic"},
            {{"minor-pentatonic"}, {0, 3, 5, 7, 10}, "minor pentatonic"},
            {{"blues"}, {0, 3, 5, 6, 7, 10}, "blues (minor pentatonic and the flat fifth)"},
        };
        return k;
    }

    bool parseScale(const std::string &text, std::string &canonical, int &mask, std::string &err)
    {
        static const char *kKeys[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
        std::istringstream in(text);
        std::string key, name, extra;
        in >> key >> name >> extra;
        auto vocabulary = [] {
            std::string v;
            for (const auto &k : scaleKinds()) v += (v.empty() ? "" : ", ") + k.names[0];
            return v;
        };
        if (key.empty() || name.empty() || !extra.empty())
        { err = "a scale is a key and a name — `D dorian`, `Bb minor` (" + vocabulary() + ")"; return false; }
        static const int kLetter[] = {9, 11, 0, 2, 4, 5, 7}; // A B C D E F G
        const char l = (char)std::toupper((unsigned char)key[0]);
        int pc = l >= 'A' && l <= 'G' ? kLetter[l - 'A'] : -1;
        if (pc >= 0 && key.size() == 2) pc = key[1] == '#' ? pc + 1 : key[1] == 'b' ? pc + 11 : -1;
        if (pc < 0 || key.size() > 2) { err = "`" + key + "` is not a key (C, F#, Bb …)"; return false; }
        pc %= 12;
        std::string lower;
        for (char ch : name) lower += (char)std::tolower((unsigned char)ch);
        for (const auto &k : scaleKinds())
            if (std::find(k.names.begin(), k.names.end(), lower) != k.names.end())
            {
                mask = 0;
                for (int s : k.steps) mask |= 1 << ((pc + s) % 12);
                canonical = std::string(kKeys[pc]) + " " + k.names[0];
                return true;
            }
        err = "`" + name + "` is not a scale (" + vocabulary() + ")";
        return false;
    }

    int snapToScale(int pitch, int mask, int dir)
    {
        pitch = std::clamp(pitch, 0, 127);
        if (!(mask & 0xFFF)) return pitch;
        auto in = [mask](int p) { return p >= 0 && p <= 127 && (mask >> (p % 12) & 1); };
        if (dir == 0)
        {
            for (int d = 0; d < 12; ++d)
            {
                if (in(pitch - d)) return pitch - d; // a tie goes down
                if (in(pitch + d)) return pitch + d;
            }
            return pitch;
        }
        for (int p = pitch; p >= 0 && p <= 127; p += dir)
            if (in(p)) return p;
        for (int p = pitch; p >= 0 && p <= 127; p -= dir) // the end of the keyboard: the other way
            if (in(p)) return p;
        return pitch;
    }

    namespace
    {
        // a group of places with an `order` each: renumbered, stable, only when two share one
        template <class T, class Key> bool tidyGroup(std::vector<T> &v, Key key)
        {
            bool moved = false;
            std::map<std::string, std::vector<T *>> groups;
            for (auto &x : v) groups[key(x)].push_back(&x);
            for (auto &g : groups)
            {
                std::set<int> seen;
                bool twice = false;
                for (const T *x : g.second) twice = !seen.insert(x->order).second || twice;
                if (!twice) continue;
                std::stable_sort(g.second.begin(), g.second.end(), [](const T *a, const T *b) { return a->order < b->order; });
                for (size_t i = 0; i < g.second.size(); ++i)
                    if (g.second[i]->order != (int)i) { g.second[i]->order = (int)i; moved = true; }
            }
            return moved;
        }
    }

    bool tidyOrders(Project &p)
    {
        auto all = [](const auto &) { return std::string(); };
        bool moved = tidyGroup(p.ports, all);
        moved = tidyGroup(p.mixers, all) || moved;
        moved = tidyGroup(p.strips, [](const Strip &s) { return s.mixer; }) || moved;
        moved = tidyGroup(p.lanes, all) || moved;
        moved = tidyGroup(p.scenes, all) || moved;
        return moved;
    }

    bool gatherLaneGroups(Project &p)
    {
        std::vector<Lane *> order;
        for (auto &l : p.lanes) order.push_back(&l);
        std::stable_sort(order.begin(), order.end(), [](const Lane *a, const Lane *b) { return a->order < b->order; });
        std::vector<Lane *> out;
        std::set<std::string> done;
        for (Lane *l : order)
        {
            if (l->group.empty()) { out.push_back(l); continue; }
            if (!done.insert(l->group).second) continue; // gathered with its group's first
            for (Lane *m : order)
                if (m->group == l->group) out.push_back(m);
        }
        bool changed = false;
        for (size_t i = 0; i < out.size(); ++i)
            if (out[i]->order != (int)i) { out[i]->order = (int)i; changed = true; }
        const size_t before = p.groups.size();
        p.groups.erase(std::remove_if(p.groups.begin(), p.groups.end(), [&](const LaneGroup &g) { return !done.count(g.id); }), p.groups.end());
        return changed || p.groups.size() != before;
    }

    int scaleMaskOf(const Pattern &pt)
    {
        std::string canon, err;
        int mask = 0;
        return !pt.scale.empty() && parseScale(pt.scale, canon, mask, err) ? mask : 0;
    }

    bool noteSounds(const Note &n, const std::string &clip, long long repetition)
    {
        if (n.chance >= 1.0) return true;
        if (n.chance <= 0.0) return false;
        // FNV-1a over the clip, then splitmix64's finaliser over the rest: 53 bits → [0, 1)
        unsigned long long h = 1469598103934665603ULL;
        for (unsigned char ch : clip) { h ^= ch; h *= 1099511628211ULL; }
        auto mix = [&h](unsigned long long v) {
            h ^= v + 0x9E3779B97F4A7C15ULL + (h << 6) + (h >> 2);
            unsigned long long z = h;
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
            h = z ^ (z >> 31);
        };
        mix((unsigned long long)repetition);
        mix((unsigned long long)n.pitch);
        mix((unsigned long long)std::llround(n.at * kPpq));
        return (double)(h >> 11) / 9007199254740992.0 < n.chance;
    }
}
}
