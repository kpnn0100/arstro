/*
 *  Arstro ImageProcessing Library
 *
 *  Grain: a monochrome film-grain overlay. Deterministic value noise (seeded by
 *  pixel coordinate, so it is stable frame-to-frame for stills), strongest in the
 *  midtones, scaled by amount and feature size. A whole-image op.
 */
#pragma once
#include "../base/ImageProcessor.h"

namespace arstro
{
    class Grain : public ImageProcessor
    {
    public:
        enum PropertyIndex
        {
            amountID,
            sizeID,
            propertyCount
        };

        Grain();

        void setAmount(Pixel v) { setProperty(amountID, v); }  // 0..100
        void setSize(Pixel v) { setProperty(sizeID, v); }      // 0..100 (feature size)
        void setSeed(unsigned s) { mSeed = s; }                // VideoProcessor can vary per frame

        void process(const Image &in, Image &out) override;

    private:
        unsigned mSeed = 1;
    };
}
