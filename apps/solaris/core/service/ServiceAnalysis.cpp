// SolarisService — the analysis tools (R-ASSIST-1): an audio clip split into what sustains and what strikes, its
// melody turned into notes, the song's chords found from a clip or the whole mix. The listening is the DSP library's
// (src/analysis, REQ-analysis-1…4: offline, deterministic); this file hands it the sound — a clip's raw render, as
// `clip consolidate` makes it, or the song's mix — and puts what it heard back into the song as clips, a pattern and
// chords. Each command is ONE edit inside `dispatch`'s all-or-nothing.
#include "Compile.h"
#include "Engine.h"
#include "Format.h"
#include "SolarisService.h"
#include "WorkerPool.h"
#include "analysis/Chords.h"
#include "analysis/Separate.h"
#include "analysis/Transcribe.h"
#include <algorithm>
#include <cmath>

namespace arstro
{
namespace solaris
{
    using K = Command::Kind;

    namespace
    {
        constexpr double kEps = 0.5 / kPpq;

        /** The lanes `made`, in that order, right under `under` — renumbered as `lane move` does, and in its group
         *  when it has one (R-ARR-3). Nothing moves when `under` is no lane (a clip on none: they stay last). */
        void placeUnder(Project &p, const std::string &under, const std::vector<std::string> &made)
        {
            const Lane *u = p.lane(under);
            if (!u) return;
            const std::string group = u->group;
            std::vector<const Lane *> order;
            for (const auto &l : p.lanes) order.push_back(&l);
            std::stable_sort(order.begin(), order.end(), [](const Lane *a, const Lane *b) { return a->order < b->order; });
            std::vector<std::string> ids;
            for (const Lane *l : order)
                if (std::find(made.begin(), made.end(), l->id) == made.end()) ids.push_back(l->id);
            ids.insert(std::find(ids.begin(), ids.end(), under) + 1, made.begin(), made.end());
            for (size_t i = 0; i < ids.size(); ++i) p.lane(ids[i])->order = (int)i;
            for (const auto &m : made) p.lane(m)->group = group;
            gatherLaneGroups(p);
        }

        std::vector<float> monoOf(const std::vector<std::vector<float>> &ch)
        {
            std::vector<float> m(ch.empty() ? 0 : ch[0].size());
            for (size_t i = 0; i < m.size(); ++i) m[i] = 0.5f * (ch[0][i] + ch[ch.size() > 1 ? 1 : 0][i]);
            return m;
        }
    }

    bool SolarisService::renderSongMix(std::vector<std::vector<float>> &ch, std::string &err)
    {
        // the whole song as a render hears it — every strip, its devices, its plugins (their own instances), the
        // master — from its start to its end (no tail: a chord is what plays inside the song)
        CompileResult cr = compile(mProject, [this](const std::string &src) { return pcmFor(src); }, [this](const std::string &s, std::string &t, std::string &w) { return textFor(s, t, w); });
        const auto plugins = attachPlugins(cr, mProject, PluginUse::Render);
        engine::Engine eng;
        WorkerPool pool(WorkerPool::threadsFor(mSettings.threads));
        eng.setParallel(&pool);
        if (!eng.build(cr.graph, err)) return false;
        eng.seek(0);
        eng.captureMaster(true);
        const long long lag = eng.outputLatency(), end = cr.graph.end + lag;
        ch.assign(2, {});
        engine::PortBuffers pb;
        for (long long pos = 0; pos < end;)
        {
            const int n = (int)std::min<long long>(4096, end - pos);
            eng.render(n, pb);
            const auto &m = eng.capturedMaster();
            for (int k = 0; k < 2; ++k) ch[(size_t)k].insert(ch[(size_t)k].end(), m[(size_t)k].begin(), m[(size_t)k].end());
            pos += n;
        }
        for (auto &x : ch) x.erase(x.begin(), x.begin() + (long)std::min<long long>(lag, (long long)x.size()));
        return true;
    }

    bool SolarisService::analysisCommand(const Command &c, std::string &err)
    {
        Project &p = mProject;
        const double rate = p.header.sampleRate;
        // an audio clip whose sound is there, and what it plays, raw
        auto clipSound = [&](const std::string &id, const std::string &verb, const Clip *&cl, std::vector<std::vector<float>> &ch) {
            cl = p.clip(id);
            if (!cl) { err = "no clip `" + id + "`"; return false; }
            if (!cl->isAudio()) { err = id + " is a note clip — its notes are its pattern's; `" + verb + "` listens to an audio clip"; return false; }
            if (!pcmFor(cl->src)) { err = id + "'s file (" + cl->src + ") is offline — nothing to listen to"; return false; }
            return renderClipsRaw({cl->id}, cl->track, cl->at, cl->at + clipLengthBeats(p, *cl), ch, err);
        };
        const auto nameOf = [](const Clip &cl) { return cl.name.empty() ? cl.id : cl.name; };

        if (c.kind == K::ClipSeparate)
        {
            const Clip *cl = nullptr;
            std::vector<std::vector<float>> ch;
            if (!clipSound(c.arg(0), "clip separate", cl, ch)) return false;
            const Clip orig = *cl; // a copy: `clip add` grows the clip list
            const Separation sep = separateHarmonicPercussive(ch, rate);
            const std::string name = nameOf(orig);
            std::vector<std::string> made, lanes;
            for (int half = 0; half < 2; ++half)
            {
                const std::string what = half == 0 ? "harmonic" : "percussive";
                const std::string file = writeBounce(name + " " + what, half == 0 ? sep.harmonic : sep.percussive, err, "Separated");
                if (file.empty()) return false;
                Command add;
                add.kind = K::ClipAdd;
                add.flags = {{"src", file}, {"at", canonicalBeats(orig.at)}, {"lane", "new"}}; // R-MIX-2: a new file, its own strip
                if (!clipCommand(add, err)) return false;
                Clip &nc = p.clips.back();
                nc.name = name + " " + what;
                if (Strip *s = p.strip(nc.track)) s->name = nc.name;
                if (Lane *l = p.lane(nc.lane)) l->name = nc.name;
                made.push_back(nc.id);
                lanes.push_back(nc.lane);
            }
            placeUnder(p, orig.lane, lanes);
            p.clips.erase(std::remove_if(p.clips.begin(), p.clips.end(), [&](const Clip &x) { return x.id == orig.id; }), p.clips.end());
            changed("clip.deleted", orig.id);
            mOutput = made[0] + "\n"; // the harmonic clip; `made:` names the percussive one and the strips and lanes
            return true;
        }
        if (c.kind == K::ClipToMidi)
        {
            const Clip *cl = nullptr;
            std::vector<std::vector<float>> ch;
            if (!clipSound(c.arg(0), "clip to-midi", cl, ch)) return false;
            const Clip orig = *cl;
            const std::vector<float> mono = monoOf(ch);
            const auto notes = transcribe(mono.data(), (long long)mono.size(), rate);
            if (notes.empty())
            {
                err = "no note heard in " + orig.id + " — `clip to-midi` hears one melody, one note at a time, above −50 dBFS (not chords, not noise)";
                return false;
            }
            const double len = clipLengthBeats(p, orig), s0 = sampleOfBeat(orig.at);
            Command add;
            add.kind = K::ClipAdd;
            add.flags = {{"instrument", c.flag("instrument", "synth")}, {"lane", "new"}, {"at", canonicalBeats(orig.at)}, {"length", canonicalBeats(toTick(len))}};
            if (!clipCommand(add, err)) return false;
            Clip &nc = p.clips.back();
            Pattern *pt = p.pattern(nc.pattern);
            pt->name = nameOf(orig) + " notes";
            pt->length = toTick(len);
            nc.name = pt->name;
            // the notes, from seconds in the clip to beats in it — through the song's tempo map at its place
            auto beatIn = [&](double seconds) { return toTick(beatOfSample(s0 + seconds * rate) - orig.at); };
            for (const auto &tn : notes)
            {
                Note n;
                n.at = beatIn(tn.start);
                n.length = std::max(1.0 / kPpq, toTick(beatIn(tn.end) - n.at));
                n.pitch = tn.pitch;
                n.vel = tn.velocity;
                if (n.at < pt->length - kEps) pt->notes.push_back(n);
            }
            placeUnder(p, orig.lane, {nc.lane});
            changed("pattern.changed", pt->id);
            mOutput = nc.id + "\n";
            return true;
        }
        if (c.kind == K::ChordsClear)
        {
            if (p.chords.empty()) { err = "the song has no chords — nothing to clear"; return false; }
            for (const auto &x : p.chords) changed("chord.deleted", x.id);
            mOutput = std::to_string(p.chords.size()) + (p.chords.size() == 1 ? " chord" : " chords") + " taken out\n";
            p.chords.clear();
            return true;
        }

        // K::ChordsDetect — a clip's sound, or the song's mix; the span it covers, in beats
        std::vector<std::vector<float>> ch;
        double from = 0, to = 0;
        std::string what;
        if (c.args.empty())
        {
            if (!renderSongMix(ch, err)) return false;
            if (ch[0].empty()) { err = "the song plays nothing yet — no chord to hear"; return false; }
            to = toTick(beatOfSample((double)ch[0].size()));
            what = "the song";
        }
        else
        {
            const Clip *cl = nullptr;
            if (!clipSound(c.arg(0), "chords detect", cl, ch)) return false;
            from = cl->at;
            to = cl->at + clipLengthBeats(p, *cl);
            what = cl->id;
        }
        // the beats of the tempo map inside the span, in seconds from its start — its edges too
        const double s0 = sampleOfBeat(from);
        std::vector<double> beats{0.0};
        for (double b = std::floor(from + kEps) + 1.0; b < to - kEps; b += 1.0) beats.push_back((sampleOfBeat(b) - s0) / rate);
        beats.push_back((double)ch[0].size() / rate);
        const std::vector<float> mono = monoOf(ch);
        const auto spans = detectChords(mono.data(), (long long)mono.size(), rate, beats);

        // what was in the span goes; a chord across an edge keeps what lies outside it
        bool touched = false;
        std::vector<Chord> kept;
        for (const auto &x : p.chords)
        {
            const double e = x.at + x.length;
            if (x.at >= to - kEps || e <= from + kEps) { kept.push_back(x); continue; }
            bool changedOne = false;
            if (x.at < from - kEps)
            {
                Chord before = x;
                before.length = toTick(from - x.at);
                kept.push_back(before);
                changedOne = true;
            }
            if (e > to + kEps)
            {
                Chord after = x;
                if (changedOne) { after.id = p.nextId("cd"); after.remarks = Remarks(); changed("chord.added", after.id); }
                after.at = to;
                after.length = toTick(e - to);
                kept.push_back(after);
                changedOne = true;
            }
            changed(changedOne ? "chord.changed" : "chord.deleted", x.id);
            touched = true;
        }
        p.chords = kept;
        std::string names;
        size_t n = 0;
        for (const auto &s : spans)
        {
            if (s.name == "N") continue;
            Chord x;
            x.at = toTick(beatOfSample(s0 + s.start * rate));
            x.length = toTick(std::min(to, beatOfSample(s0 + s.end * rate)) - x.at);
            if (x.length <= 0) continue;
            x.id = p.nextId("cd");
            x.name = s.name;
            p.chords.push_back(x);
            changed("chord.added", x.id);
            names += " " + x.name;
            ++n;
        }
        if (n == 0 && !touched) { err = "no chord heard in " + what + " (beats " + canonicalBeats(from) + " … " + canonicalBeats(to) + ")"; return false; }
        std::stable_sort(p.chords.begin(), p.chords.end(), [](const Chord &a, const Chord &b) { return a.at < b.at; });
        mPending.push_back(Event(Event::Kind::ChordsDetected).with("n", std::to_string(n)).with("from", canonicalBeats(from)).with("to", canonicalBeats(to)));
        mOutput = std::to_string(n) + (n == 1 ? " chord" : " chords") + " in " + what + ", beats " + canonicalBeats(from) + " … " + canonicalBeats(to) + ":" +
                  (names.empty() ? std::string(" none") : names) + "\n";
        return true;
    }
}
}
