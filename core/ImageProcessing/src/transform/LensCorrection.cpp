#include "LensCorrection.h"
#include "../base/Parallel.h"
#include <cmath>

namespace arstro
{
    LensCorrection::LensCorrection() : ImageProcessor(propertyCount)
    {
        mSmoothEnable = false;
        initProperty(distortionID, 0);
        initProperty(caID, 0);
        initProperty(vignetteID, 0);
    }

    void LensCorrection::process(const Image &in, Image &out)
    {
        out.resizeLike(in);
        const int w = in.width(), h = in.height(), ch = in.channels();
        const int colorCh = ch >= 3 ? 3 : ch;
        const float dist = getProperty(distortionID) / 100.f;
        const float ca = getProperty(caID) / 100.f;
        const float vig = getProperty(vignetteID) / 100.f;
        Pixel *d = out.data();

        if (dist == 0.f && ca == 0.f && vig == 0.f)
        {
            const Pixel *s = in.data();
            for (size_t i = 0; i < in.pixelCount() * (size_t)ch; ++i) d[i] = s[i];
            return;
        }

        const float cx = (w - 1) * 0.5f, cy = (h - 1) * 0.5f;
        const float maxR = std::sqrt(cx * cx + cy * cy);
        const float invMaxR = maxR > 0.f ? 1.f / maxR : 0.f;
        const float kDist = dist * 0.4f;     // edge magnification at the corner
        const float kCA = ca * 0.03f;        // up to 3% opposing R/B radial scale

        par::parallelFor(h, [&](int y0, int y1) {
            for (int y = y0; y < y1; ++y)
                for (int x = 0; x < w; ++x)
                {
                    const float dx = x - cx, dy = y - cy;
                    const float r = std::sqrt(dx * dx + dy * dy) * invMaxR;  // 0..1
                    const float f = 1.f + kDist * r * r;
                    const float fr = f * (1.f - kCA * r);
                    const float fb = f * (1.f + kCA * r);
                    Pixel *q = d + ((size_t)y * w + x) * ch;
                    if (colorCh >= 3)
                    {
                        q[0] = in.sampleBilinear(cx + dx * fr, cy + dy * fr, 0);
                        q[1] = in.sampleBilinear(cx + dx * f, cy + dy * f, 1);
                        q[2] = in.sampleBilinear(cx + dx * fb, cy + dy * fb, 2);
                    }
                    else
                    {
                        q[0] = in.sampleBilinear(cx + dx * f, cy + dy * f, 0);
                    }
                    for (int c = colorCh; c < ch; ++c)
                        q[c] = in.sampleBilinear(cx + dx * f, cy + dy * f, c);
                    if (vig != 0.f)
                    {
                        const float mult = 1.f + vig * r * r;
                        for (int c = 0; c < colorCh; ++c) q[c] *= mult;
                    }
                }
        });
    }
}
