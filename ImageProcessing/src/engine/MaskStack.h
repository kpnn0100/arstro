/*
 *  Arstro ImageProcessing Library
 *
 *  MaskStack: applies the local-adjustment masks of an EditParams to a linear-light
 *  framed image, in place. Each mask builds a 0..1 coverage plane from its geometry
 *  (radial / linear / brush), renders an adjusted copy of the image through the same
 *  point/effect processors the global pipeline uses (so a local edit behaves exactly
 *  like its global counterpart), then blends adjusted over base by coverage.
 *
 *  Kept separate from EditEngine so the mask math is a single-responsibility unit
 *  the UI can mirror for its overlay and a video editor can reuse unchanged.
 */
#pragma once
#include "../base/Image.h"
#include "EditParams.h"
#include <vector>

namespace arstro
{
    /** Coverage of one mask at normalised framed-image coords (nx,ny in 0..1). */
    float maskCoverage(const MaskParams &m, float nx, float ny);

    /** Apply every mask in `masks` to `img` (linear light), in order. */
    void applyMaskStack(Image &img, const std::vector<MaskParams> &masks);
}
