/*
 *  Arstro ImageProcessing Library
 *
 *  ColorMixer: per-hue-band HSL adjustment (Lightroom's HSL/Color mixer). Eight
 *  bands (red, orange, yellow, green, aqua, blue, purple, magenta), each with a
 *  hue / saturation / luminance offset. A pixel is adjusted by the weighted blend
 *  of the bands nearest its hue. A point op.
 */
#pragma once
#include "../base/ImageProcessor.h"

namespace arstro
{
    class ColorMixer : public PointProcessor
    {
    public:
        static constexpr int kBands = 8;
        enum PropertyIndex { propertyCount = kBands * 3 };  // [band*3 + {0:hue,1:sat,2:lum}]

        ColorMixer();

        void setBandHue(int band, Pixel v) { setProperty(band * 3 + 0, v); }        // -100..+100
        void setBandSaturation(int band, Pixel v) { setProperty(band * 3 + 1, v); }  // -100..+100
        void setBandLuminance(int band, Pixel v) { setProperty(band * 3 + 2, v); }   // -100..+100

    protected:
        void processPixel(const Pixel *in, Pixel *out, int channels) override;
    };
}
