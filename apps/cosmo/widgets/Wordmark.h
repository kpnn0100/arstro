/*
 *  cosmo_v2 by arstro — Wordmark: "<name>." — an app's name with the accent dot (R-FAMILY-5).
 *
 *  One wordmark lock for the family (R-G-2a): the name in `foreground`, SemiBold, tracked IN by
 *  0.03 × size; the dot in the accent, the same face and size. The dot is placed by the MEASURED
 *  width of the name. Cosmo's top bar still places it at `estimateTextWidth` (chars × px × 0.6),
 *  which no real font matches — the dot detaches from the word. Interstellar's top bar and
 *  Cosmo's own flying wordmark already measure, each with its own copy of these five lines; this
 *  is the one copy.
 *
 *  Measurement needs the render target, so both functions take one and are only valid in a paint.
 *  Header-only and stateless: nothing to construct, nothing to advance.
 */
#pragma once
#include "../../../core/Artboard/include/artboard/artboard.h"
#include "../Theme.h"
#include <string>

namespace arstro
{
namespace cosmo_v2
{
    struct Wordmark
    {
        /** Tracking as a fraction of the size — in, not out (design law §2.3). */
        static constexpr double kTrackPerPx = -0.03;

        /** Width of "<name>." at `sizePx`, by the target's real metrics. */
        static double width(const artboard::IRenderTarget &t, const std::string &name, double sizePx)
        {
            const double sp = kTrackPerPx * sizePx;
            return t.measureText(name, sizePx, font::sansSemiBold(), sp) + t.measureText(".", sizePx, font::sansSemiBold(), sp);
        }

        /** Draw "<name>." with its left edge at `x` on `baselineY`; `alpha` fades both runs (a
         *  wordmark that flies or cross-fades). Returns its width, for what is laid out after it. */
        static double draw(artboard::IRenderTarget &t, const std::string &name, double x, double baselineY,
                           double sizePx, double alpha = 1.0)
        {
            const double sp = kTrackPerPx * sizePx;
            const double nameW = t.measureText(name, sizePx, font::sansSemiBold(), sp);
            artboard::Color fg = palette::foreground(), dot = palette::primary();
            fg.a *= alpha;
            dot.a *= alpha;
            t.setFill(fg);
            t.drawText(name, x, baselineY, sizePx, font::sansSemiBold(), sp);
            t.setFill(dot);
            t.drawText(".", x + nameW, baselineY, sizePx, font::sansSemiBold(), sp);   // at the real end of the name
            return nameW + t.measureText(".", sizePx, font::sansSemiBold(), sp);
        }
    };
}
}
