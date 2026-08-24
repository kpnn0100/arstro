/*
 *  Arstro ImageProcessing Library
 *
 *  Contrast: scale linear-light RGB around a fixed mid-grey pivot. A point op.
 *  out = (in - pivot) * slope + pivot, with slope = 1 + amount/100 and the pivot
 *  at 18% grey (0.18 in linear), so a pixel at mid-grey is unchanged while values
 *  above/below move apart (positive amount) or together (negative amount).
 */
#pragma once
#include "../base/ImageProcessor.h"

namespace arstro
{
    class Contrast : public PointProcessor
    {
    public:
        enum PropertyIndex
        {
            contrastID,
            propertyCount
        };

        Contrast();

        /** Contrast amount, nominally -100..+100 (0 = identity). */
        void setContrast(Pixel amount) { setProperty(contrastID, amount); }

        /** 0 makes the slope exactly 1 — and `(x - pivot) * 1 + pivot` is not even
         *  bit-identical to x in float, so skipping is the MORE accurate answer as well
         *  as the free one (R-PREVIEW-6). */
        bool isIdentity() const override { return paramValue(contrastID) == (Pixel)0; }

        void update() override;

    protected:
        void processPixel(const Pixel *in, Pixel *out, int channels) override;

    private:
        static constexpr Pixel kPivot = (Pixel)0.18;
        Pixel mSlope = 1;
    };
}
