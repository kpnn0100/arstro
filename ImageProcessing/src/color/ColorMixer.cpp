#include "ColorMixer.h"
#include "../base/ColorSpace.h"
#include <algorithm>
#include <cmath>

namespace arstro
{
    ColorMixer::ColorMixer() : PointProcessor(0)
    {
        mSmoothEnable = false;
        for (int c = 0; c < 3; ++c)
            rebuild(c);  // empty points -> flat 0 (identity)
    }

    void ColorMixer::setCurve(Channel c, const std::vector<std::pair<float, float>> &points)
    {
        mPoints[c] = points;
        for (auto &p : mPoints[c])
        {
            // wrap hue into [0,360), clamp y into [-1,1]
            float h = std::fmod(p.first, 360.0f);
            if (h < 0) h += 360.0f;
            p.first = h;
            p.second = p.second < -1 ? -1 : (p.second > 1 ? 1 : p.second);
        }
        std::sort(mPoints[c].begin(), mPoints[c].end(),
                  [](auto &a, auto &b) { return a.first < b.first; });
        rebuild(c);
    }

    void ColorMixer::rebuild(int c)
    {
        const auto &pts = mPoints[c];
        if (pts.empty())
        {
            for (int i = 0; i < kLut; ++i) mLut[c][i] = 0.0f;
            return;
        }
        if (pts.size() == 1)
        {
            for (int i = 0; i < kLut; ++i) mLut[c][i] = pts[0].second;
            return;
        }
        const int m = (int)pts.size();
        for (int i = 0; i < kLut; ++i)
        {
            const float hue = (float)i / kLut * 360.0f;
            float y;
            if (hue < pts[0].first || hue >= pts[m - 1].first)
            {
                // wrap segment: last point -> first point (across the 360/0 seam)
                const float h0 = pts[m - 1].first;
                const float h1 = pts[0].first + 360.0f;
                const float hq = hue < pts[0].first ? hue + 360.0f : hue;
                const float t = h1 > h0 ? (hq - h0) / (h1 - h0) : 0.0f;
                y = pts[m - 1].second + (pts[0].second - pts[m - 1].second) * t;
            }
            else
            {
                int k = 0;
                while (k < m - 1 && hue >= pts[k + 1].first) ++k;
                const float h0 = pts[k].first, h1 = pts[k + 1].first;
                const float t = h1 > h0 ? (hue - h0) / (h1 - h0) : 0.0f;
                y = pts[k].second + (pts[k + 1].second - pts[k].second) * t;
            }
            mLut[c][i] = y;
        }
    }

    float ColorMixer::sampleCyclic(int c, float hue) const
    {
        float f = hue / 360.0f * kLut;
        int i = (int)f;
        i = ((i % kLut) + kLut) % kLut;
        const int j = (i + 1) % kLut;
        const float frac = f - std::floor(f);
        return mLut[c][i] * (1.0f - frac) + mLut[c][j] * frac;
    }

    void ColorMixer::processPixel(const Pixel *in, Pixel *out, int channels)
    {
        const int colorCh = channels >= 3 ? 3 : channels;
        if (colorCh < 3)
        {
            for (int ch = 0; ch < channels; ++ch) out[ch] = in[ch];
            return;
        }
        Pixel h, s, l;
        color::rgbToHsl(in[0], in[1], in[2], h, s, l);
        const float yh = sampleCyclic(Hue, (float)h);
        const float ys = sampleCyclic(Sat, (float)h);
        const float yl = sampleCyclic(Lum, (float)h);
        h += (Pixel)(yh * 60.0f);
        s *= (Pixel)1 + (Pixel)ys;
        l += (Pixel)(yl * 0.5f);
        if (h < 0) h += 360;
        if (h >= 360) h -= 360;
        if (s < 0) s = 0; if (s > 1) s = 1;
        if (l < 0) l = 0; if (l > 1) l = 1;
        color::hslToRgb(h, s, l, out[0], out[1], out[2]);
        for (int ch = 3; ch < channels; ++ch) out[ch] = in[ch];
    }
}
