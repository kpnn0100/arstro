/*
 *  Arstro ImageProcessing Library
 *
 *  NoiseReduction: two independent denoisers, mirroring a Detail panel's split.
 *    - Colour NR smooths the chroma (per-channel offset from luminance) with a
 *      Gaussian, removing colour speckle while keeping luminance detail intact.
 *    - Luminance NR runs an edge-preserving bilateral filter on a perceptual
 *      luminance plane, so flat noise is smoothed but real edges are kept; the
 *      luminance change is re-applied to RGB as a ratio (hue preserved).
 *
 *  Params: luminance 0..100, colour 0..100 (0 = bypass that stage).
 */
#pragma once
#include "../base/ImageProcessor.h"

namespace arstro
{
    class NoiseReduction : public ImageProcessor
    {
    public:
        enum PropertyIndex { luminanceID, colorID, propertyCount };
        NoiseReduction();

        void setLuminance(float v) { setProperty(luminanceID, v); }
        void setColor(float v) { setProperty(colorID, v); }

        /** Neither channel means no neighbourhood pass at all (R-PREVIEW-6). */
        bool isIdentity() const override
        {
            return paramValue(luminanceID) <= (Pixel)0 && paramValue(colorID) <= (Pixel)0;
        }

        void process(const Image &in, Image &out) override;
    };
}
