/*
 *  Arstro ImageProcessing Library
 *
 *  ToneRegions: the Highlights / Shadows / Whites / Blacks adjustments (the basic-panel
 *  tone sliders that sit below Exposure/Contrast). A point op driven by the
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

        /** All four regions flat means every weight multiplies zero (R-PREVIEW-6). */
        bool isIdentity() const override
        {
            return paramValue(highlightsID) == (Pixel)0 && paramValue(shadowsID) == (Pixel)0 &&
                   paramValue(whitesID) == (Pixel)0 && paramValue(blacksID) == (Pixel)0;
        }

    protected:
        void processPixel(const Pixel *in, Pixel *out, int channels) override;
    };
}
