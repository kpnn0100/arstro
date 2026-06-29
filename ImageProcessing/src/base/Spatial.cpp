#include "Spatial.h"
#include "ColorSpace.h"
#include "Parallel.h"
#include <cmath>

namespace arstro
{
    namespace spatial
    {
        void luminancePlane(const Image &img, std::vector<Pixel> &out)
        {
            const int w = img.width(), h = img.height(), ch = img.channels();
            out.resize((size_t)w * h);
            const Pixel *s = img.data();
            par::parallelFor(h, [&](int y0, int y1) {
                for (int y = y0; y < y1; ++y)
                    for (int x = 0; x < w; ++x)
                    {
                        const Pixel *p = s + ((size_t)y * w + x) * ch;
                        Pixel r = ch >= 1 ? p[0] : 0;
                        Pixel g = ch >= 3 ? p[1] : r;
                        Pixel b = ch >= 3 ? p[2] : r;
                        out[(size_t)y * w + x] = color::luminance(r, g, b);
                    }
            });
        }

        void gaussianBlurPlane(const std::vector<Pixel> &src, std::vector<Pixel> &dst,
                               int w, int h, float sigma)
        {
            const size_t n = (size_t)w * h;
            if (sigma <= 0.f || w <= 0 || h <= 0)
            {
                if (&dst != &src) dst = src;
                return;
            }
            const int r = std::max(1, (int)std::ceil(sigma * 3.f));
            std::vector<float> k(2 * r + 1);
            float sum = 0.f;
            const float inv2s2 = 1.f / (2.f * sigma * sigma);
            for (int i = -r; i <= r; ++i) { float v = std::exp(-(float)(i * i) * inv2s2); k[i + r] = v; sum += v; }
            for (auto &v : k) v /= sum;

            // Copy source first so dst==src is safe; tmp holds the horizontal pass.
            std::vector<Pixel> in = src;
            std::vector<Pixel> tmp(n);
            par::parallelFor(h, [&](int y0, int y1) {
                for (int y = y0; y < y1; ++y)
                    for (int x = 0; x < w; ++x)
                    {
                        double acc = 0.0;
                        for (int i = -r; i <= r; ++i)
                        {
                            int xx = x + i; if (xx < 0) xx = 0; else if (xx >= w) xx = w - 1;
                            acc += (double)in[(size_t)y * w + xx] * k[i + r];
                        }
                        tmp[(size_t)y * w + x] = (Pixel)acc;
                    }
            });
            dst.resize(n);
            par::parallelFor(h, [&](int y0, int y1) {
                for (int y = y0; y < y1; ++y)
                    for (int x = 0; x < w; ++x)
                    {
                        double acc = 0.0;
                        for (int i = -r; i <= r; ++i)
                        {
                            int yy = y + i; if (yy < 0) yy = 0; else if (yy >= h) yy = h - 1;
                            acc += (double)tmp[(size_t)yy * w + x] * k[i + r];
                        }
                        dst[(size_t)y * w + x] = (Pixel)acc;
                    }
            });
        }
    }
}
