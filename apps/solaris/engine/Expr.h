/*
 *  solaris_engine — Expr and Curve: what a binding is, once compiled (R-AUTO-2, R-AUTO-4, R-AUTO-7).
 *
 *  A formula arrives here already parsed and resolved (the core's `Formula`): a postfix program over
 *  numbers and VARIABLE SLOTS. The engine knows no names — slot 0…3 are the song's position (beat,
 *  bar, bpm, seconds), then one slot per automation curve, then one per binding's evaluated value
 *  (a link reads an earlier binding's slot; the core orders bindings so every link reads one already
 *  evaluated). Evaluation is a fixed stack, no allocation (R-PLAY-2), and the same function the
 *  service's `eval` uses, so a script and the audio agree.
 *
 *  A Curve is an automation's points in SAMPLES as Interstellar's keyframes (R-ANIM-2, R-AUTO-10 —
 *  law 16, ONE curve model in the suite): `apps/interstellar/model/Anim.h` is included in place and
 *  evaluates it — header-only, a fixed bisection, no allocation, so it runs on the audio thread as
 *  the formulas do. A point's shape maps onto the key's two sides (`curveKeys`): linear (both sides
 *  linear), hold (its out side holds until the next point), smooth (After Effects' Ease on the segment
 *  after it — speed 0, influence ⅓ — whose Bézier is exactly the smoothstep R-AUTO-4 drew), bezier
 *  (both sides bezier, shaped by its handles). Before the first point it holds the first value; after
 *  the last, the last; with none, 0; never outside the automation's range (a handle's overshoot stops
 *  there).
 */
#pragma once
#include "../../interstellar/model/Anim.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace arstro
{
namespace solaris
{
namespace engine
{
    struct ExprOp
    {
        enum Kind : uint8_t { Num, Var, Neg, Add, Sub, Mul, Div, Pow, Fn } kind = Num;
        enum Func : uint8_t { Sin, Cos, Tan, Abs, Sign, Min, Max, Clamp, Lerp, PowF, Exp, Log, Sqrt, Floor, Ceil, Round, Frac } fn = Sin;
        int index = 0;   // Var: the slot
        double num = 0;  // Num: the constant
    };

    inline int funcArity(ExprOp::Func f)
    {
        switch (f)
        {
        case ExprOp::Min: case ExprOp::Max: case ExprOp::PowF: return 2;
        case ExprOp::Clamp: case ExprOp::Lerp: return 3;
        default: return 1;
        }
    }

    struct Expr
    {
        static constexpr int kStack = 32;
        std::vector<ExprOp> ops; // postfix; the core refuses a formula deeper than kStack

        /** NaN when the program is malformed (an empty or unbalanced program never reaches here). */
        double eval(const double *vars) const
        {
            double st[kStack];
            int sp = 0;
            for (const ExprOp &o : ops)
            {
                switch (o.kind)
                {
                case ExprOp::Num: st[sp++] = o.num; break;
                case ExprOp::Var: st[sp++] = vars[o.index]; break;
                case ExprOp::Neg: st[sp - 1] = -st[sp - 1]; break;
                case ExprOp::Add: --sp; st[sp - 1] += st[sp]; break;
                case ExprOp::Sub: --sp; st[sp - 1] -= st[sp]; break;
                case ExprOp::Mul: --sp; st[sp - 1] *= st[sp]; break;
                case ExprOp::Div: --sp; st[sp - 1] /= st[sp]; break;
                case ExprOp::Pow: --sp; st[sp - 1] = std::pow(st[sp - 1], st[sp]); break;
                case ExprOp::Fn:
                {
                    const int n = funcArity(o.fn);
                    sp -= n;
                    const double a = st[sp], b = n > 1 ? st[sp + 1] : 0.0, c = n > 2 ? st[sp + 2] : 0.0;
                    double v = 0;
                    switch (o.fn)
                    {
                    case ExprOp::Sin: v = std::sin(a); break;
                    case ExprOp::Cos: v = std::cos(a); break;
                    case ExprOp::Tan: v = std::tan(a); break;
                    case ExprOp::Abs: v = std::fabs(a); break;
                    case ExprOp::Sign: v = a > 0 ? 1.0 : (a < 0 ? -1.0 : 0.0); break;
                    case ExprOp::Min: v = std::min(a, b); break;
                    case ExprOp::Max: v = std::max(a, b); break;
                    case ExprOp::Clamp: v = std::min(std::max(a, b), c); break;
                    case ExprOp::Lerp: v = a + (b - a) * c; break;
                    case ExprOp::PowF: v = std::pow(a, b); break;
                    case ExprOp::Exp: v = std::exp(a); break;
                    case ExprOp::Log: v = std::log(a); break;
                    case ExprOp::Sqrt: v = std::sqrt(a); break;
                    case ExprOp::Floor: v = std::floor(a); break;
                    case ExprOp::Ceil: v = std::ceil(a); break;
                    case ExprOp::Round: v = std::round(a); break;
                    case ExprOp::Frac: v = a - std::floor(a); break;
                    }
                    st[sp++] = v;
                    break;
                }
                }
            }
            return sp == 1 ? st[0] : std::nan("");
        }
    };

    namespace anim = ::arstro::interstellar::anim;

    /** A point as the `.slp` spells it (R-AUTO-4, R-AUTO-10), on any time base — samples for the
     *  engine, beats for a drawing. A bezier side's SPEED is in value units per unit of `t`; its
     *  INFLUENCE is the % of the neighbouring segment its handle reaches into (0.1 … 100). */
    struct CurvePoint
    {
        enum Shape : uint8_t { Linear, Hold, Smooth, Bezier };
        static constexpr double kInfluence = 100.0 / 3.0; // Ease's ⅓: a Bézier whose time is exactly linear in u
        double t = 0, v = 0;
        Shape shape = Linear;
        double speedIn = 0, inflIn = kInfluence, speedOut = 0, inflOut = kInfluence;
    };

    /** `linear | hold | smooth | bezier`; false for anything else. */
    inline bool shapeNamed(const std::string &s, CurvePoint::Shape &out)
    {
        if (s == "linear") out = CurvePoint::Linear;
        else if (s == "hold") out = CurvePoint::Hold;
        else if (s == "smooth") out = CurvePoint::Smooth;
        else if (s == "bezier") out = CurvePoint::Bezier;
        else return false;
        return true;
    }

    /** The points as Interstellar's keys — the ONE mapping, used by the compiler and the timeline's
     *  drawing alike. A segment is decided by its first point's OUT side and its second's IN side:
     *  a smooth point eases both ends of the segment after it, unless the next point is bezier — its
     *  own handle wins on its side. */
    inline std::vector<anim::Key> curveKeys(const std::vector<CurvePoint> &pts)
    {
        std::vector<anim::Key> keys;
        keys.reserve(pts.size());
        for (size_t i = 0; i < pts.size(); ++i)
        {
            const CurvePoint &p = pts[i];
            anim::Key k;
            k.t = p.t;
            k.v = p.v;
            k.in = k.out = anim::Side::Linear;
            if (p.shape == CurvePoint::Bezier)
            {
                k.in = k.out = anim::Side::Bezier;
                k.speedIn = p.speedIn;
                k.inflIn = p.inflIn;
                k.speedOut = p.speedOut;
                k.inflOut = p.inflOut;
            }
            else if (p.shape == CurvePoint::Hold) k.out = anim::Side::Hold;
            else if (p.shape == CurvePoint::Smooth)
            {
                k.out = anim::Side::Bezier;
                k.speedOut = 0.0;
                k.inflOut = CurvePoint::kInfluence;
            }
            if (i > 0 && pts[i - 1].shape == CurvePoint::Smooth && p.shape != CurvePoint::Bezier)
            {
                k.in = anim::Side::Bezier; // the smooth before it eases in
                k.speedIn = 0.0;
                k.inflIn = CurvePoint::kInfluence;
            }
            keys.push_back(k);
        }
        return keys;
    }

    struct Curve
    {
        std::vector<anim::Key> keys;              // t in samples, ascending; speeds per sample
        double lo = -HUGE_VAL, hi = HUGE_VAL;     // the automation's range

        double valueAt(long long s) const
        {
            if (keys.empty()) return 0.0;
            return std::min(std::max(anim::eval(keys, (double)s), lo), hi); // Interstellar's evaluator: no allocation
        }
    };

    /** What a binding drives. Values are in the address's own units (dB, −1…1, Hz, …). */
    struct Bind
    {
        enum Kind : uint8_t { StripGain, StripPan, SendGain, MasterGain, DeviceParam, MasterDeviceParam } kind = StripGain;
        int strip = -1;      // StripGain/Pan, SendGain, DeviceParam
        int index = -1;      // SendGain: the send; DeviceParam/MasterDeviceParam: the device in the rack
        int param = -1;      // DeviceParam/MasterDeviceParam: the registry parameter's index
        double lo = 0, hi = 1, own = 0; // the clamp, and the value before any evaluation
        bool integer = false;
        Expr expr;
    };

    /** The song's position as a formula sees it — slots 0…3. */
    struct Clock
    {
        double samplesPerBeat = 24000, beatsPerBar = 4, bpm = 120, rate = 48000;
        void fill(double *vars, long long sample) const
        {
            const double beat = (double)sample / samplesPerBeat;
            vars[0] = beat;
            vars[1] = beat / beatsPerBar;
            vars[2] = bpm;
            vars[3] = (double)sample / rate;
        }
    };
    constexpr int kClockSlots = 4;

    /** Evaluate every binding at `sample`, in order, into `vars` (sized 4 + curves + binds) and
     *  `values` (one per bind; on entry the last good value, kept when a result is not finite). */
    inline void evaluateBinds(const Clock &clock, const std::vector<Curve> &curves, const std::vector<Bind> &binds,
                              long long sample, double *vars, double *values)
    {
        clock.fill(vars, sample);
        for (size_t c = 0; c < curves.size(); ++c) vars[kClockSlots + c] = curves[c].valueAt(sample);
        const size_t base = kClockSlots + curves.size();
        for (size_t b = 0; b < binds.size(); ++b)
        {
            double v = binds[b].expr.eval(vars);
            if (std::isfinite(v))
            {
                v = std::min(std::max(v, binds[b].lo), binds[b].hi);
                if (binds[b].integer) v = std::round(v);
                values[b] = v;
            }
            vars[base + b] = values[b];
        }
    }
}
}
}
