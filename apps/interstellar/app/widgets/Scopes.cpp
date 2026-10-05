#include "Scopes.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace arstro
{
namespace interstellar_v1
{
    namespace
    {
        /** density → 0..255, log-scaled so one stray pixel still shows next to a million */
        uint8_t level(uint32_t n, double logMax)
        {
            if (n == 0 || logMax <= 0) return 0;
            const double v = std::log1p((double)n) / logMax;
            return (uint8_t)std::clamp((int)std::lround(40.0 + 215.0 * std::pow(v, 0.7)), 0, 255);
        }
        void paint(interstellar::Raster &out, int w, int h, const std::vector<uint32_t> &cnt, uint8_t r, uint8_t g, uint8_t b, int x0 = 0, int outW = -1)
        {
            uint32_t mx = 0;
            for (uint32_t c : cnt) mx = std::max(mx, c);
            const double lm = std::log1p((double)mx);
            if (outW < 0) outW = w;
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x)
                {
                    const uint8_t a = level(cnt[(size_t)y * w + x], lm);
                    uint8_t *p = &out.rgba[((size_t)y * outW + x0 + x) * 4];
                    p[0] = (uint8_t)(r * a / 255);
                    p[1] = (uint8_t)(g * a / 255);
                    p[2] = (uint8_t)(b * a / 255);
                    p[3] = a;
                }
        }
    }

    ScopeData scopesOf(const interstellar::Raster &f)
    {
        ScopeData d;
        if (f.empty()) return d;
        d.valid = true;
        d.width = f.width;
        d.height = f.height;
        const int W = ScopeData::kScopeW, H = ScopeData::kScopeH, PW = ScopeData::kParadeW, N = ScopeData::kVectorN;
        std::vector<uint32_t> wave((size_t)W * H, 0), par[3], vec((size_t)N * N, 0), rgb[3];
        for (auto &c : rgb) c.assign((size_t)W * H, 0);
        for (auto &p : par) p.assign((size_t)PW * H, 0);
        bool used[3][256] = {};
        uint64_t hi[3] = {0, 0, 0}, lo[3] = {0, 0, 0};
        const size_t n = (size_t)f.width * f.height;
        const size_t step = std::max<size_t>(1, n / 120000);   // the scopes sample; the counts below see every pixel
        for (size_t i = 0; i < n; ++i)
        {
            const uint8_t *p = &f.rgba[i * 4];
            for (int c = 0; c < 3; ++c)
            {
                used[c][p[c]] = true;
                hi[c] += p[c] == 255;
                lo[c] += p[c] == 0;
            }
            if (i % step) continue;
            const int x = (int)(i % (size_t)f.width);
            const double y709 = 0.2126 * p[0] + 0.7152 * p[1] + 0.0722 * p[2];
            const int lum = std::clamp((int)std::lround(y709), 0, 255);
            d.hist.r[p[0]]++; d.hist.g[p[1]]++; d.hist.b[p[2]]++; d.hist.lum[lum]++;
            const int col = x * W / f.width, row = (255 - lum) * (H - 1) / 255;
            wave[(size_t)row * W + col]++;
            const int pcol = x * PW / f.width;
            for (int c = 0; c < 3; ++c) par[c][(size_t)((255 - p[c]) * (H - 1) / 255) * PW + pcol]++;
            for (int c = 0; c < 3; ++c) rgb[c][(size_t)((255 - p[c]) * (H - 1) / 255) * W + col]++;
            // chroma: Cb right, Cr up — red sits upper-left, as on any vectorscope
            const double cb = (p[2] - y709) / (1.8556 * 255.0), cr = (p[0] - y709) / (1.5748 * 255.0);
            const int vx = std::clamp((int)std::lround((cb + 0.5) * (N - 1)), 0, N - 1);
            const int vy = std::clamp((int)std::lround((0.5 - cr) * (N - 1)), 0, N - 1);
            vec[(size_t)vy * N + vx]++;
        }
        for (int b = 0; b < HistogramData::kBins; ++b)
            d.hist.maxCount = std::max({d.hist.maxCount, d.hist.r[b], d.hist.g[b], d.hist.b[b], d.hist.lum[b]});
        for (int c = 0; c < 3; ++c)
        {
            d.clipHi[c] = 100.0 * hi[c] / n;
            d.clipLo[c] = 100.0 * lo[c] / n;
            for (int v = 0; v < 256; ++v) d.levels[c] += used[c][v];
        }
        d.waveform.allocate(W, H);
        paint(d.waveform, W, H, wave, 225, 236, 228);
        {
            // additive: each channel's density lights its own primary, so agreement reads white
            uint32_t mx = 0;
            for (const auto &c : rgb) for (uint32_t v : c) mx = std::max(mx, v);
            const double lm = std::log1p((double)mx);
            d.waveformRgb.allocate(W, H);
            for (size_t i = 0; i < (size_t)W * H; ++i)
            {
                uint8_t *o = &d.waveformRgb.rgba[i * 4];
                for (int c = 0; c < 3; ++c) o[c] = level(rgb[c][i], lm);
                o[3] = std::max(o[0], std::max(o[1], o[2]));
            }
        }
        d.parade.allocate(3 * PW, H);
        paint(d.parade, PW, H, par[0], 236, 92, 92, 0, 3 * PW);
        paint(d.parade, PW, H, par[1], 104, 214, 112, PW, 3 * PW);
        paint(d.parade, PW, H, par[2], 110, 152, 246, 2 * PW, 3 * PW);
        d.vector.allocate(N, N);
        paint(d.vector, N, N, vec, 232, 232, 232);
        return d;
    }

    interstellar::Raster clipMaskOf(const interstellar::Raster &f)
    {
        interstellar::Raster m;
        if (f.empty()) return m;
        m.allocate(f.width, f.height, 0);
        const size_t n = (size_t)f.width * f.height;
        for (size_t i = 0; i < n; ++i)
        {
            const uint8_t *p = &f.rgba[i * 4];
            uint8_t *o = &m.rgba[i * 4];
            if (p[0] == 255 || p[1] == 255 || p[2] == 255) { o[0] = 255; o[1] = 40; o[2] = 40; o[3] = 220; }
            else if (p[0] == 0 || p[1] == 0 || p[2] == 0) { o[0] = 40; o[1] = 90; o[2] = 255; o[3] = 220; }
        }
        return m;
    }
}
}
