#include "Compile.h"
#include "Bindings.h"
#include "Format.h"
#include "MixLaws.h"
#include "PluginDevice.h"
#include "device/Device.h"
#include "instrument/Sfz.h"
#include <algorithm>
#include <cmath>
#include <map>

namespace arstro
{
namespace solaris
{
    engine::TempoMap tempoMapOf(const Project &p)
    {
        auto m = engine::TempoMap::constant(p.header.bpm, p.header.sampleRate);
        for (const auto &t : p.tempos) m.add(t.at, t.bpm, t.ramp);
        return m;
    }

    long long beatsToSamples(const Project &p, double beats)
    {
        if (!p.tempos.empty()) return (long long)std::llround(tempoMapOf(p).sampleAt(beats)); // R-TEMPO-1
        return (long long)std::llround(beats * 60.0 / p.header.bpm * p.header.sampleRate);
    }

    double secondsToBeats(const Project &p, double seconds) { return seconds * p.header.bpm / 60.0; }

    double clipSecondsPerBeat(const Project &p, const Clip &c) { return c.warp && c.bpm > 0 ? 60.0 / c.bpm : 60.0 / p.header.bpm; }

    std::string stretchKey(const Project &p, const Clip &c)
    {
        if (!c.isAudio() || !c.takes.empty() || (!(c.warp && c.bpm > 0) && c.pitch == 0.0)) return std::string();
        const bool warped = c.warp && c.bpm > 0;
        if (warped && !p.tempos.empty())
        {
            // R-TEMPO-1: under a tempo that changes, the map itself — its own beats to where the song hears them
            std::string pts;
            for (const auto &t : p.tempos) pts += (pts.empty() ? "" : ";") + canonicalNumber(t.at) + "," + canonicalNumber(t.bpm) + (t.ramp ? ",r" : "");
            return "stretch:map:" + canonicalNumber(c.bpm) + ":" + canonicalNumber(c.pitch) + ":" + canonicalNumber(c.in) + ":" + canonicalNumber(c.out) + ":" +
                   canonicalNumber(c.at) + ":" + canonicalNumber(p.header.bpm) + ":" + pts + ":" + c.src;
        }
        const double ratio = warped ? c.bpm / p.header.bpm : 1.0;
        return "stretch:" + canonicalNumber(ratio) + ":" + canonicalNumber(c.pitch) + ":" + canonicalNumber(c.in) + ":" + canonicalNumber(c.out) + ":" + c.src;
    }

    bool parseStretchKey(const std::string &key, int rate, StretchSpec &out)
    {
        if (key.compare(0, 8, "stretch:") != 0) return false;
        size_t at = 8;
        auto field = [&](std::string &f) {
            const size_t colon = key.find(':', at);
            if (colon == std::string::npos) return false;
            f = key.substr(at, colon - at);
            at = colon + 1;
            return true;
        };
        std::string f;
        if (key.compare(8, 4, "map:") == 0)
        {
            at = 12;
            double clipBpm = 0, clipAt = 0, songBpm = 0;
            double *v[6] = {&clipBpm, &out.semitones, &out.in, &out.out, &clipAt, &songBpm};
            for (double *x : v)
                if (!field(f) || !parseNumber(f, *x)) return false;
            std::string pts;
            if (!field(pts)) return false;
            out.src = key.substr(at);
            if (out.src.empty() || clipBpm <= 0 || songBpm <= 0 || out.out <= out.in) return false;
            auto tm = engine::TempoMap::constant(songBpm, rate);
            size_t a = 0;
            while (a < pts.size())
            {
                size_t e = pts.find(';', a);
                if (e == std::string::npos) e = pts.size();
                const std::string one = pts.substr(a, e - a);
                double b = 0, bpm = 0;
                const size_t c1 = one.find(','), c2 = one.find(',', c1 + 1);
                if (c1 == std::string::npos || !parseNumber(one.substr(0, c1), b) || !parseNumber(one.substr(c1 + 1, c2 == std::string::npos ? std::string::npos : c2 - c1 - 1), bpm))
                    return false;
                tm.add(b, bpm, c2 != std::string::npos);
                a = e + 1;
            }
            // its own beats u, every sixteenth and at its end: heard at s(at + u) − s(at), read at u·60/bpm seconds in
            const double U = (out.out - out.in) * clipBpm / 60.0, s0 = tm.sampleAt(clipAt);
            for (double u = 0.0;; u += 0.25)
            {
                const double uu = std::min(u, U);
                out.map.push_back({tm.sampleAt(clipAt + uu) - s0, uu * 60.0 / clipBpm * rate});
                if (uu >= U) break;
            }
            out.ratio = out.map.back().first / std::max(1.0, out.map.back().second);
            return true;
        }
        double *v[4] = {&out.ratio, &out.semitones, &out.in, &out.out};
        for (double *x : v)
            if (!field(f) || !parseNumber(f, *x)) return false;
        out.src = key.substr(at);
        return !out.src.empty();
    }

    double clipLengthBeats(const Project &p, const Clip &c)
    {
        if (c.isAudio())
        {
            if (c.loop && c.length > 0) return c.length;
            if ((c.warp && c.bpm > 0) || p.tempos.empty()) return (c.out - c.in) / clipSecondsPerBeat(p, c); // R-WARP-1: its own beats
            const auto tm = tempoMapOf(p); // R-TEMPO-1: its seconds, where it is in the song
            return tm.beatAt(tm.sampleAt(c.at) + (c.out - c.in) * p.header.sampleRate) - c.at;
        }
        if (c.length > 0) return c.length;
        const Pattern *pt = p.pattern(c.pattern);
        return pt ? pt->length : 0.0;
    }

    void heardFades(const Project &p, const Clip &c, double &fadeIn, double &fadeOut)
    {
        fadeIn = c.fadeIn;
        fadeOut = c.fadeOut;
        if (!c.isAudio()) return;
        // (C6) the later of two overlapping clips begins inside the earlier and ends after it; one inside another is mixed
        const double cs = c.at, ce = c.at + clipLengthBeats(p, c), eps = 1e-9;
        for (const auto &d : p.clips)
        {
            if (&d == &c || !d.isAudio() || d.track != c.track || d.lane != c.lane) continue;
            const double ds = d.at, de = d.at + clipLengthBeats(p, d);
            if (ds < cs - eps && de > cs + eps && de < ce - eps) fadeIn = std::max(fadeIn, de - cs);
            if (ds > cs + eps && ds < ce - eps && de > ce + eps) fadeOut = std::max(fadeOut, ce - ds);
        }
    }

    // a parameter's text is the DSP library's (REQ-device-7) — the same text a VST3 plugin's state stores
    bool parseParam(const ParamSpec &spec, const std::string &text, double &value) { return paramFromText(spec, text, value); }

    std::string paramText(const ParamSpec &spec, double value) { return paramToText(spec, value); }

    double stripGainDb(const Project &p, const Strip &s)
    {
        const Strip *v = s.vca.empty() ? nullptr : p.strip(s.vca);
        const double vg = v && v->kind == "vca" ? v->gain : 0.0;
        return s.gain <= -119.95 || vg <= -119.95 ? -120.0 : s.gain + vg;
    }

    std::set<std::string> silentStrips(const Project &p)
    {
        std::set<std::string> silent, soloed;
        for (const auto &s : p.strips)
        {
            if (s.mute) silent.insert(s.id);
            if (s.solo) soloed.insert(s.id);
            // R-MIX-19: a VCA's mute mutes its members, its solo solos them — each as if its own
            if (const Strip *v = s.vca.empty() ? nullptr : p.strip(s.vca))
                if (v->kind == "vca")
                {
                    if (v->mute) silent.insert(s.id);
                    if (v->solo) soloed.insert(s.id);
                }
        }
        if (soloed.empty()) return silent;
        // edges: a strip → the strips its output and sends land on
        std::map<std::string, std::vector<std::string>> down, up;
        auto edge = [&](const std::string &a, const std::string &b) {
            if (!p.strip(b)) return; // master or a port
            down[a].push_back(b);
            up[b].push_back(a);
        };
        for (const auto &s : p.strips) edge(s.id, s.out);
        for (const auto &sd : p.sends)
            if (!sd.sidechain) edge(sd.from, sd.to); // a key is heard by a detector, not by you: it brings nothing into a solo (R-MIX-15)
        std::set<std::string> audible;
        auto walk = [&](const std::string &from, std::map<std::string, std::vector<std::string>> &g) {
            std::vector<std::string> stack = {from};
            while (!stack.empty())
            {
                const std::string id = stack.back();
                stack.pop_back();
                if (!audible.insert(id).second && id != from) continue;
                for (const auto &n : g[id])
                    if (!audible.count(n)) stack.push_back(n);
            }
        };
        for (const auto &s : soloed)
        {
            walk(s, down); // what the solo feeds: its buses, its sends' returns
            walk(s, up);   // what feeds the solo
        }
        for (const auto &s : p.strips)
            if (!audible.count(s.id) && !s.soloSafe) silent.insert(s.id); // R-MIX-20: a solo-safe bus no solo silences
        return silent;
    }

    namespace
    {
        engine::DeviceDesc deviceDesc(const DeviceNode &d, const PcmProvider &pcm, const TextProvider &text, std::vector<std::string> &warnings, bool &ok)
        {
            engine::DeviceDesc out;
            out.id = d.id;
            out.type = d.type;
            out.bypass = d.bypass;
            if (d.type == "vst3")
            {
                // R-VST-6: another maker's plugin — its parameters are numbers 0 … 1 by id; what plays it is the caller's
                // (attachPlugins); until then an offline stand-in, so the graph always builds
                ok = true;
                std::vector<std::string> names;
                for (const auto &kv : d.params)
                {
                    uint32_t id = 0;
                    double v = 0;
                    if (!pluginParamId(kv.first, id)) { warnings.push_back("device " + d.id + " (vst3): `" + kv.first + "` is not a plugin parameter (p<id>) — ignored"); continue; }
                    if (!parseNumber(kv.second, v) || !std::isfinite(v)) { warnings.push_back("device " + d.id + ": `" + kv.first + "=" + kv.second + "` is not a value — ignored"); continue; }
                    out.params.emplace_back(kv.first, std::min(1.0, std::max(0.0, v)));
                    names.push_back(kv.first);
                }
                auto c = std::make_shared<PluginConnection>();
                c->uid = d.plugin;
                c->bundle = d.bundle;
                c->name = d.pluginName;
                c->instrument = d.pluginKind == "instrument";
                c->failed = "not started";
                setPluginType(*c, names);
                out.make = [c] { return makePluginDevice(c, false); };
                return out;
            }
            const DeviceType *t = DeviceRegistry::find(d.type);
            ok = t != nullptr;
            if (!t)
            {
                warnings.push_back("device " + d.id + ": this build has no `" + d.type + "` — left out");
                return out;
            }
            for (const auto &kv : d.params)
            {
                const int i = t->paramIndex(kv.first);
                double v = 0;
                if (i < 0) { warnings.push_back("device " + d.id + " (" + d.type + "): no parameter `" + kv.first + "` — ignored"); continue; }
                if (!parseParam(t->params[i], kv.second, v))
                {
                    warnings.push_back("device " + d.id + ": `" + kv.first + "=" + kv.second + "` is not a value — ignored");
                    continue;
                }
                out.params.emplace_back(kv.first, v);
            }
            // R-EDM-8: a sampler's sound, decoded by the host; one that cannot be read plays nothing, said
            if (!d.sample.empty())
            {
                if (!t->takesSample) warnings.push_back("device " + d.id + " (" + d.type + ") plays no sample — `sample=` ignored");
                else if (t->takesDefinition)
                {
                    // R-INST-9: an instrument's definition (an SFZ): its text, read by the DSP library's parser, and
                    // each sound it names, beside it — one that cannot be read leaves its regions silent, said
                    std::string body, where;
                    if (!text || !text(d.sample, body, where)) warnings.push_back("device " + d.id + ": cannot read its instrument " + d.sample + " — it plays nothing");
                    else
                    {
                        const SfzInstrument sfz = parseSfz(body);
                        for (const auto &pr : sfz.problems) warnings.push_back("device " + d.id + ": " + d.sample + ": " + pr);
                        const auto slash = where.find_last_of('/');
                        const std::string dir = slash == std::string::npos ? std::string(".") : where.substr(0, slash);
                        for (const auto &name : sfzSounds(sfz))
                        {
                            auto snd = pcm ? pcm(dir + "/" + name) : nullptr;
                            if (!snd) warnings.push_back("device " + d.id + ": cannot read " + name + " (named by " + d.sample + ") — its regions are silent");
                            else out.named.emplace_back(name, snd);
                        }
                        out.definition = body;
                    }
                }
                else if (!(out.sample = pcm ? pcm(d.sample) : nullptr)) warnings.push_back("device " + d.id + ": cannot read its sample " + d.sample + " — it plays nothing");
            }
            return out;
        }

        engine::Target target(const std::string &to, const std::map<std::string, int> &stripIndex,
                              const std::map<std::string, int> &portIndex)
        {
            engine::Target t;
            if (to.empty() || to == "master") { t.kind = engine::Target::Master; return t; }
            auto s = stripIndex.find(to);
            if (s != stripIndex.end()) { t.kind = engine::Target::Strip; t.index = s->second; return t; }
            auto po = portIndex.find(to);
            if (po != portIndex.end()) { t.kind = engine::Target::Port; t.index = po->second; return t; }
            t.kind = engine::Target::None;
            return t;
        }
    }

    CompileResult compile(const Project &p, const PcmProvider &pcm, const TextProvider &text)
    {
        CompileResult r;
        engine::MixGraph &g = r.graph;
        g.sampleRate = p.header.sampleRate;
        g.masterGain = engine::dbToLinear(p.header.masterGain);

        std::map<std::string, int> portIndex;
        for (const auto &po : p.ports)
            if (po.dir == "out")
            {
                portIndex[po.id] = (int)g.ports.size();
                g.ports.push_back(engine::Port{po.id, po.name, po.channels});
            }
        for (const auto &o : p.header.masterOut)
            if (portIndex.count(o)) g.masterPorts.push_back(portIndex[o]);

        std::vector<const Strip *> order;
        for (const Strip *s : p.stripsInOrder())
            if (s->kind != "vca") order.push_back(s); // R-MIX-19: a VCA carries no sound — no strip of the graph
        std::map<std::string, int> stripIndex;
        for (const Strip *s : order)
        {
            stripIndex[s->id] = (int)r.stripIds.size();
            r.stripIds.push_back(s->id);
        }
        // R-MIX-19: the dB a strip's VCA adds (0 for none)
        auto vcaDb = [&](const Strip &s) {
            const Strip *v = s.vca.empty() ? nullptr : p.strip(s.vca);
            return v && v->kind == "vca" ? v->gain : 0.0;
        };
        const auto silent = silentStrips(p);
        std::map<std::string, std::pair<int, int>> sendAt; // send id → (graph strip, its index in that strip's sends)
        const double spb = 60.0 / p.header.bpm * p.header.sampleRate; // samples per beat (at the start)
        // R-TEMPO-1: a beat's sample through the song's map — for a song of one tempo, b·spb exactly as ever
        const engine::TempoMap tm = tempoMapOf(p);
        const bool oneTempo = tm.isConstant();
        auto S = [&](double b) { return (long long)std::llround(oneTempo ? b * spb : tm.sampleAt(b)); };

        for (const Strip *s : order)
        {
            engine::Strip es;
            es.id = s->id;
            es.kind = s->kind == "instrument" ? engine::Strip::Instrument : s->kind == "bus" ? engine::Strip::Bus : engine::Strip::Audio;
            es.gain = engine::dbToLinear(stripGainDb(p, *s)); // R-MIX-19: its own plus its VCA's
            es.invert = s->phase;                            // R-MIX-20
            es.width = s->width / 100.0;
            es.pan = s->pan;
            es.silent = silent.count(s->id) > 0;
            const Strip *vs = s->vca.empty() ? nullptr : p.strip(s->vca);
            es.keyLive = !s->mute && !(vs && vs->kind == "vca" && vs->mute); // solo-silenced, it still keys; muted (or its VCA), not
            es.out = target(s->out, stripIndex, portIndex);
            for (const auto &sd : p.sends)
                if (sd.from == s->id)
                {
                    sendAt[sd.id] = {(int)g.strips.size(), (int)es.sends.size()};
                    es.sends.push_back(engine::Send{target(sd.to, stripIndex, portIndex), engine::dbToLinear(sd.gain), sd.pre, sd.sidechain});
                }
            if (!s->frozen.empty())
            {
                // R-PERF-3: frozen — it plays its file from the start instead of its own work (its clips, notes and
                // devices kept but idle: not built); its fader, pan, sends and output as ever
                es.kind = engine::Strip::Audio;
                if (auto pc = pcm ? pcm(s->frozen) : nullptr)
                {
                    engine::Region rg;
                    rg.clip = s->id + ":frozen";
                    rg.pcm = pc;
                    rg.frames = rg.srcFrames = pc->frames;
                    es.regions.push_back(rg);
                }
                else r.warnings.push_back("strip " + s->id + " is frozen, but its file " + s->frozen + " cannot be read — it is silent");
                for (const auto &c : p.clips) // the song ends where it did: its clips' ends, not the file's tail
                    if (c.track == s->id) g.end = std::max(g.end, S(c.at + clipLengthBeats(p, c)));
                g.strips.push_back(es);
                continue;
            }
            bool instrumentOk = true;
            if (const Rack *rk = p.rack(s->id))
                for (size_t k = 0; k < rk->devices.size(); ++k)
                {
                    bool ok = true;
                    auto dd = deviceDesc(rk->devices[k], pcm, text, r.warnings, ok);
                    if (ok)
                    {
                        r.devices[dd.id] = {(int)g.strips.size(), (int)es.rack.size()};
                        es.rack.push_back(dd);
                    }
                    else if (k == 0 && es.kind == engine::Strip::Instrument) instrumentOk = false;
                }
            if (es.kind == engine::Strip::Instrument)
            {
                const DeviceType *first = es.rack.empty() ? nullptr : DeviceRegistry::find(es.rack[0].type);
                const Rack *rk = p.rack(s->id);
                const bool plugin = !es.rack.empty() && es.rack[0].type == "vst3" && rk && rk->devices[0].pluginKind == "instrument"; // R-VST-6
                if (!instrumentOk || (!plugin && (!first || first->kind != DeviceKind::Instrument)))
                {
                    r.warnings.push_back("strip " + s->id + " has no instrument this build can play — it is silent");
                    es.kind = engine::Strip::Bus; // its effects still process what is routed to it
                }
            }

            for (const auto &c : p.clips)
            {
                if (c.track != s->id) continue;
                const long long start = S(c.at);
                if (c.isAudio() && es.kind == engine::Strip::Audio && !c.takes.empty())
                {
                    // (C5) R-REC-2: each stretch of the clip from the take its comp gives it — the regions overlap 5 ms
                    // either side of a seam, faded across it, so a cut never clicks; the clip's own fades at its ends
                    const double len = clipLengthBeats(p, c);
                    std::vector<double> cuts{c.at, c.at + len};
                    for (const auto &x : c.comp)
                        for (double b : {x.from, x.to})
                            if (b > c.at + 1e-9 && b < c.at + len - 1e-9) cuts.push_back(b);
                    std::sort(cuts.begin(), cuts.end());
                    cuts.erase(std::unique(cuts.begin(), cuts.end(), [](double a, double b) { return std::fabs(a - b) < 1e-9; }), cuts.end());
                    std::vector<std::pair<int, std::pair<double, double>>> runs; // take, [from, to) — neighbours of one take merged
                    for (size_t k = 0; k + 1 < cuts.size(); ++k)
                    {
                        const int t = c.takeAt(0.5 * (cuts[k] + cuts[k + 1]));
                        if (!runs.empty() && runs.back().first == t) runs.back().second.second = cuts[k + 1];
                        else runs.push_back({t, {cuts[k], cuts[k + 1]}});
                    }
                    const long long x5 = (long long)std::llround(0.005 * p.header.sampleRate);
                    const long long clipFrames = S(c.at + len) - start;
                    double fin = 0, fout = 0;
                    heardFades(p, c, fin, fout);
                    for (size_t k = 0; k < runs.size(); ++k)
                    {
                        const auto &take = c.takes[(size_t)runs[k].first - 1];
                        engine::Region rg;
                        rg.clip = c.id;
                        rg.pcm = pcm ? pcm(take.src) : nullptr;
                        if (!rg.pcm) r.warnings.push_back("clip " + c.id + ": take " + std::to_string(runs[k].first) + ", " + take.src + ", is offline — silent");
                        const long long a = S(runs[k].second.first) - (k > 0 ? x5 : 0);
                        const long long b = S(runs[k].second.second) + (k + 1 < runs.size() ? x5 : 0);
                        rg.start = a;
                        rg.frames = b - a;
                        rg.srcOffset = (long long)std::llround(c.in * p.header.sampleRate) + (a - start); // a take begins where the clip does
                        rg.srcFrames = rg.pcm ? rg.pcm->frames : rg.frames;
                        if (c.reverse)
                        {
                            // R-EDIT-1: the clip read from its end — this stretch, the clip's span mirrored
                            rg.reverse = true;
                            rg.srcFrames = rg.frames;
                            rg.srcOffset = (long long)std::llround(c.in * p.header.sampleRate) + clipFrames - (a - start) - rg.frames;
                        }
                        rg.gain = engine::dbToLinear(c.gain);
                        rg.fadeIn = k > 0 ? 2 * x5 : oneTempo ? (long long)std::llround(fin * spb) : S(c.at + fin) - start;
                        rg.fadeOut = k + 1 < runs.size() ? 2 * x5 : oneTempo ? (long long)std::llround(fout * spb) : S(c.at + len) - S(c.at + len - fout);
                        g.end = std::max(g.end, rg.start + rg.frames);
                        es.regions.push_back(rg);
                    }
                }
                else if (c.isAudio() && es.kind == engine::Strip::Audio)
                {
                    engine::Region rg;
                    rg.clip = c.id;
                    const std::string key = stretchKey(p, c); // R-WARP-1: its span stretched, made once by the provider
                    rg.pcm = pcm ? pcm(key.empty() ? c.src : key) : nullptr;
                    if (!rg.pcm) r.warnings.push_back("clip " + c.id + ": " + c.src + " is offline — silent");
                    rg.start = start;
                    rg.srcOffset = key.empty() ? (long long)std::llround(c.in * p.header.sampleRate) : 0;
                    // the span's frames — stretched, the stretched sound's (an offline one, as long as it would be)
                    const double ratio = c.warp && c.bpm > 0 ? c.bpm / p.header.bpm : 1.0;
                    rg.srcFrames = !key.empty() && rg.pcm ? rg.pcm->frames : (long long)std::llround((c.out - c.in) * p.header.sampleRate * ratio);
                    rg.loop = c.loop;
                    rg.reverse = c.reverse; // R-EDIT-1
                    rg.frames = (c.loop && c.length > 0) ? S(c.at + c.length) - start : rg.srcFrames;
                    rg.gain = engine::dbToLinear(c.gain);
                    double fin = 0, fout = 0;
                    heardFades(p, c, fin, fout); // (C6) R-EDIT-1: an overlap on its lane crossfades
                    const double len = clipLengthBeats(p, c);
                    rg.fadeIn = oneTempo ? (long long)std::llround(fin * spb) : S(c.at + fin) - start;
                    rg.fadeOut = oneTempo ? (long long)std::llround(fout * spb) : S(c.at + len) - S(c.at + len - fout);
                    g.end = std::max(g.end, rg.start + rg.frames);
                    es.regions.push_back(rg);
                }
                else if (!c.isAudio() && es.kind == engine::Strip::Instrument)
                {
                    const Pattern *pt = p.pattern(c.pattern);
                    if (!pt || pt->length <= 0) continue;
                    const double len = clipLengthBeats(p, c);
                    const double vel = engine::dbToLinear(c.gain); // clip gain scales velocity
                    // (C1) a clip longer than its pattern loops it; a note is cut at the clip's end; (C4) R-ROLL-11 a
                    // note of chance < 1 is rolled per repetition — a hash, never a clock
                    // R-LAUNCH-1: a clip may start `offset` beats into its pattern — its notes read in pattern time, then moved
                    const double off = c.offset;
                    long long rep = 0;
                    for (double k = 0; k - off < len; k += pt->length, ++rep)
                        for (const auto &n : pt->notes)
                        {
                            const double on = k + n.at - off;
                            if (on < 0 || on >= len || !noteSounds(n, c.id, rep)) continue;
                            const double off = std::min(on + n.length, len);
                            const int v = std::clamp((int)std::lround(n.vel * vel), 1, 127);
                            es.notes.push_back(engine::NoteEvent{S(c.at + on), n.pitch, v, true});
                            es.notes.push_back(engine::NoteEvent{S(c.at + off), n.pitch, 0, false});
                        }
                    // (C3) R-MIDI-3: the wheels — each repetition's lane from its start, a ramp a step every kControl
                    // samples, each point on its sample; at the clip's end, at rest again
                    for (const char *lane : {"bend", "cc1"})
                    {
                        std::vector<const ExprPoint *> pts;
                        for (const auto &x : pt->expr)
                            if (x.lane == lane) pts.push_back(&x);
                        if (pts.empty()) continue;
                        std::stable_sort(pts.begin(), pts.end(), [](const ExprPoint *a, const ExprPoint *b) { return a->at < b->at; });
                        const int kind = lane[0] == 'b' ? 0 : 1;
                        const double step = (double)engine::kControlSamples / (oneTempo ? spb : tm.samplesPerBeatAt(c.at)); // beats
                        // pattern time `k + t`, played at clip time `k + t − offset` (R-LAUNCH-1); before the clip: not played
                        auto emit = [&](double beat, double value) {
                            if (beat < 0) return;
                            es.controls.push_back(engine::ControlEvent{S(c.at + beat), kind, value});
                        };
                        if (off > 0) emit(0, exprAt(pt->expr, lane, std::fmod(off, pt->length))); // where it starts into its pattern
                        for (double k = 0; k - off < len; k += pt->length)
                        {
                            const double end = std::min(k + pt->length - off, len);
                            emit(k - off, exprAt(pt->expr, lane, 0.0));
                            for (size_t i = 0; i < pts.size(); ++i)
                            {
                                if (k + pts[i]->at - off >= end) break;
                                emit(k + pts[i]->at - off, pts[i]->value);
                                if (i + 1 < pts.size() && pts[i + 1]->value != pts[i]->value)
                                    for (double t = pts[i]->at + step; t < pts[i + 1]->at && k + t - off < end; t += step)
                                        emit(k + t - off, exprAt(pt->expr, lane, t));
                            }
                        }
                        emit(len, 0.0);
                    }
                    g.end = std::max(g.end, S(c.at + len));
                }
            }
            std::stable_sort(es.controls.begin(), es.controls.end(), [](const engine::ControlEvent &a, const engine::ControlEvent &b) { return a.at < b.at; });
            // (C2) by time; at one sample, note-offs before note-ons, so a repeated note retriggers
            std::stable_sort(es.notes.begin(), es.notes.end(), [](const engine::NoteEvent &a, const engine::NoteEvent &b) {
                return a.at != b.at ? a.at < b.at : (!a.on && b.on);
            });
            // R-LAUNCH-1: its cells of the session grid, heard only once launched — each a loop from its own start, at the
            // song's first tempo (a slot does not follow a tempo map's changes yet); a pattern shorter than
            // its slot repeats in it, a note cut at the slot's end; chance rolled per repetition, as a clip's
            {
                const double spb0 = 60.0 / p.header.bpm * p.header.sampleRate;
                auto at0 = [&](double beats) { return (long long)std::llround(beats * spb0); };
                for (const auto &sl : p.slots)
                {
                    if (sl.strip != s->id) continue;
                    engine::SlotPlay sp;
                    sp.id = sl.id;
                    if (!sl.pattern.empty() && es.kind == engine::Strip::Instrument)
                    {
                        const Pattern *pt = p.pattern(sl.pattern);
                        if (!pt || pt->length <= 0) continue;
                        const double len = sl.length > 0 ? sl.length : pt->length;
                        sp.length = at0(len);
                        long long rep = 0;
                        for (double k = 0; k < len; k += pt->length, ++rep)
                            for (const auto &n : pt->notes)
                            {
                                const double on = k + n.at;
                                if (on >= len || !noteSounds(n, sl.id, rep)) continue;
                                sp.notes.push_back(engine::NoteEvent{at0(on), n.pitch, std::clamp(n.vel, 1, 127), true});
                                sp.notes.push_back(engine::NoteEvent{at0(std::min(on + n.length, len)), n.pitch, 0, false});
                            }
                        std::stable_sort(sp.notes.begin(), sp.notes.end(), [](const engine::NoteEvent &a, const engine::NoteEvent &b) {
                            return a.at != b.at ? a.at < b.at : (!a.on && b.on);
                        });
                    }
                    else if (!sl.src.empty() && es.kind == engine::Strip::Audio)
                    {
                        sp.pcm = pcm ? pcm(sl.src) : nullptr;
                        if (!sp.pcm) r.warnings.push_back("slot " + sl.id + ": " + sl.src + " is offline — silent");
                        sp.srcOffset = (long long)std::llround(sl.in * p.header.sampleRate);
                        sp.srcFrames = sl.out > sl.in ? (long long)std::llround((sl.out - sl.in) * p.header.sampleRate)
                                                      : (sp.pcm ? std::max(0LL, sp.pcm->frames - sp.srcOffset) : 0);
                        sp.length = sl.length > 0 ? at0(sl.length) : sp.srcFrames;
                        sp.gain = engine::dbToLinear(sl.gain);
                    }
                    else continue;
                    es.slots.push_back(sp);
                }
            }
            g.strips.push_back(es);
        }
        if (const Rack *mr = p.rack("master"))
            for (const auto &d : mr->devices)
            {
                bool ok = true;
                auto dd = deviceDesc(d, pcm, text, r.warnings, ok);
                if (ok)
                {
                    r.devices[dd.id] = {-1, (int)g.masterRack.size()};
                    g.masterRack.push_back(dd);
                }
            }

        // automation and formulas (R-AUTO-7): curves in samples, bindings ordered so a link reads an earlier one
        g.clock.samplesPerBeat = spb;
        g.clock.map = tm; // R-TEMPO-1: formulas and curves read the song's beat through it
        g.clock.beatsPerBar = std::max(1, std::atoi(p.header.sig.c_str()));
        g.clock.bpm = p.header.bpm;
        g.clock.rate = p.header.sampleRate;
        std::map<std::string, int> curveOf;
        for (const auto &a : p.automations)
        {
            curveOf[a.id] = (int)g.curves.size();
            r.curveIds.push_back(a.id);
            g.curves.push_back(compileCurve(a, spb));
        }
        std::vector<std::string> problems;
        const auto ordered = orderBindings(p, problems);
        for (const auto &x : problems) r.warnings.push_back("binding " + x + " — inert, its own value plays");
        std::map<std::string, int> bindOf;
        for (const auto &ob : ordered)
        {
            engine::Bind b;
            const AddressSpec &sp = ob.spec;
            switch (sp.kind)
            {
            case AddressSpec::StripGain:
            case AddressSpec::StripPan:
                if (!stripIndex.count(sp.node))
                {
                    // R-MIX-19: a VCA's own gain bound — not yet (refused at the grammar; a file edited by hand)
                    r.warnings.push_back("binding " + ob.binding->address + ": a VCA's gain or pan is not bound yet — inert");
                    continue;
                }
                b.kind = sp.kind == AddressSpec::StripGain ? engine::Bind::StripGain : engine::Bind::StripPan;
                b.strip = stripIndex.at(sp.node);
                if (sp.kind == AddressSpec::StripGain)
                    if (const Strip *s = p.strip(sp.node)) b.offset = vcaDb(*s); // R-MIX-19: its VCA's dB, after the formula
                break;
            case AddressSpec::SendGain:
            {
                const auto it = sendAt.find(sp.node);
                if (it == sendAt.end()) { r.warnings.push_back("binding " + ob.binding->address + ": its send is not in the graph — inert"); continue; }
                b.kind = engine::Bind::SendGain;
                b.strip = it->second.first;
                b.index = it->second.second;
                break;
            }
            case AddressSpec::MasterGain:
                b.kind = engine::Bind::MasterGain;
                break;
            case AddressSpec::DeviceParam:
            {
                const auto it = r.devices.find(sp.node);
                if (it == r.devices.end()) { r.warnings.push_back("binding " + ob.binding->address + ": its device is left out — inert"); continue; }
                b.kind = it->second.first < 0 ? engine::Bind::MasterDeviceParam : engine::Bind::DeviceParam;
                b.strip = it->second.first;
                b.index = it->second.second;
                b.param = sp.paramIndex;
                if (sp.paramIndex < 0) b.paramName = sp.field; // R-VST-6: a plugin's, found by name at build
                break;
            }
            }
            b.lo = sp.lo;
            b.hi = sp.hi;
            b.integer = sp.integer;
            b.own = sp.value;
            b.expr = ob.formula.expr;
            const int bindBase = engine::kClockSlots + (int)g.curves.size();
            for (auto &o : b.expr.ops)
            {
                if (o.kind != engine::ExprOp::Var || o.index >= 0) continue;
                const std::string &name = ob.formula.names[(size_t)(-o.index - 1)];
                if (curveOf.count(name)) o.index = engine::kClockSlots + curveOf[name];
                else if (bindOf.count(name)) o.index = bindBase + bindOf[name];
                else
                {
                    // a link to an address nothing drives: its own value, a constant
                    AddressSpec other;
                    std::string why;
                    describeAddress(p, name, other, why);
                    o.kind = engine::ExprOp::Num;
                    o.num = other.value;
                }
            }
            bindOf[ob.binding->address] = (int)g.binds.size();
            r.bindAddresses.push_back(ob.binding->address);
            g.binds.push_back(std::move(b));
        }
        return r;
    }

    engine::Curve compileCurve(const Automation &a, double spb)
    {
        std::vector<engine::CurvePoint> pts;
        pts.reserve(a.points.size());
        for (const auto &pt : a.points)
        {
            engine::CurvePoint c;
            c.t = (double)std::llround(pt.at * spb);
            c.v = pt.value;
            if (!engine::shapeNamed(pt.shape, c.shape)) c.shape = engine::CurvePoint::Linear; // validate refuses it first
            c.speedIn = pt.speedIn / spb; // the .slp's are per BEAT
            c.inflIn = pt.inflIn;
            c.speedOut = pt.speedOut / spb;
            c.inflOut = pt.inflOut;
            pts.push_back(c);
        }
        engine::Curve curve;
        curve.keys = engine::curveKeys(pts);
        curve.lo = a.min;
        curve.hi = a.max;
        curve.loop = a.loop > 0 ? std::llround(a.loop * spb) : 0; // R-AUTO-12
        curve.from = std::llround(a.at * spb);
        curve.span = std::llround(a.length * spb);
        return curve;
    }
}
}
