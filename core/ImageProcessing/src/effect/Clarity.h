/*
 *  Arstro ImageProcessing Library
 *
 *  Clarity: midtone local contrast at a LARGE radius — the classic "punch" slider.
 *  A large-radius high-pass of a perceptual luminance plane is added back, gated by
 *  a midtone mask so shadows and highlights are spared (avoids halo/clipping). The
 *  luminance change is re-applied to RGB as a ratio so hue is preserved. Param:
 *  -100..100 (negative softens, for a dreamy look).
 */
#pragma once
#include "../base/ImageProcessor.h"

namespace arstro
{
    class Clarity : public ImageProcessor
    {
    public:
        enum PropertyIndex { amountID, propertyCount };
        Clarity();
        void setAmount(float v) { setProperty(amountID, v); }
        /** Zero amount skips the large-radius blur entirely (R-PREVIEW-6). */
        bool isIdentity() const override { return paramValue(amountID) == (Pixel)0; }

        void process(const Image &in, Image &out) override;
    };
}
