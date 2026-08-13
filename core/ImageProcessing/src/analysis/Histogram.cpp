#include "Histogram.h"
#include "../base/ColorSpace.h"
#include "../base/Parallel.h"
#include <algorithm>
#include <cmath>
#include <mutex>
#include <utility>
#include <vector>

namespace arstro
{
    static inline int binOf(Pixel encoded)
    {
        int b = (int)(encoded * (Pixel)(HistogramData::kBins - 1) + (Pixel)0.5);
        if (b < 0) b = 0;
        if (b > HistogramData::kBins - 1) b = HistogramData::kBins - 1;
        return b;
    }

    HistogramData Histogram::compute(const Image &image)
    {
        HistogramData h;
        const int ch = image.channels();
        if (ch < 1 || image.empty())
            return h;
        const bool linear = image.space() == ColorSpace::LinearSRGB;
        const size_t px = image.pixelCount();
        const Pixel *d = image.data();
        const int colorCh = ch >= 3 ? 3 : 1;

        // Per-pixel work does up to 4 srgbEncode (pow) calls, so it dominates a
        // preview render — parallelise it: each chunk accumulates a local histogram
        // (bin adds are associative -> identical result) then merges once.
        std::mutex mtx;
        par::parallelFor((int)px, [&](int i0, int i1) {
            HistogramData loc;
            for (int i = i0; i < i1; ++i)
            {
                const Pixel *p = d + (size_t)i * ch;
                Pixel rv = p[0];
                Pixel gv = colorCh >= 3 ? p[1] : p[0];
                Pixel bv = colorCh >= 3 ? p[2] : p[0];
                Pixel lin_r = linear ? rv : color::srgbDecode(rv);
                Pixel lin_g = linear ? gv : color::srgbDecode(gv);
                Pixel lin_b = linear ? bv : color::srgbDecode(bv);
                Pixel lumLin = color::luminance(lin_r, lin_g, lin_b);
                if (linear)
                {
                    rv = color::srgbEncode(rv);
                    gv = color::srgbEncode(gv);
                    bv = color::srgbEncode(bv);
                }
                Pixel lumEnc = color::srgbEncode(lumLin);
                ++loc.r[binOf(rv)];
                ++loc.g[binOf(gv)];
                ++loc.b[binOf(bv)];
                ++loc.lum[binOf(lumEnc)];
            }
            std::lock_guard<std::mutex> lk(mtx);
            for (int b = 0; b < HistogramData::kBins; ++b)
            {
                h.r[b] += loc.r[b]; h.g[b] += loc.g[b]; h.b[b] += loc.b[b]; h.lum[b] += loc.lum[b];
            }
        });

        for (int i = 0; i < HistogramData::kBins; ++i)
        {
            if (h.r[i] > h.maxCount) h.maxCount = h.r[i];
            if (h.g[i] > h.maxCount) h.maxCount = h.g[i];
            if (h.b[i] > h.maxCount) h.maxCount = h.b[i];
            if (h.lum[i] > h.maxCount) h.maxCount = h.lum[i];
        }
        return h;
    }

    HueHistogram Histogram::computeHue(const Image &image)
    {
        HueHistogram hh;
        const int ch = image.channels();
        if (ch < 3 || image.empty()) return hh;
        const size_t px = image.pixelCount();
        const Pixel *d = image.data();

        // Merge order matters here (unlike compute() above): bins are float sums,
        // and float addition is not associative, so folding chunks in whatever order
        // their threads happen to finish would make the result depend on scheduling
        // — the same render could produce slightly different bins from one call to
        // the next. Each chunk's partial result is tagged with its start offset and
        // reduced into `hh` in a fixed (start-offset) order after every thread has
        // joined, so the result is reproducible regardless of thread timing.
        std::mutex mtx;
        std::vector<std::pair<int, HueHistogram>> parts;
        par::parallelFor((int)px, [&](int i0, int i1) {
            HueHistogram loc;
            for (int i = i0; i < i1; ++i)
            {
                const Pixel *p = d + (size_t)i * ch;
                Pixel h, s, l;
                color::rgbToHsl(p[0], p[1], p[2], h, s, l);  // linear-space hue, as the mixer keys on
                if (s <= (Pixel)0) continue;                  // skip greys
                int bin = (int)(h / (Pixel)360 * HueHistogram::kBins);
                if (bin < 0) bin = 0; if (bin >= HueHistogram::kBins) bin = HueHistogram::kBins - 1;
                loc.bins[bin] += (float)s;                    // weight by saturation
            }
            std::lock_guard<std::mutex> lk(mtx);
            parts.emplace_back(i0, loc);
        });
        std::sort(parts.begin(), parts.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
        for (const auto &part : parts)
            for (int b = 0; b < HueHistogram::kBins; ++b) hh.bins[b] += part.second.bins[b];

        float mx = 1e-6f;
        for (float v : hh.bins) if (v > mx) mx = v;
        for (float &v : hh.bins) v /= mx;                 // normalise peak to 1
        return hh;
    }

    void Histogram::toLinear(const HistogramData &in, float out[4][HistogramData::kBins])
    {
        const float denom = in.maxCount > 0 ? (float)in.maxCount : 1.0f;
        for (int i = 0; i < HistogramData::kBins; ++i)
        {
            out[0][i] = (float)in.r[i] / denom;
            out[1][i] = (float)in.g[i] / denom;
            out[2][i] = (float)in.b[i] / denom;
            out[3][i] = (float)in.lum[i] / denom;
        }
    }

    void Histogram::toLog(const HistogramData &in, float out[4][HistogramData::kBins])
    {
        const float denom = std::log1p((float)in.maxCount);
        const float inv = denom > 0 ? 1.0f / denom : 1.0f;
        const std::array<uint32_t, HistogramData::kBins> *src[4] = {&in.r, &in.g, &in.b, &in.lum};
        for (int row = 0; row < 4; ++row)
            for (int i = 0; i < HistogramData::kBins; ++i)
                out[row][i] = std::log1p((float)(*src[row])[i]) * inv;
    }
}
