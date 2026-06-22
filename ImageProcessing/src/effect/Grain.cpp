#include "Grain.h"
#include "../base/ColorSpace.h"
#include <cmath>

namespace arstro
{
    namespace
    {
        // Deterministic [-1,1] hash for an integer cell coordinate.
        Pixel cellNoise(int ix, int iy, unsigned seed)
        {
            unsigned h = (unsigned)ix * 374761393u + (unsigned)iy * 668265263u + seed * 362437u;
            h = (h ^ (h >> 13)) * 1274126177u;
            return ((Pixel)(h & 0xffffu) / (Pixel)65535) * (Pixel)2 - (Pixel)1;
        }
    }

    Grain::Grain() : ImageProcessor(propertyCount)
    {
        mSmoothEnable = false;
        initProperty(amountID, 0);
        initProperty(sizeID, 0);
    }

    void Grain::process(const Image &in, Image &out)
    {
        out.resizeLike(in);
        const Pixel amount = getProperty(amountID) / (Pixel)100;
        const int ch = in.channels();
        const int colorCh = ch >= 3 ? 3 : ch;
        if (amount <= (Pixel)0)  // identity
        {
            const size_t n = (size_t)in.width() * in.height() * ch;
            for (size_t i = 0; i < n; ++i) out.data()[i] = in.data()[i];
            return;
        }
        const Pixel cell = (Pixel)1 + getProperty(sizeID) / (Pixel)100 * (Pixel)4;  // 1..5 px
        const Pixel strength = amount * (Pixel)0.15;
        const int w = in.width(), h = in.height();
        for (int y = 0; y < h; ++y)
        {
            const Pixel fy = (Pixel)y / cell;
            const int y0 = (int)std::floor(fy);
            const Pixel ty = fy - (Pixel)y0;
            for (int x = 0; x < w; ++x)
            {
                const Pixel fx = (Pixel)x / cell;
                const int x0 = (int)std::floor(fx);
                const Pixel tx = fx - (Pixel)x0;
                const Pixel n = lerp(lerp(cellNoise(x0, y0, mSeed), cellNoise(x0 + 1, y0, mSeed), tx),
                                     lerp(cellNoise(x0, y0 + 1, mSeed), cellNoise(x0 + 1, y0 + 1, mSeed), tx),
                                     ty);
                const Pixel *s = in.data() + ((size_t)y * w + x) * ch;
                Pixel *d = out.data() + ((size_t)y * w + x) * ch;
                Pixel L = colorCh >= 3 ? color::luminance(s[0], s[1], s[2]) : s[0];
                const Pixel midW = (Pixel)4 * L * ((Pixel)1 - L);  // peaks at mid grey
                const Pixel delta = n * strength * (midW < 0 ? 0 : midW);
                for (int c = 0; c < colorCh; ++c) d[c] = s[c] + delta;
                for (int c = colorCh; c < ch; ++c) d[c] = s[c];
            }
        }
    }
}
