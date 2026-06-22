#include "ToneRegions.h"
#include "../base/ColorSpace.h"

namespace arstro
{
    namespace
    {
        inline Pixel clamp01p(Pixel x) { return x < 0 ? 0 : (x > 1 ? 1 : x); }
        inline Pixel smooth(Pixel t)
        {
            t = clamp01p(t);
            return t * t * ((Pixel)3 - (Pixel)2 * t);  // smoothstep
        }
    }

    ToneRegions::ToneRegions() : PointProcessor(propertyCount)
    {
        mSmoothEnable = false;
        initProperty(highlightsID, 0);
        initProperty(shadowsID, 0);
        initProperty(whitesID, 0);
        initProperty(blacksID, 0);
    }

    void ToneRegions::processPixel(const Pixel *in, Pixel *out, int channels)
    {
        const int colorCh = channels >= 3 ? 3 : channels;
        const Pixel hi = getProperty(highlightsID) / (Pixel)100;
        const Pixel sh = getProperty(shadowsID) / (Pixel)100;
        const Pixel wh = getProperty(whitesID) / (Pixel)100;
        const Pixel bl = getProperty(blacksID) / (Pixel)100;

        Pixel r = in[0];
        Pixel g = colorCh >= 3 ? in[1] : in[0];
        Pixel b = colorCh >= 3 ? in[2] : in[0];
        const Pixel L = color::luminance(r, g, b);

        // Region weights over luminance (smooth, overlapping).
        const Pixel wShadow = (Pixel)1 - smooth(L / (Pixel)0.5);
        const Pixel wHigh = smooth((L - (Pixel)0.5) / (Pixel)0.5);
        const Pixel wBlack = (Pixel)1 - smooth(L / (Pixel)0.28);
        const Pixel wWhite = smooth((L - (Pixel)0.72) / (Pixel)0.28);

        // Additive luminance offset (stable near black; chroma preserved well enough).
        const Pixel delta = (Pixel)0.5 * (sh * wShadow + hi * wHigh + wh * wWhite + bl * wBlack);

        for (int c = 0; c < colorCh; ++c)
            out[c] = in[c] + delta;
        for (int c = colorCh; c < channels; ++c)
            out[c] = in[c];
    }
}
