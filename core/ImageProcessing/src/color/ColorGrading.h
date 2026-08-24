/*
 *  Arstro ImageProcessing Library
 *
 *  ColorGrading: three-way colour wheels (shadows / midtones / highlights), each a
 *  hue + saturation + luminance, plus a hue-RANGE REMAP that rotates an input hue
 *  window toward a target hue (e.g. red->orange, blue->teal). A point op.
 */
#pragma once
#include "../base/ImageProcessor.h"

namespace arstro
{
    class ColorGrading : public PointProcessor
    {
    public:
        enum Region { Shadows = 0, Midtones = 1, Highlights = 2 };
        enum PropertyIndex
        {
            shadowHueID, shadowSatID, shadowLumID,
            midHueID, midSatID, midLumID,
            highHueID, highSatID, highLumID,
            balanceID,
            remapEnableID, remapSrcHueID, remapRangeID, remapDstHueID, remapStrengthID,
            propertyCount
        };

        ColorGrading();

        void setGradeHue(Region r, Pixel deg) { setProperty(shadowHueID + r * 3, deg); }      // 0..360
        void setGradeSaturation(Region r, Pixel v) { setProperty(shadowSatID + r * 3, v); }   // 0..100
        void setGradeLuminance(Region r, Pixel v) { setProperty(shadowLumID + r * 3, v); }    // -100..+100
        void setBalance(Pixel v) { setProperty(balanceID, v); }                                // -100..+100

        void setHueRemapEnabled(bool on) { setProperty(remapEnableID, on ? 1 : 0); }
        void setHueRemap(Pixel srcHueDeg, Pixel rangeDeg, Pixel dstHueDeg, Pixel strength01)
        {
            setProperty(remapSrcHueID, srcHueDeg);
            setProperty(remapRangeID, rangeDeg);
            setProperty(remapDstHueID, dstHueDeg);
            setProperty(remapStrengthID, strength01);
        }

        /** A wheel with no saturation and no luminance contributes nothing whatever its
         *  hue, and `balance` only moves the crossover BETWEEN contributions — so the
         *  three wheels being flat is identity regardless of it. The hue remap counts
         *  only when it is both enabled and has strength (R-PREVIEW-6). */
        bool isIdentity() const override
        {
            for (int region = 0; region < 3; ++region)
                if (paramValue(shadowSatID + region * 3) != (Pixel)0 ||
                    paramValue(shadowLumID + region * 3) != (Pixel)0)
                    return false;
            return paramValue(remapEnableID) <= (Pixel)0.5 || paramValue(remapStrengthID) == (Pixel)0;
        }

    protected:
        void processPixel(const Pixel *in, Pixel *out, int channels) override;
    };
}
