#include "ToneCurve.h"
#include "../base/ColorSpace.h"
#include <algorithm>

namespace arstro
{
    ToneCurve::ToneCurve()
    {
        mSmoothEnable = false;
        mPoints = {{0.0f, 0.0f}, {1.0f, 1.0f}};  // identity
        rebuildLut();
    }

    void ToneCurve::setPoints(const std::vector<std::pair<float, float>> &points)
    {
        mPoints = points;
        for (auto &p : mPoints)
        {
            p.first = p.first < 0 ? 0 : (p.first > 1 ? 1 : p.first);
            p.second = p.second < 0 ? 0 : (p.second > 1 ? 1 : p.second);
        }
        std::sort(mPoints.begin(), mPoints.end(),
                  [](const std::pair<float, float> &a, const std::pair<float, float> &b)
                  { return a.first < b.first; });
        if (mPoints.empty())
            mPoints = {{0.0f, 0.0f}, {1.0f, 1.0f}};
        rebuildLut();
    }

    void ToneCurve::setLogScale(bool log) { mLog = log; }

    void ToneCurve::rebuildLut()
    {
        // Evaluate the piecewise-linear curve through mPoints at each LUT entry.
        for (int i = 0; i < kLut; ++i)
        {
            const float x = (float)i / (kLut - 1);
            float y;
            if (x <= mPoints.front().first)
                y = mPoints.front().second;
            else if (x >= mPoints.back().first)
                y = mPoints.back().second;
            else
            {
                y = mPoints.back().second;
                for (size_t k = 1; k < mPoints.size(); ++k)
                {
                    if (x <= mPoints[k].first)
                    {
                        const float x0 = mPoints[k - 1].first, y0 = mPoints[k - 1].second;
                        const float x1 = mPoints[k].first, y1 = mPoints[k].second;
                        const float t = x1 > x0 ? (x - x0) / (x1 - x0) : 0.0f;
                        y = y0 + (y1 - y0) * t;
                        break;
                    }
                }
            }
            mLut[i] = (Pixel)y;
        }
    }

    Pixel ToneCurve::curveAt(Pixel d) const
    {
        if (d < 0) d = 0;
        if (d > 1) d = 1;
        const Pixel f = d * (Pixel)(kLut - 1);
        int i = (int)f;
        if (i >= kLut - 1) return mLut[kLut - 1];
        const Pixel frac = f - (Pixel)i;
        return mLut[i] * ((Pixel)1 - frac) + mLut[i + 1] * frac;
    }

    void ToneCurve::processPixel(const Pixel *in, Pixel *out, int channels)
    {
        const int colorCh = channels >= 3 ? 3 : channels;
        for (int c = 0; c < colorCh; ++c)
        {
            // map linear -> display domain, apply curve, map back to linear
            const Pixel d = mLog ? color::srgbEncode(in[c]) : in[c];
            const Pixel y = curveAt(d);
            out[c] = mLog ? color::srgbDecode(y) : y;
        }
        for (int c = colorCh; c < channels; ++c)
            out[c] = in[c];
    }
}
