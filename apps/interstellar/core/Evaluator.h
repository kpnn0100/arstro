/*
 *  interstellar_core — Evaluator: steps 1-4 of the frame pipeline (R-EVAL).
 *
 *      1  resolve time        t -> frame index, per-clip local time, source frame via speed
 *      2  automation          every autolink active at t -> its address's automated value
 *      3  bindings            topological order over the DAG; reads see step 2's values
 *      4  rack composition    a rack node's own values, composed up its ancestors
 *
 *  **The order is a contract, not an implementation detail** (binding.md §6). Automation before
 *  bindings is what lets a binding read an automated value; both before composition is what
 *  stops a group's offsets being composed from a value that was going to change. Any other
 *  order produces a different picture, so a golden test guards it.
 *
 *  **`resolve` is PURE.** No global state, no cache that survives a parameter change, no
 *  dependence on evaluation history. R-NFR-1's determinism rests entirely on that, and so does
 *  every golden-frame test — which is why the frame cache's key is a hash of what this returns
 *  rather than of the parameter struct it came from (R-PLAY-3).
 *
 *  **One authority per address** (R-EVAL-1): a binding, else an active automation link, else the
 *  static value. The model REFUSES configurations where two apply, so this function never has to
 *  choose between them — `BindingGraph::set` and `Automation::addLink` are where that is enforced.
 */
#pragma once
#include "Automation.h"
#include "BindingGraph.h"
#include "ParamRegistry.h"
#include "Project.h"
#include "RackAccess.h"
#include <map>
#include <string>
#include <vector>

namespace arstro
{
namespace interstellar
{
    /** Every address the frame used, and what it resolved to. Published in the model so a
     *  number the renderer used can be read back (R-EVAL-3) — the R-CPU-4 lesson, which this
     *  app would otherwise repeat several hundred times per frame. */
    struct ResolvedValues
    {
        std::map<std::string, double> values;
        double t = 0;
        long long frame = 0;
        bool has(const std::string &a) const { return values.count(a) != 0; }
        double get(const std::string &a, double fallback = 0.0) const
        {
            auto it = values.find(a);
            return it == values.end() ? fallback : it->second;
        }
    };

    class Evaluator
    {
    public:
        Evaluator(const Project &p, const RackAccess *rack, const Automation &au,
                  const BindingGraph &bg)
            : mP(p), mRack(rack), mAuto(au), mBind(bg) {}

        /** Steps 1-3 for every address that has a producer, plus every address any expression
         *  reads. Pure. */
        ResolvedValues resolve(double t) const;

        /** One address at `t`. `trace`, when given, collects each input and the intermediate —
         *  an expression whose value a user cannot account for is a support problem, and a cut
         *  accumulates hundreds of them (R-BIND-7). */
        bool value(const std::string &address, double t, double &out,
                   std::vector<std::string> *trace = nullptr) const;

        /** The STATIC value of an address: the project's own field, or the rack's. This is what
         *  `add`/`multiply` compose onto and what a fade ramps from. */
        bool staticValue(const std::string &address, double &out) const;

        /** Which clips are live at `t`, bottom track first — the composite order. */
        struct ActiveClip
        {
            const Clip *clip = nullptr;
            const Track *track = nullptr;
            double localTime = 0;      // seconds into the clip's source
            long long sourceFrame = 0; // nearest-neighbour (R-CUT-6)
            double progress = 0;       // 0..1 through the clip
        };
        std::vector<ActiveClip> activeAt(double t) const;

        /** Step 4: the rack node's own resolved values folded up its ancestors, each ancestor
         *  weighted by its grade weight and skipped when bypassed. The fold itself is COSMO's
         *  `composeParams` in the real render path; this returns the per-leaf numbers so the
         *  same arithmetic is assertable with no engine. */
        std::map<std::string, double> effectiveRackParams(const std::string &cosmoNode,
                                                          const ResolvedValues &rv) const;

        /** The frame-cache key's parameter component: a hash over every value that fed this
         *  layer. Hashing only the parameter struct would miss a change to a SHAPE that a link
         *  maps into it, which is the exact stale-frame bug the key exists to prevent. */
        uint64_t paramHash(const Clip &c, const ResolvedValues &rv) const;

    private:
        const Project &mP;
        const RackAccess *mRack;
        const Automation &mAuto;
        const BindingGraph &mBind;
    };
}
}
