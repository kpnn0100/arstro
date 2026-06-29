#include "Sharpen.h"
#include "../base/ColorSpace.h"
#include "../base/Spatial.h"
#include "../base/Parallel.h"
#include <cmath>
#include <vector>

namespace arstro
{
    Sharpen::Sharpen() : ImageProcessor(propertyCount)
    {
        mSmoothEnable = false;
        initProperty(amountID, 0);
        initProperty(radiusID, 1);
        initProperty(maskingID, 0);
    }

    static inline float smoothstep01(float lo, float hi, float x)
    {
        if (hi <= lo) return x >= hi ? 1.f : 0.f;
        float t = (x - lo) / (hi - lo);
        if (t < 0.f) t = 0.f; else if (t > 1.f) t = 1.f;
        return t * t * (3.f - 2.f * t);
    }

    void Sharpen::process(const Image &in, Image &out)
    {
        out.resizeLike(in);
        const int w = in.width(), h = in.height(), ch = in.channels();
        const size_t px = in.pixelCount();
        const float amount = getProperty(amountID) / 100.f;
        const float sigma = getProperty(radiusID);
        const float masking = getProperty(maskingID) / 100.f;
        const Pixel *s = in.data();
        Pixel *d = out.data();

        if (amount == 0.f)
        {
            for (size_t i = 0; i < px * (size_t)ch; ++i) d[i] = s[i];
            return;
        }

        // Perceptual luminance plane P, its blur, and the high-pass.
        std::vector<Pixel> L, P(px), Pb;
        spatial::luminancePlane(in, L);
        for (size_t i = 0; i < px; ++i) P[i] = color::srgbEncode(L[i]);
        spatial::gaussianBlurPlane(P, Pb, w, h, sigma);

        std::vector<Pixel> detail(px), detailBlur;
        for (size_t i = 0; i < px; ++i) detail[i] = std::fabs((float)(P[i] - Pb[i]));
        spatial::gaussianBlurPlane(detail, detailBlur, w, h, sigma);
        const float thr = masking * 0.12f;  // edge strength gate grows with masking

        par::parallelFor(h, [&](int y0, int y1) {
            for (int y = y0; y < y1; ++y)
                for (int x = 0; x < w; ++x)
                {
                    const size_t i = (size_t)y * w + x;
                    float mask = 1.f;
                    if (masking > 0.f)
                        mask = (1.f - masking) + masking * smoothstep01(0.f, thr + 1e-4f, (float)detailBlur[i]);
                    float hp = (float)(P[i] - Pb[i]);
                    float newP = (float)P[i] + amount * hp * mask;
                    if (newP < 0.f) newP = 0.f; else if (newP > 1.f) newP = 1.f;
                    Pixel newL = color::srgbDecode(newP);
                    Pixel ratio = newL / (L[i] > (Pixel)1e-4 ? L[i] : (Pixel)1e-4);
                    const Pixel *p = s + i * ch;
                    Pixel *q = d + i * ch;
                    const int colorCh = ch >= 3 ? 3 : ch;
                    for (int c = 0; c < colorCh; ++c) q[c] = p[c] * ratio;
                    for (int c = colorCh; c < ch; ++c) q[c] = p[c];
                }
        });
    }
}
