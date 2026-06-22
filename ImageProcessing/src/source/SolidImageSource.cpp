#include "SolidImageSource.h"

namespace arstro
{
    Image SolidImageSource::generate()
    {
        Image img(mWidth, mHeight, mChannels, ColorSpace::LinearSRGB);
        const int ch = img.channels();
        const size_t px = img.pixelCount();
        Pixel *d = img.data();
        for (size_t i = 0; i < px; ++i)
        {
            Pixel *p = d + i * ch;
            if (ch >= 1) p[0] = mR;
            if (ch >= 2) p[1] = mG;
            if (ch >= 3) p[2] = mB;
            if (ch >= 4) p[3] = mA;
        }
        return img;
    }
}
