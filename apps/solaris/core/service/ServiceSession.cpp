// SolarisService — the session grid (R-LAUNCH-1; docs/discussion.md Round 17): scenes (rows) × strips (columns), a slot
// a pattern or a file waiting in a scene for a strip. Scenes and slots are the song's nodes, edited as any; launching
// them is live — heard, never rendered, until recorded.
#include "Format.h"
#include "SolarisService.h"
#include <algorithm>
#include <cmath>

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
