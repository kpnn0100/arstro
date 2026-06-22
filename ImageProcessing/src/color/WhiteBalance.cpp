#include "WhiteBalance.h"
#include "../base/ColorSpace.h"

namespace arstro
{
    WhiteBalance::WhiteBalance() : PointProcessor(propertyCount)
    {
        mSmoothEnable = false;
        initProperty(tempID, 6500);  // working-space white = identity
        initProperty(tintID, 0);
        update();
    }

    void WhiteBalance::update()
    {
        color::kelvinToRgbGain(getProperty(tempID), getProperty(tintID), mGr, mGg, mGb);
    }

    void WhiteBalance::processPixel(const Pixel *in, Pixel *out, int channels)
    {
        const int colorCh = channels >= 3 ? 3 : channels;
        if (colorCh >= 3)
        {
            out[0] = in[0] * mGr;
            out[1] = in[1] * mGg;
            out[2] = in[2] * mGb;
        }
        else
        {
            for (int c = 0; c < colorCh; ++c)
                out[c] = in[c];  // grayscale: WB is a no-op
        }
        for (int c = colorCh; c < channels; ++c)
            out[c] = in[c];
    }
}
