/*
 *  cosmo_v2 by arstro — a rounded rectangle with an INDEPENDENT radius per
 *  corner. Artboard's drawRoundedRect rounds all four corners uniformly; the
 *  before/split/after switch (and any segmented-control highlight that must
 *  hug one edge of its tray) needs to round only the corners that touch the
 *  container edge and leave the interior corners square. This is pure path
 *  composition over the existing IRenderTarget primitives (no HAL change),
 *  the same approach Icons.h takes -- so it lives in the app layer rather than
 *  in Artboard core.
 */
#pragma once
#include "../../Artboard/src/render/RenderTarget.h"
#include "../../Artboard/src/core/Geometry.h"
#include "../../Artboard/src/scene/Shapes.h"

namespace arstro
{
namespace cosmo_v2
{
    /** Fill/stroke `rect` with per-corner radii (tl, tr, br, bl). Each radius is
     *  clamped to half the smaller side. Corner order matches CSS border-radius:
     *  top-left, top-right, bottom-right, bottom-left. */
    inline void drawRoundedRectCorners(artboard::IRenderTarget &t, const artboard::Rect &rect,
                                       double tl, double tr, double br, double bl,
                                       const artboard::Paint &paint)
    {
        const double x = rect.x, y = rect.y, w = rect.w, h = rect.h;
        const double half = (w < h ? w : h) * 0.5;
        auto clamp = [half](double r) { return r < 0 ? 0.0 : (r > half ? half : r); };
        tl = clamp(tl); tr = clamp(tr); br = clamp(br); bl = clamp(bl);

        t.beginPath();
        t.moveTo(x + tl, y);
        t.lineTo(x + w - tr, y);
        if (tr > 0) t.quadTo(x + w, y, x + w, y + tr); else t.lineTo(x + w, y);
        t.lineTo(x + w, y + h - br);
        if (br > 0) t.quadTo(x + w, y + h, x + w - br, y + h); else t.lineTo(x + w, y + h);
        t.lineTo(x + bl, y + h);
        if (bl > 0) t.quadTo(x, y + h, x, y + h - bl); else t.lineTo(x, y + h);
        t.lineTo(x, y + tl);
        if (tl > 0) t.quadTo(x, y, x + tl, y); else t.lineTo(x, y);
        t.closePath();
        artboard::applyPaint(t, paint);
    }
}
}
