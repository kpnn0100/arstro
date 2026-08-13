/*
 *  Arstro ImageProcessing Library
 *
 *  ToneRegions: the Highlights / Shadows / Whites / Blacks adjustments (Lightroom's
 *  Basic-panel tone sliders below Exposure/Contrast). A point op driven by the
 *  pixel's luminance: smooth weight masks select each tonal region, and the
 *  adjustment is applied as a luminance RATIO so chroma is preserved.
 */
#pragma once
#include "../base/ImageProcessor.h"

namespace arstro
{
    class ToneRegions : public PointProcessor
    {
    public:
        enum PropertyIndex
        {
            highlightsID,
            shadowsID,
            whitesID,
            blacksID,
            propertyCount
        };

        ToneRegions();

        void setHighlights(Pixel v) { setProperty(highlightsID, v); }  // -100..+100
        void setShadows(Pixel v) { setProperty(shadowsID, v); }        // -100..+100
        void setWhites(Pixel v) { setProperty(whitesID, v); }          // -100..+100
        void setBlacks(Pixel v) { setProperty(blacksID, v); }          // -100..+100

    protected:
        void processPixel(const Pixel *in, Pixel *out, int channels) override;
    };
}
