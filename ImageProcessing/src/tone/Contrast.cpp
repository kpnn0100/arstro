#include "Contrast.h"

namespace arstro
{
    Contrast::Contrast() : PointProcessor(propertyCount)
    {
        mSmoothEnable = false;
        initProperty(contrastID, 0);
        update();
    }

    void Contrast::update()
    {
        mSlope = (Pixel)1 + getProperty(contrastID) / (Pixel)100;
    }

    void Contrast::processPixel(const Pixel *in, Pixel *out, int channels)
    {
        const int colorCh = channels >= 3 ? 3 : channels;
        for (int c = 0; c < colorCh; ++c)
            out[c] = (in[c] - kPivot) * mSlope + kPivot;
        for (int c = colorCh; c < channels; ++c)
            out[c] = in[c];
    }
}
