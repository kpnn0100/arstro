/*
 *  interstellar_core — Automation: shapes, links, lanes and the boundary lint (R-AUTO).
 *
 *  An automation clip is a NAMED, REUSABLE SHAPE and not a property of a parameter. That one
 *  decision is what makes *"multi param can use the same automation"* true rather than
 *  maintained: one shape drives an EV ramp and a scale push, and moving one breakpoint moves
 *  both. A keyframe belongs to one field by construction, so nothing can be shared.
 *
 *  A LINK is what applies a shape to an address — a time placement, a value mapping, and a mode
 *  that says how the mapped value meets the address's static value. Links on one address may
 *  not overlap (R-AUTO-4): two producers for one value is a picture that depends on evaluation
 *  order, and a renderer whose output depends on evaluation order cannot be tested.
 */
#pragma once
#include "Project.h"
#include <cmath>
#include <string>
#include <vector>

namespace arstro
{
namespace interstellar
{
    /** A finding the renderer or the UI must surface rather than absorb. */
    struct LintFinding
    {
        std::string address, detail;
        NodeId node;
        double at = 0;
        int severity = 1;   // 1 = warn, 2 = error
    };

    class Automation
    {
    public:
        explicit Automation(Project &p) : mP(p) {}

        struct Sample { bool active = false; double value = 0; };

        /** The automated value of `address` at `t`, given its static value. Applies the time
         *  mapping, the value mapping, the mode and the fades, in that order. `active == false`
         *  means no link covers `t` and the address is its static value (R-AUTO-5). */
        Sample sample(const std::string &address, double t, double staticValue) const;

        /** Would a link at `at` for `dur` overlap an existing one on `address`? (R-AUTO-4) */
        bool wouldOverlap(const std::string &address, double at, double dur,
                          const NodeId &ignore = {}) const;

        /** Add a link, refusing an overlap and a missing shape. */
        bool addLink(const AutoLink &l, std::string &err);

        /** One lane per automated address of `objectName` — the mixer's projection. A VIEW of
         *  the links, never a stored structure (R-G-3). */
        struct Lane
        {
            std::string address;
            std::vector<const AutoLink *> links;
        };
        std::vector<Lane> lanes(const std::string &objectName) const;

        /** Every link whose first mapped value differs from the static value with `fadeIn == 0`
         *  — a step in the rendered image. A hard change the user asked for is legitimate; one
         *  they did not notice is a defect they will blame on the renderer (R-AUTO-5). The
         *  callback supplies each address's static value so this stays free of the evaluator. */
        template <class StaticFn>
        std::vector<LintFinding> lintBoundaries(StaticFn staticOf) const
        {
            std::vector<LintFinding> out;
            for (const auto &l : mP.autoLinks)
            {
                const AutoClip *shape = mP.autoClip(l.clip);
                if (!shape)
                {
                    out.push_back({l.target, "the shape '" + l.clip + "' does not exist", l.id, l.at, 2});
                    continue;
                }
                if (l.fadeIn != 0) continue;
                double sv = 0;
                if (!staticOf(l.target, sv)) continue;
                const double first = mapValue(l, shape->value(0.0), sv);
                const double eps = 1e-6;
                if (std::fabs(first - sv) > eps)
                    out.push_back({l.target,
                                   "steps by " + canonicalNumber(first - sv) +
                                       " on entry with fadeIn=0",
                                   l.id, l.at, 1});
            }
            return out;
        }

        /** The shape value mapped into the target's unit and combined with the static value by
         *  the link's mode. Public because the lint and the sampler must agree exactly. */
        static double mapValue(const AutoLink &l, double shapeV, double staticValue);

        /** Local time within a link's shape, and whether `t` is inside it. `clipAt` is the
         *  scoped clip's start when `l.scope` is set (R-AUTO-7). */
        static bool localTime(const AutoLink &l, const AutoClip &shape, double t, double clipAt,
                              double &localT);

    private:
        Project &mP;
    };
}
}
