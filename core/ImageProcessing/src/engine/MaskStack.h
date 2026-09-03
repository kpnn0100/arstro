/*
 *  Arstro ImageProcessing Library
 *
 *  MaskStack: applies the local-adjustment masks of an EditParams to a linear-light
 *  framed image, in place. Each mask builds a 0..1 coverage plane from its geometry
 *  (radial / linear / brush / a hand-drawn closed path) — or, for a SEMANTIC mask, from the
 *  pixels themselves — renders an adjusted copy of the image through the same
 *  point/effect processors the global pipeline uses (so a local edit behaves exactly
 *  like its global counterpart), then blends adjusted over base by coverage.
 *
 *  Kept separate from EditEngine so the mask math is a single-responsibility unit
 *  the UI can mirror for its overlay and a video editor can reuse unchanged.
 */
#pragma once
#include "../base/CurvePoint.h"
#include "../base/Image.h"
#include "EditParams.h"
#include <utility>
#include <vector>

namespace arstro
{
    /** Coverage of one mask at normalised framed-image coords (nx,ny in 0..1).
     *
     *  For a `Path` mask this is the HARD-EDGED answer — inside the outline or not. The
     *  feather of a path is a distance from its boundary, which no per-point function can
     *  give without measuring every segment for every pixel; the render builds a coverage
     *  plane instead (`buildMaskCoverage`). Callers that want one point's coverage (a hit
     *  test, an overlay) want the hard answer anyway.
     *
     *  An unknown type answers 0 — which is what a project holding the withdrawn `Semantic`
     *  type (4) gets, and the answer this function has always given for a type it does not
     *  know. */
    float maskCoverage(const MaskParams &m, float nx, float ny);

    /** Flatten a path mask's closed bezier outline to a polygon in normalised coords, in the
     *  order the points are stored. ONE sampler, shared by the render and the editor's
     *  drawing, for the same reason `curve::sample` is shared — two flatteners drift, and a
     *  mask whose drawn outline is not the outline it renders is worse than no mask.
     *
     *  Deliberately does NOT sort by x: a closed outline is not a function of x. Returns
     *  fewer than 3 points (usually none) when there is no area to fill.
     *
     *  `inline`, and here rather than in the .cpp, precisely BECAUSE it is shared: the overlay
     *  that draws the outline is a widget, and a widget must not have to link the whole engine
     *  (with its processors and its thread pool) to ask what shape it is drawing. Nothing in it
     *  needs more than `curve::cubic`. */
    inline std::vector<std::pair<float, float>> maskPathPolygon(const std::vector<CurvePoint> &pts,
                                                                int perSeg = 12)
    {
        std::vector<std::pair<float, float>> out;
        if (pts.size() < 3) return out;      // no area: not a shape yet
        if (perSeg < 1) perSeg = 1;
        const std::size_t n = pts.size();
        out.reserve(n * (std::size_t)perSeg + 1);
        for (std::size_t i = 0; i < n; ++i)
        {
            const CurvePoint &a = pts[i];
            const CurvePoint &b = pts[(i + 1) % n];   // closed: the last segment wraps
            // A corner point ignores its handles, exactly as a corner does on a tone curve —
            // so a polygon drawn with plain clicks stays a polygon and does not bulge.
            const float x1 = a.smooth ? a.x + a.ox : a.x;
            const float y1 = a.smooth ? a.y + a.oy : a.y;
            const float x2 = b.smooth ? b.x + b.ix : b.x;
            const float y2 = b.smooth ? b.y + b.iy : b.y;
            out.push_back({a.x, a.y});
            for (int s = 1; s < perSeg; ++s)
            {
                const float t = (float)s / (float)perSeg;
                out.push_back({curve::cubic(a.x, x1, x2, b.x, t),
                               curve::cubic(a.y, y1, y2, b.y, t)});
            }
        }
        return out;
    }

    /** Build a mask's coverage plane at `w`x`h` (row-major, one value per pixel, 0..1).
     *
     *  A plane rather than a per-pixel call because a path's feather is a distance from its
     *  boundary: measuring it per pixel against every segment is O(pixels x segments), while
     *  filling the outline once and blurring the result is O(pixels) and is also how a real
     *  editor feathers a shape. The other mask types are closed-form, so they still evaluate
     *  per pixel and never allocate a plane — `applyMaskStack` only builds one for `Path`. */
    void buildMaskCoverage(const MaskParams &m, int w, int h, std::vector<Pixel> &out);

    /** Apply every mask in `masks` to `img` (linear light), in order. */
    void applyMaskStack(Image &img, const std::vector<MaskParams> &masks);
}
