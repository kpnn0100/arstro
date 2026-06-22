#include "Exposure.h"
#include <cmath>

namespace arstro
{
    Exposure::Exposure() : PointProcessor(propertyCount)
    {
        mSmoothEnable = false;
        initProperty(exposureEvID, 0);
        update();
    }

    void Exposure::update()
    {
        mGain = (Pixel)std::pow(2.0, (double)getProperty(exposureEvID));
    }

    void Exposure::processPixel(const Pixel *in, Pixel *out, int channels)
    {
        const int colorCh = channels >= 3 ? 3 : channels;
        for (int c = 0; c < colorCh; ++c)
            out[c] = in[c] * mGain;
        for (int c = colorCh; c < channels; ++c)
            out[c] = in[c];  // pass alpha through
    }
}
