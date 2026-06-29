#include "NoiseReduction.h"
#include "../base/ColorSpace.h"
#include "../base/Spatial.h"
#include "../base/Parallel.h"
#include <cmath>
#include <vector>

namespace arstro
{
    NoiseReduction::NoiseReduction() : ImageProcessor(propertyCount)
    {
        mSmoothEnable = false;
        initProperty(luminanceID, 0);
        initProperty(colorID, 0);
    }

    void NoiseReduction::process(const Image &in, Image &out)
    {
        out.resizeLike(in);
        const int w = in.width(), h = in.height(), ch = in.channels();
        const size_t px = in.pixelCount();
        const int colorCh = ch >= 3 ? 3 : ch;
        const float lumAmt = getProperty(luminanceID) / 100.f;
        const float colorAmt = getProperty(colorID) / 100.f;
        const Pixel *s = in.data();
        Pixel *d = out.data();

        if (lumAmt <= 0.f && colorAmt <= 0.f)
        {
            for (size_t i = 0; i < px * (size_t)ch; ++i) d[i] = s[i];
            return;
        }

        // Start from the source; alpha is copied straight through.
        for (size_t i = 0; i < px; ++i)
        {
            const Pixel *p = s + i * ch;
            Pixel *q = d + i * ch;
            for (int c = 0; c < ch; ++c) q[c] = p[c];
        }

        std::vector<Pixel> L;
        spatial::luminancePlane(in, L);

        // ── Colour NR: blur the chroma (channel - luma), keep luma sharp ──
        if (colorAmt > 0.f && colorCh >= 3)
        {
            const float sigma = 1.0f + colorAmt * 4.0f;
            std::vector<Pixel> chroma(px), blurred;
            for (int c = 0; c < colorCh; ++c)
            {
                for (size_t i = 0; i < px; ++i) chroma[i] = s[i * ch + c] - L[i];
                spatial::gaussianBlurPlane(chroma, blurred, w, h, sigma);
                for (size_t i = 0; i < px; ++i)
                {
                    Pixel nc = chroma[i] + (blurred[i] - chroma[i]) * colorAmt;
                    d[i * ch + c] = L[i] + nc;
                }
            }
        }

        // ── Luminance NR: edge-preserving bilateral on perceptual luma ──
        if (lumAmt > 0.f)
        {
            // perceptual luma of the (colour-denoised) working image
            std::vector<Pixel> L2(px), P(px);
            for (size_t i = 0; i < px; ++i)
            {
                const Pixel *q = d + i * ch;
                Pixel r = q[0], g = colorCh >= 3 ? q[1] : r, b = colorCh >= 3 ? q[2] : r;
                L2[i] = color::luminance(r, g, b);
                P[i] = color::srgbEncode(L2[i]);
            }
            const float sigmaS = 0.8f + lumAmt * 2.2f;
            int rad = (int)std::ceil(sigmaS * 2.f); if (rad < 1) rad = 1; if (rad > 4) rad = 4;
            const float invS2 = 1.f / (2.f * sigmaS * sigmaS);
            const float sigmaR = 0.02f + lumAmt * 0.13f;
            const float invR2 = 1.f / (2.f * sigmaR * sigmaR);

            par::parallelFor(h, [&](int y0, int y1) {
                for (int y = y0; y < y1; ++y)
                    for (int x = 0; x < w; ++x)
                    {
                        const size_t i = (size_t)y * w + x;
                        const float center = (float)P[i];
                        double acc = 0.0, wsum = 0.0;
                        for (int dy = -rad; dy <= rad; ++dy)
                        {
                            int yy = y + dy; if (yy < 0) yy = 0; else if (yy >= h) yy = h - 1;
                            for (int dx = -rad; dx <= rad; ++dx)
                            {
                                int xx = x + dx; if (xx < 0) xx = 0; else if (xx >= w) xx = w - 1;
                                float pv = (float)P[(size_t)yy * w + xx];
                                float dr = pv - center;
                                float ws = std::exp(-(float)(dx * dx + dy * dy) * invS2 - dr * dr * invR2);
                                acc += pv * ws; wsum += ws;
                            }
                        }
                        float newP = wsum > 0.0 ? (float)(acc / wsum) : center;
                        Pixel newL = color::srgbDecode(newP < 0.f ? 0.f : (newP > 1.f ? 1.f : newP));
                        Pixel ratio = newL / (L2[i] > (Pixel)1e-4 ? L2[i] : (Pixel)1e-4);
                        Pixel *q = d + i * ch;
                        for (int c = 0; c < colorCh; ++c) q[c] = q[c] * ratio;
                    }
            });
        }
    }
}
