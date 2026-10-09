#include "Compile.h"
#include "Bindings.h"
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

    // a parameter's text is the DSP library's (REQ-device-7) — the same text a VST3 plugin's state stores
    bool parseParam(const ParamSpec &spec, const std::string &text, double &value) { return paramFromText(spec, text, value); }

    std::string paramText(const ParamSpec &spec, double value) { return paramToText(spec, value); }

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
            if (!audible.count(s.id)) silent.insert(s.id);
        return silent;
    }

    namespace
    {
        engine::DeviceDesc deviceDesc(const DeviceNode &d, const PcmProvider &pcm, std::vector<std::string> &warnings, bool &ok)
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
            // R-EDM-8: a sampler's sound, decoded by the host; one that cannot be read plays nothing, said
            if (!d.sample.empty())
            {
                if (!t->takesSample) warnings.push_back("device " + d.id + " (" + d.type + ") plays no sample — `sample=` ignored");
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
        std::map<std::string, std::pair<int, int>> sendAt; // send id → (graph strip, its index in that strip's sends)
        const double spb = 60.0 / p.header.bpm * p.header.sampleRate; // samples per beat

        for (const Strip *s : order)
        {
            engine::Strip es;
            es.id = s->id;
            es.kind = s->kind == "instrument" ? engine::Strip::Instrument : s->kind == "bus" ? engine::Strip::Bus : engine::Strip::Audio;
            es.gain = engine::dbToLinear(s->gain);
            es.pan = s->pan;
            es.silent = silent.count(s->id) > 0;
            es.keyLive = !s->mute; // solo-silenced, it still keys (a soloed bass keeps its pump)
            es.out = target(s->out, stripIndex, portIndex);
            for (const auto &sd : p.sends)
                if (sd.from == s->id)
                {
                    sendAt[sd.id] = {(int)g.strips.size(), (int)es.sends.size()};
                    es.sends.push_back(engine::Send{target(sd.to, stripIndex, portIndex), engine::dbToLinear(sd.gain), sd.pre, sd.sidechain});
                }
            bool instrumentOk = true;
            if (const Rack *rk = p.rack(s->id))
                for (size_t k = 0; k < rk->devices.size(); ++k)
                {
                    bool ok = true;
                    auto dd = deviceDesc(rk->devices[k], pcm, r.warnings, ok);
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
                auto dd = deviceDesc(d, pcm, r.warnings, ok);
                if (ok)
                {
                    r.devices[dd.id] = {-1, (int)g.masterRack.size()};
                    g.masterRack.push_back(dd);
                }
            }

        // automation and formulas (R-AUTO-7): curves in samples, bindings ordered so a link reads an earlier one
        g.clock.samplesPerBeat = spb;
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
                b.kind = sp.kind == AddressSpec::StripGain ? engine::Bind::StripGain : engine::Bind::StripPan;
                b.strip = stripIndex.at(sp.node);
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
        return curve;
    }
}
}
