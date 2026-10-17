// SolarisService — the session grid (R-LAUNCH-1; docs/discussion.md Round 17): scenes (rows) × strips (columns), a slot
// a pattern or a file waiting in a scene for a strip. Scenes and slots are the song's nodes, edited as any; launching
// them is live — heard, never rendered, until recorded.
#include "Compile.h"
#include "Format.h"
#include "Player.h"
#include "SolarisService.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace arstro
{
namespace solaris
{
    using K = Command::Kind;

    namespace
    {
        // the scenes numbered 0 … n−1 in their order, `moved` placed at `at`
        void renumber(Project &p, const std::string &moved, int at)
        {
            std::vector<Scene *> v;
            for (auto &x : p.scenes)
                if (x.id != moved) v.push_back(&x);
            std::stable_sort(v.begin(), v.end(), [](const Scene *a, const Scene *b) { return a->order < b->order; });
            if (Scene *m = p.scene(moved)) v.insert(v.begin() + std::clamp(at, 0, (int)v.size()), m);
            for (size_t i = 0; i < v.size(); ++i) v[i]->order = (int)i;
        }
    }

    double SolarisService::slotBeats(const Slot &sl) const
    {
        if (sl.length > 0) return sl.length;
        if (const Pattern *pt = mProject.pattern(sl.pattern)) return pt->length;
        return sl.out > sl.in ? (sl.out - sl.in) * mProject.header.bpm / 60.0 : 0.0; // a file, at the song's first tempo
    }

    double SolarisService::launchBeat()
    {
        // R-LAUNCH-1: the next quantum after what the engine has rendered — two blocks on, so the message is there
        // first; never in the past
        const double ahead = beatOfSample((double)(mPlayer->rendered() + 2LL * std::max(64, mSettings.bufferSize)));
        const std::string &q = mProject.header.launchQuantize;
        int num = 4, den = 4;
        std::sscanf(mProject.header.sig.c_str(), "%d/%d", &num, &den);
        const double step = q == "beat" ? 1.0 : q == "none" ? 0.0 : num * 4.0 / std::max(1, den);
        return step > 0 ? toTick(std::ceil(ahead / step - 1e-9) * step) : toTick(ahead);
    }

    void SolarisService::advanceLaunches(double heard)
    {
        // what was due by the beat now heard has landed — as the engine landed it, a latency earlier
        for (auto it = mLaunch.begin(); it != mLaunch.end();)
        {
            Launch &l = it->second;
            if (l.pend && heard + 1e-9 >= l.at)
            {
                if (mRecording) // what the strip played until now is a span; a slot landing opens the next
                {
                    for (auto &sp : mSpans)
                        if (sp.strip == it->first && sp.to < 0) sp.to = l.at;
                    if (l.pend == 1 && !l.pendSlot.empty()) mSpans.push_back(Span{it->first, l.pendSlot, l.at, -1, 0});
                }
                if (l.pend == 2) { it = mLaunch.erase(it); continue; } // the arrangement again
                l.session = true;
                l.slot = l.pendSlot;
                l.start = l.at;
                l.pend = 0;
                l.pendSlot.clear();
            }
            ++it;
        }
    }

    std::vector<LaunchModel> SolarisService::launchedModel(double heard) const
    {
        std::vector<LaunchModel> out;
        for (const auto &kv : mLaunch)
        {
            const Launch &l = kv.second;
            LaunchModel m;
            m.strip = kv.first;
            m.session = l.session;
            m.slot = l.slot;
            m.next = l.pend == 2 ? "back" : l.pend == 1 ? (l.pendSlot.empty() ? std::string("stop") : l.pendSlot) : std::string();
            m.at = l.pend ? l.at : l.start;
            if (l.session && !l.slot.empty())
                if (const Slot *sl = mProject.slot(l.slot))
                {
                    const double len = slotBeats(*sl);
                    if (len > 0) m.progress = std::fmod(std::max(0.0, heard - l.start), len) / len;
                }
            out.push_back(m);
        }
        return out;
    }

    bool SolarisService::commitLaunches(double stopBeat, std::string &err)
    {
        // R-LAUNCH-1: what the session played while recording, into the arrangement — so the song plays back what was
        // heard: each span as clips on its strip's lane, and the strip's own clips under it giving way (one inside goes;
        // one across an edge is cut there — a note clip's second half starts into its pattern, `offset`)
        (void)err;
        mRecording = false;
        Project &p = mProject;
        int placed = 0;
        for (auto &sp : mSpans)
            if (sp.to < 0) sp.to = stopBeat;
        for (const Span &sp : mSpans)
        {
            const Slot *sl = p.slot(sp.slot);
            const Strip *st = p.strip(sp.strip);
            if (!sl || !st || sp.to <= sp.from + 1e-9) continue;
            const double from = sp.from, to = sp.to;
            // its lane: an instrument's track, else where its clips are — a new one when it has none
            std::string lane;
            for (const auto &l : p.lanes)
                if (st->kind == "instrument" && l.track == st->id) lane = l.id;
            if (lane.empty() && st->kind == "audio")
                for (const auto &c : p.clips)
                    if (c.track == st->id && !c.lane.empty()) { lane = c.lane; break; }
            if (lane.empty())
            {
                Lane l;
                l.id = p.nextId("ln");
                for (const auto &x : p.lanes) l.order = std::max(l.order, x.order + 1);
                if (st->kind == "instrument") l.track = st->id;
                else l.name = st->name;
                p.lanes.push_back(l);
                lane = l.id;
            }
            // the arrangement under the span gives way
            std::vector<Clip> tails;
            for (auto it = p.clips.begin(); it != p.clips.end();)
            {
                Clip &c = *it;
                if (c.track != st->id) { ++it; continue; }
                const double a = c.at, b = c.at + clipLengthBeats(p, c);
                if (b <= from + 1e-9 || a >= to - 1e-9) { ++it; continue; }
                const bool head = a < from - 1e-9, tail = b > to + 1e-9;
                if (!head && !tail) { it = p.clips.erase(it); continue; } // inside: it goes
                const double secPerBeat = b > a ? (c.out - c.in) / (b - a) : 0.0;
                Clip t = c;
                if (tail)
                {
                    const double skip = to - a;
                    t.at = to;
                    t.fadeIn = 0;
                    if (c.isAudio())
                    {
                        if (c.loop) t.length = b - to; // (a looped file cut at its start restarts its loop — not yet otherwise)
                        else t.in = c.in + skip * secPerBeat;
                    }
                    else
                    {
                        t.offset = c.offset + skip;
                        t.length = b - to;
                    }
                }
                if (head)
                {
                    c.fadeOut = 0;
                    if (c.isAudio() && !c.loop) c.out = c.in + (from - a) * secPerBeat;
                    else c.length = from - a;
                    if (tail) tails.push_back(t);
                }
                else c = t; // only its tail is left
                ++it;
            }
            for (auto &t : tails)
            {
                t.id = p.nextId("ac");
                p.clips.push_back(t);
            }
            // the span, as clips: one when the slot's loop is its pattern's, else one a loop (a slot plays the start of a
            // longer pattern, or a shorter one repeated)
            const double len = slotBeats(*sl);
            if (len <= 0) continue;
            auto clipAt = [&](double at) {
                Clip c;
                c.id = p.nextId("ac");
                c.name = sl->name;
                c.track = st->id;
                c.lane = lane;
                c.at = at;
                return c;
            };
            if (!sl->pattern.empty())
            {
                const Pattern *pt = p.pattern(sl->pattern);
                if (!pt || pt->length <= 0) continue;
                if (std::fabs(len - pt->length) < 1e-9)
                {
                    Clip c = clipAt(from);
                    c.pattern = sl->pattern;
                    c.length = to - from;
                    c.offset = std::fmod(sp.phase, pt->length);
                    p.clips.push_back(c);
                    ++placed;
                }
                else
                    for (double t = from, ph = sp.phase; t < to - 1e-9; ph = 0)
                    {
                        const double seg = std::min(len - ph, to - t);
                        Clip c = clipAt(t);
                        c.pattern = sl->pattern;
                        c.length = seg;
                        c.offset = ph;
                        p.clips.push_back(c);
                        ++placed;
                        t += seg;
                    }
            }
            else
            {
                // a file plays the first of each loop it lasts; the rest of the loop is silence — no clip there
                const double spb = 60.0 / p.header.bpm, span = (sl->out - sl->in) / spb; // beats, at the song's first tempo
                for (double t = from, ph = sp.phase; t < to - 1e-9; ph = 0)
                {
                    const double seg = std::min(len - ph, to - t);
                    if (ph < span - 1e-9)
                    {
                        Clip c = clipAt(t);
                        c.src = sl->src;
                        c.in = sl->in + ph * spb;
                        c.out = c.in + std::min(seg, span - ph) * spb;
                        c.gain = sl->gain;
                        p.clips.push_back(c);
                        ++placed;
                    }
                    t += seg;
                }
            }
        }
        mSpans.clear();
        if (placed) changed("session.recorded", std::to_string(placed));
        return true;
    }

    bool SolarisService::sessionCommand(const Command &c, std::string &err)
    {
        if (!requireOpen(err)) return false;
        Project &p = mProject;
        double v = 0;
        auto index = [&](const std::string &flag, int fallback, int &out) {
            out = fallback;
            if (!c.has(flag)) return true;
            if (!parseNumber(c.flag(flag), v) || v != std::floor(v) || v < 0) { err = "--" + flag + " is a row: 0, 1, 2 …"; return false; }
            out = (int)v;
            return true;
        };
        switch (c.kind)
        {
        case K::SceneAdd:
        {
            int at = 0;
            if (!index("at", (int)p.scenes.size(), at)) return false;
            Scene sc;
            sc.id = p.nextId("sc");
            sc.name = c.args.empty() ? "Scene " + std::to_string(p.scenes.size() + 1) : c.arg(0);
            sc.order = (int)p.scenes.size();
            p.scenes.push_back(sc);
            renumber(p, sc.id, at);
            mOutput = sc.id + "\n";
            changed("scene.added", sc.id);
            return true;
        }
        case K::SceneDelete: case K::SceneMove:
        {
            const Scene *sc = p.scene(c.arg(0));
            if (!sc) { err = "no scene " + c.arg(0); return false; }
            const std::string id = sc->id;
            if (c.kind == K::SceneMove)
            {
                int to = 0;
                if (!index("to", 0, to)) return false;
                renumber(p, id, to);
                changed("scene.moved", id);
                return true;
            }
            p.slots.erase(std::remove_if(p.slots.begin(), p.slots.end(), [&](const Slot &x) { return x.scene == id; }), p.slots.end());
            p.scenes.erase(std::remove_if(p.scenes.begin(), p.scenes.end(), [&](const Scene &x) { return x.id == id; }), p.scenes.end());
            renumber(p, std::string(), 0);
            changed("scene.deleted", id);
            return true;
        }
        case K::SlotAdd:
        {
            const Scene *sc = p.scene(c.arg(0));
            if (!sc) { err = "no scene " + c.arg(0); return false; }
            const Strip *st = p.strip(c.arg(1));
            if (!st) { err = "no strip " + c.arg(1); return false; }
            const int given = (int)c.has("pattern") + (int)c.has("clip") + (int)c.has("src");
            if (given != 1) { err = "a slot plays ONE thing: --pattern <pt>, --clip <ac> or --src <file>"; return false; }
            Slot sl;
            sl.id = p.nextId("sl");
            sl.scene = sc->id;
            sl.strip = st->id;
            if (c.has("pattern")) sl.pattern = c.flag("pattern");
            else if (c.has("src"))
            {
                // a file: measured now, as `clip add` measures one — its span is its loop when no --length is given
                sl.src = relativePath(c.flag("src"));
                const auto pcm = pcmFor(sl.src);
                if (!pcm) { err = "cannot read " + c.flag("src"); return false; }
                sl.out = (double)pcm->frames / p.header.sampleRate;
            }
            else
            {
                const Clip *cl = p.clip(c.flag("clip"));
                if (!cl) { err = "no clip " + c.flag("clip"); return false; }
                sl.pattern = cl->pattern;
                sl.src = cl->src;
                sl.in = cl->in;
                sl.out = cl->out;
                sl.length = cl->length;
                sl.name = cl->name;
            }
            if (c.has("length"))
            {
                if (!parseNumber(c.flag("length"), v) || !(v > 0) || v > 4096) { err = "--length is beats a loop lasts, above 0"; return false; }
                sl.length = toTick(v);
            }
            p.slots.push_back(sl); // the validator refuses a strip that cannot play it, a cell already full, a pattern or scene that is not
            mOutput = sl.id + "\n";
            changed("slot.added", sl.id);
            return true;
        }
        case K::Launch:
        {
            // R-LAUNCH-1: a slot, or a scene's slots — on the next quantum the engine has not rendered; stopped, the
            // transport starts and they play at once
            std::vector<const Slot *> go;
            if (const Slot *sl = p.slot(c.arg(0))) go.push_back(sl);
            else if (const Scene *sc = p.scene(c.arg(0)))
            {
                for (const Scene *row : p.scenesInOrder())
                    if (row->id == sc->id)
                        for (const Strip *st : p.stripsInOrder())
                            for (const auto &x : p.slots)
                                if (x.scene == sc->id && x.strip == st->id) go.push_back(&x);
                if (go.empty()) { err = "scene " + sc->id + " (" + sc->name + ") has no slots"; return false; }
            }
            else { err = "no slot or scene " + c.arg(0) + " (`state print --json` lists slots[] and scenes[])"; return false; }
            double at = 0;
            if (!mPlayer || !mPlayer->running())
            {
                Command play;
                play.kind = K::TransportPlay;
                if (!transportCommand(play, err)) return false;
                at = mPosition; // where it starts: at once
            }
            else at = launchBeat();
            const long long sample = (long long)std::llround(sampleOfBeat(at));
            for (const Slot *sl : go)
            {
                const auto it = std::find(mLiveStrips.begin(), mLiveStrips.end(), sl->strip);
                if (it == mLiveStrips.end()) continue;
                Player::Live m;
                m.kind = Player::Live::Launch;
                m.strip = (int)(it - mLiveStrips.begin());
                std::snprintf(m.name, sizeof m.name, "%s", sl->id.c_str());
                m.sample = sample;
                mPlayer->send(m);
                Launch &l = mLaunch[sl->strip];
                l.pend = 1;
                l.pendSlot = sl->id;
                l.at = at;
            }
            mOutput = "launched on beat " + canonicalBeats(at) + "\n";
            return true;
        }
        case K::LaunchStop: case K::LaunchBack:
        {
            if (!mPlayer || !mPlayer->running()) { err = "nothing is playing"; return false; }
            std::vector<std::string> strips;
            const std::string who = c.args.empty() ? std::string("all") : c.arg(0);
            if (who == "all")
                for (const auto &kv : mLaunch) strips.push_back(kv.first);
            else if (!p.strip(who)) { err = "no strip " + who; return false; }
            else strips.push_back(who);
            const double at = launchBeat();
            const long long sample = (long long)std::llround(sampleOfBeat(at));
            for (const auto &id : strips)
            {
                const auto it = std::find(mLiveStrips.begin(), mLiveStrips.end(), id);
                if (it == mLiveStrips.end()) continue;
                Player::Live m;
                m.kind = c.kind == K::LaunchBack ? Player::Live::LaunchBack : Player::Live::Launch; // a launch of nothing stops
                m.strip = (int)(it - mLiveStrips.begin());
                m.sample = sample;
                mPlayer->send(m);
                Launch &l = mLaunch[id];
                l.pend = c.kind == K::LaunchBack ? 2 : 1;
                l.pendSlot.clear();
                l.at = at;
            }
            mOutput = std::string(c.kind == K::LaunchBack ? "back to the arrangement" : "stopping") + " on beat " + canonicalBeats(at) + "\n";
            return true;
        }
        case K::SlotDelete:
        {
            const Slot *sl = p.slot(c.arg(0));
            if (!sl) { err = "no slot " + c.arg(0); return false; }
            const std::string id = sl->id;
            p.slots.erase(std::remove_if(p.slots.begin(), p.slots.end(), [&](const Slot &x) { return x.id == id; }), p.slots.end());
            changed("slot.deleted", id);
            return true;
        }
        default:
            return false;
        }
    }
}
}
