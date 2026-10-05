/*
 *  interstellar_core — keyframes (R-ANIM).
 *
 *  A curve animates one parameter of one node: a rack node's colour key or an effect's parameter in
 *  SOURCE time (the footage's own clock — every clip of a source sees the same animation at the
 *  same source frame, and the reference-frame slider walks it), or a clip's opacity and geometry on
 *  the CLIP's own footage clock (in-point + offset × speed: a moved, trimmed or split clip keeps
 *  every key on the frame it was set on). The model evaluates (model/Anim.h); this file
 *  routes addresses to curves, runs `key add|remove|set|clear`, and applies curves on the render
 *  path. Law 2 holds: a curve is not a scalar, so the rack's and the effects' curves are the BASE's
 *  and a derived version inherits them live — its `#tlgrade` deltas add on top — and a pinned
 *  version reads the curves its pin snapshotted.
 */
#include "ServiceInternal.h"
#include "ParamRegistry.h"
#include "Effects.h"
#include "Project.h"
#include "Schema.h"
#include "Versions.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <sstream>

namespace arstro
{
namespace interstellar
{
    using EK = Event::Kind;
    using CK = Command::Kind;

    namespace
    {
        /** Times are written to the millisecond (project-format: "times to three places"), so a key
         *  is placed there too — a key at 1.0416̅ would read back at 1.042 and miss itself. */
        double msRound(double t) { return std::round(t * 1000.0) / 1000.0; }

        bool parseDouble(const std::string &s, double &out)
        {
            if (s.empty()) return false;
            char *end = nullptr;
            out = std::strtod(s.c_str(), &end);
            return end && *end == '\0' && std::isfinite(out);
        }

        std::pair<std::string, std::string> splitFirst(std::string a)
        {
            for (const char *prefix : {"rack:", "clip:", "fx:"})
                if (a.rfind(prefix, 0) == 0) a = a.substr(std::strlen(prefix));
            const auto dot = a.find('.');
            return {a.substr(0, dot), dot == std::string::npos ? std::string() : a.substr(dot + 1)};
        }

        const std::set<std::string> &clipAnimatable()
        {
            static const std::set<std::string> k{"opacity", "geom.x", "geom.y", "geom.scale", "geom.rotation", "geom.anchor.x",
                                                  "geom.anchor.y", "geom.crop.x", "geom.crop.y", "geom.crop.w", "geom.crop.h"};
            return k;
        }

        AnimKey toNode(const NodeId &animId, const anim::Key &k)
        {
            AnimKey n;
            n.anim = animId;
            n.t = k.t;
            n.v = k.v;
            n.in = anim::sideName(k.in);
            n.out = anim::sideName(k.out);
            n.speedIn = k.speedIn;
            n.speedOut = k.speedOut;
            n.inflIn = k.inflIn;
            n.inflOut = k.inflOut;
            return n;
        }

        /** "0.30000000000000004" is noise in a committed file — store what a float can hold. */
        double tidy(double v)
        {
            char buf[32];
            std::snprintf(buf, sizeof buf, "%.7g", v);
            return std::strtod(buf, nullptr);
        }
    }

    // ── where "now" is on a curve's clock ───────────────────────────────────────────────────────

    double InterstellarService::sourceNow(const NodeId &roId) const
    {
        // A source's clock stands where Grade shows it: its reference frame (R-RACK-3). A group has
        // no footage of its own — it stands where the Grade target does.
        const RackObj *ro = mProject->rackObj(roId);
        if (ro && !ro->media.empty()) return ro->frame;
        if (mModel.selectedRack >= 0 && mModel.selectedRack < (int)mModel.rack.size())
        {
            const RackObj *t = mProject->rackObj(mModel.rack[(size_t)mModel.selectedRack].rackObj);
            if (t && !t->media.empty()) return t->frame;
        }
        return 0.0;
    }

    bool InterstellarService::animTarget(const std::string &address, AnimTarget &out, std::string &why)
    {
        auto fail = [&](const std::string &w) { why = w; return false; };
        Project &P = *mProject;
        const auto parts = splitFirst(address);
        const NodeId id = P.idForRef(parts.first);
        const NodeKind kind = P.kindOf(id);
        out = AnimTarget{};
        out.node = id;
        if (kind == NodeKind::RackObj)
        {
            const RackObj *ro = P.rackObj(id);
            const auto dot = parts.second.find('.');
            const ParamDef *d = dot == std::string::npos ? nullptr : cosmoKey(parts.second.substr(dot + 1));
            if (!d || d->filter != parts.second.substr(0, dot))
                return fail("`" + address + "` is not an animatable parameter — a rack node animates its colour keys, <bind>.<filter>.<key>");
            // R-ANIM-6: a curve, a wheel or a crop animates as a SHAPE; a switch or a whole number does not
            const bool shape = d->kind == ParamKind::Points || d->kind == ParamKind::Triple || d->kind == ParamKind::Quad;
            if (d->kind != ParamKind::Scalar && !shape)
                return fail("`" + parts.second + "` is a " + std::string(d->kind == ParamKind::Int ? "whole number" : "switch") +
                            " — a number, a curve, a wheel or a crop has a curve; this does not");
            out.shape = shape;
            out.key = parts.second;
            out.cosmoKey = d->key;
            out.address = ro->name + "." + parts.second;
            out.owner = "rack";
            out.lo = d->min;
            out.hi = d->max;
            out.now = sourceNow(id);
            return true;
        }
        if (kind == NodeKind::Effect)
        {
            const Effect *e = P.effect(id);
            out.key = parts.second;
            out.address = e->id + "." + parts.second;
            out.owner = "effect";
            out.now = sourceNow(e->node);
            if (parts.second == "mix") { out.lo = 0; out.hi = 1; return true; }
            if (const render::EffectTypeDef *def = render::effectType(e->type))
                for (const auto &p : def->params)
                    if (p.key == parts.second) { out.lo = p.min; out.hi = p.max; return true; }
            return fail("`" + address + "` is not an animatable parameter of " + e->id + " (" + e->type + ") — its mix and its parameters are");
        }
        if (kind == NodeKind::Clip)
        {
            if (!clipAnimatable().count(parts.second))
                return fail("`" + address + "` is not animatable — a clip animates its opacity and geometry (geom.x, geom.scale, …), not its placement");
            ResolvedTimeline R;
            std::string err;
            if (!resolved(currentTimeline(), R, err)) return fail(err);
            const Clip *c = nullptr;
            for (const auto &x : R.clips) if (x.id == id) c = &x;
            if (!c) return fail(parts.first + " is not in this timeline");
            const ParamDef *d = ownerField(ParamOwner::Clip, parts.second);
            out.key = parts.second;
            out.address = c->name + "." + parts.second;
            out.owner = "clip";
            out.clipClock = true;
            out.lo = d ? d->min : -1e9;
            out.hi = d ? d->max : 1e9;
            // the clip's own footage clock (Premiere's and Resolve's): a move, a head trim or a split
            // leaves every key on the frame it was set on
            out.now = std::clamp(c->in + (mModel.playhead - c->at) * c->speed, c->in, c->out);
            const auto prov = R.provenance.find(id);
            out.inherited = prov != R.provenance.end() && prov->second != Provenance::Local;
            out.staticValue = schema::find<Clip>(parts.second)->num(*c);
            return true;
        }
        return fail("no node named `" + parts.first + "` with animatable parameters");
    }

    double InterstellarService::staticValue(const AnimTarget &t)
    {
        const Project &P = *mProject;
        if (t.owner == "clip") return t.staticValue;
        if (t.owner == "effect")
        {
            const Effect *e = P.effect(t.node);
            if (t.key == "mix") return e->mix;
            double v = 0;
            if (const render::EffectTypeDef *def = render::effectType(e->type))
                for (const auto &p : def->params) if (p.key == t.key) v = p.def;
            for (const auto &kv : e->unknown) if (kv.first == t.key) parseDouble(kv.second, v);
            return v;
        }
        // the rack: the node's own value as Cosmo holds it (the base's)
        ColourTree tree;
        std::map<NodeId, int> idx;
        std::string source, err;
        double v = 0;
        if (colourTreeFor(rootOf(currentTimeline()), tree, idx, source, err) && idx.count(t.node))
            paramScalar(tree[(size_t)idx[t.node]].own, t.cosmoKey, v);
        return v;
    }

    std::string InterstellarService::staticText(const AnimTarget &t)
    {
        // a shape's own text as Cosmo holds it (the base's)
        ColourTree tree;
        std::map<NodeId, int> idx;
        std::string source, err, v;
        if (colourTreeFor(rootOf(currentTimeline()), tree, idx, source, err) && idx.count(t.node))
            paramText(tree[(size_t)idx[t.node]].own, t.cosmoKey, v);
        return v;
    }

    NodeId InterstellarService::rootOf(const NodeId &tl) const
    {
        const auto chain = mProject->chain(tl);
        return chain.empty() ? tl : chain.back();
    }

    double InterstellarService::animatedValue(const AnimTarget &t)
    {
        const Anim *a = mProject->animOf(t.node, t.key);
        if (!a) return staticValue(t);
        const auto ks = mProject->keysOf(a->id);
        return ks.empty() ? staticValue(t) : anim::eval(ks, t.now);
    }

    // ── writing ─────────────────────────────────────────────────────────────────────────────────

    bool InterstellarService::curveEditable(const AnimTarget &t)
    {
        // Law 2 (R-ANIM-5): a curve is not a scalar delta, so the rack's and the effects' curves are
        // edited on the root timeline; a clip's on the timeline that owns the clip.
        const Timeline *tl = mProject->timeline(currentTimeline());
        if (t.owner == "clip")
        {
            if (t.inherited)
                return fail(t.address + " is the base's clip — its animation is the base's; key it there (R-ANIM-5)");
            return true;
        }
        if (tl && !tl->base.empty())
        {
            const Timeline *root = mProject->timeline(rootOf(tl->id));
            return fail("curves are the rack's and every version inherits them (R-ANIM-5) — key " + t.address + " on " +
                        (root ? root->name : std::string("the root timeline")) + "; a version adds a scalar override with `set`");
        }
        if (tl && tl->pinned()) return fail("timeline " + tl->name + " is pinned — its colour, curves included, is read-only (R-VER-3)");
        return true;
    }

    bool InterstellarService::upsertKey(const AnimTarget &t, double at, double v, const anim::Key *shape, const std::string *text)
    {
        Project &P = *mProject;
        if (t.shape && (!text || text->empty())) return fail(t.address + " is a shape: its key is a value in its own syntax, not a number");
        if (!t.shape && (v < t.lo || v > t.hi))
            return fail(t.address + " is " + canonicalNumber(t.lo) + ".." + canonicalNumber(t.hi) + ", got " + canonicalNumber(v));
        at = msRound(at);
        const Anim *a = P.animOf(t.node, t.key);
        NodeId animId;
        if (a) animId = a->id;
        else
        {
            Anim n;
            n.id = animId = P.freshId("an_");
            n.node = t.node;
            n.key = t.key;
            P.anims.push_back(n);
        }
        for (auto &k : P.animKeys)
            if (k.anim == animId && std::fabs(k.t - at) < 5e-4)
            {
                k.v = t.shape ? 0.0 : tidy(v);
                if (t.shape) k.shape = *text;
                if (shape)
                {
                    const AnimKey s = toNode(animId, *shape);
                    k.in = s.in; k.out = s.out; k.speedIn = s.speedIn; k.speedOut = s.speedOut; k.inflIn = s.inflIn; k.inflOut = s.inflOut;
                }
                return true;
            }
        anim::Key k;
        if (shape) k = *shape;
        k.t = at;
        k.v = t.shape ? 0.0 : tidy(v);
        AnimKey n = toNode(animId, k);
        if (t.shape) n.shape = *text;
        P.animKeys.push_back(n);
        return true;
    }

    /** The static value a curve leaves behind when it goes (After Effects' stopwatch: the value at
     *  the current time stays). */
    bool InterstellarService::writeStaticText(const AnimTarget &t, const std::string &text)
    {
        std::string err;
        const int node = cosmoNodeOf(t.node);
        if (node < 0) return fail(t.address + " is offline");
        return mRack.setParam(node, t.cosmoKey, text, err) || fail(t.address + ": " + err);
    }

    bool InterstellarService::writeStatic(const AnimTarget &t, double v)
    {
        Project &P = *mProject;
        std::string err;
        const std::string s = canonicalNumber(tidy(v));
        if (t.owner == "clip") return setField(P, currentTimeline(), t.node, t.key, s, err) || fail(err);
        if (t.owner == "effect")
        {
            Effect *e = P.effect(t.node);
            if (t.key == "mix") { e->mix = v; return true; }
            for (auto &kv : e->unknown) if (kv.first == t.key) { kv.second = s; return true; }
            e->unknown.emplace_back(t.key, s);
            return true;
        }
        const int node = cosmoNodeOf(t.node);
        if (node < 0) return fail(t.address + " is offline");
        return mRack.setParam(node, t.cosmoKey, s, err) || fail(t.address + ": " + err);
    }

    bool InterstellarService::setAnimated(const std::string &address, const std::string &value, bool &handled)
    {
        // R-ANIM-3: a parameter with a curve is keyed, not set — at the current time on its clock
        handled = false;
        const Project &P = *mProject;
        const auto parts = splitFirst(address);
        const NodeId id = P.idForRef(parts.first);
        if (!P.animOf(id, parts.second)) return true;
        AnimTarget t;
        std::string why;
        if (!animTarget(address, t, why)) return fail(why);
        const Timeline *tl = P.timeline(currentTimeline());
        if (t.owner == "rack" && tl && !tl->base.empty()) return true;   // a version: a delta on the animated value (setAddress)
        handled = true;
        double v = 0;
        if (!t.shape && !parseDouble(value, v)) return fail(address + " needs a number, got `" + value + "`");
        if (!curveEditable(t) || !upsertKey(t, t.now, v, nullptr, &value)) return false;
        markDirty();
        bumpFrame();
        emit(Event(EK::ParamsChanged).with("address", t.address).with("value", value).with("target", "curve"));
        emit(Event(EK::KeysChanged).with("address", t.address).with("keys", (int)mProject->keysOf(mProject->animOf(id, parts.second)->id).size()));
        return true;
    }

    bool InterstellarService::animCommand(const Command &c)
    {
        Project &P = *mProject;
        AnimTarget t;
        std::string why;
        if (!animTarget(c.arg(0), t, why)) return fail("key: " + why);
        if (!curveEditable(t)) return false;
        const Anim *a = P.animOf(t.node, t.key);
        const std::vector<anim::Key> before = a ? P.keysOf(a->id) : std::vector<anim::Key>{};
        double at = t.now;
        if (c.has("at"))
        {
            const std::string s = c.flag("at");   // flag() returns by value (D-10)
            if (!parseDouble(s, at)) return fail("key: --at is a time in seconds on " + t.address + "'s clock, got `" + s + "`");
        }
        at = msRound(at);
        auto keyAt = [&](double x) -> AnimKey * {
            if (!a) return nullptr;
            for (auto &k : P.animKeys)
                if (k.anim == a->id && std::fabs(k.t - x) < 5e-4) return &k;
            return nullptr;
        };
        auto number = [&](const char *flag, double &v) {
            const std::string s = c.flag(flag);
            if (!parseDouble(s, v)) return fail(std::string("key: --") + flag + " needs a number, got `" + s + "`");
            return true;
        };
        auto finish = [&](const std::string &what) {
            markDirty();
            bumpFrame();
            const Anim *now = P.animOf(t.node, t.key);
            const int n = now ? (int)P.keysOf(now->id).size() : 0;
            mOutput = t.address + ": " + what + " (" + std::to_string(n) + (n == 1 ? " key" : " keys") + ")\n";
            emit(Event(EK::KeysChanged).with("address", t.address).with("keys", n));
            return true;
        };

        switch (c.kind)
        {
            case CK::KeyAdd:
            {
                // the value: given, else what the parameter shows at that time now — so adding a key
                // never changes the picture
                double v = 0;
                std::string text;
                if (t.shape)
                {
                    const auto sk = a ? P.shapeKeysOf(a->id) : std::vector<anim::ShapeKey>{};
                    text = c.has("value") ? c.flag("value") : sk.empty() ? staticText(t) : anim::evalShape(sk, at);
                }
                else
                {
                    v = before.empty() ? staticValue(t) : anim::eval(before, at);
                    if (c.has("value") && !number("value", v)) return false;
                }
                anim::Key shape;
                const anim::Key *sp = nullptr;
                if (c.has("ease"))
                {
                    if (!anim::preset(shape, c.flag("ease"))) return fail("key add: --ease is linear, ease, ease-in, ease-out or hold, got " + c.flag("ease"));
                    sp = &shape;
                }
                if (!upsertKey(t, at, v, sp, &text)) return false;
                return finish("key at " + canonicalNumber(at) + " = " + (t.shape ? text : canonicalNumber(tidy(v))));
            }
            case CK::KeyRemove:
            {
                AnimKey *k = keyAt(at);
                if (!k) return fail("key remove: " + t.address + " has no key at " + canonicalNumber(at));
                if (before.size() == 1)
                {
                    // the last key: the curve goes and its value stays as the parameter's own
                    const std::string kept = t.shape ? k->shape : canonicalNumber(k->v);
                    if (!(t.shape ? writeStaticText(t, k->shape) : writeStatic(t, k->v))) return false;
                    P.dropAnim(a->id);
                    return finish("the last key removed — the value " + kept + " stays");
                }
                const NodeId animId = a->id;
                P.animKeys.erase(std::remove_if(P.animKeys.begin(), P.animKeys.end(),
                                                [&](const AnimKey &x) { return x.anim == animId && std::fabs(x.t - at) < 5e-4; }),
                                 P.animKeys.end());
                return finish("key at " + canonicalNumber(at) + " removed");
            }
            case CK::KeyClear:
            {
                if (!a) return fail("key clear: " + t.address + " is not animated");
                if (!(t.shape ? writeStaticText(t, anim::evalShape(P.shapeKeysOf(a->id), t.now)) : writeStatic(t, anim::eval(before, t.now))))
                    return false;
                P.dropAnim(a->id);
                return finish("animation removed — the value at " + canonicalNumber(t.now) + " stays");
            }
            case CK::KeySet:
            {
                AnimKey *k = keyAt(at);
                if (!k) return fail("key set: " + t.address + " has no key at " + canonicalNumber(at) + " — `key add` makes one");
                anim::Key x = before[0];
                for (const auto &b : before) if (std::fabs(b.t - at) < 5e-4) x = b;
                if (c.has("ease") && !anim::preset(x, c.flag("ease")))
                    return fail("key set: --ease is linear, ease, ease-in, ease-out or hold, got " + c.flag("ease"));
                for (const char *side : {"in", "out"})
                    if (c.has(side))
                    {
                        anim::Side s;
                        if (!anim::parseSide(c.flag(side), s)) return fail(std::string("key set: --") + side + " is linear, bezier or hold, got " + c.flag(side));
                        if (s == anim::Side::Hold && std::strcmp(side, "in") == 0) return fail("key set: a hold is an outgoing side — `--out hold`");
                        (std::strcmp(side, "in") == 0 ? x.in : x.out) = s;
                    }
                // a speed or an influence makes that side a bezier: that is what giving one means
                double v = 0;
                if (t.shape && (c.has("speed-in") || c.has("speed-out")))
                    return fail("key set: " + t.address + " is a shape — its sides ease by influence; a speed has no unit to be in");
                if (c.has("speed-in")) { if (!number("speed-in", v)) return false; x.speedIn = v; x.in = anim::Side::Bezier; }
                if (c.has("speed-out")) { if (!number("speed-out", v)) return false; x.speedOut = v; x.out = anim::Side::Bezier; }
                for (const char *f : {"influence-in", "influence-out"})
                    if (c.has(f))
                    {
                        if (!number(f, v)) return false;
                        if (v <= 0 || v > 100) return fail(std::string("key set: --") + f + " is a percent above 0 and at most 100, got " + canonicalNumber(v));
                        if (std::strcmp(f, "influence-in") == 0) { x.inflIn = v; x.in = anim::Side::Bezier; }
                        else { x.inflOut = v; x.out = anim::Side::Bezier; }
                    }
                if (t.shape && c.has("value")) k->shape = c.flag("value");
                else if (c.has("value") && !number("value", x.v)) return false;
                if (!t.shape && (x.v < t.lo || x.v > t.hi)) return fail(t.address + " is " + canonicalNumber(t.lo) + ".." + canonicalNumber(t.hi) + ", got " + canonicalNumber(x.v));
                double to = at;
                if (c.has("to"))
                {
                    if (!number("to", to)) return false;
                    to = msRound(to);
                    if (std::fabs(to - at) >= 5e-4 && keyAt(to)) return fail("key set: " + t.address + " already has a key at " + canonicalNumber(to));
                }
                const AnimKey n = toNode(a->id, x);
                k->t = to;
                k->v = t.shape ? 0.0 : tidy(x.v);
                k->in = n.in; k->out = n.out;
                k->speedIn = tidy(n.speedIn); k->speedOut = tidy(n.speedOut);
                k->inflIn = tidy(n.inflIn); k->inflOut = tidy(n.inflOut);
                return finish("key at " + canonicalNumber(to) + (to != at ? " (moved from " + canonicalNumber(at) + ")" : std::string()) +
                              " = " + (t.shape ? k->shape : canonicalNumber(k->v)) + ", in " + k->in + ", out " + k->out);
            }
            default: return fail("key: unknown verb");
        }
    }

    // ── keeping curves with their nodes ─────────────────────────────────────────────────────────

    void InterstellarService::pruneAnims()
    {
        // A node that went (rack remove, effect remove, a clip deleted on its own timeline) takes
        // its curves with it — a curve naming nothing would make the file refuse to open.
        Project &P = *mProject;
        std::vector<NodeId> gone;
        for (const auto &a : P.anims)
            if (!P.rackObj(a.node) && !P.effect(a.node) && !P.clip(a.node)) gone.push_back(a.node);
        for (const auto &n : gone) P.dropAnimsOf(n);
    }

    void InterstellarService::copyAnims(const NodeId &from, const NodeId &to, double shift)
    {
        Project &P = *mProject;
        std::vector<Anim> src;
        for (const auto &a : P.anims) if (a.node == from) src.push_back(a);
        for (const auto &a : src)
        {
            Anim n;
            n.id = P.freshId("an_");
            n.node = to;
            n.key = a.key;
            P.anims.push_back(n);
            std::vector<AnimKey> keys;
            for (const auto &k : P.animKeys) if (k.anim == a.id) keys.push_back(k);
            for (auto k : keys)
            {
                k.anim = n.id;
                k.t = msRound(k.t + shift);
                k.notes = Notes{};
                P.animKeys.push_back(k);
            }
        }
    }

    // ── the render path ─────────────────────────────────────────────────────────────────────────

    void InterstellarService::applyColourCurves(const NodeId &tl, ColourTree &tree, const std::map<NodeId, int> &idx, double srcT)
    {
        // each node's OWN value becomes its curve at the source time; the fold, the deltas and the
        // groups proceed exactly as for a still value
        const PinCurves *pin = pinCurvesFor(tl);
        for (const auto &kv : idx)
        {
            EditParams &own = tree[(size_t)kv.second].own;
            if (pin)
            {
                for (const auto &c : *pin)
                    if (c.node == kv.first)
                        setParamText(own, c.key.substr(c.key.find('.') + 1),
                                     c.shapes.empty() ? canonicalNumber(anim::eval(c.keys, srcT)) : anim::evalShape(c.shapes, srcT));
                continue;
            }
            for (const auto &a : mProject->anims)
            {
                if (a.node != kv.first) continue;
                const std::string k = a.key.substr(a.key.find('.') + 1);
                const auto sk = mProject->shapeKeysOf(a.id);
                if (!sk.empty()) { setParamText(own, k, anim::evalShape(sk, srcT)); continue; }   // R-ANIM-6
                const auto ks = mProject->keysOf(a.id);
                if (!ks.empty()) setParamText(own, k, canonicalNumber(anim::eval(ks, srcT)));
            }
        }
    }

    double InterstellarService::curveAt(const NodeId &node, const std::string &key, double t, double fallback) const
    {
        const Anim *a = mProject->animOf(node, key);
        if (!a) return fallback;
        const auto ks = mProject->keysOf(a->id);
        return ks.empty() ? fallback : anim::eval(ks, t);
    }

    // ── pins freeze curves too (R-VER-3) ─────────────────────────────────────────────────────────

    std::string InterstellarService::colourCurveText() const
    {
        // the rack's curves, one line per key, in file order — part of what a pin freezes and of
        // the commit it is named by
        std::ostringstream o;
        for (const auto &a : mProject->anims)
        {
            if (!mProject->rackObj(a.node)) continue;
            for (const auto &k : mProject->animKeys)
            {
                if (k.anim != a.id) continue;
                o << a.node << ' ' << a.key << ' ' << canonicalNumber(k.t) << ' ' << canonicalNumber(k.v) << ' ' << k.in << ' ' << k.out << ' '
                  << canonicalNumber(k.speedIn) << ' ' << canonicalNumber(k.inflIn) << ' ' << canonicalNumber(k.speedOut) << ' '
                  << canonicalNumber(k.inflOut) << ' ' << (k.shape.empty() ? std::string("-") : k.shape) << '\n';
            }
        }
        return o.str();
    }

    const InterstellarService::PinCurves *InterstellarService::pinCurvesFor(const NodeId &tl)
    {
        for (const NodeId &t : mProject->chain(tl))
        {
            const Timeline *x = mProject->timeline(t);
            if (!x || !x->pinned()) continue;
            const std::string commit = x->pinCommit();
            auto it = mPinCurves.find(commit);
            if (it == mPinCurves.end())
            {
                PinCurves pc;
                std::ifstream f(pinsDir() + "/" + commit + ".anim");
                std::string line;
                while (std::getline(f, line))
                {
                    std::istringstream s(line);
                    std::string node, key, in, out, shape;
                    anim::Key k;
                    if (!(s >> node >> key >> k.t >> k.v >> in >> out >> k.speedIn >> k.inflIn >> k.speedOut >> k.inflOut)) continue;
                    if (!(s >> shape)) shape = "-";   // a pin written before shapes keyed numbers only
                    anim::parseSide(in, k.in);
                    anim::parseSide(out, k.out);
                    PinCurve *c = nullptr;
                    for (auto &x2 : pc) if (x2.node == node && x2.key == key) c = &x2;
                    if (!c) { pc.push_back(PinCurve{node, key, {}, {}}); c = &pc.back(); }
                    if (shape != "-") c->shapes.push_back(anim::ShapeKey{k, shape});
                    else c->keys.push_back(k);
                }
                for (auto &c : pc)
                {
                    std::sort(c.keys.begin(), c.keys.end(), [](const anim::Key &a, const anim::Key &b) { return a.t < b.t; });
                    std::sort(c.shapes.begin(), c.shapes.end(), [](const anim::ShapeKey &a, const anim::ShapeKey &b) { return a.k.t < b.k.t; });
                }
                it = mPinCurves.emplace(commit, std::move(pc)).first;
            }
            return &it->second;
        }
        return nullptr;
    }

    // ── the model ───────────────────────────────────────────────────────────────────────────────

    void InterstellarService::fillAnimModel(AppModel &m)
    {
        m.anims.clear();
        if (!mOpen) return;
        const Project &P = *mProject;
        for (const auto &a : P.anims)
        {
            AnimModel am;
            am.id = a.id;
            am.node = a.node;
            am.key = a.key;
            std::string addr;
            if (const RackObj *ro = P.rackObj(a.node)) { am.owner = "rack"; am.nodeBind = ro->name; addr = ro->name + "." + a.key; }
            else if (const Effect *e = P.effect(a.node)) { am.owner = "effect"; am.nodeBind = e->id; addr = e->id + "." + a.key; }
            else if (const Clip *c = P.clip(a.node)) { am.owner = "clip"; am.nodeBind = c->name.empty() ? c->id : c->name; addr = am.nodeBind + "." + a.key; }
            am.address = addr;
            am.clock = am.owner == "clip" ? "clip" : "source";
            AnimTarget t;
            std::string why;
            if (!addr.empty() && animTarget(addr, t, why))
            {
                am.now = t.now;
                am.min = t.lo;
                am.max = t.hi;
            }
            const auto ks = P.keysOf(a.id);
            const auto sk = P.shapeKeysOf(a.id);
            am.shape = !sk.empty();
            am.value = ks.empty() || am.shape ? 0.0 : anim::eval(ks, am.now);
            if (am.shape) am.shapeNow = anim::evalShape(sk, am.now);
            for (const auto &k : ks)
            {
                KeyframeModel km;
                km.t = k.t;
                km.v = k.v;
                km.in = anim::sideName(k.in);
                km.out = anim::sideName(k.out);
                km.speedIn = k.speedIn;
                km.speedOut = k.speedOut;
                km.inflIn = k.inflIn;
                km.inflOut = k.inflOut;
                for (const auto &x : sk) if (std::fabs(x.k.t - k.t) < 5e-4) km.shape = x.shape;
                am.keys.push_back(km);
            }
            m.anims.push_back(std::move(am));
        }
    }
}
}
