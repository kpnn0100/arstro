/*
 *  cosmo_v2 by arstro — Starfield: the loading-screen backdrop (R-LOADING).
 *
 *  A field of small white particles at deterministic random positions, each
 *  fading in and out on its own slow cycle (a twinkling "star sky"). Purely a
 *  drawing helper — no state changes over time except the phase read from the
 *  frame clock — so it is cheap and reproducible. Platform-free: emits only
 *  rounded-rect fills through the IRenderTarget.
 */
#pragma once
#include "../../../core/Artboard/include/artboard/artboard.h"
#include <cmath>
#include <vector>

namespace arstro
{
namespace cosmo_v2
{
    class Starfield
    {
    public:
        /** Lay out `count` particles deterministically (unit coords + per-particle
         *  size, twinkle phase and speed). Seeded from a constant so it is stable. */
        void init(int count)
        {
            mStars.clear();
            mStars.reserve(count);
            uint32_t s = 0x9E3779B9u;  // fixed seed -> reproducible field
            auto rnd = [&s]() { s = s * 1664525u + 1013904223u; return (s >> 8) / 16777216.0; };  // [0,1)
            for (int i = 0; i < count; ++i)
            {
                Star st;
                st.u = rnd();
                st.v = rnd();
                st.size = 0.35 + rnd() * 0.8;                // small px radius (fine star specks)
                st.phase = rnd() * 6.2831853;                // twinkle offset
                st.speed = 0.4 + rnd() * 1.1;                // twinkle rate
                st.baseA = 0.25 + rnd() * 0.55;              // peak brightness
                mStars.push_back(st);
            }
        }

        /** Draw the field into `r` at overall `alpha`; each particle's own opacity
         *  oscillates (fade in/out) from the frame clock `nowMs`. */
        void draw(artboard::IRenderTarget &t, const artboard::Rect &r, double alpha, double nowMs) const
        {
            if (alpha <= 0.001) return;
            for (const auto &st : mStars)
            {
                const double tw = 0.5 + 0.5 * std::sin(nowMs * 0.001 * st.speed + st.phase);  // 0..1 fade
                const double a = alpha * st.baseA * tw;
                if (a <= 0.004) continue;
                const double x = r.x + st.u * r.w, y = r.y + st.v * r.h, s = st.size;
                artboard::drawRoundedRect(t, artboard::Rect{x - s, y - s, s * 2.0, s * 2.0}, s,
                                          artboard::Paint::filled(artboard::Color{1.0, 1.0, 1.0, a}));
            }
        }

    private:
        struct Star { double u, v, size, phase, speed, baseA; };
        std::vector<Star> mStars;
    };
}
}
