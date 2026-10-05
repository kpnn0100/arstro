/*
 *  interstellar_model — keyframe curves (R-ANIM-2). Header-only: the service evaluates them on the
 *  render path and the UI draws the same function in the graph editor, so the two cannot disagree.
 *
 *  After Effects' model, because it is the one editors already think in. Each keyframe has an
 *  incoming and an outgoing side; a side is LINEAR, BEZIER or HOLD. A bezier side has a SPEED
 *  (value units per second — the curve's slope as it leaves or arrives) and an INFLUENCE (how far
 *  into the segment that slope pulls, 0–100 %). Between key A and key B the curve is the cubic
 *  Bézier in (time, value):
 *
 *      P0 = (tA, vA)
 *      P1 = (tA + infOutA·Δt, vA + speedOutA·infOutA·Δt)
 *      P2 = (tB − infInB·Δt,  vB − speedInB·infInB·Δt)
 *      P3 = (tB, vB)
 *
 *  A linear side is a bezier side whose speed is the segment's own slope and whose influence is
 *  ⅓ — so two linear sides give exactly the straight line, and a lerp is used for them. A hold on
 *  A's outgoing side keeps vA until B. Before the first key the curve is the first value, after the
 *  last the last. With every control point inside [tA, tB] the time component is monotonic, so the
 *  value at t is found by bisection on the curve parameter.
 */
#pragma once
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace arstro
{
namespace interstellar
{
namespace anim
{
    enum class Side { Linear, Bezier, Hold };

    inline const char *sideName(Side s) { return s == Side::Bezier ? "bezier" : s == Side::Hold ? "hold" : "linear"; }
    inline bool parseSide(const std::string &s, Side &out)
    {
        if (s == "linear") out = Side::Linear;
        else if (s == "bezier") out = Side::Bezier;
        else if (s == "hold") out = Side::Hold;
        else return false;
        return true;
    }

    struct Key
    {
        double t = 0, v = 0;
        Side in = Side::Linear, out = Side::Linear;
        double speedIn = 0, speedOut = 0;            // value units per second
        double inflIn = 33.333, inflOut = 33.333;    // percent of the segment, 0.1 … 100
    };

    inline double clampInfl(double pct) { return std::clamp(pct, 0.1, 100.0) / 100.0; }

    /** The value of the segment from `a` to `b` at `t` (a.t <= t <= b.t). */
    inline double segment(const Key &a, const Key &b, double t)
    {
        const double dt = b.t - a.t;
        if (dt <= 0) return b.v;
        if (a.out == Side::Hold) return a.v;
        const double u0 = (t - a.t) / dt;
        if (a.out == Side::Linear && b.in == Side::Linear) return a.v + (b.v - a.v) * u0;
        const double slope = (b.v - a.v) / dt;
        const double so = a.out == Side::Bezier ? a.speedOut : slope, io = a.out == Side::Bezier ? clampInfl(a.inflOut) : 1.0 / 3.0;
        const double si = b.in == Side::Bezier ? b.speedIn : slope, ii = b.in == Side::Bezier ? clampInfl(b.inflIn) : 1.0 / 3.0;
        const double x0 = 0, x1 = io, x2 = 1.0 - ii, x3 = 1.0;   // time, normalised to the segment
        const double y0 = a.v, y1 = a.v + so * io * dt, y2 = b.v - si * ii * dt, y3 = b.v;
        auto bez = [](double p0, double p1, double p2, double p3, double u) {
            const double m = 1.0 - u;
            return m * m * m * p0 + 3 * m * m * u * p1 + 3 * m * u * u * p2 + u * u * u * p3;
        };
        double lo = 0, hi = 1;
        for (int i = 0; i < 48; ++i)
        {
            const double mid = 0.5 * (lo + hi);
            (bez(x0, x1, x2, x3, mid) < u0 ? lo : hi) = mid;
        }
        return bez(y0, y1, y2, y3, 0.5 * (lo + hi));
    }

    /** The curve's value at `t`; `keys` sorted by t, not empty. */
    inline double eval(const std::vector<Key> &keys, double t)
    {
        if (keys.empty()) return 0.0;
        if (t <= keys.front().t) return keys.front().v;
        if (t >= keys.back().t) return keys.back().v;
        const auto hi = std::upper_bound(keys.begin(), keys.end(), t, [](double x, const Key &k) { return x < k.t; });
        return segment(*(hi - 1), *hi, t);
    }

    /** Apply a preset to one key, as the right-click menu names them (R-ANIM-2). */
    inline bool preset(Key &k, const std::string &name)
    {
        auto ease = [](Side &s, double &speed, double &infl) { s = Side::Bezier; speed = 0.0; infl = 33.333; };
        if (name == "linear") { k.in = k.out = Side::Linear; }
        else if (name == "ease") { ease(k.in, k.speedIn, k.inflIn); ease(k.out, k.speedOut, k.inflOut); }
        else if (name == "ease-in") ease(k.in, k.speedIn, k.inflIn);       // arriving slows to a stop
        else if (name == "ease-out") ease(k.out, k.speedOut, k.inflOut);   // leaving starts from a stop
        else if (name == "hold") k.out = Side::Hold;
        else return false;
        return true;
    }
}
}
}
