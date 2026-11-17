#include "BrandMark.h"
#include <algorithm>
#include <cmath>

namespace arstro
{
namespace cosmo_v2
{
namespace brand
{
    using namespace artboard;

    namespace
    {
        constexpr double kPi = 3.14159265358979323846;

        // ── easing: the same cubic-beziers the brand pages name, so the C++ and the page are
        //    one curve and not two that look alike (arstro.rule.animation, the easing table) ──
        enum class Ease
        {
            Hold,     // the value does not move until the next key
            Out,      // arriving:            cubic-bezier(.2, .8, .2, 1)
            In,       // leaving, for POSITION: cubic-bezier(.6, 0, .9, .4). Never for opacity — an
                      // accelerating fade ends in a pop (measured 0.10 of alpha in one 8 ms step),
                      // which is exactly what rule 3 forbids. Fades use Sine.
            InOut,    // rest to rest:        cubic-bezier(.7, 0, .3, 1)
            Sine,     // a tiny loop:         cubic-bezier(.42, 0, .58, 1)
            SoftOut,  // first half of a shake:  cubic-bezier(0, 0, .58, 1)
            SoftIn,   // last half of a shake:   cubic-bezier(.42, 0, 1, 1)
            Pass      // a passing streak:    cubic-bezier(.45, 0, .8, .6)
        };

        double bezier(double x1, double y1, double x2, double y2, double x)
        {
            // Solve x(t) = x for t by bisection (x(t) is monotonic for x1, x2 in [0,1]), then
            // return y(t). 24 steps is far past a pixel of error at any size we draw.
            double lo = 0.0, hi = 1.0, t = x;
            for (int i = 0; i < 24; ++i)
            {
                t = 0.5 * (lo + hi);
                const double u = 1.0 - t;
                const double bx = 3.0 * u * u * t * x1 + 3.0 * u * t * t * x2 + t * t * t;
                if (bx < x) lo = t; else hi = t;
            }
            const double u = 1.0 - t;
            return 3.0 * u * u * t * y1 + 3.0 * u * t * t * y2 + t * t * t;
        }

        double ease(Ease e, double x)
        {
            switch (e)
            {
            case Ease::Hold:    return 0.0;
            case Ease::Out:     return bezier(0.2, 0.8, 0.2, 1.0, x);
            case Ease::In:      return bezier(0.6, 0.0, 0.9, 0.4, x);
            case Ease::InOut:   return bezier(0.7, 0.0, 0.3, 1.0, x);
            case Ease::Sine:    return bezier(0.42, 0.0, 0.58, 1.0, x);
            case Ease::SoftOut: return bezier(0.0, 0.0, 0.58, 1.0, x);
            case Ease::SoftIn:  return bezier(0.42, 0.0, 1.0, 1.0, x);
            case Ease::Pass:    return bezier(0.45, 0.0, 0.8, 0.6, x);
            }
            return x;
        }

        /** One key of a track: at fraction `t` the value is `v`, and `e` shapes the way to the
         *  NEXT key. */
        struct Key { double t, v; Ease e; };

        template <int N> double track(const Key (&k)[N], double u)
        {
            if (u <= k[0].t) return k[0].v;
            for (int i = 0; i + 1 < N; ++i)
                if (u <= k[i + 1].t)
                {
                    const double span = k[i + 1].t - k[i].t;
                    const double x = span > 0.0 ? (u - k[i].t) / span : 1.0;
                    return k[i].v + (k[i + 1].v - k[i].v) * ease(k[i].e, x);
                }
            return k[N - 1].v;
        }

        double clamp01(double v) { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); }
        double wrap01(double ms, double period) { const double p = std::fmod(ms, period) / period; return p < 0.0 ? p + 1.0 : p; }

        // ───────────────────────── Cosmo ─────────────────────────
        void cosmoIn(Pose &p, double u)
        {
            static const Key ringA[] = {{0.0, 0.0, Ease::Out}, {0.30, 1.0, Ease::Hold}};
            static const Key ringS[] = {{0.0, 0.6, Ease::Out}, {0.55, 1.0, Ease::Hold}};
            static const Key orbit[] = {{0.0, -150.0, Ease::Hold}, {0.25, -150.0, Ease::Out}, {0.82, 0.0, Ease::Hold}};
            static const Key dot[] = {{0.0, 0.0, Ease::Hold}, {0.25, 0.0, Ease::Sine}, {0.45, 1.0, Ease::Hold}};
            static const Key trail[] = {{0.0, 0.0, Ease::Hold}, {0.28, 0.0, Ease::Sine}, {0.48, 0.9, Ease::Sine}, {0.82, 0.0, Ease::Hold}};
            p.ringAlpha = track(ringA, u); p.ringScale = track(ringS, u);
            p.orbitDeg = track(orbit, u); p.dotAlpha = track(dot, u); p.trailAlpha = track(trail, u);
        }
        void cosmoOut(Pose &p, double u)
        {
            static const Key orbit[] = {{0.0, 0.0, Ease::InOut}, {0.16, -7.0, Ease::In}, {1.0, 140.0, Ease::Hold}};
            static const Key dot[] = {{0.0, 1.0, Ease::Hold}, {0.40, 1.0, Ease::Sine}, {1.0, 0.0, Ease::Hold}};
            static const Key trail[] = {{0.0, 0.0, Ease::Hold}, {0.16, 0.0, Ease::Sine}, {0.46, 0.9, Ease::Sine}, {1.0, 0.0, Ease::Hold}};
            static const Key ringS[] = {{0.0, 1.0, Ease::Hold}, {0.20, 1.0, Ease::In}, {1.0, 0.7, Ease::Hold}};
            static const Key ringA[] = {{0.0, 1.0, Ease::Hold}, {0.45, 1.0, Ease::Sine}, {1.0, 0.0, Ease::Hold}};
            p.orbitDeg = track(orbit, u); p.dotAlpha = track(dot, u); p.trailAlpha = track(trail, u);
            p.ringScale = track(ringS, u); p.ringAlpha = track(ringA, u);
        }
        void cosmoIdle(Pose &p, double ms)
        {
            static const Key orbit[] = {{0.0, 0.0, Ease::InOut}, {1.0, 360.0, Ease::Hold}};
            static const Key trail[] = {{0.0, 0.0, Ease::Sine}, {0.5, 0.9, Ease::Sine}, {1.0, 0.0, Ease::Hold}};
            const double u = wrap01(ms, 1600.0);
            p.orbitDeg = track(orbit, u); p.trailAlpha = track(trail, u);
        }

        // ───────────────────── Interstellar — Warp ─────────────────────
        void warpIn(Pose &p, double u)
        {
            static const Key shipX[] = {{0.0, -70.0, Ease::Out}, {0.55, 0.0, Ease::Hold}};
            static const Key shipA[] = {{0.0, 0.0, Ease::Out}, {0.22, 1.0, Ease::Hold}};
            p.shipX = track(shipX, u); p.shipAlpha = track(shipA, u);
            // The middle streak leads, then the top, then the bottom: 80 ms apart.
            const double start[3] = {0.26, 0.18, 0.34};
            for (int i = 0; i < 3; ++i)
            {
                const Key x[] = {{0.0, -46.0, Ease::Hold}, {start[i], -46.0, Ease::Out}, {start[i] + 0.50, 0.0, Ease::Hold}};
                const Key a[] = {{0.0, 0.0, Ease::Hold}, {start[i], 0.0, Ease::Out}, {start[i] + 0.22, 1.0, Ease::Hold}};
                p.barX[i] = track(x, u); p.barAlpha[i] = track(a, u);
            }
        }
        void warpOut(Pose &p, double u)
        {
            static const Key shipX[] = {{0.0, 0.0, Ease::InOut}, {0.18, -3.0, Ease::In}, {1.0, 90.0, Ease::Hold}};
            static const Key shipA[] = {{0.0, 1.0, Ease::Hold}, {0.40, 1.0, Ease::Sine}, {1.0, 0.0, Ease::Hold}};
            p.shipX = track(shipX, u); p.shipAlpha = track(shipA, u);
            // The streaks follow a shorter way and are gone before the ship is: the bottom one
            // first, the middle one last.
            const double hold[3] = {0.24, 0.28, 0.20}, end[3] = {0.82, 0.88, 0.76}, dist[3] = {42.0, 52.0, 34.0};
            for (int i = 0; i < 3; ++i)
            {
                const Key x[] = {{0.0, 0.0, Ease::InOut}, {0.18, -3.0, Ease::In}, {end[i], dist[i], Ease::Hold}};
                const Key a[] = {{0.0, 1.0, Ease::Hold}, {hold[i], 1.0, Ease::Sine}, {end[i], 0.0, Ease::Hold}};
                p.barX[i] = track(x, u); p.barAlpha[i] = track(a, u);
            }
        }
        void warpIdle(Pose &p, double ms)
        {
            // The ship shakes up and down only — it flies to the right, so sideways would read
            // as a change of speed (arstro.rule.animation, Loops).
            static const Key shake[] = {{0.0, 0.0, Ease::SoftOut}, {0.25, -1.5, Ease::Sine}, {0.75, 1.5, Ease::SoftIn}, {1.0, 0.0, Ease::Hold}};
            p.shipY = track(shake, wrap01(ms, 400.0));
            const double u = wrap01(ms, 1200.0);
            static const Key b0x[] = {{0.0, 0.0, Ease::Sine}, {0.5, -7.0, Ease::Sine}, {1.0, 0.0, Ease::Hold}};
            static const Key b0a[] = {{0.0, 1.0, Ease::Sine}, {0.5, 0.55, Ease::Sine}, {1.0, 1.0, Ease::Hold}};
            static const Key b1x[] = {{0.0, 0.0, Ease::Hold}, {0.20, 0.0, Ease::Sine}, {0.62, -9.0, Ease::Sine}, {1.0, 0.0, Ease::Hold}};
            static const Key b1a[] = {{0.0, 1.0, Ease::Hold}, {0.20, 1.0, Ease::Sine}, {0.62, 0.55, Ease::Sine}, {1.0, 1.0, Ease::Hold}};
            static const Key b2x[] = {{0.0, 0.0, Ease::Sine}, {0.36, -6.0, Ease::Sine}, {0.72, 0.0, Ease::Hold}, {1.0, 0.0, Ease::Hold}};
            static const Key b2a[] = {{0.0, 1.0, Ease::Sine}, {0.36, 0.5, Ease::Sine}, {0.72, 1.0, Ease::Hold}, {1.0, 1.0, Ease::Hold}};
            p.barX[0] = track(b0x, u); p.barAlpha[0] = track(b0a, u);
            p.barX[1] = track(b1x, u); p.barAlpha[1] = track(b1a, u);
            p.barX[2] = track(b2x, u); p.barAlpha[2] = track(b2a, u);
            // Two light streaks pass right to left, one above and one below. Each keeps its
            // length; it fades in, travels 108 units, and is at zero before it stops.
            const double at[2] = {0.04, 0.50};
            for (int i = 0; i < 2; ++i)
            {
                const Key x[] = {{0.0, 46.0, Ease::Hold}, {at[i], 46.0, Ease::Pass}, {at[i] + 0.40, -62.0, Ease::Hold}};
                const Key a[] = {{0.0, 0.0, Ease::Hold}, {at[i], 0.0, Ease::Sine}, {at[i] + 0.10, 1.0, Ease::Hold},
                                 {at[i] + 0.26, 1.0, Ease::Sine}, {at[i] + 0.40, 0.0, Ease::Hold}};
                p.passX[i] = track(x, u); p.passAlpha[i] = track(a, u);
            }
        }

        // ───────────────────── Solaris — Synth sun ─────────────────────
        void sunIn(Pose &p, double u)
        {
            static const Key sunY[] = {{0.0, 40.0, Ease::Out}, {0.60, 0.0, Ease::Hold}};
            static const Key sunA[] = {{0.0, 0.0, Ease::Out}, {0.25, 1.0, Ease::Hold}};
            p.sunY = track(sunY, u); p.sunAlpha = track(sunA, u);
            // The reflection appears top first, each stripe settling 6 units as it fades in.
            const double start[3] = {0.40, 0.52, 0.64};
            for (int i = 0; i < 3; ++i)
            {
                const Key y[] = {{0.0, -6.0, Ease::Hold}, {start[i], -6.0, Ease::Out}, {start[i] + 0.30, 0.0, Ease::Hold}};
                const Key a[] = {{0.0, 0.0, Ease::Hold}, {start[i], 0.0, Ease::Out}, {start[i] + 0.30, 1.0, Ease::Hold}};
                p.stripeY[i] = track(y, u); p.stripeAlpha[i] = track(a, u);
            }
        }
        void sunOut(Pose &p, double u)
        {
            static const Key sunY[] = {{0.0, 0.0, Ease::InOut}, {0.16, -2.0, Ease::In}, {1.0, 40.0, Ease::Hold}};
            static const Key sunA[] = {{0.0, 1.0, Ease::Hold}, {0.45, 1.0, Ease::Sine}, {1.0, 0.0, Ease::Hold}};
            p.sunY = track(sunY, u); p.sunAlpha = track(sunA, u);
            // The reflection goes bottom first, each stripe fading WHILE it drifts down.
            const double start[3] = {0.24, 0.12, 0.0};
            for (int i = 0; i < 3; ++i)
            {
                const Key y[] = {{0.0, 0.0, Ease::Hold}, {start[i], 0.0, Ease::In}, {start[i] + 0.50, 6.0, Ease::Hold}};
                const Key a[] = {{0.0, 1.0, Ease::Hold}, {start[i], 1.0, Ease::Sine}, {start[i] + 0.50, 0.0, Ease::Hold}};
                p.stripeY[i] = track(y, u); p.stripeAlpha[i] = track(a, u);
            }
        }
        void sunIdle(Pose &p, double ms)
        {
            // Up and down only. The sun lifts on an 800 ms beat; a ripple runs down the stripes.
            static const Key beat[] = {{0.0, 0.0, Ease::Sine}, {0.5, -2.0, Ease::Sine}, {1.0, 0.0, Ease::Hold}};
            p.sunY = track(beat, wrap01(ms, 800.0));
            const double u = wrap01(ms, 1600.0), at[3] = {0.0, 0.15, 0.30};
            for (int i = 0; i < 3; ++i)
            {
                const Key y[] = {{0.0, 0.0, Ease::Hold}, {at[i], 0.0, Ease::Sine}, {at[i] + 0.25, 1.5, Ease::Sine}, {at[i] + 0.50, 0.0, Ease::Hold}, {1.0, 0.0, Ease::Hold}};
                const Key a[] = {{0.0, 1.0, Ease::Hold}, {at[i], 1.0, Ease::Sine}, {at[i] + 0.25, 0.4, Ease::Sine}, {at[i] + 0.50, 1.0, Ease::Hold}, {1.0, 1.0, Ease::Hold}};
                p.stripeY[i] = track(y, u); p.stripeAlpha[i] = track(a, u);
            }
        }

        // ───────────────────────── drawing ─────────────────────────
        /** Maps the 96 grid into the caller's box. */
        struct Grid
        {
            double ox, oy, s;
            double x(double v) const { return ox + v * s; }
            double y(double v) const { return oy + v * s; }
        };

        Color faded(Color c, double a) { c.a *= clamp01(a); return c; }

        void circlePath(IRenderTarget &t, double cx, double cy, double r)
        {
            const double k = 0.5522847498 * r;
            t.beginPath();
            t.moveTo(cx + r, cy);
            t.cubicTo(cx + r, cy + k, cx + k, cy + r, cx, cy + r);
            t.cubicTo(cx - k, cy + r, cx - r, cy + k, cx - r, cy);
            t.cubicTo(cx - r, cy - k, cx - k, cy - r, cx, cy - r);
            t.cubicTo(cx + k, cy - r, cx + r, cy - k, cx + r, cy);
            t.closePath();
        }

        /** A bar with fully round ends (`h` is its thickness). */
        void pillPath(IRenderTarget &t, double x, double y, double w, double h)
        {
            const double r = h * 0.5, k = 0.5522847498 * r;
            t.beginPath();
            t.moveTo(x + r, y);
            t.lineTo(x + w - r, y);
            t.cubicTo(x + w - r + k, y, x + w, y + r - k, x + w, y + r);
            t.cubicTo(x + w, y + r + k, x + w - r + k, y + h, x + w - r, y + h);
            t.lineTo(x + r, y + h);
            t.cubicTo(x + r - k, y + h, x, y + r + k, x, y + r);
            t.cubicTo(x, y + r - k, x + r - k, y, x + r, y);
            t.closePath();
        }

        /** The mark's two-tone, laid across the WHOLE mark (bottom left to top right) the way
         *  the brand pages draw it, so a part keeps its place in the gradient while it moves. */
        void gradientFill(IRenderTarget &t, const Grid &g, Mark m, double a)
        {
            Color c0, c1; colours(m, c0, c1);
            t.setLinearFill(g.x(8.0), g.y(92.0), g.x(88.0), g.y(4.0), faded(c0, a), faded(c1, a));
        }

        void drawCosmo(IRenderTarget &t, const Grid &g, const Pose &p, const Color &ink, double a)
        {
            const double cx = 44.0, cy = 44.0;
            if (p.ringAlpha * a > 0.003)
            {
                // The ring is a stroke, so its uniform scale scales the radius and the weight
                // together — the shape is never deformed (rule 1).
                t.setStroke(faded(ink, p.ringAlpha * a), 9.0 * p.ringScale * g.s);
                circlePath(t, g.x(cx), g.y(cy), 25.0 * p.ringScale * g.s);
                t.strokePath();
            }
            // The moon's rest place is (75,73): 42.45 units from the ring's centre at 43.09°.
            const double R = 42.45, rest = 43.09;
            Color blue, unused; colours(Mark::Cosmo, blue, unused);
            if (p.trailAlpha * a > 0.003)
            {
                // The light streak behind the moon: a fixed 41° of arc that ends where the moon
                // begins. Drawn as short pieces so it can fade toward its tail without a
                // gradient stroke (the HAL has no stroke gradient).
                const int n = 8;
                for (int i = 0; i < n; ++i)
                {
                    const double a0 = (rest + p.orbitDeg - 50.0 + 41.0 * i / n) * kPi / 180.0;
                    const double a1 = (rest + p.orbitDeg - 50.0 + 41.0 * (i + 1) / n) * kPi / 180.0;
                    t.setStroke(faded(blue, p.trailAlpha * a * (i + 1.0) / n), 5.0 * g.s);
                    t.beginPath();
                    t.moveTo(g.x(cx + R * std::cos(a0)), g.y(cy + R * std::sin(a0)));
                    t.lineTo(g.x(cx + R * std::cos(a1)), g.y(cy + R * std::sin(a1)));
                    t.strokePath();
                }
            }
            if (p.dotAlpha * a > 0.003)
            {
                const double ang = (rest + p.orbitDeg) * kPi / 180.0;
                t.setFill(faded(blue, p.dotAlpha * a));
                circlePath(t, g.x(cx + R * std::cos(ang)), g.y(cy + R * std::sin(ang)), 8.0 * g.s);
                t.fillPath();
            }
        }

        void drawWarp(IRenderTarget &t, const Grid &g, const Pose &p, const Color &ink, double a)
        {
            // Passing light: white, clear at both ends. Two halves, because a fill has two stops.
            const double passY[2] = {15.0, 80.0};
            for (int i = 0; i < 2; ++i)
            {
                const double pa = p.passAlpha[i] * a * 0.75;
                if (pa <= 0.003) continue;
                const double x0 = g.x(40.0 + p.passX[i]), y0 = g.y(passY[i]), w = 22.0 * g.s, h = 2.5 * g.s;
                const Color clear = faded(ink, 0.0), full = faded(ink, pa);
                t.setLinearFill(x0, 0.0, x0 + w * 0.5, 0.0, clear, full);
                t.beginPath(); t.moveTo(x0, y0); t.lineTo(x0 + w * 0.5, y0); t.lineTo(x0 + w * 0.5, y0 + h); t.lineTo(x0, y0 + h); t.closePath(); t.fillPath();
                t.setLinearFill(x0 + w * 0.5, 0.0, x0 + w, 0.0, full, clear);
                t.beginPath(); t.moveTo(x0 + w * 0.5, y0); t.lineTo(x0 + w, y0); t.lineTo(x0 + w, y0 + h); t.lineTo(x0 + w * 0.5, y0 + h); t.closePath(); t.fillPath();
            }
            // The three streaks: top, middle, bottom. Fixed lengths — they move, they never grow.
            const double bx[3] = {18.0, 6.0, 24.0}, by[3] = {29.0, 43.5, 58.0}, bw[3] = {22.0, 34.0, 16.0};
            for (int i = 0; i < 3; ++i)
            {
                if (p.barAlpha[i] * a <= 0.003) continue;
                gradientFill(t, g, Mark::Interstellar, p.barAlpha[i] * a);
                pillPath(t, g.x(bx[i] + p.barX[i]), g.y(by[i]), bw[i] * g.s, 9.0 * g.s);
                t.fillPath();
            }
            if (p.shipAlpha * a > 0.003)
            {
                // The ship: the triangle (52,27) (86,48) (52,69) grown by 4 units with round
                // corners — the brand page's 8-unit round-joined outline, built as one filled
                // path because the HAL has no join style to ask for.
                const double vx[3] = {52.0, 86.0, 52.0}, vy[3] = {27.0, 48.0, 69.0}, r = 4.0;
                t.setFill(faded(ink, p.shipAlpha * a));
                t.beginPath();
                bool first = true;
                for (int i = 0; i < 3; ++i)
                {
                    const int prev = (i + 2) % 3, next = (i + 1) % 3;
                    // outward normals of the edge arriving at vertex i and the edge leaving it
                    auto normal = [&](int from, int to, double &nx, double &ny) {
                        const double ex = vx[to] - vx[from], ey = vy[to] - vy[from], len = std::hypot(ex, ey);
                        nx = ey / len; ny = -ex / len;   // the triangle is wound clockwise on screen
                    };
                    double n0x, n0y, n1x, n1y;
                    normal(prev, i, n0x, n0y); normal(i, next, n1x, n1y);
                    double a0 = std::atan2(n0y, n0x), a1 = std::atan2(n1y, n1x);
                    while (a1 < a0) a1 += 2.0 * kPi;
                    const int steps = 6;
                    for (int s = 0; s <= steps; ++s)
                    {
                        const double ang = a0 + (a1 - a0) * s / steps;
                        const double px = g.x(vx[i] + p.shipX + r * std::cos(ang)), py = g.y(vy[i] + p.shipY + r * std::sin(ang));
                        if (first) { t.moveTo(px, py); first = false; } else t.lineTo(px, py);
                    }
                }
                t.closePath();
                t.fillPath();
            }
        }

        void drawSynthSun(IRenderTarget &t, const Grid &g, const Pose &p, const Color &ink, double a)
        {
            if (p.sunAlpha * a > 0.003)
            {
                // The horizon is a FIXED clip at y = 49.5 and the sun only translates behind it —
                // a reveal, not a stretch (arstro.rule.animation, rule 1, "a fixed clip").
                t.save();
                t.clipRect(g.x(-20.0), g.y(-40.0), 136.0 * g.s, 89.5 * g.s);
                gradientFill(t, g, Mark::Solaris, p.sunAlpha * a);
                circlePath(t, g.x(48.0), g.y(49.0 + p.sunY), 38.0 * g.s);
                t.fillPath();
                t.restore();
            }
            // The reflection: three bars that shorten. Fixed lengths — a level is never drawn by
            // growing a bar.
            const double sx[3] = {13.0, 20.0, 33.0}, sy[3] = {56.0, 69.0, 82.0}, sw[3] = {70.0, 56.0, 30.0};
            for (int i = 0; i < 3; ++i)
            {
                if (p.stripeAlpha[i] * a <= 0.003) continue;
                t.setFill(faded(ink, p.stripeAlpha[i] * a));
                pillPath(t, g.x(sx[i]), g.y(sy[i] + p.stripeY[i]), sw[i] * g.s, 8.0 * g.s);
                t.fillPath();
            }
        }
    }

    double loopMs(Mark m) { return m == Mark::Interstellar ? 1200.0 : 1600.0; }

    void colours(Mark m, Color &start, Color &end)
    {
        switch (m)
        {
        case Mark::Cosmo:        start = end = Color::hex(0x4F7EF7); break;
        case Mark::Interstellar: start = Color::hex(0x14B8A6); end = Color::hex(0x38A3F5); break;
        case Mark::Solaris:      start = Color::hex(0xFF6A1A); end = Color::hex(0xFFD23F); break;
        }
    }

    Pose poseAt(Mark m, Phase phase, double ms)
    {
        Pose p;
        if (phase == Phase::Gone) { p.alpha = 0.0; return p; }
        if (phase == Phase::Rest || phase == Phase::Settle) return p;   // Settle is the Player's: it needs where the loop was
        if (phase == Phase::Idle)
        {
            switch (m)
            {
            case Mark::Cosmo: cosmoIdle(p, ms); break;
            case Mark::Interstellar: warpIdle(p, ms); break;
            case Mark::Solaris: sunIdle(p, ms); break;
            }
            return p;
        }
        const bool in = phase == Phase::In;
        const double u = clamp01(ms / (in ? kInMs : kOutMs));
        switch (m)
        {
        case Mark::Cosmo: in ? cosmoIn(p, u) : cosmoOut(p, u); break;
        case Mark::Interstellar: in ? warpIn(p, u) : warpOut(p, u); break;
        case Mark::Solaris: in ? sunIn(p, u) : sunOut(p, u); break;
        }
        return p;
    }

    void draw(IRenderTarget &t, const Rect &box, Mark m, const Pose &pose, const Color &structure, double alpha)
    {
        const double a = clamp01(alpha) * clamp01(pose.alpha);
        if (a <= 0.003) return;
        const double side = std::min(box.w, box.h);
        const Grid g{box.x + (box.w - side) * 0.5, box.y + (box.h - side) * 0.5, side / 96.0};
        switch (m)
        {
        case Mark::Cosmo: drawCosmo(t, g, pose, structure, a); break;
        case Mark::Interstellar: drawWarp(t, g, pose, structure, a); break;
        case Mark::Solaris: drawSynthSun(t, g, pose, structure, a); break;
        }
    }

    // ───────────────────────── Player ─────────────────────────
    void Player::enter(Phase p, double atMs) { mPhase = p; mT0 = atMs; }

    void Player::showUp(double nowMs, bool thenIdle)
    {
        mNow = nowMs; mThenIdle = thenIdle; mWantOut = false; mWantRest = false;
        enter(Phase::In, nowMs);
    }

    void Player::requestOut(double nowMs, Leave how)
    {
        mNow = nowMs;
        if (mPhase == Phase::Rest) enter(Phase::Out, nowMs);
        else if (mPhase == Phase::In || mPhase == Phase::Settle) mWantOut = true;
        else if (mPhase == Phase::Idle && !mWantOut)
        {
            mWantOut = true;
            if (how == Leave::EaseOut) { mFrom = poseAt(mMark, Phase::Idle, nowMs - mT0); enter(Phase::Settle, nowMs); }
            else latchBoundary(nowMs);
        }
    }

    void Player::requestRest(double nowMs, Leave how)
    {
        mNow = nowMs;
        if (mPhase == Phase::In) mThenIdle = false;
        else if (mPhase == Phase::Idle && !mWantRest && !mWantOut)
        {
            mWantRest = true;
            if (how == Leave::EaseOut) { mFrom = poseAt(mMark, Phase::Idle, nowMs - mT0); enter(Phase::Settle, nowMs); }
            else latchBoundary(nowMs);
        }
    }

    void Player::latchBoundary(double nowMs)
    {
        // The NEXT moment the loop is back at its start pose. Latched once, when asked: computed
        // per frame it is always "the next one" and is never reached.
        const double period = loopMs(mMark);
        mBoundary = mT0 + std::ceil((nowMs - mT0) / period - 1e-9) * period;
    }

    void Player::advance(double nowMs)
    {
        mNow = nowMs;
        const bool reduced = reducedMotion();
        const double inMs = reduced ? kReducedMs : kInMs, outMs = reduced ? kReducedMs : kOutMs;
        if (mPhase == Phase::In && nowMs - mT0 >= inMs)
        {
            const double end = mT0 + inMs;
            if (mWantOut) { mWantOut = false; enter(Phase::Out, end); }
            else enter(mThenIdle && !reduced ? Phase::Idle : Phase::Rest, end);
        }
        if (mPhase == Phase::Idle && (mWantOut || mWantRest) && (nowMs >= mBoundary || reduced))
        {
            // Leave the loop only where it is back at its start pose, so nothing jumps. The
            // next phase is dated from that boundary, not from the frame that noticed it.
            const double at = reduced ? nowMs : mBoundary;
            if (mWantOut) { mWantOut = false; mWantRest = false; enter(Phase::Out, at); }
            else { mWantRest = false; enter(Phase::Rest, at); }
        }
        if (mPhase == Phase::Settle && (nowMs - mT0 >= kSettleMs || reduced))
        {
            const double at = reduced ? nowMs : mT0 + kSettleMs;
            if (mWantOut) { mWantOut = false; mWantRest = false; enter(Phase::Out, at); }
            else { mWantRest = false; enter(Phase::Rest, at); }
        }
        if (mPhase == Phase::Out && nowMs - mT0 >= outMs) enter(Phase::Gone, mT0 + outMs);
    }

    double Player::outProgress() const
    {
        if (mPhase == Phase::Gone) return 1.0;
        if (mPhase != Phase::Out) return 0.0;
        return clamp01((mNow - mT0) / (reducedMotion() ? kReducedMs : kOutMs));
    }

    Pose Player::pose() const
    {
        if (reducedMotion())
        {
            // No travel, no streaks, no loop — but still a fade: rule 3 does not switch off.
            Pose p;
            if (mPhase == Phase::Gone) p.alpha = 0.0;
            else if (mPhase == Phase::In) p.alpha = clamp01((mNow - mT0) / kReducedMs);
            else if (mPhase == Phase::Out) p.alpha = 1.0 - clamp01((mNow - mT0) / kReducedMs);
            return p;
        }
        if (mPhase == Phase::Settle)
        {
            // Ease every part from where the loop was to the rest pose: rest to rest, so the
            // strong in-out curve. The moon goes home the short way round.
            const double k = ease(Ease::InOut, clamp01((mNow - mT0) / kSettleMs));
            const Pose rest; Pose p;
            auto mix = [k](double from, double to) { return from + (to - from) * k; };
            p.ringScale = mix(mFrom.ringScale, rest.ringScale); p.ringAlpha = mix(mFrom.ringAlpha, rest.ringAlpha);
            p.orbitDeg = mix(mFrom.orbitDeg, std::round(mFrom.orbitDeg / 360.0) * 360.0);
            p.dotAlpha = mix(mFrom.dotAlpha, rest.dotAlpha); p.trailAlpha = mix(mFrom.trailAlpha, rest.trailAlpha);
            p.shipX = mix(mFrom.shipX, 0.0); p.shipY = mix(mFrom.shipY, 0.0); p.shipAlpha = mix(mFrom.shipAlpha, 1.0);
            for (int i = 0; i < 3; ++i)
            {
                p.barX[i] = mix(mFrom.barX[i], 0.0); p.barAlpha[i] = mix(mFrom.barAlpha[i], 1.0);
                p.stripeY[i] = mix(mFrom.stripeY[i], 0.0); p.stripeAlpha[i] = mix(mFrom.stripeAlpha[i], 1.0);
            }
            for (int i = 0; i < 2; ++i) { p.passX[i] = mFrom.passX[i]; p.passAlpha[i] = mix(mFrom.passAlpha[i], 0.0); }
            p.sunY = mix(mFrom.sunY, 0.0); p.sunAlpha = mix(mFrom.sunAlpha, 1.0);
            return p;
        }
        return poseAt(mMark, mPhase, mNow - mT0);
    }
}
}
}
