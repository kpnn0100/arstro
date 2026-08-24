/*
 *  Arstro ImageProcessing Library
 *
 *  LensCorrection: manual geometric lens fixes, applied as a single resampling
 *  pass (a geometry op, like Crop/Rotate). Three controls, all centred on the
 *  frame and scaled by normalised radius (corner = 1):
 *    - distortion -100..100: radial magnify (+) / shrink (-) of the edges,
 *      correcting pincushion / barrel.
 *    - ca (chromatic aberration) -100..100: opposing radial scale of the red and
 *      blue planes vs green, removing lateral colour fringing at high-contrast edges.
 *    - vignette -100..100: corner darkening (-) / brightening (+), an r^2 falloff.
 *  Identity at all-zero (bilinear sampling at integer coords is exact).
 */
#pragma once
#include "../base/ImageProcessor.h"

namespace arstro
{
    class LensCorrection : public ImageProcessor
    {
    public:
        enum PropertyIndex { distortionID, caID, vignetteID, propertyCount };
        LensCorrection();

        void setDistortion(float v) { setProperty(distortionID, v); }
        void setChromaticAberration(float v) { setProperty(caID, v); }
        void setVignette(float v) { setProperty(vignetteID, v); }

        /** No distortion, no CA, no vignette (R-PREVIEW-6). */
        bool isIdentity() const override
        {
            return paramValue(distortionID) == (Pixel)0 && paramValue(caID) == (Pixel)0 &&
                   paramValue(vignetteID) == (Pixel)0;
        }

        void process(const Image &in, Image &out) override;
    };
}
