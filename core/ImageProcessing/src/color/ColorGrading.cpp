#include "ColorGrading.h"
#include "../base/ColorSpace.h"
#include <cmath>

namespace arstro
{
    namespace
    {
        inline Pixel clamp01p(Pixel x) { return x < 0 ? 0 : (x > 1 ? 1 : x); }
        inline Pixel smooth(Pixel t)
        {
            t = clamp01p(t);
            return t * t * ((Pixel)3 - (Pixel)2 * t);
        }
    }

    ColorGrading::ColorGrading() : PointProcessor(propertyCount)
    {
        mSmoothEnable = false;
        for (int i = 0; i < propertyCount; ++i)
            initProperty(i, 0);
    }

    void ColorGrading::processPixel(const Pixel *in, Pixel *out, int channels)
    {
        const int colorCh = channels >= 3 ? 3 : channels;
        if (colorCh < 3)
        {
            for (int c = 0; c < channels; ++c) out[c] = in[c];
            return;
        }
        Pixel r = in[0], g = in[1], b = in[2];
        const Pixel L = color::luminance(r, g, b);

        // luminance region weights; balance shifts the crossover.
        const Pixel pivot = (Pixel)0.5 + getProperty(balanceID) / (Pixel)100 * (Pixel)0.25;
        const Pixel wHigh = smooth((L - pivot) / (Pixel)0.5);
        const Pixel wShadow = (Pixel)1 - smooth(L / (pivot * (Pixel)2));
        Pixel wMid = (Pixel)1 - wHigh - wShadow;
        if (wMid < 0) wMid = 0;

        const Pixel rw[3] = {wShadow, wMid, wHigh};
        for (int region = 0; region < 3; ++region)
        {
            const Pixel hue = getProperty(shadowHueID + region * 3);
            const Pixel sat = getProperty(shadowSatID + region * 3) / (Pixel)100;
            const Pixel lum = getProperty(shadowLumID + region * 3) / (Pixel)100;
            const Pixel w = rw[region];
            if (sat > (Pixel)1e-4)
            {
                Pixel tr, tg, tb;
                color::hslToRgb(hue, sat, (Pixel)0.5, tr, tg, tb);  // tint colour
                r += (tr - (Pixel)0.5) * w * sat;
                g += (tg - (Pixel)0.5) * w * sat;
                b += (tb - (Pixel)0.5) * w * sat;
            }
            const Pixel dl = lum * w * (Pixel)0.3;
            r += dl; g += dl; b += dl;
        }

        // hue-range remap
        if (getProperty(remapEnableID) > (Pixel)0.5)
        {
            const Pixel range = getProperty(remapRangeID);
            if (range > (Pixel)1e-3)
            {
                Pixel h, s, l;
                color::rgbToHsl(r, g, b, h, s, l);
                const Pixel dist = std::fabs(color::hueDelta(h, getProperty(remapSrcHueID)));
                if (dist < range)
                {
                    const Pixel w = (Pixel)1 - dist / range;  // triangular falloff
                    const Pixel toDst = color::hueDelta(h, getProperty(remapDstHueID));
                    h += w * getProperty(remapStrengthID) * toDst;
                    if (h < 0) h += 360;
                    if (h >= 360) h -= 360;
                    color::hslToRgb(h, s, l, r, g, b);
                }
            }
        }

        out[0] = r; out[1] = g; out[2] = b;
        for (int c = 3; c < channels; ++c) out[c] = in[c];
    }
}
