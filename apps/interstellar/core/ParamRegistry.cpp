#include "ParamRegistry.h"
#include <algorithm>
#include <cmath>
#include <sstream>

namespace arstro
{
namespace interstellar
{
    namespace
    {
        using E = ParamRegistry::Entry;
        using T = ParamRegistry::Type;
        using O = ParamRegistry::Owner;
        using K = ParamRegistry::ObjKind;

        E mk(K obj, const char *filter, const char *param, T type, const char *unit,
             double mn, double mx, double def, O owner, const char *key,
             bool automatable = true, bool readOnly = false)
        {
            E e;
            e.obj = obj; e.filter = filter; e.param = param; e.type = type; e.unit = unit;
            e.min = mn; e.max = mx; e.def = def; e.owner = owner;
            e.key = *key ? key : param;
            e.automatable = automatable && !readOnly;
            e.bindable = !readOnly;
            e.readOnly = readOnly;
            return e;
        }

        /** Levenshtein, capped — only for "did you mean". */
        int editDistance(const std::string &a, const std::string &b)
        {
            std::vector<int> prev(b.size() + 1), cur(b.size() + 1);
            for (size_t j = 0; j <= b.size(); ++j) prev[j] = (int)j;
            for (size_t i = 1; i <= a.size(); ++i)
            {
                cur[0] = (int)i;
                for (size_t j = 1; j <= b.size(); ++j)
                    cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1)});
                prev = cur;
            }
            return prev[b.size()];
        }

        std::vector<E> build()
        {
            std::vector<E> v;

            // ── rack objects: COSMO's vocabulary, grouped by COSMO's panels (R-PARAM-4) ──
            // Leaf names are exactly EditParamsIO's keys. A name invented here would mean a
            // preset and an expression spelling one parameter two ways.
            struct S { const char *filter, *key, *unit; double mn, mx, def; };
            static const S kRack[] = {
                {"basic", "exposure",       "ev",  -5,   5,    0},
                {"basic", "contrast",       "",  -100, 100,    0},
                {"basic", "highlights",     "",  -100, 100,    0},
                {"basic", "shadows",        "",  -100, 100,    0},
                {"basic", "whites",         "",  -100, 100,    0},
                {"basic", "blacks",         "",  -100, 100,    0},
                {"basic", "temp",           "K",  2000, 12000, 6500},
                {"basic", "tint",           "",  -100, 100,    0},
                {"basic", "vibrance",       "",  -100, 100,    0},
                {"basic", "saturation",     "",  -100, 100,    0},
                {"basic", "texture",        "",  -100, 100,    0},
                {"basic", "clarity",        "",  -100, 100,    0},
                {"basic", "dehaze",         "",  -100, 100,    0},
                {"detail", "sharpenAmount",  "",     0, 150,    0},
                {"detail", "sharpenRadius",  "px",   0.5, 3,    1},
                {"detail", "sharpenMasking", "",     0, 100,    0},
                {"detail", "nrLuminance",    "",     0, 100,    0},
                {"detail", "nrColor",        "",     0, 100,    0},
                {"detail", "grainAmount",    "",     0, 100,    0},
                {"detail", "grainSize",      "",     0, 100,    0},
                {"detail", "lensDistortion", "",  -100, 100,    0},
                {"detail", "lensCA",         "",  -100, 100,    0},
                {"detail", "lensVignette",   "",  -100, 100,    0},
                {"mixer", "mixerSpread",     "",     0, 100,   25},
                {"grade", "balance",         "",  -100, 100,    0},
                {"grade", "remapSrc",        "deg",  0, 360,    0},
                {"grade", "remapRange",      "deg",  0, 180,   30},
                {"grade", "remapDst",        "deg",  0, 360,    0},
                {"grade", "remapStrength",   "",     0, 100,    0},
                {"xform", "rotation",        "deg", -45,  45,   0},
                // `crop` is ONE key in Cosmo's codec (`crop=x,y,w,h`), addressed per component
                // here because the address space is per-scalar. RackEmbed turns a component
                // write into a read-modify-write of the quadruple.
                {"xform", "crop.x",          "",     0,   1,    0},
                {"xform", "crop.y",          "",     0,   1,    0},
                {"xform", "crop.w",          "",     0,   1,    1},
                {"xform", "crop.h",          "",     0,   1,    1},
            };
            for (const auto &s : kRack)
                v.push_back(mk(K::Rack, s.filter, s.key, T::Float, s.unit, s.mn, s.mx, s.def,
                               O::Cosmo, s.key));
            // Interstellar's own rack-object parameters (Project::RackObj) — Cosmo has no
            // concept of either.
            v.push_back(mk(K::Rack, "", "opacity", T::Float, "", 0, 1, 1, O::Interstellar, "opacity"));
            v.push_back(mk(K::Rack, "", "bypass", T::Bool, "", 0, 1, 0, O::Cosmo, "bypass",
                           /*automatable=*/false));

            // ── clips ──
            v.push_back(mk(K::Clip, "", "opacity", T::Float, "", 0, 1, 1, O::Interstellar, "opacity"));
            v.push_back(mk(K::Clip, "", "speed", T::Float, "x", 0.05, 20, 1, O::Interstellar, "speed", false));
            v.push_back(mk(K::Clip, "", "at", T::Float, "s", 0, 1e6, 0, O::Interstellar, "at", false));
            v.push_back(mk(K::Clip, "", "in", T::Float, "s", 0, 1e6, 0, O::Interstellar, "in", false));
            v.push_back(mk(K::Clip, "", "out", T::Float, "s", 0, 1e6, 0, O::Interstellar, "out", false));
            v.push_back(mk(K::Clip, "", "blend", T::Enum, "", 0, 6, 0, O::Interstellar, "blend", false));
            v.push_back(mk(K::Clip, "", "fit", T::Enum, "", 0, 3, 0, O::Interstellar, "fit", false));
            v.push_back(mk(K::Clip, "", "src", T::Ref, "", 0, 0, 0, O::Interstellar, "src", false));
            struct G { const char *p, *unit; double mn, mx, def; };
            static const G kGeom[] = {
                {"x", "px", -20000, 20000, 0}, {"y", "px", -20000, 20000, 0},
                {"scale", "", 0.01, 20, 1},    {"rotation", "deg", -360, 360, 0},
                {"anchor.x", "", 0, 1, 0.5},   {"anchor.y", "", 0, 1, 0.5},
                {"crop.x", "", 0, 1, 0},       {"crop.y", "", 0, 1, 0},
                {"crop.w", "", 0, 1, 1},       {"crop.h", "", 0, 1, 1}};
            for (const auto &g : kGeom)
                v.push_back(mk(K::Clip, "geom", g.p, T::Float, g.unit, g.mn, g.mx, g.def,
                               O::Interstellar, g.p));

            // ── tracks ──
            v.push_back(mk(K::Track, "", "opacity", T::Float, "", 0, 1, 1, O::Interstellar, "opacity"));
            v.push_back(mk(K::Track, "", "gain", T::Float, "dB", -60, 12, 0, O::Interstellar, "gain"));
            v.push_back(mk(K::Track, "", "blend", T::Enum, "", 0, 6, 0, O::Interstellar, "blend", false));
            v.push_back(mk(K::Track, "", "mute", T::Bool, "", 0, 1, 0, O::Interstellar, "mute", false));

            // ── automation clips: `value` is the shape's output, and it is READ-ONLY, which is
            // what lets a binding read it (`1 + ac_push.value * 0.08`) without becoming a
            // second authority for it (R-BIND-5). ──
            v.push_back(mk(K::AutoClip, "", "value", T::Float, "", 0, 1, 0, O::Interstellar,
                           "value", false, /*readOnly=*/true));
            v.push_back(mk(K::AutoClip, "", "dur", T::Float, "s", 0.001, 1e6, 1, O::Interstellar,
                           "dur", false));

            // ── the project, all read-only ──
            static const char *kProj[] = {"playhead", "fps", "width", "height", "duration"};
            for (const char *k : kProj)
                v.push_back(mk(K::Project, "", k, T::Float, "", 0, 1e9, 0, O::Interstellar, k,
                               false, /*readOnly=*/true));
            return v;
        }
    }

    const std::vector<ParamRegistry::Entry> &ParamRegistry::all()
    {
        static const std::vector<Entry> v = build();
        return v;
    }

    const char *ParamRegistry::typeName(Type t)
    {
        switch (t)
        {
            case Type::Float: return "float";
            case Type::Int: return "int";
            case Type::Bool: return "bool";
            case Type::Enum: return "enum";
            case Type::Ref: return "ref";
        }
        return "float";
    }
    const char *ParamRegistry::ownerName(Owner o)
    {
        return o == Owner::Cosmo ? "cosmo" : "interstellar";
    }
    const char *ParamRegistry::objKindName(ObjKind k)
    {
        switch (k)
        {
            case ObjKind::Rack: return "rack";
            case ObjKind::Clip: return "clip";
            case ObjKind::Track: return "track";
            case ObjKind::AutoClip: return "autoclip";
            case ObjKind::Project: return "project";
        }
        return "rack";
    }

    bool ParamRegistry::resolve(const Project &p, const RackAccess *rack, const std::string &address,
                                Resolved &out, std::string &err)
    {
        err.clear();
        const auto dot = address.find('.');
        if (dot == std::string::npos || dot == 0)
        {
            err = "not an address: '" + address + "' — expected <object>[.<filter>].<param>";
            return false;
        }
        const std::string objName = address.substr(0, dot);
        const std::string suffix = address.substr(dot + 1);

        // Which object is this? The bind name is the user's language; the id is everyone else's.
        ObjKind kind;
        NodeId objectId;
        if (objName == "project") { kind = ObjKind::Project; }
        else if (const Track *t = p.track(objName)) { kind = ObjKind::Track; objectId = t->id; }
        else if (const Clip *c = p.clip(objName)) { kind = ObjKind::Clip; objectId = c->id; }
        else if (const AutoClip *a = p.autoClip(objName)) { kind = ObjKind::AutoClip; objectId = a->id; }
        else if (const RackObj *r = p.rackObj(objName)) { kind = ObjKind::Rack; objectId = r->node; }
        else
        {
            // Not a known object. Say so with the closest names, because an address space this
            // large is not eyeballable (R-PARAM-5).
            std::vector<std::pair<int, std::string>> near;
            auto consider = [&](const std::string &n) {
                if (!n.empty()) near.emplace_back(editDistance(objName, n), n);
            };
            for (const auto &t : p.tracks) consider(t.name);
            for (const auto &c : p.clips) consider(c.name);
            for (const auto &a : p.autoClips) consider(a.name);
            for (const auto &r : p.rackObjs) consider(r.name);
            consider("project");
            std::sort(near.begin(), near.end());
            err = "no such object: '" + objName + "'";
            if (!near.empty() && near.front().first <= 3)
            {
                err += " — did you mean '" + near.front().second + "'";
                if (near.size() > 1 && near[1].first <= 3) err += " or '" + near[1].second + "'";
                err += "?";
            }
            (void)rack;
            return false;
        }

        for (const Entry &e : all())
        {
            if (e.obj != kind) continue;
            if (e.suffix() != suffix) continue;
            out.entry = &e;
            out.obj = kind;
            out.objectId = objectId;
            out.objectName = objName;
            return true;
        }

        // The object exists and the parameter does not — the commonest real typo, and the one
        // cosmo's D-59 turned into a silent success.
        std::vector<std::pair<int, std::string>> near;
        for (const Entry &e : all())
            if (e.obj == kind) near.emplace_back(editDistance(suffix, e.suffix()), e.suffix());
        std::sort(near.begin(), near.end());
        err = "no such parameter: '" + objName + "." + suffix + "' on a " + objKindName(kind);
        if (!near.empty() && near.front().first <= 4)
        {
            err += " — did you mean '" + objName + "." + near.front().second + "'";
            if (near.size() > 1 && near[1].first <= 4)
                err += " or '" + objName + "." + near[1].second + "'";
            err += "?";
        }
        return false;
    }

    std::vector<std::string> ParamRegistry::addresses(const Project &p, const RackAccess *rack)
    {
        (void)rack;
        std::vector<std::string> out;
        auto emit = [&](ObjKind k, const std::string &name) {
            for (const Entry &e : all())
                if (e.obj == k) out.push_back(name + "." + e.suffix());
        };
        for (const auto &r : p.rackObjs) emit(ObjKind::Rack, r.name);
        for (const auto &t : p.tracks) emit(ObjKind::Track, t.name);
        for (const auto &c : p.clips) emit(ObjKind::Clip, c.name);
        for (const auto &a : p.autoClips) emit(ObjKind::AutoClip, a.name);
        emit(ObjKind::Project, "project");
        return out;
    }
}
}
