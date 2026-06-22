/*
 *  Arstro ImageProcessing Library
 *
 *  Rotate: lossless 90-degree steps (quarterTurns, clockwise) plus an arbitrary
 *  straighten angle (degrees) applied by inverse-mapping with bilinear sampling.
 *  Pixels that map outside the source become transparent. A whole-image op.
 */
#pragma once
#include "../base/ImageProcessor.h"

namespace arstro
{
    class Rotate : public ImageProcessor
    {
    public:
        enum PropertyIndex { angleID, quarterTurnsID, propertyCount };

        Rotate();

        void setAngle(Pixel degrees) { setProperty(angleID, degrees); }  // straighten, e.g. -45..+45
        void setQuarterTurns(int turns) { setProperty(quarterTurnsID, (Pixel)(turns & 3)); }

        void process(const Image &in, Image &out) override;
    };
}
