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
    }
}
