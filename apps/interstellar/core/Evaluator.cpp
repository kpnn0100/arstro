#include "Evaluator.h"
#include <cmath>
#include <cstring>
#include <functional>

namespace arstro
{
namespace interstellar
{
    namespace
    {
        /** FNV-1a over the bytes of a double, so a parameter hash is stable across runs on one
         *  machine and cheap to accumulate. */
        void hashMix(uint64_t &h, double v)
        {
            unsigned char b[sizeof v];
            std::memcpy(b, &v, sizeof v);
            for (unsigned char c : b) { h ^= c; h *= 1099511628211ULL; }
        }
        void hashMix(uint64_t &h, const std::string &s)
        {
            for (unsigned char c : s) { h ^= c; h *= 1099511628211ULL; }
        }
    }

    bool Evaluator::staticValue(const std::string &address, double &out) const
    {
        ParamRegistry::Resolved r;
        std::string err;
        if (!ParamRegistry::resolve(mP, mRack, address, r, err)) return false;
        const auto &e = *r.entry;

        switch (r.obj)
        {
            case ParamRegistry::ObjKind::Project:
            {
                if (e.param == "fps") { out = mP.fps; return true; }
                if (e.param == "width") { out = mP.width; return true; }
                if (e.param == "height") { out = mP.height; return true; }
                if (e.param == "duration") { out = mP.duration(); return true; }
                if (e.param == "playhead") { out = 0.0; return true; }   // supplied per frame
                return false;
            }
            case ParamRegistry::ObjKind::Track:
            {
                const Track *t = mP.track(r.objectId);
                if (!t) return false;
                if (e.param == "opacity") { out = t->opacity; return true; }
                if (e.param == "gain") { out = t->gain; return true; }
                if (e.param == "blend") { out = (double)(int)t->blend; return true; }
                if (e.param == "mute") { out = t->mute ? 1 : 0; return true; }
                return false;
            }
            case ParamRegistry::ObjKind::Clip:
            {
                const Clip *c = mP.clip(r.objectId);
                if (!c) return false;
                if (e.filter == "geom")
                {
                    const Geom &g = c->geom;
                    if (e.param == "x") { out = g.x; return true; }
                    if (e.param == "y") { out = g.y; return true; }
                    if (e.param == "scale") { out = g.scale; return true; }
                    if (e.param == "rotation") { out = g.rotation; return true; }
                    if (e.param == "anchor.x") { out = g.anchorX; return true; }
                    if (e.param == "anchor.y") { out = g.anchorY; return true; }
                    if (e.param == "crop.x") { out = g.cropX; return true; }
                    if (e.param == "crop.y") { out = g.cropY; return true; }
                    if (e.param == "crop.w") { out = g.cropW; return true; }
                    if (e.param == "crop.h") { out = g.cropH; return true; }
                    return false;
                }
                if (e.param == "opacity") { out = c->opacity; return true; }
                if (e.param == "speed") { out = c->speed; return true; }
                if (e.param == "at") { out = c->at; return true; }
                if (e.param == "in") { out = c->in; return true; }
                if (e.param == "out") { out = c->out; return true; }
                if (e.param == "blend") { out = (double)(int)c->blend; return true; }
                if (e.param == "fit") { out = (double)(int)c->fit; return true; }
                return false;
            }
            case ParamRegistry::ObjKind::AutoClip:
            {
                const AutoClip *a = mP.autoClip(r.objectId);
                if (!a) return false;
                if (e.param == "dur") { out = a->dur; return true; }
                if (e.param == "value") { out = a->value(0.0); return true; }
                return false;
            }
            case ParamRegistry::ObjKind::Rack:
            {
                if (e.owner == ParamRegistry::Owner::Interstellar)
                {
                    // The grade weight lives in the .isp, because the .cmp owns colour and only
                    // colour (R-COSMO-2).
                    const RackObj *ro = mP.rackObjForNode(r.objectId);
                    if (!ro) return false;
                    if (e.param == "opacity") { out = ro->opacity; return true; }
                    return false;
                }
                if (!mRack) return false;
                return mRack->getParam(r.objectId, e.key, out);
            }
        }
        return false;
    }

    std::vector<Evaluator::ActiveClip> Evaluator::activeAt(double t) const
    {
        std::vector<ActiveClip> out;
        for (const auto &c : mP.clips)
        {
            const Track *tr = mP.track(c.track);
            if (!tr || tr->audio) continue;

            // A transition covering `t` decides two things: the weight each side contributes, and
            // whether the OUTGOING side is live at all past its own out-point (R-CUT-4a).
            double weight = 1.0;
            bool held = false;
            bool live = t >= c.at && t < c.end();
            for (const auto &x : mP.transitions)
            {
                if (x.dur <= 0) continue;
                const Clip *in = mP.clip(x.clipB);
                if (!in) continue;
                const double start = in->at, stop = in->at + x.dur;
                if (t < start || t >= stop) continue;
                const double f = applyEase(x.easing, (t - start) / x.dur);
                if (c.id == x.clipB) weight *= f;                 // incoming: 0 -> 1
                else if (c.id == x.clipA)
                {
                    weight *= 1.0 - f;                            // outgoing: 1 -> 0
                    if (!live) { live = true; held = true; }       // held past its out-point
                }
            }
            if (!live) continue;

            ActiveClip a;
            a.clip = &c;
            a.track = tr;
            // Clip-local time, then the source frame by NEAREST NEIGHBOUR — stated because
            // R-CUT-6 forbids pretending it is interpolation. A held clip keeps counting past
            // `out` into its handles; the decoder clamps to the source's last frame when there
            // are none, which freezes rather than going black.
            a.localTime = (t - c.at) * c.speed + c.in;
            a.sourceFrame = (long long)std::floor(a.localTime * mP.fps);
            const double dur = c.duration();
            a.progress = dur > 0 ? (t - c.at) / dur : 0.0;
            a.transitionWeight = weight;
            a.heldByTransition = held;
            out.push_back(a);
        }
        // Bottom track first: that is the composite order, and `order` is the z-order.
        std::sort(out.begin(), out.end(), [](const ActiveClip &a, const ActiveClip &b) {
            if (a.track->order != b.track->order) return a.track->order < b.track->order;
            return a.clip->at < b.clip->at;
        });
        return out;
    }

    ResolvedValues Evaluator::resolve(double t) const
    {
        ResolvedValues rv;
        rv.t = t;
        rv.frame = mP.frameAt(t);

        // ── step 2: automation ───────────────────────────────────────────────────────────────
        // Every address a link targets gets its automated value; everything else stays static.
        for (const auto &l : mP.autoLinks)
        {
            if (rv.has(l.target)) continue;
            double sv = 0;
            if (!staticValue(l.target, sv)) continue;
            const Automation::Sample s = mAuto.sample(l.target, t, sv);
            rv.values[l.target] = s.active ? s.value : sv;
        }

        // An automation shape's own output at t, seeded into the map BEFORE the bindings run.
        // It used to be resolved by a special case inside the Gene scope, which meant the value
        // the expression used and the value `--explain` printed came from two different places —
        // and they disagreed: a binding resolving to 1.08 printed its input as 0.0. One authority
        // for one number (R-G-3), and the trace is now reading what the evaluation read.
        for (const auto &ac : mP.autoClips)
            rv.values[ac.name + ".value"] = ac.value(t);

        // Static values for every address an expression reads, so step 3 sees them.
        for (const auto &b : mBind.bindings())
            for (const auto &d : b.deps)
            {
                if (rv.has(d)) continue;
                double sv = 0;
                if (staticValue(d, sv)) rv.values[d] = sv;
            }

        // ── step 3: bindings, in topological order ───────────────────────────────────────────
        // Reads see step 2's values, which is what lets `1 + ac_push.value * 0.08` work.
        for (const auto *b : mBind.evaluationOrder())
        {
            gene::Scope sc;
            sc.w = mP.width;
            sc.h = mP.height;
            sc.lookupIdent = [&](const std::string &n, gene::Value &o) {
                if (n == "t") { o = gene::Value::number(t); return true; }
                if (n == "frame") { o = gene::Value::number((double)rv.frame); return true; }
                if (n == "fps") { o = gene::Value::number(mP.fps); return true; }
                if (n == "dur") { o = gene::Value::number(mP.duration()); return true; }
                return false;
            };
            sc.lookupPath = [&](const std::vector<std::string> &segs, gene::Value &o) {
                std::string a;
                for (size_t i = 0; i < segs.size(); ++i) { if (i) a += '.'; a += segs[i]; }
                // An automation clip's `value` is its output AT t — the explicit way to have a
                // curve and a calculation without two authorities (R-BIND-5).
                if (segs.size() == 2 && segs[1] == "local")
                    if (const Clip *c = mP.clip(segs[0]))
                    { o = gene::Value::number(std::max(0.0, t - c->at)); return true; }
                if (segs.size() == 2 && segs[1] == "progress")
                    if (const Clip *c = mP.clip(segs[0]))
                    {
                        const double d = c->duration();
                        o = gene::Value::number(d > 0 ? (t - c->at) / d : 0.0);
                        return true;
                    }
                auto it = rv.values.find(a);
                if (it != rv.values.end()) { o = gene::Value::number(it->second); return true; }
                double sv = 0;
                if (staticValue(a, sv)) { o = gene::Value::number(sv); return true; }
                return false;
            };

            gene::Value out;
            std::string err;
            if (gene::evaluate(b->ast, sc, out, &err) && !out.isColor)
                rv.values[b->target] = out.num;
            else
            {
                // A broken expression falls back to the static value and does NOT stop the
                // render: a master must not die on frame 4 800 because of a typo three weeks
                // old (R-BIND-8).
                double sv = 0;
                if (staticValue(b->target, sv)) rv.values[b->target] = sv;
            }
        }
        return rv;
    }

    bool Evaluator::value(const std::string &address, double t, double &out,
                          std::vector<std::string> *trace) const
    {
        ParamRegistry::Resolved r;
        std::string err;
        if (!ParamRegistry::resolve(mP, mRack, address, r, err)) return false;

        const ResolvedValues rv = resolve(t);
        if (address == "project.playhead") { out = t; return true; }
        if (trace)
        {
            if (const auto *b = mBind.forTarget(address))
            {
                trace->push_back("binding: " + b->expr);
                for (const auto &d : b->deps)
                    trace->push_back("  " + d + " = " + canonicalNumber(rv.get(d)));
            }
            else
            {
                double sv = 0;
                staticValue(address, sv);
                const Automation::Sample s = mAuto.sample(address, t, sv);
                trace->push_back("static: " + canonicalNumber(sv));
                if (s.active) trace->push_back("automation active -> " + canonicalNumber(s.value));
                else trace->push_back("no automation link covers t");
            }
        }
        if (rv.has(address)) { out = rv.get(address); return true; }
        return staticValue(address, out);
    }

    std::map<std::string, double> Evaluator::effectiveRackParams(const std::string &cosmoNode,
                                                                 const ResolvedValues &rv) const
    {
        std::map<std::string, double> eff;
        if (!mRack) return eff;

        // Walk the node up to the root, collecting each ancestor's OWN values, then fold from
        // the top down so a child's own value is added last. Cosmo's `composeParams` is the
        // authority in the real render path; this reproduces its scalar rule (offsets add) so
        // the arithmetic is assertable with no engine attached.
        const auto nodes = mRack->nodes();
        auto byId = [&](const std::string &id) -> const RackAccess::Node * {
            for (const auto &n : nodes)
                if (n.id == id) return &n;
            return nullptr;
        };
        std::vector<const RackAccess::Node *> chain;
        for (const RackAccess::Node *n = byId(cosmoNode); n;)
        {
            chain.push_back(n);
            if (n->parent < 0) break;
            const RackAccess::Node *p = nullptr;
            for (const auto &c : nodes)
                if (c.parent == n->parent && false) p = &c;   // parents are indices; resolve below
            // `parent` is an index into the flattened list, which is how EditSession reports it.
            if (n->parent >= 0 && n->parent < (int)nodes.size()) p = &nodes[(size_t)n->parent];
            n = p;
        }
        std::reverse(chain.begin(), chain.end());   // root-most first

        for (const RackAccess::Node *n : chain)
        {
            if (n->bypass) continue;   // a bypassed node contributes nothing (R-BYPASS)
            double weight = 1.0;
            if (const RackObj *ro = mP.rackObjForNode(n->id)) weight = ro->opacity;
            const std::string prefix = mP.rackObjForNode(n->id)
                                           ? mP.rackObjForNode(n->id)->name + "."
                                           : std::string();
            for (const auto &e : ParamRegistry::all())
            {
                if (e.obj != ParamRegistry::ObjKind::Rack) continue;
                if (e.owner != ParamRegistry::Owner::Cosmo) continue;
                if (e.type != ParamRegistry::Type::Float) continue;
                double own = e.def;
                const std::string addr = prefix + e.suffix();
                if (!prefix.empty() && rv.has(addr)) own = rv.get(addr);
                else if (!mRack->getParam(n->id, e.key, own)) own = e.def;
                // Scalars stack as OFFSETS from their neutral value, weighted by the node's
                // grade weight. The leaf itself is weighted too, which is what makes
                // `gr1.opacity` a continuous bypass rather than a layer opacity.
                const double offset = (own - e.def) * weight;
                auto it = eff.find(e.suffix());
                if (it == eff.end()) eff[e.suffix()] = e.def + offset;
                else it->second += offset;
            }
        }
        return eff;
    }

    uint64_t Evaluator::paramHash(const Clip &c, const ResolvedValues &rv) const
    {
        uint64_t h = 14695981039346656037ULL;
        hashMix(h, c.src);
        // Every value that fed this layer — not just the parameter struct, because a change to a
        // SHAPE a link maps in would otherwise leave the key unchanged and serve a stale frame.
        const auto eff = effectiveRackParams(c.src.rfind("rack:", 0) == 0 ? c.src.substr(5) : c.src, rv);
        for (const auto &kv : eff) { hashMix(h, kv.first); hashMix(h, kv.second); }
        for (const auto &suffix : {"opacity", "geom.x", "geom.y", "geom.scale", "geom.rotation",
                                   "geom.crop.x", "geom.crop.y", "geom.crop.w", "geom.crop.h"})
        {
            const std::string addr = c.name + "." + suffix;
            double v = 0;
            if (rv.has(addr)) v = rv.get(addr);
            else staticValue(addr, v);
            hashMix(h, addr);
            hashMix(h, v);
        }
        return h;
    }
}
}
