#include "ColorMixer.h"
#include "../base/ColorSpace.h"
#include <cmath>

namespace arstro
{
    namespace
    {
        // Band hue centers (degrees), matching Lightroom's 8 HSL bands.
        const Pixel kCenter[ColorMixer::kBands] = {0, 30, 60, 120, 180, 240, 280, 320};
        constexpr Pixel kHalfWidth = 60;  // triangular window half-width (overlaps neighbors)
    }

    ColorMixer::ColorMixer() : PointProcessor(propertyCount)
    {
        mSmoothEnable = false;
        for (int i = 0; i < propertyCount; ++i)
            initProperty(i, 0);
    }

    void ColorMixer::processPixel(const Pixel *in, Pixel *out, int channels)
    {
        const int colorCh = channels >= 3 ? 3 : channels;
        if (colorCh < 3)
        {
            for (int c = 0; c < channels; ++c) out[c] = in[c];
            return;
        }
        Pixel h, s, l;
        color::rgbToHsl(in[0], in[1], in[2], h, s, l);

        Pixel wsum = 0, dHue = 0, satFac = 0, dLum = 0;
        for (int b = 0; b < kBands; ++b)
        {
            const Pixel dist = std::fabs(color::hueDelta(h, kCenter[b]));
            if (dist >= kHalfWidth) continue;
            const Pixel w = (Pixel)1 - dist / kHalfWidth;
            wsum += w;
            dHue += w * getProperty(b * 3 + 0) / (Pixel)100 * (Pixel)30;  // +-30 deg
            satFac += w * getProperty(b * 3 + 1) / (Pixel)100;
            dLum += w * getProperty(b * 3 + 2) / (Pixel)100 * (Pixel)0.5;
        }
        if (wsum > (Pixel)1e-6)
        {
            const Pixel inv = (Pixel)1 / wsum;
            h += dHue * inv;
            s *= (Pixel)1 + satFac * inv;
            l += dLum * inv;
        }
        if (h < 0) h += 360;
        if (h >= 360) h -= 360;
        if (s < 0) s = 0; if (s > 1) s = 1;
        if (l < 0) l = 0; if (l > 1) l = 1;
        color::hslToRgb(h, s, l, out[0], out[1], out[2]);
        for (int c = 3; c < channels; ++c) out[c] = in[c];
    }
}
