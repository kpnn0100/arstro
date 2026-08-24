/*
 *  Arstro ImageProcessing Library
 *
 *  Vibrance: Saturation (uniform) + Vibrance (saturation-weighted, so already-
 *  saturated pixels move less). Operates in HSL. A point op.
 */
#pragma once
#include "../base/ImageProcessor.h"

namespace arstro
{
    class Vibrance : public PointProcessor
    {
    public:
        enum PropertyIndex
        {
            vibranceID,
            saturationID,
            propertyCount
        };

        Vibrance();

        void setVibrance(Pixel v) { setProperty(vibranceID, v); }      // -100..+100
        void setSaturation(Pixel v) { setProperty(saturationID, v); }  // -100..+100

        /** Neither a vibrance nor a saturation move means no HSL round trip at all —
         *  which also skips `rgbToHsl`/`hslToRgb`, not just a multiply (R-PREVIEW-6). */
        bool isIdentity() const override
        {
            return paramValue(vibranceID) == (Pixel)0 && paramValue(saturationID) == (Pixel)0;
        }

    protected:
        void processPixel(const Pixel *in, Pixel *out, int channels) override;
    };
}
