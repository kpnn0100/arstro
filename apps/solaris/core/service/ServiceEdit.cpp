// SolarisService — the editing commands: mixers, strips, routes, sends, racks, lanes, clips,
// patterns, notes (R-MIX, R-LANE, R-CLIP, R-FX-5). Every one runs inside `dispatch`'s
// all-or-nothing: the project is copied first and validated after, so a command that would leave
// a backward route (R-MIX-4) or a dangling reference is refused with the validator's sentence and
// changes nothing.
#include "Compile.h"
#include "Format.h"
#include "SolarisService.h"
#include "device/Device.h"
#include <algorithm>
#include <cmath>
#include <filesystem>

namespace fs = std::filesystem;

namespace arstro
{
namespace solaris
{
    using K = Command::Kind;

    namespace
    {
        bool beatsFlag(const Command &c, const std::string &name, double fallback, double &out, std::string &err)
        {
            out = fallback;
            if (!c.has(name)) return true;
            if (!parseNumber(c.flag(name), out) || !std::isfinite(out) || out < 0)
            {
                err = "--" + name + " must be a number of beats ≥ 0, got `" + c.flag(name) + "`";
                return false;
            }
            out = toTick(out);
            return true;
        }
        bool intFlag(const Command &c, const std::string &name, int lo, int hi, int fallback, int &out, std::string &err)
        {
            out = fallback;
            if (!c.has(name)) return true;
            double v = 0;
            if (!parseNumber(c.flag(name), v) || v != std::floor(v) || v < lo || v > hi)
            {
                err = "--" + name + " must be a whole number from " + std::to_string(lo) + " to " + std::to_string(hi);
                return false;
            }
            out = (int)v;
            return true;
        }
        int nextOrder(const std::vector<int> &orders)
        {
            int m = -1;
            for (int o : orders) m = std::max(m, o);
            return m + 1;
        }
        std::string stemOf(const std::string &file) { return fs::path(file).stem().string(); }
    }

    std::string SolarisService::firstMixer() const
    {
        const auto ms = mProject.mixersInOrder();
        return ms.empty() ? std::string() : ms.front()->id;
    }

    std::string SolarisService::secondMixer() const
    {
        const auto ms = mProject.mixersInOrder();
        return ms.size() > 1 ? ms[1]->id : firstMixer();
    }

    std::string SolarisService::defaultOutFor(const std::string &mixerId) const
    {
        // R-MIX-3: a new source strip feeds "Main" — the first bus on a later mixer; else master.
        const Mixer *m = mProject.mixer(mixerId);
        const int order = m ? m->order : 0;
        for (const Strip *s : mProject.stripsInOrder())
        {
            const Mixer *sm = mProject.mixerOf(*s);
            if (s->kind == "bus" && sm && sm->order > order) return s->id;
        }
        return std::string();
    }

    bool SolarisService::mixCommand(const Command &c, std::string &err)
    {
        Project &p = mProject;
        switch (c.kind)
        {
        case K::MixerAdd:
        {
            std::vector<int> orders;
            for (const auto &m : p.mixers) orders.push_back(m.order);
            Mixer m;
            m.id = p.nextId("mx");
            m.name = c.arg(0, "Mixer " + std::to_string(p.mixers.size() + 1));
            m.order = nextOrder(orders);
            p.mixers.push_back(m);
            changed("mixer.added", m.id);
            mOutput = m.id + "\n";
            return true;
        }
        case K::MixerDelete:
        {
            const std::string id = c.arg(0);
            if (!p.mixer(id)) { err = "no mixer `" + id + "`"; return false; }
            for (const auto &s : p.strips)
                if (p.mixerOf(s) && p.mixerOf(s)->id == id) { err = "mixer " + id + " still holds " + s.id + " (" + s.name + ") — move or delete its strips first"; return false; }
            p.mixers.erase(std::remove_if(p.mixers.begin(), p.mixers.end(), [&](const Mixer &m) { return m.id == id; }), p.mixers.end());
            changed("mixer.deleted", id);
            return true;
        }
        case K::MixerMove:
        {
            const std::string id = c.arg(0);
            if (!p.mixer(id)) { err = "no mixer `" + id + "`"; return false; }
            int to = 0;
            if (!c.has("to")) { err = "mixer move needs --to <index>"; return false; }
            if (!intFlag(c, "to", 0, (int)p.mixers.size() - 1, 0, to, err)) return false;
            // Strips that relied on being on the FIRST mixer by default keep their mixer explicitly.
            const std::string first = firstMixer();
            for (auto &s : p.strips)
                if (s.mixer.empty()) s.mixer = first;
            auto order = p.mixersInOrder();
            std::vector<std::string> ids;
            for (const Mixer *m : order) ids.push_back(m->id);
            ids.erase(std::find(ids.begin(), ids.end(), id));
            ids.insert(ids.begin() + to, id);
            for (size_t i = 0; i < ids.size(); ++i) p.mixer(ids[i])->order = (int)i;
            changed("mixer.moved", id);
            return true;
        }
        case K::StripAdd:
        {
            const std::string kind = c.flag("kind", "audio");
            if (kind != "audio" && kind != "instrument" && kind != "bus") { err = "--kind must be audio, instrument or bus"; return false; }
            Strip s;
            s.id = p.nextId("ch");
            s.kind = kind;
            s.mixer = c.flag("mixer", kind == "bus" ? secondMixer() : firstMixer());
            if (!s.mixer.empty() && !p.mixer(s.mixer)) { err = "no mixer `" + s.mixer + "`"; return false; }
            std::vector<int> orders;
            for (const auto &o : p.strips)
                if (p.mixerOf(o) && p.mixerOf(o)->id == s.mixer) orders.push_back(o.order);
            s.order = nextOrder(orders);
            s.out = c.has("out") ? (c.flag("out") == "master" ? std::string() : c.flag("out")) : defaultOutFor(s.mixer);
            std::string instrument;
            if (kind == "instrument")
            {
                instrument = c.flag("instrument", "synth");
                const DeviceType *t = DeviceRegistry::find(instrument);
                if (!t || t->kind != DeviceKind::Instrument)
                {
                    err = "`" + instrument + "` is not an instrument (instruments:";
                    for (const auto &x : DeviceRegistry::types())
                        if (x.kind == DeviceKind::Instrument) err += " " + x.name;
                    err += ")";
                    return false;
                }
                s.name = c.flag("name", t->label);
            }
            else
            {
                if (c.has("instrument")) { err = "--instrument is for --kind instrument"; return false; }
                s.name = c.flag("name", kind == "bus" ? "Bus" : "Audio");
            }
            p.strips.push_back(s);
            if (!instrument.empty())
            {
                Rack r;
                r.track = s.id;
                r.devices.push_back(DeviceNode{p.nextId("dv"), instrument, false, {}, {}});
                p.racks.push_back(r);
            }
            changed("strip.added", s.id);
            mOutput = s.id + "\n";
            return true;
        }
        case K::StripDelete:
        {
            const std::string id = c.arg(0);
            if (!p.strip(id)) { err = "no strip `" + id + "`"; return false; }
            for (const auto &o : p.strips)
                if (o.out == id) { err = o.id + " (" + o.name + ") routes to " + id + " — route it elsewhere first"; return false; }
            for (const auto &sd : p.sends)
                if (sd.to == id) { err = sd.id + " sends to " + id + " — delete it first"; return false; }
            const bool withClips = c.has("with-clips");
            int clips = 0;
            for (const auto &cl : p.clips) clips += cl.track == id ? 1 : 0;
            if (clips && !withClips) { err = std::to_string(clips) + " clip(s) play through " + id + " — move them, or add --with-clips"; return false; }
            p.clips.erase(std::remove_if(p.clips.begin(), p.clips.end(), [&](const Clip &x) { return x.track == id; }), p.clips.end());
            p.sends.erase(std::remove_if(p.sends.begin(), p.sends.end(), [&](const Send &x) { return x.from == id; }), p.sends.end());
            p.racks.erase(std::remove_if(p.racks.begin(), p.racks.end(), [&](const Rack &x) { return x.track == id; }), p.racks.end());
            p.strips.erase(std::remove_if(p.strips.begin(), p.strips.end(), [&](const Strip &x) { return x.id == id; }), p.strips.end());
            changed("strip.deleted", id);
            return true;
        }
        case K::StripMove:
        {
            Strip *s = p.strip(c.arg(0));
            if (!s) { err = "no strip `" + c.arg(0) + "`"; return false; }
            if (!c.has("mixer") && !c.has("order")) { err = "strip move needs --mixer and/or --order"; return false; }
            if (c.has("mixer"))
            {
                if (!p.mixer(c.flag("mixer"))) { err = "no mixer `" + c.flag("mixer") + "`"; return false; }
                s->mixer = c.flag("mixer");
            }
            int order = s->order;
            if (!intFlag(c, "order", -1000000, 1000000, order, order, err)) return false;
            s->order = order;
            changed("strip.moved", s->id);
            return true;
        }
        case K::Route:
        {
            Strip *s = p.strip(c.arg(0));
            if (!s) { err = "no strip `" + c.arg(0) + "`"; return false; }
            if (!c.has("to")) { err = "route needs --to <strip|master|port>"; return false; }
            const std::string to = c.flag("to");
            s->out = to == "master" ? std::string() : to;
            changed("route.set", s->id);
            return true;
        }
        case K::SendAdd:
        {
            if (!p.strip(c.arg(0))) { err = "no strip `" + c.arg(0) + "`"; return false; }
            if (!c.has("to")) { err = "send add needs --to <strip|master|port>"; return false; }
            Send sd;
            sd.id = p.nextId("sd");
            sd.from = c.arg(0);
            sd.to = c.flag("to");
            sd.pre = c.has("pre");
            if (c.has("gain") && (!parseNumber(c.flag("gain"), sd.gain) || !(sd.gain >= -120 && sd.gain <= 12)))
            { err = "--gain must be dB between -120 and 12"; return false; }
            p.sends.push_back(sd);
            changed("send.added", sd.id);
            mOutput = sd.id + "\n";
            return true;
        }
        case K::SendDelete:
        {
            const std::string id = c.arg(0);
            if (!p.send(id)) { err = "no send `" + id + "`"; return false; }
            p.sends.erase(std::remove_if(p.sends.begin(), p.sends.end(), [&](const Send &x) { return x.id == id; }), p.sends.end());
            changed("send.deleted", id);
            return true;
        }
        case K::DeviceAdd:
        {
            const std::string track = c.arg(0);
            const Strip *s = track == "master" ? nullptr : p.strip(track);
            if (track != "master" && !s) { err = "no strip `" + track + "` (or `master`)"; return false; }
            const std::string type = c.flag("type");
            const DeviceType *t = DeviceRegistry::find(type);
            if (!t)
            {
                std::vector<std::string> names;
                for (const auto &x : DeviceRegistry::types()) names.push_back(x.name);
                err = "no device type `" + type + "`";
                const auto near = nearest(type, names);
                err += near.empty() ? std::string(" (types: synth, drums, compressor, eq, reverb, delay, chorus, drive, filter)") : " (did you mean: " + near[0] + "?)";
                return false;
            }
            const bool instrumentStrip = s && s->kind == "instrument";
            if (t->kind == DeviceKind::Instrument)
            { err = "a strip plays ONE instrument, its first device — make an instrument strip (strip add --kind instrument --instrument " + type + ")"; return false; }
            Rack *r = p.rack(track);
            if (!r) { p.racks.push_back(Rack{track, {}, {}, {}}); r = &p.racks.back(); }
            int at = (int)r->devices.size();
            if (!intFlag(c, "at", 0, (int)r->devices.size(), at, at, err)) return false;
            if (instrumentStrip && at == 0) at = 1; // the instrument stays first
            const std::string id = p.nextId("dv");
            r->devices.insert(r->devices.begin() + at, DeviceNode{id, type, false, {}, {}});
            changed("device.added", id);
            mOutput = id + "\n";
            return true;
        }
        case K::DeviceRemove:
        case K::DeviceMove:
        {
            Rack *r = nullptr;
            DeviceNode *d = p.device(c.arg(0), &r);
            if (!d) { err = "no device `" + c.arg(0) + "`"; return false; }
            const Strip *s = p.strip(r->track);
            const bool isInstrument = s && s->kind == "instrument" && &r->devices.front() == d;
            if (isInstrument) { err = c.arg(0) + " is " + s->id + "'s instrument — an instrument strip keeps its instrument"; return false; }
            const std::string id = d->id;
            const size_t from = (size_t)(d - &r->devices.front());
            if (c.kind == K::DeviceRemove)
            {
                r->devices.erase(r->devices.begin() + from);
                if (r->devices.empty())
                {
                    const std::string track = r->track;
                    p.racks.erase(std::remove_if(p.racks.begin(), p.racks.end(), [&](const Rack &x) { return x.track == track; }), p.racks.end());
                }
                changed("device.removed", id);
                return true;
            }
            int to = 0;
            if (!c.has("to")) { err = "device move needs --to <index>"; return false; }
            if (!intFlag(c, "to", 0, (int)r->devices.size() - 1, 0, to, err)) return false;
            if (s && s->kind == "instrument" && to == 0) { err = "index 0 is " + s->id + "'s instrument"; return false; }
            DeviceNode moved = *d;
            r->devices.erase(r->devices.begin() + from);
            r->devices.insert(r->devices.begin() + to, moved);
            changed("device.moved", id);
            return true;
        }
        case K::LaneAdd:
        {
            std::vector<int> orders;
            for (const auto &l : p.lanes) orders.push_back(l.order);
            Lane l;
            l.id = p.nextId("ln");
            l.name = c.arg(0, "Lane " + std::to_string(p.lanes.size() + 1));
            l.order = nextOrder(orders);
            p.lanes.push_back(l);
            changed("lane.added", l.id);
            mOutput = l.id + "\n";
            return true;
        }
        case K::LaneDelete:
        {
            const std::string id = c.arg(0);
            if (!p.lane(id)) { err = "no lane `" + id + "`"; return false; }
            int clips = 0;
            for (const auto &cl : p.clips) clips += cl.lane == id ? 1 : 0;
            if (clips && !c.has("with-clips")) { err = std::to_string(clips) + " clip(s) are drawn on " + id + " — move them, or add --with-clips"; return false; }
            p.clips.erase(std::remove_if(p.clips.begin(), p.clips.end(), [&](const Clip &x) { return x.lane == id; }), p.clips.end());
            p.lanes.erase(std::remove_if(p.lanes.begin(), p.lanes.end(), [&](const Lane &x) { return x.id == id; }), p.lanes.end());
            changed("lane.deleted", id);
            return true;
        }
        default:
            return false;
        }
    }

    bool SolarisService::clipCommand(const Command &c, std::string &err)
    {
        Project &p = mProject;
        auto newLane = [&](const std::string &name) {
            std::vector<int> orders;
            for (const auto &l : p.lanes) orders.push_back(l.order);
            Lane l;
            l.id = p.nextId("ln");
            l.name = name;
            l.order = nextOrder(orders);
            p.lanes.push_back(l);
            return l.id;
        };
        switch (c.kind)
        {
        case K::ClipAdd:
        {
            Clip cl;
            if (!beatsFlag(c, "at", 0.0, cl.at, err)) return false;
            if (c.has("lane") && !p.lane(c.flag("lane"))) { err = "no lane `" + c.flag("lane") + "`"; return false; }
            if (c.has("src"))
            {
                // R-MIX-2: a file the song has not used gets its own strip; a used one reuses it.
                cl.src = relativePath(c.flag("src"));
                auto pcm = pcmFor(cl.src);
                if (!pcm)
                {
                    err = mHost.decodeAudio ? "cannot read " + c.flag("src") + " as audio" : "this build has no audio decoder";
                    return false;
                }
                const std::string stem = stemOf(cl.src);
                if (c.has("strip"))
                {
                    const Strip *s = p.strip(c.flag("strip"));
                    if (!s) { err = "no strip `" + c.flag("strip") + "`"; return false; }
                    cl.track = s->id;
                }
                else
                {
                    for (const auto &o : p.clips)
                        if (o.src == cl.src) { cl.track = o.track; break; }
                    if (cl.track.empty())
                    {
                        Strip s;
                        s.id = p.nextId("ch");
                        s.name = stem;
                        s.kind = "audio";
                        s.mixer = firstMixer();
                        std::vector<int> orders;
                        for (const auto &o : p.strips)
                            if (p.mixerOf(o) && p.mixerOf(o)->id == s.mixer) orders.push_back(o.order);
                        s.order = nextOrder(orders);
                        s.out = defaultOutFor(s.mixer);
                        p.strips.push_back(s);
                        cl.track = s.id;
                        changed("strip.added", s.id);
                    }
                }
                const double dur = (double)pcm->frames / p.header.sampleRate;
                cl.in = 0.0;
                cl.out = dur;
                double v = 0;
                if (c.has("in")) { if (!parseNumber(c.flag("in"), v) || v < 0) { err = "--in must be seconds ≥ 0"; return false; } cl.in = v; }
                if (c.has("out")) { if (!parseNumber(c.flag("out"), v) || v < 0) { err = "--out must be seconds ≥ 0"; return false; } cl.out = v; }
                if (c.has("length")) { if (!beatsFlag(c, "length", 0, cl.length, err)) return false; cl.loop = true; }
                cl.name = stem;
                cl.lane = c.has("lane") ? c.flag("lane") : newLane(stem);
            }
            else if (c.has("strip"))
            {
                const Strip *s = p.strip(c.flag("strip"));
                if (!s) { err = "no strip `" + c.flag("strip") + "`"; return false; }
                if (s->kind != "instrument") { err = s->id + " is a " + s->kind + " strip — notes need an instrument strip (or give --src for audio)"; return false; }
                cl.track = s->id;
                if (c.has("pattern"))
                {
                    if (!p.pattern(c.flag("pattern"))) { err = "no pattern `" + c.flag("pattern") + "`"; return false; }
                    cl.pattern = c.flag("pattern");
                }
                else
                {
                    Pattern pt;
                    pt.id = p.nextId("pt");
                    pt.name = s->name;
                    pt.length = 4.0;
                    p.patterns.push_back(pt);
                    cl.pattern = pt.id;
                }
                if (!beatsFlag(c, "length", 0, cl.length, err)) return false;
                cl.name = p.pattern(cl.pattern)->name;
                cl.lane = c.has("lane") ? c.flag("lane") : newLane(s->name);
            }
            else
            {
                err = "clip add needs --src <file> (audio) or --strip <instrument strip> (notes)";
                return false;
            }
            cl.id = p.nextId("ac");
            p.clips.push_back(cl);
            changed("clip.added", cl.id);
            mOutput = cl.id + "\n";
            return true;
        }
        case K::ClipMove:
        {
            Clip *cl = p.clip(c.arg(0));
            if (!cl) { err = "no clip `" + c.arg(0) + "`"; return false; }
            if (!c.has("at") && !c.has("lane") && !c.has("strip")) { err = "clip move needs --at, --lane and/or --strip"; return false; }
            if (!beatsFlag(c, "at", cl->at, cl->at, err)) return false;
            if (c.has("lane")) cl->lane = c.flag("lane") == "none" ? std::string() : c.flag("lane");
            if (c.has("strip")) cl->track = c.flag("strip");
            changed("clip.moved", cl->id);
            return true;
        }
        case K::ClipDuplicate:
        {
            const Clip *src = p.clip(c.arg(0));
            if (!src) { err = "no clip `" + c.arg(0) + "`"; return false; }
            Clip copy = *src;
            copy.id = p.nextId("ac");
            copy.remarks = Remarks();
            if (!beatsFlag(c, "at", toTick(src->at + clipLengthBeats(p, *src)), copy.at, err)) return false;
            p.clips.push_back(copy); // a note clip's copy plays the SAME pattern: linked (R-CLIP-3)
            changed("clip.added", copy.id);
            mOutput = copy.id + "\n";
            return true;
        }
        case K::ClipUnique:
        {
            Clip *cl = p.clip(c.arg(0));
            if (!cl) { err = "no clip `" + c.arg(0) + "`"; return false; }
            if (cl->isAudio()) { err = cl->id + " is an audio clip — only a note clip shares a pattern"; return false; }
            Pattern copy = *p.pattern(cl->pattern);
            copy.id = p.nextId("pt");
            copy.name += " copy";
            copy.remarks = Remarks();
            p.patterns.push_back(copy);
            cl->pattern = copy.id;
            changed("clip.unique", cl->id);
            mOutput = copy.id + "\n";
            return true;
        }
        case K::ClipDelete:
        {
            const std::string id = c.arg(0);
            if (!p.clip(id)) { err = "no clip `" + id + "`"; return false; }
            p.clips.erase(std::remove_if(p.clips.begin(), p.clips.end(), [&](const Clip &x) { return x.id == id; }), p.clips.end());
            changed("clip.deleted", id);
            return true;
        }
        case K::PatternNew:
        {
            Pattern pt;
            pt.id = p.nextId("pt");
            pt.name = c.flag("name", "Pattern " + std::to_string(p.patterns.size() + 1));
            if (!beatsFlag(c, "length", 4.0, pt.length, err)) return false;
            if (!(pt.length > 0)) { err = "--length must be more than 0 beats"; return false; }
            p.patterns.push_back(pt);
            changed("pattern.added", pt.id);
            mOutput = pt.id + "\n";
            return true;
        }
        case K::NoteAdd:
        case K::NoteDelete:
        {
            Pattern *pt = p.pattern(c.arg(0));
            if (!pt) { err = "no pattern `" + c.arg(0) + "`"; return false; }
            if (!c.has("pitch") || !c.has("at")) { err = "a note needs --pitch and --at"; return false; }
            int pitch = 0;
            if (!intFlag(c, "pitch", 0, 127, 60, pitch, err)) return false;
            double at = 0;
            if (!beatsFlag(c, "at", 0, at, err)) return false;
            auto same = [&](const Note &n) { return n.pitch == pitch && std::fabs(n.at - at) < 0.5 / kPpq; };
            if (c.kind == K::NoteDelete)
            {
                const auto before = pt->notes.size();
                pt->notes.erase(std::remove_if(pt->notes.begin(), pt->notes.end(), same), pt->notes.end());
                if (pt->notes.size() == before) { err = "no note " + std::to_string(pitch) + " at beat " + canonicalBeats(at) + " in " + pt->id; return false; }
                changed("note.deleted", pt->id);
                return true;
            }
            Note n;
            n.pitch = pitch;
            n.at = at;
            if (!beatsFlag(c, "length", 0.25, n.length, err)) return false;
            if (!(n.length > 0)) { err = "--length must be more than 0 beats"; return false; }
            if (!intFlag(c, "vel", 1, 127, 100, n.vel, err)) return false;
            pt->notes.erase(std::remove_if(pt->notes.begin(), pt->notes.end(), same), pt->notes.end()); // replace, not stack
            pt->notes.push_back(n);
            std::stable_sort(pt->notes.begin(), pt->notes.end(), [](const Note &a, const Note &b) {
                return a.at != b.at ? a.at < b.at : a.pitch < b.pitch;
            });
            changed("note.added", pt->id);
            return true;
        }
        default:
            return false;
        }
    }
}
}
