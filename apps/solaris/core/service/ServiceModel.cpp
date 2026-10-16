// SolarisService — what a front end can see: the AppModel refresh, `audit` (R-MIX-10) and the
// matrix (R-MIX-9). Everything here is COMPUTED from the project, never stored in it: "fed by",
// audibility under solos, resolved clip lengths, linked counts — a second front end reading the
// model gets the same answers without re-deriving them (arstro.rule §1).
#include "Bindings.h"
#include "Compile.h"
#include "Format.h"
#include "Json.h"
#include "Keymap.h"
#include "Notation.h"
#include "RegistryModel.h"
#include "SolarisService.h"
#include "device/Device.h"
#include <algorithm>
#include <cstdlib>
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
        /** `t`: the registry's type, or a plugin's from its connection (R-VST-6); null when this build has neither. */
        DeviceModel deviceModel(const DeviceNode &d, const DeviceType *t)
        {
            DeviceModel m;
            m.id = d.id;
            m.type = d.type;
            m.bypass = d.bypass;
            m.sample = d.sample;
            m.known = t != nullptr;
            m.takesSample = t && t->takesSample;
            m.takesDefinition = t && t->takesDefinition; // R-INST-9
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
            // the registry's description, at the values the song stores (RegistryModel: the ONE mapping, R-VST-7)
            std::vector<double> values;
            for (const auto &spec : t->params)
            {
                double v = spec.def;
                for (const auto &kv : d.params)
                    if (kv.first == spec.name) parseParam(spec, kv.second, v);
                values.push_back(v);
            }
            DeviceModel full = deviceModelOf(*t, values);
            full.type = m.type;
            full.id = m.id;
            full.bypass = m.bypass;
            full.sample = m.sample;
            full.known = true;
            return full;
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
        const AuditionModel audition = mModel.audition; // the machine's preview: no song decides it
        const LoudnessModel loudness = mModel.loudness; // R-METER-1: measured, kept after a stop
        std::map<std::string, double> wasLive;          // while playing, `pump` owns bindings[].live: kept
        if (mPlayer && mPlayer->running())
            for (const auto &b : mModel.bindings) wasLive[b.address] = b.live;
        mModel = AppModel();
        prunePlugins(); // R-VST-6: a removed plugin's process ends (once no engine plays it)
        mModel.transport = transport;
        mModel.audition = audition;
        mModel.loudness = loudness;
        mModel.armed = mArmed;            // R-MIDI-2: the session's
        mModel.midiDevices = mMidiPorts;
        mModel.midiMessages = mMidiCount->load(std::memory_order_relaxed);
        mModel.transport.masterClip = mMasterClip;
        mModel.transport.loopFrom = mLoopFrom;
        mModel.transport.loopTo = mLoopTo;
        mModel.transport.punchFrom = mPunchFrom; // R-REC-1
        mModel.transport.punchTo = mPunchTo;
        mModel.transport.loopClip = mClipLoop;
        mModel.transport.cue = mCue;
        if (!mModel.transport.playing) mModel.transport.position = mPosition;
        tempoAt(mModel.transport.position, mModel.transport.tempo, mModel.transport.tempoId); // R-TEMPO-1
        if (!mPresetsScanned) scanPresets(); // R-PRESET-1: the machine's — on Home too
        mModel.presets = mPresets;
        if (!mTemplatesScanned) scanTemplates(); // R-PRESET-2
        mModel.templates = mTemplates;
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
        mModel.settings.metronome = mSettings.metronome;
        mModel.settings.metronomeLevel = mSettings.metronomeLevel;
        mModel.settings.auditionLevel = mSettings.auditionLevel;
        mModel.settings.newBpm = mSettings.newBpm;
        mModel.settings.newSig = mSettings.newSig;
        mModel.settings.reducedMotion = mSettings.reducedMotion;
        mModel.settings.showIds = mSettings.showIds;
        mModel.settings.autosave = mSettings.autosave;
        mModel.settings.driver = mSettings.driver;
        mModel.settings.midiInputs = mSettings.midiInputs;
        mModel.settings.recordQuantize = mSettings.recordQuantize;
        mModel.settings.countIn = mSettings.countIn;
        mModel.recovery = mRecovery;
        mModel.recoveryAge = mRecoveryAge;
        for (const auto &a : keyActions())
        {
            KeyActionModel k{a.id, a.area, a.label, keysOf(a, mSettings.keys), {}};
            keysOf(a, {}).swap(k.defaults);
            mModel.settings.keys.push_back(std::move(k));
        }
        for (const auto &m : mSettings.maps) mModel.settings.mappings.push_back(MapModel{m.first, m.second, mapInert(m.second)}); // R-CTRL-1
        mModel.mapLearning = mMapLearning;
        mModel.settings.surface = mSettings.surface; // R-CTRL-2
        mModel.settings.surfacePort = mSettings.surfacePort;
        mModel.settings.threads = mSettings.threads; // R-PERF-2
        mModel.surface.protocol = mSettings.surface;
        mModel.surface.connected = mSurfaceIn != nullptr;
        mModel.surface.why = mSurfaceWhy;
        mModel.surface.bank = mSurfaceBank;
        mModel.devices = mDevices;
        mModel.browser = mBrowser;
        if (!mPluginsLoaded) loadPlugins(); // R-VST-6: the machine's
        mModel.plugins = mPlugins;
        if (!mLibraryLoaded) loadLibrary(); // R-PRESET-3: the machine's — on Home too
        mModel.library.clear();
        for (const auto &e : mLibrary)
        {
            LibraryEntry le{e.first, {}, false};
            for (const auto &t : e.second)
                if (t == "favourite") le.favourite = true;
                else le.tags.push_back(t);
            mModel.library.push_back(le);
        }
        for (const auto &t : DeviceRegistry::types())
        {
            DeviceTypeModel tm{t.name, t.label, t.kind == DeviceKind::Instrument ? "instrument" : "effect", {}};
            for (const auto &nn : t.noteNames) tm.noteNames.push_back(NoteNameModel{nn.first, nn.second});
            tm.takesSample = t.takesSample;
            tm.takesDefinition = t.takesDefinition; // R-INST-9
            mModel.deviceTypes.push_back(tm);
        }
        for (const auto &q : chordQualities()) mModel.chords.push_back(VocabModel{q.names[0], q.label}); // R-ROLL-11
        for (const auto &k : scaleKinds()) mModel.scales.push_back(VocabModel{k.names[0], k.label});
        for (const auto &g : grooves()) mModel.grooves.push_back(VocabModel{g.name, g.label});
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
                    double age = 0;
                    r.recovery = recoveryOf(path, age); // R-SAFE-2
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
            // its own, else one from its id's number: a default that never changes when other strips come and go
            // (one by position would recolour every later strip's clips the moment one is deleted)
            sm.colour = s->colour >= 0 ? s->colour : std::max(0, std::atoi(s->id.c_str() + std::min(s->id.size(), s->id.find('_') + 1)) - 1);
            sm.gain = s->gain;
            sm.pan = s->pan;
            sm.mute = s->mute;
            sm.solo = s->solo;
            sm.audible = !silent.count(s->id);
            sm.clip = mClipLatch.count(s->id) > 0; // R-METER-2: a latch outlives a stop and an edit
            for (const auto &sd : p.sends)
                if (sd.from == s->id)
                {
                    sm.sends.push_back(SendModel{sd.id, sd.to, sd.gain, sd.pre});
                    sm.sends.back().sidechain = sd.sidechain;
                }
            if (const Rack *r = p.rack(s->id))
                for (const auto &d : r->devices) sm.devices.push_back(pluginFacts(d, deviceModel(d, typeOf(d))));
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
            sm.targets = targetsOf(p, *s);
            sm.keyTargets = keyTargetsOf(p, *s);
            sm.input = s->input; // R-REC-1
            sm.frozen = s->frozen; // R-PERF-3
            sm.monitor = s->monitor;
            mModel.strips.push_back(sm);
        }
        if (const Rack *r = p.rack("master"))
            for (const auto &d : r->devices) mModel.masterDevices.push_back(pluginFacts(d, deviceModel(d, typeOf(d))));

        std::vector<const Lane *> lanes;
        for (const auto &l : p.lanes) lanes.push_back(&l);
        std::stable_sort(lanes.begin(), lanes.end(), [](const Lane *a, const Lane *b) { return a->order < b->order; });
        for (const Lane *l : lanes) mModel.lanes.push_back(LaneModel{l->id, laneTitle(p, *l), l->order, l->colour, l->track, !l->name.empty() || l->track.empty(), l->group});
        for (const Lane *l : lanes) // R-ARR-3: in the order of their first lanes
        {
            const LaneGroup *g = l->group.empty() ? nullptr : p.group(l->group);
            if (!g) continue;
            auto it = std::find_if(mModel.groups.begin(), mModel.groups.end(), [&](const LaneGroupModel &x) { return x.id == g->id; });
            if (it == mModel.groups.end()) { mModel.groups.push_back(LaneGroupModel{g->id, g->name, g->folded, {}}); it = mModel.groups.end() - 1; }
            it->lanes.push_back(l->id);
        }

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
            cm.reverse = c.reverse;
            cm.warp = c.warp; // R-WARP-1
            cm.bpm = c.bpm;
            cm.pitch = c.pitch;
            heardFades(p, c, cm.fadeInHeard, cm.fadeOutHeard); // R-EDIT-1: the crossfade, decided once for every front end
            if (c.isAudio())
            {
                const std::string path = resolvePath(c.src);
                cm.offline = mOffline.count(path) || (!mPcm.count(path) && !std::ifstream(path).good());
            }
            else cm.linked = uses[c.pattern];
            for (const auto &t : c.takes) cm.takes.push_back(t.src); // R-REC-2
            for (const auto &x : c.comp) cm.comp.push_back(ClipModel::CompModel{x.take, x.from, x.to});
            if (!c.takes.empty()) // R-REC-2: the runs the compile plays — computed here once, so a front end only draws them
            {
                const double end = c.at + clipLengthBeats(p, c);
                std::vector<double> cuts{c.at, end};
                for (const auto &x : c.comp)
                    for (double b : {x.from, x.to})
                        if (b > c.at + 1e-9 && b < end - 1e-9) cuts.push_back(b);
                std::sort(cuts.begin(), cuts.end());
                for (size_t k = 0; k + 1 < cuts.size(); ++k)
                {
                    if (cuts[k + 1] - cuts[k] < 1e-9) continue;
                    const int t = c.takeAt(0.5 * (cuts[k] + cuts[k + 1]));
                    if (!cm.plays.empty() && cm.plays.back().take == t) cm.plays.back().to = cuts[k + 1];
                    else cm.plays.push_back(ClipModel::CompModel{t, cuts[k], cuts[k + 1]});
                }
            }
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
            for (const auto &c : p.clips)
                if (c.pattern == pt.id)
                {
                    pm.strip = c.track;
                    if (const Rack *rk = p.rack(c.track))
                        if (!rk->devices.empty()) pm.instrument = rk->devices.front().type;
                    break;
                }
            for (const auto &c : p.clips)
                if (c.pattern == pt.id)
                {
                    if (std::find(pm.strips.begin(), pm.strips.end(), c.track) == pm.strips.end()) pm.strips.push_back(c.track);
                    pm.lastStrip = c.track; // the clips are in the order they were made: the last is the newest
                }
            for (const auto &n : pt.notes) pm.notes.push_back(NoteModel{n.pitch, n.vel, n.at, n.length, n.chance});
            std::string canon, why;
            if (!pt.scale.empty() && parseScale(pt.scale, canon, pm.scaleMask, why)) pm.scale = canon; // R-ROLL-11
            for (const auto &x : pt.expr) pm.expr.push_back(ExprPointModel{x.lane, x.at, x.value}); // R-MIDI-3
            mModel.patterns.push_back(pm);
        }
        {
            const double end = songEndBeats(); // R-ARR-1: by time, each with its section's end
            for (const auto &mk : p.markers) mModel.markers.push_back(MarkerModel{mk.id, mk.name, mk.note, mk.at, mk.length, p.sectionEnd(mk, end)});
            std::stable_sort(mModel.markers.begin(), mModel.markers.end(), [](const MarkerModel &a, const MarkerModel &b) { return a.at < b.at; });
        }
        if (!p.tempos.empty()) // R-TEMPO-1: each change, and where it is heard
        {
            const auto tm = tempoMapOf(p);
            for (const auto &t : p.tempos) mModel.tempos.push_back(TempoModel{t.id, t.at, t.bpm, t.ramp, tm.sampleAt(t.at) / p.header.sampleRate});
        }
        for (const auto &po : p.ports) mModel.ports.push_back(PortModel{po.id, po.name, po.dir, po.channels});
        mModel.matrix.columns = matrixColumnsOf(p); // R-MIX-9: the same columns `matrix print` prints

        // formulas and automation (R-AUTO-9), every one checked, the inert ones saying why
        std::vector<std::string> problems;
        const auto ordered = orderBindings(p, problems);
        std::map<std::string, const OrderedBinding *> okAt;
        for (const auto &ob : ordered) okAt[ob.binding->address] = &ob;
        for (const auto &b : p.bindings)
        {
            BindingModel bm;
            bm.address = b.address;
            bm.formula = b.formula;
            const auto it = okAt.find(b.address);
            if (it != okAt.end()) bm.reads = it->second->reads;
            else
            {
                bm.ok = false;
                for (const auto &x : problems)
                    if (x.compare(0, b.address.size() + 1, b.address + ":") == 0) { bm.problem = x.substr(b.address.size() + 2); break; }
                ParsedFormula f;
                std::string why;
                if (parseFormula(b.formula, f, why)) bm.reads = f.names;
            }
            mModel.bindings.push_back(bm);
        }
        auto formulaOf = [&](const std::string &address) {
            const Binding *b = p.binding(address);
            return b ? b->formula : std::string();
        };
        mModel.masterGainFormula = formulaOf("project.masterGain");
        // R-HIST: the tree, and what undo and redo would do from where the song is now
        for (const auto &n : mHist) mModel.history.push_back(HistoryNodeModel{n.first, n.second.parent, n.second.label});
        mModel.historyCurrent = mHistCur;
        {
            const auto cur = mHist.find(mHistCur);
            int up = 0, down = 0;
            for (auto it = cur; it != mHist.end() && it->second.parent >= 0; it = mHist.find(it->second.parent)) ++up;
            for (auto it = cur; it != mHist.end() && !it->second.kids.empty();
                 it = mHist.find(*std::max_element(it->second.kids.begin(), it->second.kids.end()))) ++down;
            mModel.undoDepth = up;
            mModel.redoDepth = down;
            mModel.undoLabel = cur != mHist.end() && cur->second.parent >= 0 ? cur->second.label : std::string();
            mModel.redoLabel = cur != mHist.end() && !cur->second.kids.empty()
                                   ? mHist.at(*std::max_element(cur->second.kids.begin(), cur->second.kids.end())).label
                                   : std::string();
        }
        auto fillDevice = [&](DeviceModel &d) {
            for (auto &pm : d.params) pm.formula = formulaOf(d.id + "." + pm.name);
            const auto lc = mLastChanged.find(d.id);
            if (lc != mLastChanged.end()) d.lastChanged = lc->second;
        };
        for (auto &sm : mModel.strips)
        {
            sm.gainFormula = formulaOf(sm.id + ".gain");
            sm.panFormula = formulaOf(sm.id + ".pan");
            for (auto &sd : sm.sends) sd.gainFormula = formulaOf(sd.id + ".gain");
            for (auto &d : sm.devices) fillDevice(d);
        }
        for (auto &d : mModel.masterDevices) fillDevice(d);
        mNowCurves.clear();
        mNowSpb = 60.0 / p.header.bpm * p.header.sampleRate;
        for (const auto &a : p.automations)
        {
            AutomationModel am;
            am.id = a.id;
            am.name = a.name;
            am.unit = a.unit;
            am.from = a.from;
            am.min = a.min;
            am.max = a.max;
            am.loop = a.loop;
            am.at = a.at;
            am.length = a.length;
            for (const auto &pt : a.points)
                am.points.push_back(AutoPointModel{pt.at, pt.value, pt.shape, pt.speedIn, pt.inflIn, pt.speedOut, pt.inflOut});
            for (const auto &bm : mModel.bindings)
                if (std::find(bm.reads.begin(), bm.reads.end(), a.id) != bm.reads.end()) am.usedBy.push_back(bm.address);
            mNowCurves.push_back(compileCurve(a, mNowSpb)); // the engine's curve: `now` is what the audio plays there
            mModel.automations.push_back(am);
        }
        // R-MIX-16: what each formula evaluates to where the transport is. While playing, the values the
        // engine evaluated for what is heard (`pump`) are kept; a binding new since is evaluated here first.
        bool fresh = false;
        for (const auto &bm : mModel.bindings) fresh |= !wasLive.count(bm.address);
        if (fresh) evaluateLive(mModel.transport.position);
        for (auto &bm : mModel.bindings)
        {
            const auto it = wasLive.find(bm.address);
            if (it != wasLive.end()) bm.live = it->second;
        }
        refreshNow();
    }

    void SolarisService::evaluateLive(double beat)
    {
        if (!mOpen || mModel.bindings.empty()) return;
        // the engine's evaluator over the engine's compiled data, as `eval` does: a script and the screen agree
        const CompileResult cr = compile(mProject, [](const std::string &) { return std::shared_ptr<const engine::Pcm>(); });
        const engine::MixGraph &g = cr.graph;
        std::vector<double> vars(engine::kClockSlots + g.curves.size() + g.binds.size(), 0.0), values(g.binds.size(), 0.0);
        for (size_t b = 0; b < g.binds.size(); ++b) values[b] = g.binds[b].own;
        // R-TEMPO-1: the beat's sample through the map (the evaluator reads the beat back through it)
        engine::evaluateBinds(g.clock, g.curves, g.binds, std::llround(g.clock.map.isConstant() ? beat * g.clock.samplesPerBeat : g.clock.map.sampleAt(beat)), vars.data(),
                              values.data());
        for (auto &bm : mModel.bindings)
        {
            const auto it = std::find(cr.bindAddresses.begin(), cr.bindAddresses.end(), bm.address);
            if (it != cr.bindAddresses.end()) { bm.live = values[(size_t)(it - cr.bindAddresses.begin())]; continue; }
            AddressSpec own; // inert: its own value plays
            std::string why;
            bm.live = describeAddress(mProject, bm.address, own, why) ? own.value : 0.0;
        }
    }

    void SolarisService::refreshNow()
    {
        // R-AUTO-11: each automation's value at the playhead — the heard position while playing
        const long long s = std::llround(mModel.transport.position * mNowSpb);
        for (size_t i = 0; i < mModel.automations.size() && i < mNowCurves.size(); ++i) mModel.automations[i].now = mNowCurves[i].valueAt(s);
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
                if (d.type == "vst3")
                {
                    // R-VST-6: a plugin that is not playing — an effect passes dry, an instrument is silent
                    const auto it = mPluginLive.find(d.id);
                    if (it == mPluginLive.end() || it->second->offline())
                        out.push_back("offline: " + d.id + " (" + (d.pluginName.empty() ? std::string("a plugin") : d.pluginName) + ") — " +
                                      (it == mPluginLive.end() ? std::string("not started") : it->second->why()) + " (device reload " + d.id + ")");
                    continue;
                }
                const DeviceType *t = DeviceRegistry::find(d.type);
                if (!t) { out.push_back("unknown device: " + d.id + " is `" + d.type + "`, which this build does not have"); continue; }
                for (const auto &kv : d.params)
                    if (t->paramIndex(kv.first) < 0) out.push_back("unknown parameter: " + d.id + " (" + d.type + ") has no `" + kv.first + "`");
            }
        if (p.header.masterOut.empty()) out.push_back("unreachable: the master feeds no port");
        // R-PERF-3: a frozen strip plays its file — what changes in its clips and devices is not heard until unfrozen
        for (const auto &s : p.strips)
            if (!s.frozen.empty())
                out.push_back("frozen: " + s.id + " (" + s.name + ") plays " + s.frozen + " — its clips' and devices' edits are not heard until `strip unfreeze " + s.id + "`");
        // R-CTRL-1: a MIDI control mapped to a number this song does not have moves nothing
        for (const auto &m : mSettings.maps)
            if (mapInert(m.second)) out.push_back("inert mapping: MIDI control " + m.first + " moves " + m.second + ", which this song does not have");
        for (const auto &pk : mLastRenderPeaks) out.push_back(pk);
        // R-EDM-8: a sampler with no sound, or one it cannot read, plays nothing
        for (const auto &rk : p.racks)
            for (const auto &d : rk.devices)
                if (const DeviceType *t = DeviceRegistry::find(d.type); t && t->takesSample)
                {
                    if (d.sample.empty())
                        out.push_back("silent: " + d.id + " (" + t->label + ") has no " + (t->takesDefinition ? "instrument — give it an .sfz" : "sample — give it one") +
                                      " (set " + d.id + ".sample=<file>)");
                    else if (mOffline.count(resolvePath(d.sample))) out.push_back("offline: " + d.id + "'s sample " + d.sample + " cannot be read");
                }
        // R-MIX-15: a key nothing listens to — no compressor on that strip with Sidechain on
        for (const auto &sd : p.sends)
        {
            if (!sd.sidechain) continue;
            bool heard = false;
            if (const Rack *rk = p.rack(sd.to))
                for (const auto &d : rk->devices)
                {
                    const DeviceType *t = DeviceRegistry::find(d.type);
                    if (!t || !t->takesKey || d.bypass) continue;
                    for (const auto &kv : d.params)
                        if (kv.first == "sidechain" && kv.second == "on") heard = true;
                }
            if (!heard) out.push_back("sidechain " + sd.id + " keys " + sd.to + ", where no compressor has Sidechain on — it ducks nothing");
        }
        // R-SVC-8: notes past a pattern's end play over its next repeat; an empty pattern plays nothing; a pattern no
        // clip plays is never heard
        for (const auto &pt : p.patterns)
        {
            size_t past = 0, clips = 0;
            for (const auto &n : pt.notes) past += n.at >= pt.length - 0.5 / kPpq ? 1 : 0;
            for (const auto &c : p.clips) clips += c.pattern == pt.id ? 1 : 0;
            const std::string label = pt.id + " (" + pt.name + ")";
            if (past)
                out.push_back("past its end: " + label + " has " + std::to_string(past) + " note(s) starting at or after beat " +
                              canonicalBeats(pt.length) + " — they play over its next repeat (set " + pt.id + ".length)");
            if (pt.notes.empty()) out.push_back("empty pattern: " + label + " has no notes" + (clips ? " — its clips play nothing" : ""));
            if (!clips) out.push_back("unused pattern: " + label + " — no clip plays it");
        }
        // R-AUTO: a formula nobody can read plays nothing; an automation nobody reads moves nothing
        {
            std::vector<std::string> problems;
            orderBindings(p, problems);
            for (const auto &x : problems) out.push_back("binding " + x + " — inert: its own value plays");
            for (const auto &a : p.automations)
                if (readersOf(a.id).empty()) out.push_back("automation " + a.id + " (" + a.name + ") moves nothing — no formula reads it");
        }
        return out;
    }

    std::string SolarisService::matrixText(bool json) const
    {
        // R-MIX-9: rows = strips in processing order; columns = the model's `matrix.columns` — every strip some
        // strip may or does reach, master, output ports — so a route to an instrument or a same-mixer strip shows
        const Project &p = mProject;
        const auto order = p.stripsInOrder();
        const std::vector<std::string> cols = matrixColumnsOf(p);

        auto cell = [&](const Strip &s, const std::string &col) {
            std::string v;
            const std::string out = s.out.empty() ? "master" : s.out;
            if (out == col) v = "●";
            for (const auto &sd : p.sends)
                if (sd.from == s.id && sd.to == col)
                    v += (v.empty() ? "" : "+") + canonicalNumber(sd.gain) + (sd.pre ? "pre" : "") + (sd.sidechain ? "key" : "");
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
