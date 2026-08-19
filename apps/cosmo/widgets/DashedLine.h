/*
 *  cosmo_v2 by arstro — strokeDashedPolyline: a dashed line, drawn by hand.
 *
 *  Exists for one job: telling a READ-ONLY line apart from an editable one inside the same
 *  plot (D-28). The curve editors draw the effective "final" curve behind the curve you are
 *  actually editing (DR-EDIT-5), and drawn solid at the same weight the two are
 *  indistinguishable — a user grabs the wrong one, gets nothing, and reasonably concludes the
 *  panel is broken. Dashed is the one convention that says "this is a readout" without a
 *  legend, at any size, in any colour.
 *
 *  Hand-rolled because `IRenderTarget` has no dash state and `core/Artboard` is a submodule:
 *  a dash setting is a real engine feature with a web backend and a recording backend to
 *  match, and it should not be introduced as a side effect of a widget fix. Arc-length
 *  stepping, so the dash rhythm stays even through a curve's tight bends instead of bunching
 *  up where the samples do.
 */
#pragma once
#include "../../../core/Artboard/include/artboard/artboard.h"
#include <cmath>
#include <vector>

namespace arstro
{
namespace cosmo_v2
{
    /** `pts` is an already-flattened polyline in the caller's current coordinate space.
     *  A `false` entry in `breakBefore` (optional, same length) starts a new run — the hue
     *  editor's curve wraps at the 360/0 seam and must not be joined across it. */
    inline void strokeDashedPolyline(artboard::IRenderTarget &t,
                                     const std::vector<artboard::Point> &pts,
                                     const artboard::Color &c, double width,
                                     double on = 4.0, double off = 3.5,
                                     const std::vector<bool> *breakBefore = nullptr)
    {
        if (pts.size() < 2 || on <= 0.0 || off < 0.0) return;
        double rem = on;
        bool pen = true;
        for (size_t i = 0; i + 1 < pts.size(); ++i)
        {
            if (breakBefore && i + 1 < breakBefore->size() && (*breakBefore)[i + 1]) continue;
            const artboard::Point a = pts[i], b = pts[i + 1];
            const double dx = b.x - a.x, dy = b.y - a.y;
            const double len = std::sqrt(dx * dx + dy * dy);
            if (len <= 1e-9) continue;
            double pos = 0.0;
            while (pos < len)
            {
                const double step = std::min(rem, len - pos);
                if (pen)
                {
                    const double t0 = pos / len, t1 = (pos + step) / len;
                    t.beginPath();
                    t.moveTo(a.x + dx * t0, a.y + dy * t0);
                    t.lineTo(a.x + dx * t1, a.y + dy * t1);
                    t.setStroke(c, width);
                    t.strokePath();
                }
                pos += step;
                rem -= step;
                if (rem <= 1e-9) { pen = !pen; rem = pen ? on : off; }
            }
        }
    }
}
}
