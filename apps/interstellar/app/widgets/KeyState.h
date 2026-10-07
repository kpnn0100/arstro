/*
 *  interstellar_v1 — what a property's animation state is, what it is called, and the lines its
 *  controls dispatch (R-ANIM-3, R-ANIM-9, R-ANIM-10).
 *
 *  A diamond on a lane: 0 = no curve, 1 = animated but no key at the current time, 2 = a key here.
 *  "Here" is the curve's own clock — for a rack node or an effect the SOURCE's time, for a clip its
 *  footage time; in the timeline that is the playhead over a clip of it. A click adds a key there
 *  (`key add <address> --at <now>` — an offset's first key is a zero offset) or removes the one there.
 *
 *  Every property that can be marked to animate is a `Prop`, named as the ANIMATION section names it:
 *  "<property> · <object> · <group>" — the group is where Grade shows it (a Basic/Detail section:
 *  Tone, Colour, Presence, Effects, Sharpening, Noise Reduction, Lens; or the Curve, Mixer, Wheels or
 *  Xform tab), the effect's name, or "Clip" for a clip's own property.
 */
#pragma once
#include "../AppHooks.h"
#include "ColourKeys.h"
#include "CommandLine.h"
#include <cmath>
#include <string>
#include <vector>

namespace arstro
{
namespace interstellar_v1
{
namespace keys
{
    inline const interstellar::AnimModel *animOf(const interstellar::AppModel &m, const std::string &node, const std::string &key)
    {
        for (const auto &a : m.anims)
            if (a.node == node && a.key == key) return &a;
        return nullptr;
    }

    /** "Now" on a source clock, as the service reckons it: the source's reference frame; a group
     *  stands where the Grade target does. */
    inline double sourceNow(const interstellar::AppModel &m, const std::string &rackObj)
    {
        for (const auto &r : m.rack)
            if (r.rackObj == rackObj && !r.media.empty()) return r.frame;
        if (m.selectedRack >= 0 && m.selectedRack < (int)m.rack.size() && !m.rack[(size_t)m.selectedRack].media.empty())
            return m.rack[(size_t)m.selectedRack].frame;
        return 0.0;
    }

    inline bool keyAt(const interstellar::AnimModel &a, double t)
    {
        for (const auto &k : a.keys)
            if (std::fabs(k.t - t) < 5e-4) return true;
        return false;
    }

    inline int state(const interstellar::AppModel &m, const std::string &node, const std::string &key, double now)
    {
        const interstellar::AnimModel *a = animOf(m, node, key);
        if (!a) return 0;
        return keyAt(*a, now) ? 2 : 1;
    }

    /** The line a diamond click dispatches. */
    inline std::string toggle(const std::string &address, int state, double now)
    {
        return std::string(state == 2 ? "key remove " : "key add ") + cmd::quote(address) + " --at " + cmd::num(now);
    }

    /** Grade's mark (R-ANIM-9): an unmarked parameter is marked in the default mode (an offset —
     *  nothing on screen changes, R-ANIM-10). */
    inline std::string mark(const std::string &address) { return "key mark " + cmd::quote(address); }

    // ── names (R-ANIM-9) ──

    struct Prop
    {
        std::string node, key, address;     // the #rackobj / #effect / #clip, its key, the whole address
        std::string label, object, group;   // "Exposure", "s_day01", "Tone"
        int objectOrder = 0, groupOrder = 0, labelOrder = 0;
        bool fixedOnly = false;             // a shape or a clip's own property (R-ANIM-10)
    };

    // a clip's own properties (R-ANIM-1), in the order an editor reads a transform, and its speed (R-EDT-3)
    inline const char *const *clipKeys(int &n)
    {
        static const char *const k[] = {"opacity", "geom.x", "geom.y", "geom.scale", "geom.rotation", "speed"};
        n = (int)(sizeof k / sizeof k[0]);
        return k;
    }
    inline const char *clipLabel(int i)
    {
        static const char *const l[] = {"Opacity", "Position X", "Position Y", "Scale", "Rotation", "Speed"};
        return l[i];
    }

    inline int rackIndex(const interstellar::AppModel &m, const std::string &rackObj)
    {
        for (size_t i = 0; i < m.rack.size(); ++i)
            if (m.rack[i].rackObj == rackObj) return (int)i;
        return 9999;
    }

    /** Where Grade shows a colour key, in Grade's order (1 = Tone …). */
    inline int panelOrder(const std::string &group)
    {
        static const char *const g[] = {"Tone", "Colour", "Presence", "Effects", "Sharpening", "Noise Reduction", "Lens", "Curve", "Mixer", "Wheels", "Xform"};
        for (int i = 0; i < 11; ++i)
            if (group == g[i]) return i + 1;
        return 50;
    }

    inline Prop colourProp(const interstellar::AppModel &m, const std::string &rackObj, const std::string &bind, const std::string &key)
    {
        Prop p;
        p.node = rackObj;
        p.key = key;
        p.address = bind + "." + key;
        p.object = bind;
        p.objectOrder = rackIndex(m, rackObj);
        p.label = key;
        p.group = "Grade";
        p.groupOrder = 50;
        int n = 0;
        const ColourKey *ck = colourKeys(n);
        for (int i = 0; i < n; ++i)
            if (key == ck[i].key)
            {
                p.label = ck[i].label;
                p.group = ck[i].group;
                p.groupOrder = panelOrder(p.group);
                p.labelOrder = i;
                p.fixedOnly = p.groupOrder >= panelOrder("Curve");   // shapes
            }
        return p;
    }

    inline Prop effectProp(const interstellar::AppModel &m, const interstellar::EffectModel &e, const std::string &key)
    {
        Prop p;
        p.node = e.id;
        p.key = key;
        p.address = e.id + "." + key;
        p.object = e.nodeBind;
        p.objectOrder = rackIndex(m, e.node);
        p.group = e.label;
        p.groupOrder = 100 + e.order;
        p.label = key == "mix" ? std::string("Mix") : key;
        for (size_t i = 0; i < e.params.size(); ++i)
            if (e.params[i].key == key) { p.label = e.params[i].label; p.labelOrder = (int)i + 1; }
        return p;
    }

    inline Prop clipProp(const interstellar::AppModel &m, const interstellar::ClipModel &c, const std::string &key)
    {
        Prop p;
        p.node = c.id;
        p.key = key;
        p.object = c.name.empty() ? c.id : c.name;
        p.address = p.object + "." + key;
        p.objectOrder = rackIndex(m, c.src);
        p.group = "Clip";
        p.groupOrder = 0;
        p.label = key;
        p.fixedOnly = true;
        int n = 0;
        const char *const *k = clipKeys(n);
        for (int i = 0; i < n; ++i)
            if (key == k[i]) { p.label = clipLabel(i); p.labelOrder = i; }
        return p;
    }

    /** What a curve is called, and where it sorts (by object, then Grade's order). */
    inline Prop describe(const interstellar::AppModel &m, const interstellar::AnimModel &a)
    {
        if (a.owner == "clip")
            for (const auto &c : m.clips)
                if (c.id == a.node) return clipProp(m, c, a.key);
        if (a.owner == "effect")
            for (const auto &e : m.effects)
                if (e.id == a.node) return effectProp(m, e, a.key);
        Prop p = colourProp(m, a.node, a.nodeBind, a.key);
        p.address = a.address.empty() ? p.address : a.address;
        p.fixedOnly = p.fixedOnly || a.shape;
        return p;
    }

    /** Every property of a source that can be marked: its colour keys (Grade's panels) and its effects. */
    inline std::vector<Prop> sourceProps(const interstellar::AppModel &m, const std::string &rackObj)
    {
        std::vector<Prop> out;
        std::string bind;
        for (const auto &r : m.rack)
            if (r.rackObj == rackObj) bind = r.bindName;
        if (bind.empty()) return out;
        int n = 0;
        const ColourKey *ck = colourKeys(n);
        for (int i = 0; i < n; ++i) out.push_back(colourProp(m, rackObj, bind, ck[i].key));
        for (const auto &e : m.effects)
        {
            if (e.node != rackObj) continue;
            out.push_back(effectProp(m, e, "mix"));
            for (const auto &pm : e.params) out.push_back(effectProp(m, e, pm.key));
        }
        return out;
    }

    /** …and a clip's: its own, then its source's. */
    inline std::vector<Prop> clipProps(const interstellar::AppModel &m, const interstellar::ClipModel &c)
    {
        std::vector<Prop> out;
        int n = 0;
        const char *const *k = clipKeys(n);
        for (int i = 0; i < n; ++i) out.push_back(clipProp(m, c, k[i]));
        const auto src = sourceProps(m, c.src);
        out.insert(out.end(), src.begin(), src.end());
        return out;
    }
}
}
}
