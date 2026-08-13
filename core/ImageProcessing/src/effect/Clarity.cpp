#include "Clarity.h"
#include "../base/ColorSpace.h"
#include "../base/Spatial.h"
#include "../base/Parallel.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace arstro
{
    Clarity::Clarity() : ImageProcessor(propertyCount)
    {
        mSmoothEnable = false;
        initProperty(amountID, 0);
    }

    void Clarity::process(const Image &in, Image &out)
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

        // Large radius scaled to the image, so the look is consistent across sizes.
        const float sigma = std::min(40.f, std::max(8.f, std::min(w, h) * 0.02f));
        std::vector<Pixel> L, P(px), Pb;
        spatial::luminancePlane(in, L);
        for (size_t i = 0; i < px; ++i) P[i] = color::srgbEncode(L[i]);
        spatial::gaussianBlurPlane(P, Pb, w, h, sigma);
        const float k = amount * 0.6f;

        par::parallelFor(h, [&](int y0, int y1) {
            for (int y = y0; y < y1; ++y)
                for (int x = 0; x < w; ++x)
                {
                    const size_t i = (size_t)y * w + x;
                    float mid = 1.f - std::fabs(2.f * (float)P[i] - 1.f);  // peaks at midtone
                    float newP = (float)P[i] + k * (float)(P[i] - Pb[i]) * mid;
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
