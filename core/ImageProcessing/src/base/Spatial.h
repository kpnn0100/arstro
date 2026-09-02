/*
 *  Arstro ImageProcessing Library
 *
 *  Spatial: shared neighbourhood helpers for the processors that need a blur or a
 *  luminance plane (Sharpen, NoiseReduction, Texture, Clarity). Kept as free
 *  functions so each processor stays single-responsibility and none of them
 *  re-implements a separable Gaussian. All operate on contiguous scalar planes
 *  (one value per pixel) or the RGB channels of an Image; alpha is never touched.
 */
#pragma once
#include "Image.h"
#include "Pixel.h"
#include <vector>

namespace arstro
{
    namespace spatial
    {
        /** Rec.709 luminance of every pixel (linear light) -> w*h plane. */
        void luminancePlane(const Image &img, std::vector<Pixel> &out);

        /** Separable Gaussian blur of a scalar plane, sigma in pixels, edges
         *  clamped. `src` and `dst` may be the same vector. sigma <= 0 copies. */
        void gaussianBlurPlane(const std::vector<Pixel> &src, std::vector<Pixel> &dst,
                               int w, int h, float sigma);

        /** Approximate Gaussian blur of a scalar plane whose cost does NOT grow with sigma:
         *  three running-sum box passes per axis, with the classic Kovesi radii chosen so the
         *  result matches a true Gaussian of the requested sigma to within ~3% of its peak.
         *  `src` and `dst` may be the same vector. sigma <= 0 copies.
         *
         *  Exists because `gaussianBlurPlane` is O(pixels x sigma): a radius that is a FRACTION
         *  of the image (R-MIXER-7) is ~4 px on a preview and ~16 px on a full-resolution export,
         *  and the export would pay for its own size twice. A box cascade costs the same six
         *  passes at any radius. Use the exact kernel where the shape of the tail matters (a
         *  mask's feather is a visible edge); use this where the plane is a WEIGHT and only its
         *  smoothness matters.
         *
         *  Below sigma 4 it DELEGATES to the exact kernel, because the two methods are strong in
         *  opposite regimes: three integer box widths reach only coarse variances (sigma 1 comes
         *  out at 0.82), and a small sigma is precisely where the exact kernel is cheap. So the
         *  accurate method is used wherever it is also the cheap one. */
        void fastBlurPlane(const std::vector<Pixel> &src, std::vector<Pixel> &dst,
                           int w, int h, float sigma);
    }
}
