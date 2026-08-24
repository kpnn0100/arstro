/*
 *  Arstro ImageProcessing Library
 *
 *  WhiteBalance: Temperature (Kelvin) + Tint (green<->magenta) as luminance-
 *  preserving per-channel gains in linear light. A point op.
 */
#pragma once
#include "../base/ImageProcessor.h"

namespace arstro
{
    class WhiteBalance : public PointProcessor
    {
    public:
        enum PropertyIndex
        {
            tempID,
            tintID,
            propertyCount
        };

        WhiteBalance();

        void setTemperature(Pixel kelvin) { setProperty(tempID, kelvin); }  // 2000..50000
        void setTint(Pixel tint) { setProperty(tintID, tint); }             // -150..+150

        /** The working-space white with no tint gives gains of exactly 1 after
         *  `kelvinToRgbGain`'s luminance normalisation (R-PREVIEW-6). */
        bool isIdentity() const override
        {
            return paramValue(tempID) == (Pixel)6500 && paramValue(tintID) == (Pixel)0;
        }

        void update() override;

    protected:
        void processPixel(const Pixel *in, Pixel *out, int channels) override;

    private:
        Pixel mGr = 1, mGg = 1, mGb = 1;
    };
}
