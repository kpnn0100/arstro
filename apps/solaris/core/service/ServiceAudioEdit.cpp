// SolarisService — audio editing on the lanes (R-EDIT-1): an audio clip cut in two, several joined into one by a raw
// render of what they play, one sliced into clips (every so many beats, or where the DSP library hears a sound begin)
// or into a Sampler kit that plays the same. Every edit here runs inside `dispatch`'s all-or-nothing; a slice is a cut
// repeated, so a cut's rules are a slice's.
#include "Compile.h"
#include "Engine.h"
#include "Format.h"
#include "SolarisService.h"
#include "device/Device.h"
#include "meter/Onsets.h"
#include "spectral/TimeStretch.h"
#include <algorithm>
#include <cmath>
#include <set>

namespace arstro
{
namespace solaris
{
    using K = Command::Kind;

    namespace
    {
        constexpr double kEps = 1e-9;

        /** The comp's ranges inside [from, to), each cut to it. */
        std::vector<CompRange> compWithin(const std::vector<CompRange> &comp, double from, double to)
        {
            std::vector<CompRange> out;
            for (auto r : comp)
            {
                r.from = std::max(r.from, from);
                r.to = std::min(r.to, to);
                if (r.to > r.from + kEps) out.push_back(r);
            }
            return out;
        }

        bool audioClip(const Project &p, const std::string &id, const Clip *&cl, const std::string &verb, std::string &err)
        {
            cl = p.clip(id);
            if (!cl) { err = "no clip `" + id + "`"; return false; }
            if (!cl->isAudio())
            {
                err = id + " is a note clip — its notes are its pattern's, shared by every clip of it (R-CLIP-3); " + verb + " takes audio clips";
                return false;
            }
            return true;
        }
    }

    std::shared_ptr<const engine::Pcm> SolarisService::stretched(const std::string &key, const std::function<std::shared_ptr<const engine::Pcm>(const std::string &)> &base,
                                                                 int rate)
    {
        // R-WARP-1: the span of its file, stretched by the ratio and moved by the semitones the key names (REQ-stretch-1…3)
        double ratio = 1, semis = 0, in = 0, out = 0;
        std::string src;
        if (!parseStretchKey(key, ratio, semis, in, out, src)) return nullptr;
        auto pcm = base(src);
        if (!pcm || pcm->frames <= 0) return nullptr;
        const long long i0 = std::clamp<long long>(std::llround(in * rate), 0, pcm->frames), i1 = std::clamp<long long>(std::llround(out * rate), i0, pcm->frames);
        std::vector<std::vector<float>> ch((size_t)pcm->channels, std::vector<float>((size_t)(i1 - i0)));
        for (int c = 0; c < pcm->channels; ++c)
            for (long long i = i0; i < i1; ++i) ch[(size_t)c][(size_t)(i - i0)] = pcm->at(i, c);
        StretchParams sp;
        sp.ratio = ratio;
        sp.semitones = semis;
        const auto y = timeStretch(ch, rate, sp);
        auto made = std::make_shared<engine::Pcm>();
        made->channels = pcm->channels;
        made->frames = y.empty() ? 0 : (long long)y[0].size();
        made->samples.resize((size_t)(made->frames * made->channels));
        for (long long i = 0; i < made->frames; ++i)
            for (int c = 0; c < made->channels; ++c) made->samples[(size_t)(i * made->channels + c)] = y[(size_t)c][(size_t)i];
        return made;
    }

    double SolarisService::tempoOf(const Clip &c)
    {
        auto pcm = pcmFor(c.src);
        if (!pcm || pcm->frames <= 0) return 0.0;
        const double rate = mProject.header.sampleRate;
        const long long i0 = std::clamp<long long>(std::llround(c.in * rate), 0, pcm->frames), i1 = std::clamp<long long>(std::llround(c.out * rate), i0, pcm->frames);
        std::vector<float> mono((size_t)(i1 - i0));
        for (long long i = i0; i < i1; ++i) mono[(size_t)(i - i0)] = pcm->channels > 1 ? 0.5f * (pcm->at(i, 0) + pcm->at(i, 1)) : pcm->at(i, 0);
        const double bpm = estimateTempo(mono.data(), (long long)mono.size(), rate); // REQ-tempo-1
        return bpm > 0 ? std::round(bpm * 1000.0) / 1000.0 : 0.0;                 // to a thousandth: what is written
    }

    bool SolarisService::splitClip(const std::string &id, double at, std::string &made, std::string &err)
    {
        Project &p = mProject;
        const Clip *found = nullptr;
        if (!audioClip(p, id, found, "clip split", err)) return false;
        const Clip cl = *found; // a copy: the clip list grows below
        const double a = cl.at, len = clipLengthBeats(p, cl), e = a + len;
        at = toTick(at);
        if (!(at > a + kEps && at < e - kEps)) { err = "--at must fall inside " + id + " (" + canonicalBeats(a) + " … " + canonicalBeats(e) + ")"; return false; }
        Clip first = cl, second = cl;
        second.id = p.nextId("ac");
        second.remarks = {};
        first.at = a;
        second.at = at;
        if (cl.loop)
        {
            // a repetition begins every span: cut there, and each half still loops from its start
            const double span = (cl.out - cl.in) / clipSecondsPerBeat(p, cl), reps = span > 0 ? (at - a) / span : 0;
            const double k = std::round(reps);
            if (span <= 0 || std::fabs(at - (a + k * span)) > 0.5 / kPpq)
            {
                std::vector<std::string> near;
                for (double b : {a + std::floor(reps) * span, a + std::ceil(reps) * span})
                    if (b > a + kEps && b < e - kEps && (near.empty() || near.back() != canonicalBeats(toTick(b)))) near.push_back(canonicalBeats(toTick(b)));
                err = id + " loops every " + canonicalBeats(toTick(span)) + " beats — it is cut only where a repetition begins";
                if (near.empty()) err += ", and none begins inside it";
                else
                {
                    err += ":";
                    for (const auto &x : near) err += " " + x;
                }
                return false;
            }
            first.length = at - a;
            second.length = e - at;
        }
        else
        {
            // R-EDIT-1: the halves read on from where the other stopped — a reversed clip's first half is its span's end
            const double d = (at - a) * clipSecondsPerBeat(p, cl); // R-WARP-1: a warped clip's own beats
            if (!cl.reverse)
            {
                first.out = cl.in + d;
                second.in = cl.in + d;
            }
            else
            {
                first.in = cl.out - d;
                second.out = cl.out - d;
            }
            first.length = second.length = 0;
        }
        first.fadeIn = std::min(cl.fadeIn, at - a);
        first.fadeOut = 0;
        second.fadeIn = 0;
        second.fadeOut = std::min(cl.fadeOut, e - at);
        first.comp = compWithin(cl.comp, a, at); // R-REC-2: each half its part of the comp, every take kept
        second.comp = compWithin(cl.comp, at, e);
        for (size_t i = 0; i < p.clips.size(); ++i)
            if (p.clips[i].id == id)
            {
                p.clips[i] = first;
                p.clips.insert(p.clips.begin() + (long)i + 1, second);
                break;
            }
        changed("clip.split", id);
        changed("clip.added", second.id);
        made = second.id;
        return true;
    }

    bool SolarisService::audioEditCommand(const Command &c, std::string &err)
    {
        Project &p = mProject;
        if (c.kind == K::ClipSplit)
        {
            double at = 0;
            if (!parseNumber(c.flag("at"), at) || !std::isfinite(at)) { err = "--at is beats, got `" + c.flag("at") + "`"; return false; }
            std::string made;
            if (!splitClip(c.arg(0), at, made, err)) return false;
            mOutput = made + "\n";
            return true;
        }
        if (c.kind == K::ClipConsolidate) return consolidateClips(c, err);

        // K::ClipSlice
        const Clip *cl = nullptr;
        if (!audioClip(p, c.arg(0), cl, "clip slice", err)) return false;
        const std::string id = cl->id;
        if (cl->loop) { err = id + " loops — `clip slice` cuts a clip that plays its span once (set " + id + ".loop=off first)"; return false; }
        const bool every = c.has("every"), transients = c.has("transients");
        if (every == transients) { err = "clip slice needs one of --every <beats> or --transients"; return false; }
        if (c.has("kit") && !every) { err = "--kit slices --every <beats> — a kit's slices are equal (--transients makes clips)"; return false; }
        if (c.has("sensitivity") && !transients) { err = "--sensitivity goes with --transients"; return false; }
        const double a = cl->at, len = clipLengthBeats(p, *cl), e = a + len;
        std::vector<double> cuts;
        if (every)
        {
            double step = 0;
            if (!parseNumber(c.flag("every"), step) || !std::isfinite(step) || toTick(step) <= 0) { err = "--every is beats > 0, got `" + c.flag("every") + "`"; return false; }
            step = toTick(step);
            if (c.has("kit")) return sliceToKit(*cl, step, err);
            for (double b = a + step; b < e - 0.5 / kPpq; b += step) cuts.push_back(toTick(b));
            if (cuts.empty()) { err = "--every " + canonicalBeats(step) + " is not shorter than " + id + " (" + canonicalBeats(len) + " beats)"; return false; }
        }
        else
        {
            if (!cl->takes.empty()) { err = id + " is comped from takes — consolidate it first, then slice what it plays"; return false; }
            OnsetParams op;
            if (c.has("sensitivity") && (!parseNumber(c.flag("sensitivity"), op.sensitivity) || op.sensitivity < 0 || op.sensitivity > 1))
            { err = "--sensitivity is 0 … 1"; return false; }
            auto pcm = pcmFor(cl->src);
            if (!pcm) { err = id + "'s file (" + cl->src + ") is offline — nothing to listen to"; return false; }
            const double rate = p.header.sampleRate, spb = clipSecondsPerBeat(p, *cl) * rate; // a sample of its file per beat of it
            const long long i0 = std::clamp<long long>(std::llround(cl->in * rate), 0, pcm->frames);
            const long long i1 = std::clamp<long long>(std::llround(cl->out * rate), i0, pcm->frames);
            std::vector<float> mono((size_t)(i1 - i0));
            for (long long i = i0; i < i1; ++i) mono[(size_t)(i - i0)] = 0.5f * (pcm->at(i, 0) + pcm->at(i, 1));
            if (cl->reverse) std::reverse(mono.begin(), mono.end()); // what the clip plays, in its order
            for (long long o : detectOnsets(mono.data(), (long long)mono.size(), rate, op))
            {
                const double b = toTick(a + (double)o / spb);
                if (b > a + 0.5 / kPpq && b < e - 0.5 / kPpq && (cuts.empty() || b > cuts.back() + kEps)) cuts.push_back(b);
            }
            if (cuts.empty())
            {
                err = "no sound begins inside " + id + " at --sensitivity " + canonicalNumber(op.sensitivity) + " — raise it, or slice --every";
                return false;
            }
        }
        std::string cur = id;
        for (double b : cuts)
        {
            std::string made;
            if (!splitClip(cur, b, made, err)) return false;
            cur = made;
        }
        mOutput = id + "\n"; // the first piece; `made:` names the rest, in order
        return true;
    }

    bool SolarisService::sliceToKit(const Clip &clip, double step, std::string &err)
    {
        Project &p = mProject;
        const Clip cl = clip; // a copy: `clip add` grows the clip list
        if (cl.warp || cl.pitch != 0.0) { err = cl.id + " is warped or pitched — a kit plays the file as it is: consolidate it first, then slice that"; return false; }
        const double len = clipLengthBeats(p, cl);
        const int n = std::max(1, (int)std::lround(len / step));
        if (n > 64) { err = "--every " + canonicalBeats(step) + " makes " + std::to_string(n) + " slices of " + cl.id + " — a Sampler holds 64 at most"; return false; }
        auto pcm = pcmFor(cl.src);
        if (!pcm || pcm->frames <= 0) { err = cl.id + "'s file (" + cl.src + ") is offline — a kit needs its sound"; return false; }
        const double fileSeconds = (double)pcm->frames / p.header.sampleRate;
        // the kit: a new instrument track — a Sampler of the clip's file — made by `clip add`'s own rules
        Command add;
        add.kind = K::ClipAdd;
        add.flags = {{"instrument", "sampler"}, {"sample", resolvePath(cl.src)}, {"lane", "new"}, {"at", canonicalBeats(cl.at)}, {"length", canonicalBeats(toTick(len))}};
        if (!clipCommand(add, err)) return false;
        const std::string kitClip = p.clips.back().id, strip = p.clips.back().track, patternId = p.clips.back().pattern;
        const std::string dv = p.rack(strip)->devices.front().id;
        double root = 60;
        if (const DeviceType *t = DeviceRegistry::find("sampler"))
            for (const auto &ps : t->params)
                if (ps.name == "root") root = ps.def;
        std::string stored;
        for (const auto &kv : std::vector<std::pair<std::string, std::string>>{
                 {"mode", "slices"}, {"slices", std::to_string(n)}, {"start", canonicalNumber(cl.in / fileSeconds)},
                 {"end", canonicalNumber(cl.out / fileSeconds)}, {"reverse", cl.reverse ? "on" : "off"}})
            if (!setAddress(dv + "." + kv.first, kv.second, stored, err)) return false;
        // slice k from its place in the clip: the k-th note — a reversed clip plays the span's last slice first
        Pattern *pt = p.pattern(patternId);
        pt->length = toTick(len);
        pt->notes.clear();
        for (int k = 0; k < n; ++k)
        {
            Note nt;
            nt.at = toTick(len * k / n);
            nt.length = std::max(1.0 / kPpq, toTick(len * (k + 1) / n) - nt.at);
            nt.pitch = (int)root + (cl.reverse ? n - 1 - k : k);
            nt.vel = 127;
            pt->notes.push_back(nt);
        }
        changed("pattern.changed", patternId);
        mOutput = kitClip + "\n";
        return true;
    }

    bool SolarisService::consolidateClips(const Command &c, std::string &err)
    {
        Project &p = mProject;
        std::vector<std::string> ids;
        for (const auto &x : c.args)
            if (std::find(ids.begin(), ids.end(), x) == ids.end()) ids.push_back(x);
        std::string track, lane;
        double from = 0, to = 0;
        for (size_t i = 0; i < ids.size(); ++i)
        {
            const Clip *cl = p.clip(ids[i]);
            if (!cl) { err = "no clip `" + ids[i] + "`"; return false; }
            if (!cl->isAudio())
            {
                err = ids[i] + " is a note clip — its sound is its instrument's; `clip consolidate` joins audio clips (a strip's bounce is R-PERF-3)";
                return false;
            }
            const double e = cl->at + clipLengthBeats(p, *cl);
            if (i == 0) { track = cl->track; lane = cl->lane; from = cl->at; to = e; }
            else if (cl->track != track) { err = ids[i] + " plays through " + cl->track + ", " + ids[0] + " through " + track + " — consolidate joins clips of one strip"; return false; }
            from = std::min(from, cl->at);
            to = std::max(to, e);
        }
        // the clips alone, raw: their strip with no devices, at unity, centred, to the master at unity — nothing else
        Project q = p;
        q.clips.erase(std::remove_if(q.clips.begin(), q.clips.end(), [&](const Clip &x) { return std::find(ids.begin(), ids.end(), x.id) == ids.end(); }), q.clips.end());
        q.strips.erase(std::remove_if(q.strips.begin(), q.strips.end(), [&](const Strip &s) { return s.id != track; }), q.strips.end());
        for (auto &s : q.strips)
        {
            s.gain = 0;
            s.pan = 0;
            s.mute = s.solo = false;
            s.out.clear();
        }
        q.racks.clear();
        q.sends.clear();
        q.bindings.clear();
        q.header.masterGain = 0;
        CompileResult cr = compile(q, [this](const std::string &src) { return pcmFor(src); });
        engine::Engine eng;
        if (!eng.build(cr.graph, err)) return false;
        const long long start = beatsToSamples(p, from), end = beatsToSamples(p, to);
        eng.seek(start);
        eng.captureMaster(true);
        const long long lag = eng.outputLatency();
        std::vector<std::vector<float>> ch(2);
        engine::PortBuffers pb;
        for (long long pos = start; pos < end + lag;)
        {
            const int n = (int)std::min<long long>(4096, end + lag - pos);
            eng.render(n, pb);
            const auto &m = eng.capturedMaster();
            for (int k = 0; k < 2; ++k) ch[k].insert(ch[k].end(), m[k].begin(), m[k].end());
            pos += n;
        }
        for (auto &x : ch) x.erase(x.begin(), x.begin() + (long)lag);

        // beside the song: <folder>/Bounces/<strip> <n>.wav — the next number no file there has
        const Strip *s = p.strip(track);
        const std::string stem = s ? s->name : track;
        const auto slash = mPath.find_last_of('/');
        const std::string dir = (slash == std::string::npos ? std::string(".") : mPath.substr(0, slash)) + "/Bounces";
        std::set<std::string> there;
        std::vector<BrowserEntry> entries;
        std::string listErr;
        if (mHost.listDir && mHost.listDir(dir, entries, listErr))
            for (const auto &x : entries) there.insert(x.name);
        for (const auto &cl : p.clips) there.insert(cl.src.substr(cl.src.find_last_of('/') + 1)); // an undone bounce's name too
        int k = 1;
        while (there.count(stem + " " + std::to_string(k) + ".wav")) ++k;
        const std::string name = stem + " " + std::to_string(k) + ".wav";
        if (!mHost.writeWav) { err = "this build cannot write audio"; return false; }
        if (!mHost.writeWav(dir + "/" + name, ch, p.header.sampleRate, 32, err)) { if (err.empty()) err = "cannot write " + dir + "/" + name; return false; }
        mPcm.erase(resolvePath(dir + "/" + name)); // a file of that name decoded before (an undone bounce) is not this one

        p.clips.erase(std::remove_if(p.clips.begin(), p.clips.end(), [&](const Clip &x) { return std::find(ids.begin(), ids.end(), x.id) != ids.end(); }), p.clips.end());
        for (const auto &x : ids) changed("clip.deleted", x);
        Command add;
        add.kind = K::ClipAdd;
        add.flags = {{"src", dir + "/" + name}, {"strip", track}, {"at", canonicalBeats(from)}}; // `clip add` keeps it relative to the song
        if (!lane.empty()) add.flags.emplace_back("lane", lane);
        return clipCommand(add, err); // prints the new clip
    }
}
}
