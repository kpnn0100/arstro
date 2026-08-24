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

    void ColorMixer::refreshIdentity()
    {
        // Judged on the LUT, not on the point list: an "empty" curve and a curve whose
        // points are all at y = 0 are both identity and arrive by different routes.
        // Exact zero, not a tolerance — rebuild() writes literal 0.0f for a flat curve.
        for (int c = 0; c < 3; ++c)
            for (int i = 0; i < kLut; ++i)
                if (mLut[c][i] != 0.0f)
                {
                    mIdentity = false;
                    return;
                }
        mIdentity = true;
    }

    void ColorMixer::setCurve(Channel c, const std::vector<CurvePoint> &points)
    {
        // Flatten the bezier control points to a dense polyline (honouring smooth
        // handles + the cyclic wrap seam), then treat those as the LUT knots. The same
        // curve::sample the editor draws with, so render == on-screen curve.
        mPoints[c] = curve::sample(points, /*cyclic*/ true, 360.0f);
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
        rebuildLut(c);
        refreshIdentity();
    }

    void ColorMixer::rebuildLut(int c)
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

        // How much this pixel's hue can be trusted (see the header). Chroma back out of the
        // HSL pair — `s` is chroma divided by the lightness envelope, so multiplying it back
        // by that envelope is exact and costs no second min/max. A deep saturated shadow keeps
        // its weight this way, where gating on `s` alone would have let dark noise through.
        const float chroma = (float)s * (1.0f - std::fabs(2.0f * (float)l - 1.0f));
        float w = (chroma - kChromaFloor) / (kChromaFull - kChromaFloor);
        w = w < 0.0f ? 0.0f : (w > 1.0f ? 1.0f : w);
        w = w * w * (3.0f - 2.0f * w);   // smoothstep: no edge where the effect switches on

        const float yh = sampleCyclic(Hue, (float)h) * w;
        const float ys = sampleCyclic(Sat, (float)h) * w;
        const float yl = sampleCyclic(Lum, (float)h) * w;
        h += (Pixel)(yh * 180.0f);  // full ±180deg bend so any hue can reach any target
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
