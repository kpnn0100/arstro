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
#include "../analysis/Segmenter.h"
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
     *  For a `Semantic` mask it returns **0**, and there is no honest alternative: the coverage
     *  is decided from the pixels and their neighbours, and this function has neither. A caller
     *  that needs a semantic mask's coverage must ask `buildMaskCoverage` with the image — which
     *  is why that overload exists and why the on-photo overlay does not draw one (R-AISEG-7:
     *  there is no geometry to drag). */
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

    /** One mask's boundary, ready to draw: the `threshold` contour of its coverage, as closed
     *  loops of points in NORMALISED framed-image coordinates — the same 0..1 space every mask's
     *  geometry lives in, so the overlay maps it with the transform it already has.
     *
     *  Geometry, not pixels. That distinction is the whole reason this type exists: a mask whose
     *  region is COMPUTED (a semantic mask, or a feathered path) has no control points a view
     *  could draw, and the alternative — handing the view a coverage plane — would put pixels
     *  somewhere the architecture says pixels do not go. A few hundred points do go there. */
    struct MaskOutline
    {
        int maskIndex = -1;
        std::vector<std::vector<std::pair<float, float>>> loops;
    };

    /** Trace the `threshold` contour of a coverage plane into closed loops (marching squares).
     *
     *  `maxEdge` is the SHORT edge of the grid it walks, not of the plane: a coverage plane is
     *  smooth by construction (R-AISEG-4 softens it, a path's feather blurs it), so tracing it at
     *  full preview resolution would spend thousands of points describing a curve that a few
     *  hundred already describe — and every one of them would have to be transformed and stroked
     *  every frame. `minLoopPoints` drops specks: a classifier's output has them, and a mask
     *  outlined with confetti reads as broken even when the coverage is right.
     *
     *  Loops are CLOSED where the region does not touch the frame edge and open where it does;
     *  the caller strokes them either way, so the distinction never has to be reported. */
    std::vector<std::vector<std::pair<float, float>>>
    traceCoverageOutline(const std::vector<Pixel> &cov, int w, int h, float threshold = 0.5f,
                         int maxEdge = 320, int minLoopPoints = 6);

    /** Build a mask's coverage plane at `w`x`h` (row-major, one value per pixel, 0..1).
     *
     *  A plane rather than a per-pixel call because a path's feather is a distance from its
     *  boundary: measuring it per pixel against every segment is O(pixels x segments), while
     *  filling the outline once and blurring the result is O(pixels) and is also how a real
     *  editor feathers a shape. The other mask types are closed-form, so they still evaluate
     *  per pixel and never allocate a plane — `applyMaskStack` only builds one for `Path`. */
    void buildMaskCoverage(const MaskParams &m, int w, int h, std::vector<Pixel> &out);

    /** The same, for a mask whose coverage depends on the PICTURE (R-AISEG-1). `img` is the
     *  linear-light framed image the mask stack is being applied to, `w`/`h` come from it, and
     *  `seg` is the optional real-model seam: it is asked first and may DECLINE, in which case
     *  the built-in classifier answers (R-AISEG-6).
     *
     *  Every other mask type falls through to the geometric overload, so one call site serves
     *  all five and a caller never has to know which kind it is holding. */
    void buildMaskCoverage(const MaskParams &m, const Image &img, std::vector<Pixel> &out,
                           ISegmenter *seg = nullptr);

    /** Apply every mask in `masks` to `img` (linear light), in order. `seg` is the optional
     *  segmentation seam a host installed (R-AISEG-6); nullptr means the built-in answers.
     *
     *  `outlines`, when given, collects the boundary of every mask whose coverage was BUILT as a
     *  plane here — which is exactly the set the view cannot draw from the parameters alone.
     *  Produced during the render rather than on request, because the plane exists for one
     *  instant and rebuilding it later would mean segmenting the photo a second time to answer a
     *  question the render has already answered. */
    void applyMaskStack(Image &img, const std::vector<MaskParams> &masks, ISegmenter *seg = nullptr,
                        std::vector<MaskOutline> *outlines = nullptr);
}
