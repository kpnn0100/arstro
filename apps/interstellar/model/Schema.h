/*
 *  interstellar_model — Schema: the field table of every typed `.isp` node. INTERNAL to this
 *  library; not part of the public API.
 *
 *  Why a table and not hand-written code per node: five readers need the same facts about a field
 *  — its key, its type, its default, whether it is written, and whether a version may override it.
 *  The parser, the serializer, `#tlset` application, `setField`'s validation and `thawCut`'s diff
 *  each wrote their own copy in the first build's shape of this code, and five copies of "what
 *  fields does a clip have" is how a field gets parsed but not written, or overridable but not
 *  diffed. One table, read five ways, cannot disagree with itself.
 *
 *  A row's `line` is where the canonical serializer puts it (0 = the `#type` line, then indented
 *  continuation lines), matching project-format.md's own layout so the file reads like the spec.
 */
#pragma once
#include "Project.h"
#include <functional>
#include <string>
#include <vector>

namespace arstro
{
namespace interstellar
{
namespace schema
{
    enum class Kind { Text, Ref, Pair, Number, Time, Int, Bool };

    template <class T> struct Field
    {
        std::string key;
        Kind kind = Kind::Text;
        int line = 0;
        /** Settable by `setField` — and therefore overridable by a `#tlset`. Identity (id, name,
         *  the declaring timeline, `from`) and shape (`kind`) are not: a version that could
         *  rename a node would make an address mean different things in different versions. */
        bool editable = false;
        double def = 0.0;                                       // the repair target
        std::function<double(const T &)> num;
        std::function<void(T &, double)> setNum;
        std::function<std::string(const T &)> text;
        std::function<void(T &, const std::string &)> setText;
        std::function<bool(const T &, const Project &)> emit;   // empty = always written
    };

    template <class T> using Emit = std::function<bool(const T &, const Project &)>;

    template <class T, class M>
    Field<T> text(const char *key, M T::*m, int line, bool editable, Emit<T> emit = {}, Kind kind = Kind::Text)
    {
        Field<T> f;
        f.key = key; f.kind = kind; f.line = line; f.editable = editable; f.emit = emit;
        f.text = [m](const T &n) { return std::string(n.*m); };
        f.setText = [m](T &n, const std::string &v) { n.*m = v; };
        return f;
    }
    template <class T, class M>
    Field<T> ref(const char *key, M T::*m, int line, bool editable, Emit<T> emit = {})
    {
        return text<T, M>(key, m, line, editable, emit, Kind::Ref);
    }
    template <class T, class M>
    Field<T> number(const char *key, M T::*m, double def, int line, bool editable, Emit<T> emit = {},
                    Kind kind = Kind::Number)
    {
        Field<T> f;
        f.key = key; f.kind = kind; f.line = line; f.editable = editable; f.def = def; f.emit = emit;
        f.num = [m](const T &n) { return (double)(n.*m); };
        f.setNum = [m](T &n, double v) { n.*m = (M)v; };
        return f;
    }
    template <class T, class M>
    Field<T> time(const char *key, M T::*m, double def, int line, bool editable, Emit<T> emit = {})
    {
        return number<T, M>(key, m, def, line, editable, emit, Kind::Time);
    }
    template <class T>
    Field<T> integer(const char *key, int T::*m, int def, int line, bool editable, Emit<T> emit = {})
    {
        return number<T, int>(key, m, def, line, editable, emit, Kind::Int);
    }
    template <class T>
    Field<T> boolean(const char *key, bool T::*m, bool def, int line, bool editable, Emit<T> emit = {})
    {
        Field<T> f;
        f.key = key; f.kind = Kind::Bool; f.line = line; f.editable = editable; f.def = def ? 1.0 : 0.0;
        f.emit = emit;
        f.num = [m](const T &n) { return (n.*m) ? 1.0 : 0.0; };
        f.setNum = [m](T &n, double v) { n.*m = v != 0.0; };
        return f;
    }
    inline Field<Clip> geom(const char *key, double Geom::*m, double def, int line)
    {
        Field<Clip> f;
        f.key = key; f.kind = Kind::Number; f.line = line; f.editable = true; f.def = def;
        f.num = [m](const Clip &c) { return c.geom.*m; };
        f.setNum = [m](Clip &c, double v) { c.geom.*m = v; };
        return f;
    }

    template <class T> Emit<T> nonEmpty(std::string T::*m)
    {
        return [m](const T &n, const Project &) { return !(n.*m).empty(); };
    }

    /** A clip/transition/audio clip writes `timeline=` only when it differs from its track's:
     *  the redundant spelling would be a second way to write one fact, and a fixed point needs
     *  exactly one. */
    template <class T> Emit<T> explicitTimeline()
    {
        return [](const T &n, const Project &p) {
            if (n.timeline.empty()) return false;
            const Track *t = p.track(n.track);
            if (t) return t->timeline != n.timeline;
            const ATrack *a = p.audioTrack(n.track);
            return !a || a->timeline != n.timeline;
        };
    }

    template <class T> const std::vector<Field<T>> &fields();
    template <class T> const char *typeName();

    // ── header ──
    template <> inline const char *typeName<Project>() { return "header"; }
    template <> inline const std::vector<Field<Project>> &fields<Project>()
    {
        static const std::vector<Field<Project>> f = {
            integer<Project>("arstro-project", &Project::formatVersion, 1, 0, false),
            text<Project>("app", &Project::app, 0, false),
            text<Project>("id", &Project::id, 0, false),
            text<Project>("name", &Project::name, 0, false),
            number<Project>("fps", &Project::fps, 24.0, 0, false),
            integer<Project>("width", &Project::width, 3840, 0, false),
            integer<Project>("height", &Project::height, 2160, 0, false),
            number<Project>("par", &Project::par, 1.0, 0, false),
            text<Project>("colorspace", &Project::colorspace, 0, false),
            text<Project>("timebase", &Project::timebase, 0, false),
            integer<Project>("sampleRate", &Project::sampleRate, 48000, 0, false),
            number<Project>("masterGain", &Project::masterGain, 0.0, 0, false),
            ref<Project>("current", &Project::current, 0, false, nonEmpty<Project>(&Project::current)),
        };
        return f;
    }

    template <> inline const char *typeName<RackRef>() { return "rack"; }
    template <> inline const std::vector<Field<RackRef>> &fields<RackRef>()
    {
        static const std::vector<Field<RackRef>> f = {
            text<RackRef>("id", &RackRef::id, 0, false),
            text<RackRef>("path", &RackRef::path, 0, false),
            text<RackRef>("branch", &RackRef::branch, 0, false),
            text<RackRef>("writeBranch", &RackRef::writeBranch, 0, false, nonEmpty<RackRef>(&RackRef::writeBranch)),
        };
        return f;
    }

    template <> inline const char *typeName<RackObj>() { return "rackobj"; }
    template <> inline const std::vector<Field<RackObj>> &fields<RackObj>()
    {
        static const std::vector<Field<RackObj>> f = {
            text<RackObj>("id", &RackObj::id, 0, false),
            text<RackObj>("name", &RackObj::name, 0, false, nonEmpty<RackObj>(&RackObj::name)),
            text<RackObj>("node", &RackObj::node, 0, false),
            text<RackObj>("kind", &RackObj::kind, 0, false),
            number<RackObj>("weight", &RackObj::weight, 1.0, 0, false),
            text<RackObj>("media", &RackObj::media, 1, false, nonEmpty<RackObj>(&RackObj::media)),
            time<RackObj>("frame", &RackObj::frame, 0.0, 1, false,
                          [](const RackObj &r, const Project &) { return !r.media.empty() || r.frame != 0.0; }),
            text<RackObj>("input", &RackObj::input, 1, false,
                          [](const RackObj &r, const Project &) { return !r.input.empty() && r.input != "rec709"; }),
            text<RackObj>("lut", &RackObj::lut, 1, false, nonEmpty<RackObj>(&RackObj::lut)),
        };
        return f;
    }

    template <> inline const char *typeName<Timeline>() { return "timeline"; }
    template <> inline const std::vector<Field<Timeline>> &fields<Timeline>()
    {
        // colour/cut are written on every DERIVED timeline even at their default, because on a
        // version "follow" is a statement the reader needs; on a root they mean nothing.
        static const std::vector<Field<Timeline>> f = {
            text<Timeline>("id", &Timeline::id, 0, false),
            text<Timeline>("name", &Timeline::name, 0, false, nonEmpty<Timeline>(&Timeline::name)),
            ref<Timeline>("base", &Timeline::base, 0, false, nonEmpty<Timeline>(&Timeline::base)),
            text<Timeline>("colour", &Timeline::colour, 0, false,
                           [](const Timeline &t, const Project &) { return !t.base.empty() || t.colour != "follow"; }),
            text<Timeline>("cut", &Timeline::cut, 0, false,
                           [](const Timeline &t, const Project &) { return !t.base.empty() || t.cut != "follow"; }),
            integer<Timeline>("order", &Timeline::order, 0, 0, false),
        };
        return f;
    }

    template <> inline const char *typeName<Track>() { return "track"; }
    template <> inline const std::vector<Field<Track>> &fields<Track>()
    {
        static const std::vector<Field<Track>> f = {
            text<Track>("id", &Track::id, 0, false),
            text<Track>("name", &Track::name, 0, false, nonEmpty<Track>(&Track::name)),
            ref<Track>("timeline", &Track::timeline, 0, false),
            text<Track>("kind", &Track::kind, 0, false),
            integer<Track>("order", &Track::order, 0, 0, true),
            boolean<Track>("mute", &Track::mute, false, 0, true, [](const Track &t, const Project &) { return t.mute; }),
            number<Track>("opacity", &Track::opacity, 1.0, 0, true,
                          [](const Track &t, const Project &) { return !t.audio() || t.opacity != 1.0; }),
            text<Track>("blend", &Track::blend, 0, true,
                        [](const Track &t, const Project &) { return !t.audio() || t.blend != "normal"; }),
            number<Track>("gain", &Track::gain, 0.0, 0, true,
                          [](const Track &t, const Project &) { return t.audio() || t.gain != 0.0; }),
            ref<Track>("from", &Track::from, 0, false, nonEmpty<Track>(&Track::from)),
        };
        return f;
    }

    template <> inline const char *typeName<Clip>() { return "clip"; }
    template <> inline const std::vector<Field<Clip>> &fields<Clip>()
    {
        static const std::vector<Field<Clip>> f = {
            text<Clip>("id", &Clip::id, 0, false),
            text<Clip>("name", &Clip::name, 0, false, nonEmpty<Clip>(&Clip::name)),
            ref<Clip>("track", &Clip::track, 0, true),
            ref<Clip>("timeline", &Clip::timeline, 0, false, explicitTimeline<Clip>()),
            integer<Clip>("order", &Clip::order, 0, 0, true),
            ref<Clip>("src", &Clip::src, 0, true),
            time<Clip>("at", &Clip::at, 0.0, 1, true),
            time<Clip>("in", &Clip::in, 0.0, 1, true),
            time<Clip>("out", &Clip::out, 0.0, 1, true),
            number<Clip>("speed", &Clip::speed, 1.0, 1, true),
            integer<Clip>("angle", &Clip::angle, 0, 1, true, [](const Clip &c, const Project &) { return c.angle != 0; }),   // R-EDT-5
            text<Clip>("fit", &Clip::fit, 1, true),
            number<Clip>("opacity", &Clip::opacity, 1.0, 1, true),
            text<Clip>("blend", &Clip::blend, 1, true),
            geom("geom.x", &Geom::x, 0.0, 2),
            geom("geom.y", &Geom::y, 0.0, 2),
            geom("geom.scale", &Geom::scale, 1.0, 2),
            geom("geom.rotation", &Geom::rotation, 0.0, 2),
            geom("geom.anchor.x", &Geom::anchorX, 0.5, 3),
            geom("geom.anchor.y", &Geom::anchorY, 0.5, 3),
            geom("geom.crop.x", &Geom::cropX, 0.0, 4),
            geom("geom.crop.y", &Geom::cropY, 0.0, 4),
            geom("geom.crop.w", &Geom::cropW, 1.0, 4),
            geom("geom.crop.h", &Geom::cropH, 1.0, 4),
            ref<Clip>("from", &Clip::from, 4, false, nonEmpty<Clip>(&Clip::from)),
        };
        return f;
    }

    inline Field<Transition> between()
    {
        Field<Transition> f;
        f.key = "between"; f.kind = Kind::Pair; f.line = 0; f.editable = true;
        f.text = [](const Transition &t) { return t.clipA + "," + t.clipB; };
        f.setText = [](Transition &t, const std::string &v) {
            const auto comma = v.find(',');
            t.clipA = comma == std::string::npos ? v : v.substr(0, comma);
            t.clipB = comma == std::string::npos ? std::string() : v.substr(comma + 1);
        };
        return f;
    }

    template <> inline const char *typeName<Transition>() { return "transition"; }
    template <> inline const std::vector<Field<Transition>> &fields<Transition>()
    {
        static const std::vector<Field<Transition>> f = {
            text<Transition>("id", &Transition::id, 0, false),
            text<Transition>("name", &Transition::name, 0, false, nonEmpty<Transition>(&Transition::name)),
            ref<Transition>("track", &Transition::track, 0, false),
            ref<Transition>("timeline", &Transition::timeline, 0, false, explicitTimeline<Transition>()),
            between(),
            text<Transition>("kind", &Transition::kind, 0, true),
            time<Transition>("dur", &Transition::dur, 0.5, 0, true),
            ref<Transition>("from", &Transition::from, 0, false, nonEmpty<Transition>(&Transition::from)),
        };
        return f;
    }

    template <> inline const char *typeName<Marker>() { return "marker"; }
    template <> inline const std::vector<Field<Marker>> &fields<Marker>()
    {
        static const std::vector<Field<Marker>> f = {
            text<Marker>("id", &Marker::id, 0, false),
            text<Marker>("name", &Marker::name, 0, false, nonEmpty<Marker>(&Marker::name)),
            ref<Marker>("timeline", &Marker::timeline, 0, false),
            time<Marker>("at", &Marker::at, 0.0, 0, true),
            text<Marker>("note", &Marker::note, 0, true, nonEmpty<Marker>(&Marker::note)),
            ref<Marker>("from", &Marker::from, 0, false, nonEmpty<Marker>(&Marker::from)),
        };
        return f;
    }

    template <> inline const char *typeName<ATrack>() { return "atrack"; }
    template <> inline const std::vector<Field<ATrack>> &fields<ATrack>()
    {
        static const std::vector<Field<ATrack>> f = {
            text<ATrack>("id", &ATrack::id, 0, false),
            text<ATrack>("name", &ATrack::name, 0, false, nonEmpty<ATrack>(&ATrack::name)),
            ref<ATrack>("timeline", &ATrack::timeline, 0, false),
            text<ATrack>("kind", &ATrack::kind, 0, false),
            integer<ATrack>("order", &ATrack::order, 0, 0, true),
            number<ATrack>("gain", &ATrack::gain, 0.0, 0, true),
            number<ATrack>("pan", &ATrack::pan, 0.0, 0, true),
            boolean<ATrack>("mute", &ATrack::mute, false, 0, true),
            boolean<ATrack>("solo", &ATrack::solo, false, 0, true),
            ref<ATrack>("out", &ATrack::out, 0, true, nonEmpty<ATrack>(&ATrack::out)),
            ref<ATrack>("from", &ATrack::from, 0, false, nonEmpty<ATrack>(&ATrack::from)),
        };
        return f;
    }

    template <> inline const char *typeName<AClip>() { return "aclip"; }
    template <> inline const std::vector<Field<AClip>> &fields<AClip>()
    {
        static const std::vector<Field<AClip>> f = {
            text<AClip>("id", &AClip::id, 0, false),
            text<AClip>("name", &AClip::name, 0, false, nonEmpty<AClip>(&AClip::name)),
            ref<AClip>("track", &AClip::track, 0, true),
            ref<AClip>("timeline", &AClip::timeline, 0, false, explicitTimeline<AClip>()),
            text<AClip>("src", &AClip::src, 0, true),
            time<AClip>("at", &AClip::at, 0.0, 0, true),
            time<AClip>("in", &AClip::in, 0.0, 0, true),
            time<AClip>("out", &AClip::out, 0.0, 0, true),
            number<AClip>("gain", &AClip::gain, 0.0, 1, true),
            time<AClip>("fadeIn", &AClip::fadeIn, 0.0, 1, true),
            time<AClip>("fadeOut", &AClip::fadeOut, 0.0, 1, true),
            boolean<AClip>("loop", &AClip::loop, false, 1, true),
            ref<AClip>("from", &AClip::from, 1, false, nonEmpty<AClip>(&AClip::from)),
        };
        return f;
    }

    template <> inline const char *typeName<Effect>() { return "effect"; }
    template <> inline const std::vector<Field<Effect>> &fields<Effect>()
    {
        static const std::vector<Field<Effect>> f = {
            text<Effect>("id", &Effect::id, 0, false),
            ref<Effect>("node", &Effect::node, 0, false),
            text<Effect>("type", &Effect::type, 0, false),
            integer<Effect>("order", &Effect::order, 0, 0, true),
            boolean<Effect>("enabled", &Effect::enabled, true, 0, true),
            number<Effect>("mix", &Effect::mix, 1.0, 0, true),
        };
        return f;
    }

    template <> inline const char *typeName<Anim>() { return "anim"; }
    template <> inline const std::vector<Field<Anim>> &fields<Anim>()
    {
        static const std::vector<Field<Anim>> f = {
            text<Anim>("id", &Anim::id, 0, false),
            ref<Anim>("node", &Anim::node, 0, false),
            text<Anim>("key", &Anim::key, 0, false),
        };
        return f;
    }

    template <> inline const char *typeName<AnimKey>() { return "key"; }
    template <> inline const std::vector<Field<AnimKey>> &fields<AnimKey>()
    {
        // a side's speed and influence are written only when that side is a bezier
        auto bezIn = [](const AnimKey &k, const Project &) { return k.in == "bezier"; };
        auto bezOut = [](const AnimKey &k, const Project &) { return k.out == "bezier"; };
        static const std::vector<Field<AnimKey>> f = {
            ref<AnimKey>("anim", &AnimKey::anim, 0, false),
            time<AnimKey>("t", &AnimKey::t, 0.0, 0, false),
            number<AnimKey>("v", &AnimKey::v, 0.0, 0, false),
            text<AnimKey>("in", &AnimKey::in, 0, false, [](const AnimKey &k, const Project &) { return k.in != "linear"; }),
            text<AnimKey>("out", &AnimKey::out, 0, false, [](const AnimKey &k, const Project &) { return k.out != "linear"; }),
            number<AnimKey>("speedIn", &AnimKey::speedIn, 0.0, 0, false, bezIn),
            number<AnimKey>("inflIn", &AnimKey::inflIn, 33.333, 0, false, bezIn),
            number<AnimKey>("speedOut", &AnimKey::speedOut, 0.0, 0, false, bezOut),
            number<AnimKey>("inflOut", &AnimKey::inflOut, 33.333, 0, false, bezOut),
            text<AnimKey>("shape", &AnimKey::shape, 0, false, [](const AnimKey &k, const Project &) { return !k.shape.empty(); }),
        };
        return f;
    }

    template <> inline const char *typeName<Fx>() { return "fx"; }
    template <> inline const std::vector<Field<Fx>> &fields<Fx>()
    {
        // A type parameter is written for its own type, and for any other only when it carries a
        // non-default value — so nothing a file said is ever dropped.
        static const std::vector<Field<Fx>> f = {
            text<Fx>("id", &Fx::id, 0, false),
            ref<Fx>("node", &Fx::node, 0, false, nonEmpty<Fx>(&Fx::node)),
            ref<Fx>("clip", &Fx::clip, 0, false, nonEmpty<Fx>(&Fx::clip)),
            text<Fx>("type", &Fx::type, 0, false),
            integer<Fx>("radius", &Fx::radius, 0, 0, false,
                        [](const Fx &x, const Project &) { return x.type != "freeze" || x.radius != 0; }),
            number<Fx>("strength", &Fx::strength, 0.5, 0, false,
                       [](const Fx &x, const Project &) { return x.type == "denoise" || x.strength != 0.5; }),
            number<Fx>("shutter", &Fx::shutter, 180.0, 0, false,
                       [](const Fx &x, const Project &) { return x.type == "blend" || x.shutter != 180.0; }),
            time<Fx>("at", &Fx::at, 0.0, 0, false,
                     [](const Fx &x, const Project &) { return x.type == "freeze" || x.at != 0.0; }),
        };
        return f;
    }

    // ── reading and writing one field ────────────────────────────────────────────────────────

    template <class T> const Field<T> *find(const std::string &key)
    {
        for (const auto &f : fields<T>())
            if (f.key == key) return &f;
        return nullptr;
    }

    template <class T> std::string fieldText(const Field<T> &f, const T &n)
    {
        switch (f.kind)
        {
            case Kind::Text: case Kind::Ref: case Kind::Pair: return f.text(n);
            case Kind::Number: return canonicalNumber(f.num(n));
            case Kind::Time: return canonicalTime(f.num(n));
            case Kind::Int: return std::to_string((long long)f.num(n));
            case Kind::Bool: return f.num(n) != 0.0 ? "true" : "false";
        }
        return {};
    }

    inline bool parseInt(const std::string &v, long long &out)
    {
        double d;
        if (!parseNumber(v, d) || d != (double)(long long)d || d < -2147483648.0 || d > 2147483647.0) return false;
        out = (long long)d;
        return true;
    }
    inline bool parseBool(const std::string &v, bool &out)
    {
        if (v == "true" || v == "1" || v == "yes") { out = true; return true; }
        if (v == "false" || v == "0" || v == "no") { out = false; return true; }
        return false;
    }

    /** Set field `f` of `n` from text. STRICT (a command): a bad value is refused with `err`.
     *  LENIENT (a file): a bad value is repaired to the field default and counted — the two
     *  differ on purpose (project-format §7's last row). */
    template <class T>
    bool readField(const Field<T> &f, T &n, const std::string &v, bool strict, int *repaired, std::string &err)
    {
        auto bad = [&](const char *what) {
            if (strict)
            {
                err = "'" + f.key + "' takes " + what + ", not '" + v + "'";
                return false;
            }
            if (repaired) ++*repaired;
            if (f.setNum) f.setNum(n, f.def);
            return true;
        };
        switch (f.kind)
        {
            case Kind::Text: case Kind::Ref: case Kind::Pair:
                f.setText(n, v);
                return true;
            case Kind::Number: case Kind::Time:
            {
                double d;
                if (!parseNumber(v, d)) return bad("a finite number");
                f.setNum(n, d);
                return true;
            }
            case Kind::Int:
            {
                long long i;
                if (!parseInt(v, i)) return bad("an integer");
                f.setNum(n, (double)i);
                return true;
            }
            case Kind::Bool:
            {
                bool b;
                if (!parseBool(v, b)) return bad("true or false");
                f.setNum(n, b ? 1.0 : 0.0);
                return true;
            }
        }
        return true;
    }

    /** Apply `key=value` pairs leniently: a known key sets its field, an unknown one replaces or
     *  joins the node's unknown keys in place. Shared by the parser and `#tlset` resolution, so a
     *  delta applies exactly the way the file would have been read. Colour refusal is the
     *  caller's job, before this runs: by here every key is one the node may carry. */
    template <class T> void applyLenient(T &n, const Fields &kv, int *repaired)
    {
        std::string ignored;
        for (const auto &p : kv)
        {
            if (const Field<T> *f = find<T>(p.first))
            {
                readField(*f, n, p.second, false, repaired, ignored);
                continue;
            }
            bool hit = false;
            for (auto &u : n.unknown)
                if (u.first == p.first) { u.second = p.second; hit = true; break; }
            if (!hit) n.unknown.push_back(p);
        }
    }

    template <class T> std::string keyList()
    {
        std::string s;
        for (const auto &f : fields<T>())
            if (f.editable) s += (s.empty() ? "" : ", ") + f.key;
        return s;
    }
}
}
}
