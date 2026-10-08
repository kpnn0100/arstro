#include "Compile.h"
#include "Format.h"
#include "MixLaws.h"
#include "device/Device.h"
#include <algorithm>
#include <cmath>
#include <map>

namespace arstro
{
namespace solaris
{
    long long beatsToSamples(const Project &p, double beats)
    {
        return (long long)std::llround(beats * 60.0 / p.header.bpm * p.header.sampleRate);
    }

    double secondsToBeats(const Project &p, double seconds) { return seconds * p.header.bpm / 60.0; }

    double clipLengthBeats(const Project &p, const Clip &c)
    {
        if (c.isAudio()) return (c.loop && c.length > 0) ? c.length : secondsToBeats(p, c.out - c.in);
        if (c.length > 0) return c.length;
        const Pattern *pt = p.pattern(c.pattern);
        return pt ? pt->length : 0.0;
    }

    bool parseParam(const ParamSpec &spec, const std::string &text, double &value)
    {
        if (spec.isChoice())
            for (size_t i = 0; i < spec.choices.size(); ++i)
                if (spec.choices[i] == text) { value = (double)i; return true; }
        double v = 0;
        if (!parseNumber(text, v)) return false;
        value = spec.clamp(v);
        return true;
    }

    std::string paramText(const ParamSpec &spec, double value)
    {
        const double v = spec.clamp(value);
        if (spec.isChoice()) return spec.choices[(size_t)v];
        if (spec.integer) return canonicalNumber(std::round(v));
        return canonicalNumber(v);
    }

    std::set<std::string> silentStrips(const Project &p)
    {
        std::set<std::string> silent, soloed;
        for (const auto &s : p.strips)
        {
            if (s.mute) silent.insert(s.id);
            if (s.solo) soloed.insert(s.id);
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
        for (const auto &sd : p.sends) edge(sd.from, sd.to);
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
            if (!audible.count(s.id)) silent.insert(s.id);
        return silent;
    }

    namespace
    {
        engine::DeviceDesc deviceDesc(const DeviceNode &d, std::vector<std::string> &warnings, bool &ok)
        {
            engine::DeviceDesc out;
            out.id = d.id;
            out.type = d.type;
            out.bypass = d.bypass;
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

    CompileResult compile(const Project &p, const PcmProvider &pcm)
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

        const auto order = p.stripsInOrder();
        std::map<std::string, int> stripIndex;
        for (const Strip *s : order)
        {
            stripIndex[s->id] = (int)r.stripIds.size();
            r.stripIds.push_back(s->id);
        }
        const auto silent = silentStrips(p);
        const double spb = 60.0 / p.header.bpm * p.header.sampleRate; // samples per beat

        for (const Strip *s : order)
        {
            engine::Strip es;
            es.id = s->id;
            es.kind = s->kind == "instrument" ? engine::Strip::Instrument : s->kind == "bus" ? engine::Strip::Bus : engine::Strip::Audio;
            es.gain = engine::dbToLinear(s->gain);
            es.pan = s->pan;
            es.silent = silent.count(s->id) > 0;
            es.out = target(s->out, stripIndex, portIndex);
            for (const auto &sd : p.sends)
                if (sd.from == s->id)
                    es.sends.push_back(engine::Send{target(sd.to, stripIndex, portIndex), engine::dbToLinear(sd.gain), sd.pre});
            bool instrumentOk = true;
            if (const Rack *rk = p.rack(s->id))
                for (size_t k = 0; k < rk->devices.size(); ++k)
                {
                    bool ok = true;
                    auto dd = deviceDesc(rk->devices[k], r.warnings, ok);
                    if (ok) es.rack.push_back(dd);
                    else if (k == 0 && es.kind == engine::Strip::Instrument) instrumentOk = false;
                }
            if (es.kind == engine::Strip::Instrument)
            {
                const DeviceType *first = es.rack.empty() ? nullptr : DeviceRegistry::find(es.rack[0].type);
                if (!instrumentOk || !first || first->kind != DeviceKind::Instrument)
                {
                    r.warnings.push_back("strip " + s->id + " has no instrument this build can play — it is silent");
                    es.kind = engine::Strip::Bus; // its effects still process what is routed to it
                }
            }

            for (const auto &c : p.clips)
            {
                if (c.track != s->id) continue;
                const long long start = (long long)std::llround(c.at * spb);
                if (c.isAudio() && es.kind == engine::Strip::Audio)
                {
                    engine::Region rg;
                    rg.clip = c.id;
                    rg.pcm = pcm ? pcm(c.src) : nullptr;
                    if (!rg.pcm) r.warnings.push_back("clip " + c.id + ": " + c.src + " is offline — silent");
                    rg.start = start;
                    rg.srcOffset = (long long)std::llround(c.in * p.header.sampleRate);
                    rg.srcFrames = (long long)std::llround((c.out - c.in) * p.header.sampleRate);
                    rg.loop = c.loop;
                    rg.frames = (c.loop && c.length > 0) ? (long long)std::llround((c.at + c.length) * spb) - start : rg.srcFrames;
                    rg.gain = engine::dbToLinear(c.gain);
                    rg.fadeIn = (long long)std::llround(c.fadeIn * spb);
                    rg.fadeOut = (long long)std::llround(c.fadeOut * spb);
                    g.end = std::max(g.end, rg.start + rg.frames);
                    es.regions.push_back(rg);
                }
                else if (!c.isAudio() && es.kind == engine::Strip::Instrument)
                {
                    const Pattern *pt = p.pattern(c.pattern);
                    if (!pt || pt->length <= 0) continue;
                    const double len = clipLengthBeats(p, c);
                    const double vel = engine::dbToLinear(c.gain); // clip gain scales velocity
                    // (C1) a clip longer than its pattern loops it; a note is cut at the clip's end
                    for (double k = 0; k < len; k += pt->length)
                        for (const auto &n : pt->notes)
                        {
                            const double on = k + n.at;
                            if (on >= len) continue;
                            const double off = std::min(on + n.length, len);
                            const int v = std::clamp((int)std::lround(n.vel * vel), 1, 127);
                            es.notes.push_back(engine::NoteEvent{(long long)std::llround((c.at + on) * spb), n.pitch, v, true});
                            es.notes.push_back(engine::NoteEvent{(long long)std::llround((c.at + off) * spb), n.pitch, 0, false});
                        }
                    g.end = std::max(g.end, (long long)std::llround((c.at + len) * spb));
                }
            }
            // (C2) by time; at one sample, note-offs before note-ons, so a repeated note retriggers
            std::stable_sort(es.notes.begin(), es.notes.end(), [](const engine::NoteEvent &a, const engine::NoteEvent &b) {
                return a.at != b.at ? a.at < b.at : (!a.on && b.on);
            });
            g.strips.push_back(es);
        }
        if (const Rack *mr = p.rack("master"))
            for (const auto &d : mr->devices)
            {
                bool ok = true;
                auto dd = deviceDesc(d, r.warnings, ok);
                if (ok) g.masterRack.push_back(dd);
            }
        return r;
    }
}
}
