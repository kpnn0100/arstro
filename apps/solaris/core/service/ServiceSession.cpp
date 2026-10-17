// SolarisService — the session grid (R-LAUNCH-1; docs/discussion.md Round 17): scenes (rows) × strips (columns), a slot
// a pattern or a file waiting in a scene for a strip. Scenes and slots are the song's nodes, edited as any; launching
// them is live — heard, never rendered, until recorded.
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
