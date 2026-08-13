/*
 *  Arstro ImageProcessing Library
 *
 *  Crop: extract a sub-rectangle given in NORMALIZED coordinates (0..1), so the
 *  crop is resolution-independent (preview and full-res agree). A whole-image op
 *  that changes the output dimensions; no resampling.
 */
#pragma once
#include "../base/ImageProcessor.h"

namespace arstro
{
    class Crop : public ImageProcessor
    {
    public:
        enum PropertyIndex { xID, yID, wID, hID, propertyCount };

        Crop();

        /** Normalized crop rect (0..1). Default is the full frame. */
        void setRect(Pixel x, Pixel y, Pixel w, Pixel h)
        {
            setProperty(xID, x); setProperty(yID, y);
            setProperty(wID, w); setProperty(hID, h);
        }
        void reset() { setRect(0, 0, 1, 1); }

        void process(const Image &in, Image &out) override;
    };
}
