/*
 *  Arstro ImageProcessing Library — ImageStats. See the header for what each number means
 *  and why this file measures but never judges.
 */
#include "ImageStats.h"
#include "../base/ColorSpace.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace arstro
{
    namespace
    {
        /** Exact decode of the 256 possible encoded values. A table because it is called per
         *  channel per pixel, and exact because 256 entries cost nothing to build right. */
        const std::array<float, 256> &decodeTable()
        {
            static const std::array<float, 256> t = [] {
                std::array<float, 256> a{};
                for (int i = 0; i < 256; ++i) a[i] = (float)color::srgbDecodeExact((Pixel)i / (Pixel)255);
                return a;
            }();
            return t;
        }

        /** Hue in degrees [0,360) and HSV saturation/value of an encoded triple in 0..1. */
        void hsv(float r, float g, float b, float &h, float &s, float &v)
        {
            const float mx = std::max(r, std::max(g, b)), mn = std::min(r, std::min(g, b));
            const float d = mx - mn;
            v = mx;
            s = mx > 0.f ? d / mx : 0.f;
            if (d <= 1e-6f) { h = 0.f; return; }
            if (mx == r) h = 60.f * std::fmod((g - b) / d, 6.f);
            else if (mx == g) h = 60.f * ((b - r) / d + 2.f);
            else h = 60.f * ((r - g) / d + 4.f);
            if (h < 0.f) h += 360.f;
        }

        /** log2 ratios of a linear mean, guarded so an empty or black set reads as neutral. */
        void cast(double r, double g, double b, float &warmth, float &tint)
        {
            const double e = 1e-6;
            if (r <= e || g <= e || b <= e) { warmth = 0.f; tint = 0.f; return; }
            warmth = (float)std::log2(r / b);
            tint = (float)std::log2(g / std::sqrt(r * b));
        }

        struct Acc
        {
            double r = 0, g = 0, b = 0, sat = 0;
            long n = 0;
            void add(float lr, float lg, float lb, float s) { r += lr; g += lg; b += lb; sat += s; ++n; }
        };
    }

    ImageStats measureImage(const uint8_t *rgba, int width, int height)
    {
        ImageStats st;
        st.width = width;
        st.height = height;
        if (!rgba || width <= 0 || height <= 0) return st;

        const std::array<float, 256> &lin = decodeTable();
        const long total = (long)width * (long)height;
        const int kLumBins = 1024;
        std::vector<long> lumHist(kLumBins, 0);

        double lumSum = 0, lumSq = 0, logSum = 0, satSum = 0;
        double rgSum = 0, rgSq = 0, ybSum = 0, ybSq = 0;
        long clipLo = 0, clipHi = 0;
        long skin = 0, sky = 0, foliage = 0, neon = 0, coloured = 0;
        double hueW[12] = {0};
        Acc world, neutral, zone[3];
        const int skyRows = (int)(height * 0.4);

        for (int y = 0; y < height; ++y)
        {
            const uint8_t *p = rgba + (size_t)y * (size_t)width * 4;
            for (int x = 0; x < width; ++x, p += 4)
            {
                const int R = p[0], G = p[1], B = p[2];
                const float lr = lin[R], lg = lin[G], lb = lin[B];
                const float Y = 0.2126f * lr + 0.7152f * lg + 0.0722f * lb;
                const float L = (float)color::srgbEncode((Pixel)Y);   // the table: 2e-5, far below a bin

                lumHist[std::min(kLumBins - 1, std::max(0, (int)(L * kLumBins)))]++;
                lumSum += L;
                lumSq += (double)L * L;
                logSum += std::log(std::max(Y, 1e-4f));

                const bool lo = R <= 2 && G <= 2 && B <= 2;
                const bool hi = R >= 254 || G >= 254 || B >= 254;
                clipLo += lo;
                clipHi += hi;

                float h, s, v;
                hsv(R / 255.f, G / 255.f, B / 255.f, h, s, v);
                satSum += s;

                const double rg = R - G, yb = 0.5 * (R + G) - B;
                rgSum += rg; rgSq += rg * rg; ybSum += yb; ybSq += yb * yb;

                const float chroma = s * v;
                if (chroma > 0.08f)
                {
                    ++coloured;
                    hueW[std::min(11, (int)(h / 30.f))] += chroma;
                }

                if (h <= 50.f && s >= 0.18f && s <= 0.65f && v >= 0.25f && v <= 0.97f && R > G && G > B) ++skin;
                if (y < skyRows && h >= 185.f && h <= 250.f && s > 0.12f && v > 0.45f) ++sky;
                if (h >= 60.f && h <= 170.f && s > 0.2f && v > 0.12f) ++foliage;
                if (s > 0.65f && v > 0.6f) ++neon;

                if (!lo && !hi)
                {
                    // Grey-world over everything unclipped that is not a light source's own colour:
                    // a neon sign says nothing about the colour of the light falling on the street.
                    if (s < 0.7f) world.add(lr, lg, lb, s);
                    if (s < 0.25f && L >= 0.1f && L <= 0.95f) neutral.add(lr, lg, lb, s);
                }
                const int z = L < 0.25f ? 0 : (L > 0.70f ? 2 : 1);
                zone[z].add(lr, lg, lb, s);
            }
        }

        const double n = (double)total;
        st.lumMean = (float)(lumSum / n);
        st.lumStdDev = (float)std::sqrt(std::max(0.0, lumSq / n - (lumSum / n) * (lumSum / n)));
        st.keyEv = (float)std::log2(std::exp(logSum / n) / 0.18);
        st.clipShadows = (float)(clipLo / n);
        st.clipHighlights = (float)(clipHi / n);
        st.meanSaturation = (float)(satSum / n);
        {
            const double mrg = rgSum / n, myb = ybSum / n;
            const double vrg = std::max(0.0, rgSq / n - mrg * mrg), vyb = std::max(0.0, ybSq / n - myb * myb);
            st.colourfulness = (float)(std::sqrt(vrg + vyb) + 0.3 * std::sqrt(mrg * mrg + myb * myb));
        }

        static const float kPct[7] = {0.01f, 0.05f, 0.25f, 0.50f, 0.75f, 0.95f, 0.99f};
        {
            long run = 0;
            int k = 0;
            for (int i = 0; i < kLumBins && k < 7; ++i)
            {
                run += lumHist[i];
                while (k < 7 && run >= (long)std::ceil(kPct[k] * n))
                    st.lumPct[k++] = (i + 0.5f) / kLumBins;
            }
            for (; k < 7; ++k) st.lumPct[k] = 1.f;
        }
        {
            const double lo = std::max(color::srgbDecodeExact((Pixel)st.lumPct[0]), (Pixel)(1.0 / 1024.0));
            const double hi = std::max(color::srgbDecodeExact((Pixel)st.lumPct[6]), (Pixel)(1.0 / 1024.0));
            st.rangeStops = (float)std::log2(hi / lo);
        }

        // Neutrals are the better witness of the light, but only when there are enough of them
        // to outvote noise — a saturated scene's few grey pixels are as likely to be a coloured
        // object's shadow as a white wall.
        st.neutralFraction = (float)(neutral.n / n);
        st.castFromNeutrals = st.neutralFraction >= 0.02f;
        const Acc &c = st.castFromNeutrals ? neutral : world;
        cast(c.r, c.g, c.b, st.castWarmth, st.castTint);

        for (int z = 0; z < 3; ++z)
        {
            ImageStats::Zone &o = st.zones[z];
            o.fraction = (float)(zone[z].n / n);
            if (zone[z].n == 0) continue;
            cast(zone[z].r, zone[z].g, zone[z].b, o.warmth, o.tint);
            o.saturation = (float)(zone[z].sat / zone[z].n);
        }

        double hueSum = 0;
        for (double w : hueW) hueSum += w;
        for (int i = 0; i < 12; ++i) st.hues[i] = hueSum > 0 ? (float)(hueW[i] / hueSum) : 0.f;
        st.colouredFraction = (float)(coloured / n);

        st.skinFraction = (float)(skin / n);
        st.skyFraction = (float)(sky / n);
        st.foliageFraction = (float)(foliage / n);
        st.neonFraction = (float)(neon / n);
        return st;
    }
}
