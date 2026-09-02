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

        namespace
        {
            /** One box pass of radius r, horizontally then vertically, by running sum — so a
             *  pass costs two adds per pixel regardless of r. Edges CLAMP (the sum is seeded
             *  with r+1 copies of the first sample and each step swaps the leaving sample for
             *  the entering one, both clamped), which matches gaussianBlurPlane's edge rule so
             *  the two are interchangeable at a boundary. */
            void boxPass(const std::vector<Pixel> &src, std::vector<Pixel> &dst, int w, int h, int r)
            {
                const float inv = 1.f / (float)(2 * r + 1);
                std::vector<Pixel> tmp((std::size_t)w * h);
                par::parallelFor(h, [&](int y0, int y1) {
                    for (int y = y0; y < y1; ++y)
                    {
                        const Pixel *in = src.data() + (std::size_t)y * w;
                        Pixel *out = tmp.data() + (std::size_t)y * w;
                        double acc = (double)in[0] * (double)(r + 1);
                        for (int i = 1; i <= r; ++i) acc += (double)in[i < w ? i : w - 1];
                        for (int x = 0; x < w; ++x)
                        {
                            out[x] = (Pixel)(acc * inv);
                            const int add = x + r + 1, sub = x - r;
                            acc += (double)in[add < w ? add : w - 1];
                            acc -= (double)in[sub > 0 ? sub : 0];
                        }
                    }
                });
                dst.resize((std::size_t)w * h);
                // Column-parallel over x, so each chunk still owns the cells it writes.
                par::parallelFor(w, [&](int x0, int x1) {
                    for (int x = x0; x < x1; ++x)
                    {
                        double acc = (double)tmp[x] * (double)(r + 1);
                        for (int i = 1; i <= r; ++i) acc += (double)tmp[(std::size_t)(i < h ? i : h - 1) * w + x];
                        for (int y = 0; y < h; ++y)
                        {
                            dst[(std::size_t)y * w + x] = (Pixel)(acc * inv);
                            const int add = y + r + 1, sub = y - r;
                            acc += (double)tmp[(std::size_t)(add < h ? add : h - 1) * w + x];
                            acc -= (double)tmp[(std::size_t)(sub > 0 ? sub : 0) * w + x];
                        }
                    }
                });
            }
        }

        void fastBlurPlane(const std::vector<Pixel> &src, std::vector<Pixel> &dst,
                           int w, int h, float sigma)
        {
            if (sigma <= 0.f || w <= 0 || h <= 0)
            {
                if (&dst != &src) dst = src;
                return;
            }
            // Below a few pixels the box cascade is not an approximation worth having: with three
            // integer box widths the reachable variances are coarse, and sigma 1 comes out at 0.82.
            // It is also exactly where the exact kernel is CHEAP — its cost is O(pixels x sigma) —
            // so the two methods are strong in opposite regimes and the cheap one is the accurate
            // one here. Above this the cascade takes over and the cost stops growing.
            if (sigma < 4.f) { gaussianBlurPlane(src, dst, w, h, sigma); return; }

            // Kovesi's box sizes for three passes: the ideal width for a single box that would
            // give this sigma, split into two integer widths so their variances sum to the
            // requested one. Solving for the count at the smaller width is what keeps the
            // approximation honest at small sigma, where rounding a single width would nearly
            // double the radius.
            const float ideal = std::sqrt(12.f * sigma * sigma / 3.f + 1.f);
            int wl = (int)std::floor(ideal); if (wl % 2 == 0) --wl; if (wl < 1) wl = 1;
            const int wu = wl + 2;
            const float mIdeal = (12.f * sigma * sigma - 3.f * (float)(wl * wl)
                                  - 12.f * (float)wl - 9.f) / (-4.f * (float)wl - 4.f);
            int m = (int)(mIdeal + 0.5f); if (m < 0) m = 0; if (m > 3) m = 3;

            dst = src;   // safe when &dst == &src; boxPass never reads its own output
            for (int pass = 0; pass < 3; ++pass)
            {
                const int r = ((pass < m ? wl : wu) - 1) / 2;
                if (r >= 1) boxPass(dst, dst, w, h, r);
            }
        }
    }
}
