#include "Project.h"
#include "Format.h"
#include <algorithm>
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
    Pattern *Project::pattern(const std::string &id) { return findId(patterns, id); }
    Clip *Project::clip(const std::string &id) { return findId(clips, id); }
    const Strip *Project::strip(const std::string &id) const { return findId(strips, id); }
    const Mixer *Project::mixer(const std::string &id) const { return findId(mixers, id); }
    const Port *Project::port(const std::string &id) const { return findId(ports, id); }
    const Lane *Project::lane(const std::string &id) const { return findId(lanes, id); }
    const Pattern *Project::pattern(const std::string &id) const { return findId(patterns, id); }
    const Clip *Project::clip(const std::string &id) const { return findId(clips, id); }

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
        for (const auto &x : patterns) ids.push_back(x.id);
        for (const auto &x : clips) ids.push_back(x.id);
        return ids;
    }

    std::string Project::nextId(const std::string &prefix) const
    {
        long best = 0;
        const std::string p = prefix + "_";
        for (const auto &id : allIds())
        {
            if (id.compare(0, p.size(), p) != 0) continue;
            const std::string tail = id.substr(p.size());
            if (tail.empty() || tail.find_first_not_of("0123456789") != std::string::npos) continue;
            best = std::max(best, std::strtol(tail.c_str(), nullptr, 10));
        }
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
            else n.unknown.emplace_back(k, v);
        }
        void apply(Reader &r, Send &n, const std::string &k, const std::string &v)
        {
            if (k == "id") n.id = v;
            else if (k == "from") n.from = v;
            else if (k == "to") n.to = v;
            else if (k == "gain") n.gain = r.num(k, v, 0.0);
            else if (k == "pre") n.pre = r.boolean(k, v, false);
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
            else n.params.emplace_back(k, v); // the registry's, checked by the core
        }
        void apply(Reader &r, Lane &n, const std::string &k, const std::string &v)
        {
            if (k == "id") n.id = v;
            else if (k == "name") n.name = v;
            else if (k == "order") n.order = r.integer(k, v, 0);
            else if (k == "colour") n.colour = r.integer(k, v, -1);
            else n.unknown.emplace_back(k, v);
        }
        void apply(Reader &r, Pattern &n, const std::string &k, const std::string &v)
        {
            if (k == "id") n.id = v;
            else if (k == "name") n.name = v;
            else if (k == "length") n.length = toTick(r.num(k, v, 4.0));
            else n.unknown.emplace_back(k, v);
        }
        void apply(Reader &r, Note &n, const std::string &k, const std::string &v)
        {
            if (k == "pitch") n.pitch = std::clamp(r.integer(k, v, 60), 0, 127);
            else if (k == "vel") n.vel = std::clamp(r.integer(k, v, 100), 1, 127);
            else if (k == "at") n.at = toTick(r.num(k, v, 0.0));
            else if (k == "length") n.length = toTick(r.num(k, v, 0.25));
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
            else if (k == "gain") n.gain = r.num(k, v, 0.0);
            else if (k == "fadeIn") n.fadeIn = toTick(r.num(k, v, 0.0));
            else if (k == "fadeOut") n.fadeOut = toTick(r.num(k, v, 0.0));
            else if (k == "loop") n.loop = r.boolean(k, v, false);
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
        enum class Cur { None, Port, Mixer, Strip, Send, Rack, Device, Lane, Pattern, Note, Clip, Raw };
        Cur cur = Cur::None;
        bool seenNode = false, sawMagic = false;
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
            case Cur::Pattern:
            case Cur::Note: return &p.patterns.back().remarks;
            case Cur::Clip: return &p.clips.back().remarks;
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
                else if (tok.type == "apattern") { p.patterns.emplace_back(); fill(p.patterns.back()); cur = Cur::Pattern; }
                else if (tok.type == "note")
                {
                    Note n;
                    for (const auto &f : tok.fields) apply(r, n, f.first, f.second);
                    if (cur == Cur::Pattern || cur == Cur::Note) { p.patterns.back().notes.push_back(n); cur = Cur::Note; }
                    else if (cur == Cur::Clip) inlineNotes[p.clips.back().id].push_back(n);
                    else
                    {
                        err = "line " + std::to_string(lineNo) + ": #note outside an #apattern or a note #aclip";
                        return false;
                    }
                }
                else if (tok.type == "aclip") { p.clips.emplace_back(); fill(p.clips.back()); cur = Cur::Clip; }
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
                else p.header.unknown.emplace_back(key, value);
                continue;
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
            emit(out, "", l.unknown(n.unknown), n.remarks);
        }
        group(!p.sends.empty());
        for (const auto &n : p.sends)
            emit(out, "", Line("asend").kv("id", n.id).kv("from", n.from).kv("to", n.to).kv("gain", canonicalNumber(n.gain))
                              .kv("pre", boolText(n.pre)).unknown(n.unknown), n.remarks);
        group(!p.racks.empty());
        for (const auto &r : p.racks)
        {
            emit(out, "", Line("arack").kv("track", r.track).unknown(r.unknown), r.remarks);
            for (const auto &d : r.devices)
            {
                Line l("aeffect");
                l.kv("id", d.id).kv("type", d.type);
                if (d.bypass) l.kv("bypass", "true");
                for (const auto &kv : d.params) l.str(kv.first, kv.second);
                emit(out, "  ", l, d.remarks);
            }
        }
        group(!p.lanes.empty());
        for (const auto &n : p.lanes)
        {
            Line l("alane");
            l.kv("id", n.id).str("name", n.name).kv("order", std::to_string(n.order));
            if (n.colour >= 0) l.kv("colour", std::to_string(n.colour));
            emit(out, "", l.unknown(n.unknown), n.remarks);
        }
        group(!p.patterns.empty());
        for (const auto &n : p.patterns)
        {
            emit(out, "", Line("apattern").kv("id", n.id).strIf("name", n.name).kv("length", canonicalBeats(n.length)).unknown(n.unknown), n.remarks);
            for (const auto &x : n.notes)
                out += "  " + Line("note").kv("pitch", std::to_string(x.pitch)).kv("at", canonicalBeats(x.at))
                                  .kv("length", canonicalBeats(x.length)).kv("vel", std::to_string(x.vel)).unknown(x.unknown).s + "\n";
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
            }
            else
            {
                l.strIf("pattern", n.pattern).kv("at", canonicalBeats(n.at));
                if (n.length > 0) l.kv("length", canonicalBeats(n.length));
                if (n.gain != 0) l.kv("gain", canonicalNumber(n.gain));
            }
            emit(out, "", l.unknown(n.unknown), n.remarks);
        }
        group(!p.raw.empty());
        for (const auto &r : p.raw)
            for (const auto &l : r.lines) out += l + "\n";
        return out;
    }

    // ── validate ─────────────────────────────────────────────────────────────────────────────

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
        auto describe = [&p](const Strip &s) {
            const Mixer *m = p.mixerOf(s);
            return s.id + " (" + s.name + ", on " + (m ? m->name : std::string("no mixer")) + ")";
        };
        // R-MIX-4: a target is "master", an output port, or a strip on a LATER mixer.
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
            const Mixer *a = p.mixerOf(from), *b = p.mixerOf(*t);
            if ((b ? b->order : 0) <= (a ? a->order : 0))
                e.push_back(describe(from) + " " + what + " → " + describe(*t) +
                            ": a strip can only feed a strip on a LATER mixer, the master or a port (R-MIX-4)");
        };
        for (const auto &s : p.strips)
        {
            if (s.kind != "audio" && s.kind != "instrument" && s.kind != "bus")
                e.push_back(s.id + " has kind `" + s.kind + "` (audio | instrument | bus)");
            if (!s.mixer.empty() && !p.mixer(s.mixer)) e.push_back(s.id + " lives on mixer `" + s.mixer + "`, which does not exist");
            checkTarget(s, s.out, "output");
        }
        for (const auto &sd : p.sends)
        {
            const Strip *from = p.strip(sd.from);
            if (!from) { e.push_back(sd.id + " sends from `" + sd.from + "`, which is no strip"); continue; }
            if (sd.to.empty()) e.push_back(sd.id + " has no target");
            checkTarget(*from, sd.to, "send " + sd.id);
        }
        {
            std::set<std::string> tracks;
            for (const auto &r : p.racks)
            {
                if (r.track != "master" && !p.strip(r.track)) e.push_back("a rack belongs to `" + r.track + "`, which is no strip");
                if (!tracks.insert(r.track).second) e.push_back("two racks belong to `" + r.track + "`");
                for (const auto &d : r.devices)
                    if (d.type.empty()) e.push_back("device " + d.id + " has no type");
            }
        }
        for (const auto &pt : p.patterns)
            if (!(pt.length > 0)) e.push_back("pattern " + pt.id + " has no length");
        for (const auto &c : p.clips)
        {
            const Strip *s = p.strip(c.track);
            if (!s) { e.push_back("clip " + c.id + " plays through `" + c.track + "`, which is no strip"); continue; }
            if (!c.lane.empty() && !p.lane(c.lane)) e.push_back("clip " + c.id + " is on lane `" + c.lane + "`, which does not exist");
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
        return e;
    }
}
}
