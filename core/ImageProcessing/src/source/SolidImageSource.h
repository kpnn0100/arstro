/*
 *  Arstro ImageProcessing Library
 *
 *  SolidImageSource: produces a canvas of one solid colour at a given size —
 *  the simplest ImageSource (useful as a test pattern / backdrop). Colour is
 *  specified in linear light.
 */
#pragma once
#include "../base/ImageSource.h"

namespace arstro
{
    class SolidImageSource : public ImageSource
    {
    public:
        SolidImageSource() = default;

        void setSize(int width, int height) { mWidth = width; mHeight = height; }
        void setChannels(int channels) { mChannels = channels; }
        /** Fill colour in LINEAR light, 0..1 per channel. */
        void setColor(Pixel r, Pixel g, Pixel b, Pixel a = (Pixel)1)
        {
            mR = r; mG = g; mB = b; mA = a;
        }

        Image generate() override;

    private:
        int mWidth = 1, mHeight = 1, mChannels = 3;
        Pixel mR = 0, mG = 0, mB = 0, mA = 1;
    };
}
