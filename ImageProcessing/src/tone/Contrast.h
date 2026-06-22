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

        void update() override;

    protected:
        void processPixel(const Pixel *in, Pixel *out, int channels) override;

    private:
        static constexpr Pixel kPivot = (Pixel)0.18;
        Pixel mSlope = 1;
    };
}
