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
 *  A Curve is an automation's points in SAMPLES with each point's shape for the segment after it:
 *  linear, hold (the value stays until the next point), smooth (a smoothstep between the two).
 *  Before the first point it holds the first value; after the last, the last; with none, 0.
 */
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
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

    struct Curve
    {
        enum Shape : uint8_t { Linear, Hold, Smooth };
        std::vector<long long> at;     // samples, ascending
        std::vector<double> value;
        std::vector<uint8_t> shape;    // the segment AFTER each point

        double valueAt(long long s) const
        {
            if (at.empty()) return 0.0;
            if (s <= at.front()) return value.front();
            if (s >= at.back()) return value.back();
            const size_t i = (size_t)(std::upper_bound(at.begin(), at.end(), s) - at.begin()) - 1;
            const double a = value[i], b = value[i + 1];
            const double span = (double)(at[i + 1] - at[i]);
            const double f = span > 0 ? (double)(s - at[i]) / span : 1.0;
            switch (shape[i])
            {
            case Hold: return a;
            case Smooth: return a + (b - a) * f * f * (3.0 - 2.0 * f);
            default: return a + (b - a) * f;
            }
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
