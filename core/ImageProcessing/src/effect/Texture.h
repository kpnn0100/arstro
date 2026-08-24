/*
 *  Arstro ImageProcessing Library
 *
 *  Texture: fine-detail local contrast (small radius). Positive adds crispness to
 *  fine structure (foliage, fabric); negative smooths it (skin) without the large
 *  haloing of Clarity. A high-pass of a perceptual luminance plane is added back;
 *  the change is re-applied to RGB as a ratio so hue is preserved. Param: -100..100.
 */
#pragma once
#include "../base/ImageProcessor.h"

namespace arstro
{
    class Texture : public ImageProcessor
    {
    public:
        enum PropertyIndex { amountID, propertyCount };
        Texture();
        void setAmount(float v) { setProperty(amountID, v); }
        /** Zero amount skips the luminance plane and its blur, not just the blend
         *  (R-PREVIEW-6). */
        bool isIdentity() const override { return paramValue(amountID) == (Pixel)0; }

        void process(const Image &in, Image &out) override;
    };
}
