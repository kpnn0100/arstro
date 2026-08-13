#include "Vibrance.h"
#include "../base/ColorSpace.h"

namespace arstro
{
    Vibrance::Vibrance() : PointProcessor(propertyCount)
    {
        mSmoothEnable = false;
        initProperty(vibranceID, 0);
        initProperty(saturationID, 0);
    }

    void Vibrance::processPixel(const Pixel *in, Pixel *out, int channels)
    {
        const int colorCh = channels >= 3 ? 3 : channels;
        if (colorCh < 3)
        {
            for (int c = 0; c < channels; ++c)
                out[c] = in[c];
            return;
        }
        const Pixel vib = getProperty(vibranceID) / (Pixel)100;
        const Pixel sat = getProperty(saturationID) / (Pixel)100;

        Pixel h, s, l;
        color::rgbToHsl(in[0], in[1], in[2], h, s, l);
        // uniform saturation, then vibrance scaled by remaining headroom (1 - s)
        s = s * ((Pixel)1 + sat);
        s = s + vib * ((Pixel)1 - s);
        if (s < 0) s = 0;
        if (s > 1) s = 1;
        color::hslToRgb(h, s, l, out[0], out[1], out[2]);
        for (int c = 3; c < channels; ++c)
            out[c] = in[c];
    }
}
