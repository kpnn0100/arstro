#include "Grain.h"
#include "../base/ColorSpace.h"
#include <cmath>

namespace arstro
{
    namespace
    {
        // Deterministic [-1,1] hash for an integer grid coordinate.
        Pixel cellNoise(int ix, int iy, unsigned seed)
        {
            unsigned h = (unsigned)ix * 374761393u + (unsigned)iy * 668265263u + seed * 362437u;
            h = (h ^ (h >> 13)) * 1274126177u;
            return ((Pixel)(h & 0xffffu) / (Pixel)65535) * (Pixel)2 - (Pixel)1;
        }
        // Perlin quintic fade — smooth (C2) interpolation, no grid/diamond artifacts.
        inline Pixel fade(Pixel t) { return t * t * t * (t * (t * (Pixel)6 - (Pixel)15) + (Pixel)10); }
        // Smooth value noise at a fractional grid coordinate.
        Pixel valueNoise(Pixel fx, Pixel fy, unsigned seed)
        {
            const int x0 = (int)std::floor(fx), y0 = (int)std::floor(fy);
            const Pixel tx = fade(fx - (Pixel)x0), ty = fade(fy - (Pixel)y0);
            const Pixel n00 = cellNoise(x0, y0, seed), n10 = cellNoise(x0 + 1, y0, seed);
            const Pixel n01 = cellNoise(x0, y0 + 1, seed), n11 = cellNoise(x0 + 1, y0 + 1, seed);
            return lerp(lerp(n00, n10, tx), lerp(n01, n11, tx), ty);
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
        // Grain feature size: larger `size` -> coarser grains, but kept SMOOTH (quintic
        // fade) and TEXTURED (a second, finer octave) so it never looks like a zoomed,
        // blocky upscale of a coarse grid.
        const Pixel cell = (Pixel)1 + getProperty(sizeID) / (Pixel)100 * (Pixel)3;  // 1..4 px
        const Pixel strength = amount * (Pixel)0.15;
        const int w = in.width(), h = in.height();
        for (int y = 0; y < h; ++y)
        {
            const Pixel fy = (Pixel)y / cell;
            for (int x = 0; x < w; ++x)
            {
                const Pixel fx = (Pixel)x / cell;
                // two octaves: base grain + finer detail (decorrelated phase/seed)
                const Pixel n = valueNoise(fx, fy, mSeed) * (Pixel)0.65 +
                                valueNoise(fx * (Pixel)2.3 + (Pixel)11.3, fy * (Pixel)2.3 + (Pixel)7.7,
                                           mSeed * 9176u + 1u) * (Pixel)0.35;
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
