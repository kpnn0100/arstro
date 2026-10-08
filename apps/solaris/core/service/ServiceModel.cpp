// SolarisService — what a front end can see: the AppModel refresh, `audit` (R-MIX-10) and the
// matrix (R-MIX-9). Everything here is COMPUTED from the project, never stored in it: "fed by",
// audibility under solos, resolved clip lengths, linked counts — a second front end reading the
// model gets the same answers without re-deriving them (arstro.rule §1).
#include "Compile.h"
#include "Format.h"
#include "Json.h"
#include "SolarisService.h"
#include "device/Device.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <sstream>

namespace arstro
{
namespace solaris
{
    namespace
    {
        DeviceModel deviceModel(const DeviceNode &d)
        {
            DeviceModel m;
            m.id = d.id;
            m.type = d.type;
            m.bypass = d.bypass;
            const DeviceType *t = DeviceRegistry::find(d.type);
            m.known = t != nullptr;
            if (!t)
            {
                m.label = d.type;
                for (const auto &kv : d.params)
                {
                    ParamModel pm;
                    pm.name = pm.label = kv.first;
                    pm.text = kv.second;
                    m.params.push_back(pm);
                }
                return m;
            }
            m.label = t->label;
            m.instrument = t->kind == DeviceKind::Instrument;
            // Every registry parameter, stored or not: the UI draws a knob per parameter (R-UI-5).
            for (const auto &spec : t->params)
            {
                ParamModel pm;
                pm.name = spec.name;
                pm.label = spec.label;
                pm.unit = spec.unit;
                pm.min = spec.min;
                pm.max = spec.max;
                pm.def = spec.def;
                pm.choices = spec.choices;
                pm.value = spec.def;
                for (const auto &kv : d.params)
                    if (kv.first == spec.name) parseParam(spec, kv.second, pm.value);
                pm.text = paramText(spec, pm.value);
                m.params.push_back(pm);
            }
            return m;
        }

        std::string dbText(float lin)
        {
            return lin > 0 ? canonicalNumber(std::round(20.0 * std::log10(lin) * 10.0) / 10.0) : "-inf";
        }
    }

    void SolarisService::refreshModel()
    {
        const bool dirty = mModel.dirty;
        const long long rev = mModel.revision;
        const std::string lastError = mModel.lastError;
        const TransportModel transport = mModel.transport;
        mModel = AppModel();
        mModel.transport = transport;
        mModel.transport.loopFrom = mLoopFrom;
        mModel.transport.loopTo = mLoopTo;
        if (!mModel.transport.playing) mModel.transport.position = mPosition;
        mModel.revision = rev + 1;
        mModel.lastError = lastError;
        mModel.audit = mAudit;
        // the machine's side: there with or without a song
        mModel.settings.sampleRate = mSettings.sampleRate;
        mModel.settings.bufferSize = mSettings.bufferSize;
        mModel.settings.latencyMs = std::round(1000.0 * mSettings.bufferSize / mSettings.sampleRate * 10.0) / 10.0;
        mModel.settings.output = mSettings.output;
        mModel.settings.input = mSettings.input;
        mModel.settings.folders = mSettings.folders;
        for (const auto &p : mSettings.ports) mModel.settings.ports.push_back(p.first + "=" + p.second);
        mModel.devices = mDevices;
        mModel.browser = mBrowser;
        if (mRecentsStale)
        {
            // Home's cards read each song's header: done when the list or a song changes, not per command
            mRecentCards.clear();
            for (const auto &path : mRecents)
            {
                RecentModel r;
                r.path = path;
                std::ifstream f(path, std::ios::binary);
                std::stringstream ss;
                Project song;
                std::string why;
                if (f) ss << f.rdbuf();
                if (!f || !parseProject(ss.str(), song, why)) r.missing = true;
                else
                {
                    r.name = song.header.name;
                    r.bpm = song.header.bpm;
                    r.strips = (int)song.strips.size();
                    for (const auto &c : song.clips) r.lengthBeats = std::max(r.lengthBeats, c.at + clipLengthBeats(song, c));
                }
                mRecentCards.push_back(r);
            }
            mRecentsStale = false;
        }
        mModel.recents = mRecentCards;
        if (!mOpen)
        {
            mModel.screen = "home";
            return;
        }
        const Project &p = mProject;
        mModel.screen = "project";
        mModel.dirty = dirty;
        mModel.projectPath = mPath;
        mModel.projectName = p.header.name;
        mModel.bpm = p.header.bpm;
        mModel.sig = p.header.sig;
        mModel.sampleRate = p.header.sampleRate;
        mModel.masterGain = p.header.masterGain;
        mModel.masterOut = p.header.masterOut;

        const auto silent = silentStrips(p);
        const auto order = p.stripsInOrder();
        for (const Mixer *m : p.mixersInOrder())
        {
            MixerModel mm;
            mm.id = m->id;
            mm.name = m->name;
            mm.order = m->order;
            for (const Strip *s : order)
                if (p.mixerOf(*s) == m) mm.strips.push_back(s->id);
            mModel.mixers.push_back(mm);
        }
        for (const Strip *s : order)
        {
            StripModel sm;
            sm.id = s->id;
            sm.name = s->name;
            sm.kind = s->kind;
            sm.mixer = p.mixerOf(*s) ? p.mixerOf(*s)->id : std::string();
            sm.out = s->out.empty() ? "master" : s->out;
            sm.order = s->order;
            sm.colour = s->colour;
            sm.gain = s->gain;
            sm.pan = s->pan;
            sm.mute = s->mute;
            sm.solo = s->solo;
            sm.audible = !silent.count(s->id);
            for (const auto &sd : p.sends)
                if (sd.from == s->id) sm.sends.push_back(SendModel{sd.id, sd.to, sd.gain, sd.pre});
            if (const Rack *r = p.rack(s->id))
                for (const auto &d : r->devices) sm.devices.push_back(deviceModel(d));
            // fed by (R-MIX-8)
            std::set<std::string> lanes, from;
            for (const auto &c : p.clips)
                if (c.track == s->id)
                {
                    ++sm.clipCount;
                    lanes.insert(c.lane.empty() ? std::string("(own row)") : c.lane);
                }
            for (const auto &o : p.strips)
                if (o.out == s->id) from.insert(o.id);
            for (const auto &sd : p.sends)
                if (sd.to == s->id) from.insert(sd.from);
            sm.fromLanes.assign(lanes.begin(), lanes.end());
            sm.fromStrips.assign(from.begin(), from.end());
            mModel.strips.push_back(sm);
        }
        if (const Rack *r = p.rack("master"))
            for (const auto &d : r->devices) mModel.masterDevices.push_back(deviceModel(d));

        std::vector<const Lane *> lanes;
        for (const auto &l : p.lanes) lanes.push_back(&l);
        std::stable_sort(lanes.begin(), lanes.end(), [](const Lane *a, const Lane *b) { return a->order < b->order; });
        for (const Lane *l : lanes) mModel.lanes.push_back(LaneModel{l->id, l->name, l->order, l->colour});

        std::map<std::string, int> uses;
        for (const auto &c : p.clips)
            if (!c.isAudio()) ++uses[c.pattern];
        for (const auto &c : p.clips)
        {
            ClipModel cm;
            cm.id = c.id;
            cm.name = c.name;
            cm.track = c.track;
            cm.lane = c.lane;
            cm.kind = c.isAudio() ? "audio" : "note";
            cm.src = c.src;
            cm.pattern = c.pattern;
            cm.at = c.at;
            cm.length = clipLengthBeats(p, c);
            cm.in = c.in;
            cm.out = c.out;
            cm.gain = c.gain;
            cm.fadeIn = c.fadeIn;
            cm.fadeOut = c.fadeOut;
            cm.loop = c.loop;
            if (c.isAudio())
            {
                const std::string path = resolvePath(c.src);
                cm.offline = mOffline.count(path) || (!mPcm.count(path) && !std::ifstream(path).good());
            }
            else cm.linked = uses[c.pattern];
            mModel.lengthBeats = std::max(mModel.lengthBeats, cm.at + cm.length);
            mModel.clips.push_back(cm);
        }
        for (const auto &pt : p.patterns)
        {
            PatternModel pm;
            pm.id = pt.id;
            pm.name = pt.name;
            pm.length = pt.length;
            pm.clips = uses[pt.id];
            for (const auto &n : pt.notes) pm.notes.push_back(NoteModel{n.pitch, n.vel, n.at, n.length});
            mModel.patterns.push_back(pm);
        }
        for (const auto &po : p.ports) mModel.ports.push_back(PortModel{po.id, po.name, po.dir, po.channels});
    }

    std::vector<std::string> SolarisService::audit() const
    {
        // R-MIX-10: what is unused, unreachable, silent by accident, or broken — each one line.
        std::vector<std::string> out;
        const Project &p = mProject;
        const auto silent = silentStrips(p);
        auto label = [](const Strip &s) { return s.id + " (" + s.name + ")"; };

        // reaches an output: a port, or the master while the master feeds a port
        std::map<std::string, bool> memo;
        std::function<bool(const std::string &)> reaches = [&](const std::string &id) -> bool {
            auto it = memo.find(id);
            if (it != memo.end()) return it->second;
            memo[id] = false;
            const Strip *s = p.strip(id);
            if (!s) return false;
            std::vector<std::string> targets = {s->out};
            for (const auto &sd : p.sends)
                if (sd.from == id) targets.push_back(sd.to);
            bool ok = false;
            for (const auto &t : targets)
            {
                if (t.empty() || t == "master") ok = ok || !p.header.masterOut.empty();
                else if (p.port(t)) ok = true;
                else ok = ok || reaches(t);
            }
            return memo[id] = ok;
        };

        std::map<std::string, int> clipsOn, inputs;
        for (const auto &c : p.clips) ++clipsOn[c.track];
        for (const auto &s : p.strips)
            if (p.strip(s.out)) ++inputs[s.out];
        for (const auto &sd : p.sends)
            if (p.strip(sd.to)) ++inputs[sd.to];

        for (const Strip *s : p.stripsInOrder())
        {
            if (s->kind != "bus" && clipsOn[s->id] == 0) out.push_back("unused: " + label(*s) + " has no clips");
            if (!reaches(s->id)) out.push_back("unreachable: " + label(*s) + " reaches no output port");
            if (s->kind == "bus" && inputs[s->id] == 1) out.push_back("single input: bus " + label(*s) + " is fed by one strip only");
            if (s->kind == "bus" && inputs[s->id] == 0) out.push_back("empty bus: " + label(*s) + " is fed by nothing");
        }
        for (const auto &c : p.clips)
        {
            const Strip *s = p.strip(c.track);
            if (s && silent.count(s->id)) out.push_back("silent: clip " + c.id + " plays through " + label(*s) + (s->mute ? ", which is muted" : ", silenced by a solo"));
            if (c.isAudio())
            {
                const std::string path = resolvePath(c.src);
                if (mOffline.count(path) || (!mPcm.count(path) && !std::ifstream(path).good()))
                    out.push_back("offline: clip " + c.id + " — " + c.src + " cannot be read");
            }
        }
        for (const auto &r : p.racks)
            for (const auto &d : r.devices)
            {
                const DeviceType *t = DeviceRegistry::find(d.type);
                if (!t) { out.push_back("unknown device: " + d.id + " is `" + d.type + "`, which this build does not have"); continue; }
                for (const auto &kv : d.params)
                    if (t->paramIndex(kv.first) < 0) out.push_back("unknown parameter: " + d.id + " (" + d.type + ") has no `" + kv.first + "`");
            }
        if (p.header.masterOut.empty()) out.push_back("unreachable: the master feeds no port");
        for (const auto &pk : mLastRenderPeaks) out.push_back(pk);
        return out;
    }

    std::string SolarisService::matrixText(bool json) const
    {
        // R-MIX-9: rows = strips in processing order; columns = later strips, master, output ports.
        const Project &p = mProject;
        const auto order = p.stripsInOrder();
        std::vector<std::string> cols;
        for (const Strip *s : order)
            if (s->kind == "bus") cols.push_back(s->id);
        cols.push_back("master");
        for (const auto &po : p.ports)
            if (po.dir == "out") cols.push_back(po.id);

        auto cell = [&](const Strip &s, const std::string &col) {
            std::string v;
            const std::string out = s.out.empty() ? "master" : s.out;
            if (out == col) v = "●";
            for (const auto &sd : p.sends)
                if (sd.from == s.id && sd.to == col)
                    v += (v.empty() ? "" : "+") + canonicalNumber(sd.gain) + (sd.pre ? "pre" : "");
            return v;
        };
        if (json)
        {
            Json rows = Json::array();
            for (const Strip *s : order)
            {
                Json cells = Json::object();
                for (const auto &c : cols)
                {
                    const std::string v = cell(*s, c);
                    if (!v.empty()) cells.set(c, v);
                }
                rows.push(Json::object().set("strip", s->id).set("name", s->name)
                              .set("mixer", p.mixerOf(*s) ? p.mixerOf(*s)->id : std::string()).set("cells", cells));
            }
            Json columns = Json::array();
            for (const auto &c : cols) columns.push(Json::string(c));
            return Json::object().set("columns", columns).set("rows", rows).dump();
        }
        std::string t = "strip";
        for (const auto &c : cols) t += "\t" + c;
        t += "\n";
        for (const Strip *s : order)
        {
            t += s->id;
            for (const auto &c : cols) t += "\t" + cell(*s, c);
            t += "\n";
        }
        return t;
    }
}
}
