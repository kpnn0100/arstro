/*
 *  Arstro ImageProcessing Library
 *
 *  MaskStack: applies the local-adjustment masks of an EditParams to a linear-light
 *  framed image, in place. Each mask builds a 0..1 coverage plane from its geometry
 *  (radial / linear / brush / a hand-drawn closed path), renders an adjusted copy of the image through the same
 *  point/effect processors the global pipeline uses (so a local edit behaves exactly
 *  like its global counterpart), then blends adjusted over base by coverage.
 *
 *  Kept separate from EditEngine so the mask math is a single-responsibility unit
 *  the UI can mirror for its overlay and a video editor can reuse unchanged.
 */
#pragma once
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
     *  test, an overlay) want the hard answer anyway. */
    float maskCoverage(const MaskParams &m, float nx, float ny);

    /** Flatten a path mask's closed bezier outline to a polygon in normalised coords, in the
     *  order the points are stored. ONE sampler, shared by the render and the editor's
     *  drawing, for the same reason `curve::sample` is shared — two flatteners drift, and a
     *  mask whose drawn outline is not the outline it renders is worse than no mask.
     *
     *  Deliberately does NOT sort by x: a closed outline is not a function of x. Returns
     *  fewer than 3 points (usually none) when there is no area to fill. */
    std::vector<std::pair<float, float>> maskPathPolygon(const std::vector<CurvePoint> &pts,
                                                         int perSeg = 12);

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
