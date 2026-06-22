/*
 *  Arstro ImageProcessing Library
 *
 *  Dehaze: a simplified dark-channel-prior dehaze. Estimates an atmospheric light
 *  from the haziest pixels and recovers J = (I - A)/t + A, where the transmission
 *  t comes from the per-pixel dark channel. Positive amount removes haze (more
 *  contrast); negative amount adds haze (fades toward the airlight). Whole-image.
 */
#pragma once
#include "../base/ImageProcessor.h"

namespace arstro
{
    class Dehaze : public ImageProcessor
    {
    public:
        enum PropertyIndex
        {
            amountID,
            propertyCount
        };

        Dehaze();

        void setAmount(Pixel v) { setProperty(amountID, v); }  // -100..+100

        void process(const Image &in, Image &out) override;
    };
}
