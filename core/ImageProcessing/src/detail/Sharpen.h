/*
 *  Arstro ImageProcessing Library
 *
 *  Sharpen: capture/output sharpening by unsharp mask. Works on a PERCEPTUAL
 *  (gamma-encoded) luminance plane so the effect is even across the tonal range
 *  and never shifts hue (the per-pixel luminance change is re-applied to RGB as a
 *  ratio). `masking` suppresses sharpening in flat areas (skies, skin) so noise is
 *  not amplified, as a Detail panel's masking slider does.
 *
 *  Params: amount 0..150 (% of the high-pass added), radius 0.5..3 px (blur sigma),
 *  masking 0..100 (0 = sharpen everything, 100 = strong edges only).
 */
#pragma once
#include "../base/ImageProcessor.h"

namespace arstro
{
    class Sharpen : public ImageProcessor
    {
    public:
        enum PropertyIndex { amountID, radiusID, maskingID, propertyCount };
        Sharpen();

        void setAmount(float v) { setProperty(amountID, v); }
        void setRadius(float px) { setProperty(radiusID, px); }
        void setMasking(float v) { setProperty(maskingID, v); }

        void process(const Image &in, Image &out) override;
    };
}
