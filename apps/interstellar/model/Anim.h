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
#include <cstdio>
#include <cstdlib>
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

    // ── shapes (R-ANIM-6): a tone curve, a colour wheel, a crop — keyed like numbers ────────────
    //
    // A shape key carries TEXT in the address's own syntax ("x,y;x,y;…", "h,s,l", "x,y,w,h"). Between
    // two keys the shape blends by the segment's PROGRESS — the same temporal sides as a number's
    // (linear, an ease, a hold), measured on a 0 → 1 ramp, so "ease out" eases a curve's change too.
    // A curve's points blend point by point when the two keys have as many; otherwise both are
    // resampled at 17 x positions first. A wheel's hue takes the short way round the circle.

    inline std::vector<double> numbers(const std::string &s)
    {
        std::vector<double> v;
        const char *p = s.c_str();
        while (*p)
        {
            char *end = nullptr;
            const double x = std::strtod(p, &end);
            if (end == p) { ++p; continue; }
            v.push_back(x);
            p = end;
        }
        return v;
    }

    inline std::string formatNumbers(const std::vector<double> &v, int group)
    {
        std::string out;
        char b[32];
        for (size_t i = 0; i < v.size(); ++i)
        {
            std::snprintf(b, sizeof b, "%.5g", v[i]);
            if (i) out += group > 0 && i % (size_t)group == 0 ? ";" : ",";
            out += b;
        }
        return out;
    }

    /** The progress (0 → 1) through the segment a → b at t, with their temporal sides. */
    inline double progress(const Key &a, const Key &b, double t)
    {
        Key x = a, y = b;
        x.v = 0.0;
        y.v = 1.0;
        x.speedOut = y.speedIn = 0.0;   // a shape's "speed" is its ease: speeds are flat, influence shapes it
        return std::clamp(segment(x, y, t), 0.0, 1.0);
    }

    inline std::string blendShape(const std::string &a, const std::string &b, double u)
    {
        if (u <= 0.0) return a;
        if (u >= 1.0) return b;
        const bool points = a.find(';') != std::string::npos || b.find(';') != std::string::npos;
        std::vector<double> va = numbers(a), vb = numbers(b);
        if (points || va.size() == 2)
        {
            // curves: pairs (x, y); resample both when the counts differ
            auto resample = [](const std::vector<double> &v) {
                std::vector<std::pair<double, double>> p;
                for (size_t i = 0; i + 1 < v.size(); i += 2) p.push_back({v[i], v[i + 1]});
                std::sort(p.begin(), p.end());
                std::vector<double> out;
                for (int k = 0; k <= 16; ++k)
                {
                    const double x = k / 16.0;
                    double y = p.empty() ? x : p.front().second;
                    for (size_t i = 0; i + 1 < p.size(); ++i)
                        if (x >= p[i].first && x <= p[i + 1].first)
                        {
                            const double w = p[i + 1].first - p[i].first;
                            y = w > 0 ? p[i].second + (p[i + 1].second - p[i].second) * (x - p[i].first) / w : p[i].second;
                        }
                    if (!p.empty() && x > p.back().first) y = p.back().second;
                    out.push_back(x);
                    out.push_back(y);
                }
                return out;
            };
            if (va.size() != vb.size() || va.size() % 2) { va = resample(va); vb = resample(vb); }
            std::vector<double> o(va.size());
            for (size_t i = 0; i < va.size(); ++i) o[i] = va[i] + (vb[i] - va[i]) * u;
            return formatNumbers(o, 2);
        }
        if (va.size() != vb.size() || va.empty()) return u < 0.5 ? a : b;   // nothing to blend: a step
        std::vector<double> o(va.size());
        for (size_t i = 0; i < va.size(); ++i) o[i] = va[i] + (vb[i] - va[i]) * u;
        if (va.size() == 3)
        {
            // a wheel: hue in degrees, the short way round
            double d = std::fmod(vb[0] - va[0] + 540.0, 360.0) - 180.0;
            o[0] = std::fmod(va[0] + d * u + 360.0, 360.0);
        }
        return formatNumbers(o, 0);
    }

    struct ShapeKey
    {
        Key k;                // its time and temporal sides (k.v unused)
        std::string shape;
    };

    /** A shape curve's value at `t`; `keys` sorted by time, not empty. */
    inline std::string evalShape(const std::vector<ShapeKey> &keys, double t)
    {
        if (keys.empty()) return std::string();
        if (t <= keys.front().k.t) return keys.front().shape;
        if (t >= keys.back().k.t) return keys.back().shape;
        size_t i = 0;
        while (i + 1 < keys.size() && keys[i + 1].k.t <= t) ++i;
        const auto &a = keys[i], &b = keys[i + 1];
        return blendShape(a.shape, b.shape, progress(a.k, b.k, t));
    }

    // ── speed ramps (R-EDT-3) ────────────────────────────────────────────────────────────────────
    //
    // A clip's speed keyed on its FOOTAGE clock: v(s) at source time s. The clip's own time at s is
    // τ(s) = ∫ ds / v(s) from its in-point, so the clip lasts τ(out) and the frame shown τ seconds into
    // it is the inverse — continuous however the curve turns. Speeds are held to 0.1 … 8×.
    struct Ramp
    {
        std::vector<double> s, tau;           // a table of τ against s, in-point first
        double duration() const { return tau.empty() ? 0.0 : tau.back(); }

        static Ramp build(const std::vector<Key> &keys, double in, double out)
        {
            Ramp r;
            if (keys.empty() || !(out > in)) return r;
            const int n = std::clamp((int)std::ceil((out - in) * 480.0), 64, 40000);
            const double h = (out - in) / n;
            auto inv = [&](double x) { return 1.0 / std::clamp(eval(keys, x), 0.1, 8.0); };
            r.s.resize((size_t)n + 1);
            r.tau.resize((size_t)n + 1);
            r.s[0] = in;
            r.tau[0] = 0.0;
            double prev = inv(in);
            for (int i = 1; i <= n; ++i)
            {
                const double x = in + h * i, cur = inv(x);
                r.s[(size_t)i] = x;
                r.tau[(size_t)i] = r.tau[(size_t)i - 1] + h * 0.5 * (prev + cur);   // trapezoid on 1/v
                prev = cur;
            }
            return r;
        }

        /** The source time `t` seconds into the clip. */
        double sourceAt(double t) const
        {
            if (tau.empty()) return 0.0;
            if (t <= 0.0) return s.front();
            if (t >= tau.back()) return s.back();
            const auto hi = std::upper_bound(tau.begin(), tau.end(), t);
            const size_t i = (size_t)(hi - tau.begin());
            const double f = (t - tau[i - 1]) / std::max(1e-12, tau[i] - tau[i - 1]);
            return s[i - 1] + (s[i] - s[i - 1]) * f;
        }
    };

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
