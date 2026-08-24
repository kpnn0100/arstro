/*
 *  Arstro ImageProcessing Library
 *
 *  Exposure: multiply linear-light RGB by 2^EV. A point op (mirrors Gain in the
 *  DSP library). Operating in LINEAR light is what makes +1 EV exactly double the
 *  signal — the headline correctness property the unit tests assert.
 */
#pragma once
#include "../base/ImageProcessor.h"

namespace arstro
{
    class Exposure : public PointProcessor
    {
    public:
        enum PropertyIndex
        {
            exposureEvID,
            propertyCount
        };

        Exposure();

        /** Exposure compensation in stops (EV), nominally -5..+5. */
        void setExposureEv(Pixel ev) { setProperty(exposureEvID, ev); }

        /** +0 EV is a gain of exactly 1, so the pass is a copy (R-PREVIEW-6). */
        bool isIdentity() const override { return paramValue(exposureEvID) == (Pixel)0; }

        void update() override;

    protected:
        void processPixel(const Pixel *in, Pixel *out, int channels) override;

    private:
        Pixel mGain = 1;
    };
}
