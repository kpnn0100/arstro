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
     *  in, not shrink (R-CROP-6). */
    inline artboard::Rect clampToFrame(artboard::Rect rect)
    {
        const double m = minSize();
        rect.w = std::clamp(rect.w, m, 1.0);
        rect.h = std::clamp(rect.h, m, 1.0);
        rect.x = std::clamp(rect.x, 0.0, 1.0 - rect.w);
        rect.y = std::clamp(rect.y, 0.0, 1.0 - rect.h);
        return rect;
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
        // Candidate A: keep the width, derive the height. Candidate B: the other way round.
        double w = rect.w, h = rect.w / nr;
        if (h > 1.0 || h > rect.h * 4.0)   // absurdly tall for this box: fit by height instead
        { h = rect.h; w = rect.h * nr; }
        // Whichever we chose, it must fit in the frame; scale both down together if not.
        const double over = std::max(w > 1.0 ? w : 1.0, h > 1.0 ? h : 1.0);
        w /= over; h /= over;
        artboard::Rect out{cx - w * 0.5, cy - h * 0.5, w, h};
        return clampToFrame(out);
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
        double l = rect.x, t = rect.y, r = rect.x + rect.w, b = rect.y + rect.h;
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
        artboard::Rect out{l, t, r - l, b - t};
        if (nr > 0.0)
        {
            // Keep the ratio by driving the SHORTER-changing axis from the other, anchored on
            // the corner/edge that did not move — so the handle under the pointer stays under
            // the pointer and the opposite side stays put.
            const bool horizontalDrag = part == Part::Left || part == Part::Right;
            const bool verticalDrag = part == Part::Top || part == Part::Bottom;
            double w = out.w, h = out.h;
            if (horizontalDrag)      h = w / nr;
            else if (verticalDrag)   w = h * nr;
            else                     h = w / nr;   // a corner: width leads, height follows
            if (h < m) { h = m; w = h * nr; }
            if (w < m) { w = m; h = w / nr; }
            // Re-anchor: whichever side was dragged keeps its new position.
            const double ax = (part == Part::Left || part == Part::TopLeft || part == Part::BottomLeft)
                                  ? (out.x + out.w) - w : out.x;
            // For a horizontal edge drag the height changed, so grow/shrink about the centre
            // line — an edge drag should not appear to move the box sideways.
            const double ay = verticalDrag || part == Part::TopLeft || part == Part::TopRight
                                  ? (out.y + out.h) - h
                                  : (horizontalDrag ? out.y + (out.h - h) * 0.5 : out.y);
            out = artboard::Rect{ax, ay, w, h};
        }
        return clampToFrame(out);
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
