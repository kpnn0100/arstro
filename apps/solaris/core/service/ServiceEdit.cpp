// SolarisService — the editing commands: mixers, strips, routes, sends, racks, lanes, clips,
// patterns, notes (R-MIX, R-LANE, R-CLIP, R-FX-5). Every one runs inside `dispatch`'s
// all-or-nothing: the project is copied first and validated after, so a command that would leave
// a backward route (R-MIX-4) or a dangling reference is refused with the validator's sentence and
// changes nothing.
#include "Compile.h"
#include "Format.h"
#include "Notation.h"
#include "SolarisService.h"
#include "device/Device.h"
#include <algorithm>
#include <cstdio>
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

        /** The strips on `mixerId` in processing order, `id` put at `at` (clamped), renumbered 0…n−1 —
         *  as `mixer move` renumbers mixers, so an order never ties (R-MIX-3/4). */
        void placeStrip(Project &p, const std::string &mixerId, const std::string &id, int at)
        {
            std::vector<std::string> ids;
            for (const Strip *s : p.stripsInOrder())
                if (s->id != id && p.mixerOf(*s) && p.mixerOf(*s)->id == mixerId) ids.push_back(s->id);
            at = std::max(0, std::min(at, (int)ids.size()));
            if (p.strip(id)) ids.insert(ids.begin() + at, id);
            for (size_t i = 0; i < ids.size(); ++i) p.strip(ids[i])->order = (int)i;
        }

        /** R-MIX-4 (amended): a new strip whose output is a strip on its OWN mixer goes just before it, so the
         *  route it was made with points forward (a bus made on Buses, feeding Main). */
        void placeBeforeItsTarget(Project &p, const std::string &id)
        {
            const Strip *s = p.strip(id);
            const Strip *t = s ? p.strip(s->out) : nullptr;
            if (!t || p.mixerOf(*t) != p.mixerOf(*s) || feedsForward(p, *s, *t)) return;
            const std::string mixerId = p.mixerOf(*s) ? p.mixerOf(*s)->id : std::string();
            int at = 0;
            for (const Strip *o : p.stripsInOrder())
            {
                if (o == t) break;
                if (o != s && p.mixerOf(*o) == p.mixerOf(*s)) ++at;
            }
            placeStrip(p, mixerId, id, at);
        }
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
        // R-MIX-3: a new strip feeds "Main" — the bus that LEAVES the nearest later mixer that has buses (a bus
        // feeding another on its own page is a return, not the way out); with none later, the one leaving its own
        // mixer (the new strip is placed before it, R-MIX-4 amended); else master.
        const Mixer *m = mProject.mixer(mixerId);
        const int order = m ? m->order : 0;
        auto exitOf = [this](const Mixer *mx) {
            for (const Strip *s : mProject.stripsInOrder())
            {
                if (s->kind != "bus" || mProject.mixerOf(*s) != mx) continue;
                const Strip *t = mProject.strip(s->out);
                if (t && mProject.mixerOf(*t) == mx) continue;
                return s->id;
            }
            return std::string();
        };
        for (const Mixer *mx : mProject.mixersInOrder())
            if (mx->order > order)
                if (const std::string id = exitOf(mx); !id.empty()) return id;
        return m ? exitOf(m) : std::string();
    }

    bool SolarisService::resolveSample(const std::string &given, std::string &stored, std::string &err, bool definition)
    {
        // as a clip's src: a relative name is the song folder's file when there is one, else the caller's
        std::string g = given;
        if (!fs::path(g).is_absolute() && !mPath.empty() && fs::exists(resolvePath(g))) g = resolvePath(g);
        stored = relativePath(g);
        if (definition)
        {
            // R-INST-9: an instrument's definition (an .sfz) — a text this machine can read; what it names, said at build
            std::string text, where;
            if (textFor(stored, text, where) && !text.empty()) return true;
            err = "cannot read " + given + " as an instrument (an .sfz)";
            return false;
        }
        if (pcmFor(stored)) return true;
        err = mHost.decodeAudio ? "cannot read " + given + " as audio" : "this build has no audio decoder";
        return false;
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
            if (kind != "audio" && kind != "instrument" && kind != "bus" && kind != "vca") { err = "--kind must be audio, instrument, bus or vca"; return false; }
            if (kind == "vca" && c.has("out")) { err = "a VCA carries no sound — it has no output (R-MIX-19)"; return false; }
            Strip s;
            s.id = p.nextId("ch");
            s.kind = kind;
            s.mixer = c.flag("mixer", kind == "bus" ? secondMixer() : firstMixer());
            if (!s.mixer.empty() && !p.mixer(s.mixer)) { err = "no mixer `" + s.mixer + "`"; return false; }
            std::vector<int> orders;
            for (const auto &o : p.strips)
                if (p.mixerOf(o) && p.mixerOf(o)->id == s.mixer) orders.push_back(o.order);
            s.order = nextOrder(orders);
            s.out = kind == "vca" ? std::string() : c.has("out") ? (c.flag("out") == "master" ? std::string() : c.flag("out")) : defaultOutFor(s.mixer);
            std::string instrument;
            DeviceNode plugin; // R-VST-6: --plugin, another maker's instrument
            if (kind == "instrument" && c.has("plugin"))
            {
                if (c.has("instrument")) { err = "--instrument or --plugin, not both"; return false; }
                if (c.has("sample")) { err = "--sample is for a sampler, not a plugin"; return false; }
                if (!pluginNode(c.flag("plugin"), plugin, err)) return false;
                if (plugin.pluginKind != "instrument")
                { err = "`" + plugin.pluginName + "` is an effect — put it in a rack (device add <ch> --plugin " + plugin.plugin + ")"; return false; }
                instrument = "vst3";
                s.name = c.flag("name", plugin.pluginName);
            }
            else if (kind == "instrument")
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
                if (c.has("instrument") || c.has("plugin")) { err = std::string(c.has("plugin") ? "--plugin" : "--instrument") + " is for --kind instrument"; return false; }
                s.name = c.flag("name", kind == "bus" ? "Bus" : kind == "vca" ? "VCA" : "Audio");
            }
            p.strips.push_back(s);
            placeBeforeItsTarget(p, s.id);
            if (!instrument.empty())
            {
                Rack r;
                r.track = s.id;
                DeviceNode dn = plugin;
                dn.id = p.nextId("dv");
                dn.type = instrument;
                if (c.has("sample"))
                {
                    // R-EDM-8: a sampler made with its sound
                    if (!DeviceRegistry::find(instrument)->takesSample) { err = "--sample is for an instrument that plays one (sampler), not " + instrument; return false; }
                    if (!resolveSample(c.flag("sample"), dn.sample, err, DeviceRegistry::find(instrument)->takesDefinition)) return false;
                }
                r.devices.push_back(dn);
                p.racks.push_back(r);
            }
            else if (c.has("sample")) { err = "--sample is for --kind instrument --instrument sampler"; return false; }
            changed("strip.added", s.id);
            if (!instrument.empty()) changed("device.added", p.rack(s.id)->devices.front().id); // R-SVC-8: every made node is said
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
            int clips = 0, slots = 0;
            for (const auto &cl : p.clips) clips += cl.track == id ? 1 : 0;
            for (const auto &sl : p.slots) slots += sl.strip == id ? 1 : 0; // R-LAUNCH-1: its cells in the grid count as its clips
            if (clips && !withClips) { err = std::to_string(clips) + " clip(s) play through " + id + " — move them, or add --with-clips"; return false; }
            if (slots && !withClips) { err = std::to_string(slots) + " slot(s) of the session grid play through " + id + " — add --with-clips"; return false; }
            p.clips.erase(std::remove_if(p.clips.begin(), p.clips.end(), [&](const Clip &x) { return x.track == id; }), p.clips.end());
            p.slots.erase(std::remove_if(p.slots.begin(), p.slots.end(), [&](const Slot &x) { return x.strip == id; }), p.slots.end()); // R-LAUNCH-1: its cells
            {
                // its formulas go with it: the strip's, its sends', its devices' (R-AUTO-1)
                std::set<std::string> nodes = {id};
                for (const auto &x : p.sends)
                    if (x.from == id) nodes.insert(x.id);
                if (const Rack *rk = p.rack(id))
                    for (const auto &d : rk->devices) nodes.insert(d.id);
                dropBindingsOf(nodes);
            }
            p.sends.erase(std::remove_if(p.sends.begin(), p.sends.end(), [&](const Send &x) { return x.from == id; }), p.sends.end());
            p.racks.erase(std::remove_if(p.racks.begin(), p.racks.end(), [&](const Rack &x) { return x.track == id; }), p.racks.end());
            for (auto &o : p.strips)
                if (o.vca == id) { o.vca.clear(); changed("strip.changed", o.id); } // R-MIX-19: a VCA gone — its members their own again
            for (auto &l : p.lanes)
                if (l.track == id)
                {
                    // R-LANE-3: its tracks become plain lanes, keeping the name they showed
                    l.name = laneTitle(p, l);
                    l.track.clear();
                    changed("lane.changed", l.id);
                }
            p.strips.erase(std::remove_if(p.strips.begin(), p.strips.end(), [&](const Strip &x) { return x.id == id; }), p.strips.end());
            changed("strip.deleted", id);
            return true;
        }
        case K::StripMove:
        {
            Strip *s = p.strip(c.arg(0));
            if (!s) { err = "no strip `" + c.arg(0) + "`"; return false; }
            if (!c.has("mixer") && !c.has("order")) { err = "strip move needs --mixer and/or --order"; return false; }
            const std::string id = s->id, from = p.mixerOf(*s) ? p.mixerOf(*s)->id : std::string();
            if (c.has("mixer"))
            {
                if (!p.mixer(c.flag("mixer"))) { err = "no mixer `" + c.flag("mixer") + "`"; return false; }
                s->mixer = c.flag("mixer");
            }
            const std::string to = p.mixerOf(*s) ? p.mixerOf(*s)->id : std::string();
            // its position among the OTHER strips of the mixer it ends on: kept on its own mixer, the end of a new one
            int others = 0, at = 0;
            bool passed = false;
            for (const Strip *o : p.stripsInOrder())
            {
                if (o->id == id) { passed = true; continue; }
                if (!p.mixerOf(*o) || p.mixerOf(*o)->id != to) continue;
                if (to == from && !passed) ++at;
                ++others;
            }
            if (to != from) at = others;
            if (!intFlag(c, "order", 0, others, at, at, err)) return false;
            placeStrip(p, to, id, at);                      // renumbered, so `--order 0` is first — never a tie
            if (from != to) placeStrip(p, from, std::string(), 0);
            changed("strip.moved", id);
            return true;
        }
        case K::StripRelink:
        {
            // a source re-linked to another line: every clip of it, in one edit (R-MIX-14)
            const Strip *from = p.strip(c.arg(0));
            if (!from) { err = "no strip `" + c.arg(0) + "`"; return false; }
            if (!c.has("to")) { err = "strip relink needs --to <strip>"; return false; }
            const Strip *to = p.strip(c.flag("to"));
            if (!to) { err = "no strip `" + c.flag("to") + "`"; return false; }
            if (to->id == from->id) { err = "its clips already play through " + to->id; return false; }
            if (to->kind != from->kind)
            {
                err = to->id + " (" + to->name + ") is " + (to->kind == "bus" ? "a bus — a bus plays no clips" : "an " + to->kind + " strip") +
                      "; " + from->id + "'s clips need " + (from->kind == "audio" ? "an audio" : "an instrument") + " strip";
                return false;
            }
            int moved = 0;
            for (auto &cl : p.clips)
                if (cl.track == from->id) { cl.track = to->id; ++moved; }
            if (!moved) { err = from->id + " (" + from->name + ") has no clips to move"; return false; }
            // R-LANE-3: its tracks go with its clips — drawn where they were, now the other line's tracks,
            // keeping the name they showed
            for (auto &l : p.lanes)
                if (l.track == from->id)
                {
                    l.name = laneTitle(p, l);
                    l.track = to->id;
                    changed("lane.changed", l.id);
                }
            changed("strip.relinked", from->id);
            mOutput = std::to_string(moved) + "\n";
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
        case K::StripDuck:
        {
            // R-MIX-18: ONE line for the side-chain a producer means — the compressor, its settings and its key,
            // made by the commands that make each (so each one's rules hold), in one edit
            const std::string ch = c.arg(0), key = c.flag("under");
            if (!p.strip(ch)) { err = "no strip `" + ch + "`"; return false; }
            if (!p.strip(key)) { err = "--under names `" + key + "`, which is no strip"; return false; }
            if (key == ch) { err = "a strip cannot duck under itself"; return false; }
            double amount = 8.0;
            if (c.has("amount") && (!parseNumber(c.flag("amount"), amount) || amount < 1.0 || amount > 24.0)) { err = "--amount is dB, 1 … 24"; return false; }
            for (const auto &sd : p.sends)
                if (sd.sidechain && sd.from == key && sd.to == ch)
                {
                    err = ch + " already ducks under " + key + " (" + sd.id + ") — its compressor's ratio sets how far";
                    return false;
                }
            Command add;
            add.kind = K::DeviceAdd;
            add.args = {ch};
            add.flags = {{"type", "compressor"}};
            if (!mixCommand(add, err)) return false;
            const std::string dv = mOutput.substr(0, mOutput.find('\n'));
            // a full-scale key is −kThreshold over the threshold: it cuts (0 − kThreshold)·(1 − 1/R) = amount  ⇒
            // R = −kThreshold / (−kThreshold − amount). A real kick peaks under full scale and cuts less (the default
            // Drum Machine kick, 12 dB asked: 7.3 dB just after it, back within 2 dB by the next beat at 120 bpm)
            constexpr double kThreshold = -30.0;
            const double ratio = -kThreshold / (-kThreshold - amount);
            const double release = std::clamp(30000.0 / std::max(1.0, p.header.bpm), 5.0, 2000.0); // half a beat, ms
            char r[32], rel[32];
            std::snprintf(r, sizeof r, "%.4g", ratio);
            std::snprintf(rel, sizeof rel, "%.4g", release);
            std::string stored;
            for (const auto &kv : std::vector<std::pair<std::string, std::string>>{
                     {"threshold", "-30"}, {"ratio", r}, {"attack", "1"}, {"release", rel}, {"sidechain", "on"}})
                if (!setAddress(dv + "." + kv.first, kv.second, stored, err)) return false;
            Command send;
            send.kind = K::SendAdd;
            send.args = {key};
            send.flags = {{"to", ch}, {"pre", "1"}, {"sidechain", "1"}};
            if (!mixCommand(send, err)) return false;
            mOutput = dv + "\n"; // the compressor first; the send is said by `made:`
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
            sd.sidechain = c.has("sidechain"); // R-MIX-15: a key; the validator holds it to feedsForward, as any send
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
            dropBindingsOf({id});
            changed("send.deleted", id);
            return true;
        }
        case K::DeviceAdd:
        {
            const std::string track = c.arg(0);
            const Strip *s = track == "master" ? nullptr : p.strip(track);
            if (track != "master" && !s) { err = "no strip `" + track + "` (or `master`)"; return false; }
            DeviceNode made;
            if (c.has("plugin"))
            {
                // R-VST-6: another maker's effect, from the machine's list
                if (c.has("type")) { err = "--type or --plugin, not both"; return false; }
                if (c.has("preset")) { err = "a plugin's presets are its own — --preset is for a registry device"; return false; }
                if (!pluginNode(c.flag("plugin"), made, err)) return false;
                if (made.pluginKind == "instrument")
                { err = "`" + made.pluginName + "` is an instrument — make it an instrument strip (strip add --kind instrument --plugin " + made.plugin + ")"; return false; }
            }
            const std::string type = c.has("plugin") ? std::string() : c.flag("type");
            const DeviceType *t = c.has("plugin") ? nullptr : DeviceRegistry::find(type);
            if (!t && !c.has("plugin"))
            {
                std::vector<std::string> names;
                for (const auto &x : DeviceRegistry::types()) names.push_back(x.name);
                err = "no device type `" + type + "`";
                const auto near = nearest(type, names);
                if (near.empty())
                {
                    err += " (types:";
                    for (const auto &n : names) err += " " + n;
                    err += ")";
                }
                else err += " (did you mean: " + near[0] + "?)";
                return false;
            }
            const bool instrumentStrip = s && s->kind == "instrument";
            if (t && t->kind == DeviceKind::Instrument)
            { err = "a strip plays ONE instrument, its first device — make an instrument strip (strip add --kind instrument --instrument " + type + ")"; return false; }
            Rack *r = p.rack(track);
            if (!r) { p.racks.push_back(Rack{track, {}, {}, {}}); r = &p.racks.back(); }
            int at = (int)r->devices.size();
            if (!intFlag(c, "at", 0, (int)r->devices.size(), at, at, err)) return false;
            if (instrumentStrip && at == 0) at = 1; // the instrument stays first
            const std::string id = p.nextId("dv");
            if (!c.has("plugin")) made = DeviceNode{id, type, false, {}, {}};
            made.id = id;
            if (c.has("sample"))
            {
                // R-FX-14: a device that plays a recording, dropped with its file in one line (an impulse response)
                if (!t) { err = "--sample is for a registry device that plays one (convolution), not a plugin"; return false; }
                if (!t->takesSample) { err = "`" + type + "` plays no sample — --sample is for one that does (convolution)"; return false; }
                if (!resolveSample(c.flag("sample"), made.sample, err, t->takesDefinition)) return false;
            }
            r->devices.insert(r->devices.begin() + at, made);
            changed("device.added", id);
            if (c.has("preset") && !applyPreset(id, c.flag("preset"), err)) return false; // R-PRESET-1: dropped with its preset
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
                dropBindingsOf({id});
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
            if (c.has("strip"))
            {
                // R-LANE-3: an instrument's track — it shows the strip's name until it is given its own
                const Strip *s = p.strip(c.flag("strip"));
                if (!s) { err = "no strip `" + c.flag("strip") + "`"; return false; }
                if (s->kind != "instrument") { err = s->id + " is a " + s->kind + " strip — a track belongs to an instrument (R-LANE-3)"; return false; }
                l.track = s->id;
                l.name = c.arg(0, "");
            }
            else l.name = c.arg(0, "Lane " + std::to_string(p.lanes.size() + 1));
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
            gatherLaneGroups(p); // R-ARR-3: a group left with no lane goes
            changed("lane.deleted", id);
            return true;
        }
        case K::LaneMove:
        {
            // R-LANE-1: lanes are reordered — renumbered in their new order, as `mixer move` does
            const std::string id = c.arg(0);
            if (p.group(id)) return arrangeCommand(c, err); // R-ARR-3: a group moves whole
            if (!p.lane(id)) { err = "no lane `" + id + "`"; return false; }
            if (!c.has("to")) { err = "lane move needs --to <index>"; return false; }
            int to = 0;
            if (!intFlag(c, "to", 0, (int)p.lanes.size() - 1, 0, to, err)) return false;
            std::vector<const Lane *> order;
            for (const auto &l : p.lanes) order.push_back(&l);
            std::stable_sort(order.begin(), order.end(), [](const Lane *a, const Lane *b) { return a->order < b->order; });
            std::vector<std::string> ids;
            for (const Lane *l : order)
                if (l->id != id) ids.push_back(l->id);
            ids.insert(ids.begin() + to, id);
            for (size_t i = 0; i < ids.size(); ++i) p.lane(ids[i])->order = (int)i;
            {
                // R-ARR-3: landing between two lanes of one group joins it; leaving its own group's run leaves it
                Lane *l = p.lane(id);
                const std::string above = to > 0 ? p.lane(ids[(size_t)to - 1])->group : std::string();
                const std::string below = to + 1 < (int)ids.size() ? p.lane(ids[(size_t)to + 1])->group : std::string();
                if (!above.empty() && above == below) l->group = above;
                else if (!l->group.empty() && above != l->group && below != l->group) l->group.clear();
                gatherLaneGroups(p);
            }
            changed("lane.moved", id);
            return true;
        }
        default:
            return false;
        }
    }

    bool SolarisService::clipCommand(const Command &c, std::string &err)
    {
        Project &p = mProject;
        // R-LANE-3: a lane made for an instrument is its TRACK, showing the strip's name; any other is named
        auto newLane = [&](const std::string &name, const std::string &owner) {
            std::vector<int> orders;
            for (const auto &l : p.lanes) orders.push_back(l.order);
            Lane l;
            l.id = p.nextId("ln");
            const Strip *s = owner.empty() ? nullptr : p.strip(owner);
            if (s && s->kind == "instrument") l.track = owner;
            else l.name = name;
            l.order = nextOrder(orders);
            p.lanes.push_back(l);
            changed("lane.added", l.id);
            return l.id;
        };
        // an instrument's track: the one its newest clip is on, else its newest, else a new one
        auto trackOf = [&](const std::string &strip) {
            for (auto it = p.clips.rbegin(); it != p.clips.rend(); ++it)
                if (it->track == strip && !it->lane.empty())
                    if (const Lane *l = p.lane(it->lane); l && l->track == strip) return it->lane;
            for (auto it = p.lanes.rbegin(); it != p.lanes.rend(); ++it)
                if (it->track == strip) return it->id;
            return newLane(std::string(), strip);
        };
        // R-SVC-8 (AMENDS "a new lane"): with no --lane a clip goes where its strip's clips already are — the
        // newest one's lane — and a new lane only for a strip with none; `--lane new` asks for one. R-LANE-3:
        // an instrument's clip goes on its track, or (an older arrangement with none) the plain lane its clips are on
        auto laneFor = [&](const std::string &track, const std::string &name) {
            if (c.flag("lane") == "new") return newLane(name, track);
            if (c.has("lane")) return c.flag("lane");
            const Strip *s = p.strip(track);
            if (s && s->kind == "instrument")
            {
                for (const auto &l : p.lanes)
                    if (l.track == track) return trackOf(track);
                for (auto it = p.clips.rbegin(); it != p.clips.rend(); ++it)
                    if (it->track == track && !it->lane.empty())
                        if (const Lane *l = p.lane(it->lane); l && l->track.empty()) return it->lane;
                return newLane(name, track);
            }
            for (auto it = p.clips.rbegin(); it != p.clips.rend(); ++it)
                if (it->track == track && !it->lane.empty() && p.lane(it->lane)) return it->lane;
            return newLane(name, track);
        };
        // R-AUTO-14: an automation clip goes on a plain lane or its automation's track; nothing else goes on that track
        auto laneTakes = [&](const Clip &cl, const std::string &lane) {
            const Lane *l = lane.empty() ? nullptr : p.lane(lane);
            if (!lane.empty() && !l) { err = "no lane `" + lane + "`"; return false; }
            if (!l) return true;
            if (cl.isAuto())
            {
                if (!l->track.empty())
                {
                    err = cl.id + " is an automation clip — " + lane + " is the track of " + l->track + ", an instrument: its notes go there (R-AUTO-14)";
                    return false;
                }
                if (!l->automation.empty() && l->automation != automationRoot(p, cl.automation))
                {
                    err = cl.id + " plays " + cl.automation + " — " + lane + " is the track of " + l->automation + ", another automation (R-AUTO-14)";
                    return false;
                }
            }
            else if (!l->automation.empty())
            {
                err = cl.id + " is " + (cl.isAudio() ? "audio" : "a note clip") + " — " + lane + " is the track of " + l->automation +
                      ", an automation: its clips go there (R-AUTO-14)";
                return false;
            }
            return true;
        };
        switch (c.kind)
        {
        case K::ClipAdd:
        {
            Clip cl;
            if (!beatsFlag(c, "at", 0.0, cl.at, err)) return false;
            if (c.has("auto")) // R-AUTO-14: a clip of an automation — on its track (made if it has none), or the lane given
            {
                for (const char *f : {"src", "strip", "instrument", "plugin", "sample", "pattern", "in", "out", "warp", "preset"})
                    if (c.has(f)) { err = std::string("--auto places an automation — not with --") + f; return false; }
                const Automation *a = p.automation(c.flag("auto"));
                if (!a) { err = "no automation `" + c.flag("auto") + "`"; return false; }
                cl.automation = a->id; // its name is its automation's, shown — never stored
                if (!beatsFlag(c, "length", 0, cl.length, err)) return false;
                const std::string root = automationRoot(p, a->id);
                if (c.flag("lane") == "new" || !c.has("lane"))
                {
                    std::string track;
                    if (!c.has("lane"))
                        for (const auto &l : p.lanes)
                            if (l.automation == root) track = l.id; // its newest track
                    if (track.empty())
                    {
                        std::vector<int> orders;
                        for (const auto &l : p.lanes) orders.push_back(l.order);
                        Lane l;
                        l.id = p.nextId("ln");
                        l.automation = root;
                        l.order = nextOrder(orders);
                        p.lanes.push_back(l);
                        changed("lane.added", l.id);
                        track = l.id;
                    }
                    cl.lane = track;
                }
                else cl.lane = c.flag("lane");
                cl.id = p.nextId("ac");
                if (!laneTakes(cl, cl.lane)) return false;
                p.clips.push_back(cl);
                changed("clip.added", cl.id);
                mOutput = cl.id + "\n";
                return true;
            }
            if (c.has("lane") && c.flag("lane") != "new" && !p.lane(c.flag("lane"))) { err = "no lane `" + c.flag("lane") + "` (or `new`)"; return false; }
            // R-LANE-3: a clip put on an instrument's track plays through it
            std::string owner;
            if (c.has("lane") && c.flag("lane") != "new") owner = p.lane(c.flag("lane"))->track;
            auto trackName = [&](const std::string &ln, const std::string &ch) {
                const Strip *s = p.strip(ch);
                return ln + " is the track of " + ch + (s ? " (" + s->name + ")" : std::string());
            };
            if (!owner.empty())
            {
                if (c.has("src")) { err = trackName(c.flag("lane"), owner) + ", an instrument — an audio clip needs a lane of its own (R-LANE-3)"; return false; }
                if (c.has("instrument") || c.has("plugin")) { err = trackName(c.flag("lane"), owner) + " — a new instrument gets its own track: give --lane new (R-LANE-3)"; return false; }
                if (c.has("strip") && c.flag("strip") != owner)
                {
                    err = trackName(c.flag("lane"), owner) + " — a clip on it plays through " + owner + "; leave out --strip, or put it on " + c.flag("strip") + "'s track (R-LANE-3)";
                    return false;
                }
            }
            if (c.has("warp") && !c.has("src")) { err = "--warp is for an audio clip (--src)"; return false; }
            if (c.has("preset") && !c.has("instrument")) { err = c.has("plugin") ? "a plugin's presets are its own — --preset goes with --instrument" : "--preset goes with --instrument (a new instrument, dropped with its preset)"; return false; }
            std::string made; // --instrument: a new instrument strip, made by `strip add`'s own rules (R-BROWSE-3: one drop, one command)
            if (c.has("instrument") || c.has("plugin"))
            {
                const std::string flag = c.has("plugin") ? "--plugin" : "--instrument";
                if (c.has("src") || c.has("strip")) { err = flag + " makes its own strip — give it without --src or --strip"; return false; }
                Command sc;
                sc.kind = K::StripAdd;
                sc.flags = {{"kind", "instrument"}};
                if (c.has("instrument")) sc.flags.emplace_back("instrument", c.flag("instrument"));
                if (c.has("plugin")) sc.flags.emplace_back("plugin", c.flag("plugin")); // R-VST-6: a plugin, one drop
                if (c.has("sample")) sc.flags.emplace_back("sample", c.flag("sample")); // a sampler and its sound, one drop
                if (!mixCommand(sc, err)) return false;
                made = p.strips.back().id;
                if (c.has("preset") && !applyPreset(p.rack(made)->devices.front().id, c.flag("preset"), err)) return false; // R-PRESET-1
            }
            if (c.has("src"))
            {
                // R-MIX-2: a file the song has not used gets its own strip; a used one reuses it.
                // A relative path names a file in the SONG's folder when one is there (what the model stores, and
                // what the browser's "Song" list hands back); otherwise the caller's working directory.
                std::string given = c.flag("src");
                if (!fs::path(given).is_absolute() && !mPath.empty() && fs::exists(resolvePath(given))) given = resolvePath(given);
                cl.src = relativePath(given);
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
                        placeBeforeItsTarget(p, s.id);
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
                cl.lane = laneFor(cl.track, stem);
                if (c.has("warp")) // R-WARP-1: dropped warped — its tempo heard on import
                {
                    cl.bpm = tempoOf(cl);
                    if (cl.bpm <= 0) { err = "no tempo heard in " + c.flag("src") + " — add it without --warp, then set its .bpm and .warp"; return false; }
                    cl.warp = true;
                }
            }
            else if (c.has("strip") || !made.empty() || !owner.empty())
            {
                const std::string sid = !made.empty() ? made : c.has("strip") ? c.flag("strip") : owner;
                const Strip *s = p.strip(sid);
                if (!s) { err = "no strip `" + sid + "`"; return false; }
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
                    changed("pattern.added", pt.id); // R-SVC-8: every made node is said
                }
                if (!beatsFlag(c, "length", 0, cl.length, err)) return false;
                cl.name = p.pattern(cl.pattern)->name;
                cl.lane = laneFor(s->id, s->name);
            }
            else
            {
                err = "clip add needs --src <file> (audio), --strip <instrument strip>, --lane <an instrument's track> or --instrument <type> (notes)";
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
            if (cl->isAuto()) // R-AUTO-14: in time and onto a plain lane or its automation's track; it plays through nothing
            {
                if (c.has("strip")) { err = cl->id + " is an automation clip — it plays through no strip (R-AUTO-14)"; return false; }
                double at = cl->at;
                if (!beatsFlag(c, "at", cl->at, at, err)) return false;
                const std::string lane = !c.has("lane") ? cl->lane : c.flag("lane") == "none" ? std::string() : c.flag("lane");
                if (!laneTakes(*cl, lane)) return false;
                cl->at = at;
                cl->lane = lane;
                changed("clip.moved", cl->id);
                return true;
            }
            if (!beatsFlag(c, "at", cl->at, cl->at, err)) return false;
            std::string lane = !c.has("lane") ? cl->lane : c.flag("lane") == "none" ? std::string() : c.flag("lane");
            if (c.has("lane") && !laneTakes(*cl, lane)) return false;
            std::string strip = c.has("strip") ? c.flag("strip") : cl->track;
            if (c.has("strip") && !p.strip(strip)) { err = "no strip `" + strip + "`"; return false; }
            const std::string owner = lane.empty() || !p.lane(lane) ? std::string() : p.lane(lane)->track;
            if (!owner.empty())
            {
                // R-LANE-3: on an instrument's track a clip plays through that instrument
                const Strip *o = p.strip(owner);
                const std::string what = lane + " is the track of " + owner + (o ? " (" + o->name + ")" : std::string());
                if (cl->isAudio()) { err = cl->id + " is audio — " + what + ", an instrument; audio needs a lane of its own (R-LANE-3)"; return false; }
                if (c.has("lane") && c.has("strip") && strip != owner)
                {
                    err = what + " — a clip on it plays through " + owner + "; give --lane or --strip, not both (R-LANE-3)";
                    return false;
                }
                if (c.has("lane")) strip = owner;                 // moved onto a track: re-routed with it
                else if (strip != owner) lane = trackOf(strip);   // told to play through another: to that one's track
            }
            const bool rerouted = strip != cl->track;
            cl = p.clip(c.arg(0));                                // (a new track may have been made)
            cl->lane = lane;
            cl->track = strip;
            changed("clip.moved", cl->id);
            if (rerouted)
                if (const Strip *s = p.strip(strip)) mOutput = cl->id + " plays through " + s->id + " (" + s->name + ")\n";
            return true;
        }
        case K::ClipDuplicate:
        {
            const Clip *src = p.clip(c.arg(0));
            if (!src) { err = "no clip `" + c.arg(0) + "`"; return false; }
            int count = 1;
            if (!intFlag(c, "count", 1, 256, 1, count, err)) return false; // R-SVC-8: N copies end to end
            Clip copy = *src;
            copy.remarks = Remarks();
            if (c.has("lane"))
            {
                // R-CLIP-9: the copy on another lane — the same rule as a move's (R-CLIP-8, R-LANE-3)
                const std::string lane = c.flag("lane") == "none" ? std::string() : c.flag("lane");
                if (!lane.empty() && !p.lane(lane)) { err = "no lane `" + lane + "`"; return false; }
                if (!laneTakes(*src, lane)) return false;
                const std::string owner = lane.empty() ? std::string() : p.lane(lane)->track;
                if (!owner.empty())
                {
                    const Strip *o = p.strip(owner);
                    if (src->isAudio())
                    {
                        err = src->id + " is audio — " + lane + " is the track of " + owner + (o ? " (" + o->name + ")" : std::string()) +
                              ", an instrument; audio needs a lane of its own (R-LANE-3)";
                        return false;
                    }
                    copy.track = owner; // onto a track: it plays through that instrument
                }
                copy.lane = lane;
            }
            const double len = clipLengthBeats(p, *src);
            double first = 0;
            if (!beatsFlag(c, "at", toTick(src->at + len), first, err)) return false;
            const std::string srcTrack = src->track; // D-16: `src` points into p.clips, which a push_back below may move
            for (int k = 0; k < count; ++k)
            {
                copy.id = p.nextId("ac");
                copy.at = toTick(first + k * len);
                p.clips.push_back(copy); // a note clip's copy plays the SAME pattern: linked (R-CLIP-3)
                changed("clip.added", copy.id);
                if (k == 0) mOutput = copy.id + "\n";
            }
            if (copy.track != srcTrack)
                if (const Strip *s = p.strip(copy.track)) mOutput += "it plays through " + s->id + " (" + s->name + ")\n";
            return true;
        }
        case K::ClipUnique:
        {
            Clip *cl = p.clip(c.arg(0));
            if (!cl) { err = "no clip `" + c.arg(0) + "`"; return false; }
            if (cl->isAudio()) { err = cl->id + " is an audio clip — only a note clip shares a pattern"; return false; }
            if (cl->isAuto())
            {
                // R-AUTO-14: its own curve — a copy `of` the automation it plays, still driving what that one drives
                const Automation *from = p.automation(cl->automation);
                const std::string root = automationRoot(p, cl->automation);
                Automation copy = *from;
                copy.id = p.nextId("au");
                copy.of = root;
                int n = 2;
                for (const auto &a : p.automations) n += a.of == root;
                copy.name = p.automation(root)->name + " " + std::to_string(n);
                copy.remarks = Remarks();
                p.automations.push_back(copy);
                cl = p.clip(c.arg(0));
                cl->automation = copy.id;
                mBindingsTouched = true;
                changed("auto.added", copy.id);
                changed("clip.unique", cl->id);
                mOutput = copy.id + "\n";
                return true;
            }
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
            const bool chord = c.kind == K::NoteAdd && c.has("chord");
            if (chord && c.has("pitch")) { err = "--chord names its own notes — give it without --pitch"; return false; }
            if (!chord && (c.has("inversion") || c.has("octave"))) { err = "--inversion and --octave shape a --chord"; return false; }
            if ((!chord && !c.has("pitch")) || !c.has("at")) { err = c.kind == K::NoteAdd ? "a note needs --pitch (or --chord) and --at" : "a note needs --pitch and --at"; return false; }
            int pitch = 0;
            std::vector<int> pitches; // R-SVC-8: a chord is many notes, ONE edit
            if (chord)
            {
                int octave = 4, inversion = 0;
                if (!intFlag(c, "octave", -1, 9, 4, octave, err) || !intFlag(c, "inversion", 0, 8, 0, inversion, err)) return false;
                if (!chordPitches(c.flag("chord"), octave, inversion, pitches, err)) return false;
            }
            else
            {
                if (!pitchOf(c.flag("pitch"), pt->id, pitch, err)) return false;
                pitches = {pitch};
            }
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
            n.at = at;
            if (!beatsFlag(c, "length", 0.25, n.length, err)) return false;
            if (!(n.length > 0)) { err = "--length must be more than 0 beats"; return false; }
            if (!intFlag(c, "vel", 1, 127, 100, n.vel, err)) return false;
            if (c.has("chance") && (!parseNumber(c.flag("chance"), n.chance) || !(n.chance >= 0 && n.chance <= 1)))
            { err = "--chance is the odds it sounds, 0 to 1"; return false; } // R-ROLL-11
            if (const int mask = scaleMaskOf(*pt)) // R-ROLL-11: placed on the scale, a tie down — a chord's notes each
            {
                for (int &k : pitches) k = snapToScale(k, mask, 0);
                std::sort(pitches.begin(), pitches.end());
                pitches.erase(std::unique(pitches.begin(), pitches.end()), pitches.end());
            }
            std::string names;
            for (int k : pitches)
            {
                pitch = k;
                n.pitch = k;
                pt->notes.erase(std::remove_if(pt->notes.begin(), pt->notes.end(), same), pt->notes.end()); // replace, not stack
                pt->notes.push_back(n);
                names += (names.empty() ? "" : " ") + pitchName(k);
            }
            std::stable_sort(pt->notes.begin(), pt->notes.end(), [](const Note &a, const Note &b) {
                return a.at != b.at ? a.at < b.at : a.pitch < b.pitch;
            });
            if (chord) mOutput = c.flag("chord") + ": " + names + "\n";
            mOutput += growToHold(*pt); // R-ROLL-9 (it used to warn)
            changed("note.added", pt->id);
            return true;
        }
        case K::NoteMove:
        {
            Pattern *pt = p.pattern(c.arg(0));
            if (!pt) { err = "no pattern `" + c.arg(0) + "`"; return false; }
            if (!c.has("pitch") || !c.has("at")) { err = "a note is named by --pitch and --at"; return false; }
            if (!c.has("to-pitch") && !c.has("to-at") && !c.has("length") && !c.has("vel") && !c.has("chance"))
            { err = "note move needs --to-pitch, --to-at, --length, --vel and/or --chance"; return false; }
            int pitch = 0;
            double at = 0;
            if (!pitchOf(c.flag("pitch"), pt->id, pitch, err) || !beatsFlag(c, "at", 0, at, err)) return false;
            auto at_ = [&](int pi, double a) {
                for (auto &n : pt->notes)
                    if (n.pitch == pi && std::fabs(n.at - a) < 0.5 / kPpq) return &n;
                return (Note *)nullptr;
            };
            Note *n = at_(pitch, at);
            if (!n) { err = "no note " + std::to_string(pitch) + " at beat " + canonicalBeats(at) + " in " + pt->id; return false; }
            Note next = *n;
            if (c.has("to-pitch") && !pitchOf(c.flag("to-pitch"), pt->id, next.pitch, err)) return false;
            if (!beatsFlag(c, "to-at", n->at, next.at, err)) return false;
            if (!beatsFlag(c, "length", n->length, next.length, err)) return false;
            if (!(next.length > 0)) { err = "--length must be more than 0 beats"; return false; }
            if (!intFlag(c, "vel", 1, 127, n->vel, next.vel, err)) return false;
            if (c.has("chance") && (!parseNumber(c.flag("chance"), next.chance) || !(next.chance >= 0 && next.chance <= 1)))
            { err = "--chance is the odds it sounds, 0 to 1"; return false; } // R-ROLL-11
            if (const int mask = scaleMaskOf(*pt); mask && next.pitch != n->pitch) // R-ROLL-11: on to the scale's next, the way it moved
                next.pitch = snapToScale(next.pitch, mask, next.pitch > n->pitch ? 1 : -1);
            const Note *there = at_(next.pitch, next.at);
            if (there && there != n) { err = "a note is already at " + std::to_string(next.pitch) + ", beat " + canonicalBeats(next.at); return false; }
            *n = next;
            std::stable_sort(pt->notes.begin(), pt->notes.end(), [](const Note &a, const Note &b) {
                return a.at != b.at ? a.at < b.at : a.pitch < b.pitch;
            });
            mOutput = growToHold(*pt); // R-ROLL-9: moved or resized past the end, the pattern holds it
            changed("note.moved", pt->id);
            return true;
        }
        default:
            return false;
        }
    }
}
}
