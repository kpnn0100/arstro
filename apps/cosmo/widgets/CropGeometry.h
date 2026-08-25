/*
 *  cosmo_v2 by arstro — CropGeometry: the crop rectangle's arithmetic, in one place.
 *
 *  `EditParams`' crop is NORMALISED — cropX/Y/W/H are 0..1 of the source image — while an
 *  aspect ratio is a ratio of PIXELS. Converting between the two needs the photo's shape, and
 *  getting it wrong is invisible until someone looks closely: `XformPanel` assumed a square,
 *  said so in a comment, and turned a requested 16:9 on a 3:2 photo into 16:10.7 (R-CROP-1).
 *
 *  Header-only and free functions, deliberately: the panel that types a ratio in and the
 *  overlay that drags a corner have to agree exactly, and the surest way to make two callers
 *  agree is to give them one implementation rather than two careful ones. Everything here is
 *  pure — no widgets, no state — so it is also directly testable.
 */
#pragma once
#include "../../../core/Artboard/include/artboard/artboard.h"
#include <algorithm>
#include <cmath>

namespace arstro
{
namespace cosmo_v2
{
namespace crop
{
    /** The smallest crop, as a fraction of each axis. Small enough never to be in the way,
     *  large enough that the preview pyramid's coarsest level is still more than one pixel:
     *  a level-3 preview is previewEdge/8, so 2% of a 1000 px preview is 2.5 px there
     *  (R-CROP-6). A crop is also clamped to this when a ratio would otherwise flatten it. */
    inline double minSize() { return 0.02; }

    /** The normalised width/height ratio that yields a pixel ratio of `r` on a `sw x sh`
     *  photo. Returns 0 when there is nothing to compute from, which callers read as "free".
     *
     *  This is the one line the whole file exists for: `r = (cw·sw)/(ch·sh)`, so
     *  `cw/ch = r · sh/sw`. */
    inline double normalisedRatio(double r, int sw, int sh)
    {
        if (r <= 0.0 || sw <= 0 || sh <= 0) return 0.0;
        return r * (double)sh / (double)sw;
    }

    /** Clamp `rect` (normalised) into the unit square, keeping at least `minSize()` in each
     *  axis. Position is adjusted before size: a box dragged past the edge should slide back
     *  in, not shrink (R-CROP-6).
     *
     *  **Never use this on a ratio-locked box.** It clamps `w` and `h` INDEPENDENTLY, so
     *  whichever axis overflows is capped on its own and the shape changes — which is exactly
     *  D-51: dragging a locked corner out of the photo collapsed the box to the full frame.
     *  `clampPositionOnly` and `fitKeepingRatio` are the ratio-safe pair. */
    inline artboard::Rect clampToFrame(artboard::Rect rect)
    {
        const double m = minSize();
        rect.w = std::clamp(rect.w, m, 1.0);
        rect.h = std::clamp(rect.h, m, 1.0);
        rect.x = std::clamp(rect.x, 0.0, 1.0 - rect.w);
        rect.y = std::clamp(rect.y, 0.0, 1.0 - rect.h);
        return rect;
    }

    /** Slide `rect` inside the frame WITHOUT touching its size — the only clamp that is safe
     *  on a shape that must be preserved. */
    inline artboard::Rect clampPositionOnly(artboard::Rect rect)
    {
        rect.x = std::clamp(rect.x, 0.0, std::max(0.0, 1.0 - rect.w));
        rect.y = std::clamp(rect.y, 0.0, std::max(0.0, 1.0 - rect.h));
        return rect;
    }

    /** Scale `w`/`h` by ONE factor so a box of ratio `nr` anchored per `maxW`/`maxH` fits, and
     *  enforce the minimum without breaking the shape.
     *
     *  This is the whole of D-51's fix. The failure it replaces was clamping the two axes
     *  separately: `w` hit the frame and stopped while `h` kept following the pointer, so the
     *  ratio drifted — "height still expand but width is limited" — and at a large overshoot
     *  both axes saturated at 1.0 and the box became a square.
     *
     *  `maxW`/`maxH` are how much room there is in the direction the box is growing, which the
     *  caller knows because it knows which handle is anchored where. */
    inline void fitKeepingRatio(double &w, double &h, double nr, double maxW, double maxH)
    {
        const double m = minSize();
        if (nr <= 0.0) return;
        // Grow to the minimum first, as a pair — a floor applied to one axis alone is the same
        // bug in miniature.
        if (w < m) { w = m; h = w / nr; }
        if (h < m) { h = m; w = h * nr; }
        // Then shrink to fit, also as a pair. `s` is one factor for both, which is what makes
        // the shape survive.
        double s = 1.0;
        if (maxW > 0.0 && w > maxW) s = std::min(s, maxW / w);
        if (maxH > 0.0 && h > maxH) s = std::min(s, maxH / h);
        if (s < 1.0) { w *= s; h *= s; }
        // The frame itself is the last word: a ratio so extreme that the minimum cannot fit
        // gets the biggest box that does, still of the right shape.
        double s2 = 1.0;
        if (w > 1.0) s2 = std::min(s2, 1.0 / w);
        if (h > 1.0) s2 = std::min(s2, 1.0 / h);
        if (s2 < 1.0) { w *= s2; h *= s2; }
    }

    /** Reshape `rect` to the normalised ratio `nr` (w/h), keeping its CENTRE and staying inside
     *  the frame. Used when a ratio is picked while a crop already exists: R-CROP-3 says a
     *  photographer who has framed a shot and then asks for 16:9 wants *their* framing at 16:9,
     *  not a fresh centred box over the whole photo.
     *
     *  Chooses the larger of "fit by width" and "fit by height" that still fits, so the result
     *  is the biggest rectangle of that shape around the same centre — shrinking one axis rather
     *  than growing the other keeps the crop inside whatever the user had. */
    inline artboard::Rect applyRatio(artboard::Rect rect, double nr)
    {
        if (nr <= 0.0) return clampToFrame(rect);   // free: shape is whatever it is
        const double cx = rect.x + rect.w * 0.5, cy = rect.y + rect.h * 0.5;
        // Candidate: keep the width, derive the height — unless that is absurdly tall for this
        // box, in which case fit by height instead so the result stays inside what the user had.
        double w = rect.w, h = rect.w / nr;
        if (h > 1.0 || h > rect.h * 4.0) { h = rect.h; w = rect.h * nr; }
        // Grown about the centre, so the room available is twice the distance to the nearer
        // edge on each axis. Scaled as a PAIR (D-51) — never clamped per axis.
        fitKeepingRatio(w, h, nr, 2.0 * std::min(cx, 1.0 - cx), 2.0 * std::min(cy, 1.0 - cy));
        return clampPositionOnly(artboard::Rect{cx - w * 0.5, cy - h * 0.5, w, h});
    }

    /** Which part of the box a point is on. Corners take priority over edges, because a corner
     *  is inside both edges' bands and is the harder target to hit. */
    enum class Part { None, Move, TopLeft, Top, TopRight, Right, BottomRight, Bottom, BottomLeft, Left };

    inline bool isCorner(Part p)
    {
        return p == Part::TopLeft || p == Part::TopRight || p == Part::BottomRight || p == Part::BottomLeft;
    }

    /** Hit-test `p` (in the same pixel space as `box`) against a crop box, with a grab band of
     *  `grab` px on each edge. Returns Move for the interior — R-CROP-3's "move the region",
     *  which is the gesture the user reported missing. */
    inline Part partAt(const artboard::Rect &box, const artboard::Point &p, double grab)
    {
        const bool nearL = std::fabs(p.x - box.x) <= grab;
        const bool nearR = std::fabs(p.x - (box.x + box.w)) <= grab;
        const bool nearT = std::fabs(p.y - box.y) <= grab;
        const bool nearB = std::fabs(p.y - (box.y + box.h)) <= grab;
        const bool inX = p.x >= box.x - grab && p.x <= box.x + box.w + grab;
        const bool inY = p.y >= box.y - grab && p.y <= box.y + box.h + grab;
        if (!inX || !inY) return Part::None;
        if (nearL && nearT) return Part::TopLeft;
        if (nearR && nearT) return Part::TopRight;
        if (nearR && nearB) return Part::BottomRight;
        if (nearL && nearB) return Part::BottomLeft;
        if (nearL) return Part::Left;
        if (nearR) return Part::Right;
        if (nearT) return Part::Top;
        if (nearB) return Part::Bottom;
        const bool inside = p.x > box.x && p.x < box.x + box.w && p.y > box.y && p.y < box.y + box.h;
        return inside ? Part::Move : Part::None;
    }

    /** Resize `rect` (normalised) by dragging `part` to normalised position (nx, ny).
     *
     *  With `nr > 0` the ratio is MAINTAINED rather than the gesture refused (R-CROP-3): a
     *  corner drives both axes from the dominant one, and an edge drags its own axis and lets
     *  the other follow about the box's fixed centre line. That is what makes a locked ratio
     *  feel like a constraint instead of a wall. */
    inline artboard::Rect resizeBy(artboard::Rect rect, Part part, double nx, double ny, double nr)
    {
        const double m = minSize();
        const double l0 = rect.x, t0 = rect.y, r0 = rect.x + rect.w, b0 = rect.y + rect.h;

        // ── FREE: each axis is independent, which is what free means. ────────────────────
        if (nr <= 0.0)
        {
            double l = l0, t = t0, r = r0, b = b0;
            switch (part)
            {
            case Part::Left:        l = std::min(nx, r - m); break;
            case Part::Right:       r = std::max(nx, l + m); break;
            case Part::Top:         t = std::min(ny, b - m); break;
            case Part::Bottom:      b = std::max(ny, t + m); break;
            case Part::TopLeft:     l = std::min(nx, r - m); t = std::min(ny, b - m); break;
            case Part::TopRight:    r = std::max(nx, l + m); t = std::min(ny, b - m); break;
            case Part::BottomRight: r = std::max(nx, l + m); b = std::max(ny, t + m); break;
            case Part::BottomLeft:  l = std::min(nx, r - m); b = std::max(ny, t + m); break;
            default: return rect;
            }
            return clampToFrame(artboard::Rect{l, t, r - l, b - t});
        }

        // ── LOCKED: anchored, and clamped as a PAIR. ─────────────────────────────────────
        //
        // Written around an explicit ANCHOR — the handle opposite the one being dragged, which
        // must not move — and one `fitKeepingRatio` call per case. The version this replaces
        // derived a ratio-correct pair and then handed it to `clampToFrame`, which clamps the
        // axes SEPARATELY: dragging a locked corner out of the photo capped the width, let the
        // height keep following the pointer, and at any real overshoot saturated both at 1.0
        // and turned the box into a square (D-51).
        //
        // A corner grows away from the opposite corner in both axes. An EDGE grows along its
        // own axis from the opposite edge, and the other axis grows about the box's centre line
        // — an edge drag must not appear to slide the box sideways.
        const double cx0 = (l0 + r0) * 0.5, cy0 = (t0 + b0) * 0.5;
        const double roomAboutCx = 2.0 * std::min(cx0, 1.0 - cx0);
        const double roomAboutCy = 2.0 * std::min(cy0, 1.0 - cy0);
        double w = 0.0, h = 0.0;

        switch (part)
        {
        case Part::BottomRight:
            w = nx - l0;  h = w / nr;
            fitKeepingRatio(w, h, nr, 1.0 - l0, 1.0 - t0);
            return clampPositionOnly(artboard::Rect{l0, t0, w, h});
        case Part::TopLeft:
            w = r0 - nx;  h = w / nr;
            fitKeepingRatio(w, h, nr, r0, b0);
            return clampPositionOnly(artboard::Rect{r0 - w, b0 - h, w, h});
        case Part::TopRight:
            w = nx - l0;  h = w / nr;
            fitKeepingRatio(w, h, nr, 1.0 - l0, b0);
            return clampPositionOnly(artboard::Rect{l0, b0 - h, w, h});
        case Part::BottomLeft:
            w = r0 - nx;  h = w / nr;
            fitKeepingRatio(w, h, nr, r0, 1.0 - t0);
            return clampPositionOnly(artboard::Rect{r0 - w, t0, w, h});
        case Part::Right:
            w = nx - l0;  h = w / nr;
            fitKeepingRatio(w, h, nr, 1.0 - l0, roomAboutCy);
            return clampPositionOnly(artboard::Rect{l0, cy0 - h * 0.5, w, h});
        case Part::Left:
            w = r0 - nx;  h = w / nr;
            fitKeepingRatio(w, h, nr, r0, roomAboutCy);
            return clampPositionOnly(artboard::Rect{r0 - w, cy0 - h * 0.5, w, h});
        case Part::Bottom:
            h = ny - t0;  w = h * nr;
            fitKeepingRatio(w, h, nr, roomAboutCx, 1.0 - t0);
            return clampPositionOnly(artboard::Rect{cx0 - w * 0.5, t0, w, h});
        case Part::Top:
            h = b0 - ny;  w = h * nr;
            fitKeepingRatio(w, h, nr, roomAboutCx, b0);
            return clampPositionOnly(artboard::Rect{cx0 - w * 0.5, b0 - h, w, h});
        default:
            return rect;
        }
    }

    /** Move `rect` (normalised) so its top-left is (nx, ny), clamped inside the frame.
     *  Position only — a move never changes the shape, whatever the ratio lock says. */
    inline artboard::Rect moveTo(artboard::Rect rect, double nx, double ny)
    {
        rect.x = nx;
        rect.y = ny;
        return clampToFrame(rect);
    }
}
}
}
