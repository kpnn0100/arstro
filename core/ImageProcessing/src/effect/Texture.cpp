#include "Texture.h"
#include "../base/ColorSpace.h"
#include "../base/Spatial.h"
#include "../base/Parallel.h"
#include <vector>

namespace arstro
{
    Texture::Texture() : ImageProcessor(propertyCount)
    {
        mSmoothEnable = false;
        initProperty(amountID, 0);
    }

    void Texture::process(const Image &in, Image &out)
    {
        out.resizeLike(in);
        const int w = in.width(), h = in.height(), ch = in.channels();
        const size_t px = in.pixelCount();
        const int colorCh = ch >= 3 ? 3 : ch;
        const float amount = getProperty(amountID) / 100.f;
        const Pixel *s = in.data();
        Pixel *d = out.data();

        if (amount == 0.f)
        {
            for (size_t i = 0; i < px * (size_t)ch; ++i) d[i] = s[i];
            return;
        }

        std::vector<Pixel> L, P(px), Pb;
        spatial::luminancePlane(in, L);
        for (size_t i = 0; i < px; ++i) P[i] = color::srgbEncode(L[i]);
        spatial::gaussianBlurPlane(P, Pb, w, h, 2.0f);  // small radius = fine detail
        const float k = amount * 0.85f;

        par::parallelFor(h, [&](int y0, int y1) {
            for (int y = y0; y < y1; ++y)
                for (int x = 0; x < w; ++x)
                {
                    const size_t i = (size_t)y * w + x;
                    float newP = (float)P[i] + k * (float)(P[i] - Pb[i]);
                    if (newP < 0.f) newP = 0.f; else if (newP > 1.f) newP = 1.f;
                    Pixel ratio = color::srgbDecode(newP) / (L[i] > (Pixel)1e-4 ? L[i] : (Pixel)1e-4);
                    const Pixel *p = s + i * ch;
                    Pixel *q = d + i * ch;
                    for (int c = 0; c < colorCh; ++c) q[c] = p[c] * ratio;
                    for (int c = colorCh; c < ch; ++c) q[c] = p[c];
                }
        });
    }
}
